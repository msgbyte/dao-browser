// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_service.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/raw_ptr.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/sequence_bound.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/delete_profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_metrics.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/automation/dao_agent_lease_manager.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"
#include "dao/browser/mcp/dao_mcp_runtime_files.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_agent_cursor_view.h"
#include "dao/browser/ui/views/dao_mcp_control_banner_view.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/hit_test.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/event.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"

namespace dao {
namespace {

bool HasDescendantLabelText(views::View* root, std::u16string_view text) {
  if (!root) {
    return false;
  }
  if (auto* label = views::AsViewClass<views::Label>(root);
      label && label->GetText() == text) {
    return true;
  }
  return std::ranges::any_of(root->children(), [text](views::View* child) {
    return HasDescendantLabelText(child, text);
  });
}

class DaoMcpProtocolTest : public testing::Test {};

TEST_F(DaoMcpProtocolTest, ParsesVersionedRequest) {
  auto parsed = ParseDaoMcpRequestLine(
      R"({"version":1,"id":"request-1","method":"tools/list","params":{}})");

  ASSERT_TRUE(parsed.has_value()) << parsed.error().error.message;
  EXPECT_EQ("request-1", parsed->id);
  EXPECT_EQ("tools/list", parsed->method);
  EXPECT_TRUE(parsed->params.empty());
}

TEST_F(DaoMcpProtocolTest, AcceptsCancellationNotificationWithoutId) {
  auto parsed = ParseDaoMcpRequestLine(
      R"({"version":1,"method":"tools/cancel","params":{"request_id":"call-1"}})");

  ASSERT_TRUE(parsed.has_value()) << parsed.error().error.message;
  EXPECT_FALSE(parsed->id.has_value());
  EXPECT_EQ("tools/cancel", parsed->method);
  EXPECT_EQ("call-1", *parsed->params.FindString("request_id"));
}

TEST_F(DaoMcpProtocolTest, RejectsMalformedJson) {
  auto parsed = ParseDaoMcpRequestLine("{");

  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, parsed.error().error.code);
}

TEST_F(DaoMcpProtocolTest, RejectsUnsupportedVersion) {
  auto parsed = ParseDaoMcpRequestLine(
      R"({"version":2,"id":"request-1","method":"tools/list","params":{}})");

  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(DaoToolErrorCode::kIpcVersionUnsupported,
            parsed.error().error.code);
  EXPECT_EQ("request-1", parsed.error().id);
}

TEST_F(DaoMcpProtocolTest, PreservesValidIdForMalformedEnvelope) {
  auto parsed = ParseDaoMcpRequestLine(
      R"({"version":1,"id":"request-1","method":[],"params":{}})");

  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, parsed.error().error.code);
  EXPECT_EQ("request-1", parsed.error().id);
}

TEST_F(DaoMcpProtocolTest, BoundsRequestId) {
  const std::string maximum_id(kDaoMcpMaxRequestIdBytes, 'i');
  std::string maximum_line = R"({"version":1,"id":")" + maximum_id +
                             R"(","method":"tools/list","params":{}})";
  auto accepted = ParseDaoMcpRequestLine(maximum_line);
  ASSERT_TRUE(accepted.has_value()) << accepted.error().error.message;
  EXPECT_EQ(maximum_id, accepted->id);

  const std::string oversized_id(kDaoMcpMaxRequestIdBytes + 1, 'i');
  std::string oversized_line = R"({"version":1,"id":")" + oversized_id +
                               R"(","method":"tools/list","params":{}})";
  auto rejected = ParseDaoMcpRequestLine(oversized_line);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_FALSE(rejected.error().id);
}

TEST_F(DaoMcpProtocolTest, RejectsLineOverEightMiB) {
  std::string oversized(kDaoMcpMaxLineBytes + 1, ' ');

  auto parsed = ParseDaoMcpRequestLine(oversized);

  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, parsed.error().error.code);
}

TEST_F(DaoMcpProtocolTest, AcceptsExactEightMiBLine) {
  const std::string prefix =
      R"({"version":1,"id":"request-1","method":"tools/list","params":{"padding":")";
  const std::string suffix = R"("}})";
  ASSERT_LT(prefix.size() + suffix.size(), kDaoMcpMaxLineBytes);
  std::string exact = prefix;
  exact.append(kDaoMcpMaxLineBytes - prefix.size() - suffix.size(), 'a');
  exact.append(suffix);

  auto parsed = ParseDaoMcpRequestLine(exact);

  ASSERT_TRUE(parsed.has_value()) << parsed.error().error.message;
  EXPECT_EQ(kDaoMcpMaxLineBytes, exact.size());
}

TEST_F(DaoMcpProtocolTest, SerializesMatchingResponseIdAndNewline) {
  std::string serialized = SerializeDaoMcpSuccessResponse(
      "request-1", base::DictValue().Set("ready", true));

  ASSERT_FALSE(serialized.empty());
  EXPECT_EQ('\n', serialized.back());
  auto parsed = base::JSONReader::Read(
      serialized.substr(0, serialized.size() - 1), base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  ASSERT_TRUE(parsed->is_dict());
  const base::DictValue& response = parsed->GetDict();
  EXPECT_EQ(kDaoMcpIpcVersion, response.FindInt("version"));
  EXPECT_EQ("request-1", *response.FindString("id"));
  const base::DictValue* result = response.FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(true, result->FindBool("ready"));
}

class DaoMcpRuntimeFilesTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

base::ScopedFD BindUnixSocket(const base::FilePath& path) {
  base::ScopedFD socket_fd(socket(AF_UNIX, SOCK_STREAM, 0));
  if (!socket_fd.is_valid()) {
    ADD_FAILURE() << "Could not create Unix socket";
    return {};
  }
  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  if (path.value().size() >= sizeof(address.sun_path)) {
    ADD_FAILURE() << "Unix socket path is too long";
    return {};
  }
  base::span(address.sun_path).copy_prefix_from(path.value());
  if (bind(socket_fd.get(), reinterpret_cast<sockaddr*>(&address),
           sizeof(address)) != 0 ||
      listen(socket_fd.get(), 1) != 0) {
    ADD_FAILURE() << "Could not bind Unix socket";
    return {};
  }
  return socket_fd;
}

TEST_F(DaoMcpRuntimeFilesTest, CreatesOwnerOnlyRuntimeFiles) {
  auto files = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId());
  ASSERT_TRUE(files->Prepare().has_value());
  base::ScopedFD socket_fd = BindUnixSocket(files->socket_path());
  ASSERT_TRUE(socket_fd.is_valid());
  ASSERT_TRUE(files->CaptureBoundSocket().has_value());
  ASSERT_TRUE(files->Publish().has_value());

  int directory_mode = 0;
  int socket_mode = 0;
  int metadata_mode = 0;
  ASSERT_TRUE(
      base::GetPosixFilePermissions(files->runtime_dir(), &directory_mode));
  ASSERT_TRUE(
      base::GetPosixFilePermissions(files->socket_path(), &socket_mode));
  ASSERT_TRUE(
      base::GetPosixFilePermissions(files->metadata_path(), &metadata_mode));
  EXPECT_EQ(0700, directory_mode & 0777);
  EXPECT_EQ(0600, socket_mode & 0777);
  EXPECT_EQ(0600, metadata_mode & 0777);
}

TEST_F(DaoMcpRuntimeFilesTest, RotatesTwoHundredFiftySixBitNonce) {
  auto files = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId());
  ASSERT_TRUE(files->Prepare().has_value());
  const std::string first_nonce = files->nonce();
  files->Cleanup();
  ASSERT_TRUE(files->Prepare().has_value());

  EXPECT_EQ(64u, first_nonce.size());
  EXPECT_EQ(64u, files->nonce().size());
  EXPECT_NE(first_nonce, files->nonce());
}

TEST_F(DaoMcpRuntimeFilesTest, PublishesVersionedMetadataAtomically) {
  auto files = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId());
  ASSERT_TRUE(files->Prepare().has_value());
  base::ScopedFD socket_fd = BindUnixSocket(files->socket_path());
  ASSERT_TRUE(socket_fd.is_valid());
  ASSERT_TRUE(files->CaptureBoundSocket().has_value());
  ASSERT_TRUE(files->Publish().has_value());

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(files->metadata_path(), &contents));
  auto parsed = base::JSONReader::Read(contents, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed);
  ASSERT_TRUE(parsed->is_dict());
  const base::DictValue& metadata = parsed->GetDict();
  EXPECT_EQ(kDaoMcpIpcVersion, metadata.FindInt("version"));
  EXPECT_EQ(files->socket_path().value(), *metadata.FindString("socket_path"));
  EXPECT_EQ(base::GetCurrentProcId(), metadata.FindInt("browser_pid"));
  EXPECT_EQ(files->nonce(), *metadata.FindString("nonce"));
}

TEST_F(DaoMcpRuntimeFilesTest, CleanupRemovesEndpointAndMetadata) {
  auto files = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId());
  ASSERT_TRUE(files->Prepare().has_value());
  base::ScopedFD socket_fd = BindUnixSocket(files->socket_path());
  ASSERT_TRUE(socket_fd.is_valid());
  ASSERT_TRUE(files->CaptureBoundSocket().has_value());
  ASSERT_TRUE(files->Publish().has_value());

  files->Cleanup();

  EXPECT_FALSE(base::PathExists(files->socket_path()));
  EXPECT_FALSE(base::PathExists(files->metadata_path()));
}

TEST_F(DaoMcpRuntimeFilesTest, RejectsSymlinkedRuntimeDirectory) {
  const base::FilePath target = temp_dir_.GetPath().AppendASCII("target");
  ASSERT_TRUE(base::CreateDirectory(target));
  ASSERT_EQ(0, symlink(target.value().c_str(),
                       temp_dir_.GetPath().AppendASCII("MCP").value().c_str()));
  auto files = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId());

  EXPECT_FALSE(files->Prepare().has_value());
  EXPECT_TRUE(base::DirectoryExists(target));
}

TEST_F(DaoMcpRuntimeFilesTest, RejectsRegularSocketLeaf) {
  auto files = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId());
  ASSERT_TRUE(files->Prepare().has_value());
  ASSERT_TRUE(base::WriteFile(files->socket_path(), "not-a-socket"));

  EXPECT_FALSE(files->CaptureBoundSocket().has_value());
  EXPECT_FALSE(files->Publish().has_value());
}

TEST_F(DaoMcpRuntimeFilesTest, RejectsSocketSwapAfterCapture) {
  auto files = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      temp_dir_.GetPath(), base::GetCurrentProcId());
  ASSERT_TRUE(files->Prepare().has_value());
  base::ScopedFD original_socket = BindUnixSocket(files->socket_path());
  ASSERT_TRUE(original_socket.is_valid());
  ASSERT_TRUE(files->CaptureBoundSocket().has_value());
  const base::FilePath displaced =
      files->runtime_dir().AppendASCII("displaced.sock");
  ASSERT_EQ(0, rename(files->socket_path().value().c_str(),
                      displaced.value().c_str()));
  base::ScopedFD replacement_socket = BindUnixSocket(files->socket_path());
  ASSERT_TRUE(replacement_socket.is_valid());

  EXPECT_FALSE(files->Publish().has_value());
  EXPECT_FALSE(base::PathExists(files->metadata_path()));
}

class BlockingMcpClient {
 public:
  explicit BlockingMcpClient(std::string socket_path)
      : socket_path_(std::move(socket_path)) {}

  ~BlockingMcpClient() { Disconnect(); }

  bool Connect() {
    socket_fd_.reset(socket(AF_UNIX, SOCK_STREAM, 0));
    if (!socket_fd_.is_valid()) {
      return false;
    }
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(address.sun_path)) {
      return false;
    }
    base::span(address.sun_path).copy_prefix_from(socket_path_);
    return HANDLE_EINTR(connect(socket_fd_.get(),
                                reinterpret_cast<sockaddr*>(&address),
                                sizeof(address))) == 0;
  }

  bool Send(std::string serialized) {
    size_t written = 0;
    while (written < serialized.size()) {
      const base::span<const uint8_t> remaining =
          base::as_byte_span(serialized).subspan(written);
      const ssize_t result = HANDLE_EINTR(send(
          socket_fd_.get(), remaining.data(), remaining.size(), MSG_NOSIGNAL));
      if (result <= 0) {
        return false;
      }
      written += static_cast<size_t>(result);
    }
    return true;
  }

  void SendAndSignal(std::string serialized, base::WaitableEvent* done) {
    Send(std::move(serialized));
    done->Signal();
  }

  std::optional<std::string> ReadChunk() {
    std::array<char, 4096> buffer;
    const ssize_t result =
        HANDLE_EINTR(read(socket_fd_.get(), buffer.data(), buffer.size()));
    if (result <= 0) {
      return std::nullopt;
    }
    return std::string(buffer.data(), static_cast<size_t>(result));
  }

  bool SetReceiveBufferSize(int32_t size) {
    return setsockopt(socket_fd_.get(), SOL_SOCKET, SO_RCVBUF, &size,
                      sizeof(size)) == 0;
  }

  void Disconnect() {
    if (socket_fd_.is_valid()) {
      shutdown(socket_fd_.get(), SHUT_RDWR);
      socket_fd_.reset();
    }
  }

 private:
  std::string socket_path_;
  base::ScopedFD socket_fd_;
};

class TestMcpClient {
 public:
  explicit TestMcpClient(std::string socket_path)
      : client_(base::ThreadPool::CreateSequencedTaskRunner(
                    {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                     base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
                std::move(socket_path)) {}

  ~TestMcpClient() {
    Disconnect();
    client_.Reset();
  }

  bool Connect() {
    base::test::TestFuture<bool> connected;
    client_.AsyncCall(&BlockingMcpClient::Connect)
        .Then(connected.GetCallback());
    return connected.Get();
  }

  bool Send(base::DictValue request) {
    std::string serialized;
    if (!base::JSONWriter::Write(request, &serialized)) {
      return false;
    }
    serialized.push_back('\n');
    return SendRaw(std::move(serialized));
  }

  bool SendBatch(std::vector<base::DictValue> requests) {
    std::string serialized;
    for (base::DictValue& request : requests) {
      std::string line;
      if (!base::JSONWriter::Write(request, &line)) {
        return false;
      }
      serialized.append(line);
      serialized.push_back('\n');
    }
    return SendRaw(std::move(serialized));
  }

  bool SetReceiveBufferSize(int32_t size) {
    base::test::TestFuture<bool> configured;
    client_.AsyncCall(&BlockingMcpClient::SetReceiveBufferSize)
        .WithArgs(size)
        .Then(configured.GetCallback());
    return configured.Get();
  }

  void SendRawWhileBlockingUi(std::string serialized) {
    base::WaitableEvent done;
    client_.AsyncCall(&BlockingMcpClient::SendAndSignal)
        .WithArgs(std::move(serialized), base::Unretained(&done));
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_sync;
    done.Wait();
  }

  size_t last_read_line_bytes() const { return last_read_line_bytes_; }

  void Disconnect() {
    if (!client_.is_null()) {
      base::test::TestFuture<void> disconnected;
      client_.AsyncCall(&BlockingMcpClient::Disconnect)
          .Then(disconnected.GetCallback());
      EXPECT_TRUE(disconnected.Wait());
    }
  }

 private:
  bool SendRaw(std::string serialized) {
    base::test::TestFuture<bool> sent;
    client_.AsyncCall(&BlockingMcpClient::Send)
        .WithArgs(std::move(serialized))
        .Then(sent.GetCallback());
    return sent.Get();
  }

 public:
  std::optional<base::DictValue> Read() {
    while (true) {
      const size_t newline = received_.find('\n');
      if (newline != std::string::npos) {
        std::string line = received_.substr(0, newline);
        received_.erase(0, newline + 1);
        last_read_line_bytes_ = line.size() + 1;
        std::optional<base::Value> parsed =
            base::JSONReader::Read(line, base::JSON_PARSE_RFC);
        if (!parsed || !parsed->is_dict()) {
          return std::nullopt;
        }
        return std::move(*parsed).TakeDict();
      }

      base::test::TestFuture<std::optional<std::string>> read;
      client_.AsyncCall(&BlockingMcpClient::ReadChunk).Then(read.GetCallback());
      std::optional<std::string> chunk = read.Take();
      if (!chunk) {
        return std::nullopt;
      }
      received_.append(*chunk);
    }
  }

 private:
  base::SequenceBound<BlockingMcpClient> client_;
  std::string received_;
  size_t last_read_line_bytes_ = 0;
};

class BlockingPackagedHelper {
 public:
  ~BlockingPackagedHelper() {
    CloseInput();
    if (process_.IsValid()) {
      process_.Terminate(/*exit_code=*/1, /*wait=*/true);
    }
  }

  bool Start(base::FilePath helper_path, base::FilePath user_data_dir) {
    base::ScopedFD child_stdin;
    base::ScopedFD parent_stdin;
    base::ScopedFD parent_stdout;
    base::ScopedFD child_stdout;
    base::ScopedFD parent_stderr;
    base::ScopedFD child_stderr;
    if (!base::CreatePipe(&child_stdin, &parent_stdin) ||
        !base::CreatePipe(&parent_stdout, &child_stdout) ||
        !base::CreatePipe(&parent_stderr, &child_stderr)) {
      return false;
    }

    base::CommandLine command(helper_path);
    command.AppendSwitchPath("user-data-dir", user_data_dir);
    base::LaunchOptions options;
    options.fds_to_remap.emplace_back(child_stdin.get(), STDIN_FILENO);
    options.fds_to_remap.emplace_back(child_stdout.get(), STDOUT_FILENO);
    options.fds_to_remap.emplace_back(child_stderr.get(), STDERR_FILENO);
    process_ = base::LaunchProcess(command, options);
    if (!process_.IsValid()) {
      return false;
    }
    stdin_ = std::move(parent_stdin);
    stdout_ = std::move(parent_stdout);
    stderr_ = std::move(parent_stderr);
    return true;
  }

  bool Send(base::DictValue message) {
    std::string serialized;
    return base::JSONWriter::Write(message, &serialized) &&
           base::WriteFileDescriptor(stdin_.get(), serialized + "\n");
  }

  std::optional<base::DictValue> ReadResponse() {
    while (true) {
      const size_t newline = received_.find('\n');
      if (newline != std::string::npos) {
        std::string line = received_.substr(0, newline);
        received_.erase(0, newline + 1);
        std::optional<base::Value> parsed =
            base::JSONReader::Read(line, base::JSON_PARSE_RFC);
        if (!parsed || !parsed->is_dict()) {
          return std::nullopt;
        }
        return std::move(*parsed).TakeDict();
      }

      pollfd descriptor = {.fd = stdout_.get(), .events = POLLIN, .revents = 0};
      int poll_result;
      do {
        poll_result = poll(&descriptor, 1, 5000);
      } while (poll_result < 0 && errno == EINTR);
      if (poll_result <= 0 ||
          !(descriptor.revents & (POLLIN | POLLHUP | POLLERR))) {
        return std::nullopt;
      }
      std::array<char, 4096> buffer;
      const ssize_t bytes =
          HANDLE_EINTR(read(stdout_.get(), buffer.data(), buffer.size()));
      if (bytes <= 0) {
        return std::nullopt;
      }
      received_.append(buffer.data(), static_cast<size_t>(bytes));
    }
  }

  void CloseInput() { stdin_.reset(); }

  bool WaitForExit() {
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_sync;
    int exit_code = -1;
    const bool exited =
        process_.WaitForExitWithTimeout(base::Seconds(5), &exit_code);
    if (exited) {
      process_.Close();
    }
    return exited && exit_code == 0;
  }

 private:
  base::Process process_;
  base::ScopedFD stdin_;
  base::ScopedFD stdout_;
  base::ScopedFD stderr_;
  std::string received_;
};

class PackagedHelperProcess {
 public:
  PackagedHelperProcess()
      : helper_(base::ThreadPool::CreateSequencedTaskRunner(
            {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
             base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {}

  ~PackagedHelperProcess() { helper_.Reset(); }

  bool Start(base::FilePath helper_path, base::FilePath user_data_dir) {
    base::test::TestFuture<bool> started;
    helper_.AsyncCall(&BlockingPackagedHelper::Start)
        .WithArgs(std::move(helper_path), std::move(user_data_dir))
        .Then(started.GetCallback());
    return started.Get();
  }

  bool Send(base::DictValue message) {
    base::test::TestFuture<bool> sent;
    helper_.AsyncCall(&BlockingPackagedHelper::Send)
        .WithArgs(std::move(message))
        .Then(sent.GetCallback());
    return sent.Get();
  }

  std::optional<base::DictValue> ReadResponse() {
    base::test::TestFuture<std::optional<base::DictValue>> response;
    helper_.AsyncCall(&BlockingPackagedHelper::ReadResponse)
        .Then(response.GetCallback());
    return response.Take();
  }

  void CloseInput() {
    base::test::TestFuture<void> closed;
    helper_.AsyncCall(&BlockingPackagedHelper::CloseInput)
        .Then(closed.GetCallback());
    EXPECT_TRUE(closed.Wait());
  }

  bool WaitForExit() {
    base::test::TestFuture<bool> exited;
    helper_.AsyncCall(&BlockingPackagedHelper::WaitForExit)
        .Then(exited.GetCallback());
    return exited.Get();
  }

 private:
  base::SequenceBound<BlockingPackagedHelper> helper_;
};

class FakeApprovalDelegate : public DaoMcpApprovalDelegate {
 public:
  struct Request {
    std::string connection_id;
    base::OnceCallback<void(bool)> callback;
  };

  void RequestApproval(const DaoMcpClientInfo& client,
                       Browser* browser,
                       std::string_view connection_id,
                       base::OnceCallback<void(bool)> callback) override {
    client_ = client;
    browser_ = browser;
    requests_.push_back({.connection_id = std::string(connection_id),
                         .callback = std::move(callback)});
  }

  void CancelApproval(std::string_view connection_id) override {
    cancelled_connection_ids_.push_back(std::string(connection_id));
  }

  bool has_pending_request() const {
    return std::ranges::any_of(requests_, [](const Request& request) {
      return !request.callback.is_null();
    });
  }
  size_t request_count() const { return requests_.size(); }
  Browser* browser() const { return browser_; }
  const std::vector<std::string>& cancelled_connection_ids() const {
    return cancelled_connection_ids_;
  }

  void Resolve(bool allowed) {
    ASSERT_FALSE(requests_.empty());
    ResolveAt(requests_.size() - 1, allowed);
  }

  void ResolveAt(size_t index, bool allowed) {
    ASSERT_LT(index, requests_.size());
    ASSERT_TRUE(requests_[index].callback);
    std::move(requests_[index].callback).Run(allowed);
  }

 private:
  DaoMcpClientInfo client_;
  raw_ptr<Browser> browser_ = nullptr;
  std::vector<Request> requests_;
  std::vector<std::string> cancelled_connection_ids_;
};

class AllowDuringCancelApprovalDelegate : public DaoMcpApprovalDelegate {
 public:
  void RequestApproval(const DaoMcpClientInfo&,
                       Browser*,
                       std::string_view,
                       base::OnceCallback<void(bool)> callback) override {
    callback_ = std::move(callback);
  }

  void CancelApproval(std::string_view) override {
    if (callback_) {
      std::move(callback_).Run(true);
    }
  }

 private:
  base::OnceCallback<void(bool)> callback_;
};

std::string ExactMaximumToolsListLine(std::string_view request_id) {
  const std::string prefix = R"({"version":1,"id":")" +
                             std::string(request_id) +
                             R"(","method":"tools/list","params":{"padding":")";
  const std::string suffix = R"("}})";
  CHECK_LT(prefix.size() + suffix.size(), kDaoMcpMaxLineBytes);
  std::string line = prefix;
  line.append(kDaoMcpMaxLineBytes - prefix.size() - suffix.size(), 'a');
  line.append(suffix);
  line.push_back('\n');
  return line;
}

class DaoMcpServiceBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    service_ = DaoMcpService::Get();
    service_->SetApprovalDelegate(nullptr);
    ASSERT_EQ(DaoMcpStatus::kDisabled, service_->GetStatus().state);
    ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/title1.html")));
  }

  void TearDownOnMainThread() override {
    service_->SetDevToolsCommandCallbackForTesting({});
    service_->SetApprovalDelegate(nullptr);
    service_->SetEnabled(false);
    service_->SetTimeoutsForTesting(base::Seconds(5), base::Seconds(30));
    InProcessBrowserTest::TearDownOnMainThread();
  }

 protected:
  base::DictValue ReadMetadata() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    base::FilePath user_data_dir =
        base::PathService::CheckedGet(chrome::DIR_USER_DATA);
    auto layout = base::MakeRefCounted<DaoMcpRuntimeFiles>(
        user_data_dir, base::GetCurrentProcId());
    std::string contents;
    EXPECT_TRUE(base::ReadFileToString(layout->metadata_path(), &contents));
    std::optional<base::Value> parsed =
        base::JSONReader::Read(contents, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed);
    EXPECT_TRUE(parsed && parsed->is_dict());
    return parsed && parsed->is_dict() ? std::move(*parsed).TakeDict()
                                       : base::DictValue();
  }

  std::unique_ptr<TestMcpClient> ConnectClient() {
    base::DictValue metadata = ReadMetadata();
    const std::string* socket_path = metadata.FindString("socket_path");
    EXPECT_TRUE(socket_path);
    if (!socket_path) {
      return nullptr;
    }
    auto client = std::make_unique<TestMcpClient>(*socket_path);
    EXPECT_TRUE(client->Connect());
    return client;
  }

  void EnableService() {
    service_->SetEnabled(true);
    ASSERT_TRUE(base::test::RunUntil([this] {
      return service_->GetStatus().state == DaoMcpStatus::kListening;
    }));
  }

  bool RuntimeEndpointIsAbsent(
      const scoped_refptr<DaoMcpRuntimeFiles>& layout) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    return !base::PathExists(layout->socket_path()) &&
           !base::PathExists(layout->metadata_path());
  }

  void WaitUntilListeningWithoutClient() {
    ASSERT_TRUE(base::test::RunUntil([this] {
      const DaoMcpServiceStatus status = service_->GetStatus();
      return status.state == DaoMcpStatus::kListening && !status.client;
    }));
  }

  base::DictValue ToolCall(std::string id,
                           std::string name = "get_page_info",
                           base::DictValue arguments = {}) {
    return base::DictValue()
        .Set("version", kDaoMcpIpcVersion)
        .Set("id", std::move(id))
        .Set("method", "tools/call")
        .Set("params", base::DictValue()
                           .Set("name", std::move(name))
                           .Set("arguments", std::move(arguments)));
  }

  base::DictValue CancelRequest(std::string request_id,
                                std::optional<std::string> id = std::nullopt) {
    base::DictValue request =
        base::DictValue()
            .Set("version", kDaoMcpIpcVersion)
            .Set("method", "tools/cancel")
            .Set("params",
                 base::DictValue().Set("request_id", std::move(request_id)));
    if (id) {
      request.Set("id", std::move(*id));
    }
    return request;
  }

  void ApproveFirstToolCall(TestMcpClient* client,
                            FakeApprovalDelegate* approval,
                            std::string request_id = "authorize-control") {
    ASSERT_TRUE(client->Send(ToolCall(std::move(request_id))));
    WaitForApprovalRequestCount(approval, approval->request_count() + 1);
    approval->Resolve(true);
    std::optional<base::DictValue> response = client->Read();
    ASSERT_TRUE(response);
    EXPECT_TRUE(response->FindDict("result"));
    ASSERT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);
  }

  void WaitForApprovalRequestCount(FakeApprovalDelegate* approval,
                                   size_t expected_count) {
    ASSERT_TRUE(base::test::RunUntil([approval, expected_count] {
      return approval->request_count() == expected_count &&
             approval->has_pending_request();
    }));
  }

  base::DictValue HelloRequest(const std::string& nonce,
                               std::string id = "hello-1") {
    return base::DictValue()
        .Set("version", kDaoMcpIpcVersion)
        .Set("id", std::move(id))
        .Set("method", "hello")
        .Set("params", base::DictValue()
                           .Set("nonce", nonce)
                           .Set("client", base::DictValue()
                                              .Set("name", "Test MCP Client")
                                              .Set("version", "1.0")));
  }

  base::DictValue InitializeRequest(std::string id) {
    return base::DictValue()
        .Set("jsonrpc", "2.0")
        .Set("id", std::move(id))
        .Set("method", "initialize")
        .Set("params",
             base::DictValue()
                 .Set("protocolVersion", "2025-11-25")
                 .Set("capabilities", base::DictValue())
                 .Set("clientInfo", base::DictValue()
                                        .Set("name", "Packaged Helper E2E")
                                        .Set("version", "1.0")));
  }

  base::DictValue InitializedNotification() {
    return base::DictValue()
        .Set("jsonrpc", "2.0")
        .Set("method", "notifications/initialized")
        .Set("params", base::DictValue());
  }

  std::string nonce() {
    base::DictValue metadata = ReadMetadata();
    const std::string* value = metadata.FindString("nonce");
    EXPECT_TRUE(value);
    return value ? *value : std::string();
  }

  raw_ptr<DaoMcpService> service_ = nullptr;
};

class DaoMcpControlBannerTest : public DaoMcpServiceBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest, IsDisabledByDefault) {
  EXPECT_FALSE(g_browser_process->local_state()->GetBoolean(
      prefs::kDaoMcpServerEnabled));
  EXPECT_EQ(DaoMcpStatus::kDisabled, service_->GetStatus().state);

  base::FilePath user_data_dir =
      base::PathService::CheckedGet(chrome::DIR_USER_DATA);
  auto layout = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      user_data_dir, base::GetCurrentProcId());
  EXPECT_TRUE(base::test::RunUntil(
      [this, layout] { return RuntimeEndpointIsAbsent(layout); }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       RejectsBadNonceBeforeApproval) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  ASSERT_EQ(DaoMcpStatus::kListening, service_->GetStatus().state);
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);

  ASSERT_TRUE(client->Send(HelloRequest("not-the-runtime-nonce")));
  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("AUTHORIZATION_DENIED", *error->FindString("code"));
  EXPECT_FALSE(approval.has_pending_request());
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       CorrelatesUnsupportedVersionError) {
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  const std::string request_id(kDaoMcpMaxRequestIdBytes, 'v');
  ASSERT_TRUE(client->Send(base::DictValue()
                               .Set("version", kDaoMcpIpcVersion + 1)
                               .Set("id", request_id)
                               .Set("method", "hello")
                               .Set("params", base::DictValue())));

  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ(request_id, *response->FindString("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("IPC_VERSION_UNSUPPORTED", *error->FindString("code"));
  EXPECT_LE(client->last_read_line_bytes(), kDaoMcpMaxLineBytes + 1);
  WaitUntilListeningWithoutClient();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       CorrelatesMalformedEnvelopeError) {
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(base::DictValue()
                               .Set("version", kDaoMcpIpcVersion)
                               .Set("id", "malformed-1")
                               .Set("method", base::ListValue())
                               .Set("params", base::DictValue())));

  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ("malformed-1", *response->FindString("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("INVALID_ARGUMENT", *error->FindString("code"));
  WaitUntilListeningWithoutClient();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       IdleDiscoveryDoesNotStartApprovalOrExpireConnection) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  service_->SetTimeoutsForTesting(base::Seconds(5), base::Milliseconds(100));
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);

  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  EXPECT_FALSE(approval.has_pending_request());
  EXPECT_EQ(DaoMcpStatus::kListening, service_->GetStatus().state);

  base::RunLoop idle;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, idle.QuitClosure(), base::Milliseconds(200));
  idle.Run();
  EXPECT_EQ(0u, approval.request_count());

  ASSERT_TRUE(client->Send(base::DictValue()
                               .Set("version", kDaoMcpIpcVersion)
                               .Set("id", "list-1")
                               .Set("method", "tools/list")
                               .Set("params", base::DictValue())));
  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  const base::DictValue* result = response->FindDict("result");
  ASSERT_TRUE(result);
  const base::ListValue* tools = result->FindList("tools");
  ASSERT_TRUE(tools);
  EXPECT_EQ(29u, tools->size());

  ASSERT_TRUE(client->Send(ToolCall("call-after-idle-discovery")));
  ASSERT_TRUE(base::test::RunUntil(
      [&approval] { return approval.has_pending_request(); }));
  EXPECT_EQ(1u, approval.request_count());
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       DefersTargetSelectionUntilFirstToolCall) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("chrome://settings/dao")));
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);

  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  std::optional<base::DictValue> hello_response = client->Read();
  ASSERT_TRUE(hello_response);
  ASSERT_TRUE(hello_response->FindDict("result"));
  EXPECT_FALSE(approval.has_pending_request());
  EXPECT_EQ(DaoMcpStatus::kListening, service_->GetStatus().state);

  ASSERT_TRUE(client->Send(base::DictValue()
                               .Set("version", kDaoMcpIpcVersion)
                               .Set("id", "list-before-target")
                               .Set("method", "tools/list")
                               .Set("params", base::DictValue())));
  std::optional<base::DictValue> list_response = client->Read();
  ASSERT_TRUE(list_response);
  const base::ListValue* tools =
      list_response->FindListByDottedPath("result.tools");
  ASSERT_TRUE(tools);
  EXPECT_EQ(29u, tools->size());
  EXPECT_FALSE(approval.has_pending_request());

  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  ASSERT_TRUE(client->Send(ToolCall("call-after-discovery")));
  ASSERT_TRUE(base::test::RunUntil(
      [&approval] { return approval.has_pending_request(); }));
  EXPECT_EQ(browser(), approval.browser());
  approval.Resolve(true);

  std::optional<base::DictValue> call_response = client->Read();
  ASSERT_TRUE(call_response);
  EXPECT_TRUE(call_response->FindDict("result"));
  EXPECT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       MaximumRequestIdProducesBoundedSuccess) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  const std::string request_id(kDaoMcpMaxRequestIdBytes, 's');
  ASSERT_TRUE(client->Send(base::DictValue()
                               .Set("version", kDaoMcpIpcVersion)
                               .Set("id", request_id)
                               .Set("method", "tools/list")
                               .Set("params", base::DictValue())));

  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ(request_id, *response->FindString("id"));
  EXPECT_TRUE(response->FindDict("result"));
  EXPECT_LE(client->last_read_line_bytes(), kDaoMcpMaxLineBytes + 1);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       ToolCallWaitsForApprovalAndUsesExternalLease) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ASSERT_TRUE(client->Send(
      base::DictValue()
          .Set("version", kDaoMcpIpcVersion)
          .Set("id", "call-1")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "get_page_info")
                             .Set("arguments", base::DictValue()))));

  ASSERT_TRUE(base::test::RunUntil(
      [&approval] { return approval.has_pending_request(); }));
  approval.Resolve(true);

  ASSERT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);
  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  const base::DictValue* result = response->FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(true, result->FindBool("ok"));
  EXPECT_FALSE(result->contains("error"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       CancelsToolCallQueuedForApprovalExactlyOnce) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  std::vector<base::DictValue> batch;
  batch.push_back(ToolCall("call-pending"));
  batch.push_back(CancelRequest("call-pending", "cancel-pending"));
  ASSERT_TRUE(client->SendBatch(std::move(batch)));

  std::optional<base::DictValue> call_response = client->Read();
  ASSERT_TRUE(call_response);
  EXPECT_EQ("call-pending", *call_response->FindString("id"));
  const base::DictValue* error = call_response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("TOOL_CANCELLED", *error->FindString("code"));
  std::optional<base::DictValue> cancel_response = client->Read();
  ASSERT_TRUE(cancel_response);
  EXPECT_EQ("cancel-pending", *cancel_response->FindString("id"));
  ASSERT_TRUE(client->Send(base::DictValue()
                               .Set("version", kDaoMcpIpcVersion)
                               .Set("id", "list-after-cancel")
                               .Set("method", "tools/list")
                               .Set("params", base::DictValue())));
  std::optional<base::DictValue> list_response = client->Read();
  ASSERT_TRUE(list_response);
  EXPECT_EQ("list-after-cancel", *list_response->FindString("id"));
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       CancelsExecutingToolCallExactlyOnce) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);
  std::vector<base::DictValue> batch;
  batch.push_back(
      ToolCall("call-active", "execute_script",
               base::DictValue().Set("code", "new Promise(() => {})")));
  batch.push_back(CancelRequest("call-active", "cancel-active"));
  ASSERT_TRUE(client->SendBatch(std::move(batch)));

  std::optional<base::DictValue> first_response = client->Read();
  std::optional<base::DictValue> second_response = client->Read();
  ASSERT_TRUE(first_response);
  ASSERT_TRUE(second_response);
  std::array<base::DictValue, 2> responses = {std::move(*first_response),
                                              std::move(*second_response)};
  const base::DictValue* call_response = nullptr;
  const base::DictValue* cancel_response = nullptr;
  for (const base::DictValue& response : responses) {
    const std::string* id = response.FindString("id");
    ASSERT_TRUE(id);
    if (*id == "call-active") {
      call_response = &response;
    } else if (*id == "cancel-active") {
      cancel_response = &response;
    }
  }
  ASSERT_TRUE(call_response);
  ASSERT_TRUE(cancel_response);
  const base::DictValue* result = call_response->FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(false, result->FindBool("ok"));
  const base::DictValue* error = result->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("TOOL_CANCELLED", *error->FindString("code"));

  ASSERT_TRUE(client->Send(base::DictValue()
                               .Set("version", kDaoMcpIpcVersion)
                               .Set("id", "list-after-active-cancel")
                               .Set("method", "tools/list")
                               .Set("params", base::DictValue())));
  std::optional<base::DictValue> list_response = client->Read();
  ASSERT_TRUE(list_response);
  EXPECT_EQ("list-after-active-cancel", *list_response->FindString("id"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpControlBannerTest,
                       FirstToolCallSnapshotsOneExactBrowserBeforeFocusChanges) {
  Browser* authorized_browser = browser();
  const std::string authorized_url = authorized_browser->tab_strip_model()
                                         ->GetActiveWebContents()
                                         ->GetLastCommittedURL()
                                         .spec();
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ASSERT_TRUE(client->Send(ToolCall("authorize-original-window")));
  WaitForApprovalRequestCount(&approval, 1u);
  ASSERT_EQ(authorized_browser, approval.browser());

  Browser* other_browser = CreateBrowser(authorized_browser->profile());
  other_browser->window()->Activate();
  approval.Resolve(true);

  EXPECT_EQ(authorized_browser, approval.browser());
  ASSERT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);
  EXPECT_EQ(authorized_browser, service_->GetAuthorizedBrowser());
  EXPECT_EQ(authorized_browser->tab_strip_model()->GetActiveWebContents(),
            service_->GetAuthorizedTarget());
  BrowserView* authorized_view =
      BrowserView::GetBrowserViewForBrowser(authorized_browser);
  BrowserView* other_view =
      BrowserView::GetBrowserViewForBrowser(other_browser);
  ASSERT_NE(nullptr, authorized_view);
  ASSERT_NE(nullptr, authorized_view->dao_mcp_control_banner());
  ASSERT_NE(nullptr, other_view);
  ASSERT_NE(nullptr, other_view->dao_mcp_control_banner());
  EXPECT_TRUE(authorized_view->dao_mcp_control_banner()->GetVisible());
  EXPECT_FALSE(other_view->dao_mcp_control_banner()->GetVisible());
  ASSERT_TRUE(client->Send(
      base::DictValue()
          .Set("version", kDaoMcpIpcVersion)
          .Set("id", "call-original-window")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "get_page_info")
                             .Set("arguments", base::DictValue()))));
  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  const base::DictValue* result = response->FindDict("result");
  ASSERT_TRUE(result);
  const base::DictValue* data = result->FindDict("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(authorized_url, *data->FindString("url"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpControlBannerTest,
                       RefreshesReplacementTargetAndClearsRemovedTarget) {
  ASSERT_TRUE(
      content::ExecJs(browser()->tab_strip_model()->GetActiveWebContents(),
                      "document.title = 'Original MCP target'"));
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_NE(nullptr, browser_view);
  DaoMcpControlBannerView* banner = browser_view->dao_mcp_control_banner();
  ASSERT_NE(nullptr, banner);
  ASSERT_TRUE(banner->GetVisible());
  EXPECT_TRUE(HasDescendantLabelText(
      banner, l10n_util::GetStringFUTF16(IDS_DAO_MCP_CONTROL_TARGET,
                                         u"Original MCP target")));

  auto replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->profile()));
  content::WebContents* replacement_ptr = replacement.get();
  std::unique_ptr<content::WebContents> discarded =
      browser()->tab_strip_model()->DiscardWebContentsAt(
          0, std::move(replacement));
  ASSERT_TRUE(content::NavigateToURL(
      replacement_ptr, embedded_test_server()->GetURL("/title2.html")));
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(replacement_ptr, service_->GetAuthorizedTarget());
  EXPECT_EQ(replacement_ptr, banner->web_contents());
  EXPECT_TRUE(banner->GetVisible());
  EXPECT_TRUE(HasDescendantLabelText(
      banner, l10n_util::GetStringFUTF16(IDS_DAO_MCP_CONTROL_TARGET,
                                         u"Title Of Awesomeness")));

  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  const int target_index =
      browser()->tab_strip_model()->GetIndexOfWebContents(replacement_ptr);
  ASSERT_NE(TabStripModel::kNoTab, target_index);
  browser()->tab_strip_model()->DetachAndDeleteWebContentsAt(target_index);

  EXPECT_EQ(nullptr, service_->GetAuthorizedTarget());
  EXPECT_FALSE(banner->GetVisible());
  EXPECT_EQ(nullptr, banner->web_contents());
}

IN_PROC_BROWSER_TEST_F(DaoMcpControlBannerTest, OccupiesDedicatedClientRow) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_NE(nullptr, browser_view);
  DaoMcpControlBannerView* banner = browser_view->dao_mcp_control_banner();
  ASSERT_NE(nullptr, banner);
  ASSERT_TRUE(banner->GetVisible());
  browser_view->GetWidget()->GetRootView()->DeprecatedLayoutImmediately();

  EXPECT_LE(banner->bounds().bottom(),
            browser_view->dao_address_bar()->bounds().y());
  EXPECT_FALSE(banner->bounds().Intersects(
      browser_view->contents_container()->bounds()));
  EXPECT_EQ(HTCLIENT,
            browser_view->NonClientHitTest(banner->bounds().CenterPoint()));
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       MissingApprovalDelegateReleasesConnectionSlot) {
  EnableService();
  std::unique_ptr<TestMcpClient> rejected = ConnectClient();
  ASSERT_TRUE(rejected);
  ASSERT_TRUE(rejected->Send(HelloRequest(nonce())));
  ASSERT_TRUE(rejected->Read());
  ASSERT_TRUE(rejected->Send(ToolCall("call-without-approval-delegate")));
  EXPECT_FALSE(rejected->Read());
  WaitUntilListeningWithoutClient();

  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(replacement->Send(HelloRequest(nonce(), "replacement-hello")));
  ASSERT_TRUE(replacement->Read());
  ASSERT_TRUE(replacement->Send(ToolCall("replacement-call")));
  WaitForApprovalRequestCount(&approval, 1u);
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       ExplicitDenialReleasesConnectionSlot) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> rejected = ConnectClient();
  ASSERT_TRUE(rejected);
  ASSERT_TRUE(rejected->Send(HelloRequest(nonce())));
  ASSERT_TRUE(rejected->Read());
  std::vector<base::DictValue> pending_with_barrier;
  pending_with_barrier.push_back(ToolCall("call-denied"));
  pending_with_barrier.push_back(base::DictValue()
                                     .Set("version", kDaoMcpIpcVersion)
                                     .Set("id", "denial-barrier")
                                     .Set("method", "tools/list")
                                     .Set("params", base::DictValue()));
  ASSERT_TRUE(rejected->SendBatch(std::move(pending_with_barrier)));
  std::optional<base::DictValue> barrier = rejected->Read();
  ASSERT_TRUE(barrier);
  EXPECT_EQ("denial-barrier", *barrier->FindString("id"));

  approval.Resolve(false);

  std::optional<base::DictValue> response = rejected->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ("call-denied", *response->FindString("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("AUTHORIZATION_DENIED", *error->FindString("code"));
  WaitUntilListeningWithoutClient();

  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(replacement->Send(HelloRequest(nonce(), "replacement-hello")));
  ASSERT_TRUE(replacement->Read());
  ASSERT_TRUE(replacement->Send(ToolCall("replacement-call")));
  WaitForApprovalRequestCount(&approval, 2u);
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       StaleApprovalCannotAuthorizeReplacementConnection) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> first = ConnectClient();
  ASSERT_TRUE(first);
  ASSERT_TRUE(first->Send(HelloRequest(nonce(), "first-hello")));
  ASSERT_TRUE(first->Read());
  ASSERT_TRUE(first->Send(ToolCall("first-call")));
  WaitForApprovalRequestCount(&approval, 1u);
  first->Disconnect();
  WaitUntilListeningWithoutClient();
  ASSERT_EQ(1u, approval.cancelled_connection_ids().size());

  std::unique_ptr<TestMcpClient> second = ConnectClient();
  ASSERT_TRUE(second);
  ASSERT_TRUE(second->Send(HelloRequest(nonce(), "second-hello")));
  ASSERT_TRUE(second->Read());
  ASSERT_TRUE(second->Send(ToolCall("second-call")));
  WaitForApprovalRequestCount(&approval, 2u);
  ASSERT_EQ(DaoMcpStatus::kPendingApproval, service_->GetStatus().state);

  approval.ResolveAt(0, true);

  EXPECT_EQ(DaoMcpStatus::kPendingApproval, service_->GetStatus().state);
  approval.ResolveAt(1, false);
  WaitUntilListeningWithoutClient();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       ApprovalTimeoutReleasesConnectionSlot) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  service_->SetTimeoutsForTesting(base::Seconds(5), base::Milliseconds(100));
  EnableService();
  std::unique_ptr<TestMcpClient> rejected = ConnectClient();
  ASSERT_TRUE(rejected);
  ASSERT_TRUE(rejected->Send(HelloRequest(nonce())));
  ASSERT_TRUE(rejected->Read());
  ASSERT_TRUE(rejected->Send(ToolCall("call-timeout")));

  std::optional<base::DictValue> response = rejected->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ("call-timeout", *response->FindString("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("AUTHORIZATION_TIMEOUT", *error->FindString("code"));
  WaitUntilListeningWithoutClient();

  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(replacement->Send(HelloRequest(nonce(), "replacement-hello")));
  ASSERT_TRUE(replacement->Read());
  ASSERT_TRUE(replacement->Send(ToolCall("replacement-call")));
  WaitForApprovalRequestCount(&approval, 2u);
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       ApprovalCancelCannotReenterAsAllowed) {
  AllowDuringCancelApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  service_->SetTimeoutsForTesting(base::Seconds(5), base::Milliseconds(100));
  EnableService();
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_EQ(false, content::EvalJs(
                       contents,
                       "globalThis.reentrantApprovalExecuted = false; false"));
  std::unique_ptr<TestMcpClient> rejected = ConnectClient();
  ASSERT_TRUE(rejected);
  ASSERT_TRUE(rejected->Send(HelloRequest(nonce())));
  ASSERT_TRUE(rejected->Read());
  ASSERT_TRUE(rejected->Send(ToolCall(
      "call-reentrant", "execute_script",
      base::DictValue().Set(
          "code", "globalThis.reentrantApprovalExecuted = true; true"))));

  std::optional<base::DictValue> response = rejected->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ("call-reentrant", *response->FindString("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("AUTHORIZATION_TIMEOUT", *error->FindString("code"));
  WaitUntilListeningWithoutClient();
  EXPECT_EQ(false,
            content::EvalJs(contents, "globalThis.reentrantApprovalExecuted"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       TargetLossDuringApprovalReleasesConnectionSlot) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> rejected = ConnectClient();
  ASSERT_TRUE(rejected);
  ASSERT_TRUE(rejected->Send(HelloRequest(nonce())));
  ASSERT_TRUE(rejected->Read());
  ASSERT_TRUE(rejected->Send(ToolCall("call-target-gone")));
  WaitForApprovalRequestCount(&approval, 1u);
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version")));

  approval.Resolve(true);

  std::optional<base::DictValue> response = rejected->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ("call-target-gone", *response->FindString("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("TARGET_FORBIDDEN", *error->FindString("code"));
  WaitUntilListeningWithoutClient();

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(replacement->Send(HelloRequest(nonce(), "replacement-hello")));
  ASSERT_TRUE(replacement->Read());
  ASSERT_TRUE(replacement->Send(ToolCall("replacement-call")));
  WaitForApprovalRequestCount(&approval, 2u);
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       TerminalLeaseFailureReleasesConnectionSlot) {
  auto acquired = DaoAgentLeaseManager::GetForProfile(browser()->profile())
                      ->TryAcquire({DaoToolClient::kDaoAgent,
                                    "agent-blocking-mcp", "Dao Agent"});
  ASSERT_TRUE(acquired.has_value());
  DaoAgentLease agent_lease = std::move(acquired).value();
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  service_->SetTimeoutsForTesting(base::Seconds(5), base::Milliseconds(100));
  EnableService();
  std::unique_ptr<TestMcpClient> rejected = ConnectClient();
  ASSERT_TRUE(rejected);
  ASSERT_TRUE(rejected->Send(HelloRequest(nonce())));
  ASSERT_TRUE(rejected->Read());
  ASSERT_TRUE(rejected->Send(ToolCall("call-lease-timeout")));

  WaitForApprovalRequestCount(&approval, 1u);
  approval.Resolve(true);

  std::optional<base::DictValue> response = rejected->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ("call-lease-timeout", *response->FindString("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("AUTHORIZATION_TIMEOUT", *error->FindString("code"));
  WaitUntilListeningWithoutClient();
  agent_lease.Reset();

  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(replacement->Send(HelloRequest(nonce(), "replacement-hello")));
  ASSERT_TRUE(replacement->Read());
  ASSERT_TRUE(replacement->Send(ToolCall("replacement-call")));
  WaitForApprovalRequestCount(&approval, 2u);
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       RejectsConcurrentExternalConnection) {
  EnableService();
  std::unique_ptr<TestMcpClient> first = ConnectClient();
  ASSERT_TRUE(first);
  std::unique_ptr<TestMcpClient> second = ConnectClient();
  ASSERT_TRUE(second);

  ASSERT_TRUE(first->Send(HelloRequest(nonce(), "first-hello")));
  EXPECT_TRUE(first->Read());
  EXPECT_FALSE(second->Read());
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest, IdleHelloCandidateIsEvicted) {
  service_->SetTimeoutsForTesting(base::Milliseconds(100), base::Seconds(30));
  EnableService();
  std::unique_ptr<TestMcpClient> idle = ConnectClient();
  ASSERT_TRUE(idle);

  std::optional<base::DictValue> timeout_response = idle->Read();
  ASSERT_TRUE(timeout_response);
  const base::DictValue* error = timeout_response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("AUTHORIZATION_TIMEOUT", *error->FindString("code"));
  EXPECT_FALSE(timeout_response->FindString("id"));
  EXPECT_FALSE(idle->Read());
  WaitUntilListeningWithoutClient();

  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(replacement->Send(HelloRequest(nonce(), "replacement-hello")));
  ASSERT_TRUE(replacement->Read());
  ASSERT_TRUE(replacement->Send(ToolCall("replacement-call")));
  WaitForApprovalRequestCount(&approval, 1u);
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest, BoundsOutstandingToolCalls) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  std::vector<base::DictValue> calls;
  for (size_t index = 0; index < 63; ++index) {
    calls.push_back(ToolCall("call-" + std::to_string(index)));
  }
  calls.push_back(base::DictValue()
                      .Set("version", kDaoMcpIpcVersion)
                      .Set("id", "pending-calls-barrier-1")
                      .Set("method", "tools/list")
                      .Set("params", base::DictValue()));
  ASSERT_TRUE(client->SendBatch(std::move(calls)));
  std::optional<base::DictValue> first_barrier = client->Read();
  ASSERT_TRUE(first_barrier);
  EXPECT_EQ("pending-calls-barrier-1", *first_barrier->FindString("id"));
  std::vector<base::DictValue> final_pending_call;
  final_pending_call.push_back(ToolCall("call-63"));
  final_pending_call.push_back(base::DictValue()
                                   .Set("version", kDaoMcpIpcVersion)
                                   .Set("id", "pending-calls-barrier-2")
                                   .Set("method", "tools/list")
                                   .Set("params", base::DictValue()));
  ASSERT_TRUE(client->SendBatch(std::move(final_pending_call)));
  std::optional<base::DictValue> second_barrier = client->Read();
  ASSERT_TRUE(second_barrier);
  EXPECT_EQ("pending-calls-barrier-2", *second_barrier->FindString("id"));
  ASSERT_TRUE(client->Send(ToolCall("call-64")));

  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ("call-64", *response->FindString("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("INTERNAL_ERROR", *error->FindString("code"));
  client->Disconnect();
  WaitUntilListeningWithoutClient();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       TerminalRequestDropsLaterRequestInSameBatch) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_EQ(false,
            content::EvalJs(contents,
                            "globalThis.postTerminalExecuted = false; false"));
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);
  std::vector<base::DictValue> batch;
  batch.push_back(HelloRequest(nonce(), "duplicate-hello"));
  batch.push_back(
      ToolCall("post-terminal-call", "execute_script",
               base::DictValue().Set(
                   "code", "globalThis.postTerminalExecuted = true; true")));
  ASSERT_TRUE(client->SendBatch(std::move(batch)));

  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ("duplicate-hello", *response->FindString("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("INVALID_ARGUMENT", *error->FindString("code"));
  EXPECT_FALSE(client->Read());
  WaitUntilListeningWithoutClient();
  EXPECT_EQ(false,
            content::EvalJs(contents, "globalThis.postTerminalExecuted"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       BoundsIngressWhileUiThreadIsBlocked) {
  EnableService();
  std::unique_ptr<TestMcpClient> rejected = ConnectClient();
  ASSERT_TRUE(rejected);
  std::string flood = ExactMaximumToolsListLine("flood-1");
  flood.append(ExactMaximumToolsListLine("flood-2"));

  rejected->SendRawWhileBlockingUi(std::move(flood));

  WaitUntilListeningWithoutClient();
  EXPECT_FALSE(rejected->Read());

  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(replacement->Send(HelloRequest(nonce(), "replacement-hello")));
  ASSERT_TRUE(replacement->Read());
  replacement->Disconnect();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest, SlowReaderFloodFailsClosed) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->SetReceiveBufferSize(1024));
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  std::vector<base::DictValue> requests;
  requests.reserve(1200);
  for (size_t index = 0; index < 1200; ++index) {
    requests.push_back(base::DictValue()
                           .Set("version", kDaoMcpIpcVersion)
                           .Set("id", "list-" + std::to_string(index))
                           .Set("method", "tools/list")
                           .Set("params", base::DictValue()));
  }

  client->SendBatch(std::move(requests));

  WaitUntilListeningWithoutClient();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       GracefulCloseHasDrainDeadline) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> slow_reader = ConnectClient();
  ASSERT_TRUE(slow_reader);
  ASSERT_TRUE(slow_reader->SetReceiveBufferSize(1024));
  ASSERT_TRUE(slow_reader->Send(HelloRequest(nonce())));
  ASSERT_TRUE(slow_reader->Read());
  std::vector<base::DictValue> requests;
  requests.reserve(64);
  for (size_t index = 0; index < 63; ++index) {
    const std::string index_string = std::to_string(index);
    std::string request_id(kDaoMcpMaxRequestIdBytes,
                           static_cast<char>('a' + index % 26));
    request_id.replace(0, index_string.size(), index_string);
    requests.push_back(base::DictValue()
                           .Set("version", kDaoMcpIpcVersion)
                           .Set("id", std::move(request_id))
                           .Set("method", "tools/list")
                           .Set("params", base::DictValue()));
  }
  requests.push_back(HelloRequest(nonce(), "duplicate-hello"));
  ASSERT_TRUE(slow_reader->SendBatch(std::move(requests)));

  WaitUntilListeningWithoutClient();
  slow_reader->Disconnect();
  ASSERT_TRUE(base::test::RunUntil(
      [this] { return !service_->connection_active_for_testing(); }));

  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(replacement->Send(HelloRequest(nonce(), "replacement-hello")));
  ASSERT_TRUE(replacement->Read());
  replacement->Disconnect();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       DisableRemovesEndpointAndReleasesLease) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);

  service_->SetEnabled(false);

  EXPECT_EQ(DaoMcpStatus::kDisabled, service_->GetStatus().state);
  base::FilePath user_data_dir =
      base::PathService::CheckedGet(chrome::DIR_USER_DATA);
  auto layout = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      user_data_dir, base::GetCurrentProcId());
  EXPECT_TRUE(base::test::RunUntil(
      [this, layout] { return RuntimeEndpointIsAbsent(layout); }));
  auto agent_lease = DaoAgentLeaseManager::GetForProfile(browser()->profile())
                         ->TryAcquire({DaoToolClient::kDaoAgent,
                                       "agent-after-disable", "Dao Agent"});
  EXPECT_TRUE(agent_lease.has_value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       PackagedHelperCompletesRealServiceRoundTripAndEof) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();

  const base::FilePath output_dir =
      base::PathService::CheckedGet(base::DIR_EXE);
  const base::FilePath packaged_helper = output_dir.AppendASCII("Dao Debug.app")
                                             .AppendASCII("Contents")
                                             .AppendASCII("Helpers")
                                             .AppendASCII("dao-mcp");
  ASSERT_TRUE(base::PathExists(packaged_helper));
  const base::FilePath user_data_dir =
      base::PathService::CheckedGet(chrome::DIR_USER_DATA);
  PackagedHelperProcess helper;
  ASSERT_TRUE(helper.Start(packaged_helper, user_data_dir));

  ASSERT_TRUE(helper.Send(InitializeRequest("initialize-e2e")));
  std::optional<base::DictValue> initialized = helper.ReadResponse();
  ASSERT_TRUE(initialized);
  EXPECT_EQ("initialize-e2e", *initialized->FindString("id"));
  ASSERT_TRUE(initialized->FindDict("result"));
  ASSERT_TRUE(helper.Send(InitializedNotification()));
  ASSERT_TRUE(helper.Send(base::DictValue()
                              .Set("jsonrpc", "2.0")
                              .Set("id", "list-e2e")
                              .Set("method", "tools/list")
                              .Set("params", base::DictValue())));
  std::optional<base::DictValue> tools_response = helper.ReadResponse();
  ASSERT_TRUE(tools_response);
  EXPECT_EQ("list-e2e", *tools_response->FindString("id"));
  const base::ListValue* tools =
      tools_response->FindListByDottedPath("result.tools");
  ASSERT_NE(nullptr, tools);
  EXPECT_EQ(29u, tools->size());
  EXPECT_EQ(DaoMcpStatus::kListening, service_->GetStatus().state);

  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "call-e2e")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "get_page_info")
                             .Set("arguments", base::DictValue()))));
  ASSERT_TRUE(base::test::RunUntil(
      [&approval] { return approval.has_pending_request(); }));
  approval.Resolve(true);
  std::optional<base::DictValue> call_response = helper.ReadResponse();
  ASSERT_TRUE(call_response);
  EXPECT_EQ("call-e2e", *call_response->FindString("id"));
  const base::DictValue* call_result = call_response->FindDict("result");
  ASSERT_NE(nullptr, call_result);
  EXPECT_FALSE(call_result->FindBool("isError").value_or(true));
  const base::DictValue* structured_content =
      call_result->FindDict("structuredContent");
  ASSERT_NE(nullptr, structured_content);
  EXPECT_EQ(service_->GetAuthorizedTarget()->GetLastCommittedURL(),
            GURL(*structured_content->FindString("url")));

  helper.CloseInput();
  EXPECT_TRUE(helper.WaitForExit());
  WaitUntilListeningWithoutClient();
  EXPECT_EQ(nullptr, service_->GetAuthorizedBrowser());
  EXPECT_EQ(nullptr, service_->GetAuthorizedTarget());
  {
    auto agent_lease =
        DaoAgentLeaseManager::GetForProfile(browser()->profile())
            ->TryAcquire({DaoToolClient::kDaoAgent,
                          "agent-after-packaged-helper-eof", "Dao Agent"});
    EXPECT_TRUE(agent_lease.has_value());
  }
  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  replacement->Disconnect();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       DisconnectReleasesExternalLease) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);

  base::RunLoop disconnected;
  base::CallbackListSubscription subscription =
      service_->AddObserver(base::BindRepeating(
          [](base::RunLoop* run_loop, const DaoMcpServiceStatus& status) {
            if (status.state == DaoMcpStatus::kListening) {
              run_loop->Quit();
            }
          },
          &disconnected));
  client->Disconnect();
  disconnected.Run();

  auto agent_lease = DaoAgentLeaseManager::GetForProfile(browser()->profile())
                         ->TryAcquire({DaoToolClient::kDaoAgent,
                                       "agent-after-disconnect", "Dao Agent"});
  EXPECT_TRUE(agent_lease.has_value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       ForbiddenNavigationTerminatesActiveControl) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);
  std::vector<DaoMcpStatus> terminal_states;
  base::CallbackListSubscription status_subscription =
      service_->AddObserver(base::BindRepeating(
          [](std::vector<DaoMcpStatus>* states,
             const DaoMcpServiceStatus& status) {
            states->push_back(status.state);
          },
          &terminal_states));

  const GURL eligible_url = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), eligible_url));
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);
  ASSERT_TRUE(
      client->Send(ToolCall("after-eligible-navigation", "get_page_info")));
  std::optional<base::DictValue> eligible_response = client->Read();
  ASSERT_TRUE(eligible_response);
  const base::DictValue* eligible_result =
      eligible_response->FindDict("result");
  ASSERT_NE(nullptr, eligible_result);
  EXPECT_TRUE(eligible_result->FindBool("ok").value_or(false));
  const base::DictValue* eligible_data = eligible_result->FindDict("data");
  ASSERT_NE(nullptr, eligible_data);
  EXPECT_EQ(eligible_url, GURL(*eligible_data->FindString("url")));
  terminal_states.clear();

  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version/")));

  EXPECT_TRUE(g_browser_process->local_state()->GetBoolean(
      prefs::kDaoMcpServerEnabled));
  ASSERT_TRUE(base::test::RunUntil([this] {
    return service_->GetStatus().state == DaoMcpStatus::kListening &&
           service_->GetAuthorizedBrowser() == nullptr &&
           service_->GetAuthorizedTarget() == nullptr;
  }));
  EXPECT_EQ(std::vector<DaoMcpStatus>({DaoMcpStatus::kListening}),
            terminal_states);
  {
    auto agent_lease =
        DaoAgentLeaseManager::GetForProfile(browser()->profile())
            ->TryAcquire({DaoToolClient::kDaoAgent,
                          "agent-after-forbidden-navigation", "Dao Agent"});
    EXPECT_TRUE(agent_lease.has_value());
  }
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(
      replacement->Send(HelloRequest(nonce(), "replacement-after-lifecycle")));
  ASSERT_TRUE(replacement->Read());
  ASSERT_TRUE(replacement->Send(ToolCall("replacement-call")));
  WaitForApprovalRequestCount(&approval, 2u);
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       TargetCloseTerminatesActiveControl) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);
  content::WebContents* authorized_target = service_->GetAuthorizedTarget();
  ASSERT_NE(nullptr, authorized_target);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  const int target_index =
      browser()->tab_strip_model()->GetIndexOfWebContents(authorized_target);
  ASSERT_NE(TabStripModel::kNoTab, target_index);

  browser()->tab_strip_model()->DetachAndDeleteWebContentsAt(target_index);

  ASSERT_TRUE(base::test::RunUntil([this] {
    return service_->GetStatus().state == DaoMcpStatus::kListening &&
           service_->GetAuthorizedBrowser() == nullptr;
  }));
  auto agent_lease =
      DaoAgentLeaseManager::GetForProfile(browser()->profile())
          ->TryAcquire({DaoToolClient::kDaoAgent, "agent-after-target-close",
                        "Dao Agent"});
  EXPECT_TRUE(agent_lease.has_value());
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpServiceBrowserTest,
    ClosePinnedTargetToForbiddenReplacementFailsClosedWithoutFallback) {
  content::WebContents* pinned =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, pinned);
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);
  ASSERT_EQ(pinned, service_->GetAuthorizedTarget());
  chrome::AddTabAt(browser(), GURL("chrome://version/"), -1, true);
  ASSERT_NE(pinned, browser()->tab_strip_model()->GetActiveWebContents());

  ASSERT_TRUE(client->Send(ToolCall("close-to-forbidden", "close_tab")));
  std::optional<base::DictValue> response = client->Read();
  ASSERT_TRUE(response);
  EXPECT_EQ("close-to-forbidden", *response->FindString("id"));
  const base::DictValue* result = response->FindDict("result");
  ASSERT_NE(nullptr, result);
  EXPECT_FALSE(result->FindBool("ok").value_or(true));
  const base::DictValue* error = result->FindDict("error");
  ASSERT_NE(nullptr, error);
  EXPECT_EQ("TARGET_FORBIDDEN", *error->FindString("code"));
  EXPECT_FALSE(client->Read().has_value());
  WaitUntilListeningWithoutClient();
  EXPECT_EQ(nullptr, service_->GetAuthorizedTarget());
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpServiceBrowserTest,
    DiscardReplacementWaitsForCommitThenRejectsForbiddenNavigation) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);

  auto legal_replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->profile()));
  content::WebContents* legal_contents = legal_replacement.get();
  std::unique_ptr<content::WebContents> first_discarded =
      browser()->tab_strip_model()->DiscardWebContentsAt(
          0, std::move(legal_replacement));
  ASSERT_TRUE(first_discarded);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);
  EXPECT_EQ(legal_contents, service_->GetAuthorizedTarget());

  const GURL discard_url = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(content::NavigateToURL(legal_contents, discard_url));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);
  EXPECT_EQ(legal_contents, service_->GetAuthorizedTarget());
  ASSERT_TRUE(client->Send(ToolCall("after-discard-commit", "get_page_info")));
  std::optional<base::DictValue> discard_response = client->Read();
  ASSERT_TRUE(discard_response);
  const base::DictValue* discard_result = discard_response->FindDict("result");
  ASSERT_NE(nullptr, discard_result);
  EXPECT_TRUE(discard_result->FindBool("ok").value_or(false));
  const base::DictValue* discard_data = discard_result->FindDict("data");
  ASSERT_NE(nullptr, discard_data);
  EXPECT_EQ(discard_url, GURL(*discard_data->FindString("url")));

  auto forbidden_replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->profile()));
  content::WebContents* forbidden_contents = forbidden_replacement.get();
  std::unique_ptr<content::WebContents> second_discarded =
      browser()->tab_strip_model()->DiscardWebContentsAt(
          0, std::move(forbidden_replacement));
  ASSERT_TRUE(second_discarded);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);
  EXPECT_EQ(forbidden_contents, service_->GetAuthorizedTarget());

  ASSERT_TRUE(content::NavigateToURL(forbidden_contents,
                                     GURL("data:text/html,forbidden")));
  WaitUntilListeningWithoutClient();
  EXPECT_EQ(nullptr, service_->GetAuthorizedTarget());
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       AuthorizedWindowCloseTerminatesActiveControl) {
  Browser* authorized_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      authorized_browser, embedded_test_server()->GetURL("/title2.html")));
  ui_test_utils::WaitForBrowserSetLastActive(authorized_browser);
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ASSERT_TRUE(client->Send(ToolCall("authorize-window")));
  WaitForApprovalRequestCount(&approval, 1u);
  ASSERT_EQ(authorized_browser, approval.browser());
  approval.Resolve(true);
  ASSERT_TRUE(client->Read());
  ASSERT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);

  CloseBrowserSynchronously(authorized_browser);

  ASSERT_TRUE(base::test::RunUntil([this] {
    return service_->GetStatus().state == DaoMcpStatus::kListening &&
           service_->GetAuthorizedBrowser() == nullptr;
  }));
  auto agent_lease =
      DaoAgentLeaseManager::GetForProfile(browser()->profile())
          ->TryAcquire({DaoToolClient::kDaoAgent, "agent-after-window-close",
                        "Dao Agent"});
  EXPECT_TRUE(agent_lease.has_value());
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpServiceBrowserTest,
    SecondaryProfileDestroyedNotificationTerminatesActiveControlAndRemovesProfileFromStorage) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_NE(nullptr, profile_manager);
  const base::FilePath secondary_path =
      profile_manager->GenerateNextProfileDirectoryPath();
  Profile& secondary_profile =
      profiles::testing::CreateProfileSync(profile_manager, secondary_path);
  Browser* secondary_browser = CreateBrowser(&secondary_profile);
  ASSERT_NE(nullptr, secondary_browser);
  chrome::AddTabAt(secondary_browser,
                   embedded_test_server()->GetURL("/title2.html"), -1, true);
  content::WebContents* secondary_contents =
      secondary_browser->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, secondary_contents);
  secondary_browser->window()->Show();
  ui_test_utils::WaitForBrowserSetLastActive(secondary_browser);

  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ASSERT_TRUE(client->Send(ToolCall("authorize-secondary-profile")));
  WaitForApprovalRequestCount(&approval, 1u);
  ASSERT_EQ(secondary_browser, approval.browser());
  approval.Resolve(true);
  ASSERT_TRUE(client->Read());
  ASSERT_EQ(DaoMcpStatus::kLeaseActive, service_->GetStatus().state);
  ASSERT_EQ(&secondary_profile,
            service_->GetAuthorizedTarget()->GetBrowserContext());

  secondary_profile.MaybeSendDestroyedNotification();
  EXPECT_FALSE(client->Read().has_value());
  WaitUntilListeningWithoutClient();
  EXPECT_EQ(nullptr, service_->GetAuthorizedBrowser());
  EXPECT_EQ(nullptr, service_->GetAuthorizedTarget());

  CloseBrowserSynchronously(secondary_browser);
  profiles::SetLastUsedProfile(secondary_path.BaseName());
  base::RunLoop deleted;
  profile_manager->GetDeleteProfileHelper().MaybeScheduleProfileForDeletion(
      secondary_path,
      base::BindOnce(
          [](base::RunLoop* run_loop, Profile*) { run_loop->Quit(); },
          &deleted),
      ProfileMetrics::DELETE_PROFILE_USER_MANAGER);
  deleted.Run();

  EXPECT_TRUE(base::test::RunUntil([profile_manager, &secondary_path] {
    return !profile_manager->GetProfileAttributesStorage()
                .GetProfileAttributesWithPath(secondary_path);
  }));
  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  replacement->Disconnect();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       StopCancelsInflightCdpExactlyOnceAndClearsAllState) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);
  content::WebContents* target = service_->GetAuthorizedTarget();
  ASSERT_NE(nullptr, target);
  ASSERT_TRUE(
      client->Send(ToolCall("highlight-before-stop", "highlight_element",
                            base::DictValue().Set("selector", "body"))));
  ASSERT_TRUE(client->Read());
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_NE(nullptr, browser_view);
  ASSERT_NE(nullptr, browser_view->dao_agent_cursor());
  browser_view->dao_agent_cursor()->ShowAtCenter();
  service_->TrackPageCursorForTesting(target);
  EXPECT_EQ(1u, service_->page_highlight_count_for_testing());
  EXPECT_EQ(1u, service_->page_cursor_count_for_testing());
  EXPECT_TRUE(browser_view->dao_agent_cursor()->GetVisible());

  const size_t completions_before =
      service_->tool_call_completion_count_for_testing();
  int stop_callback_count = 0;
  bool stop_armed = true;
  service_->SetDevToolsCommandCallbackForTesting(base::BindRepeating(
      [](DaoMcpService* service, bool* armed, int* callback_count,
         const std::string& method) {
        if (*armed && method == "Runtime.evaluate") {
          *armed = false;
          ++*callback_count;
          service->StopControl();
        }
      },
      service_.get(), &stop_armed, &stop_callback_count));
  ASSERT_TRUE(client->Send(
      ToolCall("inflight-stop", "execute_script",
               base::DictValue()
                   .Set("code", "document.body.dataset.daoMcpLateStop = 'yes'")
                   .Set("lock_tab", true))));
  std::optional<base::DictValue> cancelled = client->Read();
  ASSERT_TRUE(cancelled);
  EXPECT_EQ("inflight-stop", *cancelled->FindString("id"));
  const base::DictValue* cancelled_result = cancelled->FindDict("result");
  ASSERT_NE(nullptr, cancelled_result);
  EXPECT_FALSE(cancelled_result->FindBool("ok").value_or(true));
  const base::DictValue* cancelled_error = cancelled_result->FindDict("error");
  ASSERT_NE(nullptr, cancelled_error);
  EXPECT_EQ("TOOL_CANCELLED", *cancelled_error->FindString("code"));
  EXPECT_FALSE(client->Read().has_value());
  service_->SetDevToolsCommandCallbackForTesting({});
  WaitUntilListeningWithoutClient();

  EXPECT_EQ(1, stop_callback_count);
  EXPECT_EQ(completions_before + 1,
            service_->tool_call_completion_count_for_testing());
  EXPECT_EQ(0u, service_->active_tool_call_count_for_testing());
  EXPECT_EQ(0u, service_->pending_executor_call_count_for_testing());
  EXPECT_EQ(0u, service_->pending_devtools_command_count_for_testing());
  EXPECT_FALSE(service_->devtools_attached_for_testing());
  EXPECT_EQ(0u, service_->page_operation_count_for_testing());
  EXPECT_EQ(0u, service_->page_lock_count_for_testing());
  EXPECT_EQ(0u, service_->page_highlight_count_for_testing());
  EXPECT_EQ(0u, service_->page_cursor_count_for_testing());
  EXPECT_FALSE(DaoAgentLockTabHelper::IsLocked(target));
  EXPECT_EQ("absent",
            content::EvalJs(target,
                            "document.body.dataset.daoMcpLateStop || 'absent'")
                .ExtractString());
  EXPECT_FALSE(browser_view->dao_agent_cursor()->GetVisible());
  EXPECT_EQ(nullptr, service_->GetAuthorizedBrowser());
  EXPECT_EQ(nullptr, service_->GetAuthorizedTarget());
  auto agent_lease = DaoAgentLeaseManager::GetForProfile(browser()->profile())
                         ->TryAcquire({DaoToolClient::kDaoAgent,
                                       "agent-after-active-stop", "Dao Agent"});
  EXPECT_TRUE(agent_lease.has_value());
  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  replacement->Disconnect();
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       DisableDuringInflightCdpCompletesOnceAndClearsAllState) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);
  content::WebContents* target = service_->GetAuthorizedTarget();
  ASSERT_NE(nullptr, target);

  const size_t completions_before =
      service_->tool_call_completion_count_for_testing();
  int disable_callback_count = 0;
  bool disable_armed = true;
  service_->SetDevToolsCommandCallbackForTesting(base::BindRepeating(
      [](DaoMcpService* service, bool* armed, int* callback_count,
         const std::string& method) {
        if (*armed && method == "Runtime.evaluate") {
          *armed = false;
          ++*callback_count;
          service->SetEnabled(false);
        }
      },
      service_.get(), &disable_armed, &disable_callback_count));
  ASSERT_TRUE(client->Send(ToolCall("inflight-disable", "execute_script",
                                    base::DictValue()
                                        .Set("code",
                                             "document.body.dataset."
                                             "daoMcpLateDisable = 'yes'")
                                        .Set("lock_tab", true))));
  EXPECT_FALSE(client->Read().has_value());
  service_->SetDevToolsCommandCallbackForTesting({});

  EXPECT_EQ(1, disable_callback_count);
  EXPECT_EQ(completions_before + 1,
            service_->tool_call_completion_count_for_testing());
  EXPECT_EQ(DaoMcpStatus::kDisabled, service_->GetStatus().state);
  EXPECT_EQ(0u, service_->active_tool_call_count_for_testing());
  EXPECT_EQ(0u, service_->pending_executor_call_count_for_testing());
  EXPECT_EQ(0u, service_->pending_devtools_command_count_for_testing());
  EXPECT_FALSE(service_->devtools_attached_for_testing());
  EXPECT_EQ(0u, service_->page_operation_count_for_testing());
  EXPECT_EQ(0u, service_->page_lock_count_for_testing());
  EXPECT_EQ(0u, service_->page_highlight_count_for_testing());
  EXPECT_EQ(0u, service_->page_cursor_count_for_testing());
  EXPECT_FALSE(DaoAgentLockTabHelper::IsLocked(target));
  EXPECT_EQ("absent",
            content::EvalJs(
                target, "document.body.dataset.daoMcpLateDisable || 'absent'")
                .ExtractString());
  EXPECT_EQ(nullptr, service_->GetAuthorizedBrowser());
  EXPECT_EQ(nullptr, service_->GetAuthorizedTarget());

  base::FilePath user_data_dir =
      base::PathService::CheckedGet(chrome::DIR_USER_DATA);
  auto layout = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      user_data_dir, base::GetCurrentProcId());
  EXPECT_TRUE(base::test::RunUntil(
      [this, layout] { return RuntimeEndpointIsAbsent(layout); }));
  auto agent_lease =
      DaoAgentLeaseManager::GetForProfile(browser()->profile())
          ->TryAcquire({DaoToolClient::kDaoAgent, "agent-after-active-disable",
                        "Dao Agent"});
  EXPECT_TRUE(agent_lease.has_value());

  EnableService();
  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  replacement->Disconnect();
}

IN_PROC_BROWSER_TEST_F(DaoMcpControlBannerTest,
                       StopControlAllowsAReplacementConnection) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> first = ConnectClient();
  ASSERT_TRUE(first);
  ASSERT_TRUE(first->Send(HelloRequest(nonce())));
  ASSERT_TRUE(first->Read());
  ApproveFirstToolCall(first.get(), &approval);

  service_->StopControl();

  WaitUntilListeningWithoutClient();
  EXPECT_EQ(DaoMcpStatus::kListening, service_->GetStatus().state);
  EXPECT_EQ(nullptr, service_->GetAuthorizedBrowser());
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_NE(nullptr, browser_view);
  ASSERT_NE(nullptr, browser_view->dao_mcp_control_banner());
  EXPECT_FALSE(browser_view->dao_mcp_control_banner()->GetVisible());
  {
    auto agent_lease = DaoAgentLeaseManager::GetForProfile(browser()->profile())
                           ->TryAcquire({DaoToolClient::kDaoAgent,
                                         "agent-after-stop", "Dao Agent"});
    EXPECT_TRUE(agent_lease.has_value());
  }

  std::unique_ptr<TestMcpClient> replacement = ConnectClient();
  ASSERT_TRUE(replacement);
  ASSERT_TRUE(replacement->Send(HelloRequest(nonce(), "replacement-hello")));
  ASSERT_TRUE(replacement->Read());
  ASSERT_TRUE(replacement->Send(ToolCall("replacement-call")));
  WaitForApprovalRequestCount(&approval, 2u);
  approval.Resolve(false);
}

IN_PROC_BROWSER_TEST_F(DaoMcpControlBannerTest,
                       StopButtonCancelsInflightWorkAndReleasesLease) {
  FakeApprovalDelegate approval;
  service_->SetApprovalDelegate(&approval);
  EnableService();
  std::unique_ptr<TestMcpClient> client = ConnectClient();
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->Send(HelloRequest(nonce())));
  ASSERT_TRUE(client->Read());
  ApproveFirstToolCall(client.get(), &approval);

  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, target);
  ASSERT_TRUE(client->Send(
      ToolCall("inflight-stop", "execute_script",
               base::DictValue()
                   .Set("code",
                        "(() => { const end = Date.now() + 2000; "
                        "while (Date.now() < end) {} return 'done'; })()")
                   .Set("lock_tab", true))));
  ASSERT_TRUE(base::test::RunUntil(
      [target] { return DaoAgentLockTabHelper::IsLocked(target); }));

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_NE(nullptr, browser_view);
  DaoMcpControlBannerView* banner = browser_view->dao_mcp_control_banner();
  ASSERT_NE(nullptr, banner);
  ASSERT_TRUE(banner->GetVisible());
  ASSERT_NE(nullptr, banner->stop_button_for_testing());
  ui::MouseEvent event(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       base::TimeTicks::Now(), ui::EF_NONE,
                       ui::EF_LEFT_MOUSE_BUTTON);
  views::test::ButtonTestApi(banner->stop_button_for_testing())
      .NotifyClick(event);

  EXPECT_TRUE(base::test::RunUntil([this, target] {
    return service_->GetStatus().state == DaoMcpStatus::kListening &&
           !DaoAgentLockTabHelper::IsLocked(target);
  }));
  EXPECT_FALSE(banner->GetVisible());
  EXPECT_EQ(nullptr, service_->GetAuthorizedBrowser());
  auto agent_lease = DaoAgentLeaseManager::GetForProfile(browser()->profile())
                         ->TryAcquire({DaoToolClient::kDaoAgent,
                                       "agent-after-stop-button", "Dao Agent"});
  EXPECT_TRUE(agent_lease.has_value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest, ReenableRotatesNonce) {
  EnableService();
  const std::string first_nonce = nonce();
  service_->SetEnabled(false);
  EnableService();

  EXPECT_NE(first_nonce, nonce());
}

IN_PROC_BROWSER_TEST_F(DaoMcpServiceBrowserTest,
                       ShutdownRemovesEndpointAndMetadata) {
  EnableService();
  base::FilePath user_data_dir =
      base::PathService::CheckedGet(chrome::DIR_USER_DATA);
  auto layout = base::MakeRefCounted<DaoMcpRuntimeFiles>(
      user_data_dir, base::GetCurrentProcId());
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::PathExists(layout->socket_path()));
    ASSERT_TRUE(base::PathExists(layout->metadata_path()));
  }

  service_->Shutdown();

  EXPECT_EQ(DaoMcpStatus::kDisabled, service_->GetStatus().state);
  EXPECT_TRUE(base::test::RunUntil(
      [this, layout] { return RuntimeEndpointIsAbsent(layout); }));
}

}  // namespace
}  // namespace dao
