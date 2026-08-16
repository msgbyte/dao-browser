// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_BOOTSTRAP_TRANSACTION_H_
#define DAO_BROWSER_HOME_DAO_HOME_BOOTSTRAP_TRANSACTION_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/types/expected.h"
#include "dao/browser/home/dao_home_types.h"

namespace content {
class WebContents;
}

namespace dao {

// Owns the bounded, session-only state for one history bootstrap turn. Store,
// connector execution, preview, and publication remain asynchronous concerns
// of the service that drives this transaction.
class DaoHomeBootstrapTransaction {
 public:
  DaoHomeBootstrapTransaction(
      std::string id,
      std::string agent_turn_id,
      base::WeakPtr<content::WebContents> owner,
      std::string base_revision,
      HomeBootstrapBrief brief,
      scoped_refptr<DaoHomeMutationLease> turn_authorization,
      base::RepeatingCallback<bool(const std::string&, const std::string&)>
          context_validator);
  ~DaoHomeBootstrapTransaction();

  DaoHomeBootstrapTransaction(const DaoHomeBootstrapTransaction&) = delete;
  DaoHomeBootstrapTransaction& operator=(const DaoHomeBootstrapTransaction&) =
      delete;

  base::expected<void, HomeError> RegisterDraft(const HomeDraft& draft);
  base::expected<HomePermissionBatchRequest, HomeError> PreparePermissionBatch(
      const HomeDraft& provisional_draft,
      std::vector<HomeConnectorAuthorization> authorizations);
  base::expected<void, HomeError> ResolvePermissionBatch(
      const std::string& request_id,
      const base::flat_set<std::string>& selected_connector_ids);
  base::expected<void, HomeError> RecordConnectorOutcome(
      HomeConnectorTestOutcome outcome);
  base::expected<HomePreviewRequirements, HomeError> BindFinalDraft(
      const HomeDraft& final_draft,
      std::vector<HomeConnectorAuthorization> authorizations,
      HomeExperience experience);
  base::expected<void, HomeError> RejectPreview(const std::string& draft_id);
  base::expected<void, HomeError> MarkPreviewed(const std::string& draft_id);
  base::expected<void, HomeError> BeginPublish(const std::string& draft_id);
  void MarkPublished();
  std::vector<std::string> Cancel();

  HomeBootstrapState state() const { return state_; }

 private:
  base::expected<void, HomeError> CheckActive() const;
  const HomePermissionBatchItem* FindBatchItem(
      const std::string& connector_id) const;
  bool AllSelectedConnectorsTerminal() const;

  const std::string id_;
  const std::string agent_turn_id_;
  base::WeakPtr<content::WebContents> owner_;
  const std::string base_revision_;
  const HomeBootstrapBrief brief_;
  const scoped_refptr<DaoHomeMutationLease> turn_authorization_;
  const base::RepeatingCallback<bool(const std::string&, const std::string&)>
      context_validator_;

  HomeBootstrapState state_ = HomeBootstrapState::kPlanning;
  std::optional<HomePermissionBatchRequest> permission_request_;
  std::map<std::string, HomeConnectorTestOutcome> connector_outcomes_;
  std::vector<std::string> owned_draft_ids_;
  base::flat_set<std::string> registered_provisional_draft_ids_;
  base::flat_set<std::string> registered_final_draft_ids_;
  std::string provisional_draft_id_;
  std::string final_draft_id_;
  bool previewed_ = false;
  bool final_draft_published_ = false;
  std::optional<std::vector<std::string>> cleanup_inventory_;
};

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_BOOTSTRAP_TRANSACTION_H_
