// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_PROTOCOL_H_
#define DAO_BROWSER_MCP_DAO_MCP_PROTOCOL_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "base/types/expected.h"
#include "base/values.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

namespace dao {

inline constexpr int kDaoMcpIpcVersion = 1;
inline constexpr size_t kDaoMcpMaxLineBytes = 8 * 1024 * 1024;
inline constexpr size_t kDaoMcpMaxRequestIdBytes = 1024;

struct DaoMcpProtocolError {
  DaoMcpProtocolError();
  ~DaoMcpProtocolError();
  DaoMcpProtocolError(const DaoMcpProtocolError&);
  DaoMcpProtocolError& operator=(const DaoMcpProtocolError&);
  DaoMcpProtocolError(DaoMcpProtocolError&&) noexcept;
  DaoMcpProtocolError& operator=(DaoMcpProtocolError&&) noexcept;

  std::optional<std::string> id;
  DaoToolError error;
};

struct DaoMcpRequest {
  DaoMcpRequest();
  ~DaoMcpRequest();
  DaoMcpRequest(const DaoMcpRequest&) = delete;
  DaoMcpRequest& operator=(const DaoMcpRequest&) = delete;
  DaoMcpRequest(DaoMcpRequest&&) noexcept;
  DaoMcpRequest& operator=(DaoMcpRequest&&) noexcept;

  std::optional<std::string> id;
  std::string method;
  base::DictValue params;
};

base::expected<DaoMcpRequest, DaoMcpProtocolError> ParseDaoMcpRequestLine(
    std::string_view line);

std::string SerializeDaoMcpSuccessResponse(std::string_view id,
                                           base::DictValue result);
std::string SerializeDaoMcpErrorResponse(const std::optional<std::string>& id,
                                         const DaoToolError& error);

base::DictValue SerializeDaoBrowserToolResult(DaoBrowserToolResult result);

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_PROTOCOL_H_
