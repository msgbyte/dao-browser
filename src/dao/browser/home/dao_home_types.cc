// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_types.h"

#include <utility>

namespace dao {

DaoHomeMutationLease::DaoHomeMutationLease(
    scoped_refptr<DaoHomeMutationLease> parent)
    : parent_(std::move(parent)) {}

DaoHomeMutationLease::~DaoHomeMutationLease() = default;

DaoHomeMutationLease::CommitGuard::CommitGuard(
    const scoped_refptr<DaoHomeMutationLease>& authorization) {
  if (!authorization) {
    valid_ = false;
    return;
  }
  LockLease(authorization.get());
}

DaoHomeMutationLease::CommitGuard::~CommitGuard() = default;

HomeLaunchTarget::HomeLaunchTarget() = default;
HomeLaunchTarget::~HomeLaunchTarget() = default;
HomeLaunchTarget::HomeLaunchTarget(const HomeLaunchTarget&) = default;
HomeLaunchTarget& HomeLaunchTarget::operator=(const HomeLaunchTarget&) =
    default;
HomeLaunchTarget::HomeLaunchTarget(HomeLaunchTarget&&) noexcept = default;
HomeLaunchTarget& HomeLaunchTarget::operator=(HomeLaunchTarget&&) noexcept =
    default;

HomeSourceCandidate::HomeSourceCandidate() = default;
HomeSourceCandidate::~HomeSourceCandidate() = default;
HomeSourceCandidate::HomeSourceCandidate(const HomeSourceCandidate&) = default;
HomeSourceCandidate& HomeSourceCandidate::operator=(
    const HomeSourceCandidate&) = default;
HomeSourceCandidate::HomeSourceCandidate(HomeSourceCandidate&&) noexcept =
    default;
HomeSourceCandidate& HomeSourceCandidate::operator=(
    HomeSourceCandidate&&) noexcept = default;

HomeSourceProposal::HomeSourceProposal() = default;
HomeSourceProposal::~HomeSourceProposal() = default;
HomeSourceProposal::HomeSourceProposal(const HomeSourceProposal&) = default;
HomeSourceProposal& HomeSourceProposal::operator=(const HomeSourceProposal&) =
    default;
HomeSourceProposal::HomeSourceProposal(HomeSourceProposal&&) noexcept = default;
HomeSourceProposal& HomeSourceProposal::operator=(
    HomeSourceProposal&&) noexcept = default;

HomeBootstrapBrief::HomeBootstrapBrief() = default;
HomeBootstrapBrief::~HomeBootstrapBrief() = default;
HomeBootstrapBrief::HomeBootstrapBrief(const HomeBootstrapBrief&) = default;
HomeBootstrapBrief& HomeBootstrapBrief::operator=(const HomeBootstrapBrief&) =
    default;
HomeBootstrapBrief::HomeBootstrapBrief(HomeBootstrapBrief&&) noexcept = default;
HomeBootstrapBrief& HomeBootstrapBrief::operator=(
    HomeBootstrapBrief&&) noexcept = default;

HomeExperience::HomeExperience() = default;
HomeExperience::~HomeExperience() = default;
HomeExperience::HomeExperience(const HomeExperience&) = default;
HomeExperience& HomeExperience::operator=(const HomeExperience&) = default;
HomeExperience::HomeExperience(HomeExperience&&) noexcept = default;
HomeExperience& HomeExperience::operator=(HomeExperience&&) noexcept = default;

HomePreviewRequirements::HomePreviewRequirements() = default;
HomePreviewRequirements::~HomePreviewRequirements() = default;
HomePreviewRequirements::HomePreviewRequirements(
    const HomePreviewRequirements&) = default;
HomePreviewRequirements& HomePreviewRequirements::operator=(
    const HomePreviewRequirements&) = default;
HomePreviewRequirements::HomePreviewRequirements(
    HomePreviewRequirements&&) noexcept = default;
HomePreviewRequirements& HomePreviewRequirements::operator=(
    HomePreviewRequirements&&) noexcept = default;

void DaoHomeMutationLease::CommitGuard::LockLease(
    const DaoHomeMutationLease* lease) {
  if (lease->parent_) {
    LockLease(lease->parent_.get());
  }
  locks_.emplace_back(lease->commit_lock_);
  valid_ = valid_ && lease->valid_.load(std::memory_order_acquire);
}

void DaoHomeMutationLease::Invalidate() {
  base::AutoLock lock(commit_lock_);
  valid_.store(false, std::memory_order_release);
}

bool DaoHomeMutationLease::IsValid() const {
  return valid_.load(std::memory_order_acquire) &&
         (!parent_ || parent_->IsValid());
}

HomeConnectorPermission::HomeConnectorPermission() = default;
HomeConnectorPermission::~HomeConnectorPermission() = default;
HomeConnectorPermission::HomeConnectorPermission(
    const HomeConnectorPermission&) = default;
HomeConnectorPermission& HomeConnectorPermission::operator=(
    const HomeConnectorPermission&) = default;
HomeConnectorPermission::HomeConnectorPermission(
    HomeConnectorPermission&&) noexcept = default;
HomeConnectorPermission& HomeConnectorPermission::operator=(
    HomeConnectorPermission&&) noexcept = default;

HomeConnector::HomeConnector() = default;
HomeConnector::~HomeConnector() = default;
HomeConnector::HomeConnector(const HomeConnector&) = default;
HomeConnector& HomeConnector::operator=(const HomeConnector&) = default;
HomeConnector::HomeConnector(HomeConnector&&) noexcept = default;
HomeConnector& HomeConnector::operator=(HomeConnector&&) noexcept = default;

HomeManifest::HomeManifest() = default;
HomeManifest::~HomeManifest() = default;
HomeManifest::HomeManifest(const HomeManifest&) = default;
HomeManifest& HomeManifest::operator=(const HomeManifest&) = default;
HomeManifest::HomeManifest(HomeManifest&&) noexcept = default;
HomeManifest& HomeManifest::operator=(HomeManifest&&) noexcept = default;

HomeDraft::HomeDraft() = default;
HomeDraft::~HomeDraft() = default;
HomeDraft::HomeDraft(const HomeDraft&) = default;
HomeDraft& HomeDraft::operator=(const HomeDraft&) = default;
HomeDraft::HomeDraft(HomeDraft&&) noexcept = default;
HomeDraft& HomeDraft::operator=(HomeDraft&&) noexcept = default;

HomeVersion::HomeVersion() = default;
HomeVersion::~HomeVersion() = default;
HomeVersion::HomeVersion(const HomeVersion&) = default;
HomeVersion& HomeVersion::operator=(const HomeVersion&) = default;
HomeVersion::HomeVersion(HomeVersion&&) noexcept = default;
HomeVersion& HomeVersion::operator=(HomeVersion&&) noexcept = default;

HomeSnapshot::HomeSnapshot() = default;
HomeSnapshot::~HomeSnapshot() = default;
HomeSnapshot::HomeSnapshot(const HomeSnapshot&) = default;
HomeSnapshot& HomeSnapshot::operator=(const HomeSnapshot&) = default;
HomeSnapshot::HomeSnapshot(HomeSnapshot&&) noexcept = default;
HomeSnapshot& HomeSnapshot::operator=(HomeSnapshot&&) noexcept = default;

HomeConnectorBundle::HomeConnectorBundle() = default;
HomeConnectorBundle::~HomeConnectorBundle() = default;
HomeConnectorBundle::HomeConnectorBundle(const HomeConnectorBundle&) = default;
HomeConnectorBundle& HomeConnectorBundle::operator=(
    const HomeConnectorBundle&) = default;
HomeConnectorBundle::HomeConnectorBundle(HomeConnectorBundle&&) noexcept =
    default;
HomeConnectorBundle& HomeConnectorBundle::operator=(
    HomeConnectorBundle&&) noexcept = default;

HomeConnectorAuthorization::HomeConnectorAuthorization() = default;
HomeConnectorAuthorization::~HomeConnectorAuthorization() = default;
HomeConnectorAuthorization::HomeConnectorAuthorization(
    const HomeConnectorAuthorization&) = default;
HomeConnectorAuthorization& HomeConnectorAuthorization::operator=(
    const HomeConnectorAuthorization&) = default;
HomeConnectorAuthorization::HomeConnectorAuthorization(
    HomeConnectorAuthorization&&) noexcept = default;
HomeConnectorAuthorization& HomeConnectorAuthorization::operator=(
    HomeConnectorAuthorization&&) noexcept = default;

HomePermissionBatchItem::HomePermissionBatchItem() = default;
HomePermissionBatchItem::~HomePermissionBatchItem() = default;
HomePermissionBatchItem::HomePermissionBatchItem(
    const HomePermissionBatchItem&) = default;
HomePermissionBatchItem& HomePermissionBatchItem::operator=(
    const HomePermissionBatchItem&) = default;
HomePermissionBatchItem::HomePermissionBatchItem(
    HomePermissionBatchItem&&) noexcept = default;
HomePermissionBatchItem& HomePermissionBatchItem::operator=(
    HomePermissionBatchItem&&) noexcept = default;

HomePermissionBatchRequest::HomePermissionBatchRequest() = default;
HomePermissionBatchRequest::~HomePermissionBatchRequest() = default;
HomePermissionBatchRequest::HomePermissionBatchRequest(
    const HomePermissionBatchRequest&) = default;
HomePermissionBatchRequest& HomePermissionBatchRequest::operator=(
    const HomePermissionBatchRequest&) = default;
HomePermissionBatchRequest::HomePermissionBatchRequest(
    HomePermissionBatchRequest&&) noexcept = default;
HomePermissionBatchRequest& HomePermissionBatchRequest::operator=(
    HomePermissionBatchRequest&&) noexcept = default;

HomeConnectorTestOutcome::HomeConnectorTestOutcome() = default;
HomeConnectorTestOutcome::~HomeConnectorTestOutcome() = default;
HomeConnectorTestOutcome::HomeConnectorTestOutcome(
    HomeConnectorTestOutcome&&) noexcept = default;
HomeConnectorTestOutcome& HomeConnectorTestOutcome::operator=(
    HomeConnectorTestOutcome&&) noexcept = default;

HomeDiagnostic::HomeDiagnostic() = default;
HomeDiagnostic::~HomeDiagnostic() = default;
HomeDiagnostic::HomeDiagnostic(const HomeDiagnostic&) = default;
HomeDiagnostic& HomeDiagnostic::operator=(const HomeDiagnostic&) = default;
HomeDiagnostic::HomeDiagnostic(HomeDiagnostic&&) noexcept = default;
HomeDiagnostic& HomeDiagnostic::operator=(HomeDiagnostic&&) noexcept = default;

HomePermissionRequest::HomePermissionRequest() = default;
HomePermissionRequest::~HomePermissionRequest() = default;
HomePermissionRequest::HomePermissionRequest(const HomePermissionRequest&) =
    default;
HomePermissionRequest& HomePermissionRequest::operator=(
    const HomePermissionRequest&) = default;
HomePermissionRequest::HomePermissionRequest(HomePermissionRequest&&) noexcept =
    default;
HomePermissionRequest& HomePermissionRequest::operator=(
    HomePermissionRequest&&) noexcept = default;

const char* HomeRevisionKindToString(HomeRevisionKind kind) {
  switch (kind) {
    case HomeRevisionKind::kInitial:
      return "initial";
    case HomeRevisionKind::kUserRequest:
      return "user_request";
    case HomeRevisionKind::kHistoryBootstrap:
      return "history_bootstrap";
    case HomeRevisionKind::kSourceConnection:
      return "source_connection";
    case HomeRevisionKind::kRepair:
      return "repair";
    case HomeRevisionKind::kRollback:
      return "rollback";
    case HomeRevisionKind::kImport:
      return "import";
  }
}

std::optional<HomeRevisionKind> HomeRevisionKindFromString(
    std::string_view value) {
  if (value == "initial") {
    return HomeRevisionKind::kInitial;
  }
  if (value == "user_request") {
    return HomeRevisionKind::kUserRequest;
  }
  if (value == "history_bootstrap") {
    return HomeRevisionKind::kHistoryBootstrap;
  }
  if (value == "source_connection") {
    return HomeRevisionKind::kSourceConnection;
  }
  if (value == "repair") {
    return HomeRevisionKind::kRepair;
  }
  if (value == "rollback") {
    return HomeRevisionKind::kRollback;
  }
  if (value == "import") {
    return HomeRevisionKind::kImport;
  }
  return std::nullopt;
}

const char* HomePageCapabilityToString(HomePageCapability capability) {
  switch (capability) {
    case HomePageCapability::kReadDom:
      return "read_dom";
    case HomePageCapability::kReadStyle:
      return "read_style";
    case HomePageCapability::kScroll:
      return "scroll";
  }
}

std::optional<HomePageCapability> HomePageCapabilityFromString(
    std::string_view value) {
  if (value == "read_dom") {
    return HomePageCapability::kReadDom;
  }
  if (value == "read_style") {
    return HomePageCapability::kReadStyle;
  }
  if (value == "scroll") {
    return HomePageCapability::kScroll;
  }
  return std::nullopt;
}

}  // namespace dao
