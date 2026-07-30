// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_connection.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/socket/stream_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace dao {
namespace {

constexpr int kReadBufferBytes = 32 * 1024;
constexpr base::TimeDelta kCloseDrainTimeout = base::Seconds(1);

net::NetworkTrafficAnnotationTag DaoMcpTrafficAnnotation() {
  return net::DefineNetworkTrafficAnnotation("dao_mcp_local_ipc", R"(
    semantics {
      sender: "Dao MCP Server"
      description:
        "Exchanges local browser automation requests between the packaged "
        "dao-mcp helper and the running Dao browser process."
      trigger:
        "The user enables MCP Server in Dao settings and starts a configured "
        "local MCP client."
      data:
        "Versioned browser tool names, arguments, results, and connection "
        "metadata selected by the local MCP client."
      destination: LOCAL
    }
    policy {
      cookies_allowed: NO
      setting:
        "The user can disable this feature with the MCP Server setting."
      policy_exception_justification:
        "This is owner-only Unix domain socket IPC and never reaches a "
        "network destination."
    })");
}

}  // namespace

DaoMcpConnection::DaoMcpConnection(std::unique_ptr<net::StreamSocket> socket,
                                   std::optional<base::ProcessId> verified_pid,
                                   RequestCallback request_callback,
                                   base::OnceClosure disconnect_callback)
    : socket_(std::move(socket)),
      verified_pid_(verified_pid),
      request_callback_(std::move(request_callback)),
      disconnect_callback_(std::move(disconnect_callback)),
      read_buffer_(
          base::MakeRefCounted<net::IOBufferWithSize>(kReadBufferBytes)) {}

DaoMcpConnection::~DaoMcpConnection() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  disconnect_callback_.Reset();
  if (socket_) {
    socket_->Disconnect();
  }
}

void DaoMcpConnection::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (started_ || closed_ || !socket_) {
    return;
  }
  started_ = true;
  ReadMore();
}

void DaoMcpConnection::SendSuccess(std::string_view id,
                                   base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_) {
    return;
  }
  std::string serialized =
      SerializeDaoMcpSuccessResponse(id, std::move(result));
  if (serialized.size() > kDaoMcpMaxLineBytes + 1) {
    serialized = SerializeDaoMcpErrorResponse(
        std::optional<std::string>(id),
        MakeDaoToolError(DaoToolErrorCode::kInternalError,
                         "The IPC response exceeds the 8 MiB limit."));
  }
  if (serialized.size() > kDaoMcpMaxLineBytes + 1) {
    serialized = SerializeDaoMcpErrorResponse(
        std::nullopt,
        MakeDaoToolError(DaoToolErrorCode::kInternalError,
                         "The IPC response exceeds the 8 MiB limit."));
  }
  QueueWrite(std::move(serialized));
}

void DaoMcpConnection::SendError(const std::optional<std::string>& id,
                                 const DaoToolError& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_) {
    return;
  }
  std::string serialized = SerializeDaoMcpErrorResponse(id, error);
  if (serialized.size() > kDaoMcpMaxLineBytes + 1) {
    serialized = SerializeDaoMcpErrorResponse(
        id, MakeDaoToolError(DaoToolErrorCode::kInternalError,
                             "The IPC error exceeds the 8 MiB limit."));
  }
  if (serialized.size() > kDaoMcpMaxLineBytes + 1) {
    serialized = SerializeDaoMcpErrorResponse(
        std::nullopt,
        MakeDaoToolError(DaoToolErrorCode::kInternalError,
                         "The IPC error exceeds the 8 MiB limit."));
  }
  QueueWrite(std::move(serialized));
}

void DaoMcpConnection::StopReading() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  read_stopped_ = true;
  received_.clear();
}

void DaoMcpConnection::CloseAfterWrites() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_) {
    return;
  }
  if (!close_after_writes_) {
    close_after_writes_ = true;
    close_drain_timer_.Start(
        FROM_HERE, kCloseDrainTimeout,
        base::BindOnce(&DaoMcpConnection::Close, weak_factory_.GetWeakPtr()));
  }
  if (!write_buffer_ && writes_.empty()) {
    Close();
  }
}

void DaoMcpConnection::Close() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_) {
    return;
  }
  closed_ = true;
  close_drain_timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  request_callback_.Reset();
  received_.clear();
  writes_.clear();
  write_buffer_.reset();
  queued_write_bytes_ = 0;
  current_write_bytes_ = 0;
  if (socket_) {
    socket_->Disconnect();
  }
  NotifyDisconnected();
}

void DaoMcpConnection::ReadMore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_ || read_stopped_) {
    return;
  }
  int result = socket_->Read(
      read_buffer_.get(), read_buffer_->size(),
      base::BindOnce(&DaoMcpConnection::OnRead, weak_factory_.GetWeakPtr()));
  if (result != net::ERR_IO_PENDING) {
    OnRead(result);
  }
}

void DaoMcpConnection::OnRead(int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_ || read_stopped_) {
    return;
  }
  if (result <= 0) {
    Close();
    return;
  }
  received_.append(read_buffer_->data(), result);
  if (!ProcessReceivedLines()) {
    return;
  }
  if (received_.size() > kDaoMcpMaxLineBytes) {
    SendError(std::nullopt,
              MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                               "The IPC message exceeds the 8 MiB limit."));
    CloseAfterWrites();
    return;
  }
  ReadMore();
}

bool DaoMcpConnection::ProcessReceivedLines() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  while (true) {
    const size_t newline = received_.find('\n');
    if (newline == std::string::npos) {
      return true;
    }
    if (newline > kDaoMcpMaxLineBytes) {
      SendError(std::nullopt,
                MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                                 "The IPC message exceeds the 8 MiB limit."));
      CloseAfterWrites();
      return false;
    }

    std::string line = received_.substr(0, newline);
    received_.erase(0, newline + 1);
    auto parsed = ParseDaoMcpRequestLine(line);
    if (!parsed.has_value()) {
      DaoMcpProtocolError error = std::move(parsed).error();
      SendError(error.id, error.error);
      CloseAfterWrites();
      return false;
    }

    base::WeakPtr<DaoMcpConnection> weak_this = weak_factory_.GetWeakPtr();
    request_callback_.Run(std::move(parsed).value(), newline + 1);
    if (!weak_this || weak_this->closed_ || weak_this->close_after_writes_) {
      return false;
    }
  }
}

bool DaoMcpConnection::QueueWrite(std::string serialized) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (serialized.size() > kDaoMcpMaxLineBytes + 1 ||
      serialized.size() > kDaoMcpMaxQueuedWriteBytes - queued_write_bytes_) {
    Close();
    return false;
  }
  queued_write_bytes_ += serialized.size();
  writes_.push_back(std::move(serialized));
  if (!write_buffer_) {
    WriteMore();
  }
  return !closed_;
}

void DaoMcpConnection::WriteMore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  while (!closed_) {
    if (!write_buffer_) {
      if (writes_.empty()) {
        if (close_after_writes_) {
          Close();
        }
        return;
      }

      std::string next = std::move(writes_.front());
      writes_.pop_front();
      current_write_bytes_ = next.size();
      auto storage = base::MakeRefCounted<net::StringIOBuffer>(std::move(next));
      write_buffer_ = base::MakeRefCounted<net::DrainableIOBuffer>(
          storage, storage->size());
    }

    int result = socket_->Write(
        write_buffer_.get(), write_buffer_->BytesRemaining(),
        base::BindOnce(&DaoMcpConnection::OnWrite, weak_factory_.GetWeakPtr()),
        DaoMcpTrafficAnnotation());
    if (result == net::ERR_IO_PENDING) {
      return;
    }
    if (result <= 0) {
      Close();
      return;
    }
    write_buffer_->DidConsume(result);
    if (write_buffer_->BytesRemaining() == 0) {
      write_buffer_.reset();
      queued_write_bytes_ -= current_write_bytes_;
      current_write_bytes_ = 0;
    }
  }
}

void DaoMcpConnection::OnWrite(int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_) {
    return;
  }
  if (result <= 0) {
    Close();
    return;
  }
  write_buffer_->DidConsume(result);
  if (write_buffer_->BytesRemaining() == 0) {
    write_buffer_.reset();
    queued_write_bytes_ -= current_write_bytes_;
    current_write_bytes_ = 0;
  }
  WriteMore();
}

void DaoMcpConnection::NotifyDisconnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!disconnect_callback_) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, std::move(disconnect_callback_));
}

}  // namespace dao
