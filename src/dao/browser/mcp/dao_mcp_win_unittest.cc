// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_win_util.h"

#include <windows.h>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/scoped_refptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/process/process_handle.h"
#include "base/test/task_environment.h"
#include "base/win/security_descriptor.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"
#include "dao/browser/mcp/dao_mcp_runtime_files.h"
#include "dao/browser/mcp/helper/dao_mcp_browser_client.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source.h"
#include "net/socket/tcp_server_socket.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

TEST(DaoMcpWindowsTest, RejectsMetadataReadableByOtherUsers) {
  base::ScopedTempDir temporary;
  ASSERT_TRUE(temporary.CreateUniqueTempDir());
  const auto path = temporary.GetPath().AppendASCII("metadata.json");
  auto file = OpenDaoMcpPrivatePath(path, false, true, GENERIC_WRITE);
  ASSERT_TRUE(file.is_valid());
  file.Close();
  auto security =
      base::win::SecurityDescriptor::FromFile(path, DACL_SECURITY_INFORMATION);
  ASSERT_TRUE(security);
  ASSERT_TRUE(security->SetDaclEntry(base::win::WellKnownSid::kWorld,
                                     base::win::SecurityAccessMode::kGrant,
                                     FILE_GENERIC_READ, 0));
  ASSERT_TRUE(security->WriteToFile(path, DACL_SECURITY_INFORMATION));
  EXPECT_FALSE(
      OpenDaoMcpPrivatePath(path, false, false, GENERIC_READ).is_valid());
  EXPECT_FALSE(DeleteDaoMcpPrivateFile(path));
  EXPECT_TRUE(base::PathExists(path));
}

TEST(DaoMcpWindowsTest, PublishesPrivateListenerAndConnectsHelper) {
  base::test::TaskEnvironment tasks(
      base::test::TaskEnvironment::MainThreadType::IO);
  base::ScopedTempDir temporary;
  ASSERT_TRUE(temporary.CreateUniqueTempDir());
  auto runtime = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      temporary.GetPath(), base::GetCurrentProcId());
  ASSERT_TRUE(runtime->Prepare().has_value());
  net::TCPServerSocket listener(nullptr, net::NetLogSource());
  ASSERT_EQ(net::OK,
            listener.Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0),
                            1, std::nullopt));
  net::IPEndPoint address;
  ASSERT_EQ(net::OK, listener.GetLocalAddress(&address));
  EXPECT_EQ(GetDaoMcpLoopbackTcpOwner(address.port()),
            base::GetCurrentProcId());
  ASSERT_TRUE(runtime->CaptureBoundSocket(address.port()).has_value());
  ASSERT_TRUE(runtime->Publish().has_value());
  {
    auto file = OpenDaoMcpPrivatePath(runtime->metadata_path(), false, false,
                                      GENERIC_READ);
    ASSERT_TRUE(file.is_valid());
    std::string contents;
    ASSERT_TRUE(ReadDaoMcpPrivateFile(file.get(), 64 * 1024, &contents));
    auto metadata = base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->FindInt("port"), address.port());
    const std::string* nonce = metadata->FindString("nonce");
    ASSERT_TRUE(nonce);
    EXPECT_EQ(*nonce, runtime->nonce());
  }
  DaoMcpBrowserClient client(temporary.GetPath());
  ASSERT_TRUE(client.Connect("windows-test", "1")) << client.last_error();
  ASSERT_TRUE(client.WaitForIO(false, true, 1000));
  ASSERT_TRUE(client.FlushWrites()) << client.last_error();
  EXPECT_FALSE(client.wants_write());
  client.Disconnect();
  runtime->Cleanup();
  EXPECT_FALSE(base::PathExists(runtime->metadata_path()));
}

TEST(DaoMcpWindowsTest, RejectsListenerWithDifferentBrowserPid) {
  base::test::TaskEnvironment tasks(
      base::test::TaskEnvironment::MainThreadType::IO);
  base::ScopedTempDir temporary;
  ASSERT_TRUE(temporary.CreateUniqueTempDir());
  auto runtime = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      temporary.GetPath(), base::GetCurrentProcId());
  ASSERT_TRUE(runtime->Prepare().has_value());
  net::TCPServerSocket listener(nullptr, net::NetLogSource());
  ASSERT_EQ(net::OK,
            listener.Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0),
                            1, std::nullopt));
  net::IPEndPoint address;
  ASSERT_EQ(net::OK, listener.GetLocalAddress(&address));
  std::string metadata;
  ASSERT_TRUE(base::JSONWriter::Write(
      base::DictValue()
          .Set("version", kDaoMcpIpcVersion)
          .Set("port", static_cast<int>(address.port()))
          .Set("browser_pid", static_cast<int>(base::GetCurrentProcId()) + 1)
          .Set("nonce", runtime->nonce()),
      &metadata));
  {
    auto file = OpenDaoMcpPrivatePath(runtime->metadata_path(), false, true,
                                      GENERIC_WRITE);
    ASSERT_TRUE(file.is_valid());
    DWORD written = 0;
    ASSERT_TRUE(WriteFile(file.get(), metadata.data(),
                          base::checked_cast<DWORD>(metadata.size()), &written,
                          nullptr));
    ASSERT_EQ(written, metadata.size());
  }
  DaoMcpBrowserClient client(temporary.GetPath());
  EXPECT_FALSE(client.Connect("windows-test", "1"));
  EXPECT_FALSE(client.is_connected());
  EXPECT_EQ(client.last_error(),
            "the private MCP browser listener is unavailable");
  client.Disconnect();
  runtime->Cleanup();
}

}  // namespace
}  // namespace dao
