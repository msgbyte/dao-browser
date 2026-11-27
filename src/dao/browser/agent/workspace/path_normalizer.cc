// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/path_normalizer.h"

#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "build/build_config.h"

namespace dao {

namespace {

bool HasPrivateComponent(const base::FilePath& path) {
  const auto parts = path.GetComponents();
  if (parts.size() == 1 && parts.front() == FILE_PATH_LITERAL(".audit.log")) {
    return false;
  }
  for (const auto& part : parts) {
    if (!part.empty() && part.front() == FILE_PATH_LITERAL('.')) {
      return true;
    }
  }
  return false;
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

base::expected<base::FilePath, WorkspaceError> ResolveWorkspacePath(
    const base::FilePath& workspace_root,
    const std::string& rel_path) {
  if (rel_path.empty() || rel_path.find('\0') != std::string::npos) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }
  if (LooksAbsolute(rel_path)) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }

  base::FilePath candidate = base::FilePath::FromUTF8Unsafe(rel_path);
  if (candidate.empty() || candidate.IsAbsolute() ||
      candidate.ReferencesParent() ||
      base::FilePath::IsSeparator(candidate.value().front())) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }

  if (HasPrivateComponent(candidate)) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }
  const auto components = candidate.GetComponents();
  // A new file has no canonical path yet, but its parents may be links.
  base::FilePath resolved_root;
  if (!base::NormalizeFilePath(workspace_root, &resolved_root)) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }
  base::FilePath resolved = resolved_root;
  for (const auto& component : components) {
#if BUILDFLAG(IS_WIN)
    if (component.ends_with(L'.') || component.ends_with(L' ') ||
        component.find(L':') != std::wstring::npos ||
        base::IsReservedNameOnWindows(component)) {
      return base::unexpected(WorkspaceError::kInvalidPath);
    }
#endif
    resolved = resolved.Append(component);
    base::FilePath canonical;
    if (base::NormalizeFilePath(resolved, &canonical)) {
      resolved = canonical;
    } else if (base::PathExists(resolved) || base::IsLink(resolved)) {
      return base::unexpected(WorkspaceError::kInvalidPath);
    }
    if (resolved != resolved_root && !resolved_root.IsParent(resolved)) {
      return base::unexpected(WorkspaceError::kInvalidPath);
    }
  }
  base::FilePath relative;
  if (resolved != resolved_root &&
      !resolved_root.AppendRelativePath(resolved, &relative)) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }
  // A visible alias must not grant access to private workspace components.
  if (HasPrivateComponent(relative)) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }
  return relative;
}

}  // namespace dao
