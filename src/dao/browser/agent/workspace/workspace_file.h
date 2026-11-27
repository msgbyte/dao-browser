// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_FILE_H_
#define DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_FILE_H_

#include <optional>
#include <string_view>

#include "base/files/file.h"
#include "base/types/expected.h"
#include "build/build_config.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"

#if BUILDFLAG(IS_WIN)
#include <vector>
#else
#include "base/files/scoped_file.h"
#endif

namespace dao {

// Pins the directory and rejects links at every step. An empty path names
// the root. Windows needs to retain the whole chain to prevent replacement.
#if BUILDFLAG(IS_WIN)
std::vector<base::File>
#else
base::ScopedFD
#endif
OpenWorkspaceDirectory(const base::FilePath& root,
                       const base::FilePath& relative,
                       bool create = false);

// These operations take resolved relative paths or trusted bookkeeping names.
base::File OpenWorkspaceFile(const base::FilePath& root,
                             const base::FilePath& relative);
base::expected<std::optional<int64_t>, WorkspaceError> WorkspaceFileSize(
    const base::FilePath& root,
    const base::FilePath& relative);
bool WriteWorkspaceFile(const base::FilePath& root,
                        const base::FilePath& relative,
                        std::string_view content);
enum class WorkspacePublishMode { kReplace, kCreate };
base::expected<void, WorkspaceError> CopyToWorkspaceFile(
    const base::FilePath& root,
    const base::FilePath& relative,
    base::File& source,
    WorkspacePublishMode mode);
inline constexpr base::FilePath::CharType kWorkspacePublishDirectory[] =
    FILE_PATH_LITERAL(".dao-publish-tmp");
// The caller enumerates only immediate regular entries in a staging directory.
bool RecoverWorkspacePublication(const base::FilePath& root,
                                 const base::FilePath& relative,
                                 bool legacy);
void RemoveEmptyWorkspacePublishDirectory(const base::FilePath& root,
                                          const base::FilePath& relative);
bool DeleteWorkspaceFile(const base::FilePath& root,
                         const base::FilePath& relative);
bool AppendWorkspaceAudit(const base::FilePath& root, std::string_view line);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_FILE_H_
