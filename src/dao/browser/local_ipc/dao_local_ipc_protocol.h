// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_PROTOCOL_H_
#define DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_PROTOCOL_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "base/types/expected.h"
#include "base/values.h"
#include "dao/browser/local_ipc/dao_local_ipc_config.h"

namespace dao {

struct DaoLocalIpcError {
  std::string code;
  std::string message;
  bool retryable = false;
};

DaoLocalIpcError MakeDaoLocalIpcError(std::string code,
                                      std::string message,
                                      bool retryable = false);

struct DaoLocalIpcProtocolError {
  DaoLocalIpcProtocolError();
  ~DaoLocalIpcProtocolError();
  DaoLocalIpcProtocolError(const DaoLocalIpcProtocolError&);
  DaoLocalIpcProtocolError& operator=(const DaoLocalIpcProtocolError&);
  DaoLocalIpcProtocolError(DaoLocalIpcProtocolError&&) noexcept;
  DaoLocalIpcProtocolError& operator=(DaoLocalIpcProtocolError&&) noexcept;

  std::optional<std::string> id;
  DaoLocalIpcError error;
};

struct DaoLocalIpcRequest {
  DaoLocalIpcRequest();
  ~DaoLocalIpcRequest();
  DaoLocalIpcRequest(const DaoLocalIpcRequest&) = delete;
  DaoLocalIpcRequest& operator=(const DaoLocalIpcRequest&) = delete;
  DaoLocalIpcRequest(DaoLocalIpcRequest&&) noexcept;
  DaoLocalIpcRequest& operator=(DaoLocalIpcRequest&&) noexcept;

  std::optional<std::string> id;
  std::string method;
  base::DictValue params;
};

base::expected<DaoLocalIpcRequest, DaoLocalIpcProtocolError>
ParseDaoLocalIpcRequestLine(std::string_view line,
                            DaoLocalIpcConfig config = {});

std::string SerializeDaoLocalIpcSuccessResponse(std::string_view id,
                                                base::DictValue result,
                                                DaoLocalIpcConfig config = {});
std::string SerializeDaoLocalIpcErrorResponse(
    const std::optional<std::string>& id,
    const DaoLocalIpcError& error,
    DaoLocalIpcConfig config = {});

}  // namespace dao

#endif  // DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_PROTOCOL_H_
