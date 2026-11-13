// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_RUNTIME_FILES_H_
#define DAO_BROWSER_MCP_DAO_MCP_RUNTIME_FILES_H_

#include <utility>

#include "base/memory/scoped_refptr.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/local_ipc/dao_local_ipc_runtime_files.h"

namespace dao {

// Keep the existing ref-counted MCP API while sharing endpoint implementation.
class DaoMcpRuntimeFiles
    : public base::RefCountedThreadSafe<DaoMcpRuntimeFiles> {
 public:
  DaoMcpRuntimeFiles(base::FilePath user_data_dir, base::ProcessId browser_pid);

  DaoMcpRuntimeFiles(const DaoMcpRuntimeFiles&) = delete;
  DaoMcpRuntimeFiles& operator=(const DaoMcpRuntimeFiles&) = delete;

  base::expected<void, DaoToolError> Prepare() {
    return ConvertResult(files_->Prepare());
  }
  base::expected<void, DaoToolError> CaptureBoundSocket(uint16_t port = 0) {
    return ConvertResult(files_->CaptureBoundSocket(port));
  }
  base::expected<void, DaoToolError> Publish() {
    return ConvertResult(files_->Publish());
  }
  void Cleanup() { files_->Cleanup(); }

  const base::FilePath& runtime_dir() const { return files_->runtime_dir(); }
  const base::FilePath& socket_path() const { return files_->socket_path(); }
  const base::FilePath& metadata_path() const {
    return files_->metadata_path();
  }
  const std::string& nonce() const { return files_->nonce(); }

 private:
  friend class base::RefCountedThreadSafe<DaoMcpRuntimeFiles>;
  ~DaoMcpRuntimeFiles();

  static base::expected<void, DaoToolError> ConvertResult(
      base::expected<void, DaoLocalIpcError> result) {
    if (!result.has_value()) {
      return base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kInternalError, std::move(result.error().message),
          result.error().retryable));
    }
    return base::ok();
  }

  const scoped_refptr<DaoLocalIpcRuntimeFiles> files_;
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_RUNTIME_FILES_H_
