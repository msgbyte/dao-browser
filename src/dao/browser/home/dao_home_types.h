// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_TYPES_H_
#define DAO_BROWSER_HOME_DAO_HOME_TYPES_H_

#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/values.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace dao {

enum class HomeError {
  kInvalidArgument,
  kInvalidPath,
  kNotFound,
  kAlreadyExists,
  kUnsupportedFormat,
  kInvalidManifest,
  kInvalidPatch,
  kPatchContextMismatch,
  kQuotaExceeded,
  kStaleRevision,
  kInvalidDraft,
  kPermissionRequired,
  kCancelled,
  kIoError,
};

// A thread-safe, one-way authorization shared by the trusted Home document,
// the owning Agent turn, and the blocking project store. Child leases inherit
// invalidation from their document lease.
class DaoHomeMutationLease
    : public base::RefCountedThreadSafe<DaoHomeMutationLease> {
 public:
  class CommitGuard {
   public:
    explicit CommitGuard(
        const scoped_refptr<DaoHomeMutationLease>& authorization);
    ~CommitGuard();

    CommitGuard(const CommitGuard&) = delete;
    CommitGuard& operator=(const CommitGuard&) = delete;

    bool is_valid() const { return valid_; }

   private:
    void LockLease(const DaoHomeMutationLease* lease);

    std::vector<base::MovableAutoLock> locks_;
    bool valid_ = true;
  };

  explicit DaoHomeMutationLease(
      scoped_refptr<DaoHomeMutationLease> parent = nullptr);

  void Invalidate();
  bool IsValid() const;

 private:
  friend class base::RefCountedThreadSafe<DaoHomeMutationLease>;
  ~DaoHomeMutationLease();

  const scoped_refptr<DaoHomeMutationLease> parent_;
  mutable base::Lock commit_lock_;
  std::atomic_bool valid_{true};
};

enum class HomeAccessMode { kRead };

enum class HomePageCapability {
  kReadDom,
  kReadStyle,
  kScroll,
};

enum class HomeRevisionKind {
  kInitial,
  kUserRequest,
  kHistoryBootstrap,
  kSourceConnection,
  kRepair,
  kRollback,
  kImport,
};

enum class HomeSourceEligibility {
  kLaunchAndFeed,
  kLaunchOnly,
  kSensitiveLaunchOnly,
  kUnsupported,
};

struct HomeLaunchTarget {
  HomeLaunchTarget();
  ~HomeLaunchTarget();
  HomeLaunchTarget(const HomeLaunchTarget&);
  HomeLaunchTarget& operator=(const HomeLaunchTarget&);
  HomeLaunchTarget(HomeLaunchTarget&&) noexcept;
  HomeLaunchTarget& operator=(HomeLaunchTarget&&) noexcept;

  std::string id;
  std::string label_hint;
  GURL url;
  std::string category_hint;
  HomeSourceEligibility source_eligibility =
      HomeSourceEligibility::kUnsupported;
};

struct HomeSourceCandidate {
  HomeSourceCandidate();
  ~HomeSourceCandidate();
  HomeSourceCandidate(const HomeSourceCandidate&);
  HomeSourceCandidate& operator=(const HomeSourceCandidate&);
  HomeSourceCandidate(HomeSourceCandidate&&) noexcept;
  HomeSourceCandidate& operator=(HomeSourceCandidate&&) noexcept;

  std::string launch_target_id;
  std::string connector_kind_hint;
  GURL collection_url;
  std::string content_intent;
  std::vector<std::string> content_kinds;
  std::string schema_source;
};

struct HomeSourceProposal {
  HomeSourceProposal();
  ~HomeSourceProposal();
  HomeSourceProposal(const HomeSourceProposal&);
  HomeSourceProposal& operator=(const HomeSourceProposal&);
  HomeSourceProposal(HomeSourceProposal&&) noexcept;
  HomeSourceProposal& operator=(HomeSourceProposal&&) noexcept;

  std::string launch_target_id;
  GURL collection_url;
  std::string content_intent;
  std::vector<std::string> content_kinds;
};

struct HomeBootstrapBrief {
  HomeBootstrapBrief();
  ~HomeBootstrapBrief();
  HomeBootstrapBrief(const HomeBootstrapBrief&);
  HomeBootstrapBrief& operator=(const HomeBootstrapBrief&);
  HomeBootstrapBrief(HomeBootstrapBrief&&) noexcept;
  HomeBootstrapBrief& operator=(HomeBootstrapBrief&&) noexcept;

  std::vector<HomeLaunchTarget> launch_targets;
  std::vector<HomeSourceCandidate> source_candidates;
  std::string locale;
};

struct HomeExperience {
  HomeExperience();
  ~HomeExperience();
  HomeExperience(const HomeExperience&);
  HomeExperience& operator=(const HomeExperience&);
  HomeExperience(HomeExperience&&) noexcept;
  HomeExperience& operator=(HomeExperience&&) noexcept;

  std::vector<std::string> primary_actions;
  std::vector<std::string> source_slots;
};

struct HomePreviewRequirements {
  HomePreviewRequirements();
  ~HomePreviewRequirements();
  HomePreviewRequirements(const HomePreviewRequirements&);
  HomePreviewRequirements& operator=(const HomePreviewRequirements&);
  HomePreviewRequirements(HomePreviewRequirements&&) noexcept;
  HomePreviewRequirements& operator=(HomePreviewRequirements&&) noexcept;

  std::optional<HomeExperience> experience;
  std::map<std::string, GURL> launch_urls;
  base::flat_set<std::string> tested_connector_ids;
};

enum class HomeBootstrapState {
  kPlanning,
  kAwaitingBatchApproval,
  kTestingSources,
  kBuildingFinalHome,
  kPreviewing,
  kPublishing,
  kComplete,
  kCancelled,
  kFailed,
};

struct HomeConnectorPermission {
  HomeConnectorPermission();
  ~HomeConnectorPermission();
  HomeConnectorPermission(const HomeConnectorPermission&);
  HomeConnectorPermission& operator=(const HomeConnectorPermission&);
  HomeConnectorPermission(HomeConnectorPermission&&) noexcept;
  HomeConnectorPermission& operator=(HomeConnectorPermission&&) noexcept;

  std::vector<url::Origin> origins;
  std::vector<std::string> paths;
  base::flat_set<HomePageCapability> capabilities;
  HomeAccessMode mode = HomeAccessMode::kRead;
};

struct HomeConnector {
  HomeConnector();
  ~HomeConnector();
  HomeConnector(const HomeConnector&);
  HomeConnector& operator=(const HomeConnector&);
  HomeConnector(HomeConnector&&) noexcept;
  HomeConnector& operator=(HomeConnector&&) noexcept;

  std::string id;
  std::string module;
  std::string schema;
  HomeConnectorPermission permissions;
};

struct HomeLimits {
  int64_t max_result_bytes = 1024 * 1024;
  int max_items_per_connector = 100;
};

struct HomeManifest {
  HomeManifest();
  ~HomeManifest();
  HomeManifest(const HomeManifest&);
  HomeManifest& operator=(const HomeManifest&);
  HomeManifest(HomeManifest&&) noexcept;
  HomeManifest& operator=(HomeManifest&&) noexcept;

  int format_version = 1;
  std::string entry;
  std::vector<std::string> routes;
  std::vector<HomeConnector> connectors;
  HomeLimits limits;
};

struct HomeDraft {
  HomeDraft();
  ~HomeDraft();
  HomeDraft(const HomeDraft&);
  HomeDraft& operator=(const HomeDraft&);
  HomeDraft(HomeDraft&&) noexcept;
  HomeDraft& operator=(HomeDraft&&) noexcept;

  std::string id;
  std::string base_revision;
  std::string summary;
  HomeManifest manifest;
  std::optional<HomeLimits> previous_limits;
  bool permission_expansion = false;
  std::vector<std::string> permission_expanded_connector_ids;
};

struct HomeVersion {
  HomeVersion();
  ~HomeVersion();
  HomeVersion(const HomeVersion&);
  HomeVersion& operator=(const HomeVersion&);
  HomeVersion(HomeVersion&&) noexcept;
  HomeVersion& operator=(HomeVersion&&) noexcept;

  std::string id;
  std::string parent;
  std::string restored_from;
  std::string summary;
  HomeRevisionKind kind = HomeRevisionKind::kUserRequest;
  int64_t created_at_ms = 0;
  std::vector<std::string> changed_files;
};

struct HomeSnapshot {
  HomeSnapshot();
  ~HomeSnapshot();
  HomeSnapshot(const HomeSnapshot&);
  HomeSnapshot& operator=(const HomeSnapshot&);
  HomeSnapshot(HomeSnapshot&&) noexcept;
  HomeSnapshot& operator=(HomeSnapshot&&) noexcept;

  bool has_project = false;
  std::string revision;
  std::optional<HomeManifest> manifest;
  std::vector<std::string> granted_connector_ids;
};

struct HomeConnectorBundle {
  HomeConnectorBundle();
  ~HomeConnectorBundle();
  HomeConnectorBundle(const HomeConnectorBundle&);
  HomeConnectorBundle& operator=(const HomeConnectorBundle&);
  HomeConnectorBundle(HomeConnectorBundle&&) noexcept;
  HomeConnectorBundle& operator=(HomeConnectorBundle&&) noexcept;

  HomeConnector connector;
  HomeLimits limits;
  std::string module_source;
  std::string schema_source;
  bool granted = false;
};

struct HomeConnectorAuthorization {
  HomeConnectorAuthorization();
  ~HomeConnectorAuthorization();
  HomeConnectorAuthorization(const HomeConnectorAuthorization&);
  HomeConnectorAuthorization& operator=(const HomeConnectorAuthorization&);
  HomeConnectorAuthorization(HomeConnectorAuthorization&&) noexcept;
  HomeConnectorAuthorization& operator=(HomeConnectorAuthorization&&) noexcept;

  std::string connector_id;
  HomeConnectorBundle bundle;
  std::string fingerprint;
};

struct HomePermissionBatchItem {
  HomePermissionBatchItem();
  ~HomePermissionBatchItem();
  HomePermissionBatchItem(const HomePermissionBatchItem&);
  HomePermissionBatchItem& operator=(const HomePermissionBatchItem&);
  HomePermissionBatchItem(HomePermissionBatchItem&&) noexcept;
  HomePermissionBatchItem& operator=(HomePermissionBatchItem&&) noexcept;

  std::string connector_id;
  std::string label;
  HomeConnector connector;
  std::optional<HomeLimits> previous_limits;
  HomeLimits requested_limits;
  std::string fingerprint;
  bool authentication_may_be_required = false;
};

struct HomePermissionBatchRequest {
  HomePermissionBatchRequest();
  ~HomePermissionBatchRequest();
  HomePermissionBatchRequest(const HomePermissionBatchRequest&);
  HomePermissionBatchRequest& operator=(const HomePermissionBatchRequest&);
  HomePermissionBatchRequest(HomePermissionBatchRequest&&) noexcept;
  HomePermissionBatchRequest& operator=(HomePermissionBatchRequest&&) noexcept;

  std::string id;
  std::string transaction_id;
  std::string draft_id;
  std::string base_revision;
  std::vector<HomePermissionBatchItem> items;
};

enum class HomeConnectorTestStatus {
  kApproved,
  kSucceeded,
  kAuthenticationRequired,
  kRuntimeFailed,
  kSchemaFailed,
  kDeselected,
};

struct HomeConnectorTestOutcome {
  HomeConnectorTestOutcome();
  ~HomeConnectorTestOutcome();
  HomeConnectorTestOutcome(const HomeConnectorTestOutcome&) = delete;
  HomeConnectorTestOutcome& operator=(const HomeConnectorTestOutcome&) = delete;
  HomeConnectorTestOutcome(HomeConnectorTestOutcome&&) noexcept;
  HomeConnectorTestOutcome& operator=(HomeConnectorTestOutcome&&) noexcept;

  std::string connector_id;
  std::string fingerprint;
  HomeConnectorTestStatus status = HomeConnectorTestStatus::kApproved;
  std::optional<base::Value> sample;
  std::string error_code;
};

// Bounded, non-content connector metadata retained for explicit user-triggered
// repair. Diagnostics never include collected page text or connector results.
struct HomeDiagnostic {
  HomeDiagnostic();
  ~HomeDiagnostic();
  HomeDiagnostic(const HomeDiagnostic&);
  HomeDiagnostic& operator=(const HomeDiagnostic&);
  HomeDiagnostic(HomeDiagnostic&&) noexcept;
  HomeDiagnostic& operator=(HomeDiagnostic&&) noexcept;

  std::string revision;
  std::string connector_id;
  std::string stage;
  std::string code;
  std::string origin;
  std::string path;
  std::string detail;
  int64_t created_at_ms = 0;
};

// Opaque trusted-host approval request created by an Agent tool. The request
// contains a normalized connector scope, but only the trusted Home WebUI can
// consume its ID to publish and record the grant.
struct HomePermissionRequest {
  HomePermissionRequest();
  ~HomePermissionRequest();
  HomePermissionRequest(const HomePermissionRequest&);
  HomePermissionRequest& operator=(const HomePermissionRequest&);
  HomePermissionRequest(HomePermissionRequest&&) noexcept;
  HomePermissionRequest& operator=(HomePermissionRequest&&) noexcept;

  std::string id;
  std::string draft_id;
  std::string base_revision;
  HomeConnector connector;
  std::optional<HomeLimits> previous_limits;
  HomeLimits requested_limits;
};

const char* HomeRevisionKindToString(HomeRevisionKind kind);
std::optional<HomeRevisionKind> HomeRevisionKindFromString(
    std::string_view value);
const char* HomePageCapabilityToString(HomePageCapability capability);
std::optional<HomePageCapability> HomePageCapabilityFromString(
    std::string_view value);

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_TYPES_H_
