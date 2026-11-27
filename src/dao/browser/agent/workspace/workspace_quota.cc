// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_quota.h"

#include "dao/browser/agent/workspace/workspace_scan.h"

namespace dao {

WorkspaceQuota::WorkspaceQuota(const base::FilePath& workspace_root)
    : workspace_root_(workspace_root) {}

WorkspaceQuota::~WorkspaceQuota() = default;

WorkspaceQuota::Usage WorkspaceQuota::GetUsage() {
  if (!cached_usage_.has_value()) {
    cached_usage_ = ComputeUsage();
  }
  return cached_usage_.value_or(Usage());
}

void WorkspaceQuota::InvalidateCache() {
  cached_usage_.reset();
}

bool WorkspaceQuota::CanAcceptWrite(const base::FilePath& relative,
                                    uint64_t new_bytes,
                                    std::optional<uint64_t> existing_bytes) {
  const FileChange change{relative, existing_bytes, new_bytes};
  return CanApplyChanges(base::span_from_ref(change));
}

bool WorkspaceQuota::CanApplyChanges(base::span<const FileChange> changes) {
  // The user can add files outside Dao after a usage snapshot was displayed.
  cached_usage_ = ComputeUsage();
  if (!cached_usage_) {
    return false;
  }
  Usage kept = *cached_usage_;
  Usage written;
  for (const auto& change : changes) {
    if (change.after_bytes && *change.after_bytes > kPerFileMaxBytes) {
      return false;
    }
    if (IsWorkspaceQuotaExemptFile(change.path)) {
      continue;
    }
    if (change.before_bytes) {
      if (*change.before_bytes > kept.total_bytes || kept.entry_count == 0) {
        return false;
      }
      kept.total_bytes -= *change.before_bytes;
      --kept.entry_count;
    }
    if (change.after_bytes) {
      if (*change.after_bytes > kTotalMaxBytes - written.total_bytes ||
          written.entry_count >= kMaxEntries) {
        return false;
      }
      written.total_bytes += *change.after_bytes;
      ++written.entry_count;
    }
  }
  // Allow deletion to recover a workspace that is already over quota.
  return written.entry_count == 0 ||
         (kept.total_bytes <= kTotalMaxBytes - written.total_bytes &&
          kept.entry_count <= kMaxEntries - written.entry_count);
}

std::optional<WorkspaceQuota::Usage> WorkspaceQuota::ComputeUsage() const {
  Usage u;
  auto status = ScanWorkspace(
      workspace_root_, {}, /*recursive=*/true, WorkspaceScanScope::kQuota,
      [&](const base::FilePath&, const base::File::Info& info) {
        if (!info.is_directory) {
          u.total_bytes += static_cast<uint64_t>(info.size);
          ++u.entry_count;
        }
      });
  if (status != WorkspaceError::kOk) {
    return std::nullopt;
  }
  return u;
}

}  // namespace dao
