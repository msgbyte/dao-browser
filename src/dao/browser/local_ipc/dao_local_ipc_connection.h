// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_CONNECTION_H_
#define DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_CONNECTION_H_

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process_handle.h"
#include "base/sequence_checker.h"
#include "base/timer/timer.h"
#include "dao/browser/local_ipc/dao_local_ipc_protocol.h"

namespace net {
class DrainableIOBuffer;
class IOBufferWithSize;
class StreamSocket;
}  // namespace net

namespace dao {

class DaoLocalIpcConnection {
 public:
  using RequestCallback =
      base::RepeatingCallback<void(DaoLocalIpcRequest, size_t wire_bytes)>;

  DaoLocalIpcConnection(std::unique_ptr<net::StreamSocket> socket,
                        std::optional<base::ProcessId> verified_pid,
                        RequestCallback request_callback,
                        base::OnceClosure disconnect_callback,
                        DaoLocalIpcConfig config = {});
  ~DaoLocalIpcConnection();

  DaoLocalIpcConnection(const DaoLocalIpcConnection&) = delete;
  DaoLocalIpcConnection& operator=(const DaoLocalIpcConnection&) = delete;

  void Start();
  void SendSuccess(std::string_view id, base::DictValue result);
  void SendError(const std::optional<std::string>& id,
                 const DaoLocalIpcError& error);
  void StopReading();
  void CloseAfterWrites();
  void Close();

  std::optional<base::ProcessId> verified_pid() const { return verified_pid_; }
  bool is_closed() const { return closed_; }

 private:
  void ReadMore();
  void OnRead(int result);
  bool ProcessReceivedLines();
  bool QueueWrite(std::string serialized);
  void WriteMore();
  void OnWrite(int result);
  void NotifyDisconnected();

  const DaoLocalIpcConfig config_;
  std::unique_ptr<net::StreamSocket> socket_;
  const std::optional<base::ProcessId> verified_pid_;
  RequestCallback request_callback_;
  base::OnceClosure disconnect_callback_;
  scoped_refptr<net::IOBufferWithSize> read_buffer_;
  std::string received_;
  std::deque<std::string> writes_;
  scoped_refptr<net::DrainableIOBuffer> write_buffer_;
  size_t queued_write_bytes_ = 0;
  size_t current_write_bytes_ = 0;
  bool started_ = false;
  bool read_stopped_ = false;
  bool closed_ = false;
  bool close_after_writes_ = false;
  base::OneShotTimer close_drain_timer_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoLocalIpcConnection> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_CONNECTION_H_
