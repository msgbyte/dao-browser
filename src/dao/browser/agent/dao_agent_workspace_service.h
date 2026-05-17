// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_SERVICE_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/types/expected.h"
#include "components/keyed_service/core/keyed_service.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"

namespace dao {

class WorkspaceQuota;

// Profile-keyed service owning <Profile>/DaoAgentWorkspace/.
// All disk IO posts to `io_runner_`; callbacks reply on the calling thread.
class DaoAgentWorkspaceService : public KeyedService {
 public:
  using ReadCallback =
      base::OnceCallback<void(base::expected<ReadResult, WorkspaceError>)>;
  using WriteCallback =
      base::OnceCallback<void(base::expected<WriteResult, WorkspaceError>)>;
  using PatchCallback =
      base::OnceCallback<void(base::expected<PatchResult, WorkspaceError>)>;

  explicit DaoAgentWorkspaceService(const base::FilePath& profile_path);
  ~DaoAgentWorkspaceService() override;

  DaoAgentWorkspaceService(const DaoAgentWorkspaceService&) = delete;
  DaoAgentWorkspaceService& operator=(const DaoAgentWorkspaceService&) = delete;

  // KeyedService:
  void Shutdown() override;

  void Read(const std::string& rel_path,
            int offset_lines,
            int limit_lines,
            ReadCallback callback);
  void Write(const std::string& rel_path,
             const std::string& content,
             WriteCallback callback);

  // For the settings activity list. Snapshot is taken on the UI thread.
  std::vector<AuditEntry> GetRecentAudit() const;

  const base::FilePath& workspace_root() const { return workspace_root_; }

 private:
  // Helpers that run on io_runner_.
  base::expected<ReadResult, WorkspaceError> ReadOnIO(
      const std::string& rel_path,
      int offset,
      int limit);
  base::expected<WriteResult, WorkspaceError> WriteOnIO(
      const std::string& rel_path,
      const std::string& content);

  void EnsureRootExistsOnIO();
  void ClearStagingOnIO();

  base::FilePath workspace_root_;
  scoped_refptr<base::SequencedTaskRunner> io_runner_;

  std::unique_ptr<WorkspaceQuota> quota_;  // io_runner_-only

  base::WeakPtrFactory<DaoAgentWorkspaceService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_SERVICE_H_
