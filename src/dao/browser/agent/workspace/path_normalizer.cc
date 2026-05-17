// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/path_normalizer.h"

#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace dao {

namespace {

constexpr std::string_view kAllowedHidden = ".audit.log";

bool IsHiddenComponent(const base::FilePath::StringType& component) {
  return !component.empty() && component[0] == FILE_PATH_LITERAL('.');
}

bool LooksAbsolute(const std::string& rel_path) {
  if (rel_path.empty()) {
    return false;
  }
  if (rel_path.front() == '/') {
    return true;
  }
  if (rel_path.size() >= 2 && rel_path[1] == ':') {
    return true;
  }
  if (rel_path.size() >= 2 && rel_path[0] == '\\' && rel_path[1] == '\\') {
    return true;
  }
  return false;
}

}  // namespace

base::expected<base::FilePath, WorkspaceError> NormalizePath(
    const base::FilePath& workspace_root,
    const std::string& rel_path) {
  if (rel_path.empty()) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }
  if (LooksAbsolute(rel_path)) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }

  base::FilePath candidate(rel_path);
  if (candidate.IsAbsolute()) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }

  std::vector<base::FilePath::StringType> components =
      candidate.GetComponents();
  for (size_t i = 0; i < components.size(); ++i) {
    const auto& c = components[i];
    if (c == FILE_PATH_LITERAL("..") || c == FILE_PATH_LITERAL(".")) {
      return base::unexpected(WorkspaceError::kInvalidPath);
    }
    if (IsHiddenComponent(c)) {
      const bool is_top_level = (i == 0 && components.size() == 1);
      const bool is_allowlisted_audit =
          is_top_level &&
          c == base::FilePath::StringType(kAllowedHidden.begin(),
                                          kAllowedHidden.end());
      if (!is_allowlisted_audit) {
        return base::unexpected(WorkspaceError::kInvalidPath);
      }
    }
  }

  // Resolve symlinks on both sides so prefix checks work consistently on
  // platforms where the temp dir / profile dir contains symlinks (e.g. on
  // macOS where /var/folders is a symlink to /private/var/folders).
  base::FilePath resolved_root = base::MakeAbsoluteFilePath(workspace_root);
  if (resolved_root.empty()) {
    resolved_root = workspace_root;
  }

  base::FilePath joined = resolved_root.Append(candidate);

  base::FilePath resolved = base::MakeAbsoluteFilePath(joined);
  if (resolved.empty()) {
    resolved = joined;
  }

  if (resolved != resolved_root && !resolved_root.IsParent(resolved)) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }

  return resolved;
}

}  // namespace dao
