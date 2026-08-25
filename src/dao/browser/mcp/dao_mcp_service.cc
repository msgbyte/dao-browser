// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_service.h"

#include <unistd.h>

#include <utility>
#include <vector>

#include "base/apple/bundle_locations.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_paths.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "crypto/secure_util.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_target_policy.h"
#include "dao/browser/automation/dao_browser_tool_catalog.h"
#include "dao/browser/automation/dao_browser_tool_executor.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "dao/browser/automation/dao_page_tools.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/mcp/dao_mcp_runtime_files.h"
#include "dao/browser/mcp/dao_mcp_session_lifecycle_monitor.h"
#include "dao/browser/ui/views/dao_agent_cursor_view.h"
#include "dao/browser/ui/views/dao_mcp_approval_dialog.h"
#include "net/base/net_errors.h"

namespace dao {
namespace {

constexpr size_t kMaxClientLabelBytes = 256;
constexpr size_t kMaxPendingToolCalls = 64;
constexpr size_t kMaxPendingToolCallBytes = kDaoMcpMaxLineBytes;
constexpr base::TimeDelta kHelloTimeout = base::Seconds(5);
constexpr base::TimeDelta kApprovalTimeout = base::Seconds(30);
constexpr base::TimeDelta kLeaseRetryDelay = base::Milliseconds(50);

DaoToolError AuthorizationDenied() {
  return MakeDaoToolError(DaoToolErrorCode::kAuthorizationDenied,
                          "The MCP browser connection was not authorized.");
}

DaoToolError InvalidRequest(std::string message) {
  return MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                          std::move(message));
}

std::string SideEffectName(DaoBrowserToolSideEffect side_effect) {
  switch (side_effect) {
    case DaoBrowserToolSideEffect::kRead:
      return "read";
    case DaoBrowserToolSideEffect::kInteraction:
      return "interaction";
    case DaoBrowserToolSideEffect::kDestructive:
      return "destructive";
  }
  return "read";
}

}  // namespace

class DaoMcpPageUiDelegate final : public DaoPageTools::UiDelegate {
 public:
  DaoMcpPageUiDelegate() = default;
  ~DaoMcpPageUiDelegate() override = default;

  void MoveCursor(content::WebContents* target,
                  double x,
                  double y,
                  base::OnceCallback<void(bool)> callback) override {
    Browser* browser = target ? chrome::FindBrowserWithTab(target) : nullptr;
    BrowserView* browser_view =
        browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
    auto* cursor_view =
        browser_view ? browser_view->dao_agent_cursor() : nullptr;
    if (!target || !browser_view || !cursor_view) {
      std::move(callback).Run(false);
      return;
    }
    if (!CanAnimateAgentCursorForTarget(target)) {
      cursor_view->Hide();
      std::move(callback).Run(false);
      return;
    }

    gfx::Rect viewport_screen;
    if (target->GetRenderWidgetHostView()) {
      viewport_screen = target->GetRenderWidgetHostView()->GetViewBounds();
    } else {
      viewport_screen = browser_view->contents_container()->GetBoundsInScreen();
    }
    const gfx::Rect cursor_bounds = cursor_view->GetBoundsInScreen();
    const float view_x =
        static_cast<float>(viewport_screen.x() - cursor_bounds.x()) + x;
    const float view_y =
        static_cast<float>(viewport_screen.y() - cursor_bounds.y()) + y;
    if (!cursor_view->is_visible()) {
      cursor_view->ShowAtCenter();
    }
    cursor_view->AnimateTo(view_x, view_y,
                           base::BindOnce(
                               [](base::WeakPtr<content::WebContents> target,
                                  base::OnceCallback<void(bool)> done) {
                                 std::move(done).Run(
                                     target &&
                                     CanAnimateAgentCursorForTarget(
                                         target.get()));
                               },
                               target->GetWeakPtr(),
                               std::move(callback)));
  }

  void PlayClickRipple(content::WebContents* target) override {
    Browser* browser = target ? chrome::FindBrowserWithTab(target) : nullptr;
    BrowserView* browser_view =
        browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
    auto* cursor_view =
        browser_view ? browser_view->dao_agent_cursor() : nullptr;
    if (!cursor_view) {
      return;
    }
    if (!CanAnimateAgentCursorForTarget(target)) {
      cursor_view->Hide();
      return;
    }
    cursor_view->PlayClickRipple();
  }

  void CancelCursor(content::WebContents* target) override {
    if (!target) {
      return;
    }
    for (BrowserWindowInterface* browser_window :
         GetAllBrowserWindowInterfaces()) {
      Browser* browser = browser_window
                             ? browser_window->GetBrowserForMigrationOnly()
                             : nullptr;
      if (!browser || browser->profile() != target->GetBrowserContext()) {
        continue;
      }
      BrowserView* browser_view =
          BrowserView::GetBrowserViewForBrowser(browser);
      if (browser_view && browser_view->dao_agent_cursor()) {
        browser_view->dao_agent_cursor()->Hide();
      }
    }
  }

  bool IsTargetLocked(content::WebContents* target) override {
    return target && DaoAgentLockTabHelper::IsLocked(target);
  }

  void LockTarget(content::WebContents* target) override {
    if (target) {
      DaoAgentLockTabHelper::LockContents(target);
    }
  }

  void UnlockTarget(content::WebContents* target) override {
    if (target) {
      DaoAgentLockTabHelper::UnlockContents(target);
    }
  }
};

std::string BuildDaoMcpConfigurationForBundle(
    const base::FilePath& bundle_path) {
  const base::FilePath helper_path =
      bundle_path.Append(FILE_PATH_LITERAL("Contents"))
          .Append(FILE_PATH_LITERAL("Helpers"))
          .Append(FILE_PATH_LITERAL("dao-mcp"));
  const base::FilePath user_data_dir =
      base::PathService::CheckedGet(chrome::DIR_USER_DATA);

  base::DictValue server;
  server.Set("command", helper_path.AsUTF8Unsafe());
  base::ListValue args;
  args.Append(base::StrCat(
      {"--user-data-dir=", user_data_dir.AsUTF8Unsafe()}));
  server.Set("args", std::move(args));
  base::DictValue servers;
  servers.Set("dao", std::move(server));
  base::DictValue root;
  root.Set("mcpServers", std::move(servers));
  return base::WriteJson(root).value_or("{}");
}

DaoMcpServiceStatus::DaoMcpServiceStatus() = default;
DaoMcpServiceStatus::~DaoMcpServiceStatus() = default;
DaoMcpServiceStatus::DaoMcpServiceStatus(const DaoMcpServiceStatus&) = default;
DaoMcpServiceStatus& DaoMcpServiceStatus::operator=(
    const DaoMcpServiceStatus&) = default;
DaoMcpServiceStatus::DaoMcpServiceStatus(DaoMcpServiceStatus&&) = default;
DaoMcpServiceStatus& DaoMcpServiceStatus::operator=(DaoMcpServiceStatus&&) =
    default;

DaoMcpService::PendingToolCall::PendingToolCall() = default;
DaoMcpService::PendingToolCall::~PendingToolCall() = default;
DaoMcpService::PendingToolCall::PendingToolCall(PendingToolCall&&) noexcept =
    default;
DaoMcpService::PendingToolCall& DaoMcpService::PendingToolCall::operator=(
    PendingToolCall&&) noexcept = default;

// static
DaoMcpService* DaoMcpService::Get() {
  static base::NoDestructor<DaoMcpService> instance;
  return instance.get();
}

DaoMcpService::DaoMcpService()
    : hello_timeout_(kHelloTimeout),
      approval_timeout_(kApprovalTimeout),
      page_ui_delegate_(std::make_unique<DaoMcpPageUiDelegate>()),
      devtools_client_(std::make_unique<DaoDevToolsClient>()),
      tool_executor_(
          std::make_unique<DaoBrowserToolExecutor>(devtools_client_.get(),
                                                   page_ui_delegate_.get())) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

DaoMcpService::~DaoMcpService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Shutdown();
}

void DaoMcpService::Initialize(PrefService* local_state,
                               base::FilePath user_data_dir) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (initialized_) {
    return;
  }
  if (!local_state || user_data_dir.empty()) {
    return;
  }
  initialized_ = true;
  shutting_down_ = false;
  approval_delegate_ = DaoMcpApprovalDialogController::Get();
  local_state_ = local_state;
  user_data_dir_ = std::move(user_data_dir);
  runtime_files_ = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      user_data_dir_, base::GetCurrentProcId());
  runtime_task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
  pref_change_registrar_.Init(local_state_);
  pref_change_registrar_.Add(
      prefs::kDaoMcpServerEnabled,
      base::BindRepeating(&DaoMcpService::OnEnabledPrefChanged,
                          weak_factory_.GetWeakPtr()));
  OnEnabledPrefChanged();
}

void DaoMcpService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!initialized_) {
    return;
  }
  shutting_down_ = true;
  pref_change_registrar_.Reset();
  StopListening();
  approval_delegate_ = nullptr;
  runtime_files_.reset();
  runtime_task_runner_.reset();
  local_state_ = nullptr;
  user_data_dir_.clear();
  initialized_ = false;
  shutting_down_ = false;
}

void DaoMcpService::SetEnabled(bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!initialized_ || !local_state_) {
    return;
  }
  if (local_state_->GetBoolean(prefs::kDaoMcpServerEnabled) == enabled) {
    OnEnabledPrefChanged();
    return;
  }
  local_state_->SetBoolean(prefs::kDaoMcpServerEnabled, enabled);
}

DaoMcpServiceStatus DaoMcpService::GetStatus() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return status_;
}

std::string DaoMcpService::GetMcpConfiguration() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return BuildDaoMcpConfigurationForBundle(base::apple::OuterBundlePath());
}

Browser* DaoMcpService::GetAuthorizedBrowser() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (status_.state != DaoMcpStatus::kLeaseActive || !external_lease_ ||
      !session_) {
    return nullptr;
  }
  BrowserWindowInterface* browser_window = session_->browser_window();
  return browser_window ? browser_window->GetBrowserForMigrationOnly()
                        : nullptr;
}

content::WebContents* DaoMcpService::GetAuthorizedTarget() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!GetAuthorizedBrowser()) {
    return nullptr;
  }
  auto target = session_->ResolveTarget();
  return target.has_value() ? target.value() : nullptr;
}

void DaoMcpService::StopControl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connection_active_) {
    return;
  }
  RejectConnection(MakeDaoToolError(
      DaoToolErrorCode::kToolCancelled,
      "External MCP browser control was stopped by the user."));
}

base::CallbackListSubscription DaoMcpService::AddObserver(
    StatusObserver observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return status_observers_.Add(std::move(observer));
}

void DaoMcpService::SetApprovalDelegate(DaoMcpApprovalDelegate* delegate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  approval_delegate_ = delegate;
}

void DaoMcpService::SetTimeoutsForTesting(base::TimeDelta hello_timeout,
                                          base::TimeDelta approval_timeout) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  hello_timeout_ = hello_timeout;
  approval_timeout_ = approval_timeout;
}

void DaoMcpService::SetDevToolsCommandCallbackForTesting(
    base::RepeatingCallback<void(const std::string&)> callback) {
  devtools_client_->SetCommandCallbackForTesting(std::move(callback));
}

size_t DaoMcpService::active_tool_call_count_for_testing() const {
  return active_tool_calls_.size();
}

size_t DaoMcpService::pending_executor_call_count_for_testing() const {
  return tool_executor_->pending_count_for_testing();
}

size_t DaoMcpService::pending_devtools_command_count_for_testing() const {
  return devtools_client_->pending_command_count_for_testing();
}

bool DaoMcpService::devtools_attached_for_testing() const {
  return devtools_client_->agent_host() != nullptr;
}

size_t DaoMcpService::page_operation_count_for_testing() const {
  return tool_executor_->page_operation_count_for_testing();
}

size_t DaoMcpService::page_lock_count_for_testing() const {
  return tool_executor_->page_lock_count_for_testing();
}

size_t DaoMcpService::page_highlight_count_for_testing() const {
  return tool_executor_->page_highlight_count_for_testing();
}

size_t DaoMcpService::page_cursor_count_for_testing() const {
  return tool_executor_->page_cursor_count_for_testing();
}

void DaoMcpService::TrackPageCursorForTesting(content::WebContents* target) {
  tool_executor_->TrackPageCursorForTesting(target);
}

void DaoMcpService::OnEnabledPrefChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!initialized_ || shutting_down_ || !local_state_) {
    return;
  }
  if (local_state_->GetBoolean(prefs::kDaoMcpServerEnabled)) {
    if (!listener_active_ && !listener_start_pending_) {
      StartListening();
    }
    return;
  }
  StopListening();
}

void DaoMcpService::StartListening() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (listener_active_ || listener_start_pending_ || !runtime_files_ ||
      !runtime_task_runner_) {
    return;
  }

  listener_start_pending_ = true;
  const uint64_t runtime_generation = ++runtime_generation_;
  runtime_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&DaoMcpRuntimeFiles::Prepare, runtime_files_),
      base::BindOnce(&DaoMcpService::OnRuntimePrepared,
                     weak_factory_.GetWeakPtr(), runtime_generation));
}

void DaoMcpService::OnRuntimePrepared(
    uint64_t runtime_generation,
    base::expected<void, DaoToolError> prepared) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation != runtime_generation_) {
    return;
  }
  if (!prepared.has_value()) {
    listener_start_pending_ = false;
    UpdateStatus(DaoMcpStatus::kDisabled);
    return;
  }

  const scoped_refptr<base::SequencedTaskRunner> ui_task_runner =
      base::SequencedTaskRunner::GetCurrentDefault();
  transport_.emplace(
      content::GetIOThreadTaskRunner({}),
      base::BindPostTask(
          ui_task_runner,
          base::BindRepeating(&DaoMcpService::OnTransportAccepted,
                              weak_factory_.GetWeakPtr(), runtime_generation)),
      base::BindPostTask(
          ui_task_runner,
          base::BindRepeating(&DaoMcpService::OnTransportRequest,
                              weak_factory_.GetWeakPtr(), runtime_generation)),
      base::BindPostTask(
          ui_task_runner,
          base::BindRepeating(&DaoMcpService::OnConnectionClosed,
                              weak_factory_.GetWeakPtr(), runtime_generation)));
  transport_.AsyncCall(&DaoMcpTransport::StartListening)
      .WithArgs(runtime_files_->socket_path())
      .Then(base::BindOnce(&DaoMcpService::OnTransportListening,
                           weak_factory_.GetWeakPtr(), runtime_generation));
}

void DaoMcpService::OnTransportListening(uint64_t runtime_generation,
                                         int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation != runtime_generation_) {
    return;
  }
  if (result != net::OK) {
    listener_start_pending_ = false;
    transport_.Reset();
    QueueRuntimeCleanup();
    UpdateStatus(DaoMcpStatus::kDisabled);
    return;
  }
  runtime_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<DaoMcpRuntimeFiles> runtime_files)
              -> base::expected<void, DaoToolError> {
            auto captured = runtime_files->CaptureBoundSocket();
            if (!captured.has_value()) {
              return captured;
            }
            return runtime_files->Publish();
          },
          runtime_files_),
      base::BindOnce(&DaoMcpService::OnRuntimePublished,
                     weak_factory_.GetWeakPtr(), runtime_generation));
}

void DaoMcpService::OnRuntimePublished(
    uint64_t runtime_generation,
    base::expected<void, DaoToolError> published) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation != runtime_generation_) {
    return;
  }
  listener_start_pending_ = false;
  if (!published.has_value()) {
    if (!transport_.is_null()) {
      transport_.AsyncCall(&DaoMcpTransport::Stop);
      transport_.Reset();
    }
    QueueRuntimeCleanup();
    UpdateStatus(DaoMcpStatus::kDisabled);
    return;
  }

  listener_active_ = true;
  transport_.AsyncCall(&DaoMcpTransport::StartAccepting);
  UpdateStatus(DaoMcpStatus::kListening);
}

void DaoMcpService::QueueRuntimeCleanup() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!runtime_files_ || !runtime_task_runner_) {
    return;
  }
  runtime_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&DaoMcpRuntimeFiles::Cleanup, runtime_files_));
}

void DaoMcpService::StopListening() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++runtime_generation_;
  listener_start_pending_ = false;
  listener_active_ = false;
  connection_active_ = false;
  connection_closing_ = false;
  active_connection_generation_ = 0;
  if (!transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::Stop);
    transport_.Reset();
  }
  ResetConnectionState();
  QueueRuntimeCleanup();
  UpdateStatus(DaoMcpStatus::kDisabled);
}

void DaoMcpService::OnTransportAccepted(
    uint64_t runtime_generation,
    uint64_t connection_generation,
    std::optional<base::ProcessId> verified_pid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation != runtime_generation_ || !listener_active_ ||
      connection_active_) {
    return;
  }
  connection_active_ = true;
  connection_closing_ = false;
  active_connection_generation_ = connection_generation;
  accepted_verified_pid_ = verified_pid;
  hello_timer_.Start(
      FROM_HERE, hello_timeout_,
      base::BindOnce(&DaoMcpService::OnHelloTimeout, weak_factory_.GetWeakPtr(),
                     connection_generation));
}

void DaoMcpService::OnTransportRequest(uint64_t runtime_generation,
                                       uint64_t connection_generation,
                                       DaoMcpRequest request,
                                       size_t wire_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation == runtime_generation_ &&
      connection_generation == active_connection_generation_ &&
      connection_active_ && !connection_closing_) {
    OnRequest(std::move(request));
  }
  if (runtime_generation == runtime_generation_ && !transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::AcknowledgeRequest)
        .WithArgs(connection_generation, wire_bytes);
  }
}

void DaoMcpService::OnConnectionClosed(uint64_t runtime_generation,
                                       uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation != runtime_generation_ ||
      connection_generation != active_connection_generation_) {
    return;
  }
  active_connection_generation_ = 0;
  connection_active_ = false;
  connection_closing_ = false;
  ResetConnectionState();
  UpdateStatus(listener_active_ ? DaoMcpStatus::kListening
                                : DaoMcpStatus::kDisabled);
}

void DaoMcpService::ResetConnectionState() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  connection_closing_ = false;
  hello_timer_.Stop();
  approval_timer_.Stop();
  lease_retry_timer_.Stop();
  if (approval_delegate_ && !connection_id_.empty() &&
      approval_state_ == ApprovalState::kPending) {
    approval_delegate_->CancelApproval(connection_id_);
  }
  if (tool_executor_) {
    tool_executor_->CancelAll(
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "The MCP browser connection was closed."));
    tool_executor_->ClearSessionState(session_.get());
  }
  if (devtools_client_) {
    devtools_client_->Detach();
  }
  pending_tool_calls_.clear();
  active_tool_calls_.clear();
  active_tool_call_bytes_.clear();
  active_tool_call_bytes_total_ = 0;
  session_lifecycle_monitor_.reset();
  external_lease_.reset();
  session_.reset();
  client_info_.reset();
  connection_id_.clear();
  approval_state_ = ApprovalState::kNotRequested;
  approval_deadline_ = base::TimeTicks();
}

void DaoMcpService::OnHelloTimeout(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (connection_generation != active_connection_generation_ ||
      !connection_active_ || client_info_) {
    return;
  }
  SendError(std::nullopt,
            MakeDaoToolError(DaoToolErrorCode::kAuthorizationTimeout,
                             "The MCP browser connection handshake timed out.",
                             true));
  CloseConnectionAfterWrites();
}

void DaoMcpService::OnRequest(DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connection_active_) {
    return;
  }
  if (!client_info_ && request.method != "hello") {
    SendError(request.id,
              InvalidRequest("hello must be the first IPC request."));
    CloseConnectionAfterWrites();
  } else if (request.method == "hello") {
    HandleHello(std::move(request));
  } else if (request.method == "tools/list") {
    HandleToolsList(std::move(request));
  } else if (request.method == "tools/call") {
    HandleToolsCall(std::move(request));
  } else if (request.method == "tools/cancel") {
    HandleToolsCancel(std::move(request));
  } else {
    SendError(request.id,
              InvalidRequest("The browser IPC method is unsupported."));
  }
}

void DaoMcpService::HandleHello(DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!request.id) {
    SendError(request.id, InvalidRequest("hello requires a request id."));
    CloseConnectionAfterWrites();
    return;
  }
  if (client_info_ || session_ ||
      approval_state_ != ApprovalState::kNotRequested) {
    SendError(request.id,
              InvalidRequest("hello has already completed for this socket."));
    CloseConnectionAfterWrites();
    return;
  }

  const std::string* nonce = request.params.FindString("nonce");
  const base::DictValue* client = request.params.FindDict("client");
  const std::string* name = client ? client->FindString("name") : nullptr;
  const std::string* version = client ? client->FindString("version") : nullptr;
  const std::string& expected_nonce = runtime_files_->nonce();
  const bool nonce_matches =
      nonce && nonce->size() == expected_nonce.size() &&
      crypto::SecureMemEqual(base::as_byte_span(*nonce),
                             base::as_byte_span(expected_nonce));
  if (!nonce_matches) {
    SendError(request.id, AuthorizationDenied());
    CloseConnectionAfterWrites();
    return;
  }
  if (!name || name->empty() || name->size() > kMaxClientLabelBytes ||
      !version || version->empty() || version->size() > kMaxClientLabelBytes) {
    SendError(
        request.id,
        InvalidRequest("hello requires bounded client name and version."));
    CloseConnectionAfterWrites();
    return;
  }

  client_info_ = DaoMcpClientInfo{
      .name = *name,
      .version = *version,
      .verified_pid = accepted_verified_pid_,
  };
  connection_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  hello_timer_.Stop();
  SendSuccess(
      *request.id,
      base::DictValue()
          .Set("connection_id", connection_id_)
          .Set("status", "ready"));
}

void DaoMcpService::HandleToolsList(DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!request.id) {
    SendError(request.id, InvalidRequest("tools/list requires a request id."));
    return;
  }
  if (!client_info_) {
    SendError(request.id, AuthorizationDenied());
    return;
  }

  base::ListValue serialized_tools;
  for (const DaoBrowserToolDefinition* definition :
       DaoBrowserToolCatalog::Get()->List(DaoToolClient::kMcp)) {
    base::DictValue tool =
        base::DictValue()
            .Set("name", definition->name)
            .Set("description", definition->description)
            .Set("inputSchema", definition->input_schema.Clone())
            .Set("sideEffect", SideEffectName(definition->side_effect))
            .Set("timeoutMs",
                 static_cast<int>(definition->timeout.InMilliseconds()));
    if (definition->output_schema) {
      tool.Set("outputSchema", definition->output_schema->Clone());
    }
    serialized_tools.Append(std::move(tool));
  }
  SendSuccess(*request.id,
              base::DictValue().Set("tools", std::move(serialized_tools)));
}

void DaoMcpService::HandleToolsCall(DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!request.id) {
    SendError(request.id, InvalidRequest("tools/call requires a request id."));
    return;
  }
  if (!client_info_) {
    SendError(request.id, AuthorizationDenied());
    return;
  }
  if (active_tool_calls_.contains(*request.id)) {
    SendError(request.id,
              InvalidRequest("The browser tool request id is already active."));
    return;
  }
  if (active_tool_calls_.size() >= kMaxPendingToolCalls) {
    SendError(request.id,
              MakeDaoToolError(
                  DaoToolErrorCode::kInternalError,
                  "Too many MCP browser tool calls are outstanding.", true));
    return;
  }

  const std::string* name = request.params.FindString("name");
  const base::DictValue* arguments = request.params.FindDict("arguments");
  const DaoBrowserToolDefinition* definition =
      name ? DaoBrowserToolCatalog::Get()->Find(*name, DaoToolClient::kMcp)
           : nullptr;
  if (!name || !arguments) {
    SendError(request.id,
              InvalidRequest("tools/call requires name and object arguments."));
    return;
  }
  if (!definition) {
    SendError(request.id,
              MakeDaoToolError(DaoToolErrorCode::kUnknownTool,
                               "Unknown or unavailable MCP browser tool."));
    return;
  }
  if (approval_state_ == ApprovalState::kDenied) {
    SendError(request.id, AuthorizationDenied());
    return;
  }

  std::string serialized_arguments;
  if (!base::JSONWriter::Write(*arguments, &serialized_arguments) ||
      serialized_arguments.size() >
          kMaxPendingToolCallBytes - active_tool_call_bytes_total_) {
    SendError(request.id,
              MakeDaoToolError(DaoToolErrorCode::kInternalError,
                               "The pending MCP tool-call budget is exhausted.",
                               true));
    return;
  }

  if (approval_state_ == ApprovalState::kNotRequested) {
    auto approval_browser = PrepareApprovalSession();
    if (!approval_browser.has_value()) {
      SendError(request.id, std::move(approval_browser).error());
      return;
    }
    RequestApproval(*approval_browser);
    if (connection_closing_ || !client_info_ || !session_ ||
        approval_state_ == ApprovalState::kDenied) {
      return;
    }
  }

  PendingToolCall pending;
  pending.call.request_id = *request.id;
  pending.call.name = *name;
  pending.call.arguments = arguments->Clone();
  pending.call.timeout = definition->timeout;
  pending.buffered_bytes = serialized_arguments.size();
  active_tool_calls_.insert(*request.id);
  active_tool_call_bytes_.emplace(*request.id, pending.buffered_bytes);
  active_tool_call_bytes_total_ += pending.buffered_bytes;
  pending_tool_calls_.emplace(*request.id, std::move(pending));
  if (approval_state_ == ApprovalState::kAllowed && external_lease_) {
    DispatchPendingCalls();
  }
}

void DaoMcpService::HandleToolsCancel(DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string* request_id = request.params.FindString("request_id");
  if (!request_id || request_id->empty()) {
    if (request.id) {
      SendError(
          request.id,
          InvalidRequest("tools/cancel requires a non-empty request_id."));
    }
    return;
  }

  auto pending = pending_tool_calls_.find(*request_id);
  if (pending != pending_tool_calls_.end()) {
    auto bytes = active_tool_call_bytes_.find(*request_id);
    if (bytes != active_tool_call_bytes_.end()) {
      active_tool_call_bytes_total_ -= bytes->second;
      active_tool_call_bytes_.erase(bytes);
    }
    pending_tool_calls_.erase(pending);
    active_tool_calls_.erase(*request_id);
    SendError(std::optional<std::string>(*request_id),
              MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                               "The browser tool call was cancelled."));
  } else if (tool_executor_) {
    tool_executor_->Cancel(*request_id);
  }
  if (request.id && connection_active_) {
    SendSuccess(*request.id, base::DictValue().Set("cancelled", true));
  }
}

void DaoMcpService::SendSuccess(std::string id, base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (connection_active_ && !connection_closing_ && !transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::SendSuccess)
        .WithArgs(active_connection_generation_, std::move(id),
                  std::move(result));
  }
}

void DaoMcpService::SendError(const std::optional<std::string>& id,
                              DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (connection_active_ && !connection_closing_ && !transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::SendError)
        .WithArgs(active_connection_generation_, id, std::move(error));
  }
}

void DaoMcpService::CloseConnectionAfterWrites() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connection_active_ || connection_closing_) {
    return;
  }
  connection_closing_ = true;
  if (!transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::CloseAfterWrites)
        .WithArgs(active_connection_generation_);
  }
}

base::expected<Browser*, DaoToolError>
DaoMcpService::PrepareApprovalSession() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!client_info_ || session_ ||
      approval_state_ != ApprovalState::kNotRequested) {
    return base::unexpected(
        InvalidRequest("The MCP approval session is already initialized."));
  }

  BrowserWindowInterface* browser_window =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  Browser* browser =
      browser_window ? browser_window->GetBrowserForMigrationOnly() : nullptr;
  tabs::TabInterface* active_tab =
      browser_window ? browser_window->GetActiveTabInterface() : nullptr;
  content::WebContents* target =
      active_tab ? active_tab->GetContents() : nullptr;
  Profile* profile = browser ? browser->profile() : nullptr;
  auto target_policy = ValidateExternalTarget(browser, profile, target);
  if (!target_policy.has_value()) {
    return base::unexpected(std::move(target_policy).error());
  }

  auto session =
      std::make_unique<DaoBrowserAutomationSession>(browser_window, target);
  auto resolved_target = session->ResolveTarget();
  if (!resolved_target.has_value()) {
    return base::unexpected(std::move(resolved_target).error());
  }

  session_ = std::move(session);
  session_lifecycle_monitor_ = std::make_unique<DaoMcpSessionLifecycleMonitor>(
      session_.get(), base::BindOnce(&DaoMcpService::OnSessionInvalidated,
                                     weak_factory_.GetWeakPtr()));
  session_lifecycle_monitor_->Start();
  return browser;
}

void DaoMcpService::RequestApproval(Browser* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  approval_state_ = ApprovalState::kPending;
  approval_deadline_ = base::TimeTicks::Now() + approval_timeout_;
  UpdateStatus(DaoMcpStatus::kPendingApproval);
  if (!connection_active_ || approval_state_ != ApprovalState::kPending ||
      !client_info_ || !session_) {
    return;
  }
  BrowserWindowInterface* browser_window = session_->browser_window();
  Browser* approval_browser =
      browser_window ? browser_window->GetBrowserForMigrationOnly() : nullptr;
  if (!approval_browser || approval_browser != browser) {
    RejectConnection(MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The browser selected for MCP approval is no longer available."));
    return;
  }
  const uint64_t connection_generation = active_connection_generation_;
  const std::string connection_id = connection_id_;
  approval_timer_.Start(FROM_HERE, approval_timeout_,
                        base::BindOnce(&DaoMcpService::OnApprovalTimeout,
                                       weak_factory_.GetWeakPtr(),
                                       connection_generation, connection_id));
  if (!approval_delegate_) {
    RejectConnection(AuthorizationDenied());
    return;
  }
  DaoMcpClientInfo client = *client_info_;
  approval_delegate_->RequestApproval(
      client, approval_browser, connection_id,
      base::BindOnce(&DaoMcpService::OnApprovalResult,
                     weak_factory_.GetWeakPtr(), connection_generation,
                     connection_id));
}

void DaoMcpService::OnApprovalResult(uint64_t connection_generation,
                                     std::string connection_id,
                                     bool allowed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsCurrentApproval(connection_generation, connection_id)) {
    return;
  }
  if (!allowed) {
    RejectConnection(AuthorizationDenied());
    return;
  }
  approval_state_ = ApprovalState::kAllowed;
  TryAcquireExternalLease(connection_generation, std::move(connection_id));
}

void DaoMcpService::TryAcquireExternalLease(uint64_t connection_generation,
                                            std::string connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (connection_generation != active_connection_generation_ ||
      connection_id != connection_id_ || !connection_active_ ||
      approval_state_ != ApprovalState::kAllowed || external_lease_) {
    return;
  }
  if (!session_) {
    RejectConnection(MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The authorized browser session is no longer available."));
    return;
  }
  auto target = session_->ResolveTarget();
  if (!target.has_value()) {
    RejectConnection(std::move(target).error());
    return;
  }
  BrowserWindowInterface* browser_window = session_->browser_window();
  Browser* browser =
      browser_window ? browser_window->GetBrowserForMigrationOnly() : nullptr;
  auto target_policy = ValidateExternalTarget(browser, session_->profile(), *target);
  if (!target_policy.has_value()) {
    RejectConnection(std::move(target_policy).error());
    return;
  }
  Profile* profile = session_->profile();
  if (!profile) {
    RejectConnection(MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The authorized browser profile is no longer available."));
    return;
  }

  auto acquired = DaoAgentLeaseManager::GetForProfile(profile)->TryAcquire(
      {DaoToolClient::kMcp, connection_id_, client_info_->name});
  if (acquired.has_value()) {
    approval_timer_.Stop();
    external_lease_.emplace(std::move(acquired).value());
    UpdateStatus(DaoMcpStatus::kLeaseActive);
    DispatchPendingCalls();
    return;
  }

  const DaoToolError error = acquired.error();
  const bool retryable_busy = error.code == DaoToolErrorCode::kLeaseBusy ||
                              error.code == DaoToolErrorCode::kAgentControlBusy;
  if (retryable_busy && base::TimeTicks::Now() < approval_deadline_) {
    lease_retry_timer_.Start(
        FROM_HERE, kLeaseRetryDelay,
        base::BindOnce(&DaoMcpService::TryAcquireExternalLease,
                       weak_factory_.GetWeakPtr(), connection_generation,
                       std::move(connection_id)));
    return;
  }
  RejectConnection(error);
}

void DaoMcpService::OnApprovalTimeout(uint64_t connection_generation,
                                      std::string connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (connection_generation != active_connection_generation_ ||
      connection_id != connection_id_) {
    return;
  }
  if (approval_state_ != ApprovalState::kPending &&
      !(approval_state_ == ApprovalState::kAllowed && !external_lease_)) {
    return;
  }
  RejectConnection(
      MakeDaoToolError(DaoToolErrorCode::kAuthorizationTimeout,
                       "The MCP browser connection approval timed out.", true));
}

bool DaoMcpService::IsCurrentApproval(uint64_t connection_generation,
                                      std::string_view connection_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return connection_generation == active_connection_generation_ &&
         connection_id == connection_id_ && connection_active_ &&
         approval_state_ == ApprovalState::kPending;
}

void DaoMcpService::RejectConnection(DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  hello_timer_.Stop();
  approval_timer_.Stop();
  lease_retry_timer_.Stop();
  const bool cancel_pending_approval =
      approval_state_ == ApprovalState::kPending && !connection_id_.empty();
  approval_state_ = ApprovalState::kDenied;
  if (approval_delegate_ && cancel_pending_approval) {
    approval_delegate_->CancelApproval(connection_id_);
  }
  FailPendingCalls(error);
  if (tool_executor_) {
    tool_executor_->CancelAll(error);
    tool_executor_->ClearSessionState(session_.get());
  }
  if (devtools_client_) {
    devtools_client_->Detach();
  }
  // Active tool cancellation completes synchronously and writes its structured
  // result while the connection is still writable. Close only after those
  // exactly-once completions have been queued.
  CloseConnectionAfterWrites();
  pending_tool_calls_.clear();
  active_tool_calls_.clear();
  active_tool_call_bytes_.clear();
  active_tool_call_bytes_total_ = 0;
  session_lifecycle_monitor_.reset();
  external_lease_.reset();
  session_.reset();
  client_info_.reset();
  connection_id_.clear();
  approval_deadline_ = base::TimeTicks();
}

void DaoMcpService::OnSessionInvalidated(DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connection_active_ || connection_closing_) {
    return;
  }
  RejectConnection(std::move(error));
}

void DaoMcpService::FailPendingCalls(const DaoToolError& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::string> request_ids;
  request_ids.reserve(pending_tool_calls_.size());
  for (const auto& [request_id, _] : pending_tool_calls_) {
    request_ids.push_back(request_id);
  }
  pending_tool_calls_.clear();
  for (const std::string& request_id : request_ids) {
    active_tool_calls_.erase(request_id);
    auto bytes = active_tool_call_bytes_.find(request_id);
    if (bytes != active_tool_call_bytes_.end()) {
      active_tool_call_bytes_total_ -= bytes->second;
      active_tool_call_bytes_.erase(bytes);
    }
    SendError(std::optional<std::string>(request_id), error);
  }
}

void DaoMcpService::DispatchPendingCalls() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  while (connection_active_ && external_lease_ &&
         !pending_tool_calls_.empty()) {
    auto it = pending_tool_calls_.begin();
    PendingToolCall pending = std::move(it->second);
    pending_tool_calls_.erase(it);
    DispatchToolCall(std::move(pending));
  }
}

void DaoMcpService::DispatchToolCall(PendingToolCall pending) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string request_id = pending.call.request_id;
  tool_executor_->Execute(
      session_.get(), DaoToolClient::kMcp, std::move(pending.call),
      base::BindOnce(&DaoMcpService::OnToolCallComplete,
                     weak_factory_.GetWeakPtr(), request_id));
}

void DaoMcpService::OnToolCallComplete(std::string request_id,
                                       DaoBrowserToolResult result) {
  ++tool_call_completion_count_for_testing_;
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!active_tool_calls_.erase(request_id)) {
    return;
  }
  auto bytes = active_tool_call_bytes_.find(request_id);
  if (bytes != active_tool_call_bytes_.end()) {
    active_tool_call_bytes_total_ -= bytes->second;
    active_tool_call_bytes_.erase(bytes);
  }
  if (connection_active_) {
    SendSuccess(request_id, SerializeDaoBrowserToolResult(std::move(result)));
  }
  NotifyStatusObservers();
}

void DaoMcpService::NotifyStatusObservers() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  status_observers_.Notify(status_);
}

void DaoMcpService::UpdateStatus(DaoMcpStatus state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DaoMcpServiceStatus next;
  next.state = state;
  next.client = client_info_;
  const bool unchanged =
      next.state == status_.state &&
      ((!next.client && !status_.client) ||
       (next.client && status_.client &&
        next.client->name == status_.client->name &&
        next.client->version == status_.client->version &&
        next.client->verified_pid == status_.client->verified_pid));
  if (unchanged) {
    return;
  }
  status_ = std::move(next);
  NotifyStatusObservers();
}

}  // namespace dao
