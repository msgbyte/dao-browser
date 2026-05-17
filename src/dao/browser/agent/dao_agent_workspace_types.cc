// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_workspace_types.h"

namespace dao {

// Out-of-line definitions required by Chromium's chromium-style plugin
// for structs that hold non-trivial members (containers, pairs, etc.).

ReadResult::ReadResult() = default;
ReadResult::~ReadResult() = default;
ReadResult::ReadResult(const ReadResult&) = default;
ReadResult& ReadResult::operator=(const ReadResult&) = default;
ReadResult::ReadResult(ReadResult&&) noexcept = default;
ReadResult& ReadResult::operator=(ReadResult&&) noexcept = default;

WriteResult::WriteResult() = default;
WriteResult::~WriteResult() = default;
WriteResult::WriteResult(const WriteResult&) = default;
WriteResult& WriteResult::operator=(const WriteResult&) = default;

PatchResult::PatchResult() = default;
PatchResult::~PatchResult() = default;
PatchResult::PatchResult(const PatchResult&) = default;
PatchResult& PatchResult::operator=(const PatchResult&) = default;
PatchResult::PatchResult(PatchResult&&) noexcept = default;
PatchResult& PatchResult::operator=(PatchResult&&) noexcept = default;

ListResult::ListResult() = default;
ListResult::~ListResult() = default;
ListResult::ListResult(const ListResult&) = default;
ListResult& ListResult::operator=(const ListResult&) = default;
ListResult::ListResult(ListResult&&) noexcept = default;
ListResult& ListResult::operator=(ListResult&&) noexcept = default;

AuditEntry::AuditEntry() = default;
AuditEntry::~AuditEntry() = default;
AuditEntry::AuditEntry(const AuditEntry&) = default;
AuditEntry& AuditEntry::operator=(const AuditEntry&) = default;
AuditEntry::AuditEntry(AuditEntry&&) noexcept = default;
AuditEntry& AuditEntry::operator=(AuditEntry&&) noexcept = default;

}  // namespace dao
