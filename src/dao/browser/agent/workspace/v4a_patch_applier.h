// Copyright 2026 Dao Browser Authors. All rights reserved.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_V4A_PATCH_APPLIER_H_
#define DAO_BROWSER_AGENT_WORKSPACE_V4A_PATCH_APPLIER_H_

#include <string>

#include "base/files/file_path.h"
#include "base/types/expected.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"
#include "dao/browser/agent/workspace/v4a_patch_parser.h"

namespace dao {

class WorkspaceQuota;

// Applies a parsed patch against `workspace_root`. All writes go through
// `staging_dir/<request_id>/`; on full success, files are atomically
// renamed into place and `staging_dir/<request_id>/` is cleaned up. On
// any failure, the staging dir is wiped and no target file is touched.
//
// `quota` is consulted before staging. `quota->InvalidateCache()` is
// called on success.
base::expected<PatchResult, WorkspaceError> ApplyV4APatch(
    const base::FilePath& workspace_root,
    const base::FilePath& staging_dir,
    const std::string& request_id,
    WorkspaceQuota* quota,
    const V4APatch& patch);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_V4A_PATCH_APPLIER_H_
