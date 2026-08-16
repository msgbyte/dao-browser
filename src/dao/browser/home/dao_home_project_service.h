// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_PROJECT_SERVICE_H_
#define DAO_BROWSER_HOME_DAO_HOME_PROJECT_SERVICE_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"
#include "dao/browser/home/dao_home_project_store.h"

namespace content {
class WebContents;
}

namespace dao {

class DaoHomeBootstrapTransaction;

// Profile-keyed asynchronous facade for the on-disk Home project store.
class DaoHomeProjectService : public KeyedService {
 public:
  template <typename T>
  using ResultCallback = base::OnceCallback<void(base::expected<T, HomeError>)>;

  explicit DaoHomeProjectService(const base::FilePath& profile_path);
  ~DaoHomeProjectService() override;

  DaoHomeProjectService(const DaoHomeProjectService&) = delete;
  DaoHomeProjectService& operator=(const DaoHomeProjectService&) = delete;

  void GetSnapshot(base::OnceCallback<void(HomeSnapshot)> callback);
  void SetBeforeSnapshotReplyCallbackForTesting(base::OnceClosure callback) {
    before_snapshot_reply_callback_for_testing_ = std::move(callback);
  }
  void SetBeforeBootstrapPublishReplyCallbackForTesting(
      base::OnceClosure callback) {
    before_bootstrap_publish_reply_callback_for_testing_ = std::move(callback);
  }
  void ListVersions(
      base::OnceCallback<void(std::vector<HomeVersion>)> callback);
  void ListFiles(const std::string& revision,
                 ResultCallback<std::vector<std::string>> callback);
  void ReadFile(const std::string& revision,
                const std::string& relative_path,
                ResultCallback<std::string> callback);
  void GetDraft(const std::string& draft_id,
                ResultCallback<HomeDraft> callback);
  void ReadActivePreviewFile(const std::string& draft_id,
                             const std::string& relative_path,
                             ResultCallback<std::string> callback);
  void GetConnectorBundle(const std::string& revision,
                          const std::string& connector_id,
                          ResultCallback<HomeConnectorBundle> callback);
  void GetApprovedDraftConnectorBundle(
      const std::string& draft_id,
      const std::string& connector_id,
      ResultCallback<HomeConnectorBundle> callback);
  void ApplyPatch(const std::string& base_revision,
                  const std::string& patch,
                  const std::string& summary,
                  ResultCallback<HomeDraft> callback,
                  std::string agent_turn_id = {});
  void ReplaceFiles(
      const std::string& base_revision,
      const std::vector<std::pair<std::string, std::string>>& files,
      const std::string& summary,
      ResultCallback<HomeDraft> callback,
      std::string agent_turn_id = {});
  void AddAsset(const std::string& base_revision,
                const std::string& relative_path,
                const std::string& base64_contents,
                const std::string& summary,
                ResultCallback<HomeDraft> callback,
                std::string agent_turn_id = {});
  void Publish(const std::string& draft_id,
               HomeRevisionKind kind,
               ResultCallback<HomeVersion> callback,
               scoped_refptr<DaoHomeMutationLease> authorization);
  void PublishPreviewedDraft(const HomeDraft& draft,
                             HomeRevisionKind kind,
                             ResultCallback<HomeVersion> callback,
                             scoped_refptr<DaoHomeMutationLease> authorization);
  void ApprovePermission(const std::string& request_id,
                         ResultCallback<void> callback);
  void PublishApprovedDraft(const HomeDraft& draft,
                            HomeRevisionKind kind,
                            ResultCallback<HomeVersion> callback,
                            scoped_refptr<DaoHomeMutationLease> authorization);
  void Rollback(const std::string& base_revision,
                const std::string& target_revision,
                const std::string& summary,
                ResultCallback<HomeVersion> callback,
                scoped_refptr<DaoHomeMutationLease> authorization);
  void Reset(const std::string& base_revision,
             ResultCallback<void> callback,
             scoped_refptr<DaoHomeMutationLease> authorization);
  void ExportProject(ResultCallback<std::string> callback);
  void ImportProject(const std::string& base_revision,
                     const std::string& package_json,
                     const std::string& summary,
                     ResultCallback<HomeDraft> callback);
  void DiscardDraft(const std::string& draft_id, ResultCallback<void> callback);
  void GrantConnector(const std::string& connector_id,
                      ResultCallback<void> callback);
  void RevokeConnector(const std::string& connector_id,
                       ResultCallback<void> callback);

  base::expected<HomePermissionRequest, HomeError> RequestPermission(
      const HomeDraft& draft,
      const std::string& connector_id);
  std::optional<HomePermissionRequest> GetPendingPermission() const;
  bool CancelPermission(const std::string& request_id);
  bool IsDraftConnectorApproved(const std::string& draft_id,
                                const std::string& connector_id) const;
  base::expected<void, HomeError> MarkDraftConnectorTested(
      const std::string& draft_id,
      const std::string& connector_id);
  void BeginDraftPreview(const std::string& draft_id);
  void EndDraftPreview(const std::string& draft_id);
  void MarkDraftPreviewed(const std::string& draft_id);
  bool IsDraftPreviewed(const std::string& draft_id) const;

  void RecordDiagnostic(HomeDiagnostic diagnostic);
  std::vector<HomeDiagnostic> GetDiagnostics(const std::string& revision) const;

  base::CallbackListSubscription AddProjectChangedObserver(
      base::RepeatingClosure callback);
  base::CallbackListSubscription AddPermissionObserver(
      base::RepeatingCallback<void(const std::optional<HomePermissionRequest>&)>
          callback);

  // The one-shot bootstrap brief is intentionally kept only in this
  // UI-sequence service object. It is bound to the requesting Home document,
  // the exact external prompt that carries `claim_token`, and the Agent turn
  // started by that prompt. It is never written to disk.
  void SetHistoryBootstrapBrief(base::WeakPtr<content::WebContents> owner,
                                std::string claim_token,
                                HomeBootstrapBrief brief);
  bool ClaimHistoryBootstrap(content::WebContents* owner,
                             const std::string& claim_token,
                             const std::string& agent_turn_id);
  base::expected<HomeBootstrapBrief, HomeError> ConsumeHistoryBootstrapBrief(
      const std::string& agent_turn_id);
  base::expected<HomeBootstrapBrief, HomeError> BeginHistoryBootstrap(
      const std::string& agent_turn_id,
      const std::string& base_revision,
      scoped_refptr<DaoHomeMutationLease> turn_authorization,
      base::RepeatingCallback<bool()> owner_validator);
  using BootstrapDecisionCallback = base::OnceCallback<void(
      base::expected<base::flat_set<std::string>, HomeError>)>;
  void RequestBootstrapPermissions(const std::string& agent_turn_id,
                                   const HomeDraft& draft,
                                   std::vector<std::string> connector_ids,
                                   BootstrapDecisionCallback callback);
  void RequestBootstrapPermissionsWithProposals(
      const std::string& agent_turn_id,
      const HomeDraft& draft,
      std::vector<HomeSourceProposal> proposals,
      BootstrapDecisionCallback callback);
  void ResolveBootstrapPermissions(
      content::WebContents* owner,
      const std::string& request_id,
      base::flat_set<std::string> selected_connector_ids,
      ResultCallback<void> callback);
  std::optional<HomePermissionBatchRequest> GetPendingBootstrapPermission(
      content::WebContents* owner) const;
  base::CallbackListSubscription AddBootstrapPermissionObserver(
      base::RepeatingClosure callback);
  base::expected<void, HomeError> BeginBootstrapConnectorTest(
      const std::string& agent_turn_id,
      const std::string& draft_id,
      const std::string& connector_id);
  base::expected<void, HomeError> RecordBootstrapConnectorOutcome(
      const std::string& agent_turn_id,
      const std::string& connector_id,
      HomeConnectorTestStatus status,
      std::optional<base::Value> sample,
      std::string error_code);
  void PrepareBootstrapPreview(
      const std::string& agent_turn_id,
      const HomeDraft& draft,
      ResultCallback<HomePreviewRequirements> callback);
  base::expected<void, HomeError> CompleteBootstrapPreview(
      const std::string& agent_turn_id,
      const std::string& draft_id);
  base::expected<void, HomeError> RejectBootstrapPreview(
      const std::string& agent_turn_id,
      const std::string& draft_id);
  void PublishBootstrapDraft(const std::string& agent_turn_id,
                             const HomeDraft& draft,
                             ResultCallback<HomeVersion> callback,
                             scoped_refptr<DaoHomeMutationLease> authorization);
  bool HasActiveHistoryBootstrapForTurn(const std::string& agent_turn_id) const;
  void CancelHistoryBootstrapForOwner(content::WebContents* owner);
  void CancelHistoryBootstrapForTurn(const std::string& agent_turn_id);
  void ClearHistoryBootstrapForOwner(content::WebContents* owner);
  void ClearHistoryBootstrapForClaim(const std::string& claim_token);
  void ClearHistoryBootstrapForTurn(const std::string& agent_turn_id);
  void SetSelectedNode(std::string node_id);
  const std::string& selected_node() const { return selected_node_; }
  void ClearSelectedNode();

  base::WeakPtr<DaoHomeProjectService> GetWeakPtr();

 private:
  void NotifyProjectChanged();
  void NotifyPermissionChanged();
  void NotifyBootstrapPermissionChanged();
  bool ValidateBootstrapContext(
      const std::string& expected_turn_id,
      const std::string& expected_base_revision) const;
  base::expected<void, HomeError> RegisterBootstrapDraftForTurn(
      const std::string& agent_turn_id,
      const HomeDraft& draft);
  void NormalizeAndBindBootstrapPreview(
      const std::string& agent_turn_id,
      const HomeDraft& draft,
      ResultCallback<HomePreviewRequirements> callback);
  void FinishOrCancelBootstrap();
  static std::string DraftConnectorKey(const std::string& draft_id,
                                       const std::string& connector_id);

  base::SequenceBound<DaoHomeProjectStore> store_;
  struct HistoryBootstrapState {
    HistoryBootstrapState(base::WeakPtr<content::WebContents> owner,
                          std::string claim_token,
                          HomeBootstrapBrief brief);
    ~HistoryBootstrapState();
    HistoryBootstrapState(HistoryBootstrapState&&);
    HistoryBootstrapState& operator=(HistoryBootstrapState&&);

    base::WeakPtr<content::WebContents> owner;
    std::string claim_token;
    HomeBootstrapBrief brief;
    std::string agent_turn_id;
  };

  std::optional<HistoryBootstrapState> history_bootstrap_;
  std::unique_ptr<DaoHomeBootstrapTransaction> bootstrap_transaction_;
  std::string active_bootstrap_transaction_id_;
  HomeBootstrapBrief active_bootstrap_brief_;
  base::WeakPtr<content::WebContents> active_bootstrap_owner_;
  std::string active_bootstrap_turn_id_;
  std::string active_bootstrap_base_revision_;
  std::string current_revision_;
  base::RepeatingCallback<bool()> active_bootstrap_owner_validator_;
  bool bootstrap_permission_preparing_ = false;
  std::optional<HomePermissionBatchRequest> bootstrap_permission_request_;
  BootstrapDecisionCallback bootstrap_decision_callback_;
  base::flat_set<std::string> bootstrap_selected_connector_ids_;
  base::flat_set<std::string> bootstrap_completed_connector_ids_;
  base::flat_set<std::string> bootstrap_successful_connector_ids_;
  std::string bootstrap_connector_test_in_flight_;
  std::string selected_node_;
  std::optional<HomePermissionRequest> pending_permission_;
  std::set<std::string> approved_draft_connectors_;
  std::set<std::string> tested_draft_connectors_;
  std::set<std::string> active_preview_drafts_;
  std::set<std::string> previewed_drafts_;
  std::vector<HomeDiagnostic> diagnostics_;
  base::RepeatingClosureList project_changed_observers_;
  base::RepeatingCallbackList<void(const std::optional<HomePermissionRequest>&)>
      permission_observers_;
  base::RepeatingClosureList bootstrap_permission_observers_;
  base::OnceClosure before_snapshot_reply_callback_for_testing_;
  base::OnceClosure before_bootstrap_publish_reply_callback_for_testing_;
  base::WeakPtrFactory<DaoHomeProjectService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_PROJECT_SERVICE_H_
