// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_runtime_files.h"

#include <windows.h>

#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_number_conversions.h"
#include "crypto/random.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"
#include "dao/browser/mcp/dao_mcp_win_util.h"

namespace dao {

DaoMcpRuntimeFiles::DaoMcpRuntimeFiles(base::FilePath user_data_dir,
                                       base::ProcessId browser_pid)
    : runtime_dir_(user_data_dir.AppendASCII("MCP")),
      metadata_path_(runtime_dir_.AppendASCII("runtime.json")),
      browser_pid_(browser_pid) {}

DaoMcpRuntimeFiles::~DaoMcpRuntimeFiles() = default;

base::expected<void, DaoToolError> DaoMcpRuntimeFiles::Prepare() {
  runtime_dir_handle_.Close();
  port_ = 0;
  nonce_.clear();
  runtime_dir_handle_ =
      OpenDaoMcpPrivatePath(runtime_dir_, true, true, FILE_LIST_DIRECTORY);
  if (!runtime_dir_handle_.is_valid() ||
      !DeleteDaoMcpPrivateFile(metadata_path_)) {
    runtime_dir_handle_.Close();
    return base::unexpected(
        FileError("Could not prepare the private MCP runtime directory."));
  }
  nonce_ = base::HexEncodeLower(crypto::RandBytesAsArray<32>());
  return base::ok();
}

base::expected<void, DaoToolError> DaoMcpRuntimeFiles::CaptureBoundSocket(
    uint16_t port) {
  if (port == 0 || GetDaoMcpLoopbackTcpOwner(port) != browser_pid_) {
    return base::unexpected(
        FileError("Could not identify the bound MCP loopback socket."));
  }
  port_ = port;
  return base::ok();
}

base::expected<void, DaoToolError> DaoMcpRuntimeFiles::Publish() {
  if (!runtime_dir_handle_.is_valid() || nonce_.size() != 64 || port_ == 0 ||
      GetDaoMcpLoopbackTcpOwner(port_) != browser_pid_) {
    return base::unexpected(
        FileError("The MCP loopback socket is unavailable."));
  }
  base::DictValue metadata =
      base::DictValue()
          .Set("version", kDaoMcpIpcVersion)
          .Set("port", static_cast<int>(port_))
          .Set("browser_pid", base::checked_cast<int>(browser_pid_))
          .Set("nonce", nonce_);
  std::string serialized;
  if (!base::JSONWriter::Write(metadata, &serialized)) {
    return base::unexpected(
        FileError("Could not serialize private MCP runtime metadata."));
  }
  const base::FilePath temporary_path =
      runtime_dir_.AppendASCII(".runtime.json." + nonce_);
  auto file = OpenDaoMcpPrivatePath(temporary_path, false, true, GENERIC_WRITE);
  if (!file.is_valid()) {
    return base::unexpected(
        FileError("Could not create private MCP runtime metadata."));
  }
  DWORD written = 0;
  const bool saved = WriteFile(file.get(), serialized.data(),
                               base::checked_cast<DWORD>(serialized.size()),
                               &written, nullptr) &&
                     written == serialized.size() &&
                     FlushFileBuffers(file.get());
  file.Close();
  if (!saved ||
      !MoveFileExW(temporary_path.value().c_str(),
                   metadata_path_.value().c_str(), MOVEFILE_WRITE_THROUGH)) {
    DeleteDaoMcpPrivateFile(temporary_path);
    return base::unexpected(
        FileError("Could not publish private MCP runtime metadata."));
  }
  return base::ok();
}

void DaoMcpRuntimeFiles::Cleanup() {
  if (runtime_dir_handle_.is_valid() && !nonce_.empty()) {
    auto file = OpenDaoMcpPrivatePath(metadata_path_, false, false,
                                      GENERIC_READ | DELETE);
    std::string contents;
    if (file.is_valid() &&
        ReadDaoMcpPrivateFile(file.get(), 64 * 1024, &contents)) {
      auto metadata =
          base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
      const std::string* nonce =
          metadata ? metadata->FindString("nonce") : nullptr;
      if (nonce && *nonce == nonce_) {
        FILE_DISPOSITION_INFO disposition = {TRUE};
        SetFileInformationByHandle(file.get(), FileDispositionInfo,
                                   &disposition, sizeof(disposition));
      }
    }
  }
  runtime_dir_handle_.Close();
  nonce_.clear();
  port_ = 0;
}

DaoToolError DaoMcpRuntimeFiles::FileError(std::string message) const {
  return MakeDaoToolError(DaoToolErrorCode::kInternalError, std::move(message));
}

}  // namespace dao
