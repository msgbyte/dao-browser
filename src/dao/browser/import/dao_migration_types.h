// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_MIGRATION_TYPES_H_
#define DAO_BROWSER_IMPORT_DAO_MIGRATION_TYPES_H_

#include <cstdint>
#include <string>
#include <vector>

namespace dao::import {

enum class SourceKind {
  kChrome,
  kArc,
  kSafari,
  kEdge,
  kFirefox,
};

enum class DataCategory {
  kBookmarks,
  kHistory,
  kPasswords,
  kTabs,
  kExtensions,
};

enum class CategoryPhase {
  kPending,
  kSnapshotting,
  kReading,
  kWriting,
  kSucceeded,
  kFailed,
  kCancelled,
};

struct SourceProfile {
  SourceProfile();
  SourceProfile(const SourceProfile&);
  SourceProfile& operator=(const SourceProfile&);
  SourceProfile(SourceProfile&&);
  SourceProfile& operator=(SourceProfile&&);
  ~SourceProfile();

  std::string id;
  SourceKind kind = SourceKind::kChrome;
  std::string browser_name;
  std::string profile_name;
  std::vector<DataCategory> supported_categories;
};

struct CategoryResult {
  uint64_t imported = 0;
  uint64_t skipped = 0;
  uint64_t conflicted = 0;
  uint64_t failed = 0;
};

struct CategoryState {
  CategoryPhase phase = CategoryPhase::kPending;
  CategoryResult result;
  std::string error_code;
  uint64_t completed_items = 0;
  uint64_t total_items = 0;
  bool indeterminate = true;
};

struct JobState {
  JobState();
  JobState(const JobState&);
  JobState& operator=(const JobState&);
  JobState(JobState&&);
  JobState& operator=(JobState&&);
  ~JobState();

  std::string source_id;
  bool cancel_requested = false;
  std::vector<DataCategory> selected_categories;
  std::vector<CategoryState> category_states;
};

const char* SourceKindToString(SourceKind kind);
const char* DataCategoryToString(DataCategory category);
const char* CategoryPhaseToString(CategoryPhase phase);

bool IsTerminalPhase(CategoryPhase phase);

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_MIGRATION_TYPES_H_
