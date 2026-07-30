// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/helper/dao_mcp_stdio_server.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"
#include "dao/browser/mcp/helper/dao_mcp_browser_client.h"

namespace dao {
namespace {

constexpr char kMcpProtocolVersion[] = "2025-11-25";
constexpr char kCodexMcpProtocolVersion[] = "2025-06-18";
constexpr size_t kMaxQueuedStdoutBytes = kDaoMcpMaxLineBytes + 1;

bool IsSupportedProtocolVersion(std::string_view version) {
  return version == kMcpProtocolVersion ||
         version == kCodexMcpProtocolVersion;
}

bool IsValidRequestId(const base::Value* id) {
  if (!id) {
    return false;
  }
  if (const std::string* string_id = id->GetIfString()) {
    return string_id->size() <= kDaoMcpMaxRequestIdBytes;
  }
  return id->is_int();
}

std::string SerializeValue(const base::Value& value) {
  std::string serialized;
  if (!base::JSONWriter::Write(value, &serialized)) {
    return "null";
  }
  return serialized;
}

base::DictValue ErrorPayload(std::string_view code,
                             std::string_view message,
                             bool retryable) {
  return base::DictValue()
      .Set("code", code)
      .Set("message", message)
      .Set("retryable", retryable);
}

}  // namespace

DaoMcpStdioServer::PendingRequest::PendingRequest(base::Value id,
                                                  std::string method)
    : id(std::move(id)), method(std::move(method)) {}
DaoMcpStdioServer::PendingRequest::PendingRequest(PendingRequest&&) = default;
DaoMcpStdioServer::PendingRequest& DaoMcpStdioServer::PendingRequest::operator=(
    PendingRequest&&) = default;
DaoMcpStdioServer::PendingRequest::~PendingRequest() = default;

DaoMcpStdioServer::DaoMcpStdioServer(base::FilePath user_data_dir,
                                     std::string server_version)
    : user_data_dir_(std::move(user_data_dir)),
      server_version_(std::move(server_version)) {}

DaoMcpStdioServer::~DaoMcpStdioServer() = default;

int DaoMcpStdioServer::Run() {
  if (!ConfigureStdio()) {
    return 1;
  }

  while (!stdout_failed_ && (!stdin_eof_ || !stdout_writes_.empty())) {
    std::array<pollfd, 3> descriptors = {};
    descriptors[0] = {.fd = STDIN_FILENO,
                      .events = static_cast<short>(stdin_eof_ ? 0 : POLLIN),
                      .revents = 0};
    descriptors[1] = {
        .fd = STDOUT_FILENO,
        .events = static_cast<short>(stdout_writes_.empty() ? 0 : POLLOUT),
        .revents = 0};
    descriptors[2] = {.fd = browser_ && browser_->is_connected()
                                ? browser_->file_descriptor()
                                : -1,
                      .events = static_cast<short>(
                          browser_ && browser_->is_connected()
                              ? POLLIN | (browser_->wants_write() ? POLLOUT : 0)
                              : 0),
                      .revents = 0};

    int poll_result;
    do {
      poll_result = poll(descriptors.data(), descriptors.size(), -1);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result < 0) {
      return 1;
    }

    if (descriptors[0].revents & (POLLIN | POLLHUP | POLLERR)) {
      if (!ReadStdin()) {
        stdin_eof_ = true;
      }
    }
    if (descriptors[2].fd >= 0 &&
        (descriptors[2].revents & (POLLIN | POLLHUP | POLLERR))) {
      std::vector<base::DictValue> responses;
      const bool read_succeeded = browser_->ReadResponses(&responses);
      for (base::DictValue& response : responses) {
        HandleBrowserResponse(std::move(response));
      }
      if (!read_succeeded && browser_ && browser_->is_connected()) {
        FailBrowser(browser_->last_error());
      }
    }
    if (descriptors[2].fd >= 0 && (descriptors[2].revents & POLLOUT) &&
        browser_ && !browser_->FlushWrites()) {
      FailBrowser(browser_->last_error());
    }
    if ((descriptors[1].revents & POLLOUT) && !FlushStdout()) {
      return 1;
    }
    if (descriptors[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
      return 1;
    }
  }
  return stdout_failed_ ? 1 : 0;
}

bool DaoMcpStdioServer::ConfigureStdio() {
  for (int descriptor : {STDIN_FILENO, STDOUT_FILENO}) {
    const int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0) {
      return false;
    }
  }
  return true;
}

bool DaoMcpStdioServer::ReadStdin() {
  std::array<char, 32 * 1024> buffer;
  while (true) {
    const ssize_t bytes = read(STDIN_FILENO, buffer.data(), buffer.size());
    if (bytes < 0 && errno == EINTR) {
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;
    }
    if (bytes < 0) {
      return false;
    }
    if (bytes == 0) {
      if (!input_.empty()) {
        QueueError(nullptr, -32600,
                   "Each MCP message must end with a newline.");
        input_.clear();
      }
      return false;
    }
    input_.append(buffer.data(), static_cast<size_t>(bytes));
    if (!ProcessInputLines()) {
      return false;
    }
    if (input_.size() > kDaoMcpMaxLineBytes) {
      QueueError(nullptr, -32600, "The MCP message exceeds the 8 MiB limit.");
      input_.clear();
      return false;
    }
  }
}

bool DaoMcpStdioServer::ProcessInputLines() {
  while (true) {
    const size_t newline = input_.find('\n');
    if (newline == std::string::npos) {
      return true;
    }
    if (newline > kDaoMcpMaxLineBytes) {
      QueueError(nullptr, -32600, "The MCP message exceeds the 8 MiB limit.");
      input_.clear();
      return false;
    }
    std::string line = input_.substr(0, newline);
    input_.erase(0, newline + 1);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    HandleMessage(line);
    if (stdout_failed_) {
      return false;
    }
  }
}

void DaoMcpStdioServer::HandleMessage(std::string_view line) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(line, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    QueueError(nullptr, -32700, "The MCP message is not valid JSON.");
    return;
  }
  const base::DictValue& request = parsed->GetDict();
  const base::Value* request_id = request.Find("id");
  const std::string* jsonrpc = request.FindString("jsonrpc");
  const std::string* method = request.FindString("method");
  const base::DictValue* params = request.FindDict("params");
  if (!jsonrpc || *jsonrpc != "2.0" || !method ||
      (request_id && !IsValidRequestId(request_id))) {
    QueueError(IsValidRequestId(request_id) ? request_id : nullptr, -32600,
               "The MCP request envelope is invalid.");
    return;
  }

  if (*method == "initialize") {
    HandleInitialize(request, request_id);
    return;
  }
  if (*method == "notifications/initialized") {
    if (!request_id && state_ == State::kAwaitingInitializedNotification) {
      state_ = State::kReady;
    }
    return;
  }
  if (*method == "notifications/cancelled") {
    if (!request_id && params) {
      HandleCancelled(*params);
    }
    return;
  }
  if (*method == "ping" && IsValidRequestId(request_id)) {
    QueueSuccess(*request_id, base::Value(base::DictValue()));
    return;
  }
  if (state_ != State::kReady) {
    if (IsValidRequestId(request_id)) {
      QueueError(request_id, -32002,
                 "The MCP client has not completed initialization.");
    }
    return;
  }
  if (*method == "tools/list" && IsValidRequestId(request_id)) {
    HandleToolsList(*request_id);
    return;
  }
  if (*method == "tools/call" && IsValidRequestId(request_id)) {
    if (!params) {
      QueueError(request_id, -32602, "tools/call requires object parameters.");
      return;
    }
    HandleToolsCall(*request_id, *params);
    return;
  }
  if (IsValidRequestId(request_id)) {
    QueueError(request_id, -32601, "The MCP method is unsupported.");
  }
}

void DaoMcpStdioServer::HandleInitialize(const base::DictValue& request,
                                         const base::Value* request_id) {
  if (!IsValidRequestId(request_id)) {
    return;
  }
  if (state_ != State::kAwaitingInitialize) {
    QueueError(request_id, -32600, "initialize may only be called once.");
    return;
  }
  const base::DictValue* params = request.FindDict("params");
  const std::string* protocol =
      params ? params->FindString("protocolVersion") : nullptr;
  const base::DictValue* client_info =
      params ? params->FindDict("clientInfo") : nullptr;
  const base::DictValue* capabilities =
      params ? params->FindDict("capabilities") : nullptr;
  const std::string* client_name =
      client_info ? client_info->FindString("name") : nullptr;
  const std::string* client_version =
      client_info ? client_info->FindString("version") : nullptr;
  if (!protocol || !IsSupportedProtocolVersion(*protocol)) {
    QueueError(request_id, -32602, "The MCP protocol version is unsupported.",
               base::DictValue().Set("supported", kMcpProtocolVersion));
    return;
  }
  if (!capabilities || !client_name || client_name->empty() ||
      client_name->size() > 256 || !client_version || client_version->empty() ||
      client_version->size() > 256) {
    QueueError(request_id, -32602,
               "initialize requires capabilities and bounded clientInfo.");
    return;
  }
  client_name_ = *client_name;
  client_version_ = *client_version;
  state_ = State::kAwaitingInitializedNotification;
  QueueSuccess(
      *request_id,
      base::Value(
          base::DictValue()
              .Set("protocolVersion", *protocol)
              .Set("capabilities",
                   base::DictValue()
                       .Set("tools", base::DictValue())
                       .Set("experimental",
                            base::DictValue().Set(
                                "codex/tool-catalog-cache",
                                base::DictValue().Set("cacheable", false))))
              .Set("serverInfo", base::DictValue()
                                     .Set("name", "dao-browser")
                                     .Set("version", server_version_))));
}

void DaoMcpStdioServer::HandleToolsList(const base::Value& request_id) {
  if (!EnsureBrowser()) {
    QueueError(&request_id, -32000,
               "Dao Browser MCP server is disabled or unavailable.",
               ErrorPayload("MCP_DISABLED",
                            "Enable MCP Server in Dao Browser and keep the "
                            "browser running.",
                            true));
    return;
  }
  QueueBrowserRequest("ipc-" + std::to_string(next_internal_id_++),
                      "tools/list", request_id.Clone(), base::DictValue());
}

void DaoMcpStdioServer::HandleToolsCall(const base::Value& request_id,
                                        const base::DictValue& params) {
  const std::string* name = params.FindString("name");
  const base::Value* arguments_value = params.Find("arguments");
  const base::DictValue* arguments = params.FindDict("arguments");
  if (!name || name->empty() || (arguments_value && !arguments)) {
    QueueError(&request_id, -32602,
               "tools/call requires a name and optional object arguments.");
    return;
  }
  if (!EnsureBrowser()) {
    QueueToolError(request_id, "MCP_DISABLED",
                   "Enable MCP Server in Dao Browser and keep the browser "
                   "running.",
                   true);
    return;
  }
  QueueBrowserRequest("ipc-" + std::to_string(next_internal_id_++),
                      "tools/call", request_id.Clone(),
                      base::DictValue()
                          .Set("name", *name)
                          .Set("arguments", arguments ? arguments->Clone()
                                                      : base::DictValue()));
}

void DaoMcpStdioServer::HandleCancelled(const base::DictValue& params) {
  const base::Value* request_id = params.Find("requestId");
  if (!IsValidRequestId(request_id) || !browser_ || !browser_->is_connected()) {
    return;
  }
  const std::string key = IdKey(*request_id);
  for (auto pending = pending_.begin(); pending != pending_.end(); ++pending) {
    if (IdKey(pending->second.id) != key) {
      continue;
    }
    const std::string internal_id = pending->first;
    if (browser_->QueueRequest(
            base::DictValue()
                .Set("version", kDaoMcpIpcVersion)
                .Set("method", "tools/cancel")
                .Set("params",
                     base::DictValue().Set("request_id", internal_id)))) {
      pending_.erase(pending);
    } else {
      FailBrowser(browser_->last_error());
    }
    return;
  }
}

bool DaoMcpStdioServer::EnsureBrowser() {
  if (browser_ && browser_->is_connected()) {
    return true;
  }
  browser_ = std::make_unique<DaoMcpBrowserClient>(user_data_dir_);
  if (!browser_->Connect(client_name_, client_version_)) {
    LogUnavailableOnce();
    browser_.reset();
    return false;
  }
  return true;
}

bool DaoMcpStdioServer::QueueBrowserRequest(std::string internal_id,
                                            std::string method,
                                            base::Value request_id,
                                            base::DictValue params) {
  base::DictValue request = base::DictValue()
                                .Set("version", kDaoMcpIpcVersion)
                                .Set("id", internal_id)
                                .Set("method", method)
                                .Set("params", std::move(params));
  pending_.emplace(internal_id,
                   PendingRequest(std::move(request_id), std::move(method)));
  if (browser_->QueueRequest(std::move(request))) {
    return true;
  }
  FailBrowser(browser_->last_error());
  return false;
}

void DaoMcpStdioServer::HandleBrowserResponse(base::DictValue response) {
  const std::string* internal_id = response.FindString("id");
  if (!internal_id) {
    return;
  }
  if (*internal_id == "hello") {
    if (const base::DictValue* error = response.FindDict("error")) {
      FailBrowser("the browser rejected the MCP handshake", error);
    }
    return;
  }
  auto pending = pending_.find(*internal_id);
  if (pending == pending_.end()) {
    return;
  }
  PendingRequest request = std::move(pending->second);
  pending_.erase(pending);
  if (const base::DictValue* error = response.FindDict("error")) {
    const std::string* code = error->FindString("code");
    const std::string* message = error->FindString("message");
    const bool retryable = error->FindBool("retryable").value_or(false);
    if (request.method == "tools/call") {
      QueueToolError(request.id, code ? *code : "INTERNAL_ERROR",
                     message ? *message : "The browser tool call failed.",
                     retryable);
    } else {
      QueueError(&request.id, -32603,
                 message ? *message : "The browser request failed.",
                 error->Clone());
    }
    return;
  }
  const base::DictValue* result = response.FindDict("result");
  if (!result) {
    QueueError(&request.id, -32603,
               "The browser returned an invalid MCP response.");
    return;
  }
  if (request.method == "tools/list") {
    QueueSuccess(request.id, base::Value(AdaptToolList(result->Clone())));
  } else {
    QueueSuccess(request.id, base::Value(AdaptToolResult(result->Clone())));
  }
}

void DaoMcpStdioServer::FailBrowser(std::string_view reason,
                                    const base::DictValue* browser_error) {
  if (browser_) {
    browser_->Disconnect();
  }
  LogUnavailableOnce();
  if (!browser_error) {
    FailAllPending();
    return;
  }

  const std::string* code = browser_error->FindString("code");
  const std::string* message = browser_error->FindString("message");
  base::DictValue normalized_error =
      ErrorPayload(code && !code->empty() ? *code : "INTERNAL_ERROR",
                   message && !message->empty() ? *message : reason,
                   browser_error->FindBool("retryable").value_or(false));
  FailAllPending(&normalized_error);
}

void DaoMcpStdioServer::FailAllPending(const base::DictValue* browser_error) {
  const std::string* browser_code =
      browser_error ? browser_error->FindString("code") : nullptr;
  const std::string* browser_message =
      browser_error ? browser_error->FindString("message") : nullptr;
  const std::string_view code =
      browser_code ? std::string_view(*browser_code) : "MCP_DISABLED";
  const std::string_view message =
      browser_message ? std::string_view(*browser_message)
                      : "The Dao Browser MCP connection was lost.";
  const bool retryable =
      browser_error ? browser_error->FindBool("retryable").value_or(false)
                    : true;
  std::map<std::string, PendingRequest> pending = std::move(pending_);
  pending_.clear();
  for (auto& [unused, request] : pending) {
    if (request.method == "tools/call") {
      QueueToolError(request.id, code, message, retryable);
    } else {
      QueueError(&request.id, -32000, message,
                 ErrorPayload(code, message, retryable));
    }
  }
}

void DaoMcpStdioServer::QueueSuccess(const base::Value& id,
                                     base::Value result) {
  QueueStdout(base::DictValue()
                  .Set("jsonrpc", "2.0")
                  .Set("id", id.Clone())
                  .Set("result", std::move(result)));
}

void DaoMcpStdioServer::QueueError(const base::Value* id,
                                   int code,
                                   std::string_view message,
                                   base::DictValue data) {
  base::DictValue error =
      base::DictValue().Set("code", code).Set("message", message);
  if (!data.empty()) {
    error.Set("data", std::move(data));
  }
  base::DictValue response =
      base::DictValue().Set("jsonrpc", "2.0").Set("error", std::move(error));
  response.Set("id", id ? id->Clone() : base::Value());
  QueueStdout(std::move(response));
}

void DaoMcpStdioServer::QueueToolError(const base::Value& id,
                                       std::string_view code,
                                       std::string_view message,
                                       bool retryable) {
  base::DictValue error = ErrorPayload(code, message, retryable);
  base::ListValue content;
  content.Append(base::DictValue()
                     .Set("type", "text")
                     .Set("text", SerializeValue(base::Value(error.Clone()))));
  QueueSuccess(
      id, base::Value(base::DictValue()
                          .Set("content", std::move(content))
                          .Set("structuredContent",
                               base::DictValue().Set("error", error.Clone()))
                          .Set("isError", true)));
}

bool DaoMcpStdioServer::QueueStdout(base::DictValue response) {
  std::string serialized;
  if (!base::JSONWriter::Write(response, &serialized)) {
    stdout_failed_ = true;
    return false;
  }
  serialized.push_back('\n');
  if (serialized.size() > kDaoMcpMaxLineBytes + 1) {
    const base::Value* original_id = response.Find("id");
    base::Value response_id =
        original_id ? original_id->Clone() : base::Value();
    response = base::DictValue()
                   .Set("jsonrpc", "2.0")
                   .Set("id", std::move(response_id))
                   .Set("error",
                        base::DictValue()
                            .Set("code", -32603)
                            .Set("message",
                                 "The MCP response exceeds the 8 MiB limit."));
    serialized.clear();
    if (!base::JSONWriter::Write(response, &serialized)) {
      stdout_failed_ = true;
      return false;
    }
    serialized.push_back('\n');
    if (serialized.size() > kDaoMcpMaxLineBytes + 1) {
      stdout_failed_ = true;
      return false;
    }
  }
  if (serialized.size() > kMaxQueuedStdoutBytes ||
      stdout_queued_bytes_ > kMaxQueuedStdoutBytes - serialized.size()) {
    stdout_failed_ = true;
    return false;
  }
  stdout_queued_bytes_ += serialized.size();
  stdout_writes_.push_back(std::move(serialized));
  return true;
}

bool DaoMcpStdioServer::FlushStdout() {
  while (!stdout_writes_.empty()) {
    const std::string& front = stdout_writes_.front();
    const base::span<const char> remaining =
        base::span(front).subspan(stdout_write_offset_);
    const ssize_t written =
        write(STDOUT_FILENO, remaining.data(), remaining.size());
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;
    }
    if (written <= 0) {
      return false;
    }
    stdout_write_offset_ += static_cast<size_t>(written);
    stdout_queued_bytes_ -= static_cast<size_t>(written);
    if (stdout_write_offset_ == front.size()) {
      stdout_writes_.pop_front();
      stdout_write_offset_ = 0;
    }
  }
  return true;
}

void DaoMcpStdioServer::LogUnavailableOnce() {
  if (unavailable_logged_) {
    return;
  }
  unavailable_logged_ = true;
  constexpr char kMessage[] =
      "dao-mcp: Dao Browser MCP server is unavailable\n";
  std::ignore = write(STDERR_FILENO, kMessage, sizeof(kMessage) - 1);
}

base::DictValue DaoMcpStdioServer::AdaptToolList(base::DictValue result) {
  base::ListValue adapted;
  base::ListValue* tools = result.FindList("tools");
  if (!tools) {
    return base::DictValue().Set("tools", std::move(adapted));
  }
  for (base::Value& value : *tools) {
    if (!value.is_dict()) {
      continue;
    }
    base::DictValue tool = std::move(value).TakeDict();
    const std::string side_effect =
        tool.FindString("sideEffect") ? *tool.FindString("sideEffect") : "";
    tool.Remove("sideEffect");
    tool.Remove("timeoutMs");
    tool.Set("annotations",
             base::DictValue()
                 .Set("readOnlyHint", side_effect == "read")
                 .Set("destructiveHint", side_effect == "destructive")
                 .Set("idempotentHint", side_effect == "read")
                 .Set("openWorldHint", side_effect != "read"));
    adapted.Append(std::move(tool));
  }
  return base::DictValue().Set("tools", std::move(adapted));
}

base::DictValue DaoMcpStdioServer::AdaptToolResult(base::DictValue result) {
  if (!result.FindBool("ok").value_or(false)) {
    const base::DictValue* error = result.FindDict("error");
    base::DictValue payload =
        error ? error->Clone()
              : ErrorPayload("INTERNAL_ERROR", "The browser tool call failed.",
                             false);
    base::ListValue content;
    content.Append(
        base::DictValue()
            .Set("type", "text")
            .Set("text", SerializeValue(base::Value(payload.Clone()))));
    return base::DictValue()
        .Set("content", std::move(content))
        .Set("structuredContent",
             base::DictValue().Set("error", std::move(payload)))
        .Set("isError", true);
  }

  base::ListValue content;
  if (const base::DictValue* media = result.FindDict("media")) {
    const std::string* mime_type = media->FindString("mime_type");
    const std::string* data = media->FindString("data");
    if (mime_type && data) {
      content.Append(base::DictValue()
                         .Set("type", "image")
                         .Set("mimeType", *mime_type)
                         .Set("data", *data));
    }
  }
  const base::Value* data = result.Find("data");
  base::Value structured{base::DictValue()};
  if (data) {
    if (!result.FindDict("media")) {
      content.Append(base::DictValue()
                         .Set("type", "text")
                         .Set("text", SerializeValue(*data)));
    }
    structured =
        data->is_dict()
            ? data->Clone()
            : base::Value(base::DictValue().Set("result", data->Clone()));
  }
  return base::DictValue()
      .Set("content", std::move(content))
      .Set("structuredContent", std::move(structured))
      .Set("isError", false);
}

std::string DaoMcpStdioServer::IdKey(const base::Value& id) {
  return SerializeValue(id);
}

}  // namespace dao
