// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_CLIENT_H_
#define DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_CLIENT_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/values.h"
#include "build/build_config.h"
#include "dao/browser/local_ipc/dao_local_ipc_config.h"

#if BUILDFLAG(IS_WIN)
#include "base/process/process_handle.h"
#include "base/win/scoped_handle.h"
#include "net/socket/socket_descriptor.h"
#else
#include <sys/types.h>
#include "base/files/scoped_file.h"
#endif

namespace dao {

// A bounded, non-blocking client for the private browser socket.
class DaoLocalIpcClient {
 public:
  explicit DaoLocalIpcClient(base::FilePath user_data_dir,
                             DaoLocalIpcConfig config = {});
  ~DaoLocalIpcClient();

  DaoLocalIpcClient(const DaoLocalIpcClient&) = delete;
  DaoLocalIpcClient& operator=(const DaoLocalIpcClient&) = delete;

  bool Connect(std::string_view client_name, std::string_view client_version);
  bool QueueRequest(base::DictValue request);
  bool FlushWrites();
  bool ReadResponses(std::vector<base::DictValue>* responses);
  void Disconnect();
  bool WaitForIO(bool readable, bool writable, int timeout_ms);

#if !BUILDFLAG(IS_WIN)
  int file_descriptor() const { return socket_.get(); }
#endif
  bool is_connected() const {
#if BUILDFLAG(IS_WIN)
    return socket_ != net::kInvalidSocket;
#else
    return socket_.is_valid();
#endif
  }
  bool wants_write() const { return connecting_ || !writes_.empty(); }
  const std::string& last_error() const { return last_error_; }

 private:
  bool ReadMetadata(std::string* nonce);
  bool CompleteConnection();
  bool VerifySocketIdentity();
  bool Fail(std::string message);

  const DaoLocalIpcConfig config_;
  base::FilePath user_data_dir_;
#if BUILDFLAG(IS_WIN)
  net::SocketDescriptor socket_ = net::kInvalidSocket;
  base::win::ScopedHandle runtime_dir_handle_;
  uint16_t port_ = 0;
  base::ProcessId browser_pid_ = 0;
#else
  base::ScopedFD socket_;
  dev_t runtime_device_ = 0;
  ino_t runtime_inode_ = 0;
  dev_t socket_device_ = 0;
  ino_t socket_inode_ = 0;
#endif
  bool connecting_ = false;
  std::string received_;
  std::deque<std::string> writes_;
  size_t write_offset_ = 0;
  size_t queued_write_bytes_ = 0;
  std::string last_error_;
};

}  // namespace dao

#endif  // DAO_BROWSER_LOCAL_IPC_DAO_LOCAL_IPC_CLIENT_H_
