// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_bootstrap_transaction.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <utility>

#include "base/json/json_writer.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/home/dao_home_manifest.h"

namespace dao {
namespace {

constexpr size_t kMaxPermissionBatchItems = 3;
constexpr size_t kRequiredRankedActions = 4;
constexpr size_t kMaxPrimaryActions = 12;
constexpr size_t kMaxSourceSlots = 3;

bool SameLimits(const HomeLimits& left, const HomeLimits& right) {
  return left.max_result_bytes == right.max_result_bytes &&
         left.max_items_per_connector == right.max_items_per_connector;
}

bool SamePermission(const HomeConnectorPermission& left,
                    const HomeConnectorPermission& right) {
  return HomeConnectorPermissionFingerprint(left) ==
         HomeConnectorPermissionFingerprint(right);
}

bool SameConnector(const HomeConnector& left, const HomeConnector& right) {
  return left.id == right.id && left.module == right.module &&
         left.schema == right.schema &&
         SamePermission(left.permissions, right.permissions);
}

bool IsTerminalOutcome(HomeConnectorTestStatus status) {
  return status == HomeConnectorTestStatus::kSucceeded ||
         status == HomeConnectorTestStatus::kAuthenticationRequired ||
         status == HomeConnectorTestStatus::kRuntimeFailed ||
         status == HomeConnectorTestStatus::kSchemaFailed ||
         status == HomeConnectorTestStatus::kDeselected;
}

bool IsRecordableOutcome(HomeConnectorTestStatus status) {
  return status == HomeConnectorTestStatus::kSucceeded ||
         status == HomeConnectorTestStatus::kAuthenticationRequired ||
         status == HomeConnectorTestStatus::kRuntimeFailed ||
         status == HomeConnectorTestStatus::kSchemaFailed;
}

}  // namespace

DaoHomeBootstrapTransaction::DaoHomeBootstrapTransaction(
    std::string id,
    std::string agent_turn_id,
    base::WeakPtr<content::WebContents> owner,
    std::string base_revision,
    HomeBootstrapBrief brief,
    scoped_refptr<DaoHomeMutationLease> turn_authorization,
    base::RepeatingCallback<bool(const std::string&, const std::string&)>
        context_validator)
    : id_(std::move(id)),
      agent_turn_id_(std::move(agent_turn_id)),
      owner_(std::move(owner)),
      base_revision_(std::move(base_revision)),
      brief_(std::move(brief)),
      turn_authorization_(std::move(turn_authorization)),
      context_validator_(std::move(context_validator)) {}

DaoHomeBootstrapTransaction::~DaoHomeBootstrapTransaction() = default;

base::expected<void, HomeError> DaoHomeBootstrapTransaction::CheckActive()
    const {
  if (id_.empty() || agent_turn_id_.empty() || !owner_ ||
      !turn_authorization_ || !turn_authorization_->IsValid() ||
      !context_validator_ ||
      !context_validator_.Run(agent_turn_id_, base_revision_)) {
    return base::unexpected(HomeError::kCancelled);
  }
  return base::ok();
}

base::expected<void, HomeError> DaoHomeBootstrapTransaction::RegisterDraft(
    const HomeDraft& draft) {
  auto active = CheckActive();
  if (!active.has_value()) {
    return active;
  }
  if (draft.id.empty()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  if (draft.base_revision != base_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }

  base::flat_set<std::string>* registered_draft_ids = nullptr;
  if (state_ == HomeBootstrapState::kPlanning) {
    registered_draft_ids = &registered_provisional_draft_ids_;
  } else if (state_ == HomeBootstrapState::kBuildingFinalHome) {
    registered_draft_ids = &registered_final_draft_ids_;
  } else {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  registered_draft_ids->insert(draft.id);
  if (!std::ranges::contains(owned_draft_ids_, draft.id)) {
    owned_draft_ids_.push_back(draft.id);
  }
  return base::ok();
}

base::expected<HomePermissionBatchRequest, HomeError>
DaoHomeBootstrapTransaction::PreparePermissionBatch(
    const HomeDraft& provisional_draft,
    std::vector<HomeConnectorAuthorization> authorizations) {
  auto active = CheckActive();
  if (!active.has_value()) {
    return base::unexpected(active.error());
  }
  if (state_ != HomeBootstrapState::kPlanning) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  if (provisional_draft.id.empty()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  if (provisional_draft.base_revision != base_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  if (!registered_provisional_draft_ids_.contains(provisional_draft.id)) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  if (provisional_draft.manifest.connectors.size() > kMaxPermissionBatchItems ||
      authorizations.size() != provisional_draft.manifest.connectors.size()) {
    return base::unexpected(HomeError::kInvalidArgument);
  }

  std::map<std::string, const HomeLaunchTarget*> launch_targets;
  for (const HomeLaunchTarget& target : brief_.launch_targets) {
    if (target.id.empty() ||
        !launch_targets.emplace(target.id, &target).second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
  }
  std::map<std::string, const HomeSourceCandidate*> candidates;
  for (const HomeSourceCandidate& candidate : brief_.source_candidates) {
    if (candidate.launch_target_id.empty() ||
        !launch_targets.contains(candidate.launch_target_id) ||
        candidate.schema_source.empty() ||
        !candidates.emplace(candidate.launch_target_id, &candidate).second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
  }

  std::map<std::string, HomeConnectorAuthorization> authorization_by_id;
  for (HomeConnectorAuthorization& authorization : authorizations) {
    if (authorization.connector_id.empty() ||
        authorization.fingerprint.empty() ||
        authorization.connector_id != authorization.bundle.connector.id ||
        !authorization_by_id
             .emplace(authorization.connector_id, std::move(authorization))
             .second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
  }

  HomePermissionBatchRequest request;
  request.id = id_ + "-permission-batch";
  request.transaction_id = id_;
  request.draft_id = provisional_draft.id;
  request.base_revision = base_revision_;
  base::flat_set<std::string> draft_connector_ids;
  for (const HomeConnector& connector : provisional_draft.manifest.connectors) {
    const auto candidate = candidates.find(connector.id);
    if (candidate == candidates.end() ||
        !draft_connector_ids.insert(connector.id).second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    const auto authorization = authorization_by_id.find(connector.id);
    const auto launch_target = launch_targets.find(connector.id);
    if (authorization == authorization_by_id.end() ||
        launch_target == launch_targets.end() ||
        !SameConnector(connector, authorization->second.bundle.connector) ||
        authorization->second.bundle.schema_source !=
            candidate->second->schema_source ||
        !SameLimits(provisional_draft.manifest.limits,
                    authorization->second.bundle.limits)) {
      return base::unexpected(HomeError::kInvalidArgument);
    }

    HomePermissionBatchItem item;
    item.connector_id = connector.id;
    item.label = launch_target->second->label_hint;
    item.connector = connector;
    item.previous_limits = provisional_draft.previous_limits;
    item.requested_limits = authorization->second.bundle.limits;
    item.fingerprint = authorization->second.fingerprint;
    item.authentication_may_be_required = true;
    request.items.push_back(std::move(item));
  }

  provisional_draft_id_ = provisional_draft.id;
  permission_request_ = request;
  state_ = HomeBootstrapState::kAwaitingBatchApproval;
  return request;
}

base::expected<void, HomeError>
DaoHomeBootstrapTransaction::ResolvePermissionBatch(
    const std::string& request_id,
    const base::flat_set<std::string>& selected_connector_ids) {
  auto active = CheckActive();
  if (!active.has_value()) {
    return active;
  }
  if (state_ != HomeBootstrapState::kAwaitingBatchApproval ||
      !permission_request_ || permission_request_->id != request_id ||
      permission_request_->transaction_id != id_ ||
      permission_request_->base_revision != base_revision_ ||
      permission_request_->draft_id != provisional_draft_id_) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  for (const std::string& selected_id : selected_connector_ids) {
    if (!FindBatchItem(selected_id)) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
  }

  connector_outcomes_.clear();
  for (const HomePermissionBatchItem& item : permission_request_->items) {
    HomeConnectorTestOutcome outcome;
    outcome.connector_id = item.connector_id;
    outcome.fingerprint = item.fingerprint;
    outcome.status = selected_connector_ids.contains(item.connector_id)
                         ? HomeConnectorTestStatus::kApproved
                         : HomeConnectorTestStatus::kDeselected;
    connector_outcomes_.emplace(item.connector_id, std::move(outcome));
  }
  state_ = selected_connector_ids.empty()
               ? HomeBootstrapState::kBuildingFinalHome
               : HomeBootstrapState::kTestingSources;
  return base::ok();
}

base::expected<void, HomeError>
DaoHomeBootstrapTransaction::RecordConnectorOutcome(
    HomeConnectorTestOutcome outcome) {
  auto active = CheckActive();
  if (!active.has_value()) {
    return active;
  }
  if (state_ != HomeBootstrapState::kTestingSources || !permission_request_ ||
      permission_request_->transaction_id != id_ ||
      permission_request_->base_revision != base_revision_ ||
      !IsRecordableOutcome(outcome.status)) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  const HomePermissionBatchItem* item = FindBatchItem(outcome.connector_id);
  auto existing = connector_outcomes_.find(outcome.connector_id);
  if (!item || existing == connector_outcomes_.end() ||
      existing->second.status != HomeConnectorTestStatus::kApproved ||
      item->fingerprint != outcome.fingerprint) {
    return base::unexpected(HomeError::kPermissionRequired);
  }
  if (outcome.status == HomeConnectorTestStatus::kSucceeded) {
    if (!outcome.error_code.empty()) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    if (outcome.sample) {
      std::string serialized;
      if (!base::JSONWriter::Write(*outcome.sample, &serialized) ||
          serialized.size() >
              static_cast<size_t>(item->requested_limits.max_result_bytes)) {
        return base::unexpected(HomeError::kQuotaExceeded);
      }
    }
  } else if (outcome.error_code.empty() || outcome.sample) {
    return base::unexpected(HomeError::kInvalidArgument);
  }

  // The raw result is used only for this native budget check. It must not
  // survive the connector callback or become available to final generation.
  outcome.sample.reset();
  existing->second = std::move(outcome);
  if (AllSelectedConnectorsTerminal()) {
    state_ = HomeBootstrapState::kBuildingFinalHome;
  }
  return base::ok();
}

base::expected<HomePreviewRequirements, HomeError>
DaoHomeBootstrapTransaction::BindFinalDraft(
    const HomeDraft& final_draft,
    std::vector<HomeConnectorAuthorization> authorizations,
    HomeExperience experience) {
  auto active = CheckActive();
  if (!active.has_value()) {
    return base::unexpected(active.error());
  }
  if (state_ != HomeBootstrapState::kBuildingFinalHome ||
      !permission_request_ || permission_request_->transaction_id != id_ ||
      permission_request_->base_revision != base_revision_ ||
      !AllSelectedConnectorsTerminal()) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  if (final_draft.id.empty()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  if (final_draft.base_revision != base_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  if (!registered_final_draft_ids_.contains(final_draft.id)) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  if (authorizations.size() != final_draft.manifest.connectors.size()) {
    return base::unexpected(HomeError::kInvalidArgument);
  }

  base::flat_set<std::string> launch_target_ids;
  for (const HomeLaunchTarget& target : brief_.launch_targets) {
    if (target.id.empty() || !target.url.is_valid() ||
        !launch_target_ids.insert(target.id).second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
  }
  if (experience.primary_actions.size() > kMaxPrimaryActions ||
      experience.source_slots.size() > kMaxSourceSlots) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  base::flat_set<std::string> action_ids;
  for (const std::string& action_id : experience.primary_actions) {
    if (!launch_target_ids.contains(action_id) ||
        !action_ids.insert(action_id).second) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
  }
  const size_t required_action_count =
      std::min(kRequiredRankedActions, brief_.launch_targets.size());
  for (size_t index = 0; index < required_action_count; ++index) {
    if (!action_ids.contains(brief_.launch_targets[index].id)) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
  }

  std::map<std::string, HomeConnectorAuthorization> authorization_by_id;
  for (HomeConnectorAuthorization& authorization : authorizations) {
    if (authorization.connector_id.empty() ||
        authorization.connector_id != authorization.bundle.connector.id ||
        !authorization_by_id
             .emplace(authorization.connector_id, std::move(authorization))
             .second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
  }

  base::flat_set<std::string> final_connector_ids;
  for (const HomeConnector& connector : final_draft.manifest.connectors) {
    if (!final_connector_ids.insert(connector.id).second) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
    const auto outcome = connector_outcomes_.find(connector.id);
    const auto authorization = authorization_by_id.find(connector.id);
    const HomePermissionBatchItem* item = FindBatchItem(connector.id);
    if (!item || outcome == connector_outcomes_.end() ||
        outcome->second.status != HomeConnectorTestStatus::kSucceeded ||
        authorization == authorization_by_id.end() ||
        authorization->second.fingerprint != item->fingerprint ||
        authorization->second.fingerprint != outcome->second.fingerprint ||
        !SameConnector(connector, authorization->second.bundle.connector) ||
        !SameLimits(final_draft.manifest.limits,
                    authorization->second.bundle.limits)) {
      return base::unexpected(HomeError::kPermissionRequired);
    }
  }

  base::flat_set<std::string> source_slot_ids;
  for (const std::string& connector_id : experience.source_slots) {
    const auto outcome = connector_outcomes_.find(connector_id);
    if (!source_slot_ids.insert(connector_id).second ||
        !final_connector_ids.contains(connector_id) ||
        outcome == connector_outcomes_.end() ||
        outcome->second.status != HomeConnectorTestStatus::kSucceeded) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
  }

  HomePreviewRequirements requirements;
  requirements.experience = std::move(experience);
  for (const HomeLaunchTarget& target : brief_.launch_targets) {
    requirements.launch_urls.emplace(target.id, target.url);
  }
  requirements.tested_connector_ids = std::move(final_connector_ids);

  final_draft_id_ = final_draft.id;
  previewed_ = false;
  state_ = HomeBootstrapState::kPreviewing;
  return requirements;
}

base::expected<void, HomeError> DaoHomeBootstrapTransaction::RejectPreview(
    const std::string& draft_id) {
  auto active = CheckActive();
  if (!active.has_value()) {
    return active;
  }
  if (state_ != HomeBootstrapState::kPreviewing || previewed_ ||
      draft_id != final_draft_id_ || final_draft_id_.empty()) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  final_draft_id_.clear();
  state_ = HomeBootstrapState::kBuildingFinalHome;
  return base::ok();
}

base::expected<void, HomeError> DaoHomeBootstrapTransaction::MarkPreviewed(
    const std::string& draft_id) {
  auto active = CheckActive();
  if (!active.has_value()) {
    return active;
  }
  if (state_ != HomeBootstrapState::kPreviewing || previewed_ ||
      draft_id != final_draft_id_ || final_draft_id_.empty()) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  previewed_ = true;
  return base::ok();
}

base::expected<void, HomeError> DaoHomeBootstrapTransaction::BeginPublish(
    const std::string& draft_id) {
  auto active = CheckActive();
  if (!active.has_value()) {
    return active;
  }
  if (state_ != HomeBootstrapState::kPreviewing || !previewed_ ||
      draft_id != final_draft_id_ || final_draft_id_.empty()) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  state_ = HomeBootstrapState::kPublishing;
  return base::ok();
}

void DaoHomeBootstrapTransaction::MarkPublished() {
  if (!CheckActive().has_value() || state_ != HomeBootstrapState::kPublishing ||
      final_draft_id_.empty()) {
    return;
  }
  final_draft_published_ = true;
  state_ = HomeBootstrapState::kComplete;
}

std::vector<std::string> DaoHomeBootstrapTransaction::Cancel() {
  if (cleanup_inventory_) {
    return *cleanup_inventory_;
  }

  std::vector<std::string> inventory;
  for (const std::string& draft_id : owned_draft_ids_) {
    if (!final_draft_published_ || draft_id != final_draft_id_) {
      inventory.push_back(draft_id);
    }
  }
  for (auto& [connector_id, outcome] : connector_outcomes_) {
    outcome.sample.reset();
  }
  if (state_ != HomeBootstrapState::kComplete &&
      state_ != HomeBootstrapState::kFailed) {
    state_ = HomeBootstrapState::kCancelled;
  }
  cleanup_inventory_ = inventory;
  return inventory;
}

const HomePermissionBatchItem* DaoHomeBootstrapTransaction::FindBatchItem(
    const std::string& connector_id) const {
  if (!permission_request_) {
    return nullptr;
  }
  const auto item =
      std::ranges::find_if(permission_request_->items,
                           [&](const HomePermissionBatchItem& candidate) {
                             return candidate.connector_id == connector_id;
                           });
  return item == permission_request_->items.end() ? nullptr : &*item;
}

bool DaoHomeBootstrapTransaction::AllSelectedConnectorsTerminal() const {
  if (!permission_request_ ||
      connector_outcomes_.size() != permission_request_->items.size()) {
    return false;
  }
  return std::ranges::all_of(connector_outcomes_, [](const auto& entry) {
    return IsTerminalOutcome(entry.second.status);
  });
}

}  // namespace dao
