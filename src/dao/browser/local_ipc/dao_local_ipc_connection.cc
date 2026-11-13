// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/local_ipc/dao_local_ipc_connection.h"

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

net::NetworkTrafficAnnotationTag DaoLocalIpcTrafficAnnotation() {
  return net::DefineNetworkTrafficAnnotation("dao_local_ipc", R"(
    semantics {
      sender: "Dao Local IPC"
      description:
        "Exchanges authenticated local requests between packaged Dao helpers "
        "and the running Dao browser process."
      trigger:
        "The user starts a configured local client or the Home editing CLI."
      data:
        "Versioned request names, arguments, results, and client metadata."
      destination: LOCAL
    }
    policy {
      cookies_allowed: NO
      setting:
        "Browser operations require the endpoint's user authorization."
      policy_exception_justification:
        "This is owner-only local IPC and never reaches a remote destination."
    })");
}

}  // namespace

DaoLocalIpcConnection::DaoLocalIpcConnection(
    std::unique_ptr<net::StreamSocket> socket,
    std::optional<base::ProcessId> verified_pid,
    RequestCallback request_callback,
    base::OnceClosure disconnect_callback,
    DaoLocalIpcConfig config)
    : config_(config),
      socket_(std::move(socket)),
      verified_pid_(verified_pid),
      request_callback_(std::move(request_callback)),
      disconnect_callback_(std::move(disconnect_callback)),
      read_buffer_(
          base::MakeRefCounted<net::IOBufferWithSize>(kReadBufferBytes)) {}

DaoLocalIpcConnection::~DaoLocalIpcConnection() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  disconnect_callback_.Reset();
  if (socket_) {
    socket_->Disconnect();
  }
}

void DaoLocalIpcConnection::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (started_ || closed_ || !socket_) {
    return;
  }
  started_ = true;
  ReadMore();
}

void DaoLocalIpcConnection::SendSuccess(std::string_view id,
                                        base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_) {
    return;
  }
  std::string serialized =
      SerializeDaoLocalIpcSuccessResponse(id, std::move(result), config_);
  if (serialized.size() > config_.max_line_bytes + 1) {
    serialized = SerializeDaoLocalIpcErrorResponse(
        std::optional<std::string>(id),
        MakeDaoLocalIpcError("INTERNAL_ERROR",
                             "The IPC response exceeds the endpoint limit."),
        config_);
  }
  if (serialized.size() > config_.max_line_bytes + 1) {
    serialized = SerializeDaoLocalIpcErrorResponse(
        std::nullopt,
        MakeDaoLocalIpcError("INTERNAL_ERROR",
                             "The IPC response exceeds the endpoint limit."),
        config_);
  }
  QueueWrite(std::move(serialized));
}

void DaoLocalIpcConnection::SendError(const std::optional<std::string>& id,
                                      const DaoLocalIpcError& error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_) {
    return;
  }
  std::string serialized =
      SerializeDaoLocalIpcErrorResponse(id, error, config_);
  if (serialized.size() > config_.max_line_bytes + 1) {
    serialized = SerializeDaoLocalIpcErrorResponse(
        id,
        MakeDaoLocalIpcError("INTERNAL_ERROR",
                             "The IPC error exceeds the endpoint limit."),
        config_);
  }
  if (serialized.size() > config_.max_line_bytes + 1) {
    serialized = SerializeDaoLocalIpcErrorResponse(
        std::nullopt,
        MakeDaoLocalIpcError("INTERNAL_ERROR",
                             "The IPC error exceeds the endpoint limit."),
        config_);
  }
  QueueWrite(std::move(serialized));
}

void DaoLocalIpcConnection::StopReading() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  read_stopped_ = true;
  received_.clear();
}

void DaoLocalIpcConnection::CloseAfterWrites() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_) {
    return;
  }
  if (!close_after_writes_) {
    close_after_writes_ = true;
    close_drain_timer_.Start(FROM_HERE, kCloseDrainTimeout,
                             base::BindOnce(&DaoLocalIpcConnection::Close,
                                            weak_factory_.GetWeakPtr()));
  }
  if (!write_buffer_ && writes_.empty()) {
    Close();
  }
}

void DaoLocalIpcConnection::Close() {
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

void DaoLocalIpcConnection::ReadMore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (closed_ || read_stopped_) {
    return;
  }
  int result = socket_->Read(read_buffer_.get(), read_buffer_->size(),
                             base::BindOnce(&DaoLocalIpcConnection::OnRead,
                                            weak_factory_.GetWeakPtr()));
  if (result != net::ERR_IO_PENDING) {
    OnRead(result);
  }
}

void DaoLocalIpcConnection::OnRead(int result) {
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
  if (received_.size() > config_.max_line_bytes) {
    SendError(std::nullopt, MakeDaoLocalIpcError(
                                "INVALID_ARGUMENT",
                                "The IPC message exceeds the endpoint limit."));
    CloseAfterWrites();
    return;
  }
  ReadMore();
}

bool DaoLocalIpcConnection::ProcessReceivedLines() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  while (true) {
    const size_t newline = received_.find('\n');
    if (newline == std::string::npos) {
      return true;
    }
    if (newline > config_.max_line_bytes) {
      SendError(
          std::nullopt,
          MakeDaoLocalIpcError("INVALID_ARGUMENT",
                               "The IPC message exceeds the endpoint limit."));
      CloseAfterWrites();
      return false;
    }

    std::string line = received_.substr(0, newline);
    received_.erase(0, newline + 1);
    auto parsed = ParseDaoLocalIpcRequestLine(line, config_);
    if (!parsed.has_value()) {
      DaoLocalIpcProtocolError error = std::move(parsed).error();
      SendError(error.id, error.error);
      CloseAfterWrites();
      return false;
    }

    base::WeakPtr<DaoLocalIpcConnection> weak_this = weak_factory_.GetWeakPtr();
    request_callback_.Run(std::move(parsed).value(), newline + 1);
    if (!weak_this || weak_this->closed_ || weak_this->close_after_writes_) {
      return false;
    }
  }
}

bool DaoLocalIpcConnection::QueueWrite(std::string serialized) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (serialized.size() > config_.max_line_bytes + 1 ||
      serialized.size() > (config_.max_line_bytes + 1) - queued_write_bytes_) {
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

void DaoLocalIpcConnection::WriteMore() {
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

    int result =
        socket_->Write(write_buffer_.get(), write_buffer_->BytesRemaining(),
                       base::BindOnce(&DaoLocalIpcConnection::OnWrite,
                                      weak_factory_.GetWeakPtr()),
                       DaoLocalIpcTrafficAnnotation());
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

void DaoLocalIpcConnection::OnWrite(int result) {
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

void DaoLocalIpcConnection::NotifyDisconnected() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!disconnect_callback_) {
    return;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, std::move(disconnect_callback_));
}

}  // namespace dao
