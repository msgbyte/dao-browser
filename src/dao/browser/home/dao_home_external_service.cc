// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_external_service.h"

#include <algorithm>
#include <utility>

#include "base/base64.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/process/process_handle.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/dialogs/browser_dialogs.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/url_constants.h"
#include "crypto/secure_util.h"
#include "dao/browser/home/dao_home_file_snapshot.h"
#include "dao/browser/home/dao_home_project_service.h"
#include "dao/browser/home/dao_home_project_service_factory.h"
#include "dao/browser/home/dao_home_sample_shape.h"
#include "dao/browser/local_ipc/dao_local_ipc_runtime_files.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/webui/dao_home_ui.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/dialog_model.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/views/widget/widget.h"
#include "url/url_constants.h"

namespace dao {
namespace {

DEFINE_LOCAL_ELEMENT_IDENTIFIER_VALUE(kHomeProfilePicker);

base::DictValue Ok() {
  return base::DictValue().Set("ok", true);
}

base::DictValue Error(std::string code) {
  return base::DictValue()
      .Set("ok", false)
      .Set("error", base::DictValue()
                        .Set("code", std::move(code))
                        .Set("message", l10n_util::GetStringUTF8(
                                            IDS_DAO_HOME_EXTERNAL_ERROR)));
}

base::DictValue Error(HomeError error) {
  switch (error) {
    case HomeError::kStaleRevision:
      return Error("STALE_REVISION");
    case HomeError::kInvalidPath:
      return Error("WORKSPACE_INVALID");
    case HomeError::kInvalidManifest:
      return Error("INVALID_MANIFEST");
    case HomeError::kQuotaExceeded:
      return Error("QUOTA_EXCEEDED");
    case HomeError::kPermissionRequired:
      return Error("SOURCE_PERMISSION_REQUIRED");
    case HomeError::kCancelled:
      return Error("HOME_NOT_ACTIVE");
    case HomeError::kAlreadyExists:
      return Error("HOME_BUSY");
    case HomeError::kNotFound:
      return Error("NOT_FOUND");
    default:
      return Error("INVALID_ARGUMENT");
  }
}

bool BoundedText(const std::string* value, size_t maximum) {
  return value && !value->empty() && value->size() <= maximum &&
         base::IsStringUTF8(*value) && value->find('\0') == std::string::npos;
}

bool IsHome(content::WebContents* contents) {
  return contents &&
         contents->GetLastCommittedURL().SchemeIs(content::kChromeUIScheme) &&
         contents->GetLastCommittedURL().host() == "home";
}

DaoHomeUI* GetHomeUI(content::WebContents* contents) {
  auto* ui = IsHome(contents) ? contents->GetWebUI() : nullptr;
  return ui && ui->GetController() ? ui->GetController()->GetAs<DaoHomeUI>()
                                   : nullptr;
}

base::DictValue VersionValue(const HomeVersion& version) {
  return base::DictValue()
      .Set("revision", version.id)
      .Set("parent_revision", version.parent)
      .Set("summary", version.summary)
      .Set("created_at_ms", static_cast<double>(version.created_at_ms));
}

std::u16string ProfileLabel(Profile* profile) {
  auto* entry = g_browser_process->profile_manager()
                    ->GetProfileAttributesStorage()
                    .GetProfileAttributesWithPath(profile->GetPath());
  return entry ? entry->GetName() : profile->GetBaseName().AsUTF16Unsafe();
}

}  // namespace

struct DaoHomeExternalService::Connection {
  bool authenticated = false;
  bool pending = false;
  std::string client;
  base::OneShotTimer timeout;
};

struct DaoHomeExternalService::Approval {
  uint64_t connection = 0;
  base::WeakPtr<Profile> profile;
  std::vector<base::WeakPtr<Profile>> choices;
  raw_ptr<ui::DialogModel> model = nullptr;
  std::string client;
  Reply reply;
  base::WeakPtr<views::Widget> widget;
  base::OneShotTimer timer;
};

struct DaoHomeExternalService::Session : content::WebContentsObserver,
                                         TabStripModelObserver {
  using content::WebContentsObserver::Observe;
  explicit Session(DaoHomeExternalService* owner) : service(owner) {}
  void WebContentsDestroyed() override { AbandonWindow(); }
  void PrimaryPageChanged(content::Page&) override {
    if (!owner_document_committed && IsHome(web_contents())) {
      owner_document_committed = true;
      return;
    }
    AbandonWindow();
  }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility == content::Visibility::HIDDEN && lease)
      service->InvalidatePreview(token);
  }
  void OnTabStripModelChanged(TabStripModel* model,
                              const TabStripModelChange&,
                              const TabStripSelectionChange&) override {
    if (model->count() != 1 || model->GetActiveWebContents() != web_contents())
      AbandonWindow();
  }
  void OnTabStripModelDestroyed(TabStripModel*) override { AbandonWindow(); }

  void AbandonWindow() {
    auto* contents = web_contents();
    service->InvalidatePreview(token);
    TabStripModelObserver::StopObservingAll(this);
    Observe(nullptr);
    window_id.reset();
    owner_document_committed = false;
    if (auto* ui = GetHomeUI(contents))
      ui->NotifyExternalSessionChanged();
  }

  raw_ptr<DaoHomeExternalService> service;
  std::optional<int> window_id;
  bool owner_document_committed = false;
  base::WeakPtr<Profile> profile;
  base::WeakPtr<DaoHomeProjectService> project;
  std::string token;
  std::string client;
  base::Time expires;
  base::OneShotTimer expiration;
  base::OneShotTimer poll;
  base::OneShotTimer preview_timeout;
  base::TimeTicks deadline;
  base::TimeTicks permission_deadline;
  std::string permission_id;
  std::optional<HomeDraft> draft;
  std::string digest;
  scoped_refptr<DaoHomeMutationLease> lease;
  scoped_refptr<DaoHomeMutationLease> authorization =
      base::MakeRefCounted<DaoHomeMutationLease>();
  std::optional<std::string> single_connector;
  size_t connector_index = 0;
  base::ListValue sources;
  std::string source_failure;
  bool awaiting_source = false;
  bool busy = false;
  bool preview_valid = false;
  bool published = false;
  uint64_t operation_connection = 0;
  Reply pending;
};

DaoHomeExternalService* DaoHomeExternalService::Get() {
  static base::NoDestructor<DaoHomeExternalService> instance;
  return instance.get();
}

DaoHomeExternalService::DaoHomeExternalService() = default;
DaoHomeExternalService::~DaoHomeExternalService() = default;

void DaoHomeExternalService::Initialize(base::FilePath user_data_dir) {
  if (running_)
    return;
  running_ = true;
  user_data_dir_ = std::move(user_data_dir);
  runtime_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
  runtime_files_ = base::MakeRefCounted<DaoLocalIpcRuntimeFiles>(
      user_data_dir_, base::GetCurrentProcId(), DaoLocalIpcConfig::Home());
  runtime_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoLocalIpcRuntimeFiles::Prepare, runtime_files_),
      base::BindOnce(&DaoHomeExternalService::OnRuntimePrepared,
                     weak_factory_.GetWeakPtr()));
}

void DaoHomeExternalService::OnRuntimePrepared(
    base::expected<void, DaoLocalIpcError> result) {
  if (!result.has_value()) {
    Shutdown();
    return;
  }
  const auto runner = base::SequencedTaskRunner::GetCurrentDefault();
  transport_.emplace(
      content::GetIOThreadTaskRunner({}),
      base::BindPostTask(
          runner, base::BindRepeating(&DaoHomeExternalService::OnAccepted,
                                      weak_factory_.GetWeakPtr())),
      base::BindPostTask(runner,
                         base::BindRepeating(&DaoHomeExternalService::OnRequest,
                                             weak_factory_.GetWeakPtr())),
      base::BindPostTask(
          runner, base::BindRepeating(&DaoHomeExternalService::OnDisconnected,
                                      weak_factory_.GetWeakPtr())),
      DaoLocalIpcConfig::Home());
  transport_.AsyncCall(&DaoLocalIpcTransport::StartListening)
      .WithArgs(runtime_files_->socket_path())
      .Then(base::BindOnce(&DaoHomeExternalService::OnListening,
                           weak_factory_.GetWeakPtr()));
}

void DaoHomeExternalService::OnListening(base::expected<uint16_t, int> result) {
  if (!result.has_value()) {
    Shutdown();
    return;
  }
  runtime_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<DaoLocalIpcRuntimeFiles> files,
             uint16_t port) -> base::expected<void, DaoLocalIpcError> {
            auto captured = files->CaptureBoundSocket(port);
            return captured.has_value() ? files->Publish() : captured;
          },
          runtime_files_, *result),
      base::BindOnce(&DaoHomeExternalService::OnRuntimePublished,
                     weak_factory_.GetWeakPtr()));
}

void DaoHomeExternalService::OnRuntimePublished(
    base::expected<void, DaoLocalIpcError> result) {
  if (result.has_value()) {
    transport_.AsyncCall(&DaoLocalIpcTransport::StartAccepting);
  } else {
    Shutdown();
  }
}

void DaoHomeExternalService::Shutdown() {
  while (!sessions_.empty())
    StopSession(sessions_.begin()->first, false);
  revoked_sessions_.clear();
  FinishApproval(false);
  weak_factory_.InvalidateWeakPtrs();
  if (!transport_.is_null()) {
    transport_.AsyncCall(&DaoLocalIpcTransport::Stop);
    transport_.Reset();
  }
  connections_.clear();
  if (runtime_files_ && runtime_runner_)
    runtime_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&DaoLocalIpcRuntimeFiles::Cleanup, runtime_files_));
  runtime_files_.reset();
  running_ = false;
}

void DaoHomeExternalService::OnAccepted(uint64_t connection,
                                        std::optional<base::ProcessId>) {
  if (connections_.size() >= 8) {
    transport_.AsyncCall(&DaoLocalIpcTransport::CloseConnection)
        .WithArgs(connection);
    return;
  }
  auto state = std::make_unique<Connection>();
  state->timeout.Start(
      FROM_HERE, base::Seconds(5),
      base::BindOnce(
          [](base::WeakPtr<DaoHomeExternalService> self, uint64_t id) {
            if (self && !self->transport_.is_null())
              self->transport_.AsyncCall(&DaoLocalIpcTransport::CloseConnection)
                  .WithArgs(id);
          },
          weak_factory_.GetWeakPtr(), connection));
  connections_.emplace(connection, std::move(state));
}

void DaoHomeExternalService::OnRequest(uint64_t connection,
                                       DaoLocalIpcRequest request,
                                       size_t wire_bytes) {
  transport_.AsyncCall(&DaoLocalIpcTransport::AcknowledgeRequest)
      .WithArgs(connection, wire_bytes);
  auto found = connections_.find(connection);
  if (found == connections_.end() || !request.id)
    return;
  Connection& state = *found->second;
  if (!state.authenticated) {
    const auto* nonce = request.params.FindString("nonce");
    const auto* client = request.params.FindStringByDottedPath("client.name");
    const auto* version =
        request.params.FindStringByDottedPath("client.version");
    if (request.method != "hello" || !nonce ||
        !crypto::SecureMemEqual(base::as_byte_span(*nonce),
                                base::as_byte_span(runtime_files_->nonce())) ||
        !BoundedText(client, 256) || !BoundedText(version, 128)) {
      SendReply(connection, *request.id, Error("AUTHORIZATION_DENIED"));
      transport_.AsyncCall(&DaoLocalIpcTransport::CloseAfterWrites)
          .WithArgs(connection);
      return;
    }
    state.authenticated = true;
    state.client = *client;
    state.timeout.Stop();
    SendReply(connection, *request.id, Ok().Set("protocol_version", 1));
    return;
  }
  if (state.pending) {
    transport_.AsyncCall(&DaoLocalIpcTransport::SendSuccess)
        .WithArgs(connection, *request.id, Error("HOME_BUSY"));
    return;
  }
  state.pending = true;
  Dispatch(connection, state.client, std::move(request.method),
           std::move(request.params),
           base::BindOnce(&DaoHomeExternalService::SendReply,
                          weak_factory_.GetWeakPtr(), connection, *request.id));
}

void DaoHomeExternalService::SendReply(uint64_t connection,
                                       std::string id,
                                       base::DictValue result) {
  const auto found = connections_.find(connection);
  if (found == connections_.end() || transport_.is_null())
    return;
  found->second->pending = false;
  transport_.AsyncCall(&DaoLocalIpcTransport::SendSuccess)
      .WithArgs(connection, std::move(id), std::move(result));
}

void DaoHomeExternalService::OnDisconnected(uint64_t connection) {
  connections_.erase(connection);
  if (approval_ && approval_->connection == connection)
    FinishApproval(false);
  for (auto& [token, session] : sessions_) {
    if (session->pending && session->operation_connection == connection)
      InvalidatePreview(token);
  }
}

DaoHomeExternalService::Session* DaoHomeExternalService::FindSession(
    const std::string& token) {
  auto found = sessions_.find(token);
  if (found == sessions_.end())
    return nullptr;
  Session* session = found->second.get();
  if (!session->profile || !session->project ||
      base::Time::Now() >= session->expires) {
    // Connector/preview cancellation can synchronously reenter this lookup.
    // Defer destruction so observers already unwinding on this session remain
    // alive until their callback has returned.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DaoHomeExternalService::StopSession,
                                  weak_factory_.GetWeakPtr(), token, false));
    return nullptr;
  }
  return session;
}

DaoHomeExternalService::Session* DaoHomeExternalService::FindOwner(
    content::WebContents* contents) const {
  if (!contents)
    return nullptr;
  for (const auto& [token, session] : sessions_)
    if (session->web_contents() == contents && session->window_id) {
      auto* browser =
          GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
      if (browser && browser->GetSessionID().id() == *session->window_id)
        return session.get();
    }
  return nullptr;
}

base::Value DaoHomeExternalService::GetContext(
    content::WebContents* contents) const {
  Session* session = FindOwner(contents);
  return session ? base::Value(base::DictValue()
                                   .Set("client", session->client)
                                   .Set("published", session->published))
                 : base::Value();
}

DaoHomeExternalService::Session* DaoHomeExternalService::FindEditor(
    Profile* profile) const {
  if (!profile || !profile->IsRegularProfile())
    return nullptr;
  for (const auto& [token, session] : sessions_) {
    if (session->profile.get() == profile && session->project &&
        base::Time::Now() < session->expires)
      return session.get();
  }
  return nullptr;
}

base::Value DaoHomeExternalService::GetEditor(Profile* profile) const {
  Session* session = FindEditor(profile);
  return session ? base::Value(base::DictValue().Set("client", session->client))
                 : base::Value();
}

void DaoHomeExternalService::StopForProfile(Profile* profile) {
  if (Session* session = FindEditor(profile))
    StopSession(session->token);
}

bool DaoHomeExternalService::CanAccessDraft(content::WebContents* contents,
                                            const std::string& draft_id) const {
  for (const auto& [token, session] : sessions_) {
    if (session->draft && session->draft->id == draft_id)
      return FindOwner(contents) == session.get() && session->profile &&
             base::Time::Now() < session->expires;
  }
  return !FindOwner(contents);
}

void DaoHomeExternalService::StopForContents(content::WebContents* contents) {
  if (Session* session = FindOwner(contents))
    StopSession(session->token);
}

std::string DaoHomeExternalService::SessionErrorCode(
    const std::string& token) const {
  auto found = revoked_sessions_.find(token);
  return found != revoked_sessions_.end() && found->second > base::Time::Now()
             ? "SESSION_REVOKED"
             : "SESSION_EXPIRED";
}

void DaoHomeExternalService::StopSession(const std::string& requested_token,
                                         bool revoked) {
  const std::string token = requested_token;
  auto found = sessions_.find(token);
  if (found == sessions_.end())
    return;
  Session& session = *found->second;
  const auto profile = session.profile;
  if (revoked)
    revoked_sessions_[token] = session.expires;
  session.authorization->Invalidate();
  base::WeakPtr<content::WebContents> contents =
      session.web_contents() ? session.web_contents()->GetWeakPtr() : nullptr;
  const auto window_id = session.window_id;
  InvalidatePreview(token);
  if (session.draft && session.project)
    session.project->DiscardDraft(
        session.draft->id,
        base::BindOnce([](base::expected<void, HomeError>) {}));
  session.Observe(nullptr);
  TabStripModelObserver::StopObservingAll(&session);
  sessions_.erase(found);
  NotifyEditors(profile.get());
  if (contents && IsHome(contents.get())) {
    auto* browser = GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
        contents.get());
    if (browser && window_id && browser->GetSessionID().id() == *window_id &&
        browser->GetTabStripModel()->count() == 1)
      browser->GetWindow()->Close();
    else if (auto* ui = GetHomeUI(contents.get()))
      ui->NotifyExternalSessionChanged();
  }
}

void DaoHomeExternalService::OpenSession(uint64_t connection,
                                         std::string client,
                                         base::DictValue params,
                                         Reply reply) {
  const auto* reason = params.FindString("reason");
  if (!BoundedText(reason, 1024)) {
    std::move(reply).Run(Error("INVALID_ARGUMENT"));
    return;
  }
  if (approval_) {
    std::move(reply).Run(Error("HOME_BUSY"));
    return;
  }
  std::erase_if(revoked_sessions_, [](const auto& item) {
    return item.second <= base::Time::Now();
  });
  if (sessions_.size() + revoked_sessions_.size() >= 256) {
    std::move(reply).Run(Error("HOME_BUSY"));
    return;
  }
  Profile* profile = nullptr;
  std::vector<base::WeakPtr<Profile>> choices;
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  if (const auto* id = params.FindString("profile")) {
    for (Profile* candidate :
         g_browser_process->profile_manager()->GetLoadedProfiles())
      if (candidate->GetBaseName().AsUTF8Unsafe() == *id)
        profile = candidate;
  } else {
    for (Profile* candidate :
         g_browser_process->profile_manager()->GetLoadedProfiles()) {
      if (candidate->IsRegularProfile())
        choices.push_back(candidate->GetWeakPtr());
    }
    if (!choices.empty() &&
        std::ranges::all_of(choices, [this](const auto& candidate) {
          return FindEditor(candidate.get()) != nullptr;
        })) {
      std::move(reply).Run(Error("HOME_BUSY"));
      return;
    }
    std::erase_if(choices, [this](const auto& candidate) {
      return FindEditor(candidate.get()) != nullptr;
    });
    if (!choices.empty())
      profile = choices.front().get();
  }
  if (!profile || !profile->IsRegularProfile()) {
    std::move(reply).Run(Error("PROFILE_REQUIRED"));
    return;
  }
  if (FindEditor(profile)) {
    std::move(reply).Run(Error("HOME_BUSY"));
    return;
  }
  if (!browser || browser->GetProfile() != profile ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    browser = CreateBrowserWindow(
        BrowserWindowCreateParams(*profile, /*from_user_gesture=*/false));
    if (browser) {
      NavigateParams navigation(browser, GURL(url::kAboutBlankURL),
                                ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
      navigation.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
      Navigate(&navigation);
    }
  }
  if (!browser) {
    std::move(reply).Run(Error("BROWSER_UNAVAILABLE"));
    return;
  }
  approval_ = std::make_unique<Approval>();
  approval_->connection = connection;
  approval_->profile = profile->GetWeakPtr();
  approval_->choices = std::move(choices);
  approval_->client = std::move(client);
  approval_->reply = std::move(reply);
  const uint64_t generation = ++approval_generation_;
  if (approval_for_testing_) {
    FinishApproval(approval_for_testing_.Run(profile));
    return;
  }
  approval_->timer.Start(
      FROM_HERE, base::Seconds(60),
      base::BindOnce(&DaoHomeExternalService::FinishApprovalIfCurrent,
                     weak_factory_.GetWeakPtr(), generation, false));
  auto model =
      ui::DialogModel::Builder()
          .SetTitle(
              l10n_util::GetStringUTF16(IDS_DAO_HOME_EXTERNAL_APPROVAL_TITLE))
          .AddParagraph(ui::DialogModelLabel(
              base::UTF8ToUTF16(approval_->client + "\n" + *reason)))
          .AddParagraph(ui::DialogModelLabel(
              l10n_util::GetStringUTF16(IDS_DAO_HOME_EXTERNAL_APPROVAL_SCOPE)))
          .AddOkButton(
              base::BindOnce(&DaoHomeExternalService::FinishApprovalIfCurrent,
                             weak_factory_.GetWeakPtr(), generation, true),
              ui::DialogModel::Button::Params().SetLabel(
                  l10n_util::GetStringUTF16(IDS_DAO_HOME_EXTERNAL_ALLOW)))
          .AddCancelButton(
              base::BindOnce(&DaoHomeExternalService::FinishApprovalIfCurrent,
                             weak_factory_.GetWeakPtr(), generation, false),
              ui::DialogModel::Button::Params().SetLabel(
                  l10n_util::GetStringUTF16(IDS_DAO_HOME_EXTERNAL_DENY)))
          .SetCloseActionCallback(
              base::BindOnce(&DaoHomeExternalService::FinishApprovalIfCurrent,
                             weak_factory_.GetWeakPtr(), generation, false))
          .Build();
  if (approval_->choices.size() > 1) {
    std::vector<ui::SimpleComboboxModel::Item> items;
    for (const auto& choice : approval_->choices)
      items.emplace_back(ProfileLabel(choice.get()));
    model->AddCombobox(
        kHomeProfilePicker,
        l10n_util::GetStringUTF16(IDS_DAO_HOME_EXTERNAL_PROFILE),
        std::make_unique<ui::SimpleComboboxModel>(std::move(items)));
  } else {
    model->AddParagraph(
        ui::DialogModelLabel(ProfileLabel(profile)),
        l10n_util::GetStringUTF16(IDS_DAO_HOME_EXTERNAL_PROFILE));
  }
  approval_->model = model.get();
  browser->GetWindow()->Show();
  browser->GetWindow()->Activate();
  auto* widget = chrome::ShowBrowserModal(browser, std::move(model));
  if (approval_ && widget)
    approval_->widget = widget->GetWeakPtr();
}

void DaoHomeExternalService::FinishApprovalIfCurrent(uint64_t generation,
                                                     bool allowed) {
  if (generation == approval_generation_)
    FinishApproval(allowed);
}

void DaoHomeExternalService::FinishApproval(bool allowed) {
  if (!approval_)
    return;
  auto approval = std::move(approval_);
  if (allowed && approval->model && approval->choices.size() > 1) {
    const auto index =
        approval->model->GetComboboxByUniqueId(kHomeProfilePicker)
            ->selected_index();
    approval->profile =
        index < approval->choices.size() ? approval->choices[index] : nullptr;
  }
  approval->model = nullptr;
  if (approval->widget)
    approval->widget->Close();
  if (!allowed || !approval->profile) {
    std::move(approval->reply).Run(Error("AUTHORIZATION_DENIED"));
    return;
  }
  for (const auto& [token, session] : sessions_) {
    if (session->profile.get() == approval->profile.get() &&
        session->expires > base::Time::Now()) {
      std::move(approval->reply).Run(Error("HOME_BUSY"));
      return;
    }
  }
  auto session = std::make_unique<Session>(this);
  session->profile = approval->profile;
  session->project =
      DaoHomeProjectServiceFactory::GetForProfile(session->profile.get())
          ->GetWeakPtr();
  session->token = base::HexEncode(base::RandBytesAsString(32));
  session->client = approval->client;
  session->expires = base::Time::Now() + base::Hours(1);
  const auto token = session->token;
  session->expiration.Start(
      FROM_HERE, base::Hours(1),
      base::BindOnce(&DaoHomeExternalService::StopSession,
                     weak_factory_.GetWeakPtr(), token, false));
  auto result =
      Ok().Set("token", token)
          .Set("profile_id", session->profile->GetBaseName().AsUTF8Unsafe())
          .Set("expires_at_ms",
               session->expires.InMillisecondsFSinceUnixEpoch());
  sessions_.emplace(token, std::move(session));
  NotifyEditors(approval->profile.get());
  std::move(approval->reply).Run(std::move(result));
}

void DaoHomeExternalService::SetApprovalForTesting(
    ApprovalForTesting approval) {
  approval_for_testing_ = std::move(approval);
}

void DaoHomeExternalService::ExecuteForTesting(std::string method,
                                               base::DictValue params,
                                               Reply reply) {
  Dispatch(0, "Home test", std::move(method), std::move(params),
           std::move(reply));
}

void DaoHomeExternalService::Dispatch(uint64_t connection,
                                      std::string client,
                                      std::string method,
                                      base::DictValue params,
                                      Reply reply) {
  if (method == "session.open") {
    OpenSession(connection, std::move(client), std::move(params),
                std::move(reply));
    return;
  }
  const auto* token = params.FindString("token");
  Session* session = token ? FindSession(*token) : nullptr;
  if (!session || session->client != client) {
    std::move(reply).Run(
        Error(token ? SessionErrorCode(*token) : "SESSION_EXPIRED"));
    return;
  }
  if (method == "session.close") {
    StopSession(*token);
    std::move(reply).Run(Ok());
    return;
  }
  if (method == "session.status") {
    GetStatus(*session, std::move(params), std::move(reply));
    return;
  }
  if (method == "project.snapshot") {
    session->project->GetSnapshot(base::BindOnce(
        [](base::WeakPtr<DaoHomeExternalService> self, std::string token,
           Reply reply, HomeSnapshot snapshot) {
          Session* session = self ? self->FindSession(token) : nullptr;
          if (!session) {
            std::move(reply).Run(Error(self ? self->SessionErrorCode(token)
                                            : "SESSION_EXPIRED"));
            return;
          }
          session->project->GetFileSnapshot(
              snapshot.revision,
              base::BindOnce(
                  [](base::WeakPtr<DaoHomeExternalService> self,
                     std::string token, Reply reply, std::string revision,
                     base::expected<HomeFileSnapshot, HomeError> files) {
                    if (!self || !self->FindSession(token)) {
                      std::move(reply).Run(
                          Error(self ? self->SessionErrorCode(token)
                                     : "SESSION_EXPIRED"));
                      return;
                    }
                    if (!files.has_value()) {
                      std::move(reply).Run(Error(files.error()));
                      return;
                    }
                    std::move(reply).Run(
                        Ok().Set("revision", revision)
                            .Set("digest", HomeFileSnapshotDigest(*files))
                            .Set("files", EncodeHomeFileSnapshot(*files)));
                  },
                  self, token, std::move(reply), snapshot.revision));
        },
        weak_factory_.GetWeakPtr(), *token, std::move(reply)));
    return;
  }
  if (method == "project.versions") {
    session->project->ListVersions(base::BindOnce(
        [](base::WeakPtr<DaoHomeExternalService> self, std::string token,
           Reply reply, std::vector<HomeVersion> versions) {
          if (!self || !self->FindSession(token)) {
            std::move(reply).Run(Error(self ? self->SessionErrorCode(token)
                                            : "SESSION_EXPIRED"));
            return;
          }
          base::ListValue list;
          for (const auto& version : versions)
            list.Append(VersionValue(version));
          std::move(reply).Run(Ok().Set("versions", std::move(list)));
        },
        weak_factory_.GetWeakPtr(), *token, std::move(reply)));
    return;
  }
  if (method == "project.diagnostics") {
    base::ListValue list;
    for (const auto& diagnostic : session->project->GetDiagnostics(
             session->draft ? session->draft->id : "")) {
      list.Append(base::DictValue()
                      .Set("stage", diagnostic.stage)
                      .Set("code", diagnostic.code)
                      .Set("connector_id", diagnostic.connector_id));
    }
    std::move(reply).Run(Ok().Set("diagnostics", std::move(list)));
    return;
  }
  if (method == "draft.sources") {
    const auto* draft_id = params.FindString("draft_id");
    if (!session->draft || !draft_id || session->draft->id != *draft_id) {
      std::move(reply).Run(Error("PREVIEW_REQUIRED"));
      return;
    }
    base::ListValue list;
    if (session->draft) {
      for (const auto& source : session->draft->manifest.connectors) {
        auto item =
            base::DictValue()
                .Set("id", source.id)
                .Set("status", "not_tested")
                .Set("approved", session->project->IsDraftConnectorApproved(
                                     session->draft->id, source.id));
        for (const auto& tested : session->sources) {
          if (tested.GetDict().FindString("id") &&
              *tested.GetDict().FindString("id") == source.id) {
            item.Merge(tested.GetDict().Clone());
            break;
          }
        }
        list.Append(std::move(item));
      }
    }
    std::move(reply).Run(Ok().Set("sources", std::move(list)));
    return;
  }
  if (method == "preview.screenshot") {
    Screenshot(*session, std::move(reply));
    return;
  }
  if (session->busy) {
    std::move(reply).Run(Error("HOME_BUSY"));
    return;
  }
  session->busy = true;
  session->operation_connection = connection;
  reply = base::BindOnce(
      [](base::WeakPtr<DaoHomeExternalService> self, std::string token,
         Reply reply, base::DictValue result) {
        if (self) {
          const auto found = self->sessions_.find(token);
          if (found != self->sessions_.end())
            found->second->busy = false;
        }
        std::move(reply).Run(std::move(result));
      },
      weak_factory_.GetWeakPtr(), *token, std::move(reply));
  if (method == "draft.stage") {
    Stage(*session, std::move(params), std::move(reply));
    return;
  }
  if (method == "draft.preview" || method == "draft.test_source") {
    const auto* id = params.FindString("draft_id");
    const auto* source = params.FindString("connector_id");
    if (!id || (method == "draft.test_source" && !source)) {
      std::move(reply).Run(Error("INVALID_ARGUMENT"));
      return;
    }
    StartPreview(*session, *id, std::move(reply),
                 source ? std::optional<std::string>(*source) : std::nullopt);
    return;
  }
  if (method == "draft.publish") {
    Publish(*session, std::move(params), std::move(reply));
    return;
  }
  if (method == "project.rollback") {
    Rollback(*session, std::move(params), std::move(reply));
    return;
  }
  std::move(reply).Run(Error("INVALID_ARGUMENT"));
}

void DaoHomeExternalService::GetStatus(Session& session,
                                       base::DictValue params,
                                       Reply reply) {
  session.project->GetSnapshot(base::BindOnce(
      [](base::WeakPtr<DaoHomeExternalService> self, std::string token,
         base::DictValue params, Reply reply, HomeSnapshot snapshot) {
        Session* session = self ? self->FindSession(token) : nullptr;
        if (!session) {
          std::move(reply).Run(
              Error(self ? self->SessionErrorCode(token) : "SESSION_EXPIRED"));
          return;
        }
        auto* ui = GetHomeUI(session->web_contents());
        auto result =
            Ok().Set("current_revision", snapshot.revision)
                .Set("profile_id",
                     session->profile->GetBaseName().AsUTF8Unsafe())
                .Set("expires_at_ms",
                     session->expires.InMillisecondsFSinceUnixEpoch())
                .Set("preview_valid",
                     session->preview_valid && session->lease &&
                         session->lease->IsValid() && session->draft && ui &&
                         ui->IsExternalPreviewValid(session->draft->id));
        if (session->draft)
          result.Set("draft_id", session->draft->id);
        if (session->web_contents()) {
          auto* browser =
              GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
                  session->web_contents());
          if (browser)
            result.Set("preview_window_id",
                       base::NumberToString(browser->GetSessionID().id()));
        }
        const auto* operation_id = params.FindString("operation_id");
        if (!operation_id) {
          std::move(reply).Run(std::move(result));
          return;
        }
        session->project->GetOperation(
            *operation_id,
            base::BindOnce(
                [](base::WeakPtr<DaoHomeExternalService> self,
                   std::string token, Reply reply, base::DictValue result,
                   std::string operation_id,
                   std::optional<HomeOperationResult> operation) {
                  if (!self || !self->FindSession(token)) {
                    std::move(reply).Run(
                        Error(self ? self->SessionErrorCode(token)
                                   : "SESSION_EXPIRED"));
                    return;
                  }
                  auto value =
                      base::DictValue()
                          .Set("operation_id", operation_id)
                          .Set("status", operation ? "committed" : "not_found");
                  if (operation) {
                    value.Set("revision", operation->version.id);
                    value.Set("digest", operation->request.digest);
                  }
                  result.Set("operation", std::move(value));
                  std::move(reply).Run(std::move(result));
                },
                self, token, std::move(reply), std::move(result),
                *operation_id));
      },
      weak_factory_.GetWeakPtr(), session.token, std::move(params),
      std::move(reply)));
}

void DaoHomeExternalService::Stage(Session& session,
                                   base::DictValue params,
                                   Reply reply) {
  const auto* base = params.FindString("base_revision");
  const auto* files = params.FindDict("files");
  const auto* digest = params.FindString("digest");
  const auto* summary = params.FindString("summary");
  if (!base || !files || !digest ||
      (summary && !BoundedText(summary, kMaxHomeSummaryBytes))) {
    std::move(reply).Run(Error("INVALID_ARGUMENT"));
    return;
  }
  auto snapshot = DecodeHomeFileSnapshot(*files);
  if (!snapshot.has_value()) {
    std::move(reply).Run(Error(snapshot.error()));
    return;
  }
  if (HomeFileSnapshotDigest(*snapshot) != *digest) {
    std::move(reply).Run(Error("FILES_CHANGED"));
    return;
  }
  InvalidatePreview(session.token);
  if (session.draft)
    session.project->DiscardDraft(
        session.draft->id,
        base::BindOnce([](base::expected<void, HomeError>) {}));
  session.draft.reset();
  session.sources.clear();
  session.project->StageSnapshot(
      *base, *snapshot,
      summary ? *summary
              : l10n_util::GetStringUTF8(IDS_DAO_HOME_EXTERNAL_PREVIEW_TITLE),
      base::BindOnce(
          [](base::WeakPtr<DaoHomeExternalService> self,
             base::WeakPtr<DaoHomeProjectService> project, std::string token,
             std::string digest, Reply reply,
             base::expected<HomeDraft, HomeError> result) {
            Session* session = self ? self->FindSession(token) : nullptr;
            if (!session) {
              if (project && result.has_value())
                project->DiscardDraft(
                    result->id,
                    base::BindOnce([](base::expected<void, HomeError>) {}));
              std::move(reply).Run(Error(self ? self->SessionErrorCode(token)
                                              : "SESSION_EXPIRED"));
              return;
            }
            if (!result.has_value()) {
              std::move(reply).Run(Error(result.error()));
              return;
            }
            session->draft = std::move(*result);
            session->digest = digest;
            session->published = false;
            std::move(reply).Run(
                Ok().Set("draft_id", session->draft->id)
                    .Set("base_revision", session->draft->base_revision)
                    .Set("digest", digest));
          },
          weak_factory_.GetWeakPtr(), session.project, session.token, *digest,
          std::move(reply)));
}

bool DaoHomeExternalService::EnsurePreviewWindow(Session& session) {
  if (auto* contents = session.web_contents()) {
    auto* browser =
        GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
    if (IsHome(contents) && browser && session.window_id &&
        browser->GetSessionID().id() == *session.window_id &&
        browser->GetTabStripModel()->count() == 1 &&
        browser->GetTabStripModel()->GetActiveWebContents() == contents) {
      browser->GetWindow()->Show();
      return true;
    }
    session.AbandonWindow();
  }
  if (!session.profile ||
      GetBrowserWindowCreationStatusForProfile(*session.profile) !=
          BrowserWindowInterface::CreationStatus::kOk)
    return false;
  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_NORMAL,
                                   *session.profile, false);
  params.omit_from_session_restore = true;
  params.should_trigger_session_restore = false;
  auto* browser = CreateBrowserWindow(std::move(params));
  if (!browser)
    return false;
  NavigateParams navigation(
      browser, GURL(std::string(content::kChromeUIScheme) + "://home/"),
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  navigation.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&navigation);
  content::WebContents* contents =
      browser->GetTabStripModel()->GetActiveWebContents();
  if (!contents) {
    browser->GetWindow()->Close();
    return false;
  }
  session.Observe(contents);
  session.window_id = browser->GetSessionID().id();
  session.owner_document_committed = false;
  browser->GetTabStripModel()->AddObserver(&session);
  browser->GetWindow()->Show();
  return true;
}

void DaoHomeExternalService::StartPreview(
    Session& session,
    std::string draft_id,
    Reply reply,
    std::optional<std::string> connector_id) {
  if (!session.draft || session.draft->id != draft_id) {
    std::move(reply).Run(Error("PREVIEW_REQUIRED"));
    return;
  }
  InvalidatePreview(session.token);
  if (!EnsurePreviewWindow(session)) {
    std::move(reply).Run(Error("BROWSER_UNAVAILABLE"));
    return;
  }
  session.pending = std::move(reply);
  session.single_connector = std::move(connector_id);
  session.sources.clear();
  session.source_failure.clear();
  session.connector_index = 0;
  session.deadline = base::TimeTicks::Now() + base::Seconds(170);
  session.preview_timeout.Start(
      FROM_HERE, base::Seconds(170),
      base::BindOnce(
          [](base::WeakPtr<DaoHomeExternalService> self, std::string token) {
            Session* session = self ? self->FindSession(token) : nullptr;
            if (!session || !session->pending)
              return;
            self->FinishPreview(*session, Error("PREVIEW_FAILED"));
            self->InvalidatePreview(token);
          },
          weak_factory_.GetWeakPtr(), session.token));
  session.project->GetDraft(
      draft_id, base::BindOnce(&DaoHomeExternalService::OnDraftLoaded,
                               weak_factory_.GetWeakPtr(), session.token));
}

void DaoHomeExternalService::OnDraftLoaded(
    std::string token,
    base::expected<HomeDraft, HomeError> result) {
  Session* session = FindSession(token);
  if (!session || !session->pending)
    return;
  if (!result.has_value()) {
    FinishPreview(*session, Error(result.error()));
    return;
  }
  session->draft = std::move(*result);
  session->project->AuthorizeGrantedDraftConnectors(
      session->draft->id,
      base::BindOnce(
          [](base::WeakPtr<DaoHomeExternalService> self, std::string token,
             base::expected<void, HomeError> authorized) {
            Session* session = self ? self->FindSession(token) : nullptr;
            if (!session || !session->pending)
              return;
            if (!authorized.has_value()) {
              self->FinishPreview(*session, Error(authorized.error()));
              return;
            }
            self->ContinuePreview(token);
          },
          weak_factory_.GetWeakPtr(), token));
}

void DaoHomeExternalService::ContinuePreview(std::string token) {
  Session* session = FindSession(token);
  if (!session || !session->pending || session->awaiting_source)
    return;
  if (base::TimeTicks::Now() >= session->deadline) {
    FinishPreview(*session, Error("PREVIEW_FAILED"));
    return;
  }
  auto* ui = GetHomeUI(session->web_contents());
  if (!ui || !ui->IsReadyForExternal()) {
    session->poll.Start(FROM_HERE, base::Milliseconds(100),
                        base::BindOnce(&DaoHomeExternalService::ContinuePreview,
                                       weak_factory_.GetWeakPtr(), token));
    return;
  }
  if (!session->lease)
    session->lease = ui->CreateMutationLease();
  if (!session->lease || !session->lease->IsValid()) {
    FinishPreview(*session, Error("HOME_NOT_ACTIVE"));
    return;
  }
  const auto& connectors = session->draft->manifest.connectors;
  while (session->connector_index < connectors.size()) {
    const auto& connector = connectors[session->connector_index];
    if (session->single_connector &&
        connector.id != *session->single_connector) {
      ++session->connector_index;
      continue;
    }
    if (!session->project->IsDraftConnectorApproved(session->draft->id,
                                                    connector.id)) {
      if (session->permission_id.empty()) {
        auto permission =
            session->project->RequestPermission(*session->draft, connector.id);
        if (!permission.has_value()) {
          FinishPreview(*session, Error(permission.error()));
          return;
        }
        session->permission_id = permission->id;
        session->permission_deadline =
            base::TimeTicks::Now() + base::Seconds(60);
      } else if (!session->project->GetPendingPermission() ||
                 base::TimeTicks::Now() >= session->permission_deadline) {
        FinishPreview(*session, Error("SOURCE_PERMISSION_REQUIRED"));
        return;
      }
      session->poll.Start(
          FROM_HERE, base::Milliseconds(100),
          base::BindOnce(&DaoHomeExternalService::ContinuePreview,
                         weak_factory_.GetWeakPtr(), token));
      return;
    }
    session->permission_id.clear();
    session->awaiting_source = true;
    ui->CollectConnectorForAgent(
        session->draft->id, connector.id, base::Value(base::DictValue()),
        base::BindOnce(&DaoHomeExternalService::OnSourceTested,
                       weak_factory_.GetWeakPtr(), token, connector.id));
    return;
  }
  if (session->single_connector) {
    FinishPreview(
        *session,
        session->sources.empty()
            ? Error("NOT_FOUND")
            : (session->source_failure.empty() ? Ok()
                                               : Error(session->source_failure))
                  .Set("sources", session->sources.Clone()));
    return;
  }
  ui->PreviewDraftForExternal(
      session->draft->id, session->draft->manifest.entry,
      base::BindOnce(&DaoHomeExternalService::OnPreviewed,
                     weak_factory_.GetWeakPtr(), token));
}

void DaoHomeExternalService::OnSourceTested(std::string token,
                                            std::string connector_id,
                                            base::Value result) {
  Session* session = FindSession(token);
  if (!session || !session->pending)
    return;
  session->awaiting_source = false;
  if (!session->lease || !session->lease->IsValid()) {
    FinishPreview(*session, Error("HOME_NOT_ACTIVE"));
    return;
  }
  const auto* value = result.GetIfDict();
  if (!value || !value->FindBool("ok").value_or(false) ||
      !value->Find("result")) {
    const auto* code = value ? value->FindString("code") : nullptr;
    const bool login = code && (*code == "auth_required" ||
                                *code == "authentication_required" ||
                                *code == "login_required");
    const bool required =
        session->single_connector.has_value() ||
        std::ranges::contains(session->draft->test_required_connector_ids,
                              connector_id);
    session->sources.Append(
        base::DictValue()
            .Set("id", connector_id)
            .Set("status", login ? "auth_required" : "failed")
            .Set("code", code ? *code : "invalid_response")
            .Set("required", required)
            .Set("warning", !required));
    if (required && session->source_failure.empty())
      session->source_failure =
          login ? "SOURCE_AUTH_REQUIRED" : "PREVIEW_FAILED";
    ++session->connector_index;
    ContinuePreview(token);
    return;
  }
  auto tested = session->project->MarkDraftConnectorTested(session->draft->id,
                                                           connector_id);
  if (!tested.has_value()) {
    FinishPreview(*session, Error(tested.error()));
    return;
  }
  const auto& sample = *value->Find("result");
  session->sources.Append(
      base::DictValue()
          .Set("id", connector_id)
          .Set("status", "passed")
          .Set("sample_shape", BuildHomeConnectorSampleShape(sample)));
  ++session->connector_index;
  ContinuePreview(token);
}

void DaoHomeExternalService::OnPreviewed(std::string token,
                                         base::Value result) {
  Session* session = FindSession(token);
  if (!session || !session->pending)
    return;
  const auto* value = result.GetIfDict();
  if (!value || !value->FindBool("valid").value_or(false) || !session->lease ||
      !session->lease->IsValid()) {
    FinishPreview(*session,
                  Error("PREVIEW_FAILED")
                      .Set("publishable", false)
                      .Set("render", base::DictValue().Set("status", "failed"))
                      .Set("sources", session->sources.Clone()));
    return;
  }
  session->preview_valid = session->source_failure.empty();
  auto* browser = GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
      session->web_contents());
  FinishPreview(
      *session,
      (session->preview_valid ? Ok() : Error(session->source_failure))
          .Set("draft_id", session->draft->id)
          .Set("base_revision", session->draft->base_revision)
          .Set("content_digest", session->digest)
          .Set("publishable", session->preview_valid)
          .Set("render", base::DictValue().Set("status", "passed"))
          .Set("sources", session->sources.Clone())
          .Set("preview_window_id",
               browser ? base::NumberToString(browser->GetSessionID().id())
                       : ""));
}

void DaoHomeExternalService::FinishPreview(Session& session,
                                           base::DictValue result) {
  session.preview_timeout.Stop();
  session.poll.Stop();
  session.awaiting_source = false;
  if (!session.permission_id.empty() && session.project) {
    session.project->CancelPermission(session.permission_id);
    session.permission_id.clear();
  }
  Reply reply = std::move(session.pending);
  if (!result.FindBool("ok").value_or(false)) {
    session.preview_valid = false;
    if (session.lease)
      session.lease->Invalidate();
  }
  if (reply)
    std::move(reply).Run(std::move(result));
}

void DaoHomeExternalService::InvalidatePreview(const std::string& token) {
  auto found = sessions_.find(token);
  if (found == sessions_.end())
    return;
  Session& session = *found->second;
  if (session.lease)
    session.lease->Invalidate();
  session.lease.reset();
  session.preview_valid = false;
  if (session.draft && session.project)
    session.project->InvalidateDraftPreview(session.draft->id);
  FinishPreview(session, Error(SessionErrorCode(token) == "SESSION_REVOKED"
                                   ? "SESSION_REVOKED"
                                   : "HOME_NOT_ACTIVE"));
  if (auto* ui = GetHomeUI(session.web_contents()))
    ui->CancelAgentSession();
}

void DaoHomeExternalService::NotifyOwner(Session& session) {
  if (auto* ui = GetHomeUI(session.web_contents()))
    ui->NotifyExternalSessionChanged();
}

void DaoHomeExternalService::NotifyEditors(Profile* profile) {
  if (!profile)
    return;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [profile](BrowserWindowInterface* browser) {
        if (browser->GetProfile() != profile)
          return true;
        auto* tabs = browser->GetTabStripModel();
        if (tabs) {
          for (int i = 0; i < tabs->count(); ++i) {
            if (auto* ui = GetHomeUI(tabs->GetWebContentsAt(i)))
              ui->NotifyExternalEditorChanged();
          }
        }
        return true;
      });
}

void DaoHomeExternalService::Publish(Session& session,
                                     base::DictValue params,
                                     Reply reply) {
  const auto* draft = params.FindString("draft_id");
  const auto* base = params.FindString("base_revision");
  const auto* digest = params.FindString("digest");
  const auto* id = params.FindString("operation_id");
  const auto* summary = params.FindString("summary");
  if (!BoundedText(draft, 128) || !base || !BoundedText(digest, 64) ||
      !BoundedText(id, 128) || !BoundedText(summary, kMaxHomeSummaryBytes)) {
    std::move(reply).Run(Error("INVALID_ARGUMENT"));
    return;
  }
  HomeOperationRequest operation{.operation_id = *id,
                                 .method = "draft.publish",
                                 .base_revision = *base,
                                 .draft_id = *draft,
                                 .digest = *digest,
                                 .summary = *summary};
  // Provenance is checked before the live receipt: a successful commit may
  // already have consumed the draft or outlived its original browser process.
  session.project->GetOperation(
      operation.operation_id,
      base::BindOnce(
          [](base::WeakPtr<DaoHomeExternalService> self, std::string token,
             HomeOperationRequest operation, Reply reply,
             std::optional<HomeOperationResult> previous) {
            Session* session = self ? self->FindSession(token) : nullptr;
            if (!session) {
              std::move(reply).Run(Error(self ? self->SessionErrorCode(token)
                                              : "SESSION_EXPIRED"));
              return;
            }
            if (previous) {
              std::move(reply).Run(
                  previous->request == operation
                      ? Ok().Set("revision", previous->version.id)
                            .Set("operation_id", operation.operation_id)
                            .Set("digest", operation.digest)
                      : Error("INVALID_ARGUMENT"));
              return;
            }
            auto* ui = GetHomeUI(session->web_contents());
            if (!session->draft || session->draft->id != operation.draft_id ||
                !session->preview_valid || !session->lease ||
                !session->lease->IsValid() || !ui ||
                !ui->IsExternalPreviewValid(operation.draft_id)) {
              std::move(reply).Run(Error("PREVIEW_REQUIRED"));
              return;
            }
            if (session->draft->base_revision != operation.base_revision ||
                session->digest != operation.digest) {
              std::move(reply).Run(Error("FILES_CHANGED"));
              return;
            }
            session->project->PublishApprovedDraft(
                *session->draft, HomeRevisionKind::kUserRequest,
                base::BindOnce(
                    [](base::WeakPtr<DaoHomeExternalService> self,
                       std::string token, HomeOperationRequest operation,
                       Reply reply,
                       base::expected<HomeVersion, HomeError> result) {
                      if (!result.has_value()) {
                        std::move(reply).Run(Error(result.error()));
                        return;
                      }
                      // The store's commit guard decided the result. A
                      // subsequent navigation or disconnect must not turn it
                      // into a failure.
                      if (self) {
                        if (Session* session = self->FindSession(token)) {
                          self->InvalidatePreview(token);
                          session->draft.reset();
                          session->published = true;
                          self->NotifyOwner(*session);
                        }
                      }
                      std::move(reply).Run(
                          Ok().Set("revision", result->id)
                              .Set("operation_id", operation.operation_id)
                              .Set("digest", operation.digest));
                    },
                    self, token, operation, std::move(reply)),
                session->lease, operation);
          },
          weak_factory_.GetWeakPtr(), session.token, operation,
          std::move(reply)));
}

void DaoHomeExternalService::Rollback(Session& session,
                                      base::DictValue params,
                                      Reply reply) {
  const auto* revision = params.FindString("revision");
  const auto* base = params.FindString("base_revision");
  const auto* id = params.FindString("operation_id");
  const auto* summary = params.FindString("summary");
  if (!BoundedText(revision, 128) || !base || !BoundedText(id, 128) ||
      !BoundedText(summary, kMaxHomeSummaryBytes)) {
    std::move(reply).Run(Error("INVALID_ARGUMENT"));
    return;
  }
  HomeOperationRequest operation{.operation_id = *id,
                                 .method = "project.rollback",
                                 .base_revision = *base,
                                 .target_revision = *revision,
                                 .summary = *summary};
  InvalidatePreview(session.token);
  session.project->Rollback(
      *base, *revision, *summary,
      base::BindOnce(
          [](base::WeakPtr<DaoHomeExternalService> self, std::string token,
             std::string operation_id, Reply reply,
             base::expected<HomeVersion, HomeError> result) {
            if (!result.has_value()) {
              std::move(reply).Run(Error(result.error()));
              return;
            }
            if (self) {
              if (Session* session = self->FindSession(token)) {
                if (session->draft)
                  session->project->DiscardDraft(
                      session->draft->id,
                      base::BindOnce([](base::expected<void, HomeError>) {}));
                session->draft.reset();
                session->published = true;
                self->NotifyOwner(*session);
              }
            }
            std::move(reply).Run(Ok().Set("revision", result->id)
                                     .Set("operation_id", operation_id));
          },
          weak_factory_.GetWeakPtr(), session.token, *id, std::move(reply)),
      session.authorization, operation);
}

void DaoHomeExternalService::Screenshot(Session& session, Reply reply) {
  auto* ui = GetHomeUI(session.web_contents());
  if (!session.draft || !session.preview_valid || !session.lease ||
      !session.lease->IsValid() || !ui ||
      !ui->IsExternalPreviewValid(session.draft->id)) {
    std::move(reply).Run(Error("PREVIEW_REQUIRED"));
    return;
  }
  auto* view = session.web_contents()->GetRenderWidgetHostView();
  if (!view || !view->IsSurfaceAvailableForCopy()) {
    std::move(reply).Run(Error("PREVIEW_FAILED"));
    return;
  }
  const auto size = view->GetViewBounds().size();
  if (size.IsEmpty()) {
    std::move(reply).Run(Error("PREVIEW_FAILED"));
    return;
  }
  const float scale =
      std::min(1.f, 1600.f / std::max(size.width(), size.height()));
  view->CopyFromSurface(
      gfx::Rect(), gfx::ScaleToRoundedSize(size, scale), base::Seconds(2),
      base::BindPostTaskToCurrentDefault(base::BindOnce(
          [](base::WeakPtr<DaoHomeExternalService> self, std::string token,
             scoped_refptr<DaoHomeMutationLease> lease, std::string draft_id,
             Reply reply, const content::CopyFromSurfaceResult& capture) {
            Session* session = self ? self->FindSession(token) : nullptr;
            auto* ui = session ? GetHomeUI(session->web_contents()) : nullptr;
            if (!session || session->lease != lease || !lease->IsValid() ||
                !ui || !ui->IsExternalPreviewValid(draft_id)) {
              std::move(reply).Run(Error("PREVIEW_REQUIRED"));
              return;
            }
            if (!capture.has_value() || capture->bitmap.drawsNothing()) {
              std::move(reply).Run(Error("PREVIEW_FAILED"));
              return;
            }
            auto png =
                gfx::PNGCodec::EncodeBGRASkBitmap(capture->bitmap, false);
            if (!png || png->size() > 8 * 1024 * 1024) {
              std::move(reply).Run(Error("PREVIEW_FAILED"));
              return;
            }
            std::move(reply).Run(Ok().Set("mime_type", "image/png")
                                     .Set("data", base::Base64Encode(*png)));
          },
          weak_factory_.GetWeakPtr(), session.token, session.lease,
          session.draft->id, std::move(reply))));
}

}  // namespace dao
