// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_PROJECT_STORE_H_
#define DAO_BROWSER_HOME_DAO_HOME_PROJECT_STORE_H_

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/types/expected.h"
#include "dao/browser/home/dao_home_types.h"

namespace dao {

// Blocking filesystem implementation for one Profile's Dao Home project.
// Callers that live on the UI sequence must use DaoHomeProjectService, which
// serializes these operations on a MayBlock task runner.
class DaoHomeProjectStore {
 public:
  explicit DaoHomeProjectStore(const base::FilePath& profile_path);
  ~DaoHomeProjectStore();

  DaoHomeProjectStore(const DaoHomeProjectStore&) = delete;
  DaoHomeProjectStore& operator=(const DaoHomeProjectStore&) = delete;

  base::expected<void, HomeError> Initialize();

  HomeSnapshot GetSnapshot() const;
  std::vector<HomeVersion> ListVersions() const;
  base::expected<std::vector<std::string>, HomeError> ListFiles(
      const std::string& revision) const;
  base::expected<std::string, HomeError> ReadFile(
      const std::string& revision,
      const std::string& relative_path) const;
  base::expected<HomeDraft, HomeError> GetDraft(
      const std::string& draft_id) const;
  base::expected<std::string, HomeError> ReadDraftFile(
      const std::string& draft_id,
      const std::string& relative_path) const;
  base::expected<HomeConnectorBundle, HomeError> GetConnectorBundle(
      const std::string& revision,
      const std::string& connector_id) const;
  base::expected<HomeConnectorBundle, HomeError> GetDraftConnectorBundle(
      const std::string& draft_id,
      const std::string& connector_id) const;
  base::expected<std::vector<HomeConnectorAuthorization>, HomeError>
  GetDraftConnectorAuthorizations(
      const std::string& draft_id,
      const std::vector<std::string>& connector_ids) const;
  base::expected<HomeDraft, HomeError> PrepareHistoryBootstrapDraft(
      const std::string& draft_id,
      const HomeBootstrapBrief& brief,
      const std::vector<std::string>& connector_ids);
  base::expected<HomeDraft, HomeError> PrepareHistoryBootstrapFinalDraft(
      const std::string& draft_id,
      const std::string& provisional_draft_id,
      const std::vector<std::string>& connector_ids);
  base::expected<HomeExperience, HomeError> GetDraftExperience(
      const std::string& draft_id) const;
  base::expected<void, HomeError> NormalizeHistoryBootstrapExperience(
      const std::string& draft_id,
      const HomeExperience& experience);
  base::expected<void, HomeError> ValidateHistoryBootstrapFiles(
      const std::string& draft_id) const;

  base::expected<HomeDraft, HomeError> ApplyPatch(
      const std::string& base_revision,
      const std::string& patch_text,
      const std::string& summary);
  base::expected<HomeDraft, HomeError> ReplaceFiles(
      const std::string& base_revision,
      const std::vector<std::pair<std::string, std::string>>& files,
      const std::string& summary);
  base::expected<HomeDraft, HomeError> AddAsset(
      const std::string& base_revision,
      const std::string& relative_path,
      const std::string& base64_contents,
      const std::string& summary);
  base::expected<HomeVersion, HomeError> Publish(
      const std::string& draft_id,
      HomeRevisionKind kind,
      scoped_refptr<DaoHomeMutationLease> authorization);
  base::expected<HomeVersion, HomeError> PublishWithGrant(
      const std::string& draft_id,
      const std::string& connector_id,
      HomeRevisionKind kind,
      scoped_refptr<DaoHomeMutationLease> authorization);
  base::expected<HomeVersion, HomeError> PublishWithGrants(
      const std::string& draft_id,
      const std::vector<std::string>& connector_ids,
      HomeRevisionKind kind,
      scoped_refptr<DaoHomeMutationLease> authorization);
  base::expected<HomeVersion, HomeError> Rollback(
      const std::string& base_revision,
      const std::string& target_revision,
      const std::string& summary,
      scoped_refptr<DaoHomeMutationLease> authorization);
  base::expected<void, HomeError> Reset(
      const std::string& base_revision,
      scoped_refptr<DaoHomeMutationLease> authorization);

  base::expected<std::string, HomeError> ExportProject() const;
  base::expected<HomeDraft, HomeError> ImportProject(
      const std::string& base_revision,
      const std::string& package_json,
      const std::string& summary);
  base::expected<void, HomeError> DiscardDraft(const std::string& draft_id);

  base::expected<void, HomeError> GrantConnector(
      const std::string& connector_id);
  base::expected<void, HomeError> RevokeConnector(
      const std::string& connector_id);
  bool HasGrant(const std::string& connector_id) const;

  const base::FilePath& root_for_testing() const { return root_; }
  void SetBeforeRollbackPublishCallbackForTesting(base::OnceClosure callback) {
    before_rollback_publish_for_testing_ = std::move(callback);
  }

 private:
  struct DraftState {
    DraftState();
    DraftState(HomeDraft draft,
               base::FilePath path,
               std::vector<std::string> changed_files,
               std::string restored_from);
    ~DraftState();
    DraftState(const DraftState&);
    DraftState& operator=(const DraftState&);
    DraftState(DraftState&&) noexcept;
    DraftState& operator=(DraftState&&) noexcept;

    HomeDraft draft;
    base::FilePath path;
    std::vector<std::string> changed_files;
    std::string restored_from;
    std::vector<std::pair<HomeVersion, base::FilePath>> imported_revisions;
  };

  base::expected<void, HomeError> LoadState();
  base::expected<void, HomeError> PersistState() const;
  base::expected<HomeManifest, HomeError> ValidateProject(
      const base::FilePath& project_root) const;
  base::expected<HomeDraft, HomeError> RegisterDraft(
      const std::string& base_revision,
      const std::string& summary,
      const base::FilePath& draft_path,
      std::vector<std::string> changed_files,
      std::string restored_from);
  const HomeConnector* FindConnector(const HomeManifest& manifest,
                                     const std::string& connector_id) const;
  bool HasVersion(const std::string& revision) const;
  base::expected<HomeVersion, HomeError> PublishInternal(
      const std::string& draft_id,
      HomeRevisionKind kind,
      const std::vector<std::string>& connector_ids,
      scoped_refptr<DaoHomeMutationLease> authorization);
  base::FilePath RevisionPath(const std::string& revision) const;

  base::FilePath root_;
  base::FilePath revisions_root_;
  base::FilePath temporary_root_;
  std::string current_revision_;
  std::vector<HomeVersion> versions_;
  std::map<std::string, std::string> grants_;
  std::map<std::string, DraftState> drafts_;
  std::optional<HomeManifest> current_manifest_;
  base::OnceClosure before_rollback_publish_for_testing_;
};

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_PROJECT_STORE_H_
