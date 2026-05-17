// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_INDEX_H_
#define DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_INDEX_H_

#include "base/files/file_path.h"

namespace dao {

// Rewrites <workspace_root>/WORKSPACE.md to reflect the current contents
// of the workspace. Skips bookkeeping files (.audit.log, WORKSPACE.md
// itself) and the .workspace_tmp staging dir.
class WorkspaceIndex {
 public:
  explicit WorkspaceIndex(const base::FilePath& workspace_root);
  ~WorkspaceIndex();

  WorkspaceIndex(const WorkspaceIndex&) = delete;
  WorkspaceIndex& operator=(const WorkspaceIndex&) = delete;

  void Rewrite();

 private:
  base::FilePath workspace_root_;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_INDEX_H_
