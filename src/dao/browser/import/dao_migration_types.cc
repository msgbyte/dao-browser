// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_migration_types.h"

#include "base/notreached.h"

namespace dao::import {

SourceProfile::SourceProfile() = default;
SourceProfile::SourceProfile(const SourceProfile&) = default;
SourceProfile& SourceProfile::operator=(const SourceProfile&) = default;
SourceProfile::SourceProfile(SourceProfile&&) = default;
SourceProfile& SourceProfile::operator=(SourceProfile&&) = default;
SourceProfile::~SourceProfile() = default;

JobState::JobState() = default;
JobState::JobState(const JobState&) = default;
JobState& JobState::operator=(const JobState&) = default;
JobState::JobState(JobState&&) = default;
JobState& JobState::operator=(JobState&&) = default;
JobState::~JobState() = default;

const char* SourceKindToString(SourceKind kind) {
  switch (kind) {
    case SourceKind::kChrome:
      return "chrome";
    case SourceKind::kArc:
      return "arc";
    case SourceKind::kSafari:
      return "safari";
    case SourceKind::kEdge:
      return "edge";
    case SourceKind::kFirefox:
      return "firefox";
  }
  NOTREACHED();
}

const char* DataCategoryToString(DataCategory category) {
  switch (category) {
    case DataCategory::kBookmarks:
      return "bookmarks";
    case DataCategory::kHistory:
      return "history";
    case DataCategory::kPasswords:
      return "passwords";
    case DataCategory::kTabs:
      return "tabs";
    case DataCategory::kExtensions:
      return "extensions";
  }
  NOTREACHED();
}

const char* CategoryPhaseToString(CategoryPhase phase) {
  switch (phase) {
    case CategoryPhase::kPending:
      return "pending";
    case CategoryPhase::kSnapshotting:
      return "snapshotting";
    case CategoryPhase::kReading:
      return "reading";
    case CategoryPhase::kWriting:
      return "writing";
    case CategoryPhase::kSucceeded:
      return "succeeded";
    case CategoryPhase::kFailed:
      return "failed";
    case CategoryPhase::kCancelled:
      return "cancelled";
  }
  NOTREACHED();
}

bool IsTerminalPhase(CategoryPhase phase) {
  return phase == CategoryPhase::kSucceeded ||
         phase == CategoryPhase::kFailed || phase == CategoryPhase::kCancelled;
}

}  // namespace dao::import
