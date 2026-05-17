// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_QUOTA_H_
#define DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_QUOTA_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "base/files/file_path.h"

namespace dao {

class WorkspaceQuota {
 public:
  // Per-file cap matches the total cap so users effectively only see
  // the workspace-wide budget, not a separate single-file ceiling.
  static constexpr size_t kPerFileMaxBytes = 500 * 1024 * 1024;  // 500 MB
  static constexpr size_t kTotalMaxBytes = 500 * 1024 * 1024;    // 500 MB
  static constexpr size_t kMaxEntries = 500;

  struct Usage {
    uint64_t total_bytes = 0;
    uint32_t entry_count = 0;
  };

  explicit WorkspaceQuota(const base::FilePath& workspace_root);
  ~WorkspaceQuota();

  WorkspaceQuota(const WorkspaceQuota&) = delete;
  WorkspaceQuota& operator=(const WorkspaceQuota&) = delete;

  // Cached; recomputes on first call after construction or InvalidateCache().
  Usage GetUsage();

  // Returns true if writing `rel_path` of `new_bytes` is within all three
  // limits. `replacing_existing_bytes` is the on-disk size of the file
  // being overwritten (0 for new files).
  bool CanAcceptWrite(const std::string& rel_path,
                      uint64_t new_bytes,
                      uint64_t replacing_existing_bytes);

  void InvalidateCache();

 private:
  Usage ComputeUsage() const;

  base::FilePath workspace_root_;
  std::optional<Usage> cached_usage_;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_QUOTA_H_
