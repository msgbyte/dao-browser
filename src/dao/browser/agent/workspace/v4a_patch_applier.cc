// Copyright 2026 Dao Browser Authors. All rights reserved.

#include "dao/browser/agent/workspace/v4a_patch_applier.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "dao/browser/agent/workspace/path_normalizer.h"
#include "dao/browser/agent/workspace/text_only_filter.h"
#include "dao/browser/agent/workspace/workspace_quota.h"

namespace dao {

namespace {

// Read file as a vector of lines (no trailing empty entry from terminating
// newline).
base::expected<std::vector<std::string>, WorkspaceError> ReadLines(
    const base::FilePath& abs) {
  std::string raw;
  if (!base::ReadFileToString(abs, &raw)) {
    return base::unexpected(WorkspaceError::kIoError);
  }
  std::vector<std::string> lines = base::SplitString(
      raw, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (!lines.empty() && lines.back().empty()) {
    lines.pop_back();
  }
  return lines;
}

std::string JoinLines(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& l : lines) {
    out += l;
    out.push_back('\n');
  }
  return out;
}

// Locates the unique index in `body` where the hunk's context+remove block
// matches. Returns -1 if not found, -2 if found in multiple places.
int FindUniqueHunkMatch(const std::vector<std::string>& body,
                        const std::vector<std::string>& needle) {
  if (needle.empty()) {
    return -1;
  }
  int found = -1;
  for (int i = 0;
       i + static_cast<int>(needle.size()) <= static_cast<int>(body.size());
       ++i) {
    bool ok = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (body[i + j] != needle[j]) {
        ok = false;
        break;
      }
    }
    if (ok) {
      if (found != -1) {
        return -2;
      }
      found = i;
    }
  }
  return found;
}

base::expected<std::vector<std::string>, WorkspaceError> ApplyHunkToBody(
    std::vector<std::string> body,
    const V4AHunk& hunk) {
  // Needle: context + remove lines, in declaration order.
  std::vector<std::string> needle;
  std::vector<std::string> replacement;
  for (const auto& l : hunk.lines) {
    switch (l.kind) {
      case V4AHunkLine::Kind::kContext:
        needle.push_back(l.text);
        replacement.push_back(l.text);
        break;
      case V4AHunkLine::Kind::kRemove:
        needle.push_back(l.text);
        break;
      case V4AHunkLine::Kind::kAdd:
        replacement.push_back(l.text);
        break;
    }
  }
  int at = FindUniqueHunkMatch(body, needle);
  if (at < 0) {
    return base::unexpected(WorkspaceError::kPatchContextMismatch);
  }
  body.erase(body.begin() + at, body.begin() + at + needle.size());
  body.insert(body.begin() + at, replacement.begin(), replacement.end());
  return body;
}

struct StagedWrite {
  base::FilePath staged;
  base::FilePath final_dest;
};

}  // namespace

base::expected<PatchResult, WorkspaceError> ApplyV4APatch(
    const base::FilePath& workspace_root,
    const base::FilePath& staging_dir,
    const std::string& request_id,
    WorkspaceQuota* quota,
    const V4APatch& patch) {
  base::FilePath stage = staging_dir.AppendASCII(request_id);

  // Ensure cleanup runs no matter what.
  struct Cleanup {
    base::FilePath path;
    ~Cleanup() { base::DeletePathRecursively(path); }
  } cleanup{stage};

  if (!base::CreateDirectory(stage)) {
    return base::unexpected(WorkspaceError::kIoError);
  }

  PatchResult result;
  std::set<std::string> touched_destinations;

  std::vector<StagedWrite> writes;
  std::vector<base::FilePath> deletes;

  for (const V4AFileOp& op : patch.ops) {
    auto abs = NormalizePath(workspace_root, op.path);
    if (!abs.has_value()) {
      return base::unexpected(abs.error());
    }
    const std::string& key = op.path;
    if (!touched_destinations.insert(key).second) {
      return base::unexpected(WorkspaceError::kPatchParseError);
    }

    switch (op.kind) {
      case V4AFileOp::Kind::kAdd: {
        if (base::PathExists(*abs)) {
          return base::unexpected(WorkspaceError::kAlreadyExists);
        }
        if (!IsTextExtensionAllowed(*abs)) {
          return base::unexpected(WorkspaceError::kBinaryRejected);
        }
        std::string body;
        for (const auto& l : op.add_lines) {
          body += l;
          body.push_back('\n');
        }
        if (ContainsNulByte(body)) {
          return base::unexpected(WorkspaceError::kBinaryRejected);
        }
        if (!quota->CanAcceptWrite(op.path, body.size(),
                                   /*replacing_existing_bytes=*/0)) {
          return base::unexpected(WorkspaceError::kQuotaExceeded);
        }
        base::FilePath staged = stage.AppendASCII(op.path);
        if (!base::CreateDirectory(staged.DirName())) {
          return base::unexpected(WorkspaceError::kIoError);
        }
        if (!base::WriteFile(staged, body)) {
          return base::unexpected(WorkspaceError::kIoError);
        }
        writes.push_back({staged, *abs});
        result.added.push_back(op.path);
        break;
      }

      case V4AFileOp::Kind::kUpdate: {
        if (!base::PathExists(*abs)) {
          return base::unexpected(WorkspaceError::kNotFound);
        }
        auto lines = ReadLines(*abs);
        if (!lines.has_value()) {
          return base::unexpected(lines.error());
        }
        std::vector<std::string> body = *lines;
        for (const V4AHunk& h : op.hunks) {
          auto next = ApplyHunkToBody(body, h);
          if (!next.has_value()) {
            return base::unexpected(next.error());
          }
          body = *next;
        }
        std::string new_text = JoinLines(body);
        if (ContainsNulByte(new_text)) {
          return base::unexpected(WorkspaceError::kBinaryRejected);
        }
        uint64_t existing_size = 0;
        if (auto sz = base::GetFileSize(*abs); sz.has_value()) {
          existing_size = static_cast<uint64_t>(*sz);
        }
        if (!quota->CanAcceptWrite(op.path, new_text.size(), existing_size)) {
          return base::unexpected(WorkspaceError::kQuotaExceeded);
        }

        base::FilePath dest = *abs;
        if (op.move_to.has_value()) {
          auto dest_abs = NormalizePath(workspace_root, *op.move_to);
          if (!dest_abs.has_value()) {
            return base::unexpected(dest_abs.error());
          }
          if (!IsTextExtensionAllowed(*dest_abs)) {
            return base::unexpected(WorkspaceError::kBinaryRejected);
          }
          if (base::PathExists(*dest_abs) && *dest_abs != *abs) {
            return base::unexpected(WorkspaceError::kAlreadyExists);
          }
          dest = *dest_abs;
          deletes.push_back(*abs);
          result.moved.emplace_back(op.path, *op.move_to);
        } else {
          result.updated.push_back(op.path);
        }
        base::FilePath staged =
            stage.AppendASCII(op.move_to.value_or(op.path));
        if (!base::CreateDirectory(staged.DirName())) {
          return base::unexpected(WorkspaceError::kIoError);
        }
        if (!base::WriteFile(staged, new_text)) {
          return base::unexpected(WorkspaceError::kIoError);
        }
        writes.push_back({staged, dest});
        break;
      }

      case V4AFileOp::Kind::kDelete: {
        if (!base::PathExists(*abs)) {
          return base::unexpected(WorkspaceError::kNotFound);
        }
        deletes.push_back(*abs);
        result.deleted.push_back(op.path);
        break;
      }
    }
  }

  // Phase 3: commit. Renames first, then deletes (so a Move that races a
  // Delete still leaves the renamed file in place).
  for (const auto& w : writes) {
    if (!base::CreateDirectory(w.final_dest.DirName())) {
      return base::unexpected(WorkspaceError::kIoError);
    }
    if (!base::ReplaceFile(w.staged, w.final_dest, /*error=*/nullptr)) {
      return base::unexpected(WorkspaceError::kIoError);
    }
  }
  for (const auto& d : deletes) {
    base::DeleteFile(d);
  }

  quota->InvalidateCache();
  return result;
}

}  // namespace dao
