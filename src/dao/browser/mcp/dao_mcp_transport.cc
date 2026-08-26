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
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "dao/browser/mcp/dao_mcp_connection.h"
#include "net/base/net_errors.h"
#include "net/socket/stream_socket.h"
#include "net/socket/unix_domain_server_socket_posix.h"

namespace dao {
namespace {

constexpr size_t kMaxConnections = 32;
constexpr int kListenerBacklog = static_cast<int>(kMaxConnections);
constexpr size_t kMaxPendingRequests = 64;
constexpr size_t kMaxPendingRequestBytes = kDaoMcpMaxLineBytes + 1;
constexpr size_t kMaxTotalPendingRequests =
    kMaxConnections * kMaxPendingRequests;
constexpr size_t kMaxTotalPendingRequestBytes =
    kMaxConnections * kMaxPendingRequestBytes;
constexpr base::TimeDelta kPendingRequestDrainTimeout = base::Seconds(1);

}  // namespace

struct DaoMcpTransport::ConnectionState {
  std::unique_ptr<DaoMcpConnection> connection;
  size_t pending_request_count = 0;
  size_t pending_request_bytes = 0;
  bool closing = false;
  base::OneShotTimer pending_request_drain_timer;
};

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
  listener_.reset();
  accepted_socket_.reset();
  connections_.clear();
  total_pending_request_count_ = 0;
  total_pending_request_bytes_ = 0;
  accepted_verified_pid_.reset();
  socket_path_.clear();
}

void DaoMcpTransport::SendSuccess(uint64_t connection_generation,
                                  std::string id,
                                  base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (state && !state->closing) {
    state->connection->SendSuccess(id, std::move(result));
  }
}

void DaoMcpTransport::SendError(uint64_t connection_generation,
                                std::optional<std::string> id,
                                DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (state && !state->closing) {
    state->connection->SendError(id, error);
  }
}

void DaoMcpTransport::AcknowledgeRequest(uint64_t connection_generation,
                                         size_t wire_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (!state || state->pending_request_count == 0 ||
      wire_bytes > state->pending_request_bytes) {
    return;
  }
  --state->pending_request_count;
  state->pending_request_bytes -= wire_bytes;
  --total_pending_request_count_;
  total_pending_request_bytes_ -= wire_bytes;
  if (state->pending_request_count == 0 && state->closing) {
    state->pending_request_drain_timer.Stop();
    state->connection->CloseAfterWrites();
  }
}

void DaoMcpTransport::CloseAfterWrites(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (!state || state->closing) {
    return;
  }
  state->closing = true;
  state->connection->StopReading();
  if (state->pending_request_count == 0) {
    state->connection->CloseAfterWrites();
    return;
  }
  state->pending_request_drain_timer.Start(
      FROM_HERE, kPendingRequestDrainTimeout,
      base::BindOnce(&DaoMcpTransport::CloseConnection,
                     weak_factory_.GetWeakPtr(), connection_generation));
}

void DaoMcpTransport::CloseConnection(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (state) {
    state->connection->Close();
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

  if (connections_.size() >= kMaxConnections) {
    accepted_socket_->Disconnect();
    accepted_socket_.reset();
    AcceptNext();
    return;
  }

  const uint64_t connection_generation = next_connection_generation_++;
  auto state = std::make_unique<ConnectionState>();
  state->connection = std::make_unique<DaoMcpConnection>(
      std::move(accepted_socket_), accepted_verified_pid_,
      base::BindRepeating(&DaoMcpTransport::OnRequest,
                          weak_factory_.GetWeakPtr(), connection_generation),
      base::BindOnce(&DaoMcpTransport::OnConnectionClosed,
                     weak_factory_.GetWeakPtr(), connection_generation));
  DaoMcpConnection* connection = state->connection.get();
  connections_.emplace(connection_generation, std::move(state));
  accepted_callback_.Run(connection_generation, accepted_verified_pid_);
  connection->Start();
  AcceptNext();
}

void DaoMcpTransport::OnRequest(uint64_t connection_generation,
                                DaoMcpRequest request,
                                size_t wire_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (!state || state->closing) {
    return;
  }
  if (state->pending_request_count >= kMaxPendingRequests ||
      wire_bytes > kMaxPendingRequestBytes - state->pending_request_bytes ||
      total_pending_request_count_ >= kMaxTotalPendingRequests ||
      wire_bytes >
          kMaxTotalPendingRequestBytes - total_pending_request_bytes_) {
    state->connection->Close();
    return;
  }
  ++state->pending_request_count;
  state->pending_request_bytes += wire_bytes;
  ++total_pending_request_count_;
  total_pending_request_bytes_ += wire_bytes;
  request_callback_.Run(connection_generation, std::move(request), wire_bytes);
}

void DaoMcpTransport::OnConnectionClosed(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto existing = connections_.find(connection_generation);
  if (existing == connections_.end()) {
    return;
  }
  total_pending_request_count_ -= existing->second->pending_request_count;
  total_pending_request_bytes_ -= existing->second->pending_request_bytes;
  std::unique_ptr<DaoMcpConnection> connection =
      std::move(existing->second->connection);
  connections_.erase(existing);
  disconnected_callback_.Run(connection_generation);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce([](std::unique_ptr<DaoMcpConnection>) {},
                     std::move(connection)));
}

DaoMcpTransport::ConnectionState* DaoMcpTransport::FindConnection(
    uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto existing = connections_.find(connection_generation);
  return existing == connections_.end() ? nullptr : existing->second.get();
}

}  // namespace dao
