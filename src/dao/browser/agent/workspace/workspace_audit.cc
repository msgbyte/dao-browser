// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_audit.h"

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "dao/browser/agent/workspace/workspace_file.h"

namespace dao {

namespace {

std::string SerializeJson(const AuditEntry& e) {
  std::string ts, op, path;
  base::EscapeJSONString(e.ts, /*put_in_quotes=*/true, &ts);
  base::EscapeJSONString(e.op, /*put_in_quotes=*/true, &op);
  base::EscapeJSONString(e.path, /*put_in_quotes=*/true, &path);
  std::string line = base::StrCat({"{\"ts\":", ts,
                                    ",\"op\":", op,
                                    ",\"path\":", path});
  if (!e.detail.empty()) {
    line += ",";
    line += e.detail;
  }
  line += "}\n";
  return line;
}

}  // namespace

WorkspaceAudit::WorkspaceAudit(const base::FilePath& workspace_root,
                               size_t ring_buffer_size)
    : workspace_root_(workspace_root),
      ring_size_(ring_buffer_size) {}

WorkspaceAudit::~WorkspaceAudit() = default;

void WorkspaceAudit::Append(const AuditEntry& entry) {
  AppendWorkspaceAudit(workspace_root_, SerializeJson(entry));
  recent_.push_back(entry);
  while (recent_.size() > ring_size_) {
    recent_.pop_front();
  }
}

std::vector<AuditEntry> WorkspaceAudit::Snapshot() const {
  return std::vector<AuditEntry>(recent_.begin(), recent_.end());
}

}  // namespace dao
