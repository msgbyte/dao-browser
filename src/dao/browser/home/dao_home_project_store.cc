// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_project_store.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/base64.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "dao/browser/agent/workspace/text_only_filter.h"
#include "dao/browser/agent/workspace/v4a_patch_applier.h"
#include "dao/browser/agent/workspace/v4a_patch_parser.h"
#include "dao/browser/agent/workspace/workspace_quota.h"
#include "dao/browser/home/dao_home_experience.h"
#include "dao/browser/home/dao_home_manifest.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace dao {
namespace {

constexpr char kHomeDirectory[] = "DaoHome";
constexpr char kStateFile[] = "state.json";
constexpr char kManifestFile[] = "manifest.json";
constexpr char kExperienceFile[] = "experience.json";
constexpr char kPackageKind[] = "dao-home-project";
constexpr int64_t kMaxProjectBytes = 20 * 1024 * 1024;
constexpr int kMaxProjectFiles = 500;
constexpr int64_t kMaxHistoryBytes = 100 * 1024 * 1024;
constexpr size_t kMaxImportedVersions = 100;

bool IsSameSite(const GURL& left, const GURL& right) {
  if (left.host() == right.host()) {
    return true;
  }
  const std::string left_domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          left, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  const std::string right_domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          right, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  return !left_domain.empty() && left_domain == right_domain;
}

bool RequiresScopedFeedCards(std::string_view content_intent) {
  return content_intent == "following_feed" ||
         content_intent == "subscription_feed" ||
         content_intent == "activity_feed";
}

bool ContainsUnscopedFeedFallback(std::string_view module_source) {
  constexpr std::array<std::string_view, 12> kUnscopedSelectors = {
      "\"a[href]\"",
      "'a[href]'",
      "\"main a[href]\"",
      "'main a[href]'",
      "\"body a[href]\"",
      "'body a[href]'",
      "\"a\"",
      "'a'",
      "\"main a\"",
      "'main a'",
      "\"body a\"",
      "'body a'",
  };
  return std::ranges::any_of(
      kUnscopedSelectors, [&](std::string_view selector) {
        return module_source.find(selector) != std::string_view::npos;
      });
}

bool HasOnlyStructuredFeedQueries(std::string_view module_source) {
  bool found_query = false;
  size_t position = 0;
  while ((position = module_source.find("queryAll", position)) !=
         std::string_view::npos) {
    const size_t open = module_source.find('(', position + 8);
    if (open == std::string_view::npos) {
      return false;
    }
    size_t cursor = open + 1;
    while (cursor < module_source.size() &&
           base::IsAsciiWhitespace(module_source[cursor])) {
      ++cursor;
    }
    if (cursor >= module_source.size() ||
        (module_source[cursor] != '\'' && module_source[cursor] != '"')) {
      return false;
    }
    const char quote = module_source[cursor++];
    const size_t selector_start = cursor;
    while (cursor < module_source.size() && module_source[cursor] != quote) {
      if (module_source[cursor] == '\\' && cursor + 1 < module_source.size()) {
        cursor += 2;
      } else {
        ++cursor;
      }
    }
    if (cursor >= module_source.size()) {
      return false;
    }
    const std::string selector(
        module_source.substr(selector_start, cursor - selector_start));
    for (std::string_view branch : base::SplitStringPiece(
             selector, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      const std::string lower = base::ToLowerASCII(branch);
      if (lower == "a" || lower.starts_with("a[") || lower.starts_with("a.") ||
          lower.starts_with("a#") || lower.starts_with("a:") ||
          lower.starts_with("main") || lower.starts_with("body") ||
          lower.starts_with("html") || lower.starts_with("*") ||
          lower.starts_with("[href")) {
        return false;
      }
    }
    ++cursor;
    while (cursor < module_source.size() &&
           base::IsAsciiWhitespace(module_source[cursor])) {
      ++cursor;
    }
    if (cursor >= module_source.size() || module_source[cursor] != ',') {
      return false;
    }
    found_query = true;
    position = cursor + 1;
  }
  return found_query;
}

bool ContainsPersistedBootstrapReportKey(const base::Value& value) {
  if (value.is_list()) {
    for (const base::Value& child : value.GetList()) {
      if (ContainsPersistedBootstrapReportKey(child)) {
        return true;
      }
    }
    return false;
  }
  if (!value.is_dict()) {
    return false;
  }
  for (const auto [key, child] : value.GetDict()) {
    if (key == "visit_count" || key == "time_buckets" || key == "window_days" ||
        key == "titles" || ContainsPersistedBootstrapReportKey(child)) {
      return true;
    }
  }
  return false;
}

HomeError WorkspaceErrorToHomeError(WorkspaceError error) {
  switch (error) {
    case WorkspaceError::kInvalidPath:
      return HomeError::kInvalidPath;
    case WorkspaceError::kNotFound:
      return HomeError::kNotFound;
    case WorkspaceError::kAlreadyExists:
      return HomeError::kAlreadyExists;
    case WorkspaceError::kQuotaExceeded:
      return HomeError::kQuotaExceeded;
    case WorkspaceError::kPatchContextMismatch:
      return HomeError::kPatchContextMismatch;
    case WorkspaceError::kPatchParseError:
    case WorkspaceError::kBinaryRejected:
      return HomeError::kInvalidPatch;
    case WorkspaceError::kEditNotUnique:
    case WorkspaceError::kIoError:
    case WorkspaceError::kOk:
      return HomeError::kIoError;
  }
}

base::DictValue VersionToValue(const HomeVersion& version) {
  base::ListValue changed_files;
  for (const std::string& path : version.changed_files) {
    changed_files.Append(path);
  }
  return base::DictValue()
      .Set("id", version.id)
      .Set("parent", version.parent)
      .Set("restored_from", version.restored_from)
      .Set("summary", version.summary)
      .Set("kind", HomeRevisionKindToString(version.kind))
      .Set("created_at_ms", static_cast<double>(version.created_at_ms))
      .Set("changed_files", std::move(changed_files));
}

std::optional<HomeVersion> VersionFromValue(const base::Value& value) {
  if (!value.is_dict()) {
    return std::nullopt;
  }
  const base::DictValue& dict = value.GetDict();
  const std::string* id = dict.FindString("id");
  const std::string* parent = dict.FindString("parent");
  const std::string* restored_from = dict.FindString("restored_from");
  const std::string* summary = dict.FindString("summary");
  const std::string* kind = dict.FindString("kind");
  std::optional<double> created_at_ms = dict.FindDouble("created_at_ms");
  const base::ListValue* changed_files = dict.FindList("changed_files");
  if (!id || id->empty() || !parent || !restored_from || !summary || !kind ||
      !created_at_ms || !changed_files) {
    return std::nullopt;
  }
  std::optional<HomeRevisionKind> parsed_kind =
      HomeRevisionKindFromString(*kind);
  if (!parsed_kind) {
    return std::nullopt;
  }
  HomeVersion version;
  version.id = *id;
  version.parent = *parent;
  version.restored_from = *restored_from;
  version.summary = *summary;
  version.kind = *parsed_kind;
  version.created_at_ms = static_cast<int64_t>(*created_at_ms);
  for (const base::Value& path : *changed_files) {
    const std::string* string_path = path.GetIfString();
    if (!string_path || !IsValidHomeRelativePath(*string_path)) {
      return std::nullopt;
    }
    version.changed_files.push_back(*string_path);
  }
  return version;
}

base::expected<void, HomeError> AtomicWrite(const base::FilePath& target,
                                            const std::string& contents) {
  if (!base::CreateDirectory(target.DirName())) {
    return base::unexpected(HomeError::kIoError);
  }
  base::FilePath temporary = target.AddExtensionASCII(
      base::Uuid::GenerateRandomV4().AsLowercaseString());
  if (!base::WriteFile(temporary, contents)) {
    return base::unexpected(HomeError::kIoError);
  }
  if (!base::ReplaceFile(temporary, target, nullptr)) {
    base::DeleteFile(temporary);
    return base::unexpected(HomeError::kIoError);
  }
  return base::ok();
}

std::vector<std::string> ChangedPaths(const V4APatch& patch) {
  std::vector<std::string> paths;
  for (const V4AFileOp& operation : patch.ops) {
    paths.push_back(operation.path);
    if (operation.move_to) {
      paths.push_back(*operation.move_to);
    }
  }
  std::ranges::sort(paths);
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  return paths;
}

}  // namespace

DaoHomeProjectStore::DraftState::DraftState() = default;
DaoHomeProjectStore::DraftState::DraftState(
    HomeDraft draft_value,
    base::FilePath path_value,
    std::vector<std::string> changed_files_value,
    std::string restored_from_value)
    : draft(std::move(draft_value)),
      path(std::move(path_value)),
      changed_files(std::move(changed_files_value)),
      restored_from(std::move(restored_from_value)) {}
DaoHomeProjectStore::DraftState::~DraftState() = default;
DaoHomeProjectStore::DraftState::DraftState(const DraftState&) = default;
DaoHomeProjectStore::DraftState& DaoHomeProjectStore::DraftState::operator=(
    const DraftState&) = default;
DaoHomeProjectStore::DraftState::DraftState(DraftState&&) noexcept = default;
DaoHomeProjectStore::DraftState& DaoHomeProjectStore::DraftState::operator=(
    DraftState&&) noexcept = default;

DaoHomeProjectStore::DaoHomeProjectStore(const base::FilePath& profile_path)
    : root_(profile_path.AppendASCII(kHomeDirectory)),
      revisions_root_(root_.AppendASCII("revisions")),
      temporary_root_(root_.AppendASCII(".tmp")) {}

DaoHomeProjectStore::~DaoHomeProjectStore() = default;

base::expected<void, HomeError> DaoHomeProjectStore::Initialize() {
  const base::FilePath retained_reset =
      temporary_root_.AppendASCII("reset-data");
  if (!base::PathExists(root_.AppendASCII(kStateFile)) &&
      base::DirectoryExists(retained_reset)) {
    const base::FilePath recovery_root = root_.DirName().AppendASCII(
        root_.BaseName().AsUTF8Unsafe() + ".recovery-" +
        base::Uuid::GenerateRandomV4().AsLowercaseString());
    if (!base::Move(retained_reset, recovery_root) ||
        !base::DeletePathRecursively(root_) ||
        !base::Move(recovery_root, root_)) {
      return base::unexpected(HomeError::kIoError);
    }
  }
  if (!base::CreateDirectory(revisions_root_)) {
    return base::unexpected(HomeError::kIoError);
  }
  base::DeletePathRecursively(temporary_root_);
  if (!base::CreateDirectory(temporary_root_)) {
    return base::unexpected(HomeError::kIoError);
  }
  return LoadState();
}

base::expected<void, HomeError> DaoHomeProjectStore::LoadState() {
  current_revision_.clear();
  versions_.clear();
  grants_.clear();
  drafts_.clear();
  current_manifest_.reset();
  const base::FilePath state_path = root_.AppendASCII(kStateFile);
  if (!base::PathExists(state_path)) {
    return base::ok();
  }
  std::string contents;
  if (!base::ReadFileToString(state_path, &contents)) {
    return base::unexpected(HomeError::kIoError);
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(contents, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return base::unexpected(HomeError::kIoError);
  }
  const base::DictValue& state = parsed->GetDict();
  const std::string* current = state.FindString("current_revision");
  const base::ListValue* versions = state.FindList("versions");
  const base::DictValue* grants = state.FindDict("grants");
  if (!current || !versions || !grants) {
    return base::unexpected(HomeError::kIoError);
  }
  for (const base::Value& value : *versions) {
    std::optional<HomeVersion> version = VersionFromValue(value);
    if (!version) {
      return base::unexpected(HomeError::kIoError);
    }
    versions_.push_back(std::move(*version));
  }
  current_revision_ = *current;
  if ((!current_revision_.empty() && !HasVersion(current_revision_)) ||
      (!current_revision_.empty() &&
       !base::DirectoryExists(RevisionPath(current_revision_)))) {
    return base::unexpected(HomeError::kIoError);
  }
  for (const auto [id, fingerprint] : *grants) {
    const std::string* value = fingerprint.GetIfString();
    if (!value) {
      return base::unexpected(HomeError::kIoError);
    }
    grants_.emplace(id, *value);
  }
  if (!current_revision_.empty()) {
    auto manifest = ValidateProject(RevisionPath(current_revision_));
    if (!manifest.has_value()) {
      return base::unexpected(manifest.error());
    }
    current_manifest_ = std::move(manifest.value());
  }
  return base::ok();
}

base::expected<void, HomeError> DaoHomeProjectStore::PersistState() const {
  base::ListValue versions;
  for (const HomeVersion& version : versions_) {
    versions.Append(VersionToValue(version));
  }
  base::DictValue grants;
  for (const auto& [id, fingerprint] : grants_) {
    grants.Set(id, fingerprint);
  }
  base::DictValue state;
  state.Set("format_version", 1);
  state.Set("current_revision", current_revision_);
  state.Set("versions", std::move(versions));
  state.Set("grants", std::move(grants));
  std::string serialized;
  if (!base::JSONWriter::WriteWithOptions(
          state, base::JSONWriter::OPTIONS_PRETTY_PRINT, &serialized)) {
    return base::unexpected(HomeError::kIoError);
  }
  return AtomicWrite(root_.AppendASCII(kStateFile), serialized);
}

HomeSnapshot DaoHomeProjectStore::GetSnapshot() const {
  HomeSnapshot snapshot;
  snapshot.has_project = !current_revision_.empty();
  snapshot.revision = current_revision_;
  snapshot.manifest = current_manifest_;
  if (current_manifest_) {
    for (const HomeConnector& connector : current_manifest_->connectors) {
      if (HasGrant(connector.id)) {
        snapshot.granted_connector_ids.push_back(connector.id);
      }
    }
  }
  return snapshot;
}

std::vector<HomeVersion> DaoHomeProjectStore::ListVersions() const {
  return versions_;
}

bool DaoHomeProjectStore::HasVersion(const std::string& revision) const {
  return std::ranges::any_of(versions_, [&](const HomeVersion& version) {
    return version.id == revision;
  });
}

base::FilePath DaoHomeProjectStore::RevisionPath(
    const std::string& revision) const {
  return revisions_root_.AppendASCII(revision);
}

base::expected<std::vector<std::string>, HomeError>
DaoHomeProjectStore::ListFiles(const std::string& revision) const {
  if (!HasVersion(revision)) {
    return base::unexpected(HomeError::kNotFound);
  }
  const base::FilePath revision_root = RevisionPath(revision);
  std::vector<std::string> files;
  base::FileEnumerator enumerator(
      revision_root, true,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    if (base::IsLink(path)) {
      return base::unexpected(HomeError::kInvalidPath);
    }
    base::FilePath relative;
    if (!revision_root.AppendRelativePath(path, &relative)) {
      return base::unexpected(HomeError::kInvalidPath);
    }
    std::string value = relative.AsUTF8Unsafe();
    if (!IsValidHomeRelativePath(value)) {
      return base::unexpected(HomeError::kInvalidPath);
    }
    files.push_back(std::move(value));
  }
  std::ranges::sort(files);
  return files;
}

base::expected<std::string, HomeError> DaoHomeProjectStore::ReadFile(
    const std::string& revision,
    const std::string& relative_path) const {
  if (!HasVersion(revision)) {
    return base::unexpected(HomeError::kNotFound);
  }
  if (!IsValidHomeRelativePath(relative_path)) {
    return base::unexpected(HomeError::kInvalidPath);
  }
  base::FilePath path = RevisionPath(revision).AppendASCII(relative_path);
  if (base::IsLink(path)) {
    return base::unexpected(HomeError::kInvalidPath);
  }
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(path, &contents, kMaxProjectBytes)) {
    return base::unexpected(base::PathExists(path) ? HomeError::kQuotaExceeded
                                                   : HomeError::kNotFound);
  }
  return contents;
}

base::expected<HomeDraft, HomeError> DaoHomeProjectStore::GetDraft(
    const std::string& draft_id) const {
  const auto found = drafts_.find(draft_id);
  if (found == drafts_.end()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  return found->second.draft;
}

base::expected<std::string, HomeError> DaoHomeProjectStore::ReadDraftFile(
    const std::string& draft_id,
    const std::string& relative_path) const {
  const auto draft = drafts_.find(draft_id);
  if (draft == drafts_.end()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  if (!IsValidHomeRelativePath(relative_path)) {
    return base::unexpected(HomeError::kInvalidPath);
  }
  const base::FilePath path = draft->second.path.AppendASCII(relative_path);
  if (base::IsLink(path)) {
    return base::unexpected(HomeError::kInvalidPath);
  }
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(path, &contents, kMaxProjectBytes)) {
    return base::unexpected(base::PathExists(path) ? HomeError::kQuotaExceeded
                                                   : HomeError::kNotFound);
  }
  return contents;
}

base::expected<HomeConnectorBundle, HomeError>
DaoHomeProjectStore::GetConnectorBundle(const std::string& revision,
                                        const std::string& connector_id) const {
  if (revision != current_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  if (!current_manifest_) {
    return base::unexpected(HomeError::kNotFound);
  }
  const HomeConnector* connector =
      FindConnector(*current_manifest_, connector_id);
  if (!connector) {
    return base::unexpected(HomeError::kNotFound);
  }
  auto module = ReadFile(revision, connector->module);
  auto schema = ReadFile(revision, connector->schema);
  if (!module.has_value()) {
    return base::unexpected(module.error());
  }
  if (!schema.has_value()) {
    return base::unexpected(schema.error());
  }
  HomeConnectorBundle bundle;
  bundle.connector = *connector;
  bundle.limits = current_manifest_->limits;
  bundle.module_source = std::move(module.value());
  bundle.schema_source = std::move(schema.value());
  bundle.granted = HasGrant(connector_id);
  return bundle;
}

base::expected<HomeConnectorBundle, HomeError>
DaoHomeProjectStore::GetDraftConnectorBundle(
    const std::string& draft_id,
    const std::string& connector_id) const {
  const auto draft = drafts_.find(draft_id);
  if (draft == drafts_.end()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  const HomeConnector* connector =
      FindConnector(draft->second.draft.manifest, connector_id);
  if (!connector) {
    return base::unexpected(HomeError::kNotFound);
  }
  auto module = ReadDraftFile(draft_id, connector->module);
  auto schema = ReadDraftFile(draft_id, connector->schema);
  if (!module.has_value()) {
    return base::unexpected(module.error());
  }
  if (!schema.has_value()) {
    return base::unexpected(schema.error());
  }
  HomeConnectorBundle bundle;
  bundle.connector = *connector;
  bundle.limits = draft->second.draft.manifest.limits;
  bundle.module_source = std::move(module.value());
  bundle.schema_source = std::move(schema.value());
  bundle.granted = false;
  return bundle;
}

base::expected<std::vector<HomeConnectorAuthorization>, HomeError>
DaoHomeProjectStore::GetDraftConnectorAuthorizations(
    const std::string& draft_id,
    const std::vector<std::string>& connector_ids) const {
  if (!drafts_.contains(draft_id)) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  std::set<std::string> unique_connector_ids;
  std::vector<HomeConnectorAuthorization> authorizations;
  authorizations.reserve(connector_ids.size());
  for (const std::string& connector_id : connector_ids) {
    if (!unique_connector_ids.insert(connector_id).second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    auto bundle = GetDraftConnectorBundle(draft_id, connector_id);
    if (!bundle.has_value()) {
      return base::unexpected(bundle.error());
    }
    HomeConnectorAuthorization authorization;
    authorization.connector_id = connector_id;
    authorization.fingerprint =
        HomeConnectorFingerprint(bundle->connector, bundle->limits,
                                 bundle->module_source, bundle->schema_source);
    authorization.bundle = std::move(bundle.value());
    authorizations.push_back(std::move(authorization));
  }
  return authorizations;
}

base::expected<HomeDraft, HomeError>
DaoHomeProjectStore::PrepareHistoryBootstrapDraft(
    const std::string& draft_id,
    const HomeBootstrapBrief& brief,
    const std::vector<std::string>& connector_ids) {
  auto draft_it = drafts_.find(draft_id);
  if (draft_it == drafts_.end()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }

  std::map<std::string, const HomeLaunchTarget*> launch_targets;
  for (const HomeLaunchTarget& target : brief.launch_targets) {
    if (target.id.empty() ||
        !launch_targets.emplace(target.id, &target).second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
  }
  std::map<std::string, const HomeSourceCandidate*> candidates;
  for (const HomeSourceCandidate& candidate : brief.source_candidates) {
    if (candidate.launch_target_id.empty() || candidate.schema_source.empty() ||
        (RequiresScopedFeedCards(candidate.content_intent) &&
         candidate.content_kinds.empty()) ||
        !candidates.emplace(candidate.launch_target_id, &candidate).second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
  }

  HomeManifest manifest = draft_it->second.draft.manifest;
  manifest.connectors.clear();
  std::set<std::string> unique_ids;
  std::vector<std::string> changed_files = {kManifestFile};
  for (const std::string& connector_id : connector_ids) {
    const auto candidate = candidates.find(connector_id);
    const auto target = launch_targets.find(connector_id);
    if (!unique_ids.insert(connector_id).second ||
        candidate == candidates.end() || target == launch_targets.end() ||
        target->second->source_eligibility !=
            HomeSourceEligibility::kLaunchAndFeed ||
        !target->second->url.is_valid() ||
        !target->second->url.SchemeIsHTTPOrHTTPS()) {
      return base::unexpected(HomeError::kInvalidArgument);
    }

    HomeConnector connector;
    connector.id = connector_id;
    connector.module = "connectors/" + connector_id + ".js";
    connector.schema = "schemas/" + connector_id + ".json";
    if (!IsValidHomeRelativePath(connector.module) ||
        !IsValidHomeRelativePath(connector.schema)) {
      return base::unexpected(HomeError::kInvalidPath);
    }
    const GURL collection_url = candidate->second->collection_url.is_valid()
                                    ? candidate->second->collection_url
                                    : target->second->url;
    if (!collection_url.SchemeIsHTTPOrHTTPS() ||
        !IsSameSite(collection_url, target->second->url)) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    connector.permissions.origins.push_back(
        url::Origin::Create(collection_url));
    connector.permissions.paths.push_back(
        collection_url.path().empty() ? "/"
                                      : std::string(collection_url.path()));
    connector.permissions.capabilities.insert(HomePageCapability::kReadDom);
    connector.permissions.mode = HomeAccessMode::kRead;

    const base::FilePath module_path =
        draft_it->second.path.AppendASCII(connector.module);
    const base::FilePath schema_path =
        draft_it->second.path.AppendASCII(connector.schema);
    std::string authored_module;
    if (!base::ReadFileToString(module_path, &authored_module) ||
        authored_module.empty()) {
      continue;
    }
    if (RequiresScopedFeedCards(candidate->second->content_intent) &&
        (ContainsUnscopedFeedFallback(authored_module) ||
         !HasOnlyStructuredFeedQueries(authored_module))) {
      continue;
    }
    if (!base::CreateDirectory(schema_path.DirName()) ||
        !base::WriteFile(schema_path, candidate->second->schema_source)) {
      return base::unexpected(HomeError::kIoError);
    }
    changed_files.push_back(connector.schema);
    manifest.connectors.push_back(std::move(connector));
  }

  std::string manifest_json;
  if (!base::JSONWriter::WriteWithOptions(
          HomeManifestToValue(manifest), base::JSONWriter::OPTIONS_PRETTY_PRINT,
          &manifest_json) ||
      !base::WriteFile(draft_it->second.path.AppendASCII(kManifestFile),
                       manifest_json)) {
    return base::unexpected(HomeError::kIoError);
  }
  auto validated = ValidateProject(draft_it->second.path);
  if (!validated.has_value()) {
    return base::unexpected(validated.error());
  }

  HomeDraft& prepared = draft_it->second.draft;
  prepared.manifest = std::move(validated.value());
  prepared.permission_expanded_connector_ids.clear();
  for (const HomeConnector& connector : prepared.manifest.connectors) {
    prepared.permission_expanded_connector_ids.push_back(connector.id);
  }
  prepared.permission_expansion =
      !prepared.permission_expanded_connector_ids.empty();
  for (const std::string& path : changed_files) {
    if (!std::ranges::contains(draft_it->second.changed_files, path)) {
      draft_it->second.changed_files.push_back(path);
    }
  }
  return prepared;
}

base::expected<HomeDraft, HomeError>
DaoHomeProjectStore::PrepareHistoryBootstrapFinalDraft(
    const std::string& draft_id,
    const std::string& provisional_draft_id,
    const std::vector<std::string>& connector_ids) {
  auto draft_it = drafts_.find(draft_id);
  auto provisional_it = drafts_.find(provisional_draft_id);
  if (draft_it == drafts_.end() || provisional_it == drafts_.end() ||
      draft_id == provisional_draft_id) {
    return base::unexpected(HomeError::kInvalidDraft);
  }

  HomeManifest manifest = draft_it->second.draft.manifest;
  manifest.connectors.clear();
  manifest.limits = provisional_it->second.draft.manifest.limits;
  std::set<std::string> unique_ids;
  std::vector<std::string> changed_files = {kManifestFile};
  for (const std::string& connector_id : connector_ids) {
    const auto connector =
        std::ranges::find_if(provisional_it->second.draft.manifest.connectors,
                             [&](const HomeConnector& candidate) {
                               return candidate.id == connector_id;
                             });
    if (!unique_ids.insert(connector_id).second ||
        connector == provisional_it->second.draft.manifest.connectors.end()) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    auto module = ReadDraftFile(provisional_draft_id, connector->module);
    auto schema = ReadDraftFile(provisional_draft_id, connector->schema);
    if (!module.has_value() || !schema.has_value()) {
      return base::unexpected(HomeError::kInvalidDraft);
    }
    auto module_write = AtomicWrite(
        draft_it->second.path.AppendASCII(connector->module), module.value());
    auto schema_write = AtomicWrite(
        draft_it->second.path.AppendASCII(connector->schema), schema.value());
    if (!module_write.has_value() || !schema_write.has_value()) {
      return base::unexpected(HomeError::kIoError);
    }
    changed_files.push_back(connector->module);
    changed_files.push_back(connector->schema);
    manifest.connectors.push_back(*connector);
  }

  std::string manifest_json;
  if (!base::JSONWriter::WriteWithOptions(
          HomeManifestToValue(manifest), base::JSONWriter::OPTIONS_PRETTY_PRINT,
          &manifest_json)) {
    return base::unexpected(HomeError::kIoError);
  }
  auto manifest_write = AtomicWrite(
      draft_it->second.path.AppendASCII(kManifestFile), manifest_json);
  if (!manifest_write.has_value()) {
    return base::unexpected(manifest_write.error());
  }
  auto validated = ValidateProject(draft_it->second.path);
  if (!validated.has_value()) {
    return base::unexpected(validated.error());
  }

  HomeDraft& prepared = draft_it->second.draft;
  prepared.manifest = std::move(validated.value());
  prepared.permission_expanded_connector_ids.clear();
  for (const HomeConnector& connector : prepared.manifest.connectors) {
    prepared.permission_expanded_connector_ids.push_back(connector.id);
  }
  prepared.permission_expansion =
      !prepared.permission_expanded_connector_ids.empty();
  for (const std::string& path : changed_files) {
    if (!std::ranges::contains(draft_it->second.changed_files, path)) {
      draft_it->second.changed_files.push_back(path);
    }
  }
  return prepared;
}

base::expected<HomeExperience, HomeError>
DaoHomeProjectStore::GetDraftExperience(const std::string& draft_id) const {
  if (!drafts_.contains(draft_id)) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  auto contents = ReadDraftFile(draft_id, kExperienceFile);
  if (!contents.has_value()) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  return ParseHomeExperience(contents.value());
}

base::expected<void, HomeError>
DaoHomeProjectStore::NormalizeHistoryBootstrapExperience(
    const std::string& draft_id,
    const HomeExperience& experience) {
  auto draft = drafts_.find(draft_id);
  if (draft == drafts_.end()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  base::ListValue primary_actions;
  for (const std::string& action_id : experience.primary_actions) {
    primary_actions.Append(action_id);
  }
  base::ListValue source_slots;
  for (const std::string& connector_id : experience.source_slots) {
    source_slots.Append(connector_id);
  }
  std::string serialized;
  if (!base::JSONWriter::Write(
          base::DictValue()
              .Set("kind", "start_surface")
              .Set("primary_actions", std::move(primary_actions))
              .Set("source_slots", std::move(source_slots)),
          &serialized) ||
      !base::WriteFile(draft->second.path.AppendASCII(kExperienceFile),
                       serialized)) {
    return base::unexpected(HomeError::kIoError);
  }
  if (!std::ranges::contains(draft->second.changed_files, kExperienceFile)) {
    draft->second.changed_files.push_back(kExperienceFile);
  }
  return ValidateHistoryBootstrapFiles(draft_id);
}

base::expected<void, HomeError>
DaoHomeProjectStore::ValidateHistoryBootstrapFiles(
    const std::string& draft_id) const {
  const auto draft = drafts_.find(draft_id);
  if (draft == drafts_.end()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  int file_count = 0;
  int64_t total_bytes = 0;
  base::FileEnumerator enumerator(draft->second.path, true,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    std::optional<int64_t> size = base::GetFileSize(path);
    if (!size || *size < 0) {
      return base::unexpected(HomeError::kIoError);
    }
    total_bytes += *size;
    if (++file_count > kMaxProjectFiles || total_bytes > kMaxProjectBytes) {
      return base::unexpected(HomeError::kQuotaExceeded);
    }
    if (!IsTextExtensionAllowed(path)) {
      continue;
    }
    std::string contents;
    if (!base::ReadFileToStringWithMaxSize(path, &contents, kMaxProjectBytes)) {
      return base::unexpected(HomeError::kIoError);
    }
    if (ContainsNulByte(contents)) {
      continue;
    }
    std::optional<base::Value> structured =
        base::JSONReader::Read(contents, base::JSON_PARSE_RFC);
    if (structured && ContainsPersistedBootstrapReportKey(*structured)) {
      return base::unexpected(HomeError::kInvalidDraft);
    }
  }
  return base::ok();
}

base::expected<HomeManifest, HomeError> DaoHomeProjectStore::ValidateProject(
    const base::FilePath& project_root) const {
  std::string manifest_contents;
  if (!base::ReadFileToStringWithMaxSize(
          project_root.AppendASCII(kManifestFile), &manifest_contents,
          1024 * 1024)) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  auto manifest = ParseHomeManifest(manifest_contents);
  if (!manifest.has_value()) {
    return base::unexpected(manifest.error());
  }

  int file_count = 0;
  int64_t total_bytes = 0;
  base::FileEnumerator enumerator(
      project_root, true,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    if (base::IsLink(path)) {
      return base::unexpected(HomeError::kInvalidPath);
    }
    base::FilePath relative;
    if (!project_root.AppendRelativePath(path, &relative) ||
        !IsValidHomeRelativePath(relative.AsUTF8Unsafe())) {
      return base::unexpected(HomeError::kInvalidPath);
    }
    std::optional<int64_t> size = base::GetFileSize(path);
    if (!size || *size < 0) {
      return base::unexpected(HomeError::kIoError);
    }
    total_bytes += *size;
    if (++file_count > kMaxProjectFiles || total_bytes > kMaxProjectBytes) {
      return base::unexpected(HomeError::kQuotaExceeded);
    }
  }

  auto require_file = [&](const std::string& relative) {
    return base::PathExists(project_root.AppendASCII(relative)) &&
           !base::IsLink(project_root.AppendASCII(relative));
  };
  if (!require_file(manifest->entry)) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  for (const HomeConnector& connector : manifest->connectors) {
    if (!require_file(connector.module) || !require_file(connector.schema)) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
  }
  return manifest;
}

base::expected<HomeDraft, HomeError> DaoHomeProjectStore::RegisterDraft(
    const std::string& base_revision,
    const std::string& summary,
    const base::FilePath& draft_path,
    std::vector<std::string> changed_files,
    std::string restored_from) {
  auto manifest = ValidateProject(draft_path);
  if (!manifest.has_value()) {
    base::DeletePathRecursively(draft_path);
    return base::unexpected(manifest.error());
  }
  HomeDraft draft;
  draft.id = draft_path.BaseName().AsUTF8Unsafe();
  draft.base_revision = base_revision;
  draft.summary = summary;
  draft.manifest = manifest.value();
  if (current_manifest_) {
    draft.previous_limits = current_manifest_->limits;
    const bool budget_expansion =
        draft.manifest.limits.max_result_bytes >
            current_manifest_->limits.max_result_bytes ||
        draft.manifest.limits.max_items_per_connector >
            current_manifest_->limits.max_items_per_connector;
    for (const HomeConnector& connector : draft.manifest.connectors) {
      const HomeConnector* before =
          FindConnector(*current_manifest_, connector.id);
      if (!before || budget_expansion ||
          HomeConnectorPermissionFingerprint(before->permissions) !=
              HomeConnectorPermissionFingerprint(connector.permissions)) {
        draft.permission_expanded_connector_ids.push_back(connector.id);
      }
    }
  } else {
    for (const HomeConnector& connector : draft.manifest.connectors) {
      draft.permission_expanded_connector_ids.push_back(connector.id);
    }
  }
  draft.permission_expansion = !draft.permission_expanded_connector_ids.empty();
  drafts_.emplace(draft.id,
                  DraftState{draft, draft_path, std::move(changed_files),
                             std::move(restored_from)});
  return draft;
}

base::expected<HomeDraft, HomeError> DaoHomeProjectStore::ApplyPatch(
    const std::string& base_revision,
    const std::string& patch_text,
    const std::string& summary) {
  if (base_revision != current_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  if (summary.empty() || summary.size() > 500) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  auto patch = ParseV4APatch(patch_text);
  if (!patch.has_value()) {
    return base::unexpected(HomeError::kInvalidPatch);
  }
  const std::string draft_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const base::FilePath draft_path = temporary_root_.AppendASCII(draft_id);
  if (!current_revision_.empty()) {
    if (!base::CopyDirectory(RevisionPath(current_revision_), draft_path,
                             true)) {
      return base::unexpected(HomeError::kIoError);
    }
  } else if (!base::CreateDirectory(draft_path)) {
    return base::unexpected(HomeError::kIoError);
  }
  WorkspaceQuota quota(draft_path);
  auto applied =
      ApplyV4APatch(draft_path, temporary_root_.AppendASCII("patches"),
                    draft_id, &quota, patch.value());
  if (!applied.has_value()) {
    base::DeletePathRecursively(draft_path);
    return base::unexpected(WorkspaceErrorToHomeError(applied.error()));
  }
  return RegisterDraft(base_revision, summary, draft_path,
                       ChangedPaths(patch.value()), /*restored_from=*/"");
}

base::expected<HomeDraft, HomeError> DaoHomeProjectStore::ReplaceFiles(
    const std::string& base_revision,
    const std::vector<std::pair<std::string, std::string>>& files,
    const std::string& summary) {
  if (base_revision != current_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  if (current_revision_.empty()) {
    return base::unexpected(HomeError::kNotFound);
  }
  if (summary.empty() || summary.size() > 500 || files.empty() ||
      files.size() > 32) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  std::set<std::string> changed_paths;
  for (const auto& [relative_path, contents] : files) {
    if (!changed_paths.insert(relative_path).second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    if (!IsValidHomeRelativePath(relative_path)) {
      return base::unexpected(HomeError::kInvalidPath);
    }
    const base::FilePath source =
        RevisionPath(current_revision_).AppendASCII(relative_path);
    if (!base::PathExists(source) || base::DirectoryExists(source)) {
      return base::unexpected(HomeError::kNotFound);
    }
    if (!IsTextExtensionAllowed(source) || ContainsNulByte(contents)) {
      return base::unexpected(HomeError::kUnsupportedFormat);
    }
  }

  const std::string draft_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const base::FilePath draft_path = temporary_root_.AppendASCII(draft_id);
  if (!base::CopyDirectory(RevisionPath(current_revision_), draft_path, true)) {
    return base::unexpected(HomeError::kIoError);
  }
  WorkspaceQuota quota(draft_path);
  for (const auto& [relative_path, contents] : files) {
    const base::FilePath target = draft_path.AppendASCII(relative_path);
    const std::optional<int64_t> existing_size = base::GetFileSize(target);
    if (!existing_size || *existing_size < 0) {
      base::DeletePathRecursively(draft_path);
      return base::unexpected(HomeError::kIoError);
    }
    if (!quota.CanAcceptWrite(relative_path, contents.size(),
                              static_cast<uint64_t>(*existing_size))) {
      base::DeletePathRecursively(draft_path);
      return base::unexpected(HomeError::kQuotaExceeded);
    }
    if (!base::WriteFile(target, contents)) {
      base::DeletePathRecursively(draft_path);
      return base::unexpected(HomeError::kIoError);
    }
    quota.InvalidateCache();
  }
  return RegisterDraft(base_revision, summary, draft_path,
                       std::vector<std::string>(changed_paths.begin(),
                                                changed_paths.end()),
                       /*restored_from=*/"");
}

base::expected<HomeDraft, HomeError> DaoHomeProjectStore::AddAsset(
    const std::string& base_revision,
    const std::string& relative_path,
    const std::string& base64_contents,
    const std::string& summary) {
  if (base_revision != current_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  if (summary.empty() || summary.size() > 500) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  if (!IsValidHomeRelativePath(relative_path) ||
      !base::StartsWith(relative_path, "assets/",
                        base::CompareCase::SENSITIVE)) {
    return base::unexpected(HomeError::kInvalidPath);
  }
  std::string decoded;
  if (!base::Base64Decode(base64_contents, &decoded)) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  if (decoded.empty() || decoded.size() > 5 * 1024 * 1024) {
    return base::unexpected(HomeError::kQuotaExceeded);
  }

  const std::string draft_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const base::FilePath draft_path = temporary_root_.AppendASCII(draft_id);
  if (current_revision_.empty() ||
      !base::CopyDirectory(RevisionPath(current_revision_), draft_path, true)) {
    return base::unexpected(HomeError::kIoError);
  }
  const base::FilePath target = draft_path.AppendASCII(relative_path);
  if (!base::CreateDirectory(target.DirName()) ||
      !base::WriteFile(target, decoded)) {
    base::DeletePathRecursively(draft_path);
    return base::unexpected(HomeError::kIoError);
  }
  return RegisterDraft(base_revision, summary, draft_path, {relative_path},
                       /*restored_from=*/"");
}

base::expected<HomeVersion, HomeError> DaoHomeProjectStore::Publish(
    const std::string& draft_id,
    HomeRevisionKind kind,
    scoped_refptr<DaoHomeMutationLease> authorization) {
  return PublishInternal(draft_id, kind, {}, std::move(authorization));
}

base::expected<HomeVersion, HomeError> DaoHomeProjectStore::PublishWithGrant(
    const std::string& draft_id,
    const std::string& connector_id,
    HomeRevisionKind kind,
    scoped_refptr<DaoHomeMutationLease> authorization) {
  return PublishInternal(draft_id, kind, {connector_id},
                         std::move(authorization));
}

base::expected<HomeVersion, HomeError> DaoHomeProjectStore::PublishWithGrants(
    const std::string& draft_id,
    const std::vector<std::string>& connector_ids,
    HomeRevisionKind kind,
    scoped_refptr<DaoHomeMutationLease> authorization) {
  return PublishInternal(draft_id, kind, connector_ids,
                         std::move(authorization));
}

base::expected<HomeVersion, HomeError> DaoHomeProjectStore::PublishInternal(
    const std::string& draft_id,
    HomeRevisionKind kind,
    const std::vector<std::string>& connector_ids,
    scoped_refptr<DaoHomeMutationLease> authorization) {
  if (authorization && !authorization->IsValid()) {
    return base::unexpected(HomeError::kCancelled);
  }
  auto found = drafts_.find(draft_id);
  if (found == drafts_.end()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  if (found->second.draft.base_revision != current_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  auto manifest = ValidateProject(found->second.path);
  if (!manifest.has_value()) {
    return base::unexpected(manifest.error());
  }
  if (kind == HomeRevisionKind::kHistoryBootstrap &&
      !GetDraftExperience(draft_id).has_value()) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  std::map<std::string, std::string> grant_fingerprints;
  std::set<std::string> unique_connector_ids;
  for (const std::string& connector_id : connector_ids) {
    if (!unique_connector_ids.insert(connector_id).second) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    const HomeConnector* granted_connector =
        FindConnector(*manifest, connector_id);
    if (!granted_connector) {
      return base::unexpected(HomeError::kNotFound);
    }
    grant_fingerprints.emplace(
        connector_id, HomeConnectorGrantFingerprint(
                          granted_connector->permissions, manifest->limits));
  }
  const base::FilePath revision_path = RevisionPath(draft_id);
  if (base::PathExists(revision_path)) {
    return base::unexpected(HomeError::kAlreadyExists);
  }
  if (authorization && !authorization->IsValid()) {
    return base::unexpected(HomeError::kCancelled);
  }
  std::vector<std::pair<base::FilePath, base::FilePath>> imported_moves;
  for (const auto& [version, temporary_path] :
       found->second.imported_revisions) {
    const base::FilePath destination = RevisionPath(version.id);
    if (base::PathExists(destination) ||
        !base::Move(temporary_path, destination)) {
      for (auto moved = imported_moves.rbegin(); moved != imported_moves.rend();
           ++moved) {
        base::Move(moved->second, moved->first);
      }
      return base::unexpected(base::PathExists(destination)
                                  ? HomeError::kAlreadyExists
                                  : HomeError::kIoError);
    }
    imported_moves.emplace_back(temporary_path, destination);
  }
  if (!base::Move(found->second.path, revision_path)) {
    for (auto moved = imported_moves.rbegin(); moved != imported_moves.rend();
         ++moved) {
      base::Move(moved->second, moved->first);
    }
    return base::unexpected(HomeError::kIoError);
  }

  const std::string previous_revision = current_revision_;
  const std::optional<HomeManifest> previous_manifest = current_manifest_;
  const auto previous_grants = grants_;
  const size_t previous_version_count = versions_.size();
  for (const auto& imported : found->second.imported_revisions) {
    versions_.push_back(imported.first);
  }
  HomeVersion version;
  version.id = draft_id;
  version.parent = current_revision_;
  version.restored_from = found->second.restored_from;
  version.summary = found->second.draft.summary;
  version.kind = kind;
  version.created_at_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  version.changed_files = found->second.changed_files;
  versions_.push_back(version);
  current_revision_ = version.id;
  const HomeManifest next_manifest = manifest.value();
  if (kind == HomeRevisionKind::kImport) {
    grants_.clear();
  } else if (previous_manifest &&
             next_manifest.limits.max_result_bytes <=
                 previous_manifest->limits.max_result_bytes &&
             next_manifest.limits.max_items_per_connector <=
                 previous_manifest->limits.max_items_per_connector) {
    for (const HomeConnector& connector : next_manifest.connectors) {
      const HomeConnector* before =
          FindConnector(*previous_manifest, connector.id);
      const auto grant = grants_.find(connector.id);
      if (before && grant != grants_.end() &&
          HomeConnectorPermissionFingerprint(before->permissions) ==
              HomeConnectorPermissionFingerprint(connector.permissions) &&
          grant->second ==
              HomeConnectorGrantFingerprint(before->permissions,
                                            previous_manifest->limits)) {
        grant->second = HomeConnectorGrantFingerprint(connector.permissions,
                                                      next_manifest.limits);
      }
    }
  }
  for (const auto& [connector_id, fingerprint] : grant_fingerprints) {
    grants_[connector_id] = fingerprint;
  }
  current_manifest_ = next_manifest;
  auto restore_unpublished_state = [&]() {
    current_revision_ = previous_revision;
    current_manifest_ = previous_manifest;
    grants_ = previous_grants;
    versions_.resize(previous_version_count);
    base::Move(revision_path, found->second.path);
    for (auto moved = imported_moves.rbegin(); moved != imported_moves.rend();
         ++moved) {
      base::Move(moved->second, moved->first);
    }
  };
  std::unique_ptr<DaoHomeMutationLease::CommitGuard> commit_guard;
  if (authorization) {
    commit_guard =
        std::make_unique<DaoHomeMutationLease::CommitGuard>(authorization);
  }
  if (commit_guard && !commit_guard->is_valid()) {
    restore_unpublished_state();
    return base::unexpected(HomeError::kCancelled);
  }
  auto persisted = PersistState();
  if (!persisted.has_value()) {
    restore_unpublished_state();
    return base::unexpected(persisted.error());
  }
  drafts_.erase(found);
  return version;
}

base::expected<HomeVersion, HomeError> DaoHomeProjectStore::Rollback(
    const std::string& base_revision,
    const std::string& target_revision,
    const std::string& summary,
    scoped_refptr<DaoHomeMutationLease> authorization) {
  if (authorization && !authorization->IsValid()) {
    return base::unexpected(HomeError::kCancelled);
  }
  if (base_revision != current_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  if (!HasVersion(target_revision)) {
    return base::unexpected(HomeError::kNotFound);
  }
  const std::string draft_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const base::FilePath draft_path = temporary_root_.AppendASCII(draft_id);
  if (!base::CopyDirectory(RevisionPath(target_revision), draft_path, true)) {
    return base::unexpected(HomeError::kIoError);
  }
  auto files = ListFiles(target_revision);
  if (!files.has_value()) {
    base::DeletePathRecursively(draft_path);
    return base::unexpected(files.error());
  }
  auto draft = RegisterDraft(base_revision, summary, draft_path,
                             std::move(files.value()), target_revision);
  if (!draft.has_value()) {
    return base::unexpected(draft.error());
  }
  if (before_rollback_publish_for_testing_) {
    std::move(before_rollback_publish_for_testing_).Run();
  }
  auto published =
      Publish(draft->id, HomeRevisionKind::kRollback, std::move(authorization));
  if (!published.has_value()) {
    auto discarded = DiscardDraft(draft->id);
    if (!discarded.has_value()) {
      return base::unexpected(discarded.error());
    }
  }
  return published;
}

base::expected<void, HomeError> DaoHomeProjectStore::Reset(
    const std::string& base_revision,
    scoped_refptr<DaoHomeMutationLease> authorization) {
  if (authorization && !authorization->IsValid()) {
    return base::unexpected(HomeError::kCancelled);
  }
  if (base_revision != current_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  if (current_revision_.empty()) {
    return base::unexpected(HomeError::kNotFound);
  }

  const std::string backup_name =
      root_.BaseName().AsUTF8Unsafe() + ".reset-" +
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const base::FilePath backup_root = root_.DirName().AppendASCII(backup_name);
  if (!base::Move(root_, backup_root)) {
    return base::unexpected(HomeError::kIoError);
  }

  const std::string previous_revision = current_revision_;
  const std::vector<HomeVersion> previous_versions = versions_;
  const std::map<std::string, std::string> previous_grants = grants_;
  const std::map<std::string, DraftState> previous_drafts = drafts_;
  const std::optional<HomeManifest> previous_manifest = current_manifest_;
  const base::FilePath retained_root =
      temporary_root_.AppendASCII("reset-data");
  bool retained_inside_new_root = false;
  auto restore = [&]() {
    if (retained_inside_new_root) {
      base::Move(retained_root, backup_root);
    }
    base::DeletePathRecursively(root_);
    const bool restored = base::Move(backup_root, root_);
    current_revision_ = previous_revision;
    versions_ = previous_versions;
    grants_ = previous_grants;
    drafts_ = previous_drafts;
    current_manifest_ = previous_manifest;
    return restored;
  };

  current_revision_.clear();
  versions_.clear();
  grants_.clear();
  drafts_.clear();
  current_manifest_.reset();
  if (!base::CreateDirectory(revisions_root_) ||
      !base::CreateDirectory(temporary_root_) ||
      !base::Move(backup_root, retained_root)) {
    restore();
    return base::unexpected(HomeError::kIoError);
  }
  retained_inside_new_root = true;

  std::unique_ptr<DaoHomeMutationLease::CommitGuard> commit_guard;
  if (authorization) {
    commit_guard =
        std::make_unique<DaoHomeMutationLease::CommitGuard>(authorization);
  }
  if (commit_guard && !commit_guard->is_valid()) {
    restore();
    return base::unexpected(HomeError::kCancelled);
  }
  auto persisted = PersistState();
  if (!persisted.has_value()) {
    restore();
    return base::unexpected(persisted.error());
  }

  // The reset is committed once the empty state is persisted. Keeping the old
  // tree under .tmp until this point makes a failed reset fully recoverable.
  // A failed best-effort deletion is removed by Initialize() on next launch.
  base::DeletePathRecursively(retained_root);
  return base::ok();
}

base::expected<std::string, HomeError> DaoHomeProjectStore::ExportProject()
    const {
  if (current_revision_.empty()) {
    return base::unexpected(HomeError::kNotFound);
  }
  auto files = ListFiles(current_revision_);
  if (!files.has_value()) {
    return base::unexpected(files.error());
  }
  base::DictValue encoded_files;
  for (const std::string& path : files.value()) {
    auto contents = ReadFile(current_revision_, path);
    if (!contents.has_value()) {
      return base::unexpected(contents.error());
    }
    encoded_files.Set(path, base::Base64Encode(contents.value()));
  }
  base::ListValue versions;
  base::DictValue revision_files;
  int64_t history_bytes = 0;
  for (const HomeVersion& version : versions_) {
    versions.Append(VersionToValue(version));
    auto revision_paths = ListFiles(version.id);
    if (!revision_paths.has_value()) {
      return base::unexpected(revision_paths.error());
    }
    base::DictValue encoded_revision;
    for (const std::string& path : revision_paths.value()) {
      auto contents = ReadFile(version.id, path);
      if (!contents.has_value()) {
        return base::unexpected(contents.error());
      }
      history_bytes += contents->size();
      if (history_bytes > kMaxHistoryBytes) {
        return base::unexpected(HomeError::kQuotaExceeded);
      }
      encoded_revision.Set(path, base::Base64Encode(contents.value()));
    }
    revision_files.Set(version.id, std::move(encoded_revision));
  }
  base::DictValue package;
  package.Set("kind", kPackageKind);
  package.Set("format_version", 1);
  package.Set("exported_revision", current_revision_);
  package.Set("encoding", "base64");
  package.Set(
      "readme",
      "# Dao Home project\n\nThis project runs inside Dao Browser. The "
      "generated page is inspectable without Dao, but live `dao.sources`, "
      "`dao.session`, `dao.navigation`, and `dao.media` capabilities require "
      "the Dao Home runtime. Source approvals, credentials, live collected "
      "content, and Agent data are intentionally excluded from this export.\n");
  package.Set("files", std::move(encoded_files));
  package.Set("versions", std::move(versions));
  package.Set("revision_files", std::move(revision_files));
  std::string serialized;
  if (!base::JSONWriter::WriteWithOptions(
          package, base::JSONWriter::OPTIONS_PRETTY_PRINT, &serialized)) {
    return base::unexpected(HomeError::kIoError);
  }
  return serialized;
}

base::expected<HomeDraft, HomeError> DaoHomeProjectStore::ImportProject(
    const std::string& base_revision,
    const std::string& package_json,
    const std::string& summary) {
  if (base_revision != current_revision_) {
    return base::unexpected(HomeError::kStaleRevision);
  }
  if (package_json.size() > kMaxHistoryBytes * 2) {
    return base::unexpected(HomeError::kQuotaExceeded);
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(package_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return base::unexpected(HomeError::kInvalidArgument);
  }
  const base::DictValue& package = parsed->GetDict();
  const std::string* kind = package.FindString("kind");
  const std::optional<int> format = package.FindInt("format_version");
  const std::string* encoding = package.FindString("encoding");
  const base::DictValue* files = package.FindDict("files");
  const std::string* exported_revision =
      package.FindString("exported_revision");
  const base::ListValue* package_versions = package.FindList("versions");
  const base::DictValue* revision_files = package.FindDict("revision_files");
  if (!kind || *kind != kPackageKind || !format || *format != 1 || !encoding ||
      *encoding != "base64" || !files || files->empty() ||
      files->size() > kMaxProjectFiles || !exported_revision ||
      !package_versions || package_versions->empty() ||
      package_versions->size() > kMaxImportedVersions || !revision_files ||
      revision_files->size() != package_versions->size()) {
    return base::unexpected(HomeError::kUnsupportedFormat);
  }
  const base::DictValue* exported_files =
      revision_files->FindDict(*exported_revision);
  if (!exported_files || *files != *exported_files) {
    return base::unexpected(HomeError::kUnsupportedFormat);
  }

  const std::string draft_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  const base::FilePath draft_path = temporary_root_.AppendASCII(draft_id);
  if (!base::CreateDirectory(draft_path)) {
    return base::unexpected(HomeError::kIoError);
  }
  std::vector<base::FilePath> temporary_paths = {draft_path};
  auto cleanup = [&]() {
    for (const base::FilePath& path : temporary_paths) {
      base::DeletePathRecursively(path);
    }
  };
  auto decode_files =
      [&](const base::DictValue& encoded_files, const base::FilePath& output,
          int64_t max_bytes, int64_t* aggregate_bytes,
          std::vector<std::string>* paths) -> base::expected<void, HomeError> {
    if (encoded_files.empty() || encoded_files.size() > kMaxProjectFiles ||
        !base::CreateDirectory(output)) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    int64_t project_bytes = 0;
    for (const auto [relative_path, encoded_value] : encoded_files) {
      const std::string* encoded = encoded_value.GetIfString();
      if (!encoded || !IsValidHomeRelativePath(relative_path)) {
        return base::unexpected(HomeError::kInvalidPath);
      }
      std::string decoded;
      if (!base::Base64Decode(*encoded, &decoded)) {
        return base::unexpected(HomeError::kInvalidArgument);
      }
      project_bytes += decoded.size();
      *aggregate_bytes += decoded.size();
      if (project_bytes > kMaxProjectBytes || *aggregate_bytes > max_bytes) {
        return base::unexpected(HomeError::kQuotaExceeded);
      }
      const base::FilePath target = output.AppendASCII(relative_path);
      if (!base::CreateDirectory(target.DirName()) ||
          !base::WriteFile(target, decoded)) {
        return base::unexpected(HomeError::kIoError);
      }
      if (paths) {
        paths->push_back(relative_path);
      }
    }
    return base::ok();
  };

  int64_t current_bytes = 0;
  std::vector<std::string> changed_files;
  auto decoded_current = decode_files(*files, draft_path, kMaxProjectBytes,
                                      &current_bytes, &changed_files);
  if (!decoded_current.has_value()) {
    cleanup();
    return base::unexpected(decoded_current.error());
  }

  std::map<std::string, std::string> remapped_ids;
  std::map<std::string, std::set<std::string>> version_edges;
  std::vector<std::pair<HomeVersion, base::FilePath>> imported_revisions;
  int64_t history_bytes = 0;
  std::string last_version_id;
  for (const base::Value& value : *package_versions) {
    std::optional<HomeVersion> version = VersionFromValue(value);
    if (!version || version->id.empty() || version->id.size() > 128 ||
        remapped_ids.contains(version->id) ||
        (!version->parent.empty() && !remapped_ids.contains(version->parent)) ||
        (!version->restored_from.empty() &&
         !remapped_ids.contains(version->restored_from))) {
      cleanup();
      return base::unexpected(HomeError::kUnsupportedFormat);
    }
    const base::DictValue* encoded_revision =
        revision_files->FindDict(version->id);
    if (!encoded_revision) {
      cleanup();
      return base::unexpected(HomeError::kUnsupportedFormat);
    }
    const std::string old_id = version->id;
    version_edges.try_emplace(old_id);
    for (const std::string* reference :
         {&version->parent, &version->restored_from}) {
      if (!reference->empty()) {
        version_edges[old_id].insert(*reference);
        version_edges[*reference].insert(old_id);
      }
    }
    last_version_id = old_id;
    const std::string new_id =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    const base::FilePath revision_path =
        temporary_root_.AppendASCII("import-" + new_id);
    temporary_paths.push_back(revision_path);
    auto decoded_revision = decode_files(*encoded_revision, revision_path,
                                         kMaxHistoryBytes, &history_bytes,
                                         /*paths=*/nullptr);
    if (!decoded_revision.has_value()) {
      cleanup();
      return base::unexpected(decoded_revision.error());
    }
    auto manifest = ValidateProject(revision_path);
    if (!manifest.has_value()) {
      cleanup();
      return base::unexpected(manifest.error());
    }
    version->id = new_id;
    version->parent =
        version->parent.empty() ? "" : remapped_ids.at(version->parent);
    version->restored_from = version->restored_from.empty()
                                 ? ""
                                 : remapped_ids.at(version->restored_from);
    remapped_ids.emplace(old_id, new_id);
    imported_revisions.emplace_back(std::move(*version), revision_path);
  }
  const auto exported = remapped_ids.find(*exported_revision);
  if (exported == remapped_ids.end() || *exported_revision != last_version_id) {
    cleanup();
    return base::unexpected(HomeError::kUnsupportedFormat);
  }
  std::set<std::string> pending = {*exported_revision};
  std::set<std::string> reachable;
  while (!pending.empty()) {
    const std::string version_id = *pending.begin();
    pending.erase(pending.begin());
    if (!reachable.insert(version_id).second) {
      continue;
    }
    for (const std::string& neighbor : version_edges[version_id]) {
      if (!reachable.contains(neighbor)) {
        pending.insert(neighbor);
      }
    }
  }
  if (reachable.size() != remapped_ids.size()) {
    cleanup();
    return base::unexpected(HomeError::kUnsupportedFormat);
  }

  auto draft = RegisterDraft(base_revision, summary, draft_path,
                             std::move(changed_files), exported->second);
  if (!draft.has_value()) {
    for (size_t index = 1; index < temporary_paths.size(); ++index) {
      base::DeletePathRecursively(temporary_paths[index]);
    }
    return base::unexpected(draft.error());
  }
  drafts_.at(draft->id).imported_revisions = std::move(imported_revisions);
  return draft;
}

base::expected<void, HomeError> DaoHomeProjectStore::DiscardDraft(
    const std::string& draft_id) {
  auto found = drafts_.find(draft_id);
  if (found == drafts_.end()) {
    return base::unexpected(HomeError::kInvalidDraft);
  }
  bool deleted = base::DeletePathRecursively(found->second.path);
  for (const auto& imported : found->second.imported_revisions) {
    deleted = base::DeletePathRecursively(imported.second) && deleted;
  }
  drafts_.erase(found);
  if (!deleted) {
    return base::unexpected(HomeError::kIoError);
  }
  return base::ok();
}

const HomeConnector* DaoHomeProjectStore::FindConnector(
    const HomeManifest& manifest,
    const std::string& connector_id) const {
  auto found = std::ranges::find_if(manifest.connectors,
                                    [&](const HomeConnector& connector) {
                                      return connector.id == connector_id;
                                    });
  return found == manifest.connectors.end() ? nullptr : &*found;
}

base::expected<void, HomeError> DaoHomeProjectStore::GrantConnector(
    const std::string& connector_id) {
  if (!current_manifest_) {
    return base::unexpected(HomeError::kNotFound);
  }
  const HomeConnector* connector =
      FindConnector(*current_manifest_, connector_id);
  if (!connector) {
    return base::unexpected(HomeError::kNotFound);
  }
  const auto previous = grants_;
  grants_[connector_id] = HomeConnectorGrantFingerprint(
      connector->permissions, current_manifest_->limits);
  auto persisted = PersistState();
  if (!persisted.has_value()) {
    grants_ = previous;
    return base::unexpected(persisted.error());
  }
  return base::ok();
}

base::expected<void, HomeError> DaoHomeProjectStore::RevokeConnector(
    const std::string& connector_id) {
  const auto previous = grants_;
  grants_.erase(connector_id);
  auto persisted = PersistState();
  if (!persisted.has_value()) {
    grants_ = previous;
    return base::unexpected(persisted.error());
  }
  return base::ok();
}

bool DaoHomeProjectStore::HasGrant(const std::string& connector_id) const {
  if (!current_manifest_) {
    return false;
  }
  const HomeConnector* connector =
      FindConnector(*current_manifest_, connector_id);
  const auto found = grants_.find(connector_id);
  return connector && found != grants_.end() &&
         found->second ==
             HomeConnectorGrantFingerprint(connector->permissions,
                                           current_manifest_->limits);
}

}  // namespace dao
