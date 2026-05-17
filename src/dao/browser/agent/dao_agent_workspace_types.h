// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_TYPES_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_TYPES_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dao {

enum class WorkspaceError {
  kOk = 0,
  kInvalidPath,
  kNotFound,
  kAlreadyExists,
  kQuotaExceeded,
  kBinaryRejected,
  kPatchParseError,
  kPatchContextMismatch,
  kEditNotUnique,
  kIoError,
};

struct ReadResult {
  ReadResult();
  ~ReadResult();
  ReadResult(const ReadResult&);
  ReadResult& operator=(const ReadResult&);
  ReadResult(ReadResult&&) noexcept;
  ReadResult& operator=(ReadResult&&) noexcept;

  std::string content;
  int total_lines = 0;
  int returned_lines = 0;
  bool truncated = false;
};

struct WriteResult {
  WriteResult();
  ~WriteResult();
  WriteResult(const WriteResult&);
  WriteResult& operator=(const WriteResult&);

  size_t bytes_written = 0;
  bool created = false;
};

struct PatchResult {
  PatchResult();
  ~PatchResult();
  PatchResult(const PatchResult&);
  PatchResult& operator=(const PatchResult&);
  PatchResult(PatchResult&&) noexcept;
  PatchResult& operator=(PatchResult&&) noexcept;

  std::vector<std::string> added;
  std::vector<std::string> updated;
  std::vector<std::string> deleted;
  std::vector<std::pair<std::string, std::string>> moved;
};

struct ListEntry {
  // Forward-slash-normalized relative path from the workspace root.
  // Directories carry a trailing slash so the LLM does not need to
  // inspect `is_dir` to tell them apart at a glance.
  std::string path;
  bool is_dir = false;
  uint64_t size_bytes = 0;
  // ISO 8601 UTC, e.g. "2026-05-18T07:31:02Z". Empty if stat failed.
  std::string mtime;
};

struct ListResult {
  ListResult();
  ~ListResult();
  ListResult(const ListResult&);
  ListResult& operator=(const ListResult&);
  ListResult(ListResult&&) noexcept;
  ListResult& operator=(ListResult&&) noexcept;

  std::vector<ListEntry> entries;
  // Total entries that matched the query before any cap was applied.
  uint32_t total = 0;
  // True if `entries` was truncated against the hard cap below.
  bool truncated = false;
};

struct AuditEntry {
  AuditEntry();
  ~AuditEntry();
  AuditEntry(const AuditEntry&);
  AuditEntry& operator=(const AuditEntry&);
  AuditEntry(AuditEntry&&) noexcept;
  AuditEntry& operator=(AuditEntry&&) noexcept;

  std::string ts;
  std::string op;
  std::string path;
  std::string detail;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_WORKSPACE_TYPES_H_
