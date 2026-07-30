// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_transport.h"

#include <unistd.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "dao/browser/mcp/dao_mcp_connection.h"
#include "net/base/net_errors.h"
#include "net/socket/stream_socket.h"
#include "net/socket/unix_domain_server_socket_posix.h"

namespace dao {
namespace {

constexpr int kListenerBacklog = 1;
constexpr size_t kMaxPendingRequests = 64;
constexpr size_t kMaxPendingRequestBytes = kDaoMcpMaxLineBytes + 1;
constexpr base::TimeDelta kPendingRequestDrainTimeout = base::Seconds(1);

}  // namespace

DaoMcpTransport::DaoMcpTransport(AcceptedCallback accepted_callback,
                                 RequestCallback request_callback,
                                 DisconnectedCallback disconnected_callback)
    : accepted_callback_(std::move(accepted_callback)),
      request_callback_(std::move(request_callback)),
      disconnected_callback_(std::move(disconnected_callback)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

DaoMcpTransport::~DaoMcpTransport() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Stop();
}

int DaoMcpTransport::StartListening(base::FilePath socket_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Stop();
  socket_path_ = std::move(socket_path);
  listener_ = std::make_unique<net::UnixDomainServerSocket>(
      base::BindRepeating(
          [](base::WeakPtr<DaoMcpTransport> transport,
             const net::UnixDomainServerSocket::Credentials& credentials) {
            if (!transport) {
              return false;
            }
            std::optional<base::ProcessId> peer_pid;
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID) || \
    BUILDFLAG(IS_FUCHSIA)
            peer_pid = credentials.process_id;
#endif
            return transport->AuthenticatePeer(credentials.user_id, peer_pid);
          },
          weak_factory_.GetWeakPtr()),
      /*use_abstract_namespace=*/false);
  const int result =
      listener_->BindAndListen(socket_path_.value(), kListenerBacklog);
  if (result != net::OK) {
    listener_.reset();
    socket_path_.clear();
  }
  return result;
}

void DaoMcpTransport::StartAccepting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  AcceptNext();
}

void DaoMcpTransport::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  active_connection_generation_ = 0;
  pending_request_count_ = 0;
  pending_request_bytes_ = 0;
  connection_closing_ = false;
  pending_request_drain_timer_.Stop();
  listener_.reset();
  accepted_socket_.reset();
  if (connection_) {
    connection_->Close();
    connection_.reset();
  }
  accepted_verified_pid_.reset();
  socket_path_.clear();
}

void DaoMcpTransport::SendSuccess(uint64_t connection_generation,
                                  std::string id,
                                  base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsActiveConnection(connection_generation) && !connection_closing_) {
    connection_->SendSuccess(id, std::move(result));
  }
}

void DaoMcpTransport::SendError(uint64_t connection_generation,
                                std::optional<std::string> id,
                                DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsActiveConnection(connection_generation) && !connection_closing_) {
    connection_->SendError(id, error);
  }
}

void DaoMcpTransport::AcknowledgeRequest(uint64_t connection_generation,
                                         size_t wire_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveConnection(connection_generation) ||
      pending_request_count_ == 0 || wire_bytes > pending_request_bytes_) {
    return;
  }
  --pending_request_count_;
  pending_request_bytes_ -= wire_bytes;
  if (pending_request_count_ == 0 && connection_closing_) {
    pending_request_drain_timer_.Stop();
    connection_->CloseAfterWrites();
  }
}

void DaoMcpTransport::CloseAfterWrites(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveConnection(connection_generation) || connection_closing_) {
    return;
  }
  connection_closing_ = true;
  connection_->StopReading();
  if (pending_request_count_ == 0) {
    connection_->CloseAfterWrites();
    return;
  }
  pending_request_drain_timer_.Start(
      FROM_HERE, kPendingRequestDrainTimeout,
      base::BindOnce(&DaoMcpTransport::CloseConnection,
                     weak_factory_.GetWeakPtr(), connection_generation));
}

void DaoMcpTransport::CloseConnection(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (IsActiveConnection(connection_generation)) {
    connection_->Close();
  }
}

bool DaoMcpTransport::AuthenticatePeer(
    uid_t peer_uid,
    std::optional<base::ProcessId> peer_pid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (peer_uid != getuid()) {
    return false;
  }
  accepted_verified_pid_ = peer_pid;
  return true;
}

void DaoMcpTransport::AcceptNext() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!listener_ || accepted_socket_) {
    return;
  }
  accepted_verified_pid_.reset();
  const int result = listener_->Accept(
      &accepted_socket_,
      base::BindOnce(&DaoMcpTransport::OnAccepted, weak_factory_.GetWeakPtr()));
  if (result != net::ERR_IO_PENDING) {
    OnAccepted(result);
  }
}

void DaoMcpTransport::OnAccepted(int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!listener_) {
    accepted_socket_.reset();
    return;
  }
  if (result != net::OK || !accepted_socket_) {
    accepted_socket_.reset();
    LOG(ERROR) << "Dao MCP failed to accept a local connection: " << result;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DaoMcpTransport::AcceptNext,
                                  weak_factory_.GetWeakPtr()));
    return;
  }

  if (connection_) {
    accepted_socket_->Disconnect();
    accepted_socket_.reset();
    AcceptNext();
    return;
  }

  const uint64_t connection_generation = next_connection_generation_++;
  connection_ = std::make_unique<DaoMcpConnection>(
      std::move(accepted_socket_), accepted_verified_pid_,
      base::BindRepeating(&DaoMcpTransport::OnRequest,
                          weak_factory_.GetWeakPtr(), connection_generation),
      base::BindOnce(&DaoMcpTransport::OnConnectionClosed,
                     weak_factory_.GetWeakPtr(), connection_generation));
  active_connection_generation_ = connection_generation;
  pending_request_count_ = 0;
  pending_request_bytes_ = 0;
  connection_closing_ = false;
  pending_request_drain_timer_.Stop();
  accepted_callback_.Run(connection_generation, accepted_verified_pid_);
  connection_->Start();
  AcceptNext();
}

void DaoMcpTransport::OnRequest(uint64_t connection_generation,
                                DaoMcpRequest request,
                                size_t wire_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveConnection(connection_generation) || connection_closing_) {
    return;
  }
  if (pending_request_count_ >= kMaxPendingRequests ||
      wire_bytes > kMaxPendingRequestBytes - pending_request_bytes_) {
    connection_->Close();
    return;
  }
  ++pending_request_count_;
  pending_request_bytes_ += wire_bytes;
  request_callback_.Run(connection_generation, std::move(request), wire_bytes);
}

void DaoMcpTransport::OnConnectionClosed(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsActiveConnection(connection_generation)) {
    return;
  }
  active_connection_generation_ = 0;
  pending_request_count_ = 0;
  pending_request_bytes_ = 0;
  connection_closing_ = false;
  pending_request_drain_timer_.Stop();
  connection_.reset();
  disconnected_callback_.Run(connection_generation);
}

bool DaoMcpTransport::IsActiveConnection(uint64_t connection_generation) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return connection_ && connection_generation == active_connection_generation_;
}

}  // namespace dao
