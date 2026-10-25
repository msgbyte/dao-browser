// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_HELPER_DAO_MCP_BROWSER_CLIENT_H_
#define DAO_BROWSER_MCP_HELPER_DAO_MCP_BROWSER_CLIENT_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/values.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include "base/process/process_handle.h"
#include "base/win/scoped_handle.h"
#include "net/socket/socket_descriptor.h"
#else
#include <sys/types.h>
#include "base/files/scoped_file.h"
#endif

namespace dao {

// A bounded, non-blocking client for the private browser-side MCP socket.
class DaoMcpBrowserClient {
 public:
  explicit DaoMcpBrowserClient(base::FilePath user_data_dir);
  ~DaoMcpBrowserClient();

  DaoMcpBrowserClient(const DaoMcpBrowserClient&) = delete;
  DaoMcpBrowserClient& operator=(const DaoMcpBrowserClient&) = delete;

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

#endif  // DAO_BROWSER_MCP_HELPER_DAO_MCP_BROWSER_CLIENT_H_
