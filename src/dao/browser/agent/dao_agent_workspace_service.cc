// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_workspace_service.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/string_view_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/platform_util.h"
#include "dao/browser/agent/workspace/path_normalizer.h"
#include "dao/browser/agent/workspace/text_only_filter.h"
#include "dao/browser/agent/workspace/v4a_patch_applier.h"
#include "dao/browser/agent/workspace/v4a_patch_parser.h"
#include "dao/browser/agent/workspace/workspace_audit.h"
#include "dao/browser/agent/workspace/workspace_file.h"
#include "dao/browser/agent/workspace/workspace_index.h"
#include "dao/browser/agent/workspace/workspace_quota.h"
#include "dao/browser/agent/workspace/workspace_scan.h"
#include "storage/browser/blob/scoped_file.h"

namespace dao {

namespace {

constexpr char kWorkspaceDirName[] = "DaoAgentWorkspace";

}  // namespace

// Queued operations and their filesystem state share one lifetime and sequence.
class DaoAgentWorkspaceService::IOState {
 public:
  explicit IOState(const base::FilePath& root)
      : workspace_root_(root), quota_(root), audit_(root, 200), index_(root) {
    if (EnsureRoot()) {
      RecoverWorkspacePublications(workspace_root_);
    }
  }

  bool EnsureRoot() {
    return base::CreateDirectory(workspace_root_) &&
           !base::IsLink(workspace_root_);
  }

  std::vector<AuditEntry> GetRecentAudit() const { return audit_.Snapshot(); }

  UsageSnapshot GetUsageInfo() {
    auto usage = quota_.GetUsage();
    return {workspace_root_, usage.total_bytes, WorkspaceQuota::kTotalMaxBytes,
            usage.entry_count, WorkspaceQuota::kMaxEntries};
  }

  base::expected<ReadResult, WorkspaceError>
  ReadOnIO(const std::string& rel_path, int offset, int limit);
  base::expected<WriteResult, WorkspaceError> WriteOnIO(
      const std::string& rel_path,
      const std::string& content);
  base::expected<WriteResult, WorkspaceError> EditOnIO(
      const std::string& rel_path,
      const std::string& old_str,
      const std::string& new_str);
  base::expected<PatchResult, WorkspaceError> ApplyPatchOnIO(
      const std::string& patch_text);
  base::expected<WriteResult, WorkspaceError> IngestDownloadedFileOnIO(
      const std::string& rel_path,
      storage::ScopedFile download,
      const std::string& audit_detail);
  base::expected<ListResult, WorkspaceError> ListOnIO(
      const std::string& rel_path,
      bool recursive);

 private:
  const base::FilePath workspace_root_;
  WorkspaceQuota quota_;
  WorkspaceAudit audit_;
  WorkspaceIndex index_;
};

DaoAgentWorkspaceService::DaoAgentWorkspaceService(
    const base::FilePath& profile_path)
    : workspace_root_(profile_path.AppendASCII(kWorkspaceDirName)),
      io_(base::ThreadPool::CreateSequencedTaskRunner(
              {base::MayBlock(), base::TaskPriority::USER_VISIBLE}),
          workspace_root_) {}

DaoAgentWorkspaceService::~DaoAgentWorkspaceService() = default;

std::vector<AuditEntry> DaoAgentWorkspaceService::GetRecentAudit() const {
  return {};
}

void DaoAgentWorkspaceService::GetRecentAuditAsync(
    base::OnceCallback<void(std::vector<AuditEntry>)> callback) {
  io_.AsyncCall(&IOState::GetRecentAudit).Then(std::move(callback));
}

void DaoAgentWorkspaceService::GetUsageInfo(
    base::OnceCallback<void(UsageSnapshot)> callback) {
  io_.AsyncCall(&IOState::GetUsageInfo).Then(std::move(callback));
}

void DaoAgentWorkspaceService::OpenInFileManager() {
  io_.AsyncCall(&IOState::EnsureRoot)
      .Then(base::BindOnce(
          [](base::FilePath path, bool ready) {
            if (ready) {
              platform_util::OpenItem(nullptr, path, platform_util::OPEN_FOLDER,
                                      platform_util::OpenOperationCallback());
            }
          },
          workspace_root_));
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

  io_.AsyncCall(&IOState::ReadOnIO)
      .WithArgs(rel_path, offset_lines, limit_lines)
      .Then(std::move(callback));
}

base::expected<ReadResult, WorkspaceError>
DaoAgentWorkspaceService::IOState::ReadOnIO(const std::string& rel_path,
                                            int offset,
                                            int limit) {
  auto path = ResolveWorkspacePath(workspace_root_, rel_path);
  if (!path.has_value()) {
    return base::unexpected(path.error());
  }
  base::File file = OpenWorkspaceFile(workspace_root_, *path);
  if (!file.IsValid()) {
    return base::unexpected(file.error_details() ==
                                    base::File::FILE_ERROR_NOT_FOUND
                                ? WorkspaceError::kNotFound
                                : WorkspaceError::kIoError);
  }

  ReadResult out;
  const int64_t end = static_cast<int64_t>(offset) + limit;
  int64_t first_line = 0;
  bool unterminated = false;
  std::vector<uint8_t> buffer(64 * 1024);
  for (;;) {
    const auto size = file.ReadAtCurrentPos(buffer);
    if (!size) {
      return base::unexpected(WorkspaceError::kIoError);
    }
    if (*size == 0) {
      break;
    }
    auto chunk = base::as_string_view(base::span(buffer).first(*size));
    const int64_t next_line = first_line + std::ranges::count(chunk, '\n');
    unterminated = chunk.back() != '\n';
    // Count the whole file for pagination, but only copy the requested lines.
    if (next_line >= offset && first_line < end) {
      for (int64_t line = first_line; !chunk.empty() && line < end; ++line) {
        const size_t newline = chunk.find('\n');
        const size_t bytes =
            newline == std::string_view::npos ? chunk.size() : newline + 1;
        if (line >= offset) {
          out.content.append(chunk.substr(0, bytes));
        }
        chunk.remove_prefix(bytes);
      }
    }
    first_line = next_line;
  }

  if (unterminated && first_line >= offset && first_line < end) {
    out.content.push_back('\n');
  }
  const int64_t total = first_line + unterminated;
  if (total > std::numeric_limits<int>::max()) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  out.total_lines = static_cast<int>(total);
  out.returned_lines = std::clamp(out.total_lines - offset, 0, limit);
  out.truncated = end < total;
  return out;
}

void DaoAgentWorkspaceService::Write(const std::string& rel_path,
                                     const std::string& content,
                                     WriteCallback callback) {
  io_.AsyncCall(&IOState::WriteOnIO)
      .WithArgs(rel_path, content)
      .Then(std::move(callback));
}

void DaoAgentWorkspaceService::Edit(const std::string& rel_path,
                                    const std::string& old_str,
                                    const std::string& new_str,
                                    WriteCallback callback) {
  io_.AsyncCall(&IOState::EditOnIO)
      .WithArgs(rel_path, old_str, new_str)
      .Then(std::move(callback));
}

base::expected<WriteResult, WorkspaceError>
DaoAgentWorkspaceService::IOState::EditOnIO(const std::string& rel_path,
                                            const std::string& old_str,
                                            const std::string& new_str) {
  auto relative = ResolveWorkspacePath(workspace_root_, rel_path);
  if (!relative.has_value()) {
    return base::unexpected(relative.error());
  }
  auto file = OpenWorkspaceFile(workspace_root_, *relative);
  if (!file.IsValid()) {
    return base::unexpected(file.error_details() ==
                                    base::File::FILE_ERROR_NOT_FOUND
                                ? WorkspaceError::kNotFound
                                : WorkspaceError::kIoError);
  }
  base::ScopedFILE stream(base::FileToFILE(std::move(file), "rb"));
  std::string body;
  if (!stream || !base::ReadStreamToString(stream.get(), &body)) {
    return base::unexpected(WorkspaceError::kIoError);
  }

  stream.reset();

  // Count occurrences (require exactly one).
  size_t first = body.find(old_str);
  if (first == std::string::npos) {
    return base::unexpected(WorkspaceError::kNotFound);
  }
  size_t second = body.find(old_str, first + old_str.size());
  if (second != std::string::npos) {
    return base::unexpected(WorkspaceError::kEditNotUnique);
  }

  std::string updated = body;
  updated.replace(first, old_str.size(), new_str);

  if (ContainsNulByte(updated)) {
    return base::unexpected(WorkspaceError::kBinaryRejected);
  }
  if (!quota_.CanAcceptWrite(*relative, updated.size(), body.size())) {
    return base::unexpected(WorkspaceError::kQuotaExceeded);
  }

  if (!WriteWorkspaceFile(workspace_root_, *relative, updated)) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  WriteResult result;
  result.bytes_written = updated.size();
  result.created = false;
  quota_.InvalidateCache();
  AuditEntry entry;
  entry.ts = base::TimeFormatAsIso8601(base::Time::Now());
  entry.op = "edit";
  entry.path = rel_path;
  entry.detail = base::StringPrintf("\"old_len\":%zu,\"new_len\":%zu",
                                    old_str.size(), new_str.size());
  audit_.Append(entry);
  index_.Rewrite();
  return result;
}

base::expected<WriteResult, WorkspaceError>
DaoAgentWorkspaceService::IOState::WriteOnIO(const std::string& rel_path,
                                             const std::string& content) {
  auto relative = ResolveWorkspacePath(workspace_root_, rel_path);
  if (!relative.has_value()) {
    return base::unexpected(relative.error());
  }

  if (!IsTextExtensionAllowed(*relative) || ContainsNulByte(content)) {
    return base::unexpected(WorkspaceError::kBinaryRejected);
  }

  auto existing = WorkspaceFileSize(workspace_root_, *relative);
  if (!existing.has_value()) {
    return base::unexpected(existing.error());
  }
  if (!quota_.CanAcceptWrite(*relative, content.size(), *existing)) {
    return base::unexpected(WorkspaceError::kQuotaExceeded);
  }

  if (!WriteWorkspaceFile(workspace_root_, *relative, content)) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  WriteResult result;
  result.bytes_written = content.size();
  result.created = !existing->has_value();
  quota_.InvalidateCache();
  AuditEntry entry;
  entry.ts = base::TimeFormatAsIso8601(base::Time::Now());
  entry.op = "write";
  entry.path = rel_path;
  entry.detail =
      base::StringPrintf("\"bytes\":%zu,\"created\":%s", content.size(),
                         result.created ? "true" : "false");
  audit_.Append(entry);
  index_.Rewrite();
  return result;
}

void DaoAgentWorkspaceService::IngestDownloadedFile(
    const std::string& rel_path,
    storage::ScopedFile download,
    const std::string& audit_detail,
    WriteCallback callback) {
  io_.AsyncCall(&IOState::IngestDownloadedFileOnIO)
      .WithArgs(rel_path, std::move(download), audit_detail)
      .Then(std::move(callback));
}

base::expected<WriteResult, WorkspaceError>
DaoAgentWorkspaceService::IOState::IngestDownloadedFileOnIO(
    const std::string& rel_path,
    storage::ScopedFile download,
    const std::string& audit_detail) {
  auto relative = ResolveWorkspacePath(workspace_root_, rel_path);
  if (!relative.has_value()) {
    return base::unexpected(relative.error());
  }
  if (!IsTextExtensionAllowed(*relative)) {
    return base::unexpected(WorkspaceError::kBinaryRejected);
  }

  base::File source(download.path(), base::File::FLAG_OPEN |
                                         base::File::FLAG_READ |
                                         base::File::FLAG_NO_FOLLOW |
                                         base::File::FLAG_WIN_EXCLUSIVE_WRITE);
  base::File::Info info;
  if (!source.IsValid() || !source.GetInfo(&info) || info.is_directory ||
      info.size < 0) {
    return base::unexpected(WorkspaceError::kIoError);
  }

  std::string head(std::min<int64_t>(info.size, 8192), '\0');
  if (!source.ReadAtCurrentPosAndCheck(base::as_writable_byte_span(head)) ||
      source.Seek(base::File::FROM_BEGIN, 0) != 0) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  if (ContainsNulByte(head)) {
    return base::unexpected(WorkspaceError::kBinaryRejected);
  }

  auto existing_size = WorkspaceFileSize(workspace_root_, *relative);
  if (!existing_size.has_value()) {
    return base::unexpected(existing_size.error());
  }
  if (!quota_.CanAcceptWrite(*relative, info.size, *existing_size)) {
    return base::unexpected(WorkspaceError::kQuotaExceeded);
  }
  auto published = CopyToWorkspaceFile(workspace_root_, *relative, source,
                                       WorkspacePublishMode::kReplace);
  if (!published.has_value()) {
    return base::unexpected(published.error());
  }

  quota_.InvalidateCache();
  AuditEntry entry;
  entry.ts = base::TimeFormatAsIso8601(base::Time::Now());
  entry.op = "download";
  entry.path = rel_path;
  entry.detail = audit_detail;
  audit_.Append(entry);
  index_.Rewrite();

  WriteResult r;
  r.bytes_written = static_cast<size_t>(info.size);
  r.created = !existing_size->has_value();
  return r;
}

void DaoAgentWorkspaceService::List(const std::string& rel_path,
                                    bool recursive,
                                    ListCallback callback) {
  io_.AsyncCall(&IOState::ListOnIO)
      .WithArgs(rel_path, recursive)
      .Then(std::move(callback));
}

base::expected<ListResult, WorkspaceError>
DaoAgentWorkspaceService::IOState::ListOnIO(const std::string& rel_path,
                                            bool recursive) {
  base::FilePath directory;
  if (!rel_path.empty()) {
    auto relative = ResolveWorkspacePath(workspace_root_, rel_path);
    if (!relative.has_value()) {
      return base::unexpected(relative.error());
    }
    directory = std::move(*relative);
  }

  ListResult out;
  const auto status = ScanWorkspace(
      workspace_root_, directory, recursive, WorkspaceScanScope::kVisible,
      [&](const base::FilePath& path, const base::File::Info& info) {
        ++out.total;
        if (out.entries.size() >= kListMaxEntries) {
          out.truncated = true;
          return;
        }
        ListEntry entry;
        entry.path = path.NormalizePathSeparatorsTo(FILE_PATH_LITERAL('/'))
                         .AsUTF8Unsafe();
        entry.is_dir = info.is_directory;
        if (entry.is_dir) {
          entry.path.push_back('/');
        } else {
          entry.size_bytes = static_cast<uint64_t>(info.size);
        }
        if (!info.last_modified.is_null()) {
          entry.mtime = base::TimeFormatAsIso8601(info.last_modified);
        }
        out.entries.push_back(std::move(entry));
      });
  if (status != WorkspaceError::kOk) {
    return base::unexpected(status);
  }

  std::sort(
      out.entries.begin(), out.entries.end(),
      [](const ListEntry& a, const ListEntry& b) { return a.path < b.path; });
  return out;
}

void DaoAgentWorkspaceService::ApplyPatch(const std::string& patch_text,
                                          PatchCallback callback) {
  io_.AsyncCall(&IOState::ApplyPatchOnIO)
      .WithArgs(patch_text)
      .Then(std::move(callback));
}

base::expected<PatchResult, WorkspaceError>
DaoAgentWorkspaceService::IOState::ApplyPatchOnIO(
    const std::string& patch_text) {
  auto parsed = ParseV4APatch(patch_text);
  if (!parsed.has_value()) {
    return base::unexpected(WorkspaceError::kPatchParseError);
  }
  auto result = ApplyV4APatch(workspace_root_, &quota_, *parsed);
  if (result.has_value()) {
    AuditEntry entry;
    entry.ts = base::TimeFormatAsIso8601(base::Time::Now());
    entry.op = "apply_patch";
    entry.path = "";
    entry.detail = base::StringPrintf(
        "\"added\":%zu,\"updated\":%zu,\"deleted\":%zu,\"moved\":%zu",
        result->added.size(), result->updated.size(), result->deleted.size(),
        result->moved.size());
    audit_.Append(entry);
    index_.Rewrite();
  }
  return result;
}

}  // namespace dao
