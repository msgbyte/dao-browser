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

// Returns an absolute FilePath under `workspace_root` for `rel_path`, or
// WorkspaceError::kInvalidPath if the input is unsafe.
//
// Rejects: absolute paths (POSIX and Windows), any ".." segment, any
// component starting with "." (except ".audit.log" at the top level),
// empty input, paths whose resolved form escapes `workspace_root` via
// symlinks.
//
// `workspace_root` must already exist and be canonicalized. Symlink
// resolution is performed on the resolved candidate and the result must
// live inside `workspace_root`.
base::expected<base::FilePath, WorkspaceError> NormalizePath(
    const base::FilePath& workspace_root,
    const std::string& rel_path);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_PATH_NORMALIZER_H_
