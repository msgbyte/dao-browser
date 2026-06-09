// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_AGENT_UI_H_
#define DAO_BROWSER_UI_WEBUI_DAO_AGENT_UI_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
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
#include "dao/browser/agent/dao_agent_workspace_types.h"
#include "pdf/mojom/pdf.mojom-forward.h"
#include "url/gurl.h"

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace pdf {
class PDFDocumentHelper;
}  // namespace pdf

namespace dao {

class DaoAgentMemoryService;
class DaoAgentSkillService;
class DaoAgentUI;
class DaoAgentWorkspaceService;

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
                                   const base::DictValue& params)>;

  DaoAgentDevToolsClient();
  ~DaoAgentDevToolsClient() override;

  // Attach to the given WebContents. Detaches from any previous host.
  bool AttachTo(content::WebContents* web_contents);

  // Detach from the current host.
  void Detach();

  // Send a CDP command. |callback| receives the JSON result.
  void SendCommand(const std::string& method,
                   base::DictValue params,
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
  // Ensures the CDP client is attached to the current agent target.
  // Returns the target WebContents, or nullptr on failure.
  content::WebContents* EnsureAttached();
  content::WebContents* ResolveTargetContents();
  content::WebContents* GetActivePageContents();

  // Message handlers called from JS via chrome.send().
  void HandleBeginAgentTurn(const base::ListValue& args);
  void HandleEndAgentTurn(const base::ListValue& args);
  void HandleGetPageInfo(const base::ListValue& args);
  void HandleClickElement(const base::ListValue& args);
  void HandleExecuteScript(const base::ListValue& args);

  // Captures full text of the active tab if it's a PDF, using
  // pdf::PDFDocumentHelper. Sequentially calls GetPageText() for each
  // page and accumulates text up to ~512 KiB. Resolves with one of:
  //   { isPdf: false }                  -- non-PDF tab
  //   { isPdf: true, error: "..." }     -- PDF detected but capture failed
  //   { isPdf: true, url, title,
  //     pageCount, text,
  //     truncated, truncatedAtPage? }   -- success
  void HandleGetPdfText(const base::ListValue& args);

  void HandleMoveCursor(const base::ListValue& args);
  void HandleAgentClick(const base::ListValue& args);
  void HandleHighlightElement(const base::ListValue& args);
  void HandleClearHighlight(const base::ListValue& args);
  void HandleGetAccessibilityTree(const base::ListValue& args);
  void HandleClickByRef(const base::ListValue& args);
  void HandleCaptureScreenshot(const base::ListValue& args);
  void HandleScrollPage(const base::ListValue& args);
  void HandleScrollToElement(const base::ListValue& args);
  void HandleSetExpectedDomain(const base::ListValue& args);

  // Tab management handlers.
  void HandleListTabs(const base::ListValue& args);
  void HandleSwitchTab(const base::ListValue& args);
  void HandleOpenTab(const base::ListValue& args);
  void HandleCloseTab(const base::ListValue& args);

  // Keyboard/text input handlers.
  void HandlePressKeyChord(const base::ListValue& args);
  void HandleTypeText(const base::ListValue& args);

  // Network/console debugging handlers.
  void HandleEnableNetworkTracking(const base::ListValue& args);
  void HandleGetNetworkRequests(const base::ListValue& args);
  void HandleClearNetworkRequests(const base::ListValue& args);
  void HandleEnableConsoleTracking(const base::ListValue& args);
  void HandleGetConsoleMessages(const base::ListValue& args);
  void HandleClearConsoleMessages(const base::ListValue& args);

  // Reverse-engineering / source inspection handlers.
  // All four rely on the CDP Page/Network domains and are read-only.
  void HandleGetPageHtml(const base::ListValue& args);
  void HandleListPageResources(const base::ListValue& args);
  void HandleGetResourceContent(const base::ListValue& args);
  void HandleGetNetworkBody(const base::ListValue& args);

  // Shared tail of HandleGetResourceContent: run Page.getResourceContent
  // once both frame id and url are known, then reply to |callback_id|.
  void FetchResourceContentAndReply(const std::string& callback_id,
                                    const std::string& url,
                                    const std::string& frame_id);

  // HandleListPageResources continuations — split out because the
  // two-step CDP chain (Page.enable → Page.getResourceTree) runs into
  // confusing lambda-capture diagnostics when inlined.
  void OnPageEnableForResourceList(std::string callback_id,
                                   std::string type_filter,
                                   base::Value result);
  void OnResourceTreeForResourceList(std::string callback_id,
                                     std::string type_filter,
                                     base::Value result);

  // HandleGetResourceContent continuation for the frame-id-lookup path.
  void OnResourceTreeForResourceFetch(std::string callback_id,
                                      std::string url,
                                      base::Value result);
  void OnPageEnableForResourceFetch(std::string callback_id,
                                    std::string url,
                                    base::Value result);

  // Sidebar control.
  void HandleCloseSidebar(const base::ListValue& args);
  void HandleFocusAgentSidebar(const base::ListValue& args);

  // CORS-bypass fetch for the agent's web_search tool. The handler
  // runs SimpleURLLoader in the browser process so requests to e.g.
  // html.duckduckgo.com / r.jina.ai are not subject to the cross-
  // origin checks that block fetch() from `dao://agent`.
  void HandleNativeFetch(const base::ListValue& args);

  // Zero-copy `download` tool: writes content straight into the agent
  // workspace without round-tripping the body through LLM output.
  // Supported sources:
  //   { source: "page", path }      -- captures active tab outerHTML
  //   { source: "url",  path, url } -- fetches URL via SimpleURLLoader
  // On success replies { ok: true, path, bytes_written, source_url,
  // truncated }. On failure replies the standard workspace error shape.
  void HandleWorkspaceDownload(const base::ListValue& args);

  // Per-request state for in-flight nativeFetch calls. The
  // SimpleURLLoader must be kept alive until the response arrives;
  // we key by raw pointer so OnNativeFetchComplete can erase the
  // matching entry.
  struct NativeFetchRequest {
    NativeFetchRequest();
    NativeFetchRequest(NativeFetchRequest&&) noexcept;
    NativeFetchRequest& operator=(NativeFetchRequest&&) noexcept;
    ~NativeFetchRequest();

    std::unique_ptr<network::SimpleURLLoader> loader;
    std::string callback_id;
  };
  std::map<network::SimpleURLLoader*, NativeFetchRequest>
      native_fetch_inflight_;

  void OnNativeFetchComplete(network::SimpleURLLoader* loader_ptr,
                             std::optional<std::string> body);

  // Per-request state for in-flight workspace_download URL fetches.
  // Keyed by raw pointer so OnWorkspaceDownloadFileComplete can erase
  // the matching entry. The URL path streams bytes directly to
  // `staging_path` via SimpleURLLoader::DownloadToFile so multi-hundred-MB
  // downloads never sit in the browser process's heap.
  struct WorkspaceDownloadRequest {
    WorkspaceDownloadRequest();
    WorkspaceDownloadRequest(WorkspaceDownloadRequest&&) noexcept;
    WorkspaceDownloadRequest& operator=(WorkspaceDownloadRequest&&) noexcept;
    ~WorkspaceDownloadRequest();

    std::unique_ptr<network::SimpleURLLoader> loader;
    std::string callback_id;
    std::string workspace_path;
    std::string source_url;
    base::FilePath staging_path;
  };
  std::map<network::SimpleURLLoader*, WorkspaceDownloadRequest>
      workspace_download_inflight_;

  void OnDownloadStagingAllocated(network::SimpleURLLoader* loader_ptr,
                                  base::FilePath staging_path);
  void OnWorkspaceDownloadFileComplete(network::SimpleURLLoader* loader_ptr,
                                       base::FilePath returned_path);

  // Shared tail: write |body| to |path| in the workspace, then reply
  // to |callback_id| with {ok, bytes_written, path, source_url,
  // truncated}. |truncated| reflects whether the source-side capture
  // hit its size cap (CDP path's 512 KiB outerHTML budget, or the URL
  // path's 5 MiB body cap).
  void WriteDownloadedAndReply(const std::string& callback_id,
                               const std::string& path,
                               const std::string& source_url,
                               std::string body,
                               bool truncated);

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
                               base::DictValue press_params);

  // CDP event handler for network/console tracking.
  void OnCDPEvent(const std::string& method,
                  const base::DictValue& params);

  // State for an in-flight getPdfText capture. Only one capture runs at
  // a time per handler instance.
  struct PdfCaptureState {
    PdfCaptureState();
    ~PdfCaptureState();
    PdfCaptureState(const PdfCaptureState&) = delete;
    PdfCaptureState& operator=(const PdfCaptureState&) = delete;

    std::string callback_id;
    GURL initial_url;          // captured before async chain begins
    std::u16string title;
    int32_t page_count = 0;
    int32_t next_page = 0;
    std::string text;          // UTF-8 accumulator
    static constexpr size_t kBudgetBytes = 512 * 1024;
  };

  // Called from HandleGetPdfText once we have a PDFDocumentHelper and
  // the document is loaded. Issues GetPdfBytes(0) and routes the result
  // to OnPdfBytesReceived.
  void StartPdfCapture(std::unique_ptr<PdfCaptureState> state,
                       pdf::PDFDocumentHelper* helper);

  // Receives page_count from GetPdfBytes, then kicks off the page loop.
  void OnPdfBytesReceived(std::unique_ptr<PdfCaptureState> state,
                          pdf::mojom::PdfListener_GetPdfBytesStatus status,
                          const std::vector<uint8_t>& bytes,
                          uint32_t page_count);

  // Issues GetPageText for state->next_page.
  void FetchNextPdfPage(std::unique_ptr<PdfCaptureState> state);

  // Page-text callback: appends, checks budget, then either loops or
  // resolves.
  void OnPdfPageText(std::unique_ptr<PdfCaptureState> state,
                     const std::u16string& page_text);

  // Resolve helpers — each issues exactly one ResolveJavascriptCallback.
  void ResolvePdfCapture(const PdfCaptureState& state,
                         bool truncated,
                         std::optional<int32_t> truncated_at_page);
  void ResolvePdfCaptureError(const PdfCaptureState& state,
                              const std::string& error_message);
  void ResolvePdfCaptureNotPdf(const std::string& callback_id);

  // Domain security: expected domain set at session start.
  std::string expected_domain_;
  base::WeakPtr<content::WebContents> agent_turn_target_;

  // Network tracking state.
  bool network_tracking_enabled_ = false;
  std::vector<base::DictValue> network_requests_;

  // Console tracking state.
  bool console_tracking_enabled_ = false;
  std::vector<base::DictValue> console_messages_;

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

  void HandleGetMemoryContext(const base::ListValue& args);
  void HandleEndSession(const base::ListValue& args);
  void HandleLoadConversations(const base::ListValue& args);
  void HandleGetPreferences(const base::ListValue& args);
  void HandleUpdatePreference(const base::ListValue& args);
  void HandleDeleteMemory(const base::ListValue& args);
  void HandleGetEpisodes(const base::ListValue& args);
  void HandleClearAllMemory(const base::ListValue& args);
  void HandleGetStorageStats(const base::ListValue& args);
  void HandleDismissSuggestion(const base::ListValue& args);
  void HandleAcceptSuggestion(const base::ListValue& args);
  void HandleGetMemoryEnabled(const base::ListValue& args);
  void HandleSetMemoryEnabled(const base::ListValue& args);

  // Scenario & proactive settings handlers.
  void HandleSetProactiveEnabled(const base::ListValue& args);
  void HandleSetConfidenceThreshold(const base::ListValue& args);
  void HandleRecordActionFeedback(const base::ListValue& args);
  void HandleSaveEpisode(const base::ListValue& args);
  void HandleSaveSummary(const base::ListValue& args);
  void HandleGetPageContentForScenario(const base::ListValue& args);

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

  void HandleGetSkillRegistry(const base::ListValue& args);
  void HandleGetSkillContent(const base::ListValue& args);
  void HandleSaveUserSkill(const base::ListValue& args);
  void HandleDeleteUserSkill(const base::ListValue& args);
  void HandleSetSkillDisabled(const base::ListValue& args);
  void HandleOpenSkillsDirectory(const base::ListValue& args);
  void HandleOpenSkillManager(const base::ListValue& args);

  base::WeakPtrFactory<DaoAgentSkillHandler> weak_factory_{this};
};

// Workspace-specific message handler for the Agent file workspace.
class DaoAgentWorkspaceHandler : public content::WebUIMessageHandler {
 public:
  DaoAgentWorkspaceHandler();
  ~DaoAgentWorkspaceHandler() override;
  DaoAgentWorkspaceHandler(const DaoAgentWorkspaceHandler&) = delete;
  DaoAgentWorkspaceHandler& operator=(const DaoAgentWorkspaceHandler&) =
      delete;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  DaoAgentWorkspaceService* GetWorkspaceService();

  void HandleWorkspaceRead(const base::ListValue& args);
  void HandleWorkspaceWrite(const base::ListValue& args);
  void HandleWorkspaceEdit(const base::ListValue& args);
  void HandleWorkspaceApplyPatch(const base::ListValue& args);
  void HandleWorkspaceList(const base::ListValue& args);
  void HandleWorkspaceOpenFolder(const base::ListValue& args);
  void HandleWorkspaceGetRecentActivity(const base::ListValue& args);
  void HandleWorkspaceGetInfo(const base::ListValue& args);

  void ReplyOk(const std::string& cb_id, base::DictValue body);
  void ReplyError(const std::string& cb_id, WorkspaceError err);

  base::WeakPtrFactory<DaoAgentWorkspaceHandler> weak_factory_{this};
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
