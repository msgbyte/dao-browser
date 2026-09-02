// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_SERVICE_H_
#define DAO_BROWSER_MCP_DAO_MCP_SERVICE_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/process/process_handle.h"
#include "base/sequence_checker.h"
#include "base/threading/sequence_bound.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"
#include "dao/browser/automation/dao_agent_lease_manager.h"
#include "dao/browser/automation/dao_browser_tool_catalog.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"
#include "dao/browser/mcp/dao_mcp_transport.h"

class Browser;
class BrowserWindowInterface;
class PrefService;

namespace base {
class SequencedTaskRunner;
}  // namespace base
namespace content {
class WebContents;
}  // namespace content

namespace dao {

class DaoBrowserAutomationSession;
class DaoBrowserToolExecutor;
class DaoDevToolsClient;
class DaoMcpPageUiDelegate;
class DaoMcpRuntimeFiles;
class DaoMcpSessionLifecycleMonitor;

enum class DaoMcpStatus {
  kDisabled,
  kListening,
  kPendingApproval,
  kLeaseActive,
};

struct DaoMcpClientInfo {
  std::string name;
  std::string version;
  std::optional<base::ProcessId> verified_pid;
};

struct DaoMcpServiceStatus {
  DaoMcpServiceStatus();
  ~DaoMcpServiceStatus();
  DaoMcpServiceStatus(const DaoMcpServiceStatus&);
  DaoMcpServiceStatus& operator=(const DaoMcpServiceStatus&);
  DaoMcpServiceStatus(DaoMcpServiceStatus&&);
  DaoMcpServiceStatus& operator=(DaoMcpServiceStatus&&);

  DaoMcpStatus state = DaoMcpStatus::kDisabled;
  std::optional<DaoMcpClientInfo> client;
};

class DaoMcpApprovalDelegate {
 public:
  virtual ~DaoMcpApprovalDelegate() = default;

  virtual void RequestApproval(const DaoMcpClientInfo& client,
                               Browser* browser,
                               std::string_view connection_id,
                               base::OnceCallback<void(bool)> callback) = 0;
  virtual void CancelApproval(std::string_view) {}
};

std::string BuildDaoMcpConfigurationForBundle(
    const base::FilePath& bundle_path);
base::DictValue BuildDaoMcpUsageStats(PrefService* prefs);
void ResetDaoMcpUsageStats(PrefService* prefs, base::Time last_reset);

class DaoMcpService {
 public:
  using StatusObserver =
      base::RepeatingCallback<void(const DaoMcpServiceStatus&)>;

  static DaoMcpService* Get();

  void Initialize(PrefService* local_state, base::FilePath user_data_dir);
  void Shutdown();

  void SetEnabled(bool enabled);
  DaoMcpServiceStatus GetStatus() const;
  std::string GetMcpConfiguration() const;
  Browser* GetAuthorizedBrowser() const;
  content::WebContents* GetAuthorizedTarget() const;
  bool IsTargetControlled(content::WebContents* target) const;
  size_t GetControlledTargetCount() const;
  void StopControl();
  base::CallbackListSubscription AddObserver(StatusObserver observer);
  void SetApprovalDelegate(DaoMcpApprovalDelegate* delegate);
  void SetTimeoutsForTesting(base::TimeDelta hello_timeout,
                             base::TimeDelta approval_timeout);
  void SetDevToolsCommandCallbackForTesting(
      base::RepeatingCallback<void(const std::string&)> callback);
  size_t active_tool_call_count_for_testing() const;
  size_t pending_executor_call_count_for_testing() const;
  size_t pending_devtools_command_count_for_testing() const;
  bool devtools_attached_for_testing() const;
  size_t page_operation_count_for_testing() const;
  size_t page_lock_count_for_testing() const;
  size_t page_highlight_count_for_testing() const;
  size_t page_cursor_count_for_testing() const;
  void TrackPageCursorForTesting(content::WebContents* target);
  size_t tool_call_completion_count_for_testing() const {
    return tool_call_completion_count_for_testing_;
  }
  bool connection_active_for_testing() const { return !connections_.empty(); }

 private:
  friend class base::NoDestructor<DaoMcpService>;

  enum class ApprovalState {
    kNotRequested,
    kPending,
    kAllowed,
    kDenied,
  };

  struct PendingToolCall {
    PendingToolCall();
    ~PendingToolCall();
    PendingToolCall(PendingToolCall&&) noexcept;
    PendingToolCall& operator=(PendingToolCall&&) noexcept;

    PendingToolCall(const PendingToolCall&) = delete;
    PendingToolCall& operator=(const PendingToolCall&) = delete;

    DaoBrowserToolCall call;
    DaoBrowserToolGroup group = DaoBrowserToolGroup::kPage;
    std::string target_id;
    size_t buffered_bytes = 0;
  };

  struct TargetContext;
  struct ConnectionState;

  DaoMcpService();
  ~DaoMcpService();

  void OnEnabledPrefChanged();
  void StartListening();
  void OnRuntimePrepared(uint64_t runtime_generation,
                         base::expected<void, DaoToolError> prepared);
  void OnTransportListening(uint64_t runtime_generation, int result);
  void OnRuntimePublished(uint64_t runtime_generation,
                          base::expected<void, DaoToolError> published);
  void QueueRuntimeCleanup();
  void StopListening();
  void OnTransportAccepted(uint64_t runtime_generation,
                           uint64_t connection_generation,
                           std::optional<base::ProcessId> verified_pid);
  void OnTransportRequest(uint64_t runtime_generation,
                          uint64_t connection_generation,
                          DaoMcpRequest request,
                          size_t wire_bytes);
  void OnConnectionClosed(uint64_t runtime_generation,
                          uint64_t connection_generation);
  ConnectionState* FindConnection(uint64_t connection_generation);
  const ConnectionState* FindConnection(uint64_t connection_generation) const;
  ConnectionState* GetDisplayConnection();
  const ConnectionState* GetDisplayConnection() const;
  void ResetConnectionState(ConnectionState& connection);
  void OnHelloTimeout(uint64_t connection_generation);

  void OnRequest(ConnectionState& connection, DaoMcpRequest request);
  void HandleHello(ConnectionState& connection, DaoMcpRequest request);
  void HandleToolsList(ConnectionState& connection, DaoMcpRequest request);
  void HandleToolsCall(ConnectionState& connection, DaoMcpRequest request);
  void HandleToolsCancel(ConnectionState& connection, DaoMcpRequest request);
  void SendSuccess(ConnectionState& connection,
                   std::string id,
                   base::DictValue result);
  void SendError(ConnectionState& connection,
                 const std::optional<std::string>& id,
                 DaoToolError error);
  void CloseConnectionAfterWrites(ConnectionState& connection);

  base::expected<Browser*, DaoToolError> PrepareApprovalSession(
      ConnectionState& connection);
  void RequestApproval(ConnectionState& connection, Browser* browser);
  void OnApprovalResult(uint64_t connection_generation,
                        std::string connection_id,
                        bool allowed);
  void TryAcquireExternalLease(uint64_t connection_generation,
                               std::string connection_id);
  void OnApprovalTimeout(uint64_t connection_generation,
                         std::string connection_id);
  bool IsCurrentApproval(uint64_t connection_generation,
                         std::string_view connection_id) const;
  TargetContext* GetDefaultTargetContext(ConnectionState& connection);
  const TargetContext* GetDefaultTargetContext(
      const ConnectionState& connection) const;
  base::expected<std::string, DaoToolError> AddTargetContext(
      ConnectionState& connection,
      BrowserWindowInterface* browser_window,
      content::WebContents* target,
      bool allow_uncommitted_url = false);
  base::expected<void, DaoToolError> AcquireTargetLease(
      ConnectionState& connection,
      TargetContext& context,
      bool allow_uncommitted_url = false);
  void RejectConnection(ConnectionState& connection,
                        DaoToolError error,
                        bool notify_if_status_unchanged = false);
  void OnTargetInvalidated(uint64_t connection_generation,
                           std::string target_id,
                           DaoToolError error);
  void FailPendingCalls(ConnectionState& connection,
                        const DaoToolError& error);
  void FailPendingCallsForTarget(ConnectionState& connection,
                                 std::string_view target_id,
                                 const DaoToolError& error);
  void DispatchPendingCalls(ConnectionState& connection);
  void DispatchToolCall(ConnectionState& connection, PendingToolCall pending);
  void OnToolCallComplete(uint64_t connection_generation,
                          std::string request_id,
                          bool allow_uncommitted_url,
                          DaoBrowserToolResult result);

  void NotifyStatusObservers();
  void UpdateStatus(bool notify_if_unchanged = false);

  raw_ptr<PrefService> local_state_ = nullptr;
  base::FilePath user_data_dir_;
  PrefChangeRegistrar pref_change_registrar_;
  scoped_refptr<DaoMcpRuntimeFiles> runtime_files_;
  scoped_refptr<base::SequencedTaskRunner> runtime_task_runner_;
  uint64_t runtime_generation_ = 0;
  bool listener_start_pending_ = false;
  bool listener_active_ = false;
  base::SequenceBound<DaoMcpTransport> transport_;
  std::map<uint64_t, std::unique_ptr<ConnectionState>> connections_;

  raw_ptr<DaoMcpApprovalDelegate> approval_delegate_ = nullptr;
  base::TimeDelta hello_timeout_;
  base::TimeDelta approval_timeout_;
  size_t tool_call_completion_count_for_testing_ = 0;

  std::unique_ptr<DaoMcpPageUiDelegate> page_ui_delegate_;
  base::RepeatingCallback<void(const std::string&)>
      devtools_command_callback_for_testing_;

  DaoMcpServiceStatus status_;
  base::RepeatingCallbackList<void(const DaoMcpServiceStatus&)>
      status_observers_;
  bool initialized_ = false;
  bool shutting_down_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoMcpService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_SERVICE_H_
