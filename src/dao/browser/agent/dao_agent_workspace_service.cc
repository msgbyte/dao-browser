// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_workspace_service.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/i18n/time_formatting.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "chrome/browser/platform_util.h"
#include "dao/browser/agent/workspace/path_normalizer.h"
#include "dao/browser/agent/workspace/text_only_filter.h"
#include "dao/browser/agent/workspace/v4a_patch_applier.h"
#include "dao/browser/agent/workspace/v4a_patch_parser.h"
#include "dao/browser/agent/workspace/workspace_audit.h"
#include "dao/browser/agent/workspace/workspace_index.h"
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
      quota_(std::make_unique<WorkspaceQuota>(workspace_root_)),
      audit_(std::make_unique<WorkspaceAudit>(workspace_root_, /*ring=*/200)),
      index_(std::make_unique<WorkspaceIndex>(workspace_root_)) {
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

void DaoAgentWorkspaceService::GetRecentAuditAsync(
    base::OnceCallback<void(std::vector<AuditEntry>)> callback) {
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](WorkspaceAudit* audit) { return audit->Snapshot(); },
          audit_.get()),
      std::move(callback));
}

void DaoAgentWorkspaceService::GetUsageInfo(
    base::OnceCallback<void(UsageSnapshot)> callback) {
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](WorkspaceQuota* quota, base::FilePath root) {
            WorkspaceQuota::Usage usage = quota->GetUsage();
            UsageSnapshot snap;
            snap.root = std::move(root);
            snap.used_bytes = usage.total_bytes;
            snap.cap_bytes = WorkspaceQuota::kTotalMaxBytes;
            snap.file_count = usage.entry_count;
            snap.file_count_cap = WorkspaceQuota::kMaxEntries;
            return snap;
          },
          quota_.get(), workspace_root_),
      std::move(callback));
}

void DaoAgentWorkspaceService::OpenInFileManager() {
  io_runner_->PostTaskAndReply(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::EnsureRootExistsOnIO,
                     base::Unretained(this)),
      base::BindOnce(
          [](base::FilePath path) {
            platform_util::OpenItem(/*profile=*/nullptr, path,
                                    platform_util::OPEN_FOLDER,
                                    platform_util::OpenOperationCallback());
          },
          workspace_root_));
}

void DaoAgentWorkspaceService::EnsureRootExistsOnIO() {
  base::CreateDirectory(workspace_root_);
}

void DaoAgentWorkspaceService::ClearStagingOnIO() {
  const base::FilePath staging = StagingDirFor(workspace_root_);
  base::DeletePathRecursively(staging);
  base::CreateDirectory(staging);
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

void DaoAgentWorkspaceService::Edit(const std::string& rel_path,
                                    const std::string& old_str,
                                    const std::string& new_str,
                                    WriteCallback callback) {
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::EditOnIO,
                     base::Unretained(this), rel_path, old_str, new_str),
      std::move(callback));
}

base::expected<WriteResult, WorkspaceError>
DaoAgentWorkspaceService::EditOnIO(const std::string& rel_path,
                                   const std::string& old_str,
                                   const std::string& new_str) {
  auto abs = NormalizePath(workspace_root_, rel_path);
  if (!abs.has_value()) {
    return base::unexpected(abs.error());
  }
  if (!base::PathExists(*abs)) {
    return base::unexpected(WorkspaceError::kNotFound);
  }
  std::string body;
  if (!base::ReadFileToString(*abs, &body)) {
    return base::unexpected(WorkspaceError::kIoError);
  }

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
  if (!quota_->CanAcceptWrite(rel_path, updated.size(), body.size())) {
    return base::unexpected(WorkspaceError::kQuotaExceeded);
  }

  auto result = AtomicWrite(StagingDirFor(workspace_root_), *abs, updated);
  if (result.has_value()) {
    quota_->InvalidateCache();
    result->created = false;
    AuditEntry entry;
    entry.ts = base::TimeFormatAsIso8601(base::Time::Now());
    entry.op = "edit";
    entry.path = rel_path;
    entry.detail = base::StringPrintf("\"old_len\":%zu,\"new_len\":%zu",
                                      old_str.size(), new_str.size());
    audit_->Append(entry);
    index_->Rewrite();
  }
  return result;
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
    // Audit + index.
    AuditEntry entry;
    entry.ts = base::TimeFormatAsIso8601(base::Time::Now());
    entry.op = "write";
    entry.path = rel_path;
    entry.detail = base::StringPrintf("\"bytes\":%zu,\"created\":%s",
                                       content.size(),
                                       result->created ? "true" : "false");
    audit_->Append(entry);
    index_->Rewrite();
  }
  return result;
}

void DaoAgentWorkspaceService::AllocateStagingPath(
    base::OnceCallback<void(base::FilePath)> callback) {
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::AllocateStagingPathOnIO,
                     base::Unretained(this)),
      std::move(callback));
}

base::FilePath DaoAgentWorkspaceService::AllocateStagingPathOnIO() {
  const base::FilePath staging = StagingDirFor(workspace_root_);
  if (!base::CreateDirectory(staging)) {
    return {};
  }
  return staging.AppendASCII(
      base::Uuid::GenerateRandomV4().AsLowercaseString());
}

void DaoAgentWorkspaceService::IngestStagedFile(
    const std::string& rel_path,
    const base::FilePath& staged_abs_path,
    const std::string& audit_detail,
    WriteCallback callback) {
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::IngestStagedFileOnIO,
                     base::Unretained(this), rel_path, staged_abs_path,
                     audit_detail),
      std::move(callback));
}

base::expected<WriteResult, WorkspaceError>
DaoAgentWorkspaceService::IngestStagedFileOnIO(
    const std::string& rel_path,
    const base::FilePath& staged_abs_path,
    const std::string& audit_detail) {
  // Resolve target. Failures here delete the staged file so we never
  // leak partial downloads.
  auto cleanup_staging = [&] {
    if (!staged_abs_path.empty()) {
      base::DeleteFile(staged_abs_path);
    }
  };

  auto abs = NormalizePath(workspace_root_, rel_path);
  if (!abs.has_value()) {
    cleanup_staging();
    return base::unexpected(abs.error());
  }
  if (!IsTextExtensionAllowed(*abs)) {
    cleanup_staging();
    return base::unexpected(WorkspaceError::kBinaryRejected);
  }

  std::optional<int64_t> staged_size = base::GetFileSize(staged_abs_path);
  if (!staged_size.has_value()) {
    cleanup_staging();
    return base::unexpected(WorkspaceError::kIoError);
  }

  // First-8 KiB NUL probe — defense in depth against text-extensioned
  // binaries (e.g. UTF-16 .txt). Matches WriteOnIO's policy.
  // ReadFileToStringWithMaxSize returns false if the file is larger
  // than `max_size` but still populates `head` with the first
  // max_size bytes, which is exactly what we want for the probe. We
  // only treat "wanted bytes but got zero on a non-empty file" as an
  // IO error.
  {
    std::string head;
    base::ReadFileToStringWithMaxSize(staged_abs_path, &head, 8192);
    if (head.empty() && *staged_size > 0) {
      cleanup_staging();
      return base::unexpected(WorkspaceError::kIoError);
    }
    if (ContainsNulByte(head)) {
      cleanup_staging();
      return base::unexpected(WorkspaceError::kBinaryRejected);
    }
  }

  uint64_t existing = 0;
  if (base::PathExists(*abs)) {
    std::optional<int64_t> size = base::GetFileSize(*abs);
    if (size.has_value()) {
      existing = static_cast<uint64_t>(*size);
    }
  }
  if (!quota_->CanAcceptWrite(rel_path, static_cast<uint64_t>(*staged_size),
                              existing)) {
    cleanup_staging();
    return base::unexpected(WorkspaceError::kQuotaExceeded);
  }

  if (!base::CreateDirectory(abs->DirName())) {
    cleanup_staging();
    return base::unexpected(WorkspaceError::kIoError);
  }
  const bool created = !base::PathExists(*abs);
  if (!base::ReplaceFile(staged_abs_path, *abs, /*error=*/nullptr)) {
    cleanup_staging();
    return base::unexpected(WorkspaceError::kIoError);
  }
  // Successfully moved — no cleanup needed.

  quota_->InvalidateCache();
  AuditEntry entry;
  entry.ts = base::TimeFormatAsIso8601(base::Time::Now());
  entry.op = "download";
  entry.path = rel_path;
  entry.detail = audit_detail;
  audit_->Append(entry);
  index_->Rewrite();

  WriteResult r;
  r.bytes_written = static_cast<size_t>(*staged_size);
  r.created = created;
  return r;
}

void DaoAgentWorkspaceService::List(const std::string& rel_path,
                                    bool recursive,
                                    ListCallback callback) {
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::ListOnIO,
                     base::Unretained(this), rel_path, recursive),
      std::move(callback));
}

base::expected<ListResult, WorkspaceError>
DaoAgentWorkspaceService::ListOnIO(const std::string& rel_path,
                                   bool recursive) {
  // Resolve target: "" means the workspace root itself; otherwise the
  // path must validate via the workspace's normal safety filter.
  base::FilePath target;
  if (rel_path.empty()) {
    target = workspace_root_;
  } else {
    auto abs = NormalizePath(workspace_root_, rel_path);
    if (!abs.has_value()) {
      return base::unexpected(abs.error());
    }
    target = *abs;
  }
  if (!base::PathExists(target)) {
    return base::unexpected(WorkspaceError::kNotFound);
  }
  if (!base::DirectoryExists(target)) {
    return base::unexpected(WorkspaceError::kInvalidPath);
  }

  ListResult out;
  const int file_types =
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES;
  base::FileEnumerator enumerator(target, recursive, file_types);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    base::FilePath relative;
    if (!workspace_root_.AppendRelativePath(path, &relative)) {
      continue;
    }
    auto comps = relative.GetComponents();
    if (comps.empty()) {
      continue;
    }
    // Skip workspace bookkeeping and any dotfile/dotdir at any depth.
    bool skip = false;
    for (const auto& c : comps) {
      if (c == FILE_PATH_LITERAL(".workspace_tmp") ||
          c == FILE_PATH_LITERAL(".audit.log") ||
          c == FILE_PATH_LITERAL("WORKSPACE.md") ||
          (!c.empty() && c.front() == FILE_PATH_LITERAL('.'))) {
        skip = true;
        break;
      }
    }
    if (skip) {
      continue;
    }

    ++out.total;
    if (out.entries.size() >= kListMaxEntries) {
      out.truncated = true;
      continue;
    }

    base::FileEnumerator::FileInfo info = enumerator.GetInfo();
    ListEntry entry;
    // Use forward slashes regardless of platform so the LLM gets a
    // consistent shape.
    std::string rel_utf8 = relative.AsUTF8Unsafe();
#if BUILDFLAG(IS_WIN)
    std::replace(rel_utf8.begin(), rel_utf8.end(), '\\', '/');
#endif
    entry.is_dir = info.IsDirectory();
    if (entry.is_dir) {
      rel_utf8.push_back('/');
      entry.size_bytes = 0;
    } else {
      entry.size_bytes = static_cast<uint64_t>(info.GetSize());
    }
    entry.path = std::move(rel_utf8);
    base::Time mtime = info.GetLastModifiedTime();
    if (!mtime.is_null()) {
      entry.mtime = base::TimeFormatAsIso8601(mtime);
    }
    out.entries.push_back(std::move(entry));
  }

  std::sort(out.entries.begin(), out.entries.end(),
            [](const ListEntry& a, const ListEntry& b) {
              return a.path < b.path;
            });
  return out;
}

void DaoAgentWorkspaceService::ApplyPatch(const std::string& patch_text,
                                          PatchCallback callback) {
  io_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentWorkspaceService::ApplyPatchOnIO,
                     base::Unretained(this), patch_text),
      std::move(callback));
}

base::expected<PatchResult, WorkspaceError>
DaoAgentWorkspaceService::ApplyPatchOnIO(const std::string& patch_text) {
  auto parsed = ParseV4APatch(patch_text);
  if (!parsed.has_value()) {
    return base::unexpected(WorkspaceError::kPatchParseError);
  }
  const std::string request_id =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto result = ApplyV4APatch(workspace_root_,
                              StagingDirFor(workspace_root_),
                              request_id, quota_.get(), *parsed);
  if (result.has_value()) {
    AuditEntry entry;
    entry.ts = base::TimeFormatAsIso8601(base::Time::Now());
    entry.op = "apply_patch";
    entry.path = "";
    entry.detail = base::StringPrintf(
        "\"added\":%zu,\"updated\":%zu,\"deleted\":%zu,\"moved\":%zu",
        result->added.size(), result->updated.size(),
        result->deleted.size(), result->moved.size());
    audit_->Append(entry);
    index_->Rewrite();
  }
  return result;
}

}  // namespace dao
