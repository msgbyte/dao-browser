// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/helper/dao_mcp_browser_client.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <utility>

#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"

namespace dao {
namespace {

constexpr size_t kMaxQueuedWriteBytes = kDaoMcpMaxLineBytes + 1;
constexpr size_t kMaxMetadataBytes = 64 * 1024;

bool IsHexNonce(std::string_view nonce) {
  if (nonce.size() != 64) {
    return false;
  }
  for (char character : nonce) {
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

}  // namespace

DaoMcpBrowserClient::DaoMcpBrowserClient(base::FilePath user_data_dir)
    : user_data_dir_(std::move(user_data_dir)) {}

DaoMcpBrowserClient::~DaoMcpBrowserClient() = default;

bool DaoMcpBrowserClient::Connect(std::string_view client_name,
                                  std::string_view client_version) {
  Disconnect();
  std::string socket_path;
  std::string nonce;
  if (!ReadMetadata(&socket_path, &nonce)) {
    return false;
  }

  base::ScopedFD socket_fd(socket(AF_UNIX, SOCK_STREAM, 0));
  if (!socket_fd.is_valid()) {
    return Fail("could not create the browser socket");
  }
  const int flags = fcntl(socket_fd.get(), F_GETFL);
  if (flags < 0 || fcntl(socket_fd.get(), F_SETFL, flags | O_NONBLOCK) != 0) {
    return Fail("could not configure the browser socket");
  }
  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  if (socket_path.size() >= sizeof(address.sun_path)) {
    return Fail("the browser socket path is too long");
  }
  base::span(address.sun_path).copy_prefix_from(socket_path);
  int result;
  do {
    result = connect(socket_fd.get(), reinterpret_cast<sockaddr*>(&address),
                     sizeof(address));
  } while (result < 0 && errno == EINTR);
  if (result != 0 && errno != EINPROGRESS) {
    return Fail("could not connect to the running browser");
  }
  connecting_ = result != 0;
  if (!connecting_ && !VerifySocketIdentity(base::FilePath(socket_path))) {
    return false;
  }
  socket_ = std::move(socket_fd);

  return QueueRequest(
      base::DictValue()
          .Set("version", kDaoMcpIpcVersion)
          .Set("id", "hello")
          .Set("method", "hello")
          .Set("params",
               base::DictValue()
                   .Set("nonce", std::move(nonce))
                   .Set("client", base::DictValue()
                                      .Set("name", client_name)
                                      .Set("version", client_version))));
}

bool DaoMcpBrowserClient::QueueRequest(base::DictValue request) {
  if (!socket_.is_valid()) {
    return Fail("the browser socket is not connected");
  }
  std::string serialized;
  if (!base::JSONWriter::Write(request, &serialized)) {
    return Fail("could not serialize a browser request");
  }
  serialized.push_back('\n');
  if (serialized.size() > kDaoMcpMaxLineBytes + 1 ||
      serialized.size() > kMaxQueuedWriteBytes - queued_write_bytes_) {
    return Fail("the browser write queue limit was exceeded");
  }
  queued_write_bytes_ += serialized.size();
  writes_.push_back(std::move(serialized));
  return true;
}

bool DaoMcpBrowserClient::FlushWrites() {
  if (connecting_) {
    if (!CompleteConnection()) {
      return false;
    }
    if (connecting_) {
      return true;
    }
  }
  while (!writes_.empty()) {
    const std::string& front = writes_.front();
    const base::span<const char> remaining =
        base::span(front).subspan(write_offset_);
    const ssize_t written =
        write(socket_.get(), remaining.data(), remaining.size());
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;
    }
    if (written <= 0) {
      return Fail("the browser socket closed while writing");
    }
    write_offset_ += static_cast<size_t>(written);
    queued_write_bytes_ -= static_cast<size_t>(written);
    if (write_offset_ == front.size()) {
      writes_.pop_front();
      write_offset_ = 0;
    }
  }
  return true;
}

bool DaoMcpBrowserClient::ReadResponses(
    std::vector<base::DictValue>* responses) {
  if (!responses) {
    return Fail("the browser response destination is missing");
  }
  if (connecting_) {
    if (!CompleteConnection()) {
      return false;
    }
    if (connecting_) {
      return true;
    }
  }
  std::array<char, 32 * 1024> buffer;
  bool disconnected = false;
  while (true) {
    const ssize_t bytes = read(socket_.get(), buffer.data(), buffer.size());
    if (bytes < 0 && errno == EINTR) {
      continue;
    }
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    if (bytes == 0) {
      disconnected = true;
      break;
    }
    if (bytes < 0) {
      return Fail("could not read from the browser socket");
    }
    received_.append(buffer.data(), static_cast<size_t>(bytes));
    if (received_.size() > kDaoMcpMaxLineBytes &&
        received_.find('\n') == std::string::npos) {
      return Fail("a browser response exceeded the 8 MiB limit");
    }
  }

  while (true) {
    const size_t newline = received_.find('\n');
    if (newline == std::string::npos) {
      break;
    }
    if (newline > kDaoMcpMaxLineBytes) {
      return Fail("a browser response exceeded the 8 MiB limit");
    }
    std::string line = received_.substr(0, newline);
    received_.erase(0, newline + 1);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      return Fail("the browser returned an invalid response");
    }
    base::DictValue response = std::move(*parsed).TakeDict();
    const std::optional<int> version = response.FindInt("version");
    const std::string* id = response.FindString("id");
    const bool has_result = response.FindDict("result") != nullptr;
    const bool has_error = response.FindDict("error") != nullptr;
    if (!version || *version != kDaoMcpIpcVersion || !id || id->empty() ||
        id->size() > kDaoMcpMaxRequestIdBytes || has_result == has_error) {
      return Fail("the browser returned an invalid response envelope");
    }
    responses->push_back(std::move(response));
  }
  return disconnected ? Fail("the browser socket disconnected") : true;
}

void DaoMcpBrowserClient::Disconnect() {
  socket_.reset();
  connecting_ = false;
  runtime_device_ = 0;
  runtime_inode_ = 0;
  socket_device_ = 0;
  socket_inode_ = 0;
  received_.clear();
  writes_.clear();
  write_offset_ = 0;
  queued_write_bytes_ = 0;
}

bool DaoMcpBrowserClient::ReadMetadata(std::string* socket_path,
                                       std::string* nonce) {
  const base::FilePath runtime_dir = user_data_dir_.AppendASCII("MCP");
  const base::FilePath expected_socket = runtime_dir.AppendASCII("mcp.sock");
  base::ScopedFD runtime_fd(
      open(runtime_dir.value().c_str(),
           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  struct stat runtime_stat = {};
  if (!runtime_fd.is_valid() || fstat(runtime_fd.get(), &runtime_stat) != 0 ||
      !S_ISDIR(runtime_stat.st_mode) || runtime_stat.st_uid != getuid() ||
      (runtime_stat.st_mode & 0777) != 0700) {
    return Fail("the private MCP runtime directory is unavailable");
  }

  base::ScopedFD metadata_fd(openat(runtime_fd.get(), "runtime.json",
                                    O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  struct stat metadata_stat = {};
  if (!metadata_fd.is_valid() ||
      fstat(metadata_fd.get(), &metadata_stat) != 0 ||
      !S_ISREG(metadata_stat.st_mode) || metadata_stat.st_uid != getuid() ||
      (metadata_stat.st_mode & 0777) != 0600 || metadata_stat.st_size < 0 ||
      static_cast<size_t>(metadata_stat.st_size) > kMaxMetadataBytes) {
    return Fail("the private MCP runtime metadata is unavailable");
  }

  std::string contents;
  std::array<char, 4096> buffer;
  while (contents.size() <= kMaxMetadataBytes) {
    const ssize_t bytes = read(metadata_fd.get(), buffer.data(), buffer.size());
    if (bytes < 0 && errno == EINTR) {
      continue;
    }
    if (bytes < 0) {
      return Fail("could not read private MCP runtime metadata");
    }
    if (bytes == 0) {
      break;
    }
    contents.append(buffer.data(), static_cast<size_t>(bytes));
  }
  if (contents.size() > kMaxMetadataBytes) {
    return Fail("private MCP runtime metadata is too large");
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(contents, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return Fail("private MCP runtime metadata is invalid");
  }
  const base::DictValue& metadata = parsed->GetDict();
  const std::string* parsed_socket_path = metadata.FindString("socket_path");
  const std::string* parsed_nonce = metadata.FindString("nonce");
  if (metadata.FindInt("version") != kDaoMcpIpcVersion ||
      !metadata.FindInt("browser_pid") || !parsed_socket_path ||
      *parsed_socket_path != expected_socket.value() || !parsed_nonce ||
      !IsHexNonce(*parsed_nonce)) {
    return Fail("private MCP runtime metadata has an unsupported shape");
  }

  struct stat socket_stat = {};
  struct stat current_runtime_stat = {};
  if (fstatat(runtime_fd.get(), "mcp.sock", &socket_stat,
              AT_SYMLINK_NOFOLLOW) != 0 ||
      !S_ISSOCK(socket_stat.st_mode) || socket_stat.st_uid != getuid() ||
      (socket_stat.st_mode & 0777) != 0600 ||
      lstat(runtime_dir.value().c_str(), &current_runtime_stat) != 0 ||
      !S_ISDIR(current_runtime_stat.st_mode) ||
      current_runtime_stat.st_dev != runtime_stat.st_dev ||
      current_runtime_stat.st_ino != runtime_stat.st_ino) {
    return Fail("the private MCP browser socket is unavailable");
  }
  runtime_device_ = runtime_stat.st_dev;
  runtime_inode_ = runtime_stat.st_ino;
  socket_device_ = socket_stat.st_dev;
  socket_inode_ = socket_stat.st_ino;
  *socket_path = *parsed_socket_path;
  *nonce = *parsed_nonce;
  return true;
}

bool DaoMcpBrowserClient::CompleteConnection() {
  int socket_error = 0;
  socklen_t error_size = sizeof(socket_error);
  if (getsockopt(socket_.get(), SOL_SOCKET, SO_ERROR, &socket_error,
                 &error_size) != 0) {
    return Fail("could not inspect the browser socket connection");
  }
  if (socket_error == EINPROGRESS || socket_error == EALREADY) {
    return true;
  }
  if (socket_error != 0) {
    errno = socket_error;
    return Fail("could not connect to the running browser");
  }
  if (!VerifySocketIdentity(
          user_data_dir_.AppendASCII("MCP").AppendASCII("mcp.sock"))) {
    return false;
  }
  connecting_ = false;
  return true;
}

bool DaoMcpBrowserClient::VerifySocketIdentity(
    const base::FilePath& socket_path) {
  const base::FilePath runtime_dir = socket_path.DirName();
  struct stat runtime_stat = {};
  struct stat socket_stat = {};
  if (lstat(runtime_dir.value().c_str(), &runtime_stat) != 0 ||
      !S_ISDIR(runtime_stat.st_mode) || runtime_stat.st_uid != getuid() ||
      (runtime_stat.st_mode & 0777) != 0700 ||
      runtime_stat.st_dev != runtime_device_ ||
      runtime_stat.st_ino != runtime_inode_ ||
      lstat(socket_path.value().c_str(), &socket_stat) != 0 ||
      !S_ISSOCK(socket_stat.st_mode) || socket_stat.st_uid != getuid() ||
      (socket_stat.st_mode & 0777) != 0600 ||
      socket_stat.st_dev != socket_device_ ||
      socket_stat.st_ino != socket_inode_) {
    return Fail("the private MCP browser socket changed before connection");
  }
  return true;
}

bool DaoMcpBrowserClient::Fail(std::string message) {
  last_error_ = std::move(message);
  return false;
}

}  // namespace dao
