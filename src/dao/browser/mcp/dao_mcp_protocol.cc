// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_protocol.h"

#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"

namespace dao {
namespace {

DaoMcpProtocolError ProtocolError(std::optional<std::string> id,
                                  DaoToolErrorCode code,
                                  std::string message) {
  DaoMcpProtocolError error;
  error.id = std::move(id);
  error.error = MakeDaoToolError(code, std::move(message));
  return error;
}

DaoMcpProtocolError InvalidEnvelope(std::optional<std::string> id,
                                    std::string message) {
  return ProtocolError(std::move(id), DaoToolErrorCode::kInvalidArgument,
                       std::move(message));
}

std::string SerializeResponse(base::DictValue response) {
  std::string serialized;
  CHECK(base::JSONWriter::Write(response, &serialized));
  serialized.push_back('\n');
  return serialized;
}

base::DictValue SerializeError(const DaoToolError& error) {
  return base::DictValue()
      .Set("code", std::string(DaoToolErrorCodeToString(error.code)))
      .Set("message", error.message)
      .Set("retryable", error.retryable);
}

}  // namespace

DaoMcpRequest::DaoMcpRequest() = default;
DaoMcpRequest::~DaoMcpRequest() = default;
DaoMcpRequest::DaoMcpRequest(DaoMcpRequest&&) noexcept = default;
DaoMcpRequest& DaoMcpRequest::operator=(DaoMcpRequest&&) noexcept = default;

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
  if (line.empty()) {
    return base::unexpected(
        InvalidEnvelope(std::nullopt, "The IPC message is empty."));
  }
  if (line.size() > kDaoMcpMaxLineBytes) {
    return base::unexpected(InvalidEnvelope(
        std::nullopt, "The IPC message exceeds the 8 MiB limit."));
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
        id_string->size() > kDaoMcpMaxRequestIdBytes) {
      return base::unexpected(InvalidEnvelope(
          std::nullopt,
          "The IPC request id must be a bounded non-empty string."));
    }
    request_id = *id_string;
  }

  const std::optional<int> version = root.FindInt("version");
  if (!version || *version != kDaoMcpIpcVersion) {
    return base::unexpected(ProtocolError(
        std::move(request_id), DaoToolErrorCode::kIpcVersionUnsupported,
        "The browser IPC protocol version is unsupported."));
  }

  DaoMcpRequest request;
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

std::string SerializeDaoMcpSuccessResponse(std::string_view id,
                                           base::DictValue result) {
  CHECK(!id.empty());
  return SerializeResponse(base::DictValue()
                               .Set("version", kDaoMcpIpcVersion)
                               .Set("id", id)
                               .Set("result", std::move(result)));
}

std::string SerializeDaoMcpErrorResponse(const std::optional<std::string>& id,
                                         const DaoToolError& error) {
  base::DictValue response = base::DictValue()
                                 .Set("version", kDaoMcpIpcVersion)
                                 .Set("error", SerializeError(error));
  if (id) {
    response.Set("id", *id);
  }
  return SerializeResponse(std::move(response));
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
