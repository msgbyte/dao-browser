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
