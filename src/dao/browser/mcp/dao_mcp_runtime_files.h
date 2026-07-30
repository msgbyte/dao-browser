// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_RUNTIME_FILES_H_
#define DAO_BROWSER_MCP_DAO_MCP_RUNTIME_FILES_H_

#include <sys/stat.h>

#include <cstdint>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/memory/ref_counted.h"
#include "base/process/process_handle.h"
#include "base/types/expected.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

namespace dao {

class DaoMcpRuntimeFiles
    : public base::RefCountedThreadSafe<DaoMcpRuntimeFiles> {
 public:
  DaoMcpRuntimeFiles(base::FilePath user_data_dir, base::ProcessId browser_pid);

  DaoMcpRuntimeFiles(const DaoMcpRuntimeFiles&) = delete;
  DaoMcpRuntimeFiles& operator=(const DaoMcpRuntimeFiles&) = delete;

  base::expected<void, DaoToolError> Prepare();
  base::expected<void, DaoToolError> CaptureBoundSocket();
  base::expected<void, DaoToolError> Publish();
  void Cleanup();

  const base::FilePath& runtime_dir() const { return runtime_dir_; }
  const base::FilePath& socket_path() const { return socket_path_; }
  const base::FilePath& metadata_path() const { return metadata_path_; }
  const std::string& nonce() const { return nonce_; }

 private:
  friend class base::RefCountedThreadSafe<DaoMcpRuntimeFiles>;

  ~DaoMcpRuntimeFiles();
  DaoToolError FileError(std::string message) const;
  bool OpenAndSecureRuntimeDirectory(bool create_if_missing);
  bool IsPinnedRuntimeDirectoryCurrent() const;
  bool RemoveOwnedLeaf(const char* name, bool expect_socket);
  bool VerifySocket(struct stat* socket_stat) const;

  base::FilePath runtime_dir_;
  base::FilePath socket_path_;
  base::FilePath metadata_path_;
  base::ProcessId browser_pid_;
  std::string nonce_;
  base::ScopedFD runtime_dir_fd_;
  std::optional<uint64_t> socket_device_;
  std::optional<uint64_t> socket_inode_;
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_RUNTIME_FILES_H_
