// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_devtools_client.h"

#include <cctype>
#include <limits>
#include <optional>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "content/public/browser/web_contents.h"

namespace dao {
namespace {

DaoToolError MakeDetachedError() {
  return MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                          "DevTools client detached.");
}

std::optional<int> FindTopLevelCommandId(base::span<const uint8_t> message) {
  size_t depth = 0;
  bool in_string = false;
  bool escaped = false;
  size_t string_start = 0;
  for (size_t index = 0; index < message.size(); ++index) {
    const char character = static_cast<char>(message[index]);
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        in_string = false;
        if (depth != 1 || index - string_start != 2 ||
            message[string_start] != 'i' || message[string_start + 1] != 'd') {
          continue;
        }
        size_t value_index = index + 1;
        while (value_index < message.size() &&
               std::isspace(static_cast<unsigned char>(message[value_index]))) {
          ++value_index;
        }
        if (value_index >= message.size() || message[value_index++] != ':') {
          continue;
        }
        while (value_index < message.size() &&
               std::isspace(static_cast<unsigned char>(message[value_index]))) {
          ++value_index;
        }
        int command_id = 0;
        const size_t digits_start = value_index;
        while (value_index < message.size() &&
               std::isdigit(static_cast<unsigned char>(message[value_index]))) {
          const int digit = message[value_index++] - '0';
          if (command_id > (std::numeric_limits<int>::max() - digit) / 10) {
            return std::nullopt;
          }
          command_id = command_id * 10 + digit;
        }
        if (value_index > digits_start) {
          return command_id;
        }
      }
      continue;
    }
    if (character == '"') {
      in_string = true;
      string_start = index + 1;
    } else if (character == '{' || character == '[') {
      ++depth;
    } else if ((character == '}' || character == ']') && depth > 0) {
      --depth;
    }
  }
  return std::nullopt;
}

}  // namespace

DaoDevToolsClient::PendingCommand::PendingCommand(
    ResponseCallback callback,
    std::optional<size_t> max_response_bytes)
    : callback(std::move(callback)),
      max_response_bytes(std::move(max_response_bytes)) {}

DaoDevToolsClient::PendingCommand::PendingCommand(PendingCommand&&) = default;

DaoDevToolsClient::PendingCommand& DaoDevToolsClient::PendingCommand::operator=(
    PendingCommand&&) = default;

DaoDevToolsClient::PendingCommand::~PendingCommand() = default;

DaoDevToolsClient::DaoDevToolsClient() = default;

DaoDevToolsClient::~DaoDevToolsClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_shutting_down_ = true;
  DetachInternal();
}

bool DaoDevToolsClient::AttachTo(content::WebContents* web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutting_down_ || is_transitioning_ || !web_contents) {
    return false;
  }

  scoped_refptr<content::DevToolsAgentHost> host =
      content::DevToolsAgentHost::GetOrCreateFor(web_contents);
  if (!host) {
    return false;
  }

  if (agent_host_ == host) {
    return true;
  }

  is_transitioning_ = true;
  base::WeakPtr<DaoDevToolsClient> weak_this = weak_factory_.GetWeakPtr();
  if (!DetachInternal() || !weak_this) {
    return false;
  }

  weak_this->agent_host_ = host;
  const bool attached = host->AttachClient(weak_this.get());
  if (!weak_this) {
    return false;
  }
  if (!attached) {
    if (weak_this->agent_host_ == host) {
      weak_this->agent_host_ = nullptr;
    }
    weak_this->is_transitioning_ = false;
    return false;
  }

  const bool owns_host = weak_this->agent_host_ == host;
  weak_this->is_transitioning_ = false;
  return owns_host;
}

void DaoDevToolsClient::Detach() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_transitioning_) {
    return;
  }
  is_transitioning_ = true;
  base::WeakPtr<DaoDevToolsClient> weak_this = weak_factory_.GetWeakPtr();
  if (DetachInternal() && weak_this) {
    weak_this->is_transitioning_ = false;
  }
}

bool DaoDevToolsClient::DetachInternal() {
  base::WeakPtr<DaoDevToolsClient> weak_this = weak_factory_.GetWeakPtr();
  scoped_refptr<content::DevToolsAgentHost> host = std::move(agent_host_);
  if (host) {
    host->DetachClient(this);
    if (!weak_this) {
      return false;
    }
  }

  weak_this->CancelAll(MakeDetachedError());
  return !!weak_this;
}

int DaoDevToolsClient::SendCommand(const std::string& method,
                                   base::DictValue params,
                                   ResponseCallback callback,
                                   std::optional<size_t> max_response_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_cancelling_ || is_transitioning_) {
    std::move(callback).Run(base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "DevTools client is changing target state.")));
    return 0;
  }
  if (!agent_host_) {
    DaoToolError error =
        is_shutting_down_
            ? MakeDetachedError()
            : MakeDaoToolError(DaoToolErrorCode::kDevToolsAttachFailed,
                               "DevTools client is not attached to a target.");
    std::move(callback).Run(base::unexpected(std::move(error)));
    return 0;
  }

  const int command_id = next_command_id_++;
  pending_commands_.emplace(
      command_id,
      PendingCommand{std::move(callback), std::move(max_response_bytes)});

  base::DictValue command;
  command.Set("id", command_id);
  command.Set("method", method);
  command.Set("params", std::move(params));

  std::string json;
  base::JSONWriter::Write(command, &json);
  scoped_refptr<content::DevToolsAgentHost> host = agent_host_;
  base::WeakPtr<DaoDevToolsClient> weak_this = weak_factory_.GetWeakPtr();
  if (command_callback_for_testing_) {
    command_callback_for_testing_.Run(method);
    // The testing callback can exercise synchronous Stop/Disable paths. Those
    // paths cancel this command and detach the client without necessarily
    // destroying this object, so weak-pointer validity alone is insufficient.
    if (!weak_this || !pending_commands_.contains(command_id) ||
        agent_host_ != host) {
      return 0;
    }
  }
  host->DispatchProtocolMessage(weak_this.get(), base::as_byte_span(json));
  return command_id;
}

void DaoDevToolsClient::CancelCommand(int command_id, DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_commands_.find(command_id);
  if (it == pending_commands_.end()) {
    return;
  }

  ResponseCallback callback = std::move(it->second.callback);
  pending_commands_.erase(it);
  std::move(callback).Run(base::unexpected(std::move(error)));
}

void DaoDevToolsClient::CancelAll(DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_cancelling_) {
    return;
  }
  is_cancelling_ = true;
  base::WeakPtr<DaoDevToolsClient> weak_this = weak_factory_.GetWeakPtr();
  std::map<int, PendingCommand> commands;
  commands.swap(pending_commands_);
  for (auto& entry : commands) {
    std::move(entry.second.callback).Run(base::unexpected(error));
  }
  if (weak_this) {
    weak_this->is_cancelling_ = false;
  }
}

void DaoDevToolsClient::SetEventCallback(EventCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  event_callback_ = std::move(callback);
}

void DaoDevToolsClient::SetCommandCallbackForTesting(
    CommandCallbackForTesting callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  command_callback_for_testing_ = std::move(callback);
}

void DaoDevToolsClient::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    base::span<const uint8_t> message) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (agent_host_.get() != agent_host) {
    return;
  }

  std::optional<int> command_id = FindTopLevelCommandId(message);
  if (command_id) {
    auto pending = pending_commands_.find(*command_id);
    if (pending != pending_commands_.end() &&
        pending->second.max_response_bytes &&
        message.size() > *pending->second.max_response_bytes) {
      ResponseCallback callback = std::move(pending->second.callback);
      pending_commands_.erase(pending);
      std::move(callback).Run(base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kInvalidArgument,
          "DevTools protocol response exceeds the command response limit.")));
      return;
    }
  }

  std::string json(reinterpret_cast<const char*>(message.data()),
                   message.size());
  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return;
  }

  base::DictValue& dict = parsed->GetDict();
  command_id = dict.FindInt("id");
  if (!command_id.has_value()) {
    if (event_callback_) {
      const std::string* method = dict.FindString("method");
      const base::DictValue* params = dict.FindDict("params");
      if (method) {
        base::DictValue empty_params;
        event_callback_.Run(agent_host, *method,
                            params ? *params : empty_params);
      }
    }
    return;
  }

  auto it = pending_commands_.find(*command_id);
  if (it == pending_commands_.end()) {
    return;
  }

  ResponseCallback callback = std::move(it->second.callback);
  pending_commands_.erase(it);

  const base::Value* error = dict.Find("error");
  const base::Value* result = dict.Find("result");
  if (error) {
    std::string error_message;
    if (error->is_dict()) {
      const std::string* protocol_error_message =
          error->GetDict().FindString("message");
      if (protocol_error_message) {
        error_message = *protocol_error_message;
      }
    }
    if (error_message.empty()) {
      base::JSONWriter::Write(*error, &error_message);
    }
    std::move(callback).Run(base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kInternalError,
                         "DevTools protocol error: " + error_message)));
  } else if (result) {
    std::optional<base::Value> extracted_result = dict.Extract("result");
    std::move(callback).Run(std::move(*extracted_result));
  } else {
    std::move(callback).Run(base::Value());
  }
}

void DaoDevToolsClient::AgentHostClosed(
    content::DevToolsAgentHost* agent_host) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (agent_host_.get() != agent_host) {
    return;
  }

  agent_host_ = nullptr;
  CancelAll(MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                             "DevTools target closed."));
}

bool DaoDevToolsClient::IsTrusted() {
  return true;
}

std::string DaoDevToolsClient::GetTypeForMetrics() {
  return "Other";
}

}  // namespace dao
