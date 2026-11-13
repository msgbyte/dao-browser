// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_protocol.h"

#include <utility>

namespace dao {
namespace {

base::DictValue SerializeError(const DaoToolError& error) {
  return base::DictValue()
      .Set("code", std::string(DaoToolErrorCodeToString(error.code)))
      .Set("message", error.message)
      .Set("retryable", error.retryable);
}

}  // namespace

DaoLocalIpcError ToDaoLocalIpcError(const DaoToolError& error) {
  return {std::string(DaoToolErrorCodeToString(error.code)), error.message,
          error.retryable};
}

DaoMcpProtocolError::DaoMcpProtocolError() = default;
DaoMcpProtocolError::~DaoMcpProtocolError() = default;
DaoMcpProtocolError::DaoMcpProtocolError(const DaoMcpProtocolError&) = default;
DaoMcpProtocolError& DaoMcpProtocolError::operator=(
    const DaoMcpProtocolError&) = default;
DaoMcpProtocolError::DaoMcpProtocolError(DaoMcpProtocolError&&) noexcept =
    default;
DaoMcpProtocolError& DaoMcpProtocolError::operator=(
    DaoMcpProtocolError&&) noexcept = default;

base::expected<DaoMcpRequest, DaoMcpProtocolError> ParseDaoMcpRequestLine(
    std::string_view line) {
  auto parsed = ParseDaoLocalIpcRequestLine(line);
  if (parsed.has_value()) {
    return std::move(parsed).value();
  }
  DaoMcpProtocolError error;
  error.id = std::move(parsed.error().id);
  error.error =
      MakeDaoToolError(parsed.error().error.code == "IPC_VERSION_UNSUPPORTED"
                           ? DaoToolErrorCode::kIpcVersionUnsupported
                           : DaoToolErrorCode::kInvalidArgument,
                       std::move(parsed.error().error.message));
  return base::unexpected(std::move(error));
}

std::string SerializeDaoMcpSuccessResponse(std::string_view id,
                                           base::DictValue result) {
  return SerializeDaoLocalIpcSuccessResponse(id, std::move(result));
}

std::string SerializeDaoMcpErrorResponse(const std::optional<std::string>& id,
                                         const DaoToolError& error) {
  return SerializeDaoLocalIpcErrorResponse(id, ToDaoLocalIpcError(error));
}

base::DictValue SerializeDaoBrowserToolResult(DaoBrowserToolResult result) {
  base::DictValue serialized;
  serialized.Set("ok", result.ok);
  serialized.Set("data", std::move(result.data));
  if (result.media) {
    serialized.Set("media",
                   base::DictValue()
                       .Set("mime_type", std::move(result.media->mime_type))
                       .Set("data", std::move(result.media->data)));
  }
  if (result.error) {
    serialized.Set("error", SerializeError(*result.error));
  }
  if (result.target) {
    serialized.Set("target",
                   base::DictValue()
                       .Set("tab_id", std::move(result.target->tab_id))
                       .Set("url", std::move(result.target->url)));
  }
  return serialized;
}

}  // namespace dao
