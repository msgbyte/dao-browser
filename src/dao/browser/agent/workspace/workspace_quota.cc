// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_quota.h"

#include <vector>

#include "base/files/file_enumerator.h"

namespace dao {

namespace {

bool IsBookkeepingFile(const base::FilePath::StringType& name) {
  return name == FILE_PATH_LITERAL(".audit.log") ||
         name == FILE_PATH_LITERAL("WORKSPACE.md");
}

bool IsStagingDir(const base::FilePath::StringType& name) {
  return name == FILE_PATH_LITERAL(".workspace_tmp");
}

}  // namespace

WorkspaceQuota::WorkspaceQuota(const base::FilePath& workspace_root)
    : workspace_root_(workspace_root) {}

WorkspaceQuota::~WorkspaceQuota() = default;

WorkspaceQuota::Usage WorkspaceQuota::GetUsage() {
  if (!cached_usage_.has_value()) {
    cached_usage_ = ComputeUsage();
  }
  return *cached_usage_;
}

void WorkspaceQuota::InvalidateCache() {
  cached_usage_.reset();
}

bool WorkspaceQuota::CanAcceptWrite(
    [[maybe_unused]] const std::string& rel_path,
    uint64_t new_bytes,
    uint64_t replacing_existing_bytes) {
  if (new_bytes > kPerFileMaxBytes) {
    return false;
  }
  Usage u = GetUsage();
  const int64_t delta = static_cast<int64_t>(new_bytes) -
                        static_cast<int64_t>(replacing_existing_bytes);
  if (static_cast<int64_t>(u.total_bytes) + delta >
      static_cast<int64_t>(kTotalMaxBytes)) {
    return false;
  }
  // Entry count only grows for new files.
  if (replacing_existing_bytes == 0 && u.entry_count >= kMaxEntries) {
    return false;
  }
  return true;
}

WorkspaceQuota::Usage WorkspaceQuota::ComputeUsage() const {
  Usage u;
  base::FileEnumerator enumerator(workspace_root_, /*recursive=*/true,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    base::FilePath::StringType name = path.BaseName().value();
    if (IsBookkeepingFile(name)) {
      continue;
    }
    // Skip anything under .workspace_tmp.
    base::FilePath relative;
    if (workspace_root_.AppendRelativePath(path, &relative)) {
      std::vector<base::FilePath::StringType> comps = relative.GetComponents();
      if (!comps.empty() && IsStagingDir(comps[0])) {
        continue;
      }
    }
    base::FileEnumerator::FileInfo info = enumerator.GetInfo();
    u.total_bytes += static_cast<uint64_t>(info.GetSize());
    u.entry_count += 1;
  }
  return u;
}

}  // namespace dao
