// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_WIN_UTIL_H_
#define DAO_BROWSER_MCP_DAO_MCP_WIN_UTIL_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/process/process_handle.h"
#include "base/win/scoped_handle.h"

namespace dao {

// Rejects reparse points, hard-linked files, and any ACL except a protected
// current-user-only ACL. Handles disallow replacement while they are open.
base::win::ScopedHandle OpenDaoMcpPrivatePath(const base::FilePath& path,
                                              bool directory,
                                              bool create,
                                              DWORD access);
bool ReadDaoMcpPrivateFile(HANDLE file,
                           size_t max_bytes,
                           std::string* contents);
bool DeleteDaoMcpPrivateFile(const base::FilePath& path);
bool IsDaoMcpCurrentUserProcess(base::ProcessId pid);

// remote_port == 0 selects a listener; otherwise selects the established
// connection with this exact loopback tuple, not merely a reused port.
std::optional<base::ProcessId> GetDaoMcpLoopbackTcpOwner(
    uint16_t local_port,
    uint16_t remote_port = 0);

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_WIN_UTIL_H_
