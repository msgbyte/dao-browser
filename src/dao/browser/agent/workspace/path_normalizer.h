// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_PATH_NORMALIZER_H_
#define DAO_BROWSER_AGENT_WORKSPACE_PATH_NORMALIZER_H_

#include <string>

#include "base/files/file_path.h"
#include "base/types/expected.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"

namespace dao {

// Resolves in-workspace links and returns the canonical workspace-relative
// name. File operations must reopen its components without following links.
base::expected<base::FilePath, WorkspaceError> ResolveWorkspacePath(
    const base::FilePath& workspace_root,
    const std::string& rel_path);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_PATH_NORMALIZER_H_
