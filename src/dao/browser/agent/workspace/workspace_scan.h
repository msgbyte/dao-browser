// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_SCAN_H_
#define DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_SCAN_H_

#include "base/files/file.h"
#include "base/functional/function_ref.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"

namespace dao {

enum class WorkspaceScanScope { kVisible, kQuota };

void RecoverWorkspacePublications(const base::FilePath& root);

bool IsWorkspaceQuotaExemptFile(const base::FilePath& relative);

// Visits regular files and directories. Paths are relative to the workspace
// root. Quota scans also count hidden user files.
WorkspaceError ScanWorkspace(
    const base::FilePath& root,
    const base::FilePath& directory,
    bool recursive,
    WorkspaceScanScope scope,
    base::FunctionRef<void(const base::FilePath&, const base::File::Info&)>
        visit);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_SCAN_H_
