// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_AGENT_UI_H_
#define DAO_BROWSER_UI_WEBUI_DAO_AGENT_UI_H_

#include <map>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "dao/browser/agent/dao_agent_proactive_engine.h"

namespace dao {

class DaoAgentMemoryService;
class DaoAgentSkillService;
class DaoAgentUI;

// WebUI config for chrome://agent
class DaoAgentUIConfig : public content::WebUIConfig {
 public:
  DaoAgentUIConfig();

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

// CDP client that bridges WebUI JS to DevTools protocol on the active tab.
class DaoAgentDevToolsClient : public content::DevToolsAgentHostClient {
 public:
  using ResponseCallback = base::OnceCallback<void(base::Value)>;
  using EventCallback =
      base::RepeatingCallback<void(const std::string& method,
                                   const base::Value::Dict& params)>;

  DaoAgentDevToolsClient();
  ~DaoAgentDevToolsClient() override;

  // Attach to the given WebContents. Detaches from any previous host.
  bool AttachTo(content::WebContents* web_contents);

  // Detach from the current host.
  void Detach();

  // Send a CDP command. |callback| receives the JSON result.
  void SendCommand(const std::string& method,
                   base::Value::Dict params,
                   ResponseCallback callback);

  // Set a callback for CDP events (messages without an "id" field).
  void SetEventCallback(EventCallback callback);

  // content::DevToolsAgentHostClient:
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;
  bool IsTrusted() override;
  std::string GetTypeForMetrics() override;

 private:
  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  int next_command_id_ = 1;
  std::map<int, ResponseCallback> pending_callbacks_;
  EventCallback event_callback_;
};

// WebUI message handler for Dao Agent sidebar.
class DaoAgentUIHandler : public content::WebUIMessageHandler {
 public:
  DaoAgentUIHandler();
  ~DaoAgentUIHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  // Ensures the CDP client is attached to the active tab.
  // Returns the active WebContents, or nullptr on failure.
  content::WebContents* EnsureAttached();

  // Message handlers called from JS via chrome.send().
  void HandleGetPageInfo(const base::Value::List& args);
  void HandleClickElement(const base::Value::List& args);
  void HandleExecuteScript(const base::Value::List& args);
  void HandleMoveCursor(const base::Value::List& args);
  void HandleAgentClick(const base::Value::List& args);
  void HandleHighlightElement(const base::Value::List& args);
  void HandleClearHighlight(const base::Value::List& args);
  void HandleGetAccessibilityTree(const base::Value::List& args);
  void HandleClickByRef(const base::Value::List& args);
  void HandleCaptureScreenshot(const base::Value::List& args);
  void HandleScrollPage(const base::Value::List& args);
  void HandleScrollToElement(const base::Value::List& args);
  void HandleSetExpectedDomain(const base::Value::List& args);

  // Tab management handlers.
  void HandleListTabs(const base::Value::List& args);
  void HandleSwitchTab(const base::Value::List& args);
  void HandleOpenTab(const base::Value::List& args);
  void HandleCloseTab(const base::Value::List& args);

  // Keyboard/text input handlers.
  void HandlePressKeyChord(const base::Value::List& args);
  void HandleTypeText(const base::Value::List& args);

  // Network/console debugging handlers.
  void HandleEnableNetworkTracking(const base::Value::List& args);
  void HandleGetNetworkRequests(const base::Value::List& args);
  void HandleClearNetworkRequests(const base::Value::List& args);
  void HandleEnableConsoleTracking(const base::Value::List& args);
  void HandleGetConsoleMessages(const base::Value::List& args);
  void HandleClearConsoleMessages(const base::Value::List& args);

  // Sidebar control.
  void HandleCloseSidebar(const base::Value::List& args);

  void PerformCDPClick(const std::string& callback_id,
                       const std::string& escaped_selector,
                       double viewport_x,
                       double viewport_y,
                       content::WebContents* locked_contents);

  // Second half of PerformCDPClick: dispatches mousePressed then
  // mouseReleased with matching `buttons` bitmask. Split out so the
  // prefix mouseMoved event can be awaited in a separate CDP round trip
  // without ballooning the nested lambda chain.
  void DispatchPressAndRelease(const std::string& callback_id,
                               const std::string& escaped_selector,
                               double viewport_x,
                               double viewport_y,
                               content::WebContents* locked_contents,
                               base::Value::Dict press_params);

  // CDP event handler for network/console tracking.
  void OnCDPEvent(const std::string& method,
                  const base::Value::Dict& params);

  // Domain security: expected domain set at session start.
  std::string expected_domain_;

  // Network tracking state.
  bool network_tracking_enabled_ = false;
  std::vector<base::Value::Dict> network_requests_;

  // Console tracking state.
  bool console_tracking_enabled_ = false;
  std::vector<base::Value::Dict> console_messages_;

  std::unique_ptr<DaoAgentDevToolsClient> devtools_client_;
  base::WeakPtrFactory<DaoAgentUIHandler> weak_factory_{this};
};

// Memory-specific message handler, separate from tool handler.
class DaoAgentMemoryHandler : public content::WebUIMessageHandler,
                               public DaoAgentProactiveEngine::Delegate {
 public:
  DaoAgentMemoryHandler();
  ~DaoAgentMemoryHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // DaoAgentProactiveEngine::Delegate:
  void OnProactiveSuggestion(const ProactiveSuggestion& suggestion) override;

 private:
  DaoAgentMemoryService* GetMemoryService();

  void HandleGetMemoryContext(const base::Value::List& args);
  void HandleEndSession(const base::Value::List& args);
  void HandleLoadConversations(const base::Value::List& args);
  void HandleGetPreferences(const base::Value::List& args);
  void HandleUpdatePreference(const base::Value::List& args);
  void HandleDeleteMemory(const base::Value::List& args);
  void HandleGetEpisodes(const base::Value::List& args);
  void HandleClearAllMemory(const base::Value::List& args);
  void HandleGetStorageStats(const base::Value::List& args);
  void HandleDismissSuggestion(const base::Value::List& args);
  void HandleAcceptSuggestion(const base::Value::List& args);
  void HandleGetMemoryEnabled(const base::Value::List& args);
  void HandleSetMemoryEnabled(const base::Value::List& args);

  // Scenario & proactive settings handlers.
  void HandleSetProactiveEnabled(const base::Value::List& args);
  void HandleSetConfidenceThreshold(const base::Value::List& args);
  void HandleRecordActionFeedback(const base::Value::List& args);
  void HandleSaveEpisode(const base::Value::List& args);
  void HandleSaveSummary(const base::Value::List& args);
  void HandleGetPageContentForScenario(const base::Value::List& args);

  std::unique_ptr<DaoAgentProactiveEngine> proactive_engine_;
  base::WeakPtrFactory<DaoAgentMemoryHandler> weak_factory_{this};
};

// Skill-specific message handler for managing agent skills.
class DaoAgentSkillHandler : public content::WebUIMessageHandler {
 public:
  DaoAgentSkillHandler();
  ~DaoAgentSkillHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  DaoAgentSkillService* GetSkillService();

  void HandleGetSkillRegistry(const base::Value::List& args);
  void HandleGetSkillContent(const base::Value::List& args);
  void HandleSaveUserSkill(const base::Value::List& args);
  void HandleDeleteUserSkill(const base::Value::List& args);
  void HandleOpenSkillsDirectory(const base::Value::List& args);
  void HandleOpenSkillManager(const base::Value::List& args);

  base::WeakPtrFactory<DaoAgentSkillHandler> weak_factory_{this};
};

// WebUI controller for chrome://agent
class DaoAgentUI : public content::WebUIController {
 public:
  explicit DaoAgentUI(content::WebUI* web_ui);
  ~DaoAgentUI() override;
};

// WebUI config for chrome://skills (standalone skill manager page)
class DaoSkillsUIConfig : public content::WebUIConfig {
 public:
  DaoSkillsUIConfig();

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

// WebUI controller for chrome://skills
class DaoSkillsUI : public content::WebUIController {
 public:
  explicit DaoSkillsUI(content::WebUI* web_ui);
  ~DaoSkillsUI() override;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_AGENT_UI_H_
