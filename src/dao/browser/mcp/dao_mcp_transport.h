// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_TRANSPORT_H_
#define DAO_BROWSER_MCP_DAO_MCP_TRANSPORT_H_

#include <sys/types.h>

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
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"

namespace net {
class StreamSocket;
class UnixDomainServerSocket;
}  // namespace net

namespace dao {

// Owns the Unix socket listener and accepted connections. Every method and
// callback runs on the browser IO thread.
class DaoMcpTransport {
 public:
  using AcceptedCallback =
      base::RepeatingCallback<void(uint64_t, std::optional<base::ProcessId>)>;
  using RequestCallback =
      base::RepeatingCallback<void(uint64_t, DaoMcpRequest, size_t)>;
  using DisconnectedCallback = base::RepeatingCallback<void(uint64_t)>;

  DaoMcpTransport(AcceptedCallback accepted_callback,
                  RequestCallback request_callback,
                  DisconnectedCallback disconnected_callback);
  ~DaoMcpTransport();

  DaoMcpTransport(const DaoMcpTransport&) = delete;
  DaoMcpTransport& operator=(const DaoMcpTransport&) = delete;

  int StartListening(base::FilePath socket_path);
  void StartAccepting();
  void Stop();

  void SendSuccess(uint64_t connection_generation,
                   std::string id,
                   base::DictValue result);
  void SendError(uint64_t connection_generation,
                 std::optional<std::string> id,
                 DaoToolError error);
  void AcknowledgeRequest(uint64_t connection_generation, size_t wire_bytes);
  void CloseAfterWrites(uint64_t connection_generation);
  void CloseConnection(uint64_t connection_generation);

 private:
  struct ConnectionState;

  bool AuthenticatePeer(uid_t peer_uid,
                        std::optional<base::ProcessId> peer_pid);
  void AcceptNext();
  void OnAccepted(int result);
  void OnRequest(uint64_t connection_generation,
                 DaoMcpRequest request,
                 size_t wire_bytes);
  void OnConnectionClosed(uint64_t connection_generation);
  ConnectionState* FindConnection(uint64_t connection_generation);

  AcceptedCallback accepted_callback_;
  RequestCallback request_callback_;
  DisconnectedCallback disconnected_callback_;
  base::FilePath socket_path_;
  std::unique_ptr<net::UnixDomainServerSocket> listener_;
  std::unique_ptr<net::StreamSocket> accepted_socket_;
  std::map<uint64_t, std::unique_ptr<ConnectionState>> connections_;
  std::optional<base::ProcessId> accepted_verified_pid_;
  uint64_t next_connection_generation_ = 1;
  size_t total_pending_request_count_ = 0;
  size_t total_pending_request_bytes_ = 0;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoMcpTransport> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_TRANSPORT_H_
