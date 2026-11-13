// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_WIN_UTIL_H_
#define DAO_BROWSER_MCP_DAO_MCP_WIN_UTIL_H_

#include "dao/browser/local_ipc/dao_local_ipc_win_util.h"

namespace dao {

inline base::win::ScopedHandle OpenDaoMcpPrivatePath(const base::FilePath& path,
                                                     bool directory,
                                                     bool create,
                                                     DWORD access) {
  return OpenDaoLocalIpcPrivatePath(path, directory, create, access);
}
inline bool ReadDaoMcpPrivateFile(HANDLE file,
                                  size_t max_bytes,
                                  std::string* contents) {
  return ReadDaoLocalIpcPrivateFile(file, max_bytes, contents);
}
inline bool DeleteDaoMcpPrivateFile(const base::FilePath& path) {
  return DeleteDaoLocalIpcPrivateFile(path);
}
inline bool IsDaoMcpCurrentUserProcess(base::ProcessId pid) {
  return IsDaoLocalIpcCurrentUserProcess(pid);
}
inline std::optional<base::ProcessId> GetDaoMcpLoopbackTcpOwner(
    uint16_t local_port,
    uint16_t remote_port = 0) {
  return GetDaoLocalIpcLoopbackTcpOwner(local_port, remote_port);
}

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_WIN_UTIL_H_
