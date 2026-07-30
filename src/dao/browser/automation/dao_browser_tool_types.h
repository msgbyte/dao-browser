// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_BROWSER_TOOL_TYPES_H_
#define DAO_BROWSER_AUTOMATION_DAO_BROWSER_TOOL_TYPES_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/time/time.h"
#include "base/values.h"

namespace dao {

enum class DaoToolClient {
  kDaoAgent,
  kMcp,
};

struct DaoAgentClientId {
  DaoToolClient type;
  std::string connection_id;
  std::string display_name;
};

enum class DaoToolErrorCode {
  kMcpDisabled,
  kAuthorizationDenied,
  kAuthorizationTimeout,
  kAgentControlBusy,
  kLeaseBusy,
  kTargetGone,
  kTargetForbidden,
  kInvalidArgument,
  kUnknownTool,
  kIpcVersionUnsupported,
  kDevToolsAttachFailed,
  kToolTimeout,
  kToolCancelled,
  kInternalError,
};

std::string_view DaoToolErrorCodeToString(DaoToolErrorCode code);

struct DaoToolError {
  DaoToolErrorCode code = DaoToolErrorCode::kInternalError;
  std::string message;
  bool retryable = false;
};

DaoToolError MakeDaoToolError(DaoToolErrorCode code,
                              std::string message,
                              bool retryable = false);

struct DaoToolTarget {
  std::string tab_id;
  std::string url;
};

struct DaoToolMedia {
  std::string mime_type;
  std::string data;
};

struct DaoBrowserToolCall {
  DaoBrowserToolCall();
  ~DaoBrowserToolCall();
  DaoBrowserToolCall(const DaoBrowserToolCall&) = delete;
  DaoBrowserToolCall& operator=(const DaoBrowserToolCall&) = delete;
  DaoBrowserToolCall(DaoBrowserToolCall&&) noexcept;
  DaoBrowserToolCall& operator=(DaoBrowserToolCall&&) noexcept;

  std::string request_id;
  std::string name;
  base::DictValue arguments;
  base::TimeDelta timeout;
};

struct DaoBrowserToolResult {
  DaoBrowserToolResult();
  ~DaoBrowserToolResult();
  DaoBrowserToolResult(const DaoBrowserToolResult&) = delete;
  DaoBrowserToolResult& operator=(const DaoBrowserToolResult&) = delete;
  DaoBrowserToolResult(DaoBrowserToolResult&&) noexcept;
  DaoBrowserToolResult& operator=(DaoBrowserToolResult&&) noexcept;

  bool ok = false;
  base::Value data;
  std::optional<DaoToolMedia> media;
  std::optional<DaoToolError> error;
  std::optional<DaoToolTarget> target;
};

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_BROWSER_TOOL_TYPES_H_
