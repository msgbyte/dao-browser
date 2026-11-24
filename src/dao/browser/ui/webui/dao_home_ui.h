// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_HOME_UI_H_
#define DAO_BROWSER_UI_WEBUI_DAO_HOME_UI_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "dao/browser/home/dao_home_types.h"
#include "ui/webui/untrusted_web_ui_controller.h"

namespace dao {

class DaoHomeProjectService;
class DaoHomeConnectorExecutor;

class DaoHomeUI;
class DaoHomeAppUI;
class DaoHomeConnectorUI;

class DaoHomeUIConfig : public content::DefaultWebUIConfig<DaoHomeUI> {
 public:
  DaoHomeUIConfig();
  ~DaoHomeUIConfig() override;

  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class DaoHomeAppUIConfig : public content::DefaultWebUIConfig<DaoHomeAppUI> {
 public:
  DaoHomeAppUIConfig();
  ~DaoHomeAppUIConfig() override;

  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class DaoHomeConnectorUIConfig
    : public content::DefaultWebUIConfig<DaoHomeConnectorUI> {
 public:
  DaoHomeConnectorUIConfig();
  ~DaoHomeConnectorUIConfig() override;

  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

class DaoHomeUIHandler : public content::WebUIMessageHandler,
                         public content::WebContentsObserver {
 public:
  using ConnectorCallback = base::OnceCallback<void(base::Value)>;
  using PreviewCallback = base::OnceCallback<void(base::Value)>;

  explicit DaoHomeUIHandler(DaoHomeProjectService* service);
  ~DaoHomeUIHandler() override;

  void RegisterMessages() override;
  void OnJavascriptDisallowed() override;
  void OnDidAddMessageToConsole(
      content::RenderFrameHost* source_frame,
      blink::mojom::ConsoleMessageLevel log_level,
      const std::u16string& message,
      int32_t line_no,
      const std::u16string& source_id,
      const std::optional<std::u16string>& untrusted_stack_trace) override;
  void DidFinishNavigation(content::NavigationHandle* navigation) override;
  void FrameDeleted(content::FrameTreeNodeId frame_tree_node_id) override;
  void PrimaryPageChanged(content::Page& page) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;
  void CollectConnectorForAgent(std::string draft_id,
                                std::string connector_id,
                                base::Value input,
                                ConnectorCallback callback);
  void PreviewDraftForAgent(std::string draft_id,
                            std::string entry,
                            HomePreviewRequirements requirements,
                            PreviewCallback callback);
  bool IsReadyForExternal() const;
  void PreviewDraftForExternal(std::string draft_id,
                               std::string entry,
                               PreviewCallback callback);
  bool IsExternalPreviewValid(const std::string& draft_id) const;
  void NotifyExternalSessionChanged();
  void NotifyExternalEditorChanged();
  void CancelAgentSession();
  std::string ConsumeEditToken(const std::string& token);
  bool CanResumeEdit(const std::string& token) const;
  scoped_refptr<DaoHomeMutationLease> CreateMutationLease();
  void ArmAgentConnectorForTesting(ConnectorCallback callback);
  bool HasPendingAgentConnectorForTesting() const;
  void StartRuntimeConnectorForTesting(HomeConnector connector,
                                       ConnectorCallback callback);
  bool HasActiveRuntimeConnectorForTesting() const;
  bool IsAwaitingCollectionDeliveryForTesting(const std::string& id);

 private:
  void HandleGetSnapshot(const base::ListValue& args);
  void HandleGetExternalSession(const base::ListValue& args);
  void HandleGetSessionActive(const base::ListValue& args);
  void HandleGetExternalEditor(const base::ListValue& args);
  void HandleStopExternalEditor(const base::ListValue& args);
  void HandleExternalSessionReady(const base::ListValue& args);
  void HandleStopExternalSession(const base::ListValue& args);
  void HandleGetVersions(const base::ListValue& args);
  void HandleGetFiles(const base::ListValue& args);
  void HandleReadFile(const base::ListValue& args);
  void HandleGetPermission(const base::ListValue& args);
  void HandleApprovePermission(const base::ListValue& args);
  void HandleCancelPermission(const base::ListValue& args);
  void HandleResolveBootstrapPermission(const base::ListValue& args);
  void HandleOpenAgent(const base::ListValue& args);
  void HandleRollback(const base::ListValue& args);
  void HandleReset(const base::ListValue& args);
  void HandleExport(const base::ListValue& args);
  void HandleImport(const base::ListValue& args);
  void HandleStartConnector(bool draft, const base::ListValue& args);
  void HandleCallConnectorPage(const base::ListValue& args);
  void HandleCancelConnector(const base::ListValue& args);
  void HandleFinishConnector(const base::ListValue& args);
  void HandleResolveMedia(const base::ListValue& args);
  void HandleCompleteAgentConnector(const base::ListValue& args);
  void HandleNotifyAgentPreviewLoaded(const base::ListValue& args);
  void HandleRecordRuntimeError(const base::ListValue& args);
  void HandleCancelSession(const base::ListValue& args);
  void HandleSetSelection(const base::ListValue& args);
  void HandleOpenNavigation(const base::ListValue& args);
  void QueryHistoryAndOpenAgent();
  void OpenAgentWithPrompt(int prompt_id,
                           std::string history_claim_token = {},
                           std::string edit_revision = {});

  void ReplySnapshot(const std::string& callback_event, HomeSnapshot snapshot);
  void ReplyVersions(const std::string& callback_event,
                     std::vector<HomeVersion> versions);
  void ReplyAfterMutation(const std::string& callback_event);
  void DiscardDraftAndReply(std::string draft_id,
                            std::string callback_event,
                            base::Value result);
  bool HasActiveHomeOwner() const;
  bool CanAccessDraft(const std::string& draft_id) const;
  bool CanUseCollection(const HomeCollectionKey& key) const;
  void PreviewDraft(std::string draft_id,
                    std::string entry,
                    HomePreviewRequirements requirements,
                    bool retained,
                    PreviewCallback callback);
  void ReplyConnector(const std::string& callback_event, base::Value result);
  void ReplyCollection(const std::string& callback_event,
                       const HomeCollectionKey& key,
                       const std::string& scope,
                       const std::string& stage,
                       base::Value result);
  void ReplyConnectorWithDiagnostic(const std::string& callback_event,
                                    const std::string& revision,
                                    const std::string& connector_id,
                                    const std::string& stage,
                                    base::Value result,
                                    const char* reason = nullptr);
  void OnProjectChanged();
  void OnPermissionChanged(
      const std::optional<HomePermissionBatchRequest>& request);
  void OnBootstrapPermissionChanged();
  void CancelAgentConnector(std::string code, std::string message);
  void CancelAgentPreview(std::string code, std::string message);
  void FinishAgentPreviewAfterLoad();
  void FinishAgentPreviewWithSemantics(
      std::string request_id,
      content::FrameTreeNodeId frame_tree_node_id,
      base::Value result);
  bool ValidateAgentPreviewSemantics(const base::Value& result) const;
  void CompleteAgentPreview();
  void InvalidateDocumentMutationLease();
  void CancelAllSessions();
  enum class CollectionMode { kDefault, kCacheOnly, kFresh };
  void StartCollection(const std::string& callback_event,
                       std::string draft_id,
                       HomeConnectorBundle bundle,
                       base::Value input,
                       CollectionMode mode);
  enum class SourceWait {
    kNone,
    kInitialCookies,
    kValidated,
    kAttribution,
    kRevalidated
  };
  void OnCollectionFinished(std::string callback_event,
                            HomeCollectionKey key,
                            std::string execution_id,
                            SourceWait waited,
                            base::Value response);
  struct SourceWrites {
    bool Track(std::shared_ptr<const HomeSourceWrite> write);
    const char* Reason(const HomeStorageReads& reads,
                       base::TimeTicks through = base::TimeTicks::Max()) const;
    std::vector<std::shared_ptr<const HomeSourceWrite>> pending;
  };
  struct DisplayedSource {
    HomeCollectionKey key;
    HomeSourceWatches watches;
    HomeStorageReads storage_reads;
    SourceWrites writes{};
  };
  void RememberDisplayedSource(const HomeCollectionKey& key,
                               HomeSourceWatches watches,
                               HomeStorageReads storage_reads);
  void CheckDisplayedSourceWrites();
  void RevokeDisplayedSource(const HomeCollectionKey& key);
  struct Collection {
    HomeCollectionKey key;
    std::unique_ptr<DaoHomeConnectorExecutor> executor;
    std::optional<base::Value> media_result;
    base::Time collected_at;
    HomeSourceWatches watches;
    const char* session_end_reason = nullptr;
    HomeStorageReads storage_reads = std::nullopt;
    SourceWrites writes{};
    const char* UnattributedWriteReason() const;
    void Invalidate(const char* reason,
                    std::shared_ptr<const HomeSourceWrite> write);
  };
  Collection* FindCollection(const std::string& execution_id);
  DaoHomeConnectorExecutor* FindConnectorExecutor(
      const std::string& execution_id);
  const Collection* FindMediaCollection(const std::string& handle,
                                        const std::string& draft_id);
  void ResolveMedia(std::string callback_event,
                    std::string handle,
                    std::string draft_id,
                    bool check_only,
                    bool allow_wait);
  void CancelDraftCollections(const std::string& draft_id);

  raw_ptr<DaoHomeProjectService> service_;
  std::vector<Collection> active_collections_;
  std::vector<Collection> retained_collections_;
  std::vector<DisplayedSource> displayed_sources_;
  base::OneShotTimer displayed_write_timer_;
  int collection_generation_ = 0;
  int draft_collection_generation_ = 0;
  base::CallbackListSubscription project_changed_subscription_;
  base::CallbackListSubscription collection_invalidation_subscription_;
  base::CallbackListSubscription permission_subscription_;
  base::CallbackListSubscription bootstrap_permission_subscription_;
  base::CancelableTaskTracker history_tracker_;
  std::string agent_connector_request_id_;
  ConnectorCallback agent_connector_callback_;
  base::OneShotTimer agent_connector_timeout_;
  std::string agent_preview_request_id_;
  std::string agent_preview_draft_id_;
  std::string agent_preview_entry_;
  HomePreviewRequirements agent_preview_requirements_;
  content::FrameTreeNodeId agent_preview_frame_tree_node_id_;
  PreviewCallback agent_preview_callback_;
  base::OneShotTimer agent_preview_timeout_;
  base::OneShotTimer agent_preview_settle_timer_;
  bool agent_preview_runtime_failed_ = false;
  bool agent_preview_retained_ = false;
  bool external_preview_valid_ = false;
  bool external_session_ready_ = false;
  int external_session_generation_ = 0;
  std::string agent_connector_draft_id_;
  std::string agent_connector_id_;
  scoped_refptr<DaoHomeMutationLease> document_mutation_lease_;
  std::string pending_edit_token_;
  std::string edit_resume_token_;
  base::WeakPtrFactory<DaoHomeUIHandler> weak_factory_{this};
};

class DaoHomeUI : public content::WebUIController {
 public:
  using ConnectorCallback = base::OnceCallback<void(base::Value)>;
  using PreviewCallback = base::OnceCallback<void(base::Value)>;

  explicit DaoHomeUI(content::WebUI* web_ui);
  ~DaoHomeUI() override;

  WEB_UI_CONTROLLER_TYPE_DECL();

  void CollectConnectorForAgent(std::string draft_id,
                                std::string connector_id,
                                base::Value input,
                                ConnectorCallback callback);
  void PreviewDraftForAgent(std::string draft_id,
                            std::string entry,
                            HomePreviewRequirements requirements,
                            PreviewCallback callback);
  bool IsReadyForExternal() const;
  void PreviewDraftForExternal(std::string draft_id,
                               std::string entry,
                               PreviewCallback callback);
  bool IsExternalPreviewValid(const std::string& draft_id) const;
  void NotifyExternalSessionChanged();
  void NotifyExternalEditorChanged();
  void CancelAgentSession();
  std::string ConsumeEditToken(const std::string& token);
  bool CanResumeEdit(const std::string& token) const;
  scoped_refptr<DaoHomeMutationLease> CreateMutationLease();
  void ArmAgentConnectorForTesting(ConnectorCallback callback);
  bool HasPendingAgentConnectorForTesting() const;
  void StartRuntimeConnectorForTesting(HomeConnector connector,
                                       ConnectorCallback callback);
  bool HasActiveRuntimeConnectorForTesting() const;
  bool IsAwaitingCollectionDeliveryForTesting(const std::string& id);

 private:
  raw_ptr<DaoHomeUIHandler> handler_ = nullptr;
};

class DaoHomeAppUI : public ui::UntrustedWebUIController {
 public:
  explicit DaoHomeAppUI(content::WebUI* web_ui);
  ~DaoHomeAppUI() override;

 private:
  static bool ShouldHandleProjectRequest(const std::string& path);
  static void HandleProjectRequest(
      DaoHomeProjectService* service,
      const std::string& path,
      content::WebUIDataSource::GotDataCallback callback);
  static void ReplyProjectResource(
      bool inject_runtime,
      content::WebUIDataSource::GotDataCallback callback,
      base::expected<std::string, HomeError> contents);
};

class DaoHomeConnectorUI : public ui::UntrustedWebUIController {
 public:
  explicit DaoHomeConnectorUI(content::WebUI* web_ui);
  ~DaoHomeConnectorUI() override;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_HOME_UI_H_
