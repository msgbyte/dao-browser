// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_HELPER_DAO_MCP_BROWSER_CLIENT_H_
#define DAO_BROWSER_MCP_HELPER_DAO_MCP_BROWSER_CLIENT_H_

#include "dao/browser/local_ipc/dao_local_ipc_client.h"

namespace dao {

class DaoMcpBrowserClient : public DaoLocalIpcClient {
 public:
  using DaoLocalIpcClient::DaoLocalIpcClient;
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_HELPER_DAO_MCP_BROWSER_CLIENT_H_
