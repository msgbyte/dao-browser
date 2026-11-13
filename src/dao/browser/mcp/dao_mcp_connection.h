// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_CONNECTION_H_
#define DAO_BROWSER_MCP_DAO_MCP_CONNECTION_H_

#include "dao/browser/local_ipc/dao_local_ipc_connection.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"

namespace dao {

inline constexpr size_t kDaoMcpMaxQueuedWriteBytes = kDaoMcpMaxLineBytes + 1;

class DaoMcpConnection : public DaoLocalIpcConnection {
 public:
  using DaoLocalIpcConnection::DaoLocalIpcConnection;

  void SendError(const std::optional<std::string>& id,
                 const DaoToolError& error) {
    DaoLocalIpcConnection::SendError(id, ToDaoLocalIpcError(error));
  }
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_CONNECTION_H_
