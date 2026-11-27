// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_SERVICE_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_SERVICE_H_

#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/threading/sequence_bound.h"
#include "base/types/expected.h"
#include "components/keyed_service/core/keyed_service.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"

namespace storage {
class ScopedFile;
}

namespace dao {

// Profile-keyed service owning <Profile>/DaoAgentWorkspace/.
// Disk IO runs on a dedicated sequence; callbacks reply on the calling thread.
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

  // Takes ownership of a network-created temporary file, checks the text
  // policy and quota, and atomically publishes its contents at `rel_path`.
  void IngestDownloadedFile(const std::string& rel_path,
                            storage::ScopedFile download,
                            const std::string& audit_detail,
                            WriteCallback callback);

  // Lists entries under `rel_path` (relative to workspace root; "" is
  // the root itself). `recursive=true` walks every subdirectory; false
  // returns just the immediate children. The staging directory and any
  // path starting with '.' are excluded — they are internal book-
  // keeping, not workspace state the agent should see.
  void List(const std::string& rel_path, bool recursive, ListCallback callback);

  // For the settings activity list. Snapshot is taken on the UI thread.
  std::vector<AuditEntry> GetRecentAudit() const;

  // Async variant: snapshots the audit ring on the I/O sequence and replies on
  // the calling sequence.
  void GetRecentAuditAsync(
      base::OnceCallback<void(std::vector<AuditEntry>)> callback);

  // Snapshot of workspace usage information for the settings panel. Bytes
  // are exposed to the WebUI as JS numbers.
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
  class IOState;
  base::FilePath workspace_root_;
  base::SequenceBound<IOState> io_;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_SERVICE_H_
