// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent_space/dao_agent_space_service.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/barrier_callback.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/file_util_icu.h"
#include "base/no_destructor.h"
#include "base/rand_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/permissions/permission_request_manager.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/agent/dao_agent_skill_service.h"
#include "dao/browser/agent/dao_agent_skill_service_factory.h"
#include "dao/browser/agent_space/dao_agent_script_run.h"
#include "dao/browser/agent_space/dao_hidden_browser.h"
#include "dao/browser/agent_space/dao_hidden_dialog.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_tool_catalog.h"
#include "dao/browser/automation/dao_browser_tool_executor.h"
#include "dao/browser/automation/dao_tool_schema_validator.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "dao/browser/ui/webui/dao_folder_storage.h"
#include "dao/browser/ui/webui/dao_sidebar_ui.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_delegate.h"
#include "url/gurl.h"

namespace dao {
namespace {

const void* const kDaoAgentSpaceServiceKey = &kDaoAgentSpaceServiceKey;

constexpr base::TimeDelta kDefaultIdleTimeout = base::Minutes(30);
constexpr base::TimeDelta kIdleCheckInterval = base::Minutes(1);
constexpr base::TimeDelta kHelpTimeout = base::Minutes(14);
constexpr base::TimeDelta kNotifyDelay = base::Milliseconds(120);
constexpr size_t kMaxNameChars = 60;
constexpr size_t kMaxStatusChars = 120;
constexpr size_t kMaxReasonChars = 200;
constexpr size_t kMaxLabelChars = 60;
constexpr gfx::Rect kHiddenWindowBounds(0, 0, 1520, 860);
constexpr char kNotificationIdPrefix[] = "dao-agent-space-";
constexpr char kNotifierId[] = "dao_agent_spaces";
constexpr char kResultsFolderId[] = "dao-agent-results";

std::map<const BrowserWindowInterface*, DaoAgentSpace*>& SpaceBrowsers() {
  static base::NoDestructor<
      std::map<const BrowserWindowInterface*, DaoAgentSpace*>>
      browsers;
  return *browsers;
}

std::string Clamp(std::string_view text, size_t max_chars) {
  std::u16string value = base::UTF8ToUTF16(text);
  base::TrimWhitespace(value, base::TRIM_ALL, &value);
  value = base::CollapseWhitespace(value, /*trim_sequences_with_line_breaks=*/
                                   true);
  if (value.size() > max_chars) {
    value.resize(max_chars);
  }
  return base::UTF16ToUTF8(value);
}

DaoBrowserToolResult ErrorResult(DaoToolError error) {
  DaoBrowserToolResult result;
  result.error = std::move(error);
  return result;
}

DaoBrowserToolResult SuccessResult(base::DictValue data) {
  DaoBrowserToolResult result;
  result.ok = true;
  result.data = base::Value(std::move(data));
  return result;
}

DaoToolError UserInControlError(const DaoAgentSpace& space) {
  std::string why;
  switch (space.handoff_reason()) {
    case DaoAgentSpaceHandoffReason::kHelpRequested:
      why = "you asked for help";
      break;
    case DaoAgentSpaceHandoffReason::kPermissionPrompt:
      why = "a page asked for a permission only the user can answer";
      break;
    case DaoAgentSpaceHandoffReason::kFileChooser:
      why =
          "a page opened a file picker; only the user can choose files, and "
          "the picker was cancelled until they take over";
      break;
    case DaoAgentSpaceHandoffReason::kDeviceChooser:
      why =
          "a page asked to connect to a device; only the user can choose one, "
          "and the request was cancelled until they take over";
      break;
    case DaoAgentSpaceHandoffReason::kBrowserDialog:
      why =
          "the browser is showing a dialog only the user can answer, such as "
          "a site sign-in, security key or certificate prompt";
      break;
    case DaoAgentSpaceHandoffReason::kManualTakeover:
    case DaoAgentSpaceHandoffReason::kNone:
      why = "the user took over";
      break;
  }
  return MakeDaoToolError(
      DaoToolErrorCode::kSpaceUserInControl,
      base::StrCat(
          {"The user is in control of space \"", space.name(), "\" (", why,
           "). Stop now: do not retry, do not call other tools on this space "
           "and do not try to take control back. Tell the user what you were "
           "doing and wait until they hand control back or ask you to "
           "continue."}));
}

// Waits for a fresh tab's first navigation, so tools see the page instead of
// its initial empty document, which automation does not accept. Deletes
// itself and posts `done` when the page commits, stops loading, goes away,
// or after a timeout.
class FirstCommitWaiter : public content::WebContentsObserver {
 public:
  static void Wait(content::WebContents* contents, base::OnceClosure done) {
    if (!contents || !contents->GetLastCommittedURL().is_empty()) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                               std::move(done));
      return;
    }
    new FirstCommitWaiter(contents, std::move(done));
  }

  FirstCommitWaiter(const FirstCommitWaiter&) = delete;
  FirstCommitWaiter& operator=(const FirstCommitWaiter&) = delete;
  ~FirstCommitWaiter() override = default;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override {
    if (handle->IsInPrimaryMainFrame() && handle->HasCommitted()) {
      Finish();
    }
  }
  void DidStopLoading() override { Finish(); }
  void WebContentsDestroyed() override { Finish(); }

 private:
  FirstCommitWaiter(content::WebContents* contents, base::OnceClosure done)
      : content::WebContentsObserver(contents), done_(std::move(done)) {
    timeout_.Start(
        FROM_HERE, base::Seconds(15),
        base::BindOnce(&FirstCommitWaiter::Finish, base::Unretained(this)));
  }
  void Finish() {
    if (!done_) {
      return;
    }
    timeout_.Stop();
    Observe(nullptr);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             std::move(done_));
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
  }

  base::OnceClosure done_;
  base::OneShotTimer timeout_;
};

DaoToolError DialogOpenError(const DaoAgentSpace& space) {
  const DaoAgentSpaceDialog& dialog = *space.pending_dialog();
  return MakeDaoToolError(
      DaoToolErrorCode::kPageDialogOpen,
      base::StrCat(
          {"Tab ", dialog.tab_id, " in this space opened a ", dialog.type,
           " dialog: \"", dialog.message,
           "\". Nothing else can run in this space until it is answered. "
           "Call handle_dialog with accept true or false (and prompt_text for "
           "a prompt), or request_user_help if the user should decide."}));
}

DaoToolError ClosedError(const DaoAgentSpace& space) {
  if (space.state() == DaoAgentSpaceState::kFinished) {
    return MakeDaoToolError(DaoToolErrorCode::kSpaceClosed,
                            "This space is already finished. Create a new "
                            "space if more browser work is needed.");
  }
  if (space.stopped_for_inactivity()) {
    return MakeDaoToolError(
        DaoToolErrorCode::kSpaceClosed,
        "This space was stopped after a long time without activity. Create a "
        "new space if more browser work is needed.");
  }
  return MakeDaoToolError(DaoToolErrorCode::kSpaceClosed,
                          "The user stopped this space. Do not continue the "
                          "task in the background unless the user asks you "
                          "to.");
}

void ShowToast(Profile* profile, const std::u16string& message) {
  ProfileBrowserCollection* collection =
      profile ? ProfileBrowserCollection::GetForProfile(profile) : nullptr;
  BrowserWindowInterface* browser = collection
                                        ? collection->FindTabbedBrowser(
                                              /*match_original_profiles=*/false)
                                        : nullptr;
  BrowserView* view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  if (view && view->dao_toast()) {
    view->dao_toast()->ShowToast(message);
  }
}

bool HasPendingPermissionPrompt(DaoAgentSpace* space) {
  BrowserWindowInterface* browser = space->browser();
  TabListInterface* tab_list =
      browser ? TabListInterface::From(browser) : nullptr;
  if (!tab_list) {
    return false;
  }
  for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
    auto* manager = permissions::PermissionRequestManager::FromWebContents(
        tab->GetContents());
    if (manager && manager->IsRequestInProgress()) {
      return true;
    }
  }
  return false;
}

}  // namespace

struct DaoAgentSpaceService::PendingCall {
  std::string client_key;
  std::string external_id;
  std::string space_id;
  std::string tool;
  size_t event_serial = 0;
  ResultCallback callback;
};

struct DaoAgentSpaceService::PendingHelp {
  std::string client_key;
  std::string external_id;
  ResultCallback callback;
  base::OneShotTimer timer;
};

DaoAgentSpaceService::DaoAgentSpaceService(Profile* profile)
    : profile_(profile), idle_timeout_(kDefaultIdleTimeout) {
  SetDaoHiddenBrowserPredicate(
      &DaoAgentSpaceService::IsHiddenAgentSpaceBrowser);
  SetDaoHiddenDialogHandlers(&DaoAgentSpaceService::OnHiddenDialogOpened,
                             &DaoAgentSpaceService::OnHiddenDialogClosed);
  SetDaoHiddenPageNeedHandler(&DaoAgentSpaceService::OnHiddenPageNeed);
  SetDaoHiddenDownloadDirectoryProvider(
      &DaoAgentSpaceService::GetHiddenDownloadDirectory);
  if (content::DownloadManager* downloads = profile->GetDownloadManager()) {
    download_manager_observations_.AddObservation(downloads);
  }
  LoadHistory();
  browser_collection_observation_.Observe(
      GlobalBrowserCollection::GetInstance());
}

DaoAgentSpaceService::~DaoAgentSpaceService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  for (auto& [id, help] : helps_) {
    help->timer.Stop();
  }
  helps_.clear();
  scripts_.clear();
  calls_.clear();
  spaces_.clear();
}

// static
DaoAgentSpaceService* DaoAgentSpaceService::GetForProfile(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile || profile->IsOffTheRecord() || profile->IsGuestSession()) {
    return nullptr;
  }
  auto* service = static_cast<DaoAgentSpaceService*>(
      profile->GetUserData(kDaoAgentSpaceServiceKey));
  if (!service) {
    auto owned = std::make_unique<DaoAgentSpaceService>(profile);
    service = owned.get();
    profile->SetUserData(kDaoAgentSpaceServiceKey, std::move(owned));
  }
  return service;
}

// static
void DaoAgentSpaceService::StopSpacesForClient(DaoToolClient client) {
  ProfileManager* profile_manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!profile_manager) {
    return;
  }
  for (Profile* profile : profile_manager->GetLoadedProfiles()) {
    auto* service = static_cast<DaoAgentSpaceService*>(
        profile->GetUserData(kDaoAgentSpaceServiceKey));
    if (!service) {
      continue;
    }
    for (DaoAgentSpace* space : service->GetSpaces()) {
      if (space->client() == client && !space->is_closed()) {
        service->Stop(space->id());
      }
    }
  }
}

// static
bool DaoAgentSpaceService::IsAgentSpaceBrowser(
    const BrowserWindowInterface* browser) {
  return browser && SpaceBrowsers().contains(browser);
}

// static
bool DaoAgentSpaceService::IsHiddenAgentSpaceBrowser(
    BrowserWindowInterface* browser) {
  if (!IsAgentSpaceBrowser(browser)) {
    return false;
  }
  ui::BaseWindow* window = browser->GetWindow();
  return !window || !window->IsVisible();
}

// static
DaoAgentSpace* DaoAgentSpaceService::FindSpaceForBrowser(
    const BrowserWindowInterface* browser) {
  auto it = SpaceBrowsers().find(browser);
  return it == SpaceBrowsers().end() ? nullptr : it->second;
}

// static
DaoAgentSpace* DaoAgentSpaceService::FindSpaceForContents(
    content::WebContents* contents) {
  return contents
             ? FindSpaceForBrowser(
                   GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
                       contents))
             : nullptr;
}

// static
void DaoAgentSpaceService::OnHiddenDialogOpened(
    content::WebContents* contents,
    content::JavaScriptDialogType type,
    const std::u16string& message,
    const std::u16string& default_prompt) {
  if (DaoAgentSpace* space = FindSpaceForContents(contents)) {
    space->OnDialogOpened(contents, type, message, default_prompt);
  }
}

// static
void DaoAgentSpaceService::OnHiddenDialogClosed(
    content::WebContents* contents) {
  if (DaoAgentSpace* space = FindSpaceForContents(contents)) {
    space->OnDialogClosed(contents);
  }
}

// static
void DaoAgentSpaceService::OnHiddenPageNeed(content::WebContents* contents,
                                            DaoHiddenPageNeed need) {
  if (DaoAgentSpace* space = FindSpaceForContents(contents)) {
    space->OnPageNeedsUser(contents,
                           need == DaoHiddenPageNeed::kFileChooser
                               ? DaoAgentSpaceHandoffReason::kFileChooser
                               : DaoAgentSpaceHandoffReason::kDeviceChooser);
  }
}

// static
base::FilePath DaoAgentSpaceService::GetHiddenDownloadDirectory(
    content::WebContents* contents) {
  DaoAgentSpace* space = FindSpaceForContents(contents);
  if (!space || space->is_closed() || space->IsWindowVisible()) {
    return base::FilePath();
  }
  DownloadPrefs* prefs =
      DownloadPrefs::FromBrowserContext(contents->GetBrowserContext());
  if (!prefs) {
    return base::FilePath();
  }
  base::FilePath::StringType folder =
      base::FilePath::FromUTF8Unsafe(space->name()).value();
  base::i18n::ReplaceIllegalCharactersInPath(&folder, '_');
  base::TrimString(folder, FILE_PATH_LITERAL(". "), &folder);
  if (folder.empty()) {
    folder = base::FilePath::FromUTF8Unsafe(space->id()).value();
  }
  return prefs->DownloadPath()
      .Append(base::FilePath::FromUTF16Unsafe(
          l10n_util::GetStringUTF16(IDS_DAO_AGENT_SPACE_DOWNLOADS_FOLDER)))
      .Append(folder);
}

// static
void DaoAgentSpaceService::RegisterSpaceBrowser(
    const BrowserWindowInterface* browser,
    DaoAgentSpace* space) {
  if (browser) {
    SpaceBrowsers()[browser] = space;
  }
}

// static
void DaoAgentSpaceService::UnregisterSpaceBrowser(
    const BrowserWindowInterface* browser) {
  SpaceBrowsers().erase(browser);
}

// static
bool DaoAgentSpaceService::IsSpaceTool(std::string_view name) {
  return name == "create_space" || name == "list_spaces" ||
         name == "set_space_status" || name == "request_user_help" ||
         name == "finish_space" || name == "handle_dialog" ||
         name == "get_site_skills" || name == "run_browser_script";
}

// static
bool DaoAgentSpaceService::ShouldRoute(std::string_view name,
                                       const base::DictValue& arguments) {
  return IsSpaceTool(name) || arguments.contains("space_id");
}

void DaoAgentSpaceService::ExecuteTool(const DaoAgentSpaceClient& client,
                                       DaoBrowserToolCall call,
                                       ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto catalog = DaoBrowserToolCatalog::TryGet();
  const DaoBrowserToolDefinition* definition =
      catalog.has_value() ? catalog.value()->Find(call.name, client.type)
                          : nullptr;
  if (!definition) {
    std::move(callback).Run(
        ErrorResult(MakeDaoToolError(DaoToolErrorCode::kUnknownTool,
                                     "Unknown or unavailable browser tool.")));
    return;
  }
  if (call.request_id.empty()) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kInvalidArgument, "Request id is required.")));
    return;
  }
  if (IsSpaceTool(call.name)) {
    ExecuteSpaceTool(client, *definition, std::move(call), std::move(callback));
    return;
  }
  ExecuteInSpace(client, std::move(call), std::move(callback));
}

void DaoAgentSpaceService::CancelTool(const DaoAgentSpaceClient& client,
                                      std::string_view request_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string internal_id =
      base::StrCat({"space:", client.key, ":", request_id});
  auto call = calls_.find(internal_id);
  if (call != calls_.end()) {
    DaoAgentSpace* space = FindSpace(call->second->space_id);
    if (space && space->executor()) {
      space->executor()->Cancel(internal_id);
    } else {
      OnSpaceToolComplete(
          internal_id,
          ErrorResult(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                       "Browser tool call was cancelled.")));
    }
    return;
  }
  auto script =
      scripts_.find(base::StrCat({"script:", client.key, ":", request_id}));
  if (script != scripts_.end()) {
    script->second->Cancel(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                            "The script was cancelled."));
    return;
  }
  for (auto& [space_id, help] : helps_) {
    if (help->client_key == client.key && help->external_id == request_id) {
      // The handoff stays in place; only the waiting call ends.
      const std::string id = space_id;
      ResolveHelp(id, ErrorResult(MakeDaoToolError(
                          DaoToolErrorCode::kToolCancelled,
                          "Waiting for the user was cancelled.")));
      return;
    }
  }
}

void DaoAgentSpaceService::CancelClientTools(std::string_view client_key) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string script_prefix = base::StrCat({"script:", client_key, ":"});
  std::vector<std::string> scripts;
  for (const auto& [run_id, run] : scripts_) {
    if (base::StartsWith(run_id, script_prefix)) {
      scripts.push_back(run_id);
    }
  }
  for (const std::string& run_id : scripts) {
    auto run = scripts_.find(run_id);
    if (run != scripts_.end()) {
      run->second->Cancel(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                           "The script was cancelled."));
    }
  }
  std::vector<std::pair<std::string, std::string>> calls;
  for (const auto& [internal_id, call] : calls_) {
    if (call->client_key == client_key) {
      calls.emplace_back(internal_id, call->space_id);
    }
  }
  base::WeakPtr<DaoAgentSpaceService> weak_this = weak_factory_.GetWeakPtr();
  for (const auto& [internal_id, space_id] : calls) {
    DaoAgentSpace* space = FindSpace(space_id);
    if (space && space->executor()) {
      space->executor()->Cancel(internal_id);
    } else {
      OnSpaceToolComplete(
          internal_id,
          ErrorResult(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                       "Browser tool call was cancelled.")));
    }
    if (!weak_this) {
      return;
    }
  }
  std::vector<std::string> helps;
  for (const auto& [space_id, help] : helps_) {
    if (help->client_key == client_key) {
      helps.push_back(space_id);
    }
  }
  for (const std::string& space_id : helps) {
    ResolveHelp(space_id, ErrorResult(MakeDaoToolError(
                              DaoToolErrorCode::kToolCancelled,
                              "Waiting for the user was cancelled.")));
  }
}

void DaoAgentSpaceService::HandOffForPage(DaoAgentSpace* space,
                                          content::WebContents* contents,
                                          DaoAgentSpaceHandoffReason reason) {
  if (!space || space->state() != DaoAgentSpaceState::kRunning) {
    return;
  }
  if (contents) {
    space->ActivateTab(GetOrCreateSidebarTabId(contents));
  }
  space->HandOffToUser(reason, std::string());
  int message_id = IDS_DAO_AGENT_SPACE_PERMISSION_TOAST;
  switch (reason) {
    case DaoAgentSpaceHandoffReason::kFileChooser:
      message_id = IDS_DAO_AGENT_SPACE_FILE_TOAST;
      break;
    case DaoAgentSpaceHandoffReason::kDeviceChooser:
      message_id = IDS_DAO_AGENT_SPACE_DEVICE_TOAST;
      break;
    case DaoAgentSpaceHandoffReason::kBrowserDialog:
      message_id = IDS_DAO_AGENT_SPACE_BROWSER_DIALOG_TOAST;
      break;
    default:
      break;
  }
  AlertUser(space, l10n_util::GetStringFUTF16(
                       message_id, base::UTF8ToUTF16(space->name())));
  NotifySpaceChanged();
}

bool DaoAgentSpaceService::TakeOver(std::string_view space_id) {
  DaoAgentSpace* space = FindSpace(space_id);
  if (!space || space->is_closed()) {
    return false;
  }
  if (space->state() == DaoAgentSpaceState::kRunning) {
    space->HandOffToUser(DaoAgentSpaceHandoffReason::kManualTakeover,
                         std::string());
  }
  space->ShowWindow();
  return true;
}

bool DaoAgentSpaceService::HandBack(std::string_view space_id) {
  DaoAgentSpace* space = FindSpace(space_id);
  if (!space || space->state() != DaoAgentSpaceState::kUserInControl) {
    return false;
  }
  base::DictValue changes = space->ReturnToAgent();
  CloseUserAlert(space_id);
  ResolveHelp(space_id,
              SuccessResult(
                  base::DictValue()
                      .Set("success", true)
                      .Set("space_id", space->id())
                      .Set("state", "running")
                      .Set("user_changes", std::move(changes))
                      .Set("message",
                           "The user handed control back. Pages may have "
                           "changed: observe the page again before acting.")));
  return true;
}

bool DaoAgentSpaceService::Stop(std::string_view space_id) {
  DaoAgentSpace* space = FindSpace(space_id);
  if (!space || space->is_closed()) {
    return false;
  }
  CloseSpace(space, DaoAgentSpaceState::kStopped, std::string(), {});
  return true;
}

bool DaoAgentSpaceService::ShowWindow(std::string_view space_id,
                                      std::string_view tab_id) {
  DaoAgentSpace* space = FindSpace(space_id);
  if (!space || space->is_closed()) {
    return false;
  }
  if (!tab_id.empty()) {
    space->ActivateTab(tab_id);
  }
  space->ShowWindow();
  return true;
}

bool DaoAgentSpaceService::HideWindow(std::string_view space_id) {
  DaoAgentSpace* space = FindSpace(space_id);
  if (!space || space->is_closed()) {
    return false;
  }
  space->HideWindow();
  return true;
}

bool DaoAgentSpaceService::Dismiss(std::string_view space_id) {
  auto it = spaces_.find(space_id);
  if (it == spaces_.end() || !it->second->is_closed()) {
    return false;
  }
  std::erase(space_order_, it->first);
  spaces_.erase(it);
  SaveHistory();
  NotifySpaceChanged();
  return true;
}

void DaoAgentSpaceService::SaveHistory() {
  base::ListValue history;
  for (DaoAgentSpace* space : GetSpaces()) {
    if (space->is_closed()) {
      history.Append(space->ToRecord());
    }
  }
  profile_->GetPrefs()->SetList(prefs::kDaoAgentSpaceHistory,
                                std::move(history));
}

void DaoAgentSpaceService::LoadHistory() {
  for (const base::Value& value :
       profile_->GetPrefs()->GetList(prefs::kDaoAgentSpaceHistory)) {
    const base::DictValue* record = value.GetIfDict();
    const std::string* id = record ? record->FindString("space_id") : nullptr;
    const std::string* name = record ? record->FindString("name") : nullptr;
    const std::optional<int> client =
        record ? record->FindInt("client") : std::nullopt;
    if (!id || !name || !client || spaces_.contains(*id) ||
        (*client != static_cast<int>(DaoToolClient::kDaoAgent) &&
         *client != static_cast<int>(DaoToolClient::kMcp))) {
      continue;
    }
    const std::string* owner = record->FindString("owner");
    auto space = std::make_unique<DaoAgentSpace>(
        this, *id, *name, static_cast<DaoToolClient>(*client),
        owner ? *owner : std::string(), /*browser=*/nullptr);
    space->RestoreRecord(*record);
    space_order_.push_back(*id);
    spaces_.emplace(*id, std::move(space));
  }
  PruneClosedRecords();
}

DaoAgentSpace* DaoAgentSpaceService::FindSpace(std::string_view space_id) {
  auto it = spaces_.find(space_id);
  return it == spaces_.end() ? nullptr : it->second.get();
}

std::vector<DaoAgentSpace*> DaoAgentSpaceService::GetSpaces() {
  std::vector<DaoAgentSpace*> spaces;
  for (const std::string& id : space_order_) {
    if (DaoAgentSpace* space = FindSpace(id)) {
      spaces.push_back(space);
    }
  }
  return spaces;
}

base::ListValue DaoAgentSpaceService::SerializeSpaces() {
  base::ListValue list;
  for (DaoAgentSpace* space : GetSpaces()) {
    list.Append(space->ToDict(/*include_events=*/true));
  }
  return list;
}

size_t DaoAgentSpaceService::GetActiveSpaceCount() const {
  return static_cast<size_t>(std::ranges::count_if(
      spaces_, [](const auto& entry) { return !entry.second->is_closed(); }));
}

void DaoAgentSpaceService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void DaoAgentSpaceService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void DaoAgentSpaceService::NotifySpaceChanged() {
  if (notify_timer_.IsRunning()) {
    return;
  }
  notify_timer_.Start(FROM_HERE, kNotifyDelay,
                      base::BindOnce(&DaoAgentSpaceService::FlushNotifications,
                                     weak_factory_.GetWeakPtr()));
}

void DaoAgentSpaceService::OnBrowserClosed(BrowserWindowInterface* browser) {
  DaoAgentSpace* space = FindSpaceForBrowser(browser);
  if (!space) {
    return;
  }
  // The user closed the task window, or the app is quitting. The window is
  // already going away, so leave its tabs alone.
  if (!space->is_closed()) {
    CloseSpace(space, DaoAgentSpaceState::kStopped, std::string(), {},
               /*window_closing=*/true);
  }
  UnregisterSpaceBrowser(browser);
  space->DetachWindow();
}

void DaoAgentSpaceService::OnDownloadCreated(content::DownloadManager* manager,
                                             download::DownloadItem* item) {
  if (DaoAgentSpace* space = FindSpaceForContents(
          content::DownloadItemUtils::GetWebContents(item))) {
    space->TrackDownload(item);
  }
}

void DaoAgentSpaceService::ManagerGoingDown(content::DownloadManager* manager) {
  if (download_manager_observations_.IsObservingSource(manager)) {
    download_manager_observations_.RemoveObservation(manager);
  }
}

void DaoAgentSpaceService::SetIdleTimeoutForTesting(base::TimeDelta timeout) {
  idle_timeout_ = timeout;
}

void DaoAgentSpaceService::ExecuteSpaceTool(
    const DaoAgentSpaceClient& client,
    const DaoBrowserToolDefinition& definition,
    DaoBrowserToolCall call,
    ResultCallback callback) {
  auto valid = ValidateToolArguments(definition, call.arguments);
  if (!valid.has_value()) {
    std::move(callback).Run(ErrorResult(std::move(valid).error()));
    return;
  }
  if (call.name == "create_space") {
    CreateSpace(client, call.arguments, std::move(callback));
  } else if (call.name == "list_spaces") {
    ListSpaces(client, std::move(callback));
  } else if (call.name == "set_space_status") {
    SetStatus(client, call.arguments, std::move(callback));
  } else if (call.name == "request_user_help") {
    RequestUserHelp(client, std::move(call.request_id), call.arguments,
                    std::move(callback));
  } else if (call.name == "finish_space") {
    FinishSpace(client, call.arguments, std::move(callback));
  } else if (call.name == "handle_dialog") {
    HandleDialog(client, call.arguments, std::move(callback));
  } else if (call.name == "get_site_skills") {
    GetSiteSkills(call.arguments, std::move(callback));
  } else if (call.name == "run_browser_script") {
    RunScript(client, std::move(call.request_id), call.arguments,
              std::move(callback));
  } else {
    std::move(callback).Run(
        ErrorResult(MakeDaoToolError(DaoToolErrorCode::kUnknownTool,
                                     "Unknown or unavailable browser tool.")));
  }
}

void DaoAgentSpaceService::CreateSpace(const DaoAgentSpaceClient& client,
                                       const base::DictValue& arguments,
                                       ResultCallback callback) {
  if (GetActiveSpaceCount() >= kMaxActiveSpaces) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kSpaceLimitReached,
        base::StrCat({"At most ", base::NumberToString(kMaxActiveSpaces),
                      " spaces can run at once. Finish a space you no longer "
                      "need, or reuse one from list_spaces."}))));
    return;
  }
  const std::string* raw_name = arguments.FindString("name");
  const std::string name = raw_name ? Clamp(*raw_name, kMaxNameChars) : "";
  if (name.empty()) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kInvalidArgument, "name must not be blank.")));
    return;
  }
  const std::string* raw_url = arguments.FindString("url");
  GURL url(raw_url && !raw_url->empty() ? *raw_url : "about:blank");
  if (!url.is_valid() || !IsAutomationUrlEligible(url)) {
    std::move(callback).Run(ErrorResult(
        MakeDaoToolError(DaoToolErrorCode::kTargetForbidden,
                         "url must be an http, https or about:blank URL.")));
    return;
  }
  // Clean spaces get a temporary profile of their own, which goes away with
  // the window.
  const bool uses_logins = arguments.FindBool("logins").value_or(true);
  Profile* space_profile =
      uses_logins ? profile_.get()
                  // Chromium lets DevTools browser contexts open windows,
                  // which is the same kind of throwaway profile.
                  : profile_->GetOffTheRecordProfile(
                        Profile::OTRProfileID::CreateUniqueForDevTools(),
                        /*create_if_needed=*/true);
  if (!space_profile ||
      GetBrowserWindowCreationStatusForProfile(*space_profile) !=
          BrowserWindowInterface::CreationStatus::kOk) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kTargetForbidden,
        uses_logins ? "This profile cannot open browser windows right now."
                    : "Clean spaces are unavailable, for example because "
                      "private windows are turned off.")));
    return;
  }
  if (!uses_logins) {
    if (content::DownloadManager* downloads =
            space_profile->GetDownloadManager();
        downloads &&
        !download_manager_observations_.IsObservingSource(downloads)) {
      download_manager_observations_.AddObservation(downloads);
    }
  }

  BrowserWindowCreateParams params(BrowserWindowInterface::TYPE_NORMAL,
                                   *space_profile, /*from_user_gesture=*/false);
  params.initial_bounds = kHiddenWindowBounds;
  params.omit_from_session_restore = true;
  params.should_trigger_session_restore = false;
  BrowserWindowInterface* browser = CreateBrowserWindow(std::move(params));
  if (!browser) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kInternalError, "Could not create a space window.")));
    return;
  }

  std::string id;
  do {
    id =
        base::StringPrintf("s%d-%04x", next_space_number_++,
                           static_cast<unsigned>(base::RandGenerator(0x10000)));
  } while (spaces_.contains(id));
  auto owned = std::make_unique<DaoAgentSpace>(this, id, name, client.type,
                                               client.label, browser);
  DaoAgentSpace* space = owned.get();
  space->set_uses_logins(uses_logins);
  spaces_.emplace(id, std::move(owned));
  space_order_.push_back(id);
  if (!space->Initialize(url)) {
    CloseSpace(space, DaoAgentSpaceState::kStopped, std::string(), {});
    Dismiss(id);
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kInternalError, "Could not open the space page.")));
    return;
  }
  space->CompleteEvent(space->AddEvent("create_space", name), true,
                       std::string());
  if (!idle_timer_.IsRunning()) {
    idle_timer_.Start(
        FROM_HERE, kIdleCheckInterval,
        base::BindRepeating(&DaoAgentSpaceService::CheckIdleSpaces,
                            weak_factory_.GetWeakPtr()));
  }
  PruneClosedRecords();
  NotifySpaceChanged();

  // Reply once the first page committed, so the agent can act right away.
  FirstCommitWaiter::Wait(
      space->GetCurrentContents(),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentSpaceService> service, std::string id,
             std::string url, bool uses_logins, ResultCallback callback) {
            DaoAgentSpace* space = service ? service->FindSpace(id) : nullptr;
            if (!space || space->is_closed()) {
              std::move(callback).Run(ErrorResult(MakeDaoToolError(
                  DaoToolErrorCode::kSpaceClosed,
                  "The space was closed before its first page loaded.")));
              return;
            }
            base::DictValue result = space->ToDict(/*include_events=*/false);
            result.Set("success", true);
            if (!result.contains("url")) {
              result.Set("url", url);
            }
            result.Set(
                "message",
                base::StrCat(
                    {uses_logins ? "Space created in a hidden window that "
                                   "keeps the user's sign-ins. "
                                 : "Clean space created in a hidden window "
                                   "without the user's cookies or sign-ins. ",
                     "Pass this space_id to browser tools, add a short label "
                     "to actions so the user can follow along, call "
                     "set_space_status as you progress, use "
                     "request_user_help for sign-ins, codes, payments or "
                     "irreversible steps, and call finish_space when "
                     "done."}));
            std::move(callback).Run(SuccessResult(std::move(result)));
          },
          weak_factory_.GetWeakPtr(), id, url.spec(), uses_logins,
          std::move(callback)));
}

void DaoAgentSpaceService::ListSpaces(const DaoAgentSpaceClient& client,
                                      ResultCallback callback) {
  base::ListValue spaces;
  for (DaoAgentSpace* space : GetSpaces()) {
    if (space->client() != client.type || space->is_closed()) {
      continue;
    }
    base::DictValue dict = space->ToDict(/*include_events=*/false);
    dict.Set("tabs", space->SerializeTabs());
    spaces.Append(std::move(dict));
  }
  const int count = static_cast<int>(spaces.size());
  std::move(callback).Run(SuccessResult(
      base::DictValue().Set("spaces", std::move(spaces)).Set("count", count)));
}

void DaoAgentSpaceService::SetStatus(const DaoAgentSpaceClient& client,
                                     const base::DictValue& arguments,
                                     ResultCallback callback) {
  DaoToolError error;
  DaoAgentSpace* space = ResolveSpace(client, arguments, &error);
  if (!space) {
    std::move(callback).Run(ErrorResult(std::move(error)));
    return;
  }
  const std::string* text = arguments.FindString("text");
  space->SetStatus(Clamp(text ? *text : "", kMaxStatusChars));
  std::move(callback).Run(
      SuccessResult(base::DictValue().Set("success", true)));
}

void DaoAgentSpaceService::RequestUserHelp(const DaoAgentSpaceClient& client,
                                           std::string request_id,
                                           const base::DictValue& arguments,
                                           ResultCallback callback) {
  DaoToolError error;
  DaoAgentSpace* space = ResolveSpace(client, arguments, &error);
  if (!space) {
    std::move(callback).Run(ErrorResult(std::move(error)));
    return;
  }
  if (helps_.contains(space->id())) {
    std::move(callback).Run(ErrorResult(
        MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                         "Already waiting for the user in this space.")));
    return;
  }
  const std::string* raw_reason = arguments.FindString("reason");
  const std::string reason =
      Clamp(raw_reason ? *raw_reason : "", kMaxReasonChars);
  if (reason.empty()) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kInvalidArgument, "reason must not be blank.")));
    return;
  }
  if (const std::string* tab_id = arguments.FindString("tab_id")) {
    space->ActivateTab(*tab_id);
  }
  CancelSpaceCalls(space->id(), UserInControlError(*space));
  if (space->state() == DaoAgentSpaceState::kRunning) {
    space->HandOffToUser(DaoAgentSpaceHandoffReason::kHelpRequested, reason);
  }
  space->AddEvent("request_user_help", reason);

  auto help = std::make_unique<PendingHelp>();
  help->client_key = client.key;
  help->external_id = std::move(request_id);
  help->callback = std::move(callback);
  help->timer.Start(FROM_HERE, kHelpTimeout,
                    base::BindOnce(&DaoAgentSpaceService::OnHelpTimeout,
                                   weak_factory_.GetWeakPtr(), space->id()));
  helps_.emplace(space->id(), std::move(help));

  AlertUser(space, l10n_util::GetStringFUTF16(IDS_DAO_AGENT_SPACE_HELP_TOAST,
                                              base::UTF8ToUTF16(space->name()),
                                              base::UTF8ToUTF16(reason)));
  NotifySpaceChanged();
}

void DaoAgentSpaceService::FinishSpace(const DaoAgentSpaceClient& client,
                                       const base::DictValue& arguments,
                                       ResultCallback callback) {
  DaoToolError error;
  DaoAgentSpace* space = ResolveSpace(client, arguments, &error);
  if (!space) {
    std::move(callback).Run(ErrorResult(std::move(error)));
    return;
  }
  if (space->state() == DaoAgentSpaceState::kUserInControl) {
    std::move(callback).Run(ErrorResult(UserInControlError(*space)));
    return;
  }
  std::vector<std::string> keep;
  base::ListValue kept;
  if (const base::ListValue* raw_keep = arguments.FindList("keep")) {
    for (const base::Value& value : *raw_keep) {
      const std::string& tab_id = value.GetString();
      content::WebContents* contents = space->FindTab(tab_id);
      if (!contents) {
        std::move(callback).Run(ErrorResult(MakeDaoToolError(
            DaoToolErrorCode::kInvalidArgument,
            base::StrCat({"Tab ", tab_id,
                          " is not in this space. Call list_tabs with the "
                          "space_id to get current tab ids."}))));
        return;
      }
      if (std::ranges::contains(keep, tab_id)) {
        continue;
      }
      keep.push_back(tab_id);
      kept.Append(base::DictValue()
                      .Set("tab_id", tab_id)
                      .Set("url", contents->GetVisibleURL().spec())
                      .Set("title", base::UTF16ToUTF8(contents->GetTitle())));
    }
  }
  const std::string* raw_summary = arguments.FindString("summary");
  const std::string summary =
      Clamp(raw_summary ? *raw_summary : "", kMaxReasonChars);
  const std::u16string name = base::UTF8ToUTF16(space->name());
  const std::string space_id = space->id();
  const size_t kept_count = keep.size();
  base::ListValue downloads = space->SerializeDownloads();
  CloseSpace(space, DaoAgentSpaceState::kFinished, summary, keep);
  ShowToast(profile_, kept_count == 0
                          ? l10n_util::GetStringFUTF16(
                                IDS_DAO_AGENT_SPACE_FINISHED_TOAST, name)
                          : l10n_util::GetStringFUTF16(
                                IDS_DAO_AGENT_SPACE_FINISHED_KEPT_TOAST, name,
                                base::NumberToString16(kept_count)));
  std::move(callback).Run(
      SuccessResult(base::DictValue()
                        .Set("success", true)
                        .Set("space_id", space_id)
                        .Set("closed", true)
                        .Set("kept", std::move(kept))
                        .Set("downloads", std::move(downloads))));
}

void DaoAgentSpaceService::HandleDialog(const DaoAgentSpaceClient& client,
                                        const base::DictValue& arguments,
                                        ResultCallback callback) {
  DaoToolError error;
  DaoAgentSpace* space = ResolveSpace(client, arguments, &error);
  if (!space) {
    std::move(callback).Run(ErrorResult(std::move(error)));
    return;
  }
  if (space->state() == DaoAgentSpaceState::kUserInControl) {
    std::move(callback).Run(ErrorResult(UserInControlError(*space)));
    return;
  }
  if (!space->pending_dialog()) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kInvalidArgument,
        "No dialog is open in this space. Continue with the task.")));
    return;
  }
  const DaoAgentSpaceDialog dialog = *space->pending_dialog();
  const bool accept = arguments.FindBool("accept").value_or(false);
  const std::string* prompt_text = arguments.FindString("prompt_text");
  const std::string input =
      prompt_text ? *prompt_text : (accept ? dialog.default_prompt : "");
  if (!space->AnswerDialog(accept, input)) {
    std::move(callback).Run(ErrorResult(
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "The dialog closed before it could be answered.")));
    return;
  }
  space->CompleteEvent(space->AddEvent("handle_dialog", std::string()), true,
                       std::string());
  space->TouchActivity();
  std::move(callback).Run(SuccessResult(base::DictValue()
                                            .Set("success", true)
                                            .Set("tab_id", dialog.tab_id)
                                            .Set("type", dialog.type)
                                            .Set("message", dialog.message)
                                            .Set("accepted", accept)));
}

void DaoAgentSpaceService::GetSiteSkills(const base::DictValue& arguments,
                                         ResultCallback callback) {
  const std::string* raw_url = arguments.FindString("url");
  const GURL url(raw_url ? *raw_url : std::string());
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kInvalidArgument, "url must be an http(s) URL.")));
    return;
  }
  DaoAgentSkillService* skills =
      DaoAgentSkillServiceFactory::GetForProfile(profile_);
  if (!skills) {
    std::move(callback).Run(
        SuccessResult(base::DictValue().Set("skills", base::ListValue())));
    return;
  }
  const std::string host = base::ToLowerASCII(url.host());
  skills->GetSkillRegistry(base::BindOnce(
      [](base::WeakPtr<DaoAgentSpaceService> service, std::string host,
         ResultCallback callback, std::vector<SkillRegistryEntry> entries) {
        DaoAgentSkillService* skills =
            service
                ? DaoAgentSkillServiceFactory::GetForProfile(service->profile_)
                : nullptr;
        // Only notes written for this site; general skills are for the
        // built-in Agent's own prompts.
        std::vector<std::string> ids;
        for (const SkillRegistryEntry& entry : entries) {
          const bool matches =
              std::ranges::any_of(entry.hosts, [&](const std::string& pattern) {
                const std::string site = base::ToLowerASCII(pattern);
                return site != "*" &&
                       (host == site || base::EndsWith(host, "." + site));
              });
          if (matches && !entry.disabled) {
            ids.push_back(entry.id);
          }
        }
        if (!skills || ids.empty()) {
          std::move(callback).Run(SuccessResult(
              base::DictValue().Set("skills", base::ListValue())));
          return;
        }
        auto barrier = base::BarrierCallback<std::optional<SkillContent>>(
            ids.size(),
            base::BindOnce(
                [](ResultCallback callback,
                   std::vector<std::optional<SkillContent>> contents) {
                  base::ListValue list;
                  for (const std::optional<SkillContent>& content : contents) {
                    if (!content) {
                      continue;
                    }
                    list.Append(
                        base::DictValue()
                            .Set("id", content->metadata.id)
                            .Set("name", content->metadata.name)
                            .Set("description", content->metadata.description)
                            .Set("instructions", content->instructions));
                  }
                  std::move(callback).Run(SuccessResult(
                      base::DictValue().Set("skills", std::move(list))));
                },
                std::move(callback)));
        for (const std::string& id : ids) {
          skills->GetSkillContent(id, barrier);
        }
      },
      weak_factory_.GetWeakPtr(), host, std::move(callback)));
}

void DaoAgentSpaceService::OnSpaceDialogOpened(DaoAgentSpace* space) {
  // Calls stuck behind the dialog are cancelled outside the dialog manager's
  // stack.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<DaoAgentSpaceService> service,
                        std::string space_id) {
                       DaoAgentSpace* space =
                           service ? service->FindSpace(space_id) : nullptr;
                       if (space && space->pending_dialog()) {
                         service->CancelSpaceCalls(space_id,
                                                   DialogOpenError(*space));
                       }
                     },
                     weak_factory_.GetWeakPtr(), space->id()));
}

void DaoAgentSpaceService::RunScript(const DaoAgentSpaceClient& client,
                                     std::string request_id,
                                     const base::DictValue& arguments,
                                     ResultCallback callback) {
  constexpr size_t kMaxScriptBytes = 256 * 1024;
  const std::string* code = arguments.FindString("code");
  if (!code || code->empty() || code->size() > kMaxScriptBytes) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kInvalidArgument,
        "code must be non-empty JavaScript of at most 256 KiB.")));
    return;
  }
  std::string space_id;
  if (arguments.contains("space_id")) {
    DaoToolError error;
    DaoAgentSpace* space = ResolveSpace(client, arguments, &error);
    if (!space) {
      std::move(callback).Run(ErrorResult(std::move(error)));
      return;
    }
    space_id = space->id();
  }
  base::TimeDelta timeout = DaoAgentScriptRun::kDefaultTimeout;
  if (std::optional<int> timeout_ms = arguments.FindInt("timeout_ms")) {
    timeout = std::clamp(base::Milliseconds(*timeout_ms), base::Seconds(1),
                         DaoAgentScriptRun::kMaxTimeout);
  }
  const std::string run_id =
      base::StrCat({"script:", client.key, ":", request_id});
  if (scripts_.contains(run_id)) {
    std::move(callback).Run(ErrorResult(
        MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                         "Browser tool request id is already active.")));
    return;
  }
  base::WeakPtr<DaoAgentSpaceService> weak_this = weak_factory_.GetWeakPtr();
  auto run = std::make_unique<DaoAgentScriptRun>(
      profile_, run_id, *code, space_id, timeout,
      base::BindRepeating(
          [](base::WeakPtr<DaoAgentSpaceService> service,
             DaoAgentSpaceClient client, std::string request_id,
             std::string name, base::DictValue arguments,
             DaoAgentScriptRun::ToolCallback callback) {
            if (!service) {
              std::move(callback).Run(ErrorResult(
                  MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                   "Agent Spaces are shutting down.")));
              return;
            }
            DaoBrowserToolCall call;
            call.request_id = std::move(request_id);
            call.name = std::move(name);
            call.arguments = std::move(arguments);
            service->ExecuteTool(client, std::move(call), std::move(callback));
          },
          weak_this, client),
      base::BindRepeating(
          [](base::WeakPtr<DaoAgentSpaceService> service,
             DaoAgentSpaceClient client, std::string request_id) {
            if (service) {
              service->CancelTool(client, request_id);
            }
          },
          weak_this, client),
      base::BindOnce(&DaoAgentSpaceService::OnScriptDone, weak_this, run_id,
                     std::move(callback)));
  DaoAgentScriptRun* run_ptr = run.get();
  scripts_.emplace(run_id, std::move(run));
  run_ptr->Start();
}

void DaoAgentSpaceService::OnScriptDone(std::string run_id,
                                        ResultCallback callback,
                                        DaoBrowserToolResult result) {
  auto it = scripts_.find(run_id);
  if (it != scripts_.end()) {
    // The run may still be on the stack of its own DevTools dispatch.
    base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(it->second));
    scripts_.erase(it);
  }
  std::move(callback).Run(std::move(result));
}

void DaoAgentSpaceService::ExecuteInSpace(const DaoAgentSpaceClient& client,
                                          DaoBrowserToolCall call,
                                          ResultCallback callback,
                                          bool waited_for_commit) {
  DaoToolError error;
  DaoAgentSpace* space = ResolveSpace(client, call.arguments, &error);
  if (!space) {
    std::move(callback).Run(ErrorResult(std::move(error)));
    return;
  }
  // A tab the agent just opened may still show its initial empty document.
  if (!waited_for_commit && call.name != "list_tabs") {
    space->EnsureSessionTarget();
    content::WebContents* current = space->GetCurrentContents();
    if (current && current->GetLastCommittedURL().is_empty()) {
      FirstCommitWaiter::Wait(
          current, base::BindOnce(
                       [](base::WeakPtr<DaoAgentSpaceService> service,
                          DaoAgentSpaceClient client, DaoBrowserToolCall call,
                          ResultCallback callback) {
                         if (!service) {
                           std::move(callback).Run(ErrorResult(MakeDaoToolError(
                               DaoToolErrorCode::kToolCancelled,
                               "Agent Spaces are shutting down.")));
                           return;
                         }
                         service->ExecuteInSpace(client, std::move(call),
                                                 std::move(callback),
                                                 /*waited_for_commit=*/true);
                       },
                       weak_factory_.GetWeakPtr(), client, std::move(call),
                       std::move(callback)));
      return;
    }
  }
  if (space->state() == DaoAgentSpaceState::kRunning &&
      HasPendingPermissionPrompt(space)) {
    HandOffForPage(space, nullptr,
                   DaoAgentSpaceHandoffReason::kPermissionPrompt);
  }
  if (space->state() != DaoAgentSpaceState::kRunning || !space->executor() ||
      !space->session()) {
    std::move(callback).Run(ErrorResult(UserInControlError(*space)));
    return;
  }
  // The page is blocked until its dialog is answered.
  if (space->pending_dialog() && call.name != "list_tabs") {
    std::move(callback).Run(ErrorResult(DialogOpenError(*space)));
    return;
  }
  // Tabs the user opened while in control are theirs until the agent
  // switches to one on purpose.
  const std::string* target_tab = call.arguments.FindString("tab_id");
  if (call.name == "switch_tab" && target_tab) {
    space->ClaimTab(*target_tab);
  } else if (call.name != "list_tabs" && call.name != "open_tab") {
    std::string tab_id = target_tab ? *target_tab : std::string();
    if (tab_id.empty()) {
      if (content::WebContents* current = space->GetCurrentContents()) {
        tab_id = GetOrCreateSidebarTabId(current);
      }
    }
    if (space->IsUnclaimedUserTab(tab_id)) {
      std::move(callback).Run(ErrorResult(MakeDaoToolError(
          DaoToolErrorCode::kTargetForbidden,
          base::StrCat(
              {"Tab ", tab_id,
               " was opened by the user while they had control. Use it only "
               "if the task needs it: call switch_tab with this tab_id "
               "first. Never close the user's tabs without asking."}))));
      return;
    }
  }
  space->EnsureSessionTarget();
  if (call.name == "open_tab" && space->GetTabCount() >= kMaxTabsPerSpace) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kSpaceLimitReached,
        base::StrCat({"A space can hold at most ",
                      base::NumberToString(kMaxTabsPerSpace),
                      " tabs. Close tabs you no longer need first."}))));
    return;
  }
  const std::string internal_id =
      base::StrCat({"space:", client.key, ":", call.request_id});
  if (calls_.contains(internal_id)) {
    std::move(callback).Run(ErrorResult(
        MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                         "Browser tool request id is already active.")));
    return;
  }
  std::string label;
  if (const std::string* raw_label = call.arguments.FindString("label")) {
    label = Clamp(*raw_label, kMaxLabelChars);
  }
  call.arguments.Remove("space_id");
  call.arguments.Remove("label");

  auto pending = std::make_unique<PendingCall>();
  pending->client_key = client.key;
  pending->external_id = call.request_id;
  pending->space_id = space->id();
  pending->tool = call.name;
  pending->event_serial = space->AddEvent(call.name, std::move(label));
  if (const std::string* ref_id = call.arguments.FindString("ref_id")) {
    space->SetEventTarget(pending->event_serial, space->DescribeRef(*ref_id));
  }
  pending->callback = std::move(callback);
  calls_.emplace(internal_id, std::move(pending));
  space->TouchActivity();

  call.request_id = internal_id;
  space->executor()->Execute(
      space->session(), client.type, std::move(call),
      base::BindOnce(&DaoAgentSpaceService::OnSpaceToolComplete,
                     weak_factory_.GetWeakPtr(), internal_id));
}

void DaoAgentSpaceService::OnSpaceToolComplete(std::string internal_id,
                                               DaoBrowserToolResult result) {
  auto it = calls_.find(internal_id);
  if (it == calls_.end()) {
    return;
  }
  std::unique_ptr<PendingCall> pending = std::move(it->second);
  calls_.erase(it);
  if (DaoAgentSpace* space = FindSpace(pending->space_id)) {
    space->CompleteEvent(
        pending->event_serial, !result.error,
        result.error ? std::string(DaoToolErrorCodeToString(result.error->code))
                     : std::string());
    space->TouchActivity();
    if (!result.error && pending->tool == "list_tabs") {
      space->AnnotateTabs(result.data);
    }
    if (!result.error && (pending->tool == "query_elements" ||
                          pending->tool == "wait_for_element")) {
      space->RememberRefs(result.data);
    }
    if (!result.error && result.data.is_dict()) {
      base::ListValue dialogs = space->TakeDialogReports();
      if (!dialogs.empty()) {
        result.data.GetDict().Set("dialogs", std::move(dialogs));
      }
      base::ListValue closed = space->TakeClosedPopupReports();
      if (!closed.empty()) {
        result.data.GetDict().Set("closed_popups", std::move(closed));
      }
    }
  }
  std::move(pending->callback).Run(std::move(result));
}

void DaoAgentSpaceService::OnHelpTimeout(std::string space_id) {
  DaoAgentSpace* space = FindSpace(space_id);
  ResolveHelp(
      space_id,
      SuccessResult(
          base::DictValue()
              .Set("success", false)
              .Set("space_id", space_id)
              .Set("state", space ? std::string(DaoAgentSpaceStateToString(
                                        space->state()))
                                  : std::string("stopped"))
              .Set("timed_out", true)
              .Set("message",
                   "The user has not handed control back yet. Stop here, "
                   "tell the user what they need to do in the space, and "
                   "wait for them to say they are done.")));
}

void DaoAgentSpaceService::ResolveHelp(std::string_view space_id,
                                       DaoBrowserToolResult result) {
  auto it = helps_.find(space_id);
  if (it == helps_.end()) {
    return;
  }
  std::unique_ptr<PendingHelp> help = std::move(it->second);
  helps_.erase(it);
  help->timer.Stop();
  std::move(help->callback).Run(std::move(result));
}

DaoAgentSpace* DaoAgentSpaceService::ResolveSpace(
    const DaoAgentSpaceClient& client,
    const base::DictValue& arguments,
    DaoToolError* error) {
  const std::string* space_id = arguments.FindString("space_id");
  if (!space_id || space_id->empty()) {
    *error = MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                              "space_id is required.");
    return nullptr;
  }
  DaoAgentSpace* space = FindSpace(*space_id);
  if (!space || space->client() != client.type) {
    *error = MakeDaoToolError(
        DaoToolErrorCode::kSpaceNotFound,
        "No space with this space_id. Call list_spaces to see open spaces, "
        "or create_space to start a new one.");
    return nullptr;
  }
  if (space->is_closed()) {
    *error = ClosedError(*space);
    return nullptr;
  }
  return space;
}

void DaoAgentSpaceService::CancelSpaceCalls(std::string_view space_id,
                                            const DaoToolError& error) {
  std::vector<std::string> ids;
  for (const auto& [internal_id, call] : calls_) {
    if (call->space_id == space_id) {
      ids.push_back(internal_id);
    }
  }
  base::WeakPtr<DaoAgentSpaceService> weak_this = weak_factory_.GetWeakPtr();
  for (const std::string& internal_id : ids) {
    DaoAgentSpace* space = FindSpace(space_id);
    if (space && space->executor()) {
      space->executor()->Cancel(internal_id, error);
    } else {
      OnSpaceToolComplete(internal_id, ErrorResult(error));
    }
    if (!weak_this) {
      return;
    }
  }
}

void DaoAgentSpaceService::CloseSpace(
    DaoAgentSpace* space,
    DaoAgentSpaceState state,
    std::string summary,
    const std::vector<std::string>& keep_tab_ids,
    bool window_closing) {
  if (!space || space->is_closed()) {
    return;
  }
  const std::string space_id = space->id();
  CancelSpaceCalls(space_id, ClosedError(*space));
  space = FindSpace(space_id);
  if (!space) {
    return;
  }
  BrowserWindowInterface* browser = window_closing ? nullptr : space->browser();
  std::vector<std::unique_ptr<content::WebContents>> kept;
  std::vector<std::unique_ptr<content::WebContents>> discarded;
  space->MarkClosed(state, std::move(summary));
  ResolveHelp(space_id, ErrorResult(ClosedError(*space)));
  CloseUserAlert(space_id);

  // Detaching pages before closing skips beforeunload prompts, which would
  // otherwise block the hidden window from closing.
  TabListInterface* tab_list =
      browser ? TabListInterface::From(browser) : nullptr;
  if (tab_list && !tab_list->IsClosingAllTabs()) {
    std::vector<tabs::TabHandle> handles;
    for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
      handles.push_back(tab->GetHandle());
    }
    for (const tabs::TabHandle& handle : handles) {
      tabs::TabInterface* tab = handle.Get();
      if (!tab) {
        continue;
      }
      const bool keep = std::ranges::contains(
          keep_tab_ids, GetSidebarTabId(tab->GetContents()));
      std::unique_ptr<content::WebContents> contents =
          tab_list->DetachWebContents(handle);
      if (!contents) {
        continue;
      }
      DaoAgentLockTabHelper::UnlockContents(contents.get());
      (keep ? kept : discarded).push_back(std::move(contents));
    }
  }
  discarded.clear();

  if (!kept.empty()) {
    ProfileBrowserCollection* collection =
        ProfileBrowserCollection::GetForProfile(profile_);
    BrowserWindowInterface* target =
        collection ? collection->FindTabbedBrowser(
                         /*match_original_profiles=*/false)
                   : nullptr;
    if (!target) {
      target = CreateBrowserWindow(
          BrowserWindowCreateParams(*profile_, /*from_user_gesture=*/false));
      if (target) {
        target->GetWindow()->Show();
      }
    }
    TabListInterface* target_tabs =
        target ? TabListInterface::From(target) : nullptr;
    base::ListValue folder_tabs;
    for (std::unique_ptr<content::WebContents>& contents : kept) {
      if (!target_tabs) {
        continue;
      }
      const GURL url = contents->GetVisibleURL();
      const std::string title = base::UTF16ToUTF8(contents->GetTitle());
      std::string tab_id;
      if (contents->GetBrowserContext() == profile_) {
        tab_id = GetOrCreateSidebarTabId(contents.get());
        target_tabs->InsertWebContentsAt(0, std::move(contents),
                                         /*should_pin=*/false, std::nullopt);
      } else if (tabs::TabInterface* tab =
                     target_tabs->OpenTab(url, 0, /*foreground=*/false)) {
        // Pages of a clean space belong to its temporary profile; the
        // user's window gets the same address instead.
        tab_id = GetOrCreateSidebarTabId(tab->GetContents());
      }
      if (!tab_id.empty()) {
        folder_tabs.Append(base::DictValue()
                               .Set("tabId", tab_id)
                               .Set("url", url.spec())
                               .Set("title", title));
      }
    }
    // Results gather in one sidebar folder instead of mixing with the
    // user's tabs.
    if (!folder_tabs.empty()) {
      GetFolderFileTaskRunner()->PostTaskAndReplyWithResult(
          FROM_HERE,
          base::BindOnce(
              &AddTabsToFolderFile,
              profile_->GetPath().AppendASCII("dao_folders.json"),
              std::string(kResultsFolderId),
              l10n_util::GetStringUTF8(IDS_DAO_AGENT_SPACE_RESULTS_FOLDER),
              std::move(folder_tabs)),
          base::BindOnce(
              [](base::WeakPtr<Profile> profile, bool updated) {
                if (updated && profile) {
                  DaoSidebarUIHandler::NotifyFolderDataChanged(profile.get());
                }
              },
              profile_->GetWeakPtr()));
    }
  }
  PruneClosedRecords();
  SaveHistory();
  NotifySpaceChanged();
}

void DaoAgentSpaceService::PruneClosedRecords() {
  std::vector<std::string> closed;
  for (const std::string& id : space_order_) {
    DaoAgentSpace* space = FindSpace(id);
    if (space && space->is_closed()) {
      closed.push_back(id);
    }
  }
  while (closed.size() > kMaxClosedRecords) {
    const std::string id = closed.front();
    closed.erase(closed.begin());
    Dismiss(id);
  }
}

void DaoAgentSpaceService::CheckIdleSpaces() {
  const base::TimeTicks now = base::TimeTicks::Now();
  std::vector<std::string> idle;
  for (DaoAgentSpace* space : GetSpaces()) {
    if (space->state() != DaoAgentSpaceState::kRunning ||
        now - space->last_activity() < idle_timeout_) {
      continue;
    }
    const bool busy = std::ranges::any_of(calls_, [&](const auto& entry) {
      return entry.second->space_id == space->id();
    });
    if (!busy) {
      idle.push_back(space->id());
    }
  }
  for (const std::string& id : idle) {
    if (DaoAgentSpace* space = FindSpace(id)) {
      space->set_stopped_for_inactivity();
      CloseSpace(space, DaoAgentSpaceState::kStopped, std::string(), {});
    }
  }
  if (GetActiveSpaceCount() == 0) {
    idle_timer_.Stop();
  }
}

void DaoAgentSpaceService::AlertUser(DaoAgentSpace* space,
                                     const std::u16string& message) {
  ShowToast(profile_, message);
  NotificationDisplayService* display =
      NotificationDisplayServiceFactory::GetForProfile(profile_);
  if (!display) {
    return;
  }
  auto on_click = base::BindRepeating(
      [](base::WeakPtr<DaoAgentSpaceService> service, std::string space_id,
         std::optional<int> button_index) {
        if (service) {
          service->ShowWindow(space_id);
          service->CloseUserAlert(space_id);
        }
      },
      weak_factory_.GetWeakPtr(), space->id());
  message_center::Notification notification(
      message_center::NOTIFICATION_TYPE_SIMPLE,
      base::StrCat({kNotificationIdPrefix, space->id()}),
      base::UTF8ToUTF16(space->name()), message, ui::ImageModel(),
      l10n_util::GetStringUTF16(IDS_DAO_AGENT_SPACES_TITLE), GURL(),
      message_center::NotifierId(message_center::NotifierType::SYSTEM_COMPONENT,
                                 kNotifierId),
      message_center::RichNotificationData(),
      base::MakeRefCounted<message_center::HandleNotificationClickDelegate>(
          std::move(on_click)));
  display->Display(NotificationHandler::Type::TRANSIENT, notification,
                   /*metadata=*/nullptr);
}

void DaoAgentSpaceService::CloseUserAlert(std::string_view space_id) {
  if (NotificationDisplayService* display =
          NotificationDisplayServiceFactory::GetForProfile(profile_)) {
    display->Close(NotificationHandler::Type::TRANSIENT,
                   base::StrCat({kNotificationIdPrefix, space_id}));
  }
}

void DaoAgentSpaceService::FlushNotifications() {
  for (Observer& observer : observers_) {
    observer.OnAgentSpacesChanged();
  }
}

}  // namespace dao
