// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_TRANSPORT_H_
#define DAO_BROWSER_MCP_DAO_MCP_TRANSPORT_H_

#include <utility>

#include "dao/browser/local_ipc/dao_local_ipc_transport.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"

namespace dao {

inline constexpr size_t kDaoMcpMaxConnections = kDaoLocalIpcMaxConnections;

class DaoMcpTransport : public DaoLocalIpcTransport {
 public:
  using DaoLocalIpcTransport::DaoLocalIpcTransport;

  void SendError(uint64_t connection_generation,
                 std::optional<std::string> id,
                 DaoToolError error) {
    DaoLocalIpcTransport::SendError(connection_generation, std::move(id),
                                    ToDaoLocalIpcError(error));
  }
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_TRANSPORT_H_
