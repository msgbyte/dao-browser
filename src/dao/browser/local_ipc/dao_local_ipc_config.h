// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_CONFIG_H_
#define DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_CONFIG_H_

#include <cstddef>
#include <string_view>

namespace dao {

// Endpoint settings are compile-time choices, never values supplied by a peer.
struct DaoLocalIpcConfig {
  static constexpr DaoLocalIpcConfig Home() {
    return {"HomeCLI", "home.sock", 1, 32 * 1024 * 1024};
  }

  std::string_view runtime_directory = "MCP";
  std::string_view socket_name = "mcp.sock";
  int protocol_version = 1;
  size_t max_line_bytes = 8 * 1024 * 1024;
};

inline constexpr size_t kDaoLocalIpcMaxRequestIdBytes = 1024;
inline constexpr size_t kDaoLocalIpcMaxConnections = 32;

}  // namespace dao

#endif  // DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_CONFIG_H_
