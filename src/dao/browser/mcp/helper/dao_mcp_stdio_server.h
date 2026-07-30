// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_HELPER_DAO_MCP_STDIO_SERVER_H_
#define DAO_BROWSER_MCP_HELPER_DAO_MCP_STDIO_SERVER_H_

#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/values.h"

namespace dao {

class DaoMcpBrowserClient;

class DaoMcpStdioServer {
 public:
  DaoMcpStdioServer(base::FilePath user_data_dir, std::string server_version);
  ~DaoMcpStdioServer();

  DaoMcpStdioServer(const DaoMcpStdioServer&) = delete;
  DaoMcpStdioServer& operator=(const DaoMcpStdioServer&) = delete;

  int Run();

 private:
  enum class State {
    kAwaitingInitialize,
    kAwaitingInitializedNotification,
    kReady,
  };

  struct PendingRequest {
    PendingRequest(base::Value id, std::string method);
    PendingRequest(PendingRequest&&);
    PendingRequest& operator=(PendingRequest&&);
    ~PendingRequest();

    base::Value id;
    std::string method;
  };

  bool ConfigureStdio();
  bool ReadStdin();
  bool ProcessInputLines();
  void HandleMessage(std::string_view line);
  void HandleInitialize(const base::DictValue& request,
                        const base::Value* request_id);
  void HandleToolsList(const base::Value& request_id);
  void HandleToolsCall(const base::Value& request_id,
                       const base::DictValue& params);
  void HandleCancelled(const base::DictValue& params);

  bool EnsureBrowser();
  bool QueueBrowserRequest(std::string internal_id,
                           std::string method,
                           base::Value request_id,
                           base::DictValue params);
  void HandleBrowserResponse(base::DictValue response);
  void FailBrowser(std::string_view reason,
                   const base::DictValue* browser_error = nullptr);
  void FailAllPending(const base::DictValue* browser_error = nullptr);

  void QueueSuccess(const base::Value& id, base::Value result);
  void QueueError(const base::Value* id,
                  int code,
                  std::string_view message,
                  base::DictValue data = {});
  void QueueToolError(const base::Value& id,
                      std::string_view code,
                      std::string_view message,
                      bool retryable);
  bool QueueStdout(base::DictValue response);
  bool FlushStdout();
  void LogUnavailableOnce();

  base::DictValue AdaptToolList(base::DictValue result);
  base::DictValue AdaptToolResult(base::DictValue result);
  static std::string IdKey(const base::Value& id);

  base::FilePath user_data_dir_;
  std::string server_version_;
  State state_ = State::kAwaitingInitialize;
  std::string client_name_;
  std::string client_version_;
  std::string input_;
  bool stdin_eof_ = false;
  bool unavailable_logged_ = false;
  uint64_t next_internal_id_ = 1;

  std::unique_ptr<DaoMcpBrowserClient> browser_;
  std::map<std::string, PendingRequest> pending_;
  std::deque<std::string> stdout_writes_;
  size_t stdout_write_offset_ = 0;
  size_t stdout_queued_bytes_ = 0;
  bool stdout_failed_ = false;
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_HELPER_DAO_MCP_STDIO_SERVER_H_
