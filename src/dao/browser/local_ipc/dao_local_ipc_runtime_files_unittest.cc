// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/local_ipc/dao_local_ipc_runtime_files.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/process/process_handle.h"
#include "dao/browser/local_ipc/dao_local_ipc_client.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

base::ScopedFD BindSocket(const base::FilePath& path) {
  SCOPED_TRACE(path.value());
  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  if (path.value().size() >= sizeof(address.sun_path)) {
    ADD_FAILURE() << "Unix socket path is " << path.value().size()
                  << " bytes; the maximum is " << sizeof(address.sun_path) - 1;
    return {};
  }
  base::ScopedFD fd(socket(AF_UNIX, SOCK_STREAM, 0));
  if (!fd.is_valid()) {
    const int error = errno;
    ADD_FAILURE() << "socket failed: errno=" << error << " ("
                  << std::strerror(error) << ")";
    return {};
  }
  base::span(address.sun_path).copy_prefix_from(path.value());
  if (bind(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
    const int error = errno;
    ADD_FAILURE() << "bind failed: errno=" << error << " ("
                  << std::strerror(error) << ")";
    return {};
  }
  if (listen(fd.get(), 1)) {
    const int error = errno;
    ADD_FAILURE() << "listen failed: errno=" << error << " ("
                  << std::strerror(error) << ")";
    return {};
  }
  return fd;
}

class DaoLocalIpcRuntimeFilesTest : public testing::Test {
 protected:
  void SetUp() override {
    // macOS and test-launcher temporary roots can exceed sun_path's limit.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDirUnderPath(base::FilePath("/tmp"),
                                                       "dao-ipc-"));
  }
  base::ScopedTempDir temp_dir_;
};

TEST_F(DaoLocalIpcRuntimeFilesTest, HomeEndpointDoesNotTouchDefaultEndpoint) {
  auto mcp = base::MakeRefCounted<DaoLocalIpcRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId());
  auto home = base::MakeRefCounted<DaoLocalIpcRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId(), DaoLocalIpcConfig::Home());
  ASSERT_TRUE(mcp->Prepare().has_value());
  auto mcp_socket = BindSocket(mcp->socket_path());
  ASSERT_TRUE(mcp_socket.is_valid());
  ASSERT_TRUE(mcp->CaptureBoundSocket().has_value());
  ASSERT_TRUE(mcp->Publish().has_value());
  ASSERT_TRUE(home->Prepare().has_value());
  EXPECT_EQ(temp_dir_.GetPath().AppendASCII("HomeCLI/home.sock"),
            home->socket_path());
  auto home_socket = BindSocket(home->socket_path());
  ASSERT_TRUE(home_socket.is_valid());
  ASSERT_TRUE(home->CaptureBoundSocket().has_value());
  ASSERT_TRUE(home->Publish().has_value());
  int mode = 0;
  ASSERT_TRUE(base::GetPosixFilePermissions(home->runtime_dir(), &mode));
  EXPECT_EQ(0700, mode & 0777);
  ASSERT_TRUE(base::GetPosixFilePermissions(home->metadata_path(), &mode));
  EXPECT_EQ(0600, mode & 0777);
  DaoLocalIpcClient client(temp_dir_.GetPath(), DaoLocalIpcConfig::Home());
  ASSERT_TRUE(client.Connect("test", "1")) << client.last_error();
  client.Disconnect();
  home->Cleanup();
  EXPECT_FALSE(base::PathExists(home->metadata_path()));
  EXPECT_TRUE(base::PathExists(mcp->metadata_path()));
  EXPECT_TRUE(base::PathExists(mcp->socket_path()));
  mcp->Cleanup();
}

TEST_F(DaoLocalIpcRuntimeFilesTest, RejectsSymlinkedHomeDirectory) {
  const auto target = temp_dir_.GetPath().AppendASCII("target");
  ASSERT_TRUE(base::CreateDirectory(target));
  ASSERT_TRUE(base::CreateSymbolicLink(
      target, temp_dir_.GetPath().AppendASCII("HomeCLI")));
  auto home = base::MakeRefCounted<DaoLocalIpcRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId(), DaoLocalIpcConfig::Home());
  EXPECT_FALSE(home->Prepare().has_value());
  EXPECT_TRUE(base::DirectoryExists(target));
}

TEST_F(DaoLocalIpcRuntimeFilesTest, HomeClientNeverFallsBackToDefaultEndpoint) {
  auto mcp = base::MakeRefCounted<DaoLocalIpcRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId());
  ASSERT_TRUE(mcp->Prepare().has_value());
  auto socket = BindSocket(mcp->socket_path());
  ASSERT_TRUE(socket.is_valid());
  ASSERT_TRUE(mcp->CaptureBoundSocket().has_value());
  ASSERT_TRUE(mcp->Publish().has_value());
  DaoLocalIpcClient home_client(temp_dir_.GetPath(), DaoLocalIpcConfig::Home());
  EXPECT_FALSE(home_client.Connect("test", "1"));
  EXPECT_FALSE(home_client.is_connected());
  mcp->Cleanup();
}

TEST_F(DaoLocalIpcRuntimeFilesTest, ClientRejectsMetadataForAnotherSocket) {
  auto home = base::MakeRefCounted<DaoLocalIpcRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId(), DaoLocalIpcConfig::Home());
  ASSERT_TRUE(home->Prepare().has_value());
  auto socket = BindSocket(home->socket_path());
  ASSERT_TRUE(socket.is_valid());
  ASSERT_TRUE(home->CaptureBoundSocket().has_value());
  ASSERT_TRUE(home->Publish().has_value());
  std::string metadata;
  ASSERT_TRUE(base::ReadFileToString(home->metadata_path(), &metadata));
  const auto name = metadata.find("home.sock");
  ASSERT_NE(std::string::npos, name);
  metadata.replace(name, 9, "other.sock");
  ASSERT_TRUE(base::WriteFile(home->metadata_path(), metadata));
  DaoLocalIpcClient client(temp_dir_.GetPath(), DaoLocalIpcConfig::Home());
  EXPECT_FALSE(client.Connect("test", "1"));
  EXPECT_FALSE(client.is_connected());
  home->Cleanup();
}

TEST_F(DaoLocalIpcRuntimeFilesTest,
       HomeClientAcceptsLargeBoundedSnapshotQueue) {
  auto home = base::MakeRefCounted<DaoLocalIpcRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId(), DaoLocalIpcConfig::Home());
  ASSERT_TRUE(home->Prepare().has_value());
  auto socket = BindSocket(home->socket_path());
  ASSERT_TRUE(socket.is_valid());
  ASSERT_TRUE(home->CaptureBoundSocket().has_value());
  ASSERT_TRUE(home->Publish().has_value());
  DaoLocalIpcClient client(temp_dir_.GetPath(), DaoLocalIpcConfig::Home());
  ASSERT_TRUE(client.Connect("test", "1")) << client.last_error();
  EXPECT_TRUE(client.QueueRequest(
      base::DictValue().Set("snapshot", std::string(9 * 1024 * 1024, 'a'))));
  EXPECT_FALSE(client.QueueRequest(
      base::DictValue().Set("snapshot", std::string(24 * 1024 * 1024, 'a'))));
  EXPECT_TRUE(client.is_connected());
  client.Disconnect();
  home->Cleanup();
}

TEST_F(DaoLocalIpcRuntimeFilesTest, RejectsSocketIdentityReplacement) {
  auto home = base::MakeRefCounted<DaoLocalIpcRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId(), DaoLocalIpcConfig::Home());
  ASSERT_TRUE(home->Prepare().has_value());
  auto original = BindSocket(home->socket_path());
  ASSERT_TRUE(original.is_valid());
  ASSERT_TRUE(home->CaptureBoundSocket().has_value());
  ASSERT_TRUE(base::Move(home->socket_path(),
                         home->runtime_dir().AppendASCII("displaced.sock")));
  auto replacement = BindSocket(home->socket_path());
  ASSERT_TRUE(replacement.is_valid());
  EXPECT_FALSE(home->Publish().has_value());
  EXPECT_FALSE(base::PathExists(home->metadata_path()));
  home->Cleanup();
}

}  // namespace
}  // namespace dao
