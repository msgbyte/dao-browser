// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_AUDIT_H_
#define DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_AUDIT_H_

#include <cstddef>
#include <vector>

#include "base/containers/circular_deque.h"
#include "base/files/file_path.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"

namespace dao {

// Append-only JSON audit log for workspace mutations, paired with an
// in-memory ring buffer of the most recent entries. All methods must be
// called from the same sequence (typically the workspace IO runner).
class WorkspaceAudit {
 public:
  WorkspaceAudit(const base::FilePath& workspace_root,
                 size_t ring_buffer_size);
  ~WorkspaceAudit();

  WorkspaceAudit(const WorkspaceAudit&) = delete;
  WorkspaceAudit& operator=(const WorkspaceAudit&) = delete;

  // Appends `entry` to <workspace_root>/.audit.log as one JSON line, and
  // pushes it onto the ring buffer (dropping the oldest if full).
  void Append(const AuditEntry& entry);

  // Returns a copy of the ring buffer, oldest first.
  std::vector<AuditEntry> Snapshot() const;

 private:
  base::FilePath log_path_;
  base::circular_deque<AuditEntry> recent_;
  size_t ring_size_;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_WORKSPACE_AUDIT_H_
