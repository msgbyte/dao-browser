// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/local_ipc/dao_local_ipc_protocol.h"

#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"

namespace dao {
namespace {

DaoLocalIpcProtocolError ProtocolError(std::optional<std::string> id,
                                       std::string code,
                                       std::string message) {
  DaoLocalIpcProtocolError error;
  error.id = std::move(id);
  error.error = MakeDaoLocalIpcError(std::move(code), std::move(message));
  return error;
}

DaoLocalIpcProtocolError InvalidEnvelope(std::optional<std::string> id,
                                         std::string message) {
  return ProtocolError(std::move(id), "INVALID_ARGUMENT", std::move(message));
}

std::string SerializeResponse(base::DictValue response) {
  std::string serialized;
  CHECK(base::JSONWriter::Write(response, &serialized));
  serialized.push_back('\n');
  return serialized;
}

base::DictValue SerializeError(const DaoLocalIpcError& error) {
  return base::DictValue()
      .Set("code", error.code)
      .Set("message", error.message)
      .Set("retryable", error.retryable);
}

}  // namespace

DaoLocalIpcError MakeDaoLocalIpcError(std::string code,
                                      std::string message,
                                      bool retryable) {
  return {std::move(code), std::move(message), retryable};
}

DaoLocalIpcRequest::DaoLocalIpcRequest() = default;
DaoLocalIpcRequest::~DaoLocalIpcRequest() = default;
DaoLocalIpcRequest::DaoLocalIpcRequest(DaoLocalIpcRequest&&) noexcept = default;
DaoLocalIpcRequest& DaoLocalIpcRequest::operator=(
    DaoLocalIpcRequest&&) noexcept = default;

DaoLocalIpcProtocolError::DaoLocalIpcProtocolError() = default;
DaoLocalIpcProtocolError::~DaoLocalIpcProtocolError() = default;
DaoLocalIpcProtocolError::DaoLocalIpcProtocolError(
    const DaoLocalIpcProtocolError&) = default;
DaoLocalIpcProtocolError& DaoLocalIpcProtocolError::operator=(
    const DaoLocalIpcProtocolError&) = default;
DaoLocalIpcProtocolError::DaoLocalIpcProtocolError(
    DaoLocalIpcProtocolError&&) noexcept = default;
DaoLocalIpcProtocolError& DaoLocalIpcProtocolError::operator=(
    DaoLocalIpcProtocolError&&) noexcept = default;

base::expected<DaoLocalIpcRequest, DaoLocalIpcProtocolError>
ParseDaoLocalIpcRequestLine(std::string_view line, DaoLocalIpcConfig config) {
  if (line.empty()) {
    return base::unexpected(
        InvalidEnvelope(std::nullopt, "The IPC message is empty."));
  }
  if (line.size() > config.max_line_bytes) {
    return base::unexpected(InvalidEnvelope(
        std::nullopt, "The IPC message exceeds the endpoint limit."));
  }
  if (line.back() == '\n' || line.back() == '\r') {
    return base::unexpected(InvalidEnvelope(
        std::nullopt, "The IPC parser received an invalid line ending."));
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(line, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return base::unexpected(InvalidEnvelope(
        std::nullopt, "The IPC message must be one JSON object."));
  }

  base::DictValue root = std::move(*parsed).TakeDict();
  std::optional<std::string> request_id;
  if (const base::Value* id = root.Find("id")) {
    const std::string* id_string = id->GetIfString();
    if (!id_string || id_string->empty() ||
        id_string->size() > kDaoLocalIpcMaxRequestIdBytes) {
      return base::unexpected(InvalidEnvelope(
          std::nullopt,
          "The IPC request id must be a bounded non-empty string."));
    }
    request_id = *id_string;
  }

  const std::optional<int> version = root.FindInt("version");
  if (!version || *version != config.protocol_version) {
    return base::unexpected(
        ProtocolError(std::move(request_id), "IPC_VERSION_UNSUPPORTED",
                      "The browser IPC protocol version is unsupported."));
  }

  DaoLocalIpcRequest request;
  request.id = request_id;

  const std::string* method = root.FindString("method");
  const base::DictValue* params = root.FindDict("params");
  if (!method || method->empty() || !params) {
    return base::unexpected(InvalidEnvelope(
        std::move(request_id),
        "The IPC request requires a method and object params."));
  }
  request.method = *method;
  request.params = params->Clone();
  return request;
}

std::string SerializeDaoLocalIpcSuccessResponse(std::string_view id,
                                                base::DictValue result,
                                                DaoLocalIpcConfig config) {
  CHECK(!id.empty());
  return SerializeResponse(base::DictValue()
                               .Set("version", config.protocol_version)
                               .Set("id", id)
                               .Set("result", std::move(result)));
}

std::string SerializeDaoLocalIpcErrorResponse(
    const std::optional<std::string>& id,
    const DaoLocalIpcError& error,
    DaoLocalIpcConfig config) {
  base::DictValue response = base::DictValue()
                                 .Set("version", config.protocol_version)
                                 .Set("error", SerializeError(error));
  if (id) {
    response.Set("id", *id);
  }
  return SerializeResponse(std::move(response));
}

}  // namespace dao
