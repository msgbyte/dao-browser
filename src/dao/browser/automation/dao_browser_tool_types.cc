// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_browser_tool_types.h"

#include <utility>

namespace dao {

DaoBrowserToolCall::DaoBrowserToolCall() = default;

DaoBrowserToolCall::~DaoBrowserToolCall() = default;

DaoBrowserToolCall::DaoBrowserToolCall(DaoBrowserToolCall&&) noexcept = default;

DaoBrowserToolCall& DaoBrowserToolCall::operator=(
    DaoBrowserToolCall&&) noexcept = default;

DaoBrowserToolResult::DaoBrowserToolResult() = default;

DaoBrowserToolResult::~DaoBrowserToolResult() = default;

DaoBrowserToolResult::DaoBrowserToolResult(DaoBrowserToolResult&&) noexcept =
    default;

DaoBrowserToolResult& DaoBrowserToolResult::operator=(
    DaoBrowserToolResult&&) noexcept = default;

std::string_view DaoToolErrorCodeToString(DaoToolErrorCode code) {
  switch (code) {
    case DaoToolErrorCode::kMcpDisabled:
      return "MCP_DISABLED";
    case DaoToolErrorCode::kAuthorizationDenied:
      return "AUTHORIZATION_DENIED";
    case DaoToolErrorCode::kAuthorizationTimeout:
      return "AUTHORIZATION_TIMEOUT";
    case DaoToolErrorCode::kAgentControlBusy:
      return "AGENT_CONTROL_BUSY";
    case DaoToolErrorCode::kLeaseBusy:
      return "LEASE_BUSY";
    case DaoToolErrorCode::kTargetGone:
      return "TARGET_GONE";
    case DaoToolErrorCode::kTargetForbidden:
      return "TARGET_FORBIDDEN";
    case DaoToolErrorCode::kInvalidArgument:
      return "INVALID_ARGUMENT";
    case DaoToolErrorCode::kUnknownTool:
      return "UNKNOWN_TOOL";
    case DaoToolErrorCode::kIpcVersionUnsupported:
      return "IPC_VERSION_UNSUPPORTED";
    case DaoToolErrorCode::kDevToolsAttachFailed:
      return "DEVTOOLS_ATTACH_FAILED";
    case DaoToolErrorCode::kToolTimeout:
      return "TOOL_TIMEOUT";
    case DaoToolErrorCode::kToolCancelled:
      return "TOOL_CANCELLED";
    case DaoToolErrorCode::kInternalError:
      return "INTERNAL_ERROR";
  }
  return "INTERNAL_ERROR";
}

DaoToolError MakeDaoToolError(DaoToolErrorCode code,
                              std::string message,
                              bool retryable) {
  return {.code = code, .message = std::move(message), .retryable = retryable};
}

}  // namespace dao
