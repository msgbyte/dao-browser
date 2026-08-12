// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_source_detector.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "crypto/sha2.h"

namespace dao::import {
namespace {

std::vector<DataCategory> ChromiumCategories() {
  return {
      DataCategory::kBookmarks,  DataCategory::kHistory,
      DataCategory::kPasswords,  DataCategory::kTabs,
      DataCategory::kExtensions,
  };
}

std::string BuildProfileId(SourceKind kind,
                           const base::FilePath& profile_path) {
  const std::string identity = std::string(SourceKindToString(kind)) + ":" +
                               profile_path.StripTrailingSeparators()
                                   .NormalizePathSeparators()
                                   .AsUTF8Unsafe();
  return base::HexEncode(crypto::SHA256HashString(identity));
}

bool ProfileOrder(const SourceProfile& left, const SourceProfile& right) {
  const bool left_is_default =
      left.profile_name == "Default" || left.profile_name == "Personal";
  const bool right_is_default =
      right.profile_name == "Default" || right.profile_name == "Personal";
  if (left_is_default != right_is_default) {
    return left_is_default;
  }
  return left.profile_name < right.profile_name;
}

}  // namespace

DaoSourceDetector::DetectionResult::DetectionResult() = default;
DaoSourceDetector::DetectionResult::DetectionResult(const DetectionResult&) =
    default;
DaoSourceDetector::DetectionResult&
DaoSourceDetector::DetectionResult::operator=(const DetectionResult&) = default;
DaoSourceDetector::DetectionResult::DetectionResult(DetectionResult&&) =
    default;
DaoSourceDetector::DetectionResult&
DaoSourceDetector::DetectionResult::operator=(DetectionResult&&) = default;
DaoSourceDetector::DetectionResult::~DetectionResult() = default;

DaoSourceDetector::DaoSourceDetector() = default;
DaoSourceDetector::~DaoSourceDetector() = default;

void DaoSourceDetector::Detect(DetectCallback callback) {
  const uint64_t generation = ++detection_generation_;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&DaoSourceDetector::DetectFromRoots,
                     GetDefaultBrowserRoots()),
      base::BindOnce(&DaoSourceDetector::OnDetectionComplete,
                     weak_ptr_factory_.GetWeakPtr(), generation,
                     std::move(callback)));
}

std::optional<base::FilePath> DaoSourceDetector::ResolveProfilePath(
    const std::string& profile_id) const {
  auto it = profile_paths_.find(profile_id);
  if (it == profile_paths_.end()) {
    return std::nullopt;
  }
  return it->second;
}

// static
DaoSourceDetector::DetectionResult DaoSourceDetector::DetectFromRootsForTesting(
    std::vector<BrowserRoot> roots) {
  return DetectFromRoots(std::move(roots));
}

// static
std::string DaoSourceDetector::BuildProfileIdForTesting(
    SourceKind kind,
    const base::FilePath& profile_path) {
  return BuildProfileId(kind, profile_path);
}

// static
std::vector<DaoSourceDetector::BrowserRoot>
DaoSourceDetector::GetDefaultBrowserRoots() {
  const base::FilePath application_support = base::GetHomeDir().Append(
      FILE_PATH_LITERAL("Library/Application Support"));
  return {
      {SourceKind::kChrome, "Google Chrome",
       application_support.AppendASCII("Google/Chrome")},
      {SourceKind::kArc, "Arc",
       application_support.AppendASCII("Arc/User Data")},
      {SourceKind::kEdge, "Microsoft Edge",
       application_support.AppendASCII("Microsoft Edge")},
  };
}

// static
DaoSourceDetector::DetectionResult DaoSourceDetector::DetectFromRoots(
    std::vector<BrowserRoot> roots) {
  DetectionResult result;
  for (const BrowserRoot& root : roots) {
    std::string local_state_contents;
    if (!base::ReadFileToString(root.path.AppendASCII("Local State"),
                                &local_state_contents)) {
      continue;
    }

    std::optional<base::DictValue> local_state = base::JSONReader::ReadDict(
        local_state_contents, base::JSON_PARSE_RFC);
    if (!local_state) {
      continue;
    }
    const base::DictValue* info_cache =
        local_state->FindDictByDottedPath("profile.info_cache");
    if (!info_cache) {
      continue;
    }

    std::vector<SourceProfile> root_profiles;
    for (const auto [profile_directory, profile_value] : *info_cache) {
      if (!profile_value.is_dict()) {
        continue;
      }
      const base::FilePath profile_path =
          root.path.Append(base::FilePath::FromUTF8Unsafe(profile_directory));
      if (!base::DirectoryExists(profile_path)) {
        continue;
      }

      SourceProfile profile;
      profile.id = BuildProfileId(root.kind, profile_path);
      profile.kind = root.kind;
      profile.browser_name = root.browser_name;
      const std::string* display_name =
          profile_value.GetDict().FindString("name");
      profile.profile_name = display_name && !display_name->empty()
                                 ? *display_name
                                 : profile_directory;
      profile.supported_categories = ChromiumCategories();
      result.profile_paths.emplace(profile.id, profile_path);
      root_profiles.push_back(std::move(profile));
    }

    std::sort(root_profiles.begin(), root_profiles.end(), ProfileOrder);
    result.profiles.insert(result.profiles.end(),
                           std::make_move_iterator(root_profiles.begin()),
                           std::make_move_iterator(root_profiles.end()));
  }
  return result;
}

void DaoSourceDetector::OnDetectionComplete(uint64_t generation,
                                            DetectCallback callback,
                                            DetectionResult result) {
  if (generation != detection_generation_) {
    return;
  }
  profile_paths_ = std::move(result.profile_paths);
  std::move(callback).Run(std::move(result.profiles));
}

}  // namespace dao::import
