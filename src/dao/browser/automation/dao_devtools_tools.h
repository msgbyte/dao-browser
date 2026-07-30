// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_DEVTOOLS_TOOLS_H_
#define DAO_BROWSER_AUTOMATION_DAO_DEVTOOLS_TOOLS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

namespace content {
class DevToolsAgentHost;
class WebContents;
} // namespace content

namespace dao {

class DaoBrowserAutomationSession;
class DaoDevToolsClient;

class DaoDevToolsTools {
public:
  using ResultCallback = base::OnceCallback<void(DaoBrowserToolResult)>;

  explicit DaoDevToolsTools(DaoDevToolsClient *devtools_client);
  ~DaoDevToolsTools();

  DaoDevToolsTools(const DaoDevToolsTools &) = delete;
  DaoDevToolsTools &operator=(const DaoDevToolsTools &) = delete;

  static bool Handles(std::string_view name);

  void Execute(std::string request_id, DaoBrowserAutomationSession *session,
               std::string name, base::DictValue arguments,
               ResultCallback callback);
  bool Cancel(std::string_view request_id, DaoToolError error);
  void CancelAll(DaoToolError error);
  void ClearSessionState(DaoBrowserAutomationSession *session);

  static base::DictValue
  TraverseResourceTreeForTesting(const base::DictValue &frame_tree,
                                 std::string_view type_filter);
  static bool ApplyDomainEnableResultForTesting(
      DaoBrowserAutomationSession *session, uint64_t generation,
      content::DevToolsAgentHost *host, uint64_t attempt_epoch,
      bool network_domain, bool success);

private:
  struct Operation;

  Operation *FindOperation(std::string_view request_id);
  bool CancelInternal(std::string_view request_id, DaoToolError error);
  void CancelAllOperations(DaoToolError error);
  void CancelSessionOperations(DaoBrowserAutomationSession *session,
                               DaoToolError error);
  int SendCommand(
      std::string_view request_id, std::string method, base::DictValue params,
      base::OnceCallback<void(base::expected<base::Value, DaoToolError>)>
          callback,
      std::optional<size_t> max_response_bytes = std::nullopt);
  void Finish(std::string_view request_id, DaoBrowserToolResult result);
  void FinishSuccess(std::string_view request_id, base::Value data);
  void FinishError(std::string_view request_id, DaoToolError error);
  void FailPendingDomainEnableAttempt(Operation *operation);
  void CleanupOperation(Operation *operation);
  bool ValidateOperationTarget(std::string_view request_id);

  base::expected<content::WebContents *, DaoToolError>
  SyncSessionBinding(DaoBrowserAutomationSession *session, bool clear_buffers,
                     int rebind_attempt = 0);
  void OnSessionTargetChanged(DaoBrowserAutomationSession *session);
  void ReenableDomains(DaoBrowserAutomationSession *session);
  void OnDomainReenabled(base::WeakPtr<DaoBrowserAutomationSession> session,
                         uint64_t generation,
                         scoped_refptr<content::DevToolsAgentHost> host,
                         uint64_t attempt_epoch, bool network_domain,
                         base::expected<base::Value, DaoToolError> result);
  void ResetBinding(DaoBrowserAutomationSession *session,
                    bool clear_enabled_flags, bool clear_buffers);

  void OnCDPEvent(content::DevToolsAgentHost *agent_host,
                  const std::string &method, const base::DictValue &params);

  void ExecuteEnableNetwork(std::string_view request_id);
  void ExecuteGetNetwork(std::string_view request_id);
  void ExecuteClearNetwork(std::string_view request_id);
  void ExecuteGetNetworkBody(std::string_view request_id);
  void ExecuteEnableConsole(std::string_view request_id);
  void ExecuteGetConsole(std::string_view request_id);
  void ExecuteClearConsole(std::string_view request_id);
  void ExecuteListResources(std::string_view request_id);
  void OnPageEnabledForResourceList(
      std::string request_id, base::expected<base::Value, DaoToolError> result);
  void OnResourceTreeForList(std::string request_id,
                             base::expected<base::Value, DaoToolError> result);
  void ExecuteGetResourceContent(std::string_view request_id);
  void OnPageEnabledForResourceContent(
      std::string request_id, base::expected<base::Value, DaoToolError> result);
  void
  OnResourceTreeForContent(std::string request_id,
                           base::expected<base::Value, DaoToolError> result);
  void FetchResourceContent(std::string_view request_id, std::string frame_id);
  void OnResourceContent(std::string request_id,
                         base::expected<base::Value, DaoToolError> result);
  void ExecuteSearch(std::string_view request_id);
  void OnPageEnabledForSearch(std::string request_id,
                              base::expected<base::Value, DaoToolError> result);
  void
  OnResourceTreeForSearch(std::string request_id,
                          base::expected<base::Value, DaoToolError> result);
  void FetchNextSearchResource(std::string_view request_id);
  void OnSearchDocumentWorld(std::string request_id,
                             base::expected<base::Value, DaoToolError> result);
  void
  OnSearchResourceContent(std::string request_id,
                          base::expected<base::Value, DaoToolError> result);
  void FinishSearch(std::string_view request_id);

  raw_ptr<DaoDevToolsClient> devtools_client_;
  base::WeakPtr<DaoBrowserAutomationSession> bound_session_;
  base::WeakPtr<DaoBrowserAutomationSession> pending_target_change_session_;
  std::map<std::string, std::unique_ptr<Operation>, std::less<>> operations_;
  uint64_t clear_generation_ = 0;
  bool is_shutting_down_ = false;
  bool is_cancelling_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoDevToolsTools> weak_factory_{this};
};

} // namespace dao

#endif // DAO_BROWSER_AUTOMATION_DAO_DEVTOOLS_TOOLS_H_
