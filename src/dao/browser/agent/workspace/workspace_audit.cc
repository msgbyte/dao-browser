// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_audit.h"

#include <string_view>

#include "base/files/file_util.h"
#include "base/json/string_escape.h"
#include "base/strings/strcat.h"

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
    : log_path_(workspace_root.AppendASCII(".audit.log")),
      ring_size_(ring_buffer_size) {}

WorkspaceAudit::~WorkspaceAudit() = default;

void WorkspaceAudit::Append(const AuditEntry& entry) {
  const std::string line = SerializeJson(entry);
  // base::AppendToFile() does not create the file if it doesn't exist.
  // Ensure the parent directory + an empty file are in place first.
  if (!base::PathExists(log_path_)) {
    base::CreateDirectory(log_path_.DirName());
    base::WriteFile(log_path_, std::string_view());
  }
  base::AppendToFile(log_path_, line);
  recent_.push_back(entry);
  while (recent_.size() > ring_size_) {
    recent_.pop_front();
  }
}

std::vector<AuditEntry> WorkspaceAudit::Snapshot() const {
  return std::vector<AuditEntry>(recent_.begin(), recent_.end());
}

}  // namespace dao
