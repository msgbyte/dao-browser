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
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/values.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "dao/browser/agent/dao_agent_proactive_engine.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"
#include "dao/browser/agent/dao_dream_service.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "dao/browser/automation/dao_page_tools.h"
#include "pdf/mojom/pdf.mojom-forward.h"
#include "url/gurl.h"

namespace network {
class SimpleURLLoader;
}  // namespace network

namespace pdf {
class PDFDocumentHelper;
}  // namespace pdf

namespace dao {

class DaoHomeAgentTools;
class DaoHomeMutationLease;

class DaoAgentMemoryService;
class DaoAgentSkillService;
class DaoAgentUI;
class DaoIndexUI;
class DaoAgentWorkspaceService;
class DaoAgentLease;
class DaoBrowserAutomationSession;
class DaoBrowserToolExecutor;

// Serializes native memory context into the object returned to the Agent WebUI.
base::DictValue SerializeMemoryContextForAgentUi(const MemoryContext& context);

bool ShouldCountProactiveOutcomeAsDismissedForScenarioStats(
    const std::string& outcome);

// WebUI config for dao://index
class DaoIndexUIConfig : public content::WebUIConfig {
 public:
  DaoIndexUIConfig();

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

// WebUI config for chrome://memory
class DaoMemoryUIConfig : public content::WebUIConfig {
 public:
  DaoMemoryUIConfig();

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

// WebUI config for chrome://dream
class DaoDreamUIConfig : public content::WebUIConfig {
 public:
  DaoDreamUIConfig();

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

// WebUI config for chrome://agent
class DaoAgentUIConfig : public content::WebUIConfig {
 public:
  DaoAgentUIConfig();

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

// WebUI message handler for Dao Agent sidebar.
class DaoAgentUIHandler : public content::WebUIMessageHandler,
                          public DaoPageTools::UiDelegate {
 public:
  DaoAgentUIHandler();
  ~DaoAgentUIHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

  // DaoPageTools::UiDelegate:
  void MoveCursor(content::WebContents* target,
                  double x,
                  double y,
                  base::OnceCallback<void(bool)> callback) override;
  void PlayClickRipple(content::WebContents* target) override;
  void CancelCursor(content::WebContents* target) override;
  bool IsTargetLocked(content::WebContents* target) override;
  void LockTarget(content::WebContents* target) override;
  void UnlockTarget(content::WebContents* target) override;

 private:
  // Ensures the CDP client is attached to the current agent target.
  // Returns the target WebContents, or nullptr on failure.
  content::WebContents* EnsureAttached();
  bool RequireActiveAgentTurn(const std::string& callback_id);
  content::WebContents* ResolveTargetContents();
  content::WebContents* GetActivePageContents();
  void SetAgentTurnTarget(content::WebContents* target);
  void AbortAgentTurn(DaoToolError error);
  void FinishPendingBeginAgentTurn(const std::string& callback_id);
  bool OwnsActiveHomeTurn(const std::string& turn_id,
                          const base::WeakPtr<content::WebContents>& target);
  void InvalidateHomeMutationLeases();
  void ExecutePageTool(std::string callback_id,
                       std::string tool_name,
                       base::DictValue arguments);
  void ExecuteTabTool(std::string callback_id,
                      std::string tool_name,
                      base::DictValue arguments);
  void OnPageToolComplete(std::string callback_id, DaoBrowserToolResult result);
  void OnTabToolComplete(std::string callback_id,
                         base::WeakPtr<content::WebContents> previous_target,
                         DaoBrowserToolResult result);
  void ResolvePageToolError(std::string callback_id, DaoToolError error);

  // Message handlers called from JS via chrome.send().
  void HandleBeginAgentTurn(const base::ListValue& args);
  void HandleCancelBeginAgentTurn(const base::ListValue& args);
  void HandleCancelHomeHistoryClaim(const base::ListValue& args);
  void HandleEndAgentTurn(const base::ListValue& args);
  void HandleExecuteHomeTool(const base::ListValue& args);
  void HandleCancelBrowserTool(const base::ListValue& args);
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
  void HandleQueryElements(const base::ListValue& args);
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
  void HandleWaitForNetworkResponse(const base::ListValue& args);
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
  void HandleSearchInResources(const base::ListValue& args);

  // Sidebar control.
  void HandleOpenAgentSettings(const base::ListValue& args);
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

  // State for an in-flight getPdfText capture. Only one capture runs at
  // a time per handler instance.
  struct PdfCaptureState {
    PdfCaptureState();
    ~PdfCaptureState();
    PdfCaptureState(const PdfCaptureState&) = delete;
    PdfCaptureState& operator=(const PdfCaptureState&) = delete;

    std::string callback_id;
    GURL initial_url;  // captured before async chain begins
    std::u16string title;
    int32_t page_count = 0;
    int32_t next_page = 0;
    std::string text;  // UTF-8 accumulator
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

  // Domain security and pinned target state for the current Agent turn.
  std::string expected_domain_;
  std::string active_turn_id_;
  std::string pending_begin_callback_id_;
  scoped_refptr<DaoHomeMutationLease> home_turn_authorization_;
  std::optional<DaoToolError> agent_turn_unavailable_error_;
  std::unique_ptr<DaoAgentLease> agent_turn_lease_;
  std::unique_ptr<DaoBrowserAutomationSession> agent_turn_session_;
  std::unique_ptr<DaoHomeAgentTools> home_agent_tools_;
  std::map<std::string, scoped_refptr<DaoHomeMutationLease>>
      home_mutation_leases_;

  std::unique_ptr<DaoDevToolsClient> devtools_client_;
  std::unique_ptr<DaoBrowserToolExecutor> browser_tool_executor_;
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
  void RefreshPersonalScenarios();

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

// Report-only Dream message handler used by the standalone dream page.
class DaoDreamReportHandler : public content::WebUIMessageHandler {
 public:
  DaoDreamReportHandler();
  ~DaoDreamReportHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleGetDreamReport(const base::ListValue& args);
  void HandleGetDreamReports(const base::ListValue& args);
  void HandleGetTodayDreamReport(const base::ListValue& args);
  void HandleMarkDreamReportViewed(const base::ListValue& args);
  void HandleGetWeeklyDreamReport(const base::ListValue& args);
  void HandleGetWeeklyDreamReports(const base::ListValue& args);
  void HandleGetWeeklyDreamSources(const base::ListValue& args);
  void HandleOpenWeeklyDreamSource(const base::ListValue& args);
  void HandleMarkWeeklyDreamReportViewed(const base::ListValue& args);

  base::CancelableTaskTracker history_task_tracker_;
  base::WeakPtrFactory<DaoDreamReportHandler> weak_factory_{this};
};

// Debug SQL browser handler for dao://memory.
class DaoMemoryBrowserHandler : public content::WebUIMessageHandler {
 public:
  DaoMemoryBrowserHandler();
  ~DaoMemoryBrowserHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  DaoAgentMemoryService* GetMemoryService();

  void HandleGetTables(const base::ListValue& args);
  void HandleExecuteSql(const base::ListValue& args);

  base::WeakPtrFactory<DaoMemoryBrowserHandler> weak_factory_{this};
};

// Shared Dream runner bridge. Registers itself as the DaoDreamService runner
// so a resident WebUI can execute the LLM summarization for dream runs.
class DaoDreamRunnerHandler : public content::WebUIMessageHandler,
                              public DaoDreamService::Runner {
 public:
  DaoDreamRunnerHandler();
  ~DaoDreamRunnerHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // DaoDreamService::Runner:
  void RunDream(const DaoDreamService::DreamRunRequest& request,
                const base::DictValue& material) override;

 protected:
  DaoDreamService* GetDreamService();

 private:
  void HandleDreamComplete(const base::ListValue& args);
  void HandleDreamFailed(const base::ListValue& args);
  void HandleGetDreamExcludedDomains(const base::ListValue& args);
  void HandleAddDreamExcludedDomain(const base::ListValue& args);
  void HandleRemoveDreamExcludedDomain(const base::ListValue& args);
  void HandleStartManualDream(const base::ListValue& args);
  void HandleStartManualWeeklyDream(const base::ListValue& args);

  base::WeakPtrFactory<DaoDreamRunnerHandler> weak_factory_{this};
};

// Agent-page Dream settings and report-card handler.
class DaoAgentDreamHandler : public DaoDreamRunnerHandler {
 public:
  DaoAgentDreamHandler();
  ~DaoAgentDreamHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleGetDreamEnabled(const base::ListValue& args);
  void HandleSetDreamEnabled(const base::ListValue& args);
  void HandleGetDreamDebug(const base::ListValue& args);
  void HandleSetDreamDebug(const base::ListValue& args);
  void HandleGetUnviewedDreamReport(const base::ListValue& args);
  void HandleMarkDreamReportViewed(const base::ListValue& args);
  void HandleOpenDreamReport(const base::ListValue& args);

  base::WeakPtrFactory<DaoAgentDreamHandler> weak_factory_{this};
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
  DaoAgentWorkspaceHandler& operator=(const DaoAgentWorkspaceHandler&) = delete;

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

// WebUI controller for dao://index
class DaoIndexUI : public content::WebUIController {
 public:
  explicit DaoIndexUI(content::WebUI* web_ui);
  ~DaoIndexUI() override;
};

// WebUI controller for chrome://dream
class DaoDreamUI : public content::WebUIController {
 public:
  explicit DaoDreamUI(content::WebUI* web_ui);
  ~DaoDreamUI() override;
};

// WebUI controller for chrome://memory
class DaoMemoryUI : public content::WebUIController {
 public:
  explicit DaoMemoryUI(content::WebUI* web_ui);
  ~DaoMemoryUI() override;
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
