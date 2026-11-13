// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_TRANSPORT_H_
#define DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_TRANSPORT_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process_handle.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "build/build_config.h"

#if !BUILDFLAG(IS_WIN)
#include <sys/types.h>
#endif
#include "dao/browser/local_ipc/dao_local_ipc_protocol.h"

namespace net {
class StreamSocket;
class ServerSocket;
}  // namespace net

namespace dao {

// Owns the local socket listener and accepted connections. Every method and
// callback runs on the browser IO thread.
class DaoLocalIpcTransport {
 public:
  using AcceptedCallback =
      base::RepeatingCallback<void(uint64_t, std::optional<base::ProcessId>)>;
  using RequestCallback =
      base::RepeatingCallback<void(uint64_t, DaoLocalIpcRequest, size_t)>;
  using DisconnectedCallback = base::RepeatingCallback<void(uint64_t)>;

  DaoLocalIpcTransport(AcceptedCallback accepted_callback,
                       RequestCallback request_callback,
                       DisconnectedCallback disconnected_callback,
                       DaoLocalIpcConfig config = {});
  ~DaoLocalIpcTransport();

  DaoLocalIpcTransport(const DaoLocalIpcTransport&) = delete;
  DaoLocalIpcTransport& operator=(const DaoLocalIpcTransport&) = delete;

  base::expected<uint16_t, int> StartListening(base::FilePath socket_path);
  void StartAccepting();
  void Stop();

  void SendSuccess(uint64_t connection_generation,
                   std::string id,
                   base::DictValue result);
  void SendError(uint64_t connection_generation,
                 std::optional<std::string> id,
                 DaoLocalIpcError error);
  void AcknowledgeRequest(uint64_t connection_generation, size_t wire_bytes);
  void CloseAfterWrites(uint64_t connection_generation);
  void CloseConnection(uint64_t connection_generation);

 private:
  struct ConnectionState;

#if !BUILDFLAG(IS_WIN)
  bool AuthenticatePeer(uid_t peer_uid,
                        std::optional<base::ProcessId> peer_pid);
#endif
  void AcceptNext();
  void OnAccepted(int result);
  void OnRequest(uint64_t connection_generation,
                 DaoLocalIpcRequest request,
                 size_t wire_bytes);
  void OnConnectionClosed(uint64_t connection_generation);
  ConnectionState* FindConnection(uint64_t connection_generation);

  const DaoLocalIpcConfig config_;
  AcceptedCallback accepted_callback_;
  RequestCallback request_callback_;
  DisconnectedCallback disconnected_callback_;
  base::FilePath socket_path_;
  std::unique_ptr<net::ServerSocket> listener_;
  std::unique_ptr<net::StreamSocket> accepted_socket_;
  std::map<uint64_t, std::unique_ptr<ConnectionState>> connections_;
  std::optional<base::ProcessId> accepted_verified_pid_;
  uint64_t next_connection_generation_ = 1;
  size_t total_pending_request_count_ = 0;
  size_t total_pending_request_bytes_ = 0;
  bool overflow_logged_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoLocalIpcTransport> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_TRANSPORT_H_
