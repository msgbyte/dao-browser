// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_workspace_service.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "dao/browser/agent/workspace/path_normalizer.h"
#include "dao/browser/agent/workspace/text_only_filter.h"
#include "dao/browser/agent/workspace/workspace_quota.h"

namespace dao {

namespace {

constexpr char kWorkspaceDirName[] = "DaoAgentWorkspace";
constexpr char kStagingDirName[] = ".workspace_tmp";

base::FilePath StagingDirFor(const base::FilePath& root) {
  return root.AppendASCII(kStagingDirName);
}

// Writes `content` to a temp file under `staging`, then renames it over
// `target`. Returns the WriteResult on success.
base::expected<WriteResult, WorkspaceError> AtomicWrite(
    const base::FilePath& staging,
    const base::FilePath& target,
    const std::string& content) {
  if (!base::CreateDirectory(staging)) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  base::FilePath tmp = staging.AppendASCII(
      base::Uuid::GenerateRandomV4().AsLowercaseString());
  if (!base::WriteFile(tmp, content)) {
    base::DeleteFile(tmp);
    return base::unexpected(WorkspaceError::kIoError);
  }
  const bool created = !base::PathExists(target);
  if (!base::CreateDirectory(target.DirName())) {
    base::DeleteFile(tmp);
    return base::unexpected(WorkspaceError::kIoError);
  }
  if (!base::ReplaceFile(tmp, target, /*error=*/nullptr)) {
    base::DeleteFile(tmp);
    return base::unexpected(WorkspaceError::kIoError);
  }
  WriteResult r;
  r.bytes_written = content.size();
  r.created = created;
  return r;
}

}  // namespace

DaoAgentWorkspaceService::DaoAgentWorkspaceService(
    const base::FilePath& profile_path)
    : workspace_root_(profile_path.AppendASCII(kWorkspaceDirName)),
      io_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE})),
      quota_(std::make_unique<WorkspaceQuota>(workspace_root_)) {
  io_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::EnsureRootExistsOnIO,
                     base::Unretained(this)));
  io_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::ClearStagingOnIO,
                     base::Unretained(this)));
}

DaoAgentWorkspaceService::~DaoAgentWorkspaceService() = default;

void DaoAgentWorkspaceService::Shutdown() {
  weak_factory_.InvalidateWeakPtrs();
}

std::vector<AuditEntry> DaoAgentWorkspaceService::GetRecentAudit() const {
  // Audit log is implemented in Task 3.
  return {};
}

void DaoAgentWorkspaceService::EnsureRootExistsOnIO() {
  base::CreateDirectory(workspace_root_);
}

void DaoAgentWorkspaceService::ClearStagingOnIO() {
  base::DeletePathRecursively(StagingDirFor(workspace_root_));
}

void DaoAgentWorkspaceService::Read(const std::string& rel_path,
                                    int offset_lines,
                                    int limit_lines,
                                    ReadCallback callback) {
  // Clamp limit at the boundary.
  if (limit_lines <= 0) {
    limit_lines = 500;
  }
  if (limit_lines > 5000) {
    limit_lines = 5000;
  }
  if (offset_lines < 0) {
    offset_lines = 0;
  }

  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::ReadOnIO,
                     base::Unretained(this), rel_path, offset_lines,
                     limit_lines),
      std::move(callback));
}

base::expected<ReadResult, WorkspaceError>
DaoAgentWorkspaceService::ReadOnIO(const std::string& rel_path,
                                   int offset,
                                   int limit) {
  auto abs = NormalizePath(workspace_root_, rel_path);
  if (!abs.has_value()) {
    return base::unexpected(abs.error());
  }
  if (!base::PathExists(*abs)) {
    return base::unexpected(WorkspaceError::kNotFound);
  }
  std::string raw;
  if (!base::ReadFileToString(*abs, &raw)) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  std::vector<std::string_view> lines = base::SplitStringPiece(
      raw, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  // SplitStringPiece on "a\nb\n" gives ["a","b",""]; treat trailing empty
  // as terminator, not a line.
  if (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }
  const int total = static_cast<int>(lines.size());

  ReadResult out;
  out.total_lines = total;
  if (offset >= total) {
    out.returned_lines = 0;
    out.truncated = false;
    return out;
  }
  const int end = std::min(total, offset + limit);
  std::string content;
  for (int i = offset; i < end; ++i) {
    content.append(lines[i]);
    content.push_back('\n');
  }
  out.content = std::move(content);
  out.returned_lines = end - offset;
  out.truncated = (end < total);
  return out;
}

void DaoAgentWorkspaceService::Write(const std::string& rel_path,
                                     const std::string& content,
                                     WriteCallback callback) {
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::WriteOnIO,
                     base::Unretained(this), rel_path, content),
      std::move(callback));
}

base::expected<WriteResult, WorkspaceError>
DaoAgentWorkspaceService::WriteOnIO(const std::string& rel_path,
                                    const std::string& content) {
  auto abs = NormalizePath(workspace_root_, rel_path);
  if (!abs.has_value()) {
    return base::unexpected(abs.error());
  }

  if (!IsTextExtensionAllowed(*abs) || ContainsNulByte(content)) {
    return base::unexpected(WorkspaceError::kBinaryRejected);
  }

  uint64_t existing = 0;
  if (base::PathExists(*abs)) {
    std::optional<int64_t> size = base::GetFileSize(*abs);
    if (size.has_value()) {
      existing = static_cast<uint64_t>(*size);
    }
  }
  if (!quota_->CanAcceptWrite(rel_path, content.size(), existing)) {
    return base::unexpected(WorkspaceError::kQuotaExceeded);
  }

  auto result = AtomicWrite(StagingDirFor(workspace_root_), *abs, content);
  if (result.has_value()) {
    quota_->InvalidateCache();
  }
  return result;
}

}  // namespace dao
