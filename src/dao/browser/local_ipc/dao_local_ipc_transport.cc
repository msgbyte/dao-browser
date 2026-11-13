// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/local_ipc/dao_local_ipc_transport.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "dao/browser/local_ipc/dao_local_ipc_connection.h"
#include "net/base/net_errors.h"
#include "net/socket/server_socket.h"
#include "net/socket/stream_socket.h"

#if BUILDFLAG(IS_WIN)
#include "dao/browser/local_ipc/dao_local_ipc_win_util.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/log/net_log_source.h"
#include "net/socket/tcp_server_socket.h"
#else
#include <unistd.h>
#include "net/socket/unix_domain_server_socket_posix.h"
#endif

namespace dao {
namespace {

// Hard IO-thread ceiling; endpoint services enforce admission at hello time.
// Extra accepted sockets allow services to send errors while evictions drain.
constexpr size_t kMaxConnections = kDaoLocalIpcMaxConnections * 4;
constexpr int kListenerBacklog = static_cast<int>(kDaoLocalIpcMaxConnections);
constexpr size_t kMaxPendingRequests = 64;
// Aggregate credits stay sized for admitted clients: pre-hello sockets send a
// single hello, so they never need the extra headroom.
constexpr size_t kMaxTotalPendingRequests =
    kDaoLocalIpcMaxConnections * kMaxPendingRequests;
constexpr base::TimeDelta kPendingRequestDrainTimeout = base::Seconds(1);

}  // namespace

struct DaoLocalIpcTransport::ConnectionState {
  std::unique_ptr<DaoLocalIpcConnection> connection;
  size_t pending_request_count = 0;
  size_t pending_request_bytes = 0;
  bool closing = false;
  base::OneShotTimer pending_request_drain_timer;
};

DaoLocalIpcTransport::DaoLocalIpcTransport(
    AcceptedCallback accepted_callback,
    RequestCallback request_callback,
    DisconnectedCallback disconnected_callback,
    DaoLocalIpcConfig config)
    : config_(config),
      accepted_callback_(std::move(accepted_callback)),
      request_callback_(std::move(request_callback)),
      disconnected_callback_(std::move(disconnected_callback)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

DaoLocalIpcTransport::~DaoLocalIpcTransport() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Stop();
}

base::expected<uint16_t, int> DaoLocalIpcTransport::StartListening(
    base::FilePath socket_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Stop();
  socket_path_ = std::move(socket_path);
#if BUILDFLAG(IS_WIN)
  listener_ =
      std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
  int result =
      listener_->Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0),
                        kListenerBacklog, std::nullopt);
  net::IPEndPoint address;
  if (result == net::OK) {
    result = listener_->GetLocalAddress(&address);
  }
#else
  listener_ = std::make_unique<net::UnixDomainServerSocket>(
      base::BindRepeating(
          [](base::WeakPtr<DaoLocalIpcTransport> transport,
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
      static_cast<net::UnixDomainServerSocket*>(listener_.get())
          ->BindAndListen(socket_path_.value(), kListenerBacklog);
#endif
  if (result != net::OK) {
    listener_.reset();
    socket_path_.clear();
    return base::unexpected(result);
  }
#if BUILDFLAG(IS_WIN)
  return base::ok(address.port());
#else
  return base::ok(uint16_t{0});
#endif
}

void DaoLocalIpcTransport::StartAccepting() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  AcceptNext();
}

void DaoLocalIpcTransport::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  listener_.reset();
  accepted_socket_.reset();
  connections_.clear();
  total_pending_request_count_ = 0;
  total_pending_request_bytes_ = 0;
  overflow_logged_ = false;
  accepted_verified_pid_.reset();
  socket_path_.clear();
}

void DaoLocalIpcTransport::SendSuccess(uint64_t connection_generation,
                                       std::string id,
                                       base::DictValue result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (state && !state->closing) {
    state->connection->SendSuccess(id, std::move(result));
  }
}

void DaoLocalIpcTransport::SendError(uint64_t connection_generation,
                                     std::optional<std::string> id,
                                     DaoLocalIpcError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (state && !state->closing) {
    state->connection->SendError(id, error);
  }
}

void DaoLocalIpcTransport::AcknowledgeRequest(uint64_t connection_generation,
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

void DaoLocalIpcTransport::CloseAfterWrites(uint64_t connection_generation) {
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
      base::BindOnce(&DaoLocalIpcTransport::CloseConnection,
                     weak_factory_.GetWeakPtr(), connection_generation));
}

void DaoLocalIpcTransport::CloseConnection(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (state) {
    state->connection->Close();
  }
}

#if !BUILDFLAG(IS_WIN)
bool DaoLocalIpcTransport::AuthenticatePeer(
    uid_t peer_uid,
    std::optional<base::ProcessId> peer_pid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (peer_uid != getuid()) {
    return false;
  }
  accepted_verified_pid_ = peer_pid;
  return true;
}

#endif

void DaoLocalIpcTransport::AcceptNext() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!listener_ || accepted_socket_) {
    return;
  }
  accepted_verified_pid_.reset();
  const int result = listener_->Accept(
      &accepted_socket_, base::BindOnce(&DaoLocalIpcTransport::OnAccepted,
                                        weak_factory_.GetWeakPtr()));
  if (result != net::ERR_IO_PENDING) {
    OnAccepted(result);
  }
}

void DaoLocalIpcTransport::OnAccepted(int result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!listener_) {
    accepted_socket_.reset();
    return;
  }
  if (result != net::OK || !accepted_socket_) {
    accepted_socket_.reset();
    LOG(ERROR) << "Dao local IPC failed to accept a local connection: "
               << result;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DaoLocalIpcTransport::AcceptNext,
                                  weak_factory_.GetWeakPtr()));
    return;
  }

#if BUILDFLAG(IS_WIN)
  net::IPEndPoint peer;
  net::IPEndPoint local;
  if (accepted_socket_->GetPeerAddress(&peer) == net::OK &&
      accepted_socket_->GetLocalAddress(&local) == net::OK &&
      peer.address() == net::IPAddress::IPv4Localhost()) {
    accepted_verified_pid_ =
        GetDaoLocalIpcLoopbackTcpOwner(peer.port(), local.port());
  }
  if (!accepted_verified_pid_ ||
      !IsDaoLocalIpcCurrentUserProcess(*accepted_verified_pid_)) {
    accepted_socket_->Disconnect();
    accepted_socket_.reset();
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DaoLocalIpcTransport::AcceptNext,
                                  weak_factory_.GetWeakPtr()));
    return;
  }
#endif

  if (connections_.size() >= kMaxConnections) {
    if (!overflow_logged_) {
      overflow_logged_ = true;
      LOG(ERROR) << "Dao local IPC dropped a local connection: "
                 << connections_.size() << " sockets are already open.";
    }
    accepted_socket_->Disconnect();
    accepted_socket_.reset();
    AcceptNext();
    return;
  }

  const uint64_t connection_generation = next_connection_generation_++;
  auto state = std::make_unique<ConnectionState>();
  state->connection = std::make_unique<DaoLocalIpcConnection>(
      std::move(accepted_socket_), accepted_verified_pid_,
      base::BindRepeating(&DaoLocalIpcTransport::OnRequest,
                          weak_factory_.GetWeakPtr(), connection_generation),
      base::BindOnce(&DaoLocalIpcTransport::OnConnectionClosed,
                     weak_factory_.GetWeakPtr(), connection_generation),
      config_);
  DaoLocalIpcConnection* connection = state->connection.get();
  connections_.emplace(connection_generation, std::move(state));
  accepted_callback_.Run(connection_generation, accepted_verified_pid_);
  connection->Start();
  AcceptNext();
}

void DaoLocalIpcTransport::OnRequest(uint64_t connection_generation,
                                     DaoLocalIpcRequest request,
                                     size_t wire_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ConnectionState* state = FindConnection(connection_generation);
  if (!state || state->closing) {
    return;
  }
  if (state->pending_request_count >= kMaxPendingRequests ||
      wire_bytes >
          (config_.max_line_bytes + 1) - state->pending_request_bytes ||
      total_pending_request_count_ >= kMaxTotalPendingRequests ||
      wire_bytes > (kDaoLocalIpcMaxConnections * (config_.max_line_bytes + 1)) -
                       total_pending_request_bytes_) {
    state->connection->Close();
    return;
  }
  ++state->pending_request_count;
  state->pending_request_bytes += wire_bytes;
  ++total_pending_request_count_;
  total_pending_request_bytes_ += wire_bytes;
  request_callback_.Run(connection_generation, std::move(request), wire_bytes);
}

void DaoLocalIpcTransport::OnConnectionClosed(uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto existing = connections_.find(connection_generation);
  if (existing == connections_.end()) {
    return;
  }
  total_pending_request_count_ -= existing->second->pending_request_count;
  total_pending_request_bytes_ -= existing->second->pending_request_bytes;
  std::unique_ptr<DaoLocalIpcConnection> connection =
      std::move(existing->second->connection);
  connections_.erase(existing);
  disconnected_callback_.Run(connection_generation);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce([](std::unique_ptr<DaoLocalIpcConnection>) {},
                                std::move(connection)));
}

DaoLocalIpcTransport::ConnectionState* DaoLocalIpcTransport::FindConnection(
    uint64_t connection_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto existing = connections_.find(connection_generation);
  return existing == connections_.end() ? nullptr : existing->second.get();
}

}  // namespace dao
