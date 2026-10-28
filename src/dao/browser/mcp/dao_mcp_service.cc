// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_service.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/updater/dao_updater_service.h"

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
#include <algorithm>
#include <vector>

#include "base/base_paths.h"
#if BUILDFLAG(IS_MAC)
#include "base/apple/bundle_locations.h"
#endif
#include "base/containers/span.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_paths.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "crypto/secure_util.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/agent/dao_agent_plugins.h"
#include "dao/browser/agent_space/dao_agent_space_service.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_target_policy.h"
#include "dao/browser/automation/dao_browser_tool_catalog.h"
#include "dao/browser/automation/dao_browser_tool_executor.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "dao/browser/automation/dao_page_tools.h"
#include "dao/browser/automation/dao_tool_schema_validator.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/mcp/dao_mcp_runtime_files.h"
#include "dao/browser/mcp/dao_mcp_session_lifecycle_monitor.h"
#include "dao/browser/ui/views/dao_agent_cursor_view.h"
#include "dao/browser/ui/views/dao_mcp_approval_dialog.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "net/base/net_errors.h"
#endif

namespace dao {
namespace {

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
constexpr size_t kMaxClientLabelBytes = 256;
constexpr size_t kMaxPendingToolCalls = 64;
constexpr size_t kMaxPendingToolCallBytes = kDaoMcpMaxLineBytes;
constexpr base::TimeDelta kHelloTimeout = base::Seconds(5);
constexpr base::TimeDelta kApprovalTimeout = base::Minutes(1);
constexpr base::TimeDelta kLeaseRetryDelay = base::Milliseconds(50);
#endif
constexpr char kUsageTotalCalls[] = "totalCalls";
constexpr char kUsageToolCalls[] = "toolCalls";
constexpr char kUsageLastReset[] = "lastReset";

std::optional<double> ReadNonNegativeNumber(const base::Value* value) {
  if (!value) {
    return std::nullopt;
  }
  const double number = value->is_int()
                            ? static_cast<double>(value->GetInt())
                            : (value->is_double() ? value->GetDouble() : -1.0);
  return number >= 0.0 && std::isfinite(number) ? std::optional(number)
                                                : std::nullopt;
}

base::DictValue NewMcpUsageStats(base::Time last_reset) {
  base::DictValue stats;
  stats.Set(kUsageTotalCalls, 0.0);
  stats.Set(kUsageToolCalls, base::DictValue());
  stats.Set(kUsageLastReset,
            static_cast<double>(last_reset.InMillisecondsSinceUnixEpoch()));
  return stats;
}

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
void RecordMcpToolUsage(PrefService* prefs, std::string_view tool_name) {
  if (!prefs || tool_name.empty()) {
    return;
  }
  base::DictValue stats = BuildDaoMcpUsageStats(prefs);
  const double total = *stats.FindDouble(kUsageTotalCalls) + 1.0;
  base::DictValue* tool_calls = stats.FindDict(kUsageToolCalls);
  const double count =
      ReadNonNegativeNumber(tool_calls->Find(tool_name)).value_or(0.0) + 1.0;
  if (!std::isfinite(total) || !std::isfinite(count)) {
    return;
  }
  stats.Set(kUsageTotalCalls, total);
  tool_calls->Set(tool_name, count);
  prefs->SetDict(prefs::kDaoMcpUsageStats, std::move(stats));
}

DaoToolError AuthorizationDenied() {
  return MakeDaoToolError(DaoToolErrorCode::kAuthorizationDenied,
                          "The MCP browser connection was not authorized.");
}

DaoToolError InvalidRequest(std::string message) {
  return MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                          std::move(message));
}

DaoToolError PageNotApproved() {
  return MakeDaoToolError(
      DaoToolErrorCode::kAuthorizationDenied,
      "This page has not been approved. Use request_tab_access or open_tab "
      "before calling page tools.");
}

void RepairDuplicateTabIds(TabListInterface* tab_list) {
  std::vector<content::WebContents*> contents;
  for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
    contents.push_back(tab->GetContents());
  }
  RepairDuplicateSidebarTabIds(contents);
}

DaoBrowserToolResult ErrorResult(DaoToolError error) {
  DaoBrowserToolResult result;
  result.error = std::move(error);
  return result;
}

bool IsValidApprovalReason(const std::string* reason) {
  return reason && reason->size() <= 1024 && base::IsStringUTF8(*reason) &&
         !base::CollapseWhitespace(base::UTF8ToUTF16(*reason), false).empty();
}

DaoToolError MissingReasonError() {
  return InvalidRequest(
      "The first tool call requires a non-blank reason explaining why "
      "browser access is needed, within 1024 UTF-8 bytes. Include it in "
      "arguments.reason; it will be shown in the permission dialog.");
}

std::string SpaceApprovalId(std::string_view connection_id) {
  return base::StrCat({connection_id, "/space"});
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

#endif
}  // namespace

DaoMcpApprovalRequest::DaoMcpApprovalRequest() = default;
DaoMcpApprovalRequest::DaoMcpApprovalRequest(DaoMcpClientInfo client,
                                           std::string reason,
                                           base::Time requested_at)
    : client(std::move(client)),
      reason(std::move(reason)),
      requested_at(requested_at) {}
DaoMcpApprovalRequest::~DaoMcpApprovalRequest() = default;
DaoMcpApprovalRequest::DaoMcpApprovalRequest(const DaoMcpApprovalRequest&) =
    default;
DaoMcpApprovalRequest& DaoMcpApprovalRequest::operator=(
    const DaoMcpApprovalRequest&) = default;

base::DictValue BuildDaoMcpUsageStats(PrefService* prefs) {
  base::DictValue normalized = NewMcpUsageStats(base::Time::Now());
  if (!prefs) {
    return normalized;
  }
  const base::DictValue& stored = prefs->GetDict(prefs::kDaoMcpUsageStats);
  const std::optional<double> total =
      ReadNonNegativeNumber(stored.Find(kUsageTotalCalls));
  const std::optional<double> last_reset =
      ReadNonNegativeNumber(stored.Find(kUsageLastReset));
  const base::DictValue* tool_calls = stored.FindDict(kUsageToolCalls);
  if (!total || !last_reset || !tool_calls) {
    return normalized;
  }
  base::DictValue normalized_tools;
  for (const auto [name, value] : *tool_calls) {
    const std::optional<double> count = ReadNonNegativeNumber(&value);
    if (count) {
      normalized_tools.Set(name, *count);
    }
  }
  normalized.Set(kUsageTotalCalls, *total);
  normalized.Set(kUsageToolCalls, std::move(normalized_tools));
  normalized.Set(kUsageLastReset, *last_reset);
  return normalized;
}

void ResetDaoMcpUsageStats(PrefService* prefs, base::Time last_reset) {
  if (!prefs || last_reset.is_null() ||
      last_reset.InMillisecondsSinceUnixEpoch() < 0) {
    return;
  }
  prefs->SetDict(prefs::kDaoMcpUsageStats,
                 NewMcpUsageStats(last_reset));
}

void DaoMcpApprovalDelegate::RequestTabAccess(
    const DaoMcpApprovalRequest& request,
    BrowserWindowInterface* browser,
    std::string_view approval_id,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  if (!request.tab_target) {
    std::move(callback).Run(std::nullopt);
    return;
  }
  RequestApproval(
      request, browser, approval_id,
      base::BindOnce(
          [](std::string tab_id,
             base::OnceCallback<void(std::optional<std::string>)> done,
             bool allowed) {
            std::move(done).Run(allowed ? std::make_optional(std::move(tab_id))
                                        : std::nullopt);
          },
          request.tab_target->tab_id, std::move(callback)));
}

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
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
  base::TimeTicks last_activity;
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
  std::unique_ptr<DaoBrowserAutomationSession> window_session;
  std::unique_ptr<DaoMcpSessionLifecycleMonitor> window_monitor;
  bool can_create_tabs = false;
  struct PageCandidate {
    tabs::TabHandle handle;
    base::WeakPtr<content::WebContents> contents;
    std::string url;
    int64_t document_sequence_number = -1;
  };
  std::optional<PageCandidate> initial_candidate;
  uint64_t page_approval_serial = 0;
  std::string page_approval_id;
  std::string page_request_id;
  std::map<std::string, PageCandidate, std::less<>> page_candidates;
  base::OneShotTimer page_approval_timer;
  std::map<std::string, PendingToolCall, std::less<>> pending_tool_calls;
  std::set<std::string, std::less<>> active_tool_calls;
  std::map<std::string, std::string, std::less<>> active_tool_targets;
  std::map<std::string, size_t, std::less<>> active_tool_call_bytes;
  size_t active_tool_call_bytes_total = 0;
  std::optional<std::string> latest_tool_name;
  uint64_t latest_tool_call_serial = 0;
  std::unique_ptr<DaoDevToolsClient> tab_tool_devtools_client;
  std::unique_ptr<DaoBrowserToolExecutor> tab_tool_executor;
  std::map<std::string, std::unique_ptr<DaoBrowserAutomationSession>,
           std::less<>>
      tab_tool_sessions;
  ApprovalState space_approval_state = ApprovalState::kNotRequested;
  base::OneShotTimer space_approval_timer;
  base::WeakPtr<Profile> space_profile;
  std::vector<PendingToolCall> queued_space_calls;
  std::set<std::string, std::less<>> space_tool_calls;
};

class DaoMcpPageUiDelegate final : public DaoPageTools::UiDelegate {
 public:
  DaoMcpPageUiDelegate() = default;
  ~DaoMcpPageUiDelegate() override = default;

  void MoveCursor(content::WebContents* target,
                  double x,
                  double y,
                  std::optional<gfx::RectF> target_bounds,
                  base::OnceCallback<void(bool)> callback) override {
    BrowserWindowInterface* browser =
        target ? GlobalBrowserCollection::GetInstance()
                 ->FindBrowserWithTab(target)
               : nullptr;
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

    AnimateAgentCursorInViewport(browser_view, target, x, y,
                                 std::move(target_bounds),
                                 std::move(callback));
  }

  void PlayClickRipple(content::WebContents* target) override {
    BrowserWindowInterface* browser =
        target ? GlobalBrowserCollection::GetInstance()
                 ->FindBrowserWithTab(target)
               : nullptr;
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
      BrowserWindowInterface* browser = browser_window;
      if (!browser || browser->GetProfile() != target->GetBrowserContext()) {
        continue;
      }
      BrowserView* browser_view =
          BrowserView::GetBrowserViewForBrowser(browser);
      if (browser_view && browser_view->dao_agent_cursor()) {
        browser_view->dao_agent_cursor()->CancelAnimation();
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
#if BUILDFLAG(IS_WIN)
  const base::FilePath helper_path = bundle_path.AppendASCII("dao-mcp.exe");
#else
  const base::FilePath helper_path =
      bundle_path.Append(FILE_PATH_LITERAL("Contents"))
          .Append(FILE_PATH_LITERAL("Helpers"))
          .Append(FILE_PATH_LITERAL("dao-mcp"));
#endif
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

#endif

DaoMcpServiceStatus::DaoMcpServiceStatus() = default;
DaoMcpServiceStatus::~DaoMcpServiceStatus() = default;
DaoMcpServiceStatus::DaoMcpServiceStatus(const DaoMcpServiceStatus&) = default;
DaoMcpServiceStatus& DaoMcpServiceStatus::operator=(
    const DaoMcpServiceStatus&) = default;
DaoMcpServiceStatus::DaoMcpServiceStatus(DaoMcpServiceStatus&&) = default;
DaoMcpServiceStatus& DaoMcpServiceStatus::operator=(DaoMcpServiceStatus&&) =
    default;

// static
DaoMcpService* DaoMcpService::Get() {
  static base::NoDestructor<DaoMcpService> instance;
  return instance.get();
}

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
DaoMcpService::PendingToolCall::PendingToolCall() = default;
DaoMcpService::PendingToolCall::~PendingToolCall() = default;
DaoMcpService::PendingToolCall::PendingToolCall(PendingToolCall&&) noexcept =
    default;
DaoMcpService::PendingToolCall& DaoMcpService::PendingToolCall::operator=(
    PendingToolCall&&) noexcept = default;

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
#if BUILDFLAG(IS_MAC)
  return BuildDaoMcpConfigurationForBundle(base::apple::OuterBundlePath());
#else
  return BuildDaoMcpConfigurationForBundle(
      base::PathService::CheckedGet(base::DIR_MODULE));
#endif
}

BrowserWindowInterface* DaoMcpService::GetAuthorizedBrowser() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const ConnectionState* connection = GetDisplayConnection();
  const TargetContext* context =
      connection ? GetDefaultTargetContext(*connection) : nullptr;
  if (!context || !context->lease) {
    return nullptr;
  }
  BrowserWindowInterface* browser_window = context->session->browser_window();
  return browser_window;
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

std::vector<DaoMcpConnectionInfo> DaoMcpService::GetConnections() const {
  std::vector<DaoMcpConnectionInfo> result;
  for (const auto& [_, connection] : connections_) {
    if (connection->closing || !connection->client_info ||
        connection->approval_state != ApprovalState::kAllowed ||
        !connection->window_session) {
      continue;
    }
    DaoMcpConnectionInfo info;
    info.connection_id = connection->connection_id;
    info.client = *connection->client_info;
    auto* window = connection->window_session->browser_window();
    info.browser_window = window ? window->GetWeakPtr() : nullptr;
    info.can_create_tabs = connection->can_create_tabs;
    info.latest_tool_name = connection->latest_tool_name;
    for (const auto& [tab_id, context] : connection->target_contexts) {
      auto target = context->session->ResolveTarget();
      if (context->lease && target.has_value()) {
        info.tabs.push_back({{tab_id, (*target)->GetLastCommittedURL().spec()},
                             (*target)->GetTitle()});
      }
    }
    result.push_back(std::move(info));
  }
  return result;
}

bool DaoMcpService::HasWindowPermission(BrowserWindowInterface* window) const {
  // Mirrors the GetConnections() filter without resolving every page target.
  return window &&
         std::ranges::any_of(connections_, [window](const auto& entry) {
           const ConnectionState& connection = *entry.second;
           return !connection.closing && connection.client_info &&
                  connection.approval_state == ApprovalState::kAllowed &&
                  connection.window_session &&
                  connection.window_session->browser_window() == window;
         });
}

void DaoMcpService::StopControl(std::string_view connection_id) {
  std::vector<uint64_t> revoked;
  for (const auto& [generation, connection] : connections_) {
    if (connection->connection_id != connection_id) {
      continue;
    }
    revoked.push_back(generation);
    if (connection->space_profile && connection->client_info) {
      const std::string& name = connection->client_info->name;
      const base::FilePath& profile = connection->space_profile->GetPath();
      space_approval_expirations_.erase({profile, name});
      if (local_state_) {
        ScopedListPrefUpdate update(local_state_,
                                    prefs::kDaoAgentSpaceAllowedClients);
        update->EraseValue(base::Value(name));
      }
      for (const auto& [other_generation, other] : connections_) {
        if (other_generation != generation && other->client_info &&
            other->client_info->name == name && other->space_profile &&
            other->space_profile->GetPath() == profile) {
          revoked.push_back(other_generation);
        }
      }
    }
    break;
  }
  for (uint64_t generation : revoked) {
    if (ConnectionState* connection = FindConnection(generation)) {
      RejectConnection(
          *connection,
          MakeDaoToolError(
              DaoToolErrorCode::kToolCancelled,
              "External MCP browser control was stopped by the user."));
    }
  }
}

void DaoMcpService::RevokeTab(std::string_view connection_id,
                              std::string_view tab_id) {
  for (auto& [generation, connection] : connections_) {
    if (connection->connection_id == connection_id) {
      if (!connection->target_contexts.contains(tab_id)) {
        return;
      }
      // A pending picker must not restore access to the revoked page, but
      // requests for other pages stay open.
      if (auto candidate = connection->page_candidates.find(tab_id);
          candidate != connection->page_candidates.end()) {
        connection->page_candidates.erase(candidate);
        if (connection->page_candidates.empty()) {
          CancelTabAccess(*connection, AuthorizationDenied());
        }
      }
      OnTargetInvalidated(
          generation, std::string(tab_id),
          MakeDaoToolError(DaoToolErrorCode::kAuthorizationDenied,
                           "Access to this page was revoked by the user."));
      return;
    }
  }
}

bool DaoMcpService::CanAccessTab(uint64_t generation,
                                 content::WebContents* target) const {
  const auto* connection = FindConnection(generation);
  if (!connection || connection->closing || !target ||
      connection->approval_state != ApprovalState::kAllowed) {
    return false;
  }
  const auto it = connection->target_contexts.find(GetSidebarTabId(target));
  if (it == connection->target_contexts.end() || !it->second->lease) {
    return false;
  }
  auto resolved = it->second->session->ResolveTarget();
  if (!resolved.has_value() || *resolved != target) {
    return false;
  }
  BrowserWindowInterface* window = connection->window_session->browser_window();
  return window &&
         ValidateExternalTarget(window,
                                connection->window_session->profile(), target)
             .has_value();
}

void DaoMcpService::OnWindowInvalidated(uint64_t generation,
                                        DaoToolError error) {
  if (auto* connection = FindConnection(generation)) {
    RejectConnection(*connection, std::move(error));
  }
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
  const bool enabled = local_state_->GetBoolean(prefs::kDaoMcpServerEnabled);
  if (enabled) {
    if (!listener_active_ && !listener_start_pending_) {
      StartListening();
    }
    return;
  }
  // Turning access off also revokes "Always allow" for background spaces, so
  // the user can take that decision back.
  local_state_->ClearPref(prefs::kDaoAgentSpaceAllowedClients);
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

void DaoMcpService::OnTransportListening(
    uint64_t runtime_generation, base::expected<uint16_t, int> result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (runtime_generation != runtime_generation_) {
    return;
  }
  if (!result.has_value()) {
    listener_start_pending_ = false;
    transport_.Reset();
    QueueRuntimeCleanup();
    UpdateStatus();
    return;
  }
  runtime_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](scoped_refptr<DaoMcpRuntimeFiles> runtime_files, uint16_t port)
              -> base::expected<void, DaoToolError> {
            auto captured = runtime_files->CaptureBoundSocket(port);
            if (!captured.has_value()) {
              return captured;
            }
            return runtime_files->Publish();
          },
          runtime_files_, result.value()),
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
  if (!shutting_down_ && (listener_active_ || listener_start_pending_)) {
    // Turning off local agent access also ends their background tasks.
    DaoAgentSpaceService::StopSpacesForClient(DaoToolClient::kMcp);
  }
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
  space_approval_expirations_.clear();
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
  connection->last_activity = base::TimeTicks::Now();
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
  // Approved connections are listed in permission UI even without a lease.
  const bool was_listed =
      connection->second->approval_state == ApprovalState::kAllowed ||
      std::ranges::any_of(
          connection->second->target_contexts,
          [](const auto& entry) { return entry.second->lease.has_value(); });
  ResetConnectionState(*connection->second);
  connections_.erase(connection);
  UpdateStatus(was_listed);
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
  CancelTabAccess(connection, closed);
  CancelSpaceCalls(connection, closed);
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
  connection.initial_candidate.reset();
  connection.window_monitor.reset();
  connection.window_session.reset();
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

bool DaoMcpService::MakeRoomForConnection(const ConnectionState& connection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Only sockets that completed hello count. Pre-hello sockets are bounded by
  // the hello timeout and must not be evicted before their hello is handled.
  size_t admitted = 0;
  ConnectionState* victim = nullptr;
  for (auto& [_, candidate] : connections_) {
    if (candidate.get() == &connection || candidate->closing ||
        !candidate->client_info) {
      continue;
    }
    ++admitted;
    const bool busy = candidate->approval_state == ApprovalState::kPending ||
                      !candidate->pending_tool_calls.empty() ||
                      !candidate->active_tool_calls.empty();
    if (busy) {
      continue;
    }
    // Evicting an approved client costs the user another approval prompt, so
    // never-approved idle sockets go first, then the least recently active.
    const auto rank = [](const ConnectionState& state) {
      return std::pair(state.approval_state == ApprovalState::kAllowed,
                       state.last_activity);
    };
    if (!victim || rank(*candidate) < rank(*victim)) {
      victim = candidate.get();
    }
  }
  if (admitted < kDaoMcpMaxConnections) {
    return true;
  }
  if (!victim) {
    return false;
  }
  // Helpers reconnect lazily on their next request. An evicted approved
  // client releases its tab leases now and is prompted for approval again.
  RejectConnection(*victim,
                   MakeDaoToolError(DaoToolErrorCode::kTooManyClients,
                                    "The idle MCP connection was closed to "
                                    "admit a newer client.",
                                    true));
  return true;
}

void DaoMcpService::OnRequest(ConnectionState& connection,
                              DaoMcpRequest request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  connection.last_activity = base::TimeTicks::Now();
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
  if (!MakeRoomForConnection(connection)) {
    SendError(connection, request.id,
              MakeDaoToolError(DaoToolErrorCode::kTooManyClients,
                               "Too many MCP clients are connected to Dao "
                               "Browser and all of them are busy.",
                               true));
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
  // Lets local agents tell the user when a downloaded Dao update waits for a
  // restart.
  const bool update_ready =
      DaoUpdaterService::GetInstance()->GetUpdateStatus().state ==
      DaoUpdateState::kReady;
  SendSuccess(connection, *request.id,
              base::DictValue()
                  .Set("connection_id", connection.connection_id)
                  .Set("status", "ready")
                  .Set("update_ready", update_ready));
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

  // Discovery must not acquire a target or prompt for approval. Once bound,
  // keep using the approved profile even if another window becomes active.
  BrowserWindowInterface* browser_window =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  BrowserWindowInterface* browser = browser_window;
  Profile* profile = connection.window_session
                         ? connection.window_session->profile()
                         : (browser ? browser->GetProfile() : nullptr);
  const base::DictValue* settings =
      profile && !profile->IsOffTheRecord() && !profile->IsGuestSession()
          ? &profile->GetPrefs()->GetDict(prefs::kDaoAgentSettings)
          : nullptr;
  const auto plugins = GetDaoAgentPlugins(settings);
  base::ListValue serialized_tools;
  for (const DaoBrowserToolDefinition* definition :
       DaoBrowserToolCatalog::Get()->List(DaoToolClient::kMcp)) {
    bool available = true;
    for (const auto& plugin : plugins) {
      const auto* tool = plugin.GetDict().FindString("tool");
      if (tool && *tool == definition->name) {
        available = settings && IsDaoAgentPluginAuthorized(
                                    *settings, plugin.GetDict(), definition->name);
        break;
      }
    }
    if (!available) {
      continue;
    }
    base::DictValue input_schema = definition->input_schema.Clone();
    base::DictValue* properties = input_schema.FindDict("properties");
    if (!properties) {
      input_schema.Set("properties", base::DictValue());
      properties = input_schema.FindDict("properties");
    }
    properties->Set(
        "reason",
        base::DictValue()
            .Set("type", "string")
            .Set("minLength", 1)
            .Set("maxLength", 1024)
            .Set("description",
                 "Required on the first tool call and every "
                 "request_tab_access. Explain "
                 "why you need browser access for the user's task; Dao shows "
                 "this reason in the permission dialog. Use non-blank text "
                 "within 1024 UTF-8 bytes. May be omitted after requesting "
                 "permission, except for request_tab_access."));
    if (definition->group != DaoBrowserToolGroup::kSpace &&
        definition->name != "run_browser_task" &&
        definition->name != "request_tab_access") {
      properties->Set(
          "space_id",
          base::DictValue()
              .Set("type", "string")
              .Set("description",
                   "Run this tool inside the background Agent Space with this "
                   "id (from create_space) instead of the user's window. "
                   "Space calls use their own approval and never touch the "
                   "user's tabs."));
      properties->Set(
          "label",
          base::DictValue()
              .Set("type", "string")
              .Set("description",
                   "With space_id: a 3-6 word caption of this step, shown "
                   "to the user watching the space."));
    }
    if (definition->group == DaoBrowserToolGroup::kSpace) {
      // Space tools address spaces by space_id, never by tab.
    } else if (definition->group != DaoBrowserToolGroup::kTabs ||
               definition->name == "list_tabs") {
      properties->Set(
          "tab_id",
          base::DictValue()
              .Set("type", "string")
              .Set("description",
                   "On the first call, requests access only to this exact tab "
                   "instead of a window. On later calls, selects an already "
                   "approved tab. Without a tab id, initial permission only "
                   "allows creating new tabs; no existing page is selected."));
    } else if (base::DictValue* tab_id = properties->FindDict("tab_id");
               tab_id && definition->name != "request_tab_access") {
      tab_id->Set("description",
                  "Stable tab id. On the first call, requests access only to "
                  "this exact tab instead of a window.");
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
  if (DaoAgentSpaceService::ShouldRoute(*name, *arguments)) {
    HandleSpaceToolsCall(connection, std::move(request), *definition);
    return;
  }
  if (connection.approval_state == ApprovalState::kDenied) {
    SendError(connection, request.id, AuthorizationDenied());
    return;
  }

  const std::string* reason = arguments->FindString("reason");
  if ((arguments->contains("reason") ||
       connection.approval_state == ApprovalState::kNotRequested ||
       *name == "request_tab_access") &&
      !IsValidApprovalReason(reason)) {
    SendError(connection, request.id, MissingReasonError());
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

  const std::string* requested_tab_id = arguments->FindString("tab_id");
  if (arguments->contains("tab_id") &&
      (!requested_tab_id || requested_tab_id->empty())) {
    SendError(connection, request.id,
              InvalidRequest("tab_id must be a non-empty string."));
    return;
  }
  base::DictValue tool_arguments = arguments->Clone();
  if (*name != "request_tab_access") {
    tool_arguments.Remove("reason");
  }
  if (definition->group != DaoBrowserToolGroup::kTabs || *name == "list_tabs") {
    tool_arguments.Remove("tab_id");
  }
  auto validation = ValidateToolArguments(*definition, tool_arguments);
  if (!validation.has_value()) {
    SendError(connection, request.id, std::move(validation).error());
    return;
  }
  const bool window_scoped =
      IsDaoMcpWindowTool(*name) || *name == "request_tab_access";
  if (connection.approval_state == ApprovalState::kNotRequested) {
    // Window approval grants no existing page, so do not prompt for a call
    // that would still fail after approval.
    if (!window_scoped && !requested_tab_id) {
      SendError(connection, request.id, PageNotApproved());
      return;
    }
    auto approval_browser = PrepareApprovalSession(connection, requested_tab_id);
    if (!approval_browser.has_value()) {
      SendError(connection, request.id, std::move(approval_browser).error());
      return;
    }
    RequestApproval(connection, *approval_browser, *reason);
    if (connection.closing || !connection.client_info ||
        !connection.window_session ||
        connection.approval_state == ApprovalState::kDenied) {
      return;
    }
  }

  PendingToolCall pending;
  pending.call.request_id = *request.id;
  pending.call.name = *name;
  pending.call.arguments = std::move(tool_arguments);
  pending.call.timeout = definition->timeout;
  pending.group = definition->group;
  pending.target_id = connection.default_target_id;
  if (window_scoped) {
    pending.target_id.clear();
  } else if (*name == "switch_tab" || *name == "close_tab") {
    if (requested_tab_id) {
      pending.target_id = *requested_tab_id;
    } else if (arguments->contains("index")) {
      // MCP indices count granted tabs only, matching list_tabs, so they do
      // not reveal unapproved tabs.
      const std::optional<int> index = arguments->FindInt("index");
      BrowserWindowInterface* window =
          connection.window_session->browser_window();
      TabListInterface* tab_list =
          window ? TabListInterface::From(window) : nullptr;
      pending.target_id.clear();
      if (index && tab_list) {
        RepairDuplicateTabIds(tab_list);
        int position = 0;
        for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
          if (!CanAccessTab(connection.generation, tab->GetContents())) {
            continue;
          }
          if (position++ == *index) {
            pending.target_id = GetSidebarTabId(tab->GetContents());
            break;
          }
        }
      }
      if (pending.target_id.empty()) {
        SendError(connection, request.id,
                  InvalidRequest("Tab index is invalid for the granted tabs."));
        return;
      }
    } else if (*name == "switch_tab") {
      SendError(connection, request.id,
                InvalidRequest("switch_tab requires tab_id or index."));
      return;
    }
    if (!connection.target_contexts.contains(pending.target_id)) {
      SendError(connection, request.id, AuthorizationDenied());
      return;
    }
    // Resolve an index once, before queuing, so reorder cannot change the
    // target.
    pending.call.arguments.Remove("index");
    pending.call.arguments.Set("tab_id", pending.target_id);
  }
  if (definition->group != DaoBrowserToolGroup::kTabs) {
    if (requested_tab_id) {
      pending.target_id = *requested_tab_id;
      pending.call.arguments.Remove("tab_id");
    }
    if (!connection.target_contexts.contains(pending.target_id)) {
      SendError(connection, request.id, PageNotApproved());
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
      (!default_context || default_context->lease)) {
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

  auto queued_space = std::ranges::find_if(
      connection.queued_space_calls, [&](const PendingToolCall& pending) {
        return pending.call.request_id == *request_id;
      });
  auto pending = connection.pending_tool_calls.find(*request_id);
  if (queued_space != connection.queued_space_calls.end()) {
    connection.queued_space_calls.erase(queued_space);
    connection.active_tool_calls.erase(*request_id);
    SendError(connection, std::optional<std::string>(*request_id),
              MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                               "The browser tool call was cancelled."));
  } else if (connection.space_tool_calls.contains(*request_id)) {
    DaoAgentSpaceService* spaces =
        connection.space_profile
            ? DaoAgentSpaceService::GetForProfile(
                  connection.space_profile.get())
            : nullptr;
    if (spaces && connection.client_info) {
      spaces->CancelTool({DaoToolClient::kMcp, connection.connection_id,
                          connection.client_info->name},
                         *request_id);
    }
  } else if (pending != connection.pending_tool_calls.end()) {
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
  } else if (connection.page_request_id == *request_id) {
    CancelTabAccess(connection,
                    MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                     "The page access request was cancelled."));
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

void DaoMcpService::HandleSpaceToolsCall(
    ConnectionState& connection,
    DaoMcpRequest request,
    const DaoBrowserToolDefinition& definition) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string request_id = *request.id;
  base::DictValue* raw_arguments = request.params.FindDict("arguments");
  base::DictValue arguments =
      raw_arguments ? std::move(*raw_arguments) : base::DictValue();
  if (connection.space_approval_state == ApprovalState::kDenied) {
    SendError(connection, request_id, AuthorizationDenied());
    return;
  }
  if (connection.space_approval_state != ApprovalState::kPending) {
    Profile* profile = connection.space_profile.get();
    if (!profile) {
      BrowserWindowInterface* browser =
          GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
      if (browser && browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) {
        profile = browser->GetProfile();
      }
    }
    bool allowed = false;
    if (profile && DaoAgentSpaceService::GetForProfile(profile)) {
      const auto grant = space_approval_expirations_.find(
          {profile->GetPath(), connection.client_info->name});
      allowed = IsSpaceClientRemembered(connection) ||
                (grant != space_approval_expirations_.end() &&
                 space_approval_clock_->NowTicks() < grant->second);
      if (allowed) {
        connection.space_profile = profile->GetWeakPtr();
      }
    }
    connection.space_approval_state =
        allowed ? ApprovalState::kAllowed : ApprovalState::kNotRequested;
  }
  const std::string* reason = arguments.FindString("reason");
  if ((arguments.contains("reason") ||
       connection.space_approval_state == ApprovalState::kNotRequested) &&
      !IsValidApprovalReason(reason)) {
    SendError(connection, request_id, MissingReasonError());
    return;
  }
  std::string reason_text = reason ? *reason : std::string();
  arguments.Remove("reason");

  PendingToolCall pending;
  pending.call.request_id = request_id;
  pending.call.name = definition.name;
  pending.call.arguments = std::move(arguments);
  pending.call.timeout = definition.timeout;
  pending.group = definition.group;
  connection.active_tool_calls.insert(request_id);
  switch (connection.space_approval_state) {
    case ApprovalState::kAllowed:
      DispatchSpaceCall(connection, std::move(pending));
      return;
    case ApprovalState::kPending:
      connection.queued_space_calls.push_back(std::move(pending));
      return;
    case ApprovalState::kNotRequested:
      connection.queued_space_calls.push_back(std::move(pending));
      RequestSpaceApproval(connection, std::move(reason_text));
      return;
    case ApprovalState::kDenied:
      break;
  }
  connection.active_tool_calls.erase(request_id);
  SendError(connection, request_id, AuthorizationDenied());
}

void DaoMcpService::RequestSpaceApproval(ConnectionState& connection,
                                         std::string reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  BrowserWindowInterface* browser = nullptr;
  if (connection.space_profile) {
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* candidate) {
          if (candidate->GetProfile() != connection.space_profile.get() ||
              candidate->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
            return true;
          }
          browser = candidate;
          return false;
        });
  } else {
    browser = GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  }
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  if (!browser || !profile ||
      browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      !DaoAgentSpaceService::GetForProfile(profile)) {
    FailQueuedSpaceCalls(
        connection,
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "Open a regular Dao browser window, then retry. "
                         "Background spaces use that window's profile.",
                         true));
    return;
  }
  connection.space_profile = profile->GetWeakPtr();
  connection.space_approval_state = ApprovalState::kPending;
  DaoMcpApprovalRequest request(*connection.client_info, std::move(reason),
                                base::Time::Now());
  request.background_space = true;
  const uint64_t connection_generation = connection.generation;
  const std::string connection_id = connection.connection_id;
  connection.space_approval_timer.Start(
      FROM_HERE, approval_timeout_,
      base::BindOnce(&DaoMcpService::OnSpaceApprovalTimeout,
                     weak_factory_.GetWeakPtr(), connection_generation,
                     connection_id));
  if (!approval_delegate_) {
    OnSpaceApprovalResult(connection_generation, connection_id, false);
    return;
  }
  approval_delegate_->RequestApproval(
      request, browser, SpaceApprovalId(connection_id),
      base::BindOnce(&DaoMcpService::OnSpaceApprovalResult,
                     weak_factory_.GetWeakPtr(), connection_generation,
                     connection_id));
}

void DaoMcpService::OnSpaceApprovalResult(uint64_t connection_generation,
                                          std::string connection_id,
                                          bool allowed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection || connection->closing ||
      connection->connection_id != connection_id ||
      connection->space_approval_state != ApprovalState::kPending) {
    return;
  }
  connection->space_approval_timer.Stop();
  if (!allowed) {
    connection->space_approval_state = ApprovalState::kDenied;
    FailQueuedSpaceCalls(*connection, AuthorizationDenied());
    return;
  }
  if (!connection->space_profile) {
    connection->space_approval_state = ApprovalState::kNotRequested;
    FailQueuedSpaceCalls(*connection, AuthorizationDenied());
    return;
  }
  const auto key = std::make_pair(connection->space_profile->GetPath(),
                                 connection->client_info->name);
  const base::TimeTicks now = space_approval_clock_->NowTicks();
  std::erase_if(space_approval_expirations_, [now](const auto& grant) {
    return now >= grant.second;
  });
  space_approval_expirations_[key] = now + base::Hours(1);
  std::vector<uint64_t> approved;
  for (auto& [generation, candidate] : connections_) {
    if (!candidate->closing && candidate->client_info &&
        candidate->space_profile &&
        candidate->space_approval_state == ApprovalState::kPending &&
        candidate->space_profile->GetPath() == key.first &&
        candidate->client_info->name == key.second) {
      candidate->space_approval_state = ApprovalState::kAllowed;
      candidate->space_approval_timer.Stop();
      approved.push_back(generation);
    }
  }
  for (uint64_t generation : approved) {
    connection = FindConnection(generation);
    if (!connection || connection->closing) {
      continue;
    }
    if (generation != connection_generation && approval_delegate_) {
      approval_delegate_->CancelApproval(
          SpaceApprovalId(connection->connection_id));
      connection = FindConnection(generation);
      if (!connection || connection->closing) {
        continue;
      }
    }
    std::vector<PendingToolCall> queued =
        std::exchange(connection->queued_space_calls, {});
    for (PendingToolCall& pending : queued) {
      // A synchronous completion may reject and erase the connection.
      connection = FindConnection(generation);
      if (!connection || connection->closing) {
        break;
      }
      DispatchSpaceCall(*connection, std::move(pending));
    }
  }
}

void DaoMcpService::OnSpaceApprovalTimeout(uint64_t connection_generation,
                                           std::string connection_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection || connection->connection_id != connection_id ||
      connection->space_approval_state != ApprovalState::kPending) {
    return;
  }
  connection->space_approval_state = ApprovalState::kNotRequested;
  if (approval_delegate_) {
    approval_delegate_->CancelApproval(SpaceApprovalId(connection_id));
  }
  FailQueuedSpaceCalls(
      *connection,
      MakeDaoToolError(DaoToolErrorCode::kAuthorizationTimeout,
                       "The background space approval timed out.", true));
}

void DaoMcpService::DispatchSpaceCall(ConnectionState& connection,
                                      PendingToolCall pending) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string request_id = pending.call.request_id;
  Profile* profile = connection.space_profile.get();
  DaoAgentSpaceService* spaces =
      profile ? DaoAgentSpaceService::GetForProfile(profile) : nullptr;
  if (!spaces || !connection.client_info) {
    connection.active_tool_calls.erase(request_id);
    SendError(connection, request_id,
              MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                               "The browser profile for background spaces "
                               "is no longer available."));
    return;
  }
  connection.space_tool_calls.insert(request_id);
  RecordAcceptedToolCall(connection.generation,
                         profile->GetOriginalProfile()->GetPrefs(),
                         pending.call.name);
  DaoAgentSpaceClient client{DaoToolClient::kMcp, connection.connection_id,
                            connection.client_info->name};
  if (!IsSpaceClientRemembered(connection)) {
    auto grant = space_approval_expirations_.find(
        {profile->GetPath(), connection.client_info->name});
    if (grant == space_approval_expirations_.end()) {
      connection.space_tool_calls.erase(request_id);
      connection.active_tool_calls.erase(request_id);
      SendError(connection, request_id, AuthorizationDenied());
      return;
    }
    client.authorization_deadline = grant->second;
  }
  spaces->ExecuteTool(
      client, std::move(pending.call),
      base::BindOnce(&DaoMcpService::OnSpaceToolCallComplete,
                     weak_factory_.GetWeakPtr(), connection.generation,
                     request_id));
}

void DaoMcpService::OnSpaceToolCallComplete(uint64_t connection_generation,
                                            std::string request_id,
                                            DaoBrowserToolResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++tool_call_completion_count_for_testing_;
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection) {
    return;
  }
  connection->space_tool_calls.erase(request_id);
  if (!connection->active_tool_calls.erase(request_id)) {
    return;
  }
  connection->last_activity = base::TimeTicks::Now();
  if (!connection->closing) {
    SendSuccess(*connection, request_id,
                SerializeDaoBrowserToolResult(std::move(result)));
  }
}

void DaoMcpService::FailQueuedSpaceCalls(ConnectionState& connection,
                                         const DaoToolError& error) {
  std::vector<PendingToolCall> queued =
      std::exchange(connection.queued_space_calls, {});
  for (const PendingToolCall& pending : queued) {
    connection.active_tool_calls.erase(pending.call.request_id);
    SendError(connection, pending.call.request_id, error);
  }
}

void DaoMcpService::CancelSpaceCalls(ConnectionState& connection,
                                     const DaoToolError& error) {
  connection.space_approval_timer.Stop();
  const bool cancel_pending =
      connection.space_approval_state == ApprovalState::kPending &&
      !connection.connection_id.empty();
  connection.space_approval_state = ApprovalState::kNotRequested;
  if (approval_delegate_ && cancel_pending) {
    approval_delegate_->CancelApproval(
        SpaceApprovalId(connection.connection_id));
  }
  FailQueuedSpaceCalls(connection, error);
  DaoAgentSpaceService* spaces =
      connection.space_profile
          ? DaoAgentSpaceService::GetForProfile(connection.space_profile.get())
          : nullptr;
  if (spaces && !connection.space_tool_calls.empty()) {
    // Completions arrive synchronously and are written while still writable.
    spaces->CancelClientTools(connection.connection_id);
  }
  for (const std::string& request_id : connection.space_tool_calls) {
    connection.active_tool_calls.erase(request_id);
  }
  connection.space_tool_calls.clear();
}

bool DaoMcpService::IsSpaceClientRemembered(
    const ConnectionState& connection) const {
  if (!local_state_ || !connection.client_info) {
    return false;
  }
  return local_state_->GetList(prefs::kDaoAgentSpaceAllowedClients)
      .contains(connection.client_info->name);
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

base::expected<BrowserWindowInterface*, DaoToolError>
DaoMcpService::PrepareApprovalSession(ConnectionState& connection,
                                      const std::string* tab_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!connection.client_info || !connection.target_contexts.empty() ||
      connection.approval_state != ApprovalState::kNotRequested) {
    return base::unexpected(
        InvalidRequest("The MCP approval session is already initialized."));
  }

  BrowserWindowInterface* browser_window =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  BrowserWindowInterface* browser = browser_window;
  tabs::TabInterface* active_tab =
      browser_window ? browser_window->GetActiveTabInterface() : nullptr;
  if (tab_id) {
    browser_window = nullptr;
    active_tab = nullptr;
    bool ambiguous = false;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* candidate) {
          auto* tab_list = TabListInterface::From(candidate);
          if (!tab_list) {
            return true;
          }
          for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
            if (GetSidebarTabId(tab->GetContents()) != *tab_id) {
              continue;
            }
            if (active_tab) {
              ambiguous = true;
              return false;
            }
            browser_window = candidate;
            active_tab = tab;
          }
          return true;
        });
    if (ambiguous) {
      return base::unexpected(
          InvalidRequest("The requested tab id is ambiguous."));
    }
    browser = browser_window;
  }
  content::WebContents* target =
      active_tab ? active_tab->GetContents() : nullptr;
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  if (!browser || !profile || browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
      profile->IsOffTheRecord() || profile->IsGuestSession()) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kTargetForbidden,
        "The selected window is not eligible for MCP automation."));
  }
  connection.window_session =
      std::make_unique<DaoBrowserAutomationSession>(browser_window, nullptr);
  connection.can_create_tabs = !tab_id;
  connection.window_monitor = std::make_unique<DaoMcpSessionLifecycleMonitor>(
      connection.window_session.get(),
      base::BindOnce(&DaoMcpService::OnWindowInvalidated,
                     weak_factory_.GetWeakPtr(), connection.generation),
      true);
  connection.window_monitor->Start();
  if (tab_id) {
    auto target_id = AddTargetContext(connection, browser_window, target);
    if (!target_id.has_value()) {
      connection.window_monitor.reset();
      connection.window_session.reset();
      return base::unexpected(std::move(target_id).error());
    }
    connection.default_target_id = std::move(target_id).value();
    connection.initial_candidate = ConnectionState::PageCandidate{
        .handle = active_tab->GetHandle(),
        .contents = target->GetWeakPtr(),
        .url = target->GetLastCommittedURL().spec(),
        .document_sequence_number = GetDefaultTargetContext(connection)
                                        ->session->document_sequence_number()};
  }
  return browser;
}

void DaoMcpService::RequestApproval(ConnectionState& connection,
                                    BrowserWindowInterface* browser,
                                    std::string reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DaoMcpApprovalRequest request(*connection.client_info, std::move(reason),
                                base::Time::Now());
  connection.approval_state = ApprovalState::kPending;
  connection.approval_deadline = base::TimeTicks::Now() + approval_timeout_;
  UpdateStatus();
  TargetContext* context = GetDefaultTargetContext(connection);
  if (connection.closing ||
      connection.approval_state != ApprovalState::kPending ||
      !connection.client_info || !connection.window_session) {
    return;
  }
  BrowserWindowInterface* browser_window =
      connection.window_session->browser_window();
  BrowserWindowInterface* approval_browser = browser_window;
  if (!approval_browser || approval_browser != browser) {
    RejectConnection(connection, MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The browser selected for MCP approval is no longer available."));
    return;
  }
  if (connection.initial_candidate) {
    if (!context) {
      RejectConnection(connection,
                       MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                                        "The requested MCP browser target is "
                                        "no longer available."));
      return;
    }
    auto target = context->session->ResolveTarget();
    if (!target.has_value()) {
      RejectConnection(connection, std::move(target).error());
      return;
    }
    request.tab_target = DaoToolTarget{
        .tab_id = connection.default_target_id,
        .url = (*target)->GetLastCommittedURL().spec()};
    request.tab_title = (*target)->GetTitle();
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
  approval_delegate_->RequestApproval(
      request, approval_browser, connection_id,
      base::BindOnce(&DaoMcpService::OnApprovalResult,
                     weak_factory_.GetWeakPtr(), connection_generation,
                     connection_id));
}

void DaoMcpService::RequestTabAccess(ConnectionState& connection,
                                     PendingToolCall pending) {
  const uint64_t generation = connection.generation;
  const std::string request_id = pending.call.request_id;
  if (!connection.page_request_id.empty()) {
    OnToolCallComplete(generation, request_id, false,
                       ErrorResult(InvalidRequest(
                           "Another page access request is already pending.")));
    return;
  }
  BrowserWindowInterface* window = connection.window_session->browser_window();
  BrowserWindowInterface* browser = window;
  TabListInterface* tab_list =
      window ? TabListInterface::From(window) : nullptr;
  if (!browser || !tab_list || !approval_delegate_) {
    OnToolCallComplete(generation, request_id, false,
                       ErrorResult(AuthorizationDenied()));
    return;
  }
  const std::string* requested_id = pending.call.arguments.FindString("tab_id");
  if (!requested_id) {
    // The picker never exposes IDs, so restored duplicates must not block it.
    RepairDuplicateTabIds(tab_list);
  }
  DaoMcpApprovalRequest request(*connection.client_info,
                                *pending.call.arguments.FindString("reason"),
                                base::Time::Now());
  for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
    content::WebContents* contents = tab->GetContents();
    const std::string tab_id = GetOrCreateSidebarTabId(contents);
    if ((requested_id && *requested_id != tab_id) ||
        !ValidateExternalTarget(browser, browser->GetProfile(), contents)
             .has_value()) {
      continue;
    }
    DaoBrowserAutomationSession snapshot(window, contents);
    if (!connection.page_candidates
             .emplace(tab_id,
                      ConnectionState::PageCandidate{
                          .handle = tab->GetHandle(),
                          .contents = contents->GetWeakPtr(),
                          .url = contents->GetLastCommittedURL().spec(),
                          .document_sequence_number =
                              snapshot.document_sequence_number()})
             .second) {
      connection.page_candidates.clear();
      OnToolCallComplete(
          generation, request_id, false,
          ErrorResult(InvalidRequest("The requested tab id is ambiguous.")));
      return;
    }
    DaoToolTarget target{.tab_id = tab_id,
                         .url = contents->GetLastCommittedURL().spec()};
    if (requested_id) {
      request.tab_target = std::move(target);
      request.tab_title = contents->GetTitle();
    } else {
      request.tab_choices.push_back(
          {.target = std::move(target), .title = contents->GetTitle()});
    }
  }
  if (connection.page_candidates.empty()) {
    OnToolCallComplete(generation, request_id, false,
                       ErrorResult(AuthorizationDenied()));
    return;
  }
  const uint64_t serial = ++connection.page_approval_serial;
  connection.page_request_id = request_id;
  connection.page_approval_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  if (requested_id && connection.target_contexts.contains(*requested_id)) {
    OnTabAccessResult(generation, serial, *requested_id);
    return;
  }
  connection.page_approval_timer.Start(
      FROM_HERE, approval_timeout_,
      base::BindOnce(
          [](base::WeakPtr<DaoMcpService> service, uint64_t generation,
             uint64_t serial) {
            ConnectionState* connection =
                service ? service->FindConnection(generation) : nullptr;
            if (connection && connection->page_approval_serial == serial) {
              service->CancelTabAccess(
                  *connection,
                  MakeDaoToolError(DaoToolErrorCode::kAuthorizationTimeout,
                                   "The page access request timed out.", true));
            }
          },
          weak_factory_.GetWeakPtr(), generation, serial));
  approval_delegate_->RequestTabAccess(
      request, browser, connection.page_approval_id,
      base::BindOnce(&DaoMcpService::OnTabAccessResult,
                     weak_factory_.GetWeakPtr(), generation, serial));
}

void DaoMcpService::OnTabAccessResult(uint64_t generation,
                                      uint64_t serial,
                                      std::optional<std::string> tab_id) {
  ConnectionState* connection = FindConnection(generation);
  if (!connection || connection->closing ||
      connection->approval_state != ApprovalState::kAllowed ||
      connection->page_approval_serial != serial ||
      connection->page_request_id.empty()) {
    return;
  }
  const std::string request_id = std::move(connection->page_request_id);
  connection->page_request_id.clear();
  connection->page_approval_id.clear();
  connection->page_approval_timer.Stop();
  auto candidates = std::move(connection->page_candidates);
  connection->page_candidates.clear();
  auto candidate = tab_id ? candidates.find(*tab_id) : candidates.end();
  if (candidate == candidates.end()) {
    OnToolCallComplete(generation, request_id, false,
                       ErrorResult(AuthorizationDenied()));
    return;
  }
  auto& snapshot = candidate->second;
  tabs::TabInterface* tab = snapshot.handle.Get();
  content::WebContents* contents = snapshot.contents.get();
  BrowserWindowInterface* window = connection->window_session->browser_window();
  TabListInterface* tab_list = window ? TabListInterface::From(window) : nullptr;
  DaoBrowserAutomationSession current(window, contents);
  if (!contents || !tab || !tab_list || tab->GetContents() != contents ||
      GetSidebarTabId(contents) != *tab_id ||
      std::ranges::count_if(tab_list->GetAllTabs(), [&tab_id](auto* entry) {
        return GetSidebarTabId(entry->GetContents()) == *tab_id;
      }) != 1 ||
      contents->GetLastCommittedURL().spec() != snapshot.url ||
      current.document_sequence_number() != snapshot.document_sequence_number) {
    OnToolCallComplete(generation, request_id, false,
                       ErrorResult(MakeDaoToolError(
                           DaoToolErrorCode::kTargetGone,
                           "The page changed while approval was pending.")));
    return;
  }
  auto granted = AddTargetContext(*connection, window, contents);
  if (!granted.has_value()) {
    OnToolCallComplete(generation, request_id, false,
                       ErrorResult(std::move(granted).error()));
    return;
  }
  connection->default_target_id = *granted;
  DaoBrowserToolResult result;
  result.ok = true;
  result.target = DaoToolTarget{.tab_id = *granted, .url = snapshot.url};
  result.data =
      base::Value(base::DictValue()
                      .Set("tab_id", *granted)
                      .Set("url", snapshot.url)
                      .Set("title", base::UTF16ToUTF8(contents->GetTitle())));
  OnToolCallComplete(generation, request_id, false, std::move(result));
  UpdateStatus(/*notify_if_unchanged=*/true);
}

void DaoMcpService::CancelTabAccess(ConnectionState& connection,
                                    DaoToolError error) {
  if (connection.page_request_id.empty()) {
    return;
  }
  const std::string request_id = std::move(connection.page_request_id);
  const std::string approval_id = std::move(connection.page_approval_id);
  connection.page_request_id.clear();
  connection.page_approval_id.clear();
  ++connection.page_approval_serial;
  connection.page_approval_timer.Stop();
  connection.page_candidates.clear();
  if (approval_delegate_) {
    approval_delegate_->CancelApproval(approval_id);
  }
  OnToolCallComplete(connection.generation, request_id, false,
                     ErrorResult(std::move(error)));
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
  if (connection->initial_candidate) {
    const auto& candidate = *connection->initial_candidate;
    auto* tab = candidate.handle.Get();
    auto* target = candidate.contents.get();
    DaoBrowserAutomationSession snapshot(
        connection->window_session->browser_window(), target);
    if (!tab || !target || tab->GetContents() != target ||
        candidate.url != target->GetLastCommittedURL().spec() ||
        candidate.document_sequence_number !=
            snapshot.document_sequence_number()) {
      RejectConnection(*connection, AuthorizationDenied());
      return;
    }
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
  // A newly approved connection joins the permission UI even when another
  // connection still drives the summary status.
  if (!context) {
    connection->approval_timer.Stop();
    UpdateStatus(/*notify_if_unchanged=*/true);
    DispatchPendingCalls(*connection);
    return;
  }
  auto acquired = AcquireTargetLease(*connection, *context);
  if (acquired.has_value()) {
    connection->approval_timer.Stop();
    UpdateStatus(/*notify_if_unchanged=*/true);
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
  BrowserWindowInterface* browser = browser_window;
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  auto target_policy = ValidateExternalTarget(
      browser, profile, target, allow_uncommitted_url);
  if (!target_policy.has_value()) {
    return base::unexpected(std::move(target_policy).error());
  }

  if (!connection.window_session ||
      connection.window_session->browser_window() != browser_window ||
      connection.window_session->profile() != profile) {
    return base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kAuthorizationDenied,
                         "MCP tabs must remain in the approved window."));
  }

  const std::string target_id = GetOrCreateSidebarTabId(target);
  if (auto existing = connection.target_contexts.find(target_id);
      existing != connection.target_contexts.end()) {
    auto existing_target = existing->second->session->ResolveTarget();
    if (!existing_target.has_value() || *existing_target != target) {
      return base::unexpected(
          InvalidRequest("The requested tab id no longer matches its grant."));
    }
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
  BrowserWindowInterface* browser = browser_window;
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
                                     DaoToolError error,
                                     bool notify_if_status_unchanged) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Approved connections are listed in permission UI even without a lease.
  notify_if_status_unchanged |=
      connection.approval_state == ApprovalState::kAllowed ||
      std::ranges::any_of(
          connection.target_contexts,
          [](const auto& entry) { return entry.second->lease.has_value(); });
  connection.hello_timer.Stop();
  connection.approval_timer.Stop();
  connection.lease_retry_timer.Stop();
  const bool cancel_pending_approval =
      connection.approval_state == ApprovalState::kPending &&
      !connection.connection_id.empty();
  connection.approval_state = ApprovalState::kDenied;
  CancelTabAccess(connection, error);
  if (approval_delegate_ && cancel_pending_approval) {
    approval_delegate_->CancelApproval(connection.connection_id);
  }
  FailPendingCalls(connection, error);
  CancelSpaceCalls(connection, error);
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
  UpdateStatus(notify_if_status_unchanged);
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
  std::unique_ptr<TargetContext> removed = std::move(context->second);
  connection->target_contexts.erase(context);
  const bool was_controlled = removed->lease.has_value();
  if (connection->default_target_id == target_id) {
    connection->default_target_id.clear();
  }
  FailPendingCallsForTarget(*connection, target_id, error);
  std::vector<std::string> active_tab_calls;
  for (const auto& [request_id, actual_target] :
       connection->active_tool_targets) {
    if (actual_target == target_id &&
        connection->tab_tool_sessions.contains(request_id)) {
      active_tab_calls.push_back(request_id);
    }
  }
  for (const auto& request_id : active_tab_calls) {
    connection->tab_tool_executor->Cancel(request_id);
  }
  removed->tool_executor->CancelAll(error);
  removed->tool_executor->ClearSessionState(removed->session.get());
  removed->devtools_client->Detach();
  removed.reset();

  UpdateStatus(was_controlled);
}

void DaoMcpService::FailPendingCalls(ConnectionState& connection,
                                     const DaoToolError& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  connection.last_activity = base::TimeTicks::Now();
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
  connection.last_activity = base::TimeTicks::Now();
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
  const uint64_t generation = connection.generation;
  // Executing a tool can synchronously disable MCP and destroy its connection.
  while (ConnectionState* current = FindConnection(generation)) {
    if (current->closing || current->pending_tool_calls.empty()) {
      return;
    }
    auto it = current->pending_tool_calls.begin();
    PendingToolCall pending = std::move(it->second);
    current->pending_tool_calls.erase(it);
    DispatchToolCall(*current, std::move(pending));
  }
}

void DaoMcpService::DispatchToolCall(ConnectionState& connection,
                                     PendingToolCall pending) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const uint64_t connection_generation = connection.generation;
  const std::string request_id = pending.call.request_id;
  connection.active_tool_targets.emplace(request_id, pending.target_id);
  if (pending.group == DaoBrowserToolGroup::kTabs) {
    if (pending.call.name == "request_tab_access") {
      RequestTabAccess(connection, std::move(pending));
      return;
    }
    content::WebContents* target = nullptr;
    if (!IsDaoMcpWindowTool(pending.call.name)) {
      auto context = connection.target_contexts.find(pending.target_id);
      if (context != connection.target_contexts.end()) {
        auto resolved = context->second->session->ResolveTarget();
        if (resolved.has_value() &&
            CanAccessTab(connection_generation, *resolved)) {
          target = *resolved;
        }
      }
      if (!target) {
        OnToolCallComplete(connection_generation, request_id, false,
                           ErrorResult(AuthorizationDenied()));
        return;
      }
    }
    const bool allow_uncommitted_url = pending.call.name == "open_tab";
    auto session = std::make_unique<DaoBrowserAutomationSession>(
        connection.window_session->browser_window(), target);
    session->set_tab_scoped(!connection.can_create_tabs);
    session->SetTabAccessCheck(base::BindRepeating(
        [](base::WeakPtr<DaoMcpService> service, uint64_t generation,
           content::WebContents* target) {
          return service && service->CanAccessTab(generation, target);
        },
        weak_factory_.GetWeakPtr(), connection_generation));
    DaoBrowserAutomationSession* session_ptr = session.get();
    connection.tab_tool_sessions.emplace(request_id, std::move(session));
    Profile* profile = connection.window_session->profile();
    PrefService* prefs =
        profile ? profile->GetOriginalProfile()->GetPrefs() : nullptr;
    const std::string tool_name = pending.call.name;
    if (connection.tab_tool_executor->Execute(
            session_ptr, DaoToolClient::kMcp, std::move(pending.call),
            base::BindOnce(&DaoMcpService::OnToolCallComplete,
                           weak_factory_.GetWeakPtr(), connection_generation,
                           request_id, allow_uncommitted_url))) {
      RecordAcceptedToolCall(connection_generation, prefs, tool_name);
    }
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
  Profile* profile = context->second->session->profile();
  PrefService* prefs =
      profile ? profile->GetOriginalProfile()->GetPrefs() : nullptr;
  const std::string tool_name = pending.call.name;
  if (context->second->tool_executor->Execute(
          context->second->session.get(), DaoToolClient::kMcp,
          std::move(pending.call),
          base::BindOnce(&DaoMcpService::OnToolCallComplete,
                         weak_factory_.GetWeakPtr(), connection_generation,
                         request_id, false))) {
    RecordAcceptedToolCall(connection_generation, prefs, tool_name);
  }
}

void DaoMcpService::RecordAcceptedToolCall(uint64_t connection_generation,
                                           PrefService* prefs,
                                           std::string tool_name) {
  RecordMcpToolUsage(prefs, tool_name);
  ConnectionState* connection = FindConnection(connection_generation);
  if (!connection) {
    return;
  }
  connection->latest_tool_name = std::move(tool_name);
  ++connection->latest_tool_call_serial;
  UpdateStatus(/*notify_if_unchanged=*/true);
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
  connection->last_activity = base::TimeTicks::Now();
  connection->active_tool_targets.erase(request_id);
  auto bytes = connection->active_tool_call_bytes.find(request_id);
  if (bytes != connection->active_tool_call_bytes.end()) {
    connection->active_tool_call_bytes_total -= bytes->second;
    connection->active_tool_call_bytes.erase(bytes);
  }
  bool target_set_changed = false;
  auto tab_session = connection->tab_tool_sessions.find(request_id);
  if (tab_session != connection->tab_tool_sessions.end()) {
    const bool closed = result.ok && result.data.is_dict() &&
                        result.data.GetDict().contains("closed_tab_id");
    if (closed) {
      result.target.reset();
    } else if (result.ok && !connection->closing &&
               connection->approval_state == ApprovalState::kAllowed) {
      if (allow_uncommitted_url) {
        auto target = tab_session->second->ResolveTarget();
        auto granted =
            target.has_value()
                ? AddTargetContext(*connection,
                                   tab_session->second->browser_window(),
                                   *target, true)
                : base::expected<std::string, DaoToolError>(
                      base::unexpected(std::move(target).error()));
        if (granted.has_value()) {
          connection->default_target_id = *granted;
          target_set_changed = true;
        } else {
          result = ErrorResult(std::move(granted).error());
        }
      } else if (result.target &&
                 connection->target_contexts.contains(result.target->tab_id)) {
        connection->default_target_id = result.target->tab_id;
      }
    }
    connection->tab_tool_sessions.erase(tab_session);
  }
  if (!connection->closing) {
    SendSuccess(*connection, request_id,
                SerializeDaoBrowserToolResult(std::move(result)));
  }
  if (target_set_changed) {
    UpdateStatus(/*notify_if_unchanged=*/true);
  }
}

void DaoMcpService::NotifyStatusObservers() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  status_observers_.Notify(status_);
}

void DaoMcpService::UpdateStatus(bool notify_if_unchanged) {
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
    if (has_lease || (connection->approval_state == ApprovalState::kAllowed &&
                      !connection->approval_timer.IsRunning())) {
      next.state = DaoMcpStatus::kLeaseActive;
      next.client = connection->client_info;
      next.latest_tool_name = connection->latest_tool_name;
      next.latest_tool_call_serial = connection->latest_tool_call_serial;
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
        next.client->verified_pid == status_.client->verified_pid)) &&
      next.latest_tool_name == status_.latest_tool_name &&
      next.latest_tool_call_serial == status_.latest_tool_call_serial;
  if (unchanged && !notify_if_unchanged) {
    return;
  }
  if (!unchanged) {
    status_ = std::move(next);
  }
  NotifyStatusObservers();
}

#else

// No transport is exposed on platforms without authenticated MCP support.
DaoMcpService::DaoMcpService() = default;
DaoMcpService::~DaoMcpService() = default;

void DaoMcpService::Initialize(PrefService* local_state,
                             base::FilePath user_data_dir) {
  if (local_state) {
    local_state->SetBoolean(prefs::kDaoMcpServerEnabled, false);
  }
}

void DaoMcpService::Shutdown() {}

void DaoMcpService::SetEnabled(bool enabled) {
  if (enabled) {
    LOG(WARNING) << "MCP transport is unavailable on this platform";
  }
}

DaoMcpServiceStatus DaoMcpService::GetStatus() const { return {}; }
std::string DaoMcpService::GetMcpConfiguration() const { return {}; }
BrowserWindowInterface* DaoMcpService::GetAuthorizedBrowser() const {
  return nullptr;
}
content::WebContents* DaoMcpService::GetAuthorizedTarget() const {
  return nullptr;
}
bool DaoMcpService::IsTargetControlled(content::WebContents* target) const {
  return false;
}
size_t DaoMcpService::GetControlledTargetCount() const { return 0; }
std::vector<DaoMcpConnectionInfo> DaoMcpService::GetConnections() const {
  return {};
}
bool DaoMcpService::HasWindowPermission(BrowserWindowInterface*) const {
  return false;
}
void DaoMcpService::StopControl(std::string_view) {}
void DaoMcpService::RevokeTab(std::string_view, std::string_view) {}
base::CallbackListSubscription DaoMcpService::AddObserver(
    StatusObserver observer) {
  return {};
}
void DaoMcpService::SetApprovalDelegate(DaoMcpApprovalDelegate* delegate) {}

#endif

}  // namespace dao
