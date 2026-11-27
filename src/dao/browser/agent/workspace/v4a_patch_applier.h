// Copyright 2026 Dao Browser Authors. All rights reserved.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_V4A_PATCH_APPLIER_H_
#define DAO_BROWSER_AGENT_WORKSPACE_V4A_PATCH_APPLIER_H_

#include "base/files/file_path.h"
#include "base/types/expected.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"
#include "dao/browser/agent/workspace/v4a_patch_parser.h"

namespace dao {

class WorkspaceQuota;

// Validates and stages the entire patch before changing workspace files.
// A commit-time I/O error can leave earlier writes or deletes in place.
base::expected<PatchResult, WorkspaceError> ApplyV4APatch(
    const base::FilePath& workspace_root,
    WorkspaceQuota* quota,
    const V4APatch& patch);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_V4A_PATCH_APPLIER_H_
