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

class WorkspaceAudit;
class WorkspaceIndex;
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
  using ListCallback =
      base::OnceCallback<void(base::expected<ListResult, WorkspaceError>)>;

  // Hard cap on entries returned by a single List() call. Matches the
  // workspace's own entry quota — listing more would never reflect a
  // legal state of the workspace.
  static constexpr size_t kListMaxEntries = 1000;

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
  void Edit(const std::string& rel_path,
            const std::string& old_str,
            const std::string& new_str,
            WriteCallback callback);
  void ApplyPatch(const std::string& patch_text, PatchCallback callback);

  // Returns an absolute path inside the workspace's staging directory
  // that a network stack can write to (e.g. SimpleURLLoader::DownloadToFile).
  // The path is unique per call and lives on the same filesystem as the
  // workspace root so the follow-up rename in IngestStagedFile is atomic.
  // Returns an empty path if staging-dir creation fails. The caller is
  // responsible for deleting the staged file if it never reaches
  // IngestStagedFile.
  void AllocateStagingPath(
      base::OnceCallback<void(base::FilePath)> callback);

  // Validates `staged_abs_path` (text-only filter, workspace quota) and
  // atomically renames it into the workspace under `rel_path`. On failure
  // the staged file is deleted. On success the file becomes a normal
  // workspace entry and an audit row is appended with op="download".
  void IngestStagedFile(const std::string& rel_path,
                        const base::FilePath& staged_abs_path,
                        const std::string& audit_detail,
                        WriteCallback callback);

  // Lists entries under `rel_path` (relative to workspace root; "" is
  // the root itself). `recursive=true` walks every subdirectory; false
  // returns just the immediate children. The staging directory and any
  // path starting with '.' are excluded — they are internal book-
  // keeping, not workspace state the agent should see.
  void List(const std::string& rel_path,
            bool recursive,
            ListCallback callback);

  // For the settings activity list. Snapshot is taken on the UI thread.
  std::vector<AuditEntry> GetRecentAudit() const;

  // Async variant: snapshots the audit ring on the io_runner_ and replies on
  // the calling sequence.
  void GetRecentAuditAsync(
      base::OnceCallback<void(std::vector<AuditEntry>)> callback);

  // Snapshot of workspace usage information for the settings panel. Bytes
  // are uint64 because they are exposed to the WebUI as JS numbers (≤100MB
  // fits comfortably below 2^53).
  struct UsageSnapshot {
    base::FilePath root;
    uint64_t used_bytes = 0;
    uint64_t cap_bytes = 0;
    uint32_t file_count = 0;
    uint32_t file_count_cap = 0;
  };
  void GetUsageInfo(base::OnceCallback<void(UsageSnapshot)> callback);

  // Opens the workspace root in the platform's file manager. Must be called
  // on the UI thread.
  void OpenInFileManager();

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
  base::expected<WriteResult, WorkspaceError> EditOnIO(
      const std::string& rel_path,
      const std::string& old_str,
      const std::string& new_str);
  base::expected<PatchResult, WorkspaceError> ApplyPatchOnIO(
      const std::string& patch_text);
  base::FilePath AllocateStagingPathOnIO();
  base::expected<WriteResult, WorkspaceError> IngestStagedFileOnIO(
      const std::string& rel_path,
      const base::FilePath& staged_abs_path,
      const std::string& audit_detail);
  base::expected<ListResult, WorkspaceError> ListOnIO(
      const std::string& rel_path,
      bool recursive);

  void EnsureRootExistsOnIO();
  void ClearStagingOnIO();

  base::FilePath workspace_root_;
  scoped_refptr<base::SequencedTaskRunner> io_runner_;

  std::unique_ptr<WorkspaceQuota> quota_;  // io_runner_-only
  std::unique_ptr<WorkspaceAudit> audit_;  // io_runner_-only
  std::unique_ptr<WorkspaceIndex> index_;  // io_runner_-only

  base::WeakPtrFactory<DaoAgentWorkspaceService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_SERVICE_H_
