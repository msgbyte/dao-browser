// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_DEVTOOLS_CLIENT_H_
#define DAO_BROWSER_AUTOMATION_DAO_DEVTOOLS_CLIENT_H_

#include <cstddef>
#include <map>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

namespace content {
class WebContents;
} // namespace content

namespace dao {

// Shared CDP client used by both the Dao Agent and external browser tools.
class DaoDevToolsClient : public content::DevToolsAgentHostClient {
public:
  using CommandResult = base::expected<base::Value, DaoToolError>;
  using ResponseCallback = base::OnceCallback<void(CommandResult)>;
  using EventCallback = base::RepeatingCallback<void(
      content::DevToolsAgentHost *agent_host, const std::string &method,
      const base::DictValue &params)>;
  using CommandCallbackForTesting =
      base::RepeatingCallback<void(const std::string &method)>;

  DaoDevToolsClient();
  ~DaoDevToolsClient() override;

  DaoDevToolsClient(const DaoDevToolsClient &) = delete;
  DaoDevToolsClient &operator=(const DaoDevToolsClient &) = delete;

  // Attaches to `web_contents`, detaching from any previous target.
  bool AttachTo(content::WebContents *web_contents);

  // Detaches from the current target and cancels its pending commands.
  void Detach();

  // Sends a CDP command and returns its command ID, or 0 if it was not sent.
  int SendCommand(const std::string &method, base::DictValue params,
                  ResponseCallback callback,
                  std::optional<size_t> max_response_bytes = std::nullopt);

  void CancelCommand(int command_id, DaoToolError error);
  void CancelAll(DaoToolError error);

  void SetEventCallback(EventCallback callback);
  void SetCommandCallbackForTesting(CommandCallbackForTesting callback);

  bool has_pending_commands() const { return !pending_commands_.empty(); }
  content::DevToolsAgentHost *agent_host() const { return agent_host_.get(); }

  size_t pending_command_count_for_testing() const {
    return pending_commands_.size();
  }

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost *agent_host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost *agent_host) override;
  bool IsTrusted() override;
  std::string GetTypeForMetrics() override;

private:
  struct PendingCommand {
    PendingCommand(ResponseCallback callback,
                   std::optional<size_t> max_response_bytes);
    PendingCommand(PendingCommand &&);
    PendingCommand &operator=(PendingCommand &&);
    ~PendingCommand();

    PendingCommand(const PendingCommand &) = delete;
    PendingCommand &operator=(const PendingCommand &) = delete;

    ResponseCallback callback;
    std::optional<size_t> max_response_bytes;
  };

  bool DetachInternal();

  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  int next_command_id_ = 1;
  std::map<int, PendingCommand> pending_commands_;
  EventCallback event_callback_;
  CommandCallbackForTesting command_callback_for_testing_;
  bool is_shutting_down_ = false;
  bool is_transitioning_ = false;
  bool is_cancelling_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoDevToolsClient> weak_factory_{this};
};

} // namespace dao

#endif // DAO_BROWSER_AUTOMATION_DAO_DEVTOOLS_CLIENT_H_
