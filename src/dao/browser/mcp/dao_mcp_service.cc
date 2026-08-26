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
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "net/base/net_errors.h"

namespace dao {
namespace {

constexpr size_t kMaxClientLabelBytes = 256;
constexpr size_t kMaxPendingToolCalls = 64;
constexpr size_t kMaxPendingToolCallBytes = kDaoMcpMaxLineBytes;
constexpr base::TimeDelta kHelloTimeout = base::Seconds(5);
constexpr base::TimeDelta kApprovalTimeout = base::Minutes(1);
constexpr base::TimeDelta kLeaseRetryDelay = base::Milliseconds(50);

DaoToolError AuthorizationDenied() {
  return MakeDaoToolError(DaoToolErrorCode::kAuthorizationDenied,
                          "The MCP browser connection was not authorized.");
}

DaoToolError InvalidRequest(std::string message) {
  return MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                          std::move(message));
}

DaoBrowserToolResult ErrorResult(DaoToolError error) {
  DaoBrowserToolResult result;
  result.error = std::move(error);
  return result;
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

struct DaoMcpService::TargetContext {
  TargetContext() = default;
  ~TargetContext() = default;

  std::unique_ptr<DaoBrowserAutomationSession> session;
  std::unique_ptr<DaoMcpSessionLifecycleMonitor> lifecycle_monitor;
  std::unique_ptr<DaoDevToolsClient> devtools_client;
  std::unique_ptr<DaoBrowserToolExecutor> tool_executor;
  std::optional<DaoAgentLease> lease;
};

struct DaoMcpService::ConnectionState {
  uint64_t generation = 0;
  bool closing = false;
  std::optional<base::ProcessId> verified_pid;
  ApprovalState approval_state = ApprovalState::kNotRequested;
  base::TimeTicks approval_deadline;
  base::OneShotTimer hello_timer;
  base::OneShotTimer approval_timer;
  base::OneShotTimer lease_retry_timer;
  std::optional<DaoMcpClientInfo> client_info;
  std::string connection_id;
  std::map<std::string, std::unique_ptr<TargetContext>, std::less<>>
      target_contexts;
  std::string default_target_id;
  std::map<std::string, PendingToolCall, std::less<>> pending_tool_calls;
  std::set<std::string, std::less<>> active_tool_calls;
  std::map<std::string, std::string, std::less<>> active_tool_targets;
  std::map<std::string, size_t, std::less<>> active_tool_call_bytes;
  size_t active_tool_call_bytes_total = 0;
  std::unique_ptr<DaoDevToolsClient> tab_tool_devtools_client;
  std::unique_ptr<DaoBrowserToolExecutor> tab_tool_executor;
  std::map<std::string, std::unique_ptr<DaoBrowserAutomationSession>,
           std::less<>>
      tab_tool_sessions;
};

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
      page_ui_delegate_(std::make_unique<DaoMcpPageUiDelegate>()) {
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
  const ConnectionState* connection = GetDisplayConnection();
  const TargetContext* context =
      connection ? GetDefaultTargetContext(*connection) : nullptr;
  if (!context || !context->lease) {
    return nullptr;
  }
  BrowserWindowInterface* browser_window = context->session->browser_window();
  return browser_window ? browser_window->GetBrowserForMigrationOnly()
                        : nullptr;
}

content::WebContents* DaoMcpService::GetAuthorizedTarget() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!GetAuthorizedBrowser()) {
    return nullptr;
  }
  const ConnectionState* connection = GetDisplayConnection();
  const TargetContext* context =
      connection ? GetDefaultTargetContext(*connection) : nullptr;
  if (!context) {
    return nullptr;
  }
  auto target = context->session->ResolveTarget();
  return target.has_value() ? target.value() : nullptr;
}

bool DaoMcpService::IsTargetControlled(content::WebContents* target) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!target) {
    return false;
  }
  for (const auto& [_, connection] : connections_) {
    for (const auto& [_, context] : connection->target_contexts) {
      auto controlled_target = context->session->ResolveTarget();
      if (context->lease && controlled_target.has_value() &&
          *controlled_target == target) {
        return true;
      }
    }
  }
  return false;
}

size_t DaoMcpService::GetControlledTargetCount() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  size_t count = 0;
  for (const auto& [_, connection] : connections_) {
    for (const auto& [_, context] : connection->target_contexts) {
      count += context->lease.has_value();
    }
  }
  return count;
}

void DaoMcpService::StopControl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* connection = GetDisplayConnection();
  if (!connection) {
    return;
  }
  RejectConnection(*connection, MakeDaoToolError(
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
  devtools_command_callback_for_testing_ = std::move(callback);
  for (auto& [_, connection] : connections_) {
    connection->tab_tool_devtools_client->SetCommandCallbackForTesting(
        devtools_command_callback_for_testing_);
    for (auto& [_, context] : connection->target_contexts) {
      context->devtools_client->SetCommandCallbackForTesting(
          devtools_command_callback_for_testing_);
    }
  }
}

size_t DaoMcpService::active_tool_call_count_for_testing() const {
  size_t count = 0;
  for (const auto& [_, connection] : connections_) {
    count += connection->active_tool_calls.size();
  }
  return count;
}

size_t DaoMcpService::pending_executor_call_count_for_testing() const {
  size_t count = 0;
  for (const auto& [_, connection] : connections_) {
    count += connection->tab_tool_executor->pending_count_for_testing();
    for (const auto& [_, context] : connection->target_contexts) {
      count += context->tool_executor->pending_count_for_testing();
    }
  }
  return count;
}

size_t DaoMcpService::pending_devtools_command_count_for_testing() const {
  size_t count = 0;
  for (const auto& [_, connection] : connections_) {
    count +=
        connection->tab_tool_devtools_client->pending_command_count_for_testing();
    for (const auto& [_, context] : connection->target_contexts) {
      count += context->devtools_client->pending_command_count_for_testing();
    }
  }
  return count;
}

bool DaoMcpService::devtools_attached_for_testing() const {
  for (const auto& [_, connection] : connections_) {
    for (const auto& [_, context] : connection->target_contexts) {
      if (context->devtools_client->agent_host()) {
        return true;
      }
    }
  }
  return false;
}

size_t DaoMcpService::page_operation_count_for_testing() const {
  size_t count = 0;
  for (const auto& [_, connection] : connections_) {
    count += connection->tab_tool_executor->page_operation_count_for_testing();
    for (const auto& [_, context] : connection->target_contexts) {
      count += context->tool_executor->page_operation_count_for_testing();
    }
  }
  return count;
}

size_t DaoMcpService::page_lock_count_for_testing() const {
  size_t count = 0;
  for (const auto& [_, connection] : connections_) {
    count += connection->tab_tool_executor->page_lock_count_for_testing();
    for (const auto& [_, context] : connection->target_contexts) {
      count += context->tool_executor->page_lock_count_for_testing();
    }
  }
  return count;
}

size_t DaoMcpService::page_highlight_count_for_testing() const {
  size_t count = 0;
  for (const auto& [_, connection] : connections_) {
    count += connection->tab_tool_executor->page_highlight_count_for_testing();
    for (const auto& [_, context] : connection->target_contexts) {
      count += context->tool_executor->page_highlight_count_for_testing();
    }
  }
  return count;
}

size_t DaoMcpService::page_cursor_count_for_testing() const {
  size_t count = 0;
  for (const auto& [_, connection] : connections_) {
    count += connection->tab_tool_executor->page_cursor_count_for_testing();
    for (const auto& [_, context] : connection->target_contexts) {
      count += context->tool_executor->page_cursor_count_for_testing();
    }
  }
  return count;
}

void DaoMcpService::TrackPageCursorForTesting(content::WebContents* target) {
  for (auto& [_, connection] : connections_) {
    for (auto& [_, context] : connection->target_contexts) {
      auto controlled_target = context->session->ResolveTarget();
      if (controlled_target.has_value() && *controlled_target == target) {
        context->tool_executor->TrackPageCursorForTesting(target);
        return;
      }
    }
  }
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
    UpdateStatus();
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
    UpdateStatus();
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
    UpdateStatus();
    return;
  }

  listener_active_ = true;
  transport_.AsyncCall(&DaoMcpTransport::StartAccepting);
  UpdateStatus();
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
  if (!transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::Stop);
    transport_.Reset();
  }
  for (auto& [_, connection] : connections_) {
    ResetConnectionState(*connection);
  }
  connections_.clear();
  QueueRuntimeCleanup();
  UpdateStatus();
}

void DaoMcpService::OnTransportAccepted(
    uint64_t runtime_generation,
    uint64_t connection_generation,
    std::optional<base::ProcessId> verified_pid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation != runtime_generation_ || !listener_active_ ||
      connections_.contains(connection_generation)) {
    return;
  }
  auto connection = std::make_unique<ConnectionState>();
  connection->generation = connection_generation;
  connection->verified_pid = verified_pid;
  connection->tab_tool_devtools_client = std::make_unique<DaoDevToolsClient>();
  connection->tab_tool_devtools_client->SetCommandCallbackForTesting(
      devtools_command_callback_for_testing_);
  connection->tab_tool_executor = std::make_unique<DaoBrowserToolExecutor>(
      connection->tab_tool_devtools_client.get(), page_ui_delegate_.get());
  ConnectionState* connection_ptr = connection.get();
  connections_.emplace(connection_generation, std::move(connection));
  connection_ptr->hello_timer.Start(
      FROM_HERE, hello_timeout_,
      base::BindOnce(&DaoMcpService::OnHelloTimeout, weak_factory_.GetWeakPtr(),
                     connection_generation));
  UpdateStatus();
}

void DaoMcpService::OnTransportRequest(uint64_t runtime_generation,
                                       uint64_t connection_generation,
                                       DaoMcpRequest request,
                                       size_t wire_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation == runtime_generation_) {
    ConnectionState* connection = FindConnection(connection_generation);
    if (connection && !connection->closing) {
      OnRequest(*connection, std::move(request));
    }
  }
  if (runtime_generation == runtime_generation_ && !transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::AcknowledgeRequest)
        .WithArgs(connection_generation, wire_bytes);
  }
}

void DaoMcpService::OnConnectionClosed(uint64_t runtime_generation,
                                       uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation != runtime_generation_) {
    return;
  }
  auto connection = connections_.find(connection_generation);
  if (connection == connections_.end()) {
    return;
  }
  ResetConnectionState(*connection->second);
  connections_.erase(connection);
  UpdateStatus();
}

DaoMcpService::ConnectionState* DaoMcpService::FindConnection(
    uint64_t connection_generation) {
  auto connection = connections_.find(connection_generation);
  return connection == connections_.end() ? nullptr : connection->second.get();
}

const DaoMcpService::ConnectionState* DaoMcpService::FindConnection(
    uint64_t connection_generation) const {
  auto connection = connections_.find(connection_generation);
  return connection == connections_.end() ? nullptr : connection->second.get();
}

DaoMcpService::ConnectionState* DaoMcpService::GetDisplayConnection() {
  return const_cast<ConnectionState*>(
      std::as_const(*this).GetDisplayConnection());
}

const DaoMcpService::ConnectionState*
DaoMcpService::GetDisplayConnection() const {
  BrowserWindowInterface* browser_window =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  tabs::TabInterface* active_tab =
      browser_window ? browser_window->GetActiveTabInterface() : nullptr;
  content::WebContents* active_target =
      active_tab ? active_tab->GetContents() : nullptr;
  if (active_target) {
    for (const auto& [_, connection] : connections_) {
      for (const auto& [_, context] : connection->target_contexts) {
        auto target = context->session->ResolveTarget();
        if (context->lease && target.has_value() && *target == active_target) {
          return connection.get();
        }
      }
    }
  }
  for (const auto& [_, connection] : connections_) {
    for (const auto& [_, context] : connection->target_contexts) {
      if (context->lease) {
        return connection.get();
      }
    }
  }
  for (const auto& [_, connection] : connections_) {
    if (connection->approval_state == ApprovalState::kPending ||
        connection->approval_state == ApprovalState::kAllowed) {
      return connection.get();
    }
  }
  return connections_.empty() ? nullptr : connections_.begin()->second.get();
}

void DaoMcpService::ResetConnectionState(ConnectionState& connection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  connection.hello_timer.Stop();
  connection.approval_timer.Stop();
  connection.lease_retry_timer.Stop();
  if (approval_delegate_ && !connection.connection_id.empty() &&
      connection.approval_state == ApprovalState::kPending) {
    approval_delegate_->CancelApproval(connection.connection_id);
  }
  const DaoToolError closed = MakeDaoToolError(
      DaoToolErrorCode::kToolCancelled,
      "The MCP browser connection was closed.");
  connection.tab_tool_executor->CancelAll(closed);
  for (auto& [_, context] : connection.target_contexts) {
    context->tool_executor->CancelAll(closed);
    context->tool_executor->ClearSessionState(context->session.get());
    context->devtools_client->Detach();
  }
  connection.pending_tool_calls.clear();
  connection.active_tool_calls.clear();
  connection.active_tool_targets.clear();
  connection.active_tool_call_bytes.clear();
  connection.active_tool_call_bytes_total = 0;
  connection.tab_tool_sessions.clear();
  connection.default_target_id.clear();
  connection.target_contexts.clear();
  connection.client_info.reset();
  connection.connection_id.clear();
  connection.approval_state = ApprovalState::kNotRequested;
  connection.approval_deadline = base::TimeTicks();
}

void DaoMcpService::OnHelloTimeout(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection || connection->client_info) {
    return;
  }
  SendError(*connection, std::nullopt,
            MakeDaoToolError(DaoToolErrorCode::kAuthorizationTimeout,
                             "The MCP browser connection handshake timed out.",
                             true));
  CloseConnectionAfterWrites(*connection);
}

void DaoMcpService::OnRequest(ConnectionState& connection,
                              DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connection.client_info && request.method != "hello") {
    SendError(connection, request.id,
              InvalidRequest("hello must be the first IPC request."));
    CloseConnectionAfterWrites(connection);
  } else if (request.method == "hello") {
    HandleHello(connection, std::move(request));
  } else if (request.method == "tools/list") {
    HandleToolsList(connection, std::move(request));
  } else if (request.method == "tools/call") {
    HandleToolsCall(connection, std::move(request));
  } else if (request.method == "tools/cancel") {
    HandleToolsCancel(connection, std::move(request));
  } else {
    SendError(connection, request.id,
              InvalidRequest("The browser IPC method is unsupported."));
  }
}

void DaoMcpService::HandleHello(ConnectionState& connection,
                                DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!request.id) {
    SendError(connection, request.id,
              InvalidRequest("hello requires a request id."));
    CloseConnectionAfterWrites(connection);
    return;
  }
  if (connection.client_info || !connection.target_contexts.empty() ||
      connection.approval_state != ApprovalState::kNotRequested) {
    SendError(connection, request.id,
              InvalidRequest("hello has already completed for this socket."));
    CloseConnectionAfterWrites(connection);
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
    SendError(connection, request.id, AuthorizationDenied());
    CloseConnectionAfterWrites(connection);
    return;
  }
  if (!name || name->empty() || name->size() > kMaxClientLabelBytes ||
      !version || version->empty() || version->size() > kMaxClientLabelBytes) {
    SendError(
        connection, request.id,
        InvalidRequest("hello requires bounded client name and version."));
    CloseConnectionAfterWrites(connection);
    return;
  }

  connection.client_info = DaoMcpClientInfo{
      .name = *name,
      .version = *version,
      .verified_pid = connection.verified_pid,
  };
  connection.connection_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  connection.hello_timer.Stop();
  SendSuccess(
      connection, *request.id,
      base::DictValue()
          .Set("connection_id", connection.connection_id)
          .Set("status", "ready"));
  UpdateStatus();
}

void DaoMcpService::HandleToolsList(ConnectionState& connection,
                                    DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!request.id) {
    SendError(connection, request.id,
              InvalidRequest("tools/list requires a request id."));
    return;
  }
  if (!connection.client_info) {
    SendError(connection, request.id, AuthorizationDenied());
    return;
  }

  base::ListValue serialized_tools;
  for (const DaoBrowserToolDefinition* definition :
       DaoBrowserToolCatalog::Get()->List(DaoToolClient::kMcp)) {
    base::DictValue input_schema = definition->input_schema.Clone();
    if (definition->group != DaoBrowserToolGroup::kTabs) {
      base::DictValue* properties = input_schema.FindDict("properties");
      if (!properties) {
        input_schema.Set("properties", base::DictValue());
        properties = input_schema.FindDict("properties");
      }
      properties->Set(
          "tab_id",
          base::DictValue()
              .Set("type", "string")
              .Set("description",
                   "Optional controlled tab id. Uses the current automation "
                   "target when omitted."));
    }
    base::DictValue tool =
        base::DictValue()
            .Set("name", definition->name)
            .Set("description", definition->description)
            .Set("inputSchema", std::move(input_schema))
            .Set("sideEffect", SideEffectName(definition->side_effect))
            .Set("timeoutMs",
                 static_cast<int>(definition->timeout.InMilliseconds()));
    if (definition->output_schema) {
      tool.Set("outputSchema", definition->output_schema->Clone());
    }
    serialized_tools.Append(std::move(tool));
  }
  SendSuccess(connection, *request.id,
              base::DictValue().Set("tools", std::move(serialized_tools)));
}

void DaoMcpService::HandleToolsCall(ConnectionState& connection,
                                    DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!request.id) {
    SendError(connection, request.id,
              InvalidRequest("tools/call requires a request id."));
    return;
  }
  if (!connection.client_info) {
    SendError(connection, request.id, AuthorizationDenied());
    return;
  }
  if (connection.active_tool_calls.contains(*request.id)) {
    SendError(connection, request.id,
              InvalidRequest("The browser tool request id is already active."));
    return;
  }
  if (connection.active_tool_calls.size() >= kMaxPendingToolCalls) {
    SendError(connection, request.id,
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
    SendError(connection, request.id,
              InvalidRequest("tools/call requires name and object arguments."));
    return;
  }
  if (!definition) {
    SendError(connection, request.id,
              MakeDaoToolError(DaoToolErrorCode::kUnknownTool,
                               "Unknown or unavailable MCP browser tool."));
    return;
  }
  if (connection.approval_state == ApprovalState::kDenied) {
    SendError(connection, request.id, AuthorizationDenied());
    return;
  }

  std::string serialized_arguments;
  if (!base::JSONWriter::Write(*arguments, &serialized_arguments) ||
      serialized_arguments.size() >
          kMaxPendingToolCallBytes - connection.active_tool_call_bytes_total) {
    SendError(connection, request.id,
              MakeDaoToolError(DaoToolErrorCode::kInternalError,
                               "The pending MCP tool-call budget is exhausted.",
                               true));
    return;
  }

  if (connection.approval_state == ApprovalState::kNotRequested) {
    auto approval_browser = PrepareApprovalSession(connection);
    if (!approval_browser.has_value()) {
      SendError(connection, request.id, std::move(approval_browser).error());
      return;
    }
    RequestApproval(connection, *approval_browser);
    if (connection.closing || !connection.client_info ||
        connection.target_contexts.empty() ||
        connection.approval_state == ApprovalState::kDenied) {
      return;
    }
  }

  PendingToolCall pending;
  pending.call.request_id = *request.id;
  pending.call.name = *name;
  pending.call.arguments = arguments->Clone();
  pending.call.timeout = definition->timeout;
  pending.group = definition->group;
  pending.target_id = connection.default_target_id;
  if (definition->group != DaoBrowserToolGroup::kTabs) {
    if (base::Value* routing_tab_id =
            pending.call.arguments.Find("tab_id")) {
      if (!routing_tab_id->is_string() ||
          routing_tab_id->GetString().empty()) {
        SendError(connection, request.id,
                  InvalidRequest("tab_id must be a non-empty string."));
        return;
      }
      pending.target_id = routing_tab_id->GetString();
      pending.call.arguments.Remove("tab_id");
    }
    if (!connection.target_contexts.contains(pending.target_id)) {
      SendError(connection, request.id,
                MakeDaoToolError(
                    DaoToolErrorCode::kTargetGone,
                    "The requested MCP browser target is not controlled."));
      return;
    }
  }
  pending.buffered_bytes = serialized_arguments.size();
  connection.active_tool_calls.insert(*request.id);
  connection.active_tool_call_bytes.emplace(*request.id,
                                             pending.buffered_bytes);
  connection.active_tool_call_bytes_total += pending.buffered_bytes;
  connection.pending_tool_calls.emplace(*request.id, std::move(pending));
  const TargetContext* default_context = GetDefaultTargetContext(connection);
  if (connection.approval_state == ApprovalState::kAllowed &&
      default_context && default_context->lease) {
    DispatchPendingCalls(connection);
  }
}

void DaoMcpService::HandleToolsCancel(ConnectionState& connection,
                                      DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string* request_id = request.params.FindString("request_id");
  if (!request_id || request_id->empty()) {
    if (request.id) {
      SendError(
          connection, request.id,
          InvalidRequest("tools/cancel requires a non-empty request_id."));
    }
    return;
  }

  auto pending = connection.pending_tool_calls.find(*request_id);
  if (pending != connection.pending_tool_calls.end()) {
    auto bytes = connection.active_tool_call_bytes.find(*request_id);
    if (bytes != connection.active_tool_call_bytes.end()) {
      connection.active_tool_call_bytes_total -= bytes->second;
      connection.active_tool_call_bytes.erase(bytes);
    }
    connection.pending_tool_calls.erase(pending);
    connection.active_tool_calls.erase(*request_id);
    SendError(connection, std::optional<std::string>(*request_id),
              MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                               "The browser tool call was cancelled."));
  } else {
    auto tab_session = connection.tab_tool_sessions.find(*request_id);
    if (tab_session != connection.tab_tool_sessions.end()) {
      connection.tab_tool_executor->Cancel(*request_id);
    } else {
      auto target = connection.active_tool_targets.find(*request_id);
      auto context = target == connection.active_tool_targets.end()
                         ? connection.target_contexts.end()
                         : connection.target_contexts.find(target->second);
      if (context != connection.target_contexts.end()) {
        context->second->tool_executor->Cancel(*request_id);
      }
    }
  }
  if (request.id && !connection.closing) {
    SendSuccess(connection, *request.id,
                base::DictValue().Set("cancelled", true));
  }
}

void DaoMcpService::SendSuccess(ConnectionState& connection,
                                std::string id,
                                base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connection.closing && !transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::SendSuccess)
        .WithArgs(connection.generation, std::move(id),
                  std::move(result));
  }
}

void DaoMcpService::SendError(ConnectionState& connection,
                              const std::optional<std::string>& id,
                              DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connection.closing && !transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::SendError)
        .WithArgs(connection.generation, id, std::move(error));
  }
}

void DaoMcpService::CloseConnectionAfterWrites(ConnectionState& connection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (connection.closing) {
    return;
  }
  connection.closing = true;
  if (!transport_.is_null()) {
    transport_.AsyncCall(&DaoMcpTransport::CloseAfterWrites)
        .WithArgs(connection.generation);
  }
}

base::expected<Browser*, DaoToolError>
DaoMcpService::PrepareApprovalSession(ConnectionState& connection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connection.client_info || !connection.target_contexts.empty() ||
      connection.approval_state != ApprovalState::kNotRequested) {
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

  auto target_id = AddTargetContext(connection, browser_window, target);
  if (!target_id.has_value()) {
    return base::unexpected(std::move(target_id).error());
  }
  connection.default_target_id = std::move(target_id).value();
  return browser;
}

void DaoMcpService::RequestApproval(ConnectionState& connection,
                                    Browser* browser) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  connection.approval_state = ApprovalState::kPending;
  connection.approval_deadline = base::TimeTicks::Now() + approval_timeout_;
  UpdateStatus();
  TargetContext* context = GetDefaultTargetContext(connection);
  if (connection.closing ||
      connection.approval_state != ApprovalState::kPending ||
      !connection.client_info || !context) {
    return;
  }
  BrowserWindowInterface* browser_window = context->session->browser_window();
  Browser* approval_browser =
      browser_window ? browser_window->GetBrowserForMigrationOnly() : nullptr;
  if (!approval_browser || approval_browser != browser) {
    RejectConnection(connection, MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The browser selected for MCP approval is no longer available."));
    return;
  }
  const uint64_t connection_generation = connection.generation;
  const std::string connection_id = connection.connection_id;
  connection.approval_timer.Start(
      FROM_HERE, approval_timeout_,
      base::BindOnce(&DaoMcpService::OnApprovalTimeout,
                     weak_factory_.GetWeakPtr(), connection_generation,
                     connection_id));
  if (!approval_delegate_) {
    RejectConnection(connection, AuthorizationDenied());
    return;
  }
  DaoMcpClientInfo client = *connection.client_info;
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
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection) {
    return;
  }
  if (!allowed) {
    RejectConnection(*connection, AuthorizationDenied());
    return;
  }
  connection->approval_state = ApprovalState::kAllowed;
  TryAcquireExternalLease(connection_generation, std::move(connection_id));
}

void DaoMcpService::TryAcquireExternalLease(uint64_t connection_generation,
                                            std::string connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection || connection_id != connection->connection_id ||
      connection->closing ||
      connection->approval_state != ApprovalState::kAllowed) {
    return;
  }
  TargetContext* context = GetDefaultTargetContext(*connection);
  if (!context) {
    RejectConnection(*connection, MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The authorized browser session is no longer available."));
    return;
  }
  auto acquired = AcquireTargetLease(*connection, *context);
  if (acquired.has_value()) {
    connection->approval_timer.Stop();
    UpdateStatus();
    DispatchPendingCalls(*connection);
    return;
  }

  const DaoToolError error = acquired.error();
  const bool retryable_busy = error.code == DaoToolErrorCode::kLeaseBusy ||
                              error.code == DaoToolErrorCode::kAgentControlBusy;
  if (retryable_busy &&
      base::TimeTicks::Now() < connection->approval_deadline) {
    connection->lease_retry_timer.Start(
        FROM_HERE, kLeaseRetryDelay,
        base::BindOnce(&DaoMcpService::TryAcquireExternalLease,
                       weak_factory_.GetWeakPtr(), connection_generation,
                       std::move(connection_id)));
    return;
  }
  RejectConnection(*connection, error);
}

void DaoMcpService::OnApprovalTimeout(uint64_t connection_generation,
                                      std::string connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection || connection_id != connection->connection_id) {
    return;
  }
  const TargetContext* context = GetDefaultTargetContext(*connection);
  if (connection->approval_state != ApprovalState::kPending &&
      !(connection->approval_state == ApprovalState::kAllowed &&
        context && !context->lease)) {
    return;
  }
  RejectConnection(
      *connection,
      MakeDaoToolError(DaoToolErrorCode::kAuthorizationTimeout,
                       "The MCP browser connection approval timed out.", true));
}

bool DaoMcpService::IsCurrentApproval(uint64_t connection_generation,
                                      std::string_view connection_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const ConnectionState* connection = FindConnection(connection_generation);
  return connection && !connection->closing &&
         connection_id == connection->connection_id &&
         connection->approval_state == ApprovalState::kPending;
}

DaoMcpService::TargetContext* DaoMcpService::GetDefaultTargetContext(
    ConnectionState& connection) {
  auto context = connection.target_contexts.find(connection.default_target_id);
  return context == connection.target_contexts.end() ? nullptr
                                                     : context->second.get();
}

const DaoMcpService::TargetContext*
DaoMcpService::GetDefaultTargetContext(
    const ConnectionState& connection) const {
  auto context = connection.target_contexts.find(connection.default_target_id);
  return context == connection.target_contexts.end() ? nullptr
                                                     : context->second.get();
}

base::expected<std::string, DaoToolError> DaoMcpService::AddTargetContext(
    ConnectionState& connection,
    BrowserWindowInterface* browser_window,
    content::WebContents* target,
    bool allow_uncommitted_url) {
  Browser* browser =
      browser_window ? browser_window->GetBrowserForMigrationOnly() : nullptr;
  Profile* profile = browser ? browser->profile() : nullptr;
  auto target_policy = ValidateExternalTarget(
      browser, profile, target, allow_uncommitted_url);
  if (!target_policy.has_value()) {
    return base::unexpected(std::move(target_policy).error());
  }

  if (const TargetContext* default_context =
          GetDefaultTargetContext(connection)) {
    BrowserWindowInterface* authorized_window =
        default_context->session->browser_window();
    Browser* authorized_browser =
        authorized_window
            ? authorized_window->GetBrowserForMigrationOnly()
            : nullptr;
    if (!authorized_browser || authorized_window != browser_window ||
        authorized_browser->profile() != profile) {
      return base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kInvalidArgument,
          "MCP tab targets must remain in the approved browser window."));
    }
  }

  const std::string target_id = GetOrCreateSidebarTabId(target);
  if (connection.target_contexts.contains(target_id)) {
    return target_id;
  }

  auto context = std::make_unique<TargetContext>();
  context->session =
      std::make_unique<DaoBrowserAutomationSession>(browser_window, target);
  auto resolved_target = context->session->ResolveTarget();
  if (!resolved_target.has_value()) {
    return base::unexpected(std::move(resolved_target).error());
  }
  context->devtools_client = std::make_unique<DaoDevToolsClient>();
  context->devtools_client->SetCommandCallbackForTesting(
      devtools_command_callback_for_testing_);
  context->tool_executor = std::make_unique<DaoBrowserToolExecutor>(
      context->devtools_client.get(), page_ui_delegate_.get());
  if (connection.approval_state == ApprovalState::kAllowed) {
    auto acquired =
        AcquireTargetLease(connection, *context, allow_uncommitted_url);
    if (!acquired.has_value()) {
      return base::unexpected(std::move(acquired).error());
    }
  }
  context->lifecycle_monitor =
      std::make_unique<DaoMcpSessionLifecycleMonitor>(
          context->session.get(),
          base::BindOnce(&DaoMcpService::OnTargetInvalidated,
                         weak_factory_.GetWeakPtr(), connection.generation,
                         target_id));
  DaoMcpSessionLifecycleMonitor* monitor = context->lifecycle_monitor.get();
  connection.target_contexts.emplace(target_id, std::move(context));
  monitor->Start();
  if (!connection.target_contexts.contains(target_id)) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The requested MCP browser target is no longer available."));
  }
  return target_id;
}

base::expected<void, DaoToolError> DaoMcpService::AcquireTargetLease(
    ConnectionState& connection,
    TargetContext& context,
    bool allow_uncommitted_url) {
  if (context.lease) {
    return base::ok();
  }
  auto target = context.session->ResolveTarget();
  if (!target.has_value()) {
    return base::unexpected(std::move(target).error());
  }
  BrowserWindowInterface* browser_window = context.session->browser_window();
  Browser* browser =
      browser_window ? browser_window->GetBrowserForMigrationOnly() : nullptr;
  auto target_policy = ValidateExternalTarget(
      browser, context.session->profile(), *target, allow_uncommitted_url);
  if (!target_policy.has_value()) {
    return base::unexpected(std::move(target_policy).error());
  }
  Profile* profile = context.session->profile();
  if (!profile || !connection.client_info || connection.connection_id.empty()) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The authorized browser profile is no longer available."));
  }
  auto acquired = DaoAgentLeaseManager::GetForProfile(profile)->TryAcquire(
      context.session->target_handle(),
      {DaoToolClient::kMcp, connection.connection_id,
       connection.client_info->name});
  if (!acquired.has_value()) {
    return base::unexpected(std::move(acquired).error());
  }
  context.lease.emplace(std::move(acquired).value());
  return base::ok();
}

void DaoMcpService::RejectConnection(ConnectionState& connection,
                                     DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  connection.hello_timer.Stop();
  connection.approval_timer.Stop();
  connection.lease_retry_timer.Stop();
  const bool cancel_pending_approval =
      connection.approval_state == ApprovalState::kPending &&
      !connection.connection_id.empty();
  connection.approval_state = ApprovalState::kDenied;
  if (approval_delegate_ && cancel_pending_approval) {
    approval_delegate_->CancelApproval(connection.connection_id);
  }
  FailPendingCalls(connection, error);
  connection.tab_tool_executor->CancelAll(error);
  for (auto& [_, context] : connection.target_contexts) {
    context->tool_executor->CancelAll(error);
    context->tool_executor->ClearSessionState(context->session.get());
    context->devtools_client->Detach();
  }
  // Active tool cancellation completes synchronously and writes its structured
  // result while the connection is still writable. Close only after those
  // exactly-once completions have been queued.
  CloseConnectionAfterWrites(connection);
  connection.pending_tool_calls.clear();
  connection.active_tool_calls.clear();
  connection.active_tool_targets.clear();
  connection.active_tool_call_bytes.clear();
  connection.active_tool_call_bytes_total = 0;
  connection.tab_tool_sessions.clear();
  connection.default_target_id.clear();
  connection.target_contexts.clear();
  connection.client_info.reset();
  connection.connection_id.clear();
  connection.approval_deadline = base::TimeTicks();
  UpdateStatus();
}

void DaoMcpService::OnTargetInvalidated(uint64_t connection_generation,
                                        std::string target_id,
                                        DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection || connection->closing) {
    return;
  }
  auto context = connection->target_contexts.find(target_id);
  if (context == connection->target_contexts.end()) {
    return;
  }
  FailPendingCallsForTarget(*connection, target_id, error);
  context->second->tool_executor->CancelAll(error);
  context->second->tool_executor->ClearSessionState(
      context->second->session.get());
  context->second->devtools_client->Detach();
  connection->target_contexts.erase(context);
  if (connection->default_target_id == target_id) {
    connection->default_target_id = connection->target_contexts.empty()
                                        ? std::string()
                                        : connection->target_contexts.begin()->first;
  }
  if (connection->target_contexts.empty() &&
      connection->tab_tool_sessions.empty()) {
    RejectConnection(*connection, std::move(error));
    return;
  }
  UpdateStatus();
}

void DaoMcpService::FailPendingCalls(ConnectionState& connection,
                                     const DaoToolError& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<std::string> request_ids;
  request_ids.reserve(connection.pending_tool_calls.size());
  for (const auto& [request_id, _] : connection.pending_tool_calls) {
    request_ids.push_back(request_id);
  }
  connection.pending_tool_calls.clear();
  for (const std::string& request_id : request_ids) {
    connection.active_tool_calls.erase(request_id);
    connection.active_tool_targets.erase(request_id);
    auto bytes = connection.active_tool_call_bytes.find(request_id);
    if (bytes != connection.active_tool_call_bytes.end()) {
      connection.active_tool_call_bytes_total -= bytes->second;
      connection.active_tool_call_bytes.erase(bytes);
    }
    SendError(connection, std::optional<std::string>(request_id), error);
  }
}

void DaoMcpService::FailPendingCallsForTarget(
    ConnectionState& connection,
    std::string_view target_id,
    const DaoToolError& error) {
  std::vector<std::string> request_ids;
  for (const auto& [request_id, pending] : connection.pending_tool_calls) {
    if (pending.target_id == target_id) {
      request_ids.push_back(request_id);
    }
  }
  for (const std::string& request_id : request_ids) {
    connection.pending_tool_calls.erase(request_id);
    connection.active_tool_calls.erase(request_id);
    connection.active_tool_targets.erase(request_id);
    auto bytes = connection.active_tool_call_bytes.find(request_id);
    if (bytes != connection.active_tool_call_bytes.end()) {
      connection.active_tool_call_bytes_total -= bytes->second;
      connection.active_tool_call_bytes.erase(bytes);
    }
    SendError(connection, std::optional<std::string>(request_id), error);
  }
}

void DaoMcpService::DispatchPendingCalls(ConnectionState& connection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  while (!connection.closing && !connection.pending_tool_calls.empty()) {
    auto it = connection.pending_tool_calls.begin();
    PendingToolCall pending = std::move(it->second);
    connection.pending_tool_calls.erase(it);
    DispatchToolCall(connection, std::move(pending));
  }
}

void DaoMcpService::DispatchToolCall(ConnectionState& connection,
                                     PendingToolCall pending) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string request_id = pending.call.request_id;
  connection.active_tool_targets.emplace(request_id, pending.target_id);
  if (pending.group == DaoBrowserToolGroup::kTabs) {
    auto context = connection.target_contexts.find(pending.target_id);
    if (context == connection.target_contexts.end()) {
      OnToolCallComplete(
          connection.generation, request_id, false,
          ErrorResult(MakeDaoToolError(
              DaoToolErrorCode::kTargetGone,
              "The current MCP browser target is no longer available.")));
      return;
    }
    auto target = context->second->session->ResolveTarget();
    if (!target.has_value()) {
      OnToolCallComplete(connection.generation, request_id, false,
                         ErrorResult(std::move(target).error()));
      return;
    }
    const bool allow_uncommitted_url = pending.call.name == "open_tab";
    auto session = std::make_unique<DaoBrowserAutomationSession>(
        context->second->session->browser_window(), *target);
    DaoBrowserAutomationSession* session_ptr = session.get();
    connection.tab_tool_sessions.emplace(request_id, std::move(session));
    connection.tab_tool_executor->Execute(
        session_ptr, DaoToolClient::kMcp, std::move(pending.call),
        base::BindOnce(&DaoMcpService::OnToolCallComplete,
                       weak_factory_.GetWeakPtr(), connection.generation,
                       request_id,
                       allow_uncommitted_url));
    return;
  }

  auto context = connection.target_contexts.find(pending.target_id);
  if (context == connection.target_contexts.end()) {
    OnToolCallComplete(
        connection.generation, request_id, false,
        ErrorResult(MakeDaoToolError(
            DaoToolErrorCode::kTargetGone,
            "The requested MCP browser target is no longer available.")));
    return;
  }
  context->second->tool_executor->Execute(
      context->second->session.get(), DaoToolClient::kMcp,
      std::move(pending.call),
      base::BindOnce(&DaoMcpService::OnToolCallComplete,
                     weak_factory_.GetWeakPtr(), connection.generation,
                     request_id, false));
}

void DaoMcpService::OnToolCallComplete(uint64_t connection_generation,
                                       std::string request_id,
                                       bool allow_uncommitted_url,
                                       DaoBrowserToolResult result) {
  ++tool_call_completion_count_for_testing_;
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection || !connection->active_tool_calls.erase(request_id)) {
    return;
  }
  connection->active_tool_targets.erase(request_id);
  auto bytes = connection->active_tool_call_bytes.find(request_id);
  if (bytes != connection->active_tool_call_bytes.end()) {
    connection->active_tool_call_bytes_total -= bytes->second;
    connection->active_tool_call_bytes.erase(bytes);
  }
  bool target_set_changed = false;
  auto tab_session = connection->tab_tool_sessions.find(request_id);
  if (tab_session != connection->tab_tool_sessions.end()) {
    if (result.ok && !connection->closing &&
        connection->approval_state == ApprovalState::kAllowed) {
      const size_t previous_target_count = connection->target_contexts.size();
      const std::string previous_default_target_id =
          connection->default_target_id;
      auto target = tab_session->second->ResolveTarget();
      if (target.has_value()) {
        auto target_id = AddTargetContext(
            *connection, tab_session->second->browser_window(), *target,
            allow_uncommitted_url);
        if (target_id.has_value()) {
          connection->default_target_id = std::move(target_id).value();
          target_set_changed =
              connection->target_contexts.size() != previous_target_count ||
              connection->default_target_id != previous_default_target_id;
        } else {
          result.ok = false;
          result.error = std::move(target_id).error();
        }
      } else {
        result.ok = false;
        result.error = std::move(target).error();
      }
    }
    connection->tab_tool_sessions.erase(tab_session);
  }
  if (!connection->closing) {
    SendSuccess(*connection, request_id,
                SerializeDaoBrowserToolResult(std::move(result)));
  }
  if (target_set_changed) {
    UpdateStatus();
  }
  if (!connection->closing && connection->target_contexts.empty() &&
      connection->tab_tool_sessions.empty()) {
    RejectConnection(*connection, MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "No controlled MCP browser targets remain."));
  }
}

void DaoMcpService::NotifyStatusObservers() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  status_observers_.Notify(status_);
}

void DaoMcpService::UpdateStatus() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DaoMcpServiceStatus next;
  next.state = listener_active_ || listener_start_pending_
                   ? DaoMcpStatus::kListening
                   : DaoMcpStatus::kDisabled;
  const ConnectionState* connection = GetDisplayConnection();
  if (connection) {
    bool has_lease = false;
    for (const auto& [_, context] : connection->target_contexts) {
      has_lease |= context->lease.has_value();
    }
    if (has_lease) {
      next.state = DaoMcpStatus::kLeaseActive;
      next.client = connection->client_info;
    } else if (connection->approval_state == ApprovalState::kPending ||
               connection->approval_state == ApprovalState::kAllowed) {
      next.state = DaoMcpStatus::kPendingApproval;
      next.client = connection->client_info;
    }
  }
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
