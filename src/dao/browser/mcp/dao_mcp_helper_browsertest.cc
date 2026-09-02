// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/process/process.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

constexpr char kMcpProtocolVersion[] = "2025-11-25";

#if defined(DAO_MCP_VERSION)
constexpr char kExpectedDaoMcpVersion[] = DAO_MCP_VERSION;
#else
constexpr char kExpectedDaoMcpVersion[] = "missing DAO_MCP_VERSION";
#endif

base::DictValue InitializeRequest(base::Value id) {
  return base::DictValue()
      .Set("jsonrpc", "2.0")
      .Set("id", std::move(id))
      .Set("method", "initialize")
      .Set("params", base::DictValue()
                         .Set("protocolVersion", kMcpProtocolVersion)
                         .Set("capabilities", base::DictValue())
                         .Set("clientInfo", base::DictValue()
                                                .Set("name", "dao-test-client")
                                                .Set("version", "1.0")));
}

base::DictValue InitializedNotification() {
  return base::DictValue()
      .Set("jsonrpc", "2.0")
      .Set("method", "notifications/initialized")
      .Set("params", base::DictValue());
}

class HelperProcess {
 public:
  HelperProcess() = default;

  HelperProcess(const HelperProcess&) = delete;
  HelperProcess& operator=(const HelperProcess&) = delete;

  ~HelperProcess() {
    CloseInput();
    if (process_.IsValid()) {
      process_.Terminate(/*exit_code=*/1, /*wait=*/true);
    }
  }

  bool Start(const base::FilePath& user_data_dir) {
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

    base::FilePath helper_path =
        base::PathService::CheckedGet(base::DIR_EXE).AppendASCII("dao-mcp");
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
    if (!base::JSONWriter::Write(message, &serialized)) {
      return false;
    }
    serialized.push_back('\n');
    return base::WriteFileDescriptor(stdin_.get(), serialized);
  }

  bool SendBatch(std::vector<base::DictValue> messages) {
    std::string serialized;
    for (base::DictValue& message : messages) {
      std::string line;
      if (!base::JSONWriter::Write(message, &line)) {
        return false;
      }
      serialized.append(line);
      serialized.push_back('\n');
    }
    return base::WriteFileDescriptor(stdin_.get(), serialized);
  }

  std::optional<base::DictValue> ReadResponse() {
    std::optional<std::string> line = ReadLine(stdout_.get(), &stdout_buffer_);
    if (!line) {
      return std::nullopt;
    }
    stdout_lines_.push_back(*line);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(*line, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      return std::nullopt;
    }
    return std::move(*parsed).TakeDict();
  }

  bool HasResponse() {
    pollfd descriptor = {
        .fd = stdout_.get(),
        .events = POLLIN,
        .revents = 0,
    };
    return poll(&descriptor, 1, 200) > 0 && (descriptor.revents & POLLIN);
  }

  std::string ReadStderr() {
    std::string output;
    char buffer[4096];
    while (true) {
      const ssize_t bytes = read(stderr_.get(), buffer, sizeof(buffer));
      if (bytes > 0) {
        output.append(buffer, static_cast<size_t>(bytes));
        continue;
      }
      if (bytes < 0 && errno == EINTR) {
        continue;
      }
      return output;
    }
  }

  void DrainStdout() {
    while (std::optional<std::string> line =
               ReadLine(stdout_.get(), &stdout_buffer_)) {
      stdout_lines_.push_back(*line);
    }
  }

  void CloseInput() { stdin_.reset(); }

  bool WaitForExit(int* exit_code) {
    base::ScopedAllowBaseSyncPrimitivesForTesting allow_wait;
    const bool exited =
        process_.WaitForExitWithTimeout(base::Seconds(5), exit_code);
    if (exited) {
      process_.Close();
    }
    return exited;
  }

  const std::vector<std::string>& stdout_lines() const { return stdout_lines_; }

 private:
  static std::optional<std::string> ReadLine(int fd, std::string* buffer) {
    while (true) {
      const size_t newline = buffer->find('\n');
      if (newline != std::string::npos) {
        std::string line = buffer->substr(0, newline);
        buffer->erase(0, newline + 1);
        return line;
      }

      pollfd descriptor = {
          .fd = fd,
          .events = POLLIN,
          .revents = 0,
      };
      int poll_result;
      do {
        poll_result = poll(&descriptor, 1, 5000);
      } while (poll_result < 0 && errno == EINTR);
      if (poll_result <= 0) {
        return std::nullopt;
      }
      if (!(descriptor.revents & (POLLIN | POLLHUP | POLLERR))) {
        return std::nullopt;
      }

      char chunk[4096];
      const ssize_t bytes = read(fd, chunk, sizeof(chunk));
      if (bytes > 0) {
        buffer->append(chunk, static_cast<size_t>(bytes));
        continue;
      }
      if (bytes < 0 && errno == EINTR) {
        continue;
      }
      return std::nullopt;
    }
  }

  base::Process process_;
  base::ScopedFD stdin_;
  base::ScopedFD stdout_;
  base::ScopedFD stderr_;
  std::string stdout_buffer_;
  std::vector<std::string> stdout_lines_;
};

class FakeBrowserServer {
 public:
  explicit FakeBrowserServer(const base::FilePath& user_data_dir)
      : runtime_dir_(user_data_dir.AppendASCII("MCP")),
        socket_path_(runtime_dir_.AppendASCII("mcp.sock")),
        metadata_path_(runtime_dir_.AppendASCII("runtime.json")) {}

  ~FakeBrowserServer() {
    connection_.reset();
    listener_.reset();
  }

  bool Start() {
    if (!base::CreateDirectory(runtime_dir_) ||
        !base::SetPosixFilePermissions(runtime_dir_, 0700)) {
      return false;
    }
    listener_.reset(socket(AF_UNIX, SOCK_STREAM, 0));
    if (!listener_.is_valid()) {
      return false;
    }
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    if (socket_path_.value().size() >= sizeof(address.sun_path)) {
      return false;
    }
    base::span(address.sun_path).copy_prefix_from(socket_path_.value());
    if (bind(listener_.get(), reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0 ||
        listen(listener_.get(), 1) != 0 ||
        !base::SetPosixFilePermissions(socket_path_, 0600)) {
      return false;
    }
    std::string metadata;
    if (!base::JSONWriter::Write(
            base::DictValue()
                .Set("version", 1)
                .Set("socket_path", socket_path_.value())
                .Set("browser_pid", static_cast<int>(getpid()))
                .Set("nonce", std::string(64, 'a')),
            &metadata) ||
        !base::WriteFile(metadata_path_, metadata) ||
        !base::SetPosixFilePermissions(metadata_path_, 0600)) {
      return false;
    }
    return true;
  }

  bool Accept() {
    int accepted;
    do {
      accepted = accept(listener_.get(), nullptr, nullptr);
    } while (accepted < 0 && errno == EINTR);
    connection_.reset(accepted);
    return connection_.is_valid();
  }

  std::optional<base::DictValue> ReadRequest() {
    std::optional<std::string> line =
        HelperProcessReadLine(connection_.get(), &received_);
    if (!line) {
      return std::nullopt;
    }
    std::optional<base::Value> parsed =
        base::JSONReader::Read(*line, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      return std::nullopt;
    }
    return std::move(*parsed).TakeDict();
  }

  bool Send(base::DictValue response) {
    std::string serialized;
    return base::JSONWriter::Write(response, &serialized) &&
           base::WriteFileDescriptor(connection_.get(), serialized + "\n");
  }

  bool SendBatchAndClose(std::vector<base::DictValue> responses) {
    std::string serialized;
    for (base::DictValue& response : responses) {
      std::string line;
      if (!base::JSONWriter::Write(response, &line)) {
        return false;
      }
      serialized.append(line);
      serialized.push_back('\n');
    }
    const bool written =
        base::WriteFileDescriptor(connection_.get(), serialized);
    connection_.reset();
    return written;
  }

  bool ReplaceSocketWithSymlink() {
    const base::FilePath real_socket =
        runtime_dir_.AppendASCII("real-mcp.sock");
    if (rename(socket_path_.value().c_str(), real_socket.value().c_str()) !=
        0) {
      return false;
    }
    return symlink(real_socket.value().c_str(), socket_path_.value().c_str()) ==
           0;
  }

 private:
  static std::optional<std::string> HelperProcessReadLine(int fd,
                                                          std::string* buffer) {
    while (true) {
      const size_t newline = buffer->find('\n');
      if (newline != std::string::npos) {
        std::string line = buffer->substr(0, newline);
        buffer->erase(0, newline + 1);
        return line;
      }
      pollfd descriptor = {.fd = fd, .events = POLLIN, .revents = 0};
      int result;
      do {
        result = poll(&descriptor, 1, 5000);
      } while (result < 0 && errno == EINTR);
      if (result <= 0) {
        return std::nullopt;
      }
      char chunk[4096];
      const ssize_t bytes = read(fd, chunk, sizeof(chunk));
      if (bytes > 0) {
        buffer->append(chunk, static_cast<size_t>(bytes));
        continue;
      }
      if (bytes < 0 && errno == EINTR) {
        continue;
      }
      return std::nullopt;
    }
  }

  base::FilePath runtime_dir_;
  base::FilePath socket_path_;
  base::FilePath metadata_path_;
  base::ScopedFD listener_;
  base::ScopedFD connection_;
  std::string received_;
};

class DaoMcpHelperBrowserTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(user_data_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir user_data_dir_;
};

TEST_F(DaoMcpHelperBrowserTest, BuildsStandaloneAndPackagedExecutables) {
  const base::FilePath output_dir =
      base::PathService::CheckedGet(base::DIR_EXE);
  const base::FilePath standalone = output_dir.AppendASCII("dao-mcp");
  const base::FilePath packaged = output_dir.AppendASCII("Dao Debug.app")
                                      .AppendASCII("Contents")
                                      .AppendASCII("Helpers")
                                      .AppendASCII("dao-mcp");
  EXPECT_TRUE(base::PathExists(standalone));
  EXPECT_EQ(0, access(standalone.value().c_str(), X_OK));
  EXPECT_TRUE(base::PathExists(packaged));
  EXPECT_EQ(0, access(packaged.value().c_str(), X_OK));
}

TEST_F(DaoMcpHelperBrowserTest, RejectsUnsupportedProtocolVersion) {
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", 7)
          .Set("method", "initialize")
          .Set("params",
               base::DictValue()
                   .Set("protocolVersion", "2024-11-05")
                   .Set("capabilities", base::DictValue())
                   .Set("clientInfo", base::DictValue()
                                          .Set("name", "dao-test-client")
                                          .Set("version", "1.0")))));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  EXPECT_EQ("2.0", *response->FindString("jsonrpc"));
  EXPECT_EQ(7, response->FindInt("id"));
  const base::DictValue* error = response->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ(-32602, error->FindInt("code"));
  const base::DictValue* data = error->FindDict("data");
  ASSERT_TRUE(data);
  EXPECT_EQ(kMcpProtocolVersion, *data->FindString("supported"));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
  helper.DrainStdout();
  for (const std::string& line : helper.stdout_lines()) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed && parsed->is_dict());
  }
}

TEST_F(DaoMcpHelperBrowserTest, NegotiatesCodexProtocolVersion) {
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", 8)
          .Set("method", "initialize")
          .Set("params",
               base::DictValue()
                   .Set("protocolVersion", "2025-06-18")
                   .Set("capabilities", base::DictValue())
                   .Set("clientInfo", base::DictValue()
                                          .Set("name", "codex")
                                          .Set("version", "0.145.0")))));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  EXPECT_EQ("2.0", *response->FindString("jsonrpc"));
  EXPECT_EQ(8, response->FindInt("id"));
  const base::DictValue* result = response->FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ("2025-06-18", *result->FindString("protocolVersion"));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest, DisablesCodexToolCatalogCache) {
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", 9)
          .Set("method", "initialize")
          .Set("params",
               base::DictValue()
                   .Set("protocolVersion", "2025-06-18")
                   .Set("capabilities", base::DictValue())
                   .Set("clientInfo", base::DictValue()
                                          .Set("name", "codex")
                                          .Set("version", "0.145.0")))));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  const base::DictValue* result = response->FindDict("result");
  ASSERT_TRUE(result);
  const base::DictValue* capabilities = result->FindDict("capabilities");
  ASSERT_TRUE(capabilities);
  const base::DictValue* experimental =
      capabilities->FindDict("experimental");
  ASSERT_TRUE(experimental);
  const base::DictValue* tool_catalog_cache =
      experimental->FindDict("codex/tool-catalog-cache");
  ASSERT_TRUE(tool_catalog_cache);
  EXPECT_EQ(false, tool_catalog_cache->FindBool("cacheable"));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest,
       InitializesWithDaoVersionAndRequiresInitializedNotification) {
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value("init-1"))));

  std::optional<base::DictValue> initialized = helper.ReadResponse();
  ASSERT_TRUE(initialized);
  EXPECT_EQ("init-1", *initialized->FindString("id"));
  const base::DictValue* result = initialized->FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_EQ(kMcpProtocolVersion, *result->FindString("protocolVersion"));
  const std::string* instructions = result->FindString("instructions");
  ASSERT_TRUE(instructions);
  EXPECT_EQ(
      "Use this server whenever the user asks to inspect or operate Dao "
      "Browser, including the current page, tabs, navigation, or page "
      "interaction. Prefer these tools over generic browser automation for "
      "Dao Browser. Use list_tabs to establish the initial current target, "
      "then pass its tab_id to page-specific tools. Preserve that target "
      "across follow-up requests unless the user explicitly asks to switch "
      "browser tabs. Treat ambiguous requests such as open, click, or select "
      "X as page-local: inspect the current target with "
      "query_elements, then pass its document_id, snapshot_id, and ref_id to "
      "click_by_ref. Use switch_tab only for explicit browser-tab navigation.",
      *instructions);
  const base::DictValue* server_info = result->FindDict("serverInfo");
  ASSERT_TRUE(server_info);
  EXPECT_EQ("dao-browser", *server_info->FindString("name"));
  EXPECT_EQ(kExpectedDaoMcpVersion, *server_info->FindString("version"));
  ASSERT_TRUE(result->FindDict("capabilities"));
  EXPECT_TRUE(result->FindDict("capabilities")->FindDict("tools"));

  ASSERT_TRUE(helper.Send(base::DictValue()
                              .Set("jsonrpc", "2.0")
                              .Set("id", 2)
                              .Set("method", "tools/list")
                              .Set("params", base::DictValue())));
  std::optional<base::DictValue> early_response = helper.ReadResponse();
  ASSERT_TRUE(early_response);
  EXPECT_EQ(-32002, early_response->FindDict("error")->FindInt("code"));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest,
       RejectsFractionalIdsAndMissingClientCapabilities) {
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  base::DictValue missing_capabilities =
      InitializeRequest(base::Value("missing-capabilities"));
  missing_capabilities.FindDict("params")->Remove("capabilities");
  ASSERT_TRUE(helper.Send(std::move(missing_capabilities)));

  std::optional<base::DictValue> capabilities_error = helper.ReadResponse();
  ASSERT_TRUE(capabilities_error);
  EXPECT_EQ("missing-capabilities", *capabilities_error->FindString("id"));
  EXPECT_EQ(-32602, capabilities_error->FindDict("error")->FindInt("code"));

  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1.5))));
  std::optional<base::DictValue> id_error = helper.ReadResponse();
  ASSERT_TRUE(id_error);
  EXPECT_TRUE(id_error->Find("id")->is_none());
  EXPECT_EQ(-32600, id_error->FindDict("error")->FindInt("code"));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
  helper.DrainStdout();
  for (const std::string& line : helper.stdout_lines()) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed && parsed->is_dict());
  }
}

TEST_F(DaoMcpHelperBrowserTest,
       DisabledBrowserReturnsToolErrorAndKeepsStdoutPure) {
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));
  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "call-disabled")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "get_page_info")
                             .Set("arguments", base::DictValue()))));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  const base::DictValue* result = response->FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindBool("isError").value_or(false));
  const base::DictValue* structured = result->FindDict("structuredContent");
  ASSERT_TRUE(structured);
  const base::DictValue* error = structured->FindDict("error");
  ASSERT_TRUE(error);
  EXPECT_EQ("MCP_DISABLED", *error->FindString("code"));

  helper.CloseInput();
  int exit_code = -1;
  ASSERT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
  helper.DrainStdout();
  EXPECT_EQ("dao-mcp: Dao Browser MCP server is unavailable\n",
            helper.ReadStderr());
  for (const std::string& line : helper.stdout_lines()) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed && parsed->is_dict());
  }
}

TEST_F(DaoMcpHelperBrowserTest, AdaptsToolCatalogAnnotationsAndPrivateFields) {
  FakeBrowserServer browser(user_data_dir_.GetPath());
  ASSERT_TRUE(browser.Start());
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));
  ASSERT_TRUE(helper.Send(base::DictValue()
                              .Set("jsonrpc", "2.0")
                              .Set("id", 2)
                              .Set("method", "tools/list")
                              .Set("params", base::DictValue())));
  ASSERT_TRUE(browser.Accept());
  std::optional<base::DictValue> hello = browser.ReadRequest();
  std::optional<base::DictValue> list = browser.ReadRequest();
  ASSERT_TRUE(hello);
  ASSERT_TRUE(list);
  EXPECT_EQ("hello", *hello->FindString("id"));
  const std::string internal_id = *list->FindString("id");
  ASSERT_TRUE(
      browser.Send(base::DictValue()
                       .Set("version", 1)
                       .Set("id", "hello")
                       .Set("result", base::DictValue()
                                          .Set("connection_id", "connection-1")
                                          .Set("status", "pending_approval"))));

  base::ListValue tools;
  for (int index = 0; index < 31; ++index) {
    const std::string side_effect = index == 1   ? "interaction"
                                    : index == 2 ? "destructive"
                                                 : "read";
    tools.Append(
        base::DictValue()
            .Set("name", "tool_" + std::to_string(index))
            .Set("description", "A test tool")
            .Set("inputSchema", base::DictValue().Set("type", "object"))
            .Set("sideEffect", side_effect)
            .Set("timeoutMs", 1000));
  }
  ASSERT_TRUE(browser.Send(
      base::DictValue()
          .Set("version", 1)
          .Set("id", internal_id)
          .Set("result", base::DictValue().Set("tools", std::move(tools)))));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  const base::ListValue* adapted =
      response->FindDict("result")->FindList("tools");
  ASSERT_TRUE(adapted);
  ASSERT_EQ(31u, adapted->size());
  for (const base::Value& value : *adapted) {
    const base::DictValue& tool = value.GetDict();
    EXPECT_EQ(
        "For Dao Browser inspection or operation, prefer this MCP server "
        "over generic browser automation. A test tool",
        *tool.FindString("description"));
    EXPECT_FALSE(tool.contains("sideEffect"));
    EXPECT_FALSE(tool.contains("timeoutMs"));
    EXPECT_TRUE(tool.FindDict("annotations"));
  }
  EXPECT_TRUE((*adapted)[0]
                  .GetDict()
                  .FindDict("annotations")
                  ->FindBool("readOnlyHint")
                  .value_or(false));
  EXPECT_TRUE((*adapted)[2]
                  .GetDict()
                  .FindDict("annotations")
                  ->FindBool("destructiveHint")
                  .value_or(false));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest,
       PreservesStructuredHelloErrorForPendingToolCall) {
  FakeBrowserServer browser(user_data_dir_.GetPath());
  ASSERT_TRUE(browser.Start());
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));
  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "hello-forbidden")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "get_page_info")
                             .Set("arguments", base::DictValue()))));

  ASSERT_TRUE(browser.Accept());
  std::optional<base::DictValue> hello = browser.ReadRequest();
  std::optional<base::DictValue> call = browser.ReadRequest();
  ASSERT_TRUE(hello);
  ASSERT_TRUE(call);
  EXPECT_EQ("hello", *hello->FindString("id"));
  ASSERT_TRUE(browser.Send(
      base::DictValue()
          .Set("version", 1)
          .Set("id", "hello")
          .Set("error",
               base::DictValue()
                   .Set("code", "TARGET_FORBIDDEN")
                   .Set("message", "The selected browser target is forbidden.")
                   .Set("retryable", false))));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  EXPECT_EQ("hello-forbidden", *response->FindString("id"));
  const base::DictValue* result = response->FindDict("result");
  ASSERT_NE(nullptr, result);
  EXPECT_TRUE(result->FindBool("isError").value_or(false));
  const base::DictValue* error =
      result->FindDict("structuredContent")->FindDict("error");
  ASSERT_NE(nullptr, error);
  EXPECT_EQ("TARGET_FORBIDDEN", *error->FindString("code"));
  EXPECT_EQ("The selected browser target is forbidden.",
            *error->FindString("message"));
  EXPECT_FALSE(error->FindBool("retryable").value_or(true));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest, ForwardsMissingToolArgumentsAsAnEmptyObject) {
  FakeBrowserServer browser(user_data_dir_.GetPath());
  ASSERT_TRUE(browser.Start());
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));
  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "optional-arguments")
          .Set("method", "tools/call")
          .Set("params", base::DictValue().Set("name", "get_page_info"))));

  ASSERT_TRUE(browser.Accept());
  ASSERT_TRUE(browser.ReadRequest());
  std::optional<base::DictValue> call = browser.ReadRequest();
  ASSERT_TRUE(call);
  const base::DictValue* forwarded_arguments =
      call->FindDict("params")->FindDict("arguments");
  ASSERT_TRUE(forwarded_arguments);
  EXPECT_TRUE(forwarded_arguments->empty());

  ASSERT_TRUE(
      browser.Send(base::DictValue()
                       .Set("version", 1)
                       .Set("id", "hello")
                       .Set("result", base::DictValue()
                                          .Set("connection_id", "connection-1")
                                          .Set("status", "pending_approval"))));
  ASSERT_TRUE(browser.Send(
      base::DictValue()
          .Set("version", 1)
          .Set("id", *call->FindString("id"))
          .Set("result", base::DictValue()
                             .Set("ok", true)
                             .Set("data", base::DictValue().Set(
                                              "url", "https://dao.example")))));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  EXPECT_EQ("optional-arguments", *response->FindString("id"));
  EXPECT_EQ("https://dao.example", *response->FindDict("result")
                                        ->FindDict("structuredContent")
                                        ->FindString("url"));

  ASSERT_TRUE(
      helper.Send(base::DictValue()
                      .Set("jsonrpc", "2.0")
                      .Set("id", "invalid-arguments")
                      .Set("method", "tools/call")
                      .Set("params", base::DictValue()
                                         .Set("name", "get_page_info")
                                         .Set("arguments", "not-an-object"))));
  std::optional<base::DictValue> invalid_arguments = helper.ReadResponse();
  ASSERT_TRUE(invalid_arguments);
  EXPECT_EQ("invalid-arguments", *invalid_arguments->FindString("id"));
  EXPECT_EQ(-32602, invalid_arguments->FindDict("error")->FindInt("code"));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest,
       DeliversFinalBrowserBatchBeforeEofAndRejectsWrongVersion) {
  FakeBrowserServer browser(user_data_dir_.GetPath());
  ASSERT_TRUE(browser.Start());
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));
  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "final-batch")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "get_page_info")
                             .Set("arguments", base::DictValue()))));
  ASSERT_TRUE(browser.Accept());
  ASSERT_TRUE(browser.ReadRequest());
  std::optional<base::DictValue> call = browser.ReadRequest();
  ASSERT_TRUE(call);
  std::vector<base::DictValue> final_batch;
  final_batch.push_back(
      base::DictValue()
          .Set("version", 1)
          .Set("id", "hello")
          .Set("result", base::DictValue()
                             .Set("connection_id", "connection-1")
                             .Set("status", "pending_approval")));
  final_batch.push_back(
      base::DictValue()
          .Set("version", 1)
          .Set("id", *call->FindString("id"))
          .Set("result", base::DictValue()
                             .Set("ok", true)
                             .Set("data", base::DictValue().Set(
                                              "url", "https://dao.example"))));
  ASSERT_TRUE(browser.SendBatchAndClose(std::move(final_batch)));

  std::optional<base::DictValue> final_response = helper.ReadResponse();
  ASSERT_TRUE(final_response);
  EXPECT_EQ("final-batch", *final_response->FindString("id"));
  EXPECT_EQ("https://dao.example", *final_response->FindDict("result")
                                        ->FindDict("structuredContent")
                                        ->FindString("url"));
  EXPECT_FALSE(helper.HasResponse());

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);

  base::ScopedTempDir wrong_version_data_dir;
  ASSERT_TRUE(wrong_version_data_dir.CreateUniqueTempDir());
  FakeBrowserServer wrong_version_browser(wrong_version_data_dir.GetPath());
  ASSERT_TRUE(wrong_version_browser.Start());
  HelperProcess wrong_version_helper;
  ASSERT_TRUE(wrong_version_helper.Start(wrong_version_data_dir.GetPath()));
  ASSERT_TRUE(wrong_version_helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(wrong_version_helper.ReadResponse());
  ASSERT_TRUE(wrong_version_helper.Send(InitializedNotification()));
  ASSERT_TRUE(wrong_version_helper.Send(base::DictValue()
                                            .Set("jsonrpc", "2.0")
                                            .Set("id", "wrong-version")
                                            .Set("method", "tools/list")
                                            .Set("params", base::DictValue())));
  ASSERT_TRUE(wrong_version_browser.Accept());
  ASSERT_TRUE(wrong_version_browser.ReadRequest());
  std::optional<base::DictValue> list = wrong_version_browser.ReadRequest();
  ASSERT_TRUE(list);
  ASSERT_TRUE(wrong_version_browser.Send(
      base::DictValue()
          .Set("version", 2)
          .Set("id", *list->FindString("id"))
          .Set("result", base::DictValue().Set("tools", base::ListValue()))));
  std::optional<base::DictValue> version_error =
      wrong_version_helper.ReadResponse();
  ASSERT_TRUE(version_error);
  EXPECT_EQ("wrong-version", *version_error->FindString("id"));
  EXPECT_EQ(-32000, version_error->FindDict("error")->FindInt("code"));

  wrong_version_helper.CloseInput();
  EXPECT_TRUE(wrong_version_helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);

  base::ScopedTempDir numeric_id_data_dir;
  ASSERT_TRUE(numeric_id_data_dir.CreateUniqueTempDir());
  FakeBrowserServer numeric_id_browser(numeric_id_data_dir.GetPath());
  ASSERT_TRUE(numeric_id_browser.Start());
  HelperProcess numeric_id_helper;
  ASSERT_TRUE(numeric_id_helper.Start(numeric_id_data_dir.GetPath()));
  ASSERT_TRUE(numeric_id_helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(numeric_id_helper.ReadResponse());
  ASSERT_TRUE(numeric_id_helper.Send(InitializedNotification()));
  ASSERT_TRUE(numeric_id_helper.Send(base::DictValue()
                                         .Set("jsonrpc", "2.0")
                                         .Set("id", "numeric-browser-id")
                                         .Set("method", "tools/list")
                                         .Set("params", base::DictValue())));
  ASSERT_TRUE(numeric_id_browser.Accept());
  ASSERT_TRUE(numeric_id_browser.ReadRequest());
  ASSERT_TRUE(numeric_id_browser.ReadRequest());
  ASSERT_TRUE(numeric_id_browser.Send(
      base::DictValue()
          .Set("version", 1)
          .Set("id", 7)
          .Set("result", base::DictValue().Set("tools", base::ListValue()))));
  std::optional<base::DictValue> id_error = numeric_id_helper.ReadResponse();
  ASSERT_TRUE(id_error);
  EXPECT_EQ("numeric-browser-id", *id_error->FindString("id"));
  EXPECT_EQ(-32000, id_error->FindDict("error")->FindInt("code"));

  numeric_id_helper.CloseInput();
  EXPECT_TRUE(numeric_id_helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest, RejectsSymlinkedBrowserSocketBeforeConnecting) {
  FakeBrowserServer browser(user_data_dir_.GetPath());
  ASSERT_TRUE(browser.Start());
  ASSERT_TRUE(browser.ReplaceSocketWithSymlink());
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));
  ASSERT_TRUE(helper.Send(base::DictValue()
                              .Set("jsonrpc", "2.0")
                              .Set("id", "symlink")
                              .Set("method", "tools/list")
                              .Set("params", base::DictValue())));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  EXPECT_EQ("symlink", *response->FindString("id"));
  EXPECT_EQ(-32000, response->FindDict("error")->FindInt("code"));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest,
       RejectsInsecureBrowserSocketPermissionsBeforeConnecting) {
  FakeBrowserServer browser(user_data_dir_.GetPath());
  ASSERT_TRUE(browser.Start());
  ASSERT_TRUE(base::SetPosixFilePermissions(
      user_data_dir_.GetPath().AppendASCII("MCP").AppendASCII("mcp.sock"),
      0666));
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));
  ASSERT_TRUE(helper.Send(base::DictValue()
                              .Set("jsonrpc", "2.0")
                              .Set("id", "insecure-socket")
                              .Set("method", "tools/list")
                              .Set("params", base::DictValue())));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  EXPECT_EQ("insecure-socket", *response->FindString("id"));
  EXPECT_EQ(-32000, response->FindDict("error")->FindInt("code"));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest,
       OversizedAdaptedResponseReturnsOneBoundedError) {
  FakeBrowserServer browser(user_data_dir_.GetPath());
  ASSERT_TRUE(browser.Start());
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));
  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "oversized")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "execute_script")
                             .Set("arguments", base::DictValue()))));
  ASSERT_TRUE(browser.Accept());
  ASSERT_TRUE(browser.ReadRequest());
  std::optional<base::DictValue> call = browser.ReadRequest();
  ASSERT_TRUE(call);
  ASSERT_TRUE(
      browser.Send(base::DictValue()
                       .Set("version", 1)
                       .Set("id", "hello")
                       .Set("result", base::DictValue()
                                          .Set("connection_id", "connection-1")
                                          .Set("status", "pending_approval"))));
  ASSERT_TRUE(browser.Send(
      base::DictValue()
          .Set("version", 1)
          .Set("id", *call->FindString("id"))
          .Set("result", base::DictValue()
                             .Set("ok", true)
                             .Set("data", std::string(5 * 1024 * 1024, 'x')))));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  EXPECT_EQ("oversized", *response->FindString("id"));
  EXPECT_EQ(-32603, response->FindDict("error")->FindInt("code"));
  EXPECT_FALSE(helper.HasResponse());

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
  helper.DrainStdout();
  ASSERT_EQ(2u, helper.stdout_lines().size());
  for (const std::string& line : helper.stdout_lines()) {
    EXPECT_LE(line.size(), 8u * 1024u * 1024u);
    std::optional<base::Value> parsed =
        base::JSONReader::Read(line, base::JSON_PARSE_RFC);
    EXPECT_TRUE(parsed && parsed->is_dict());
  }
}

TEST_F(DaoMcpHelperBrowserTest,
       CancelQueueFailureReturnsErrorInsteadOfDroppingPendingCall) {
  FakeBrowserServer browser(user_data_dir_.GetPath());
  ASSERT_TRUE(browser.Start());
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));

  // Leave enough queue space for the hello and call, but not the following
  // cancellation notification.
  std::vector<base::DictValue> call_and_cancel;
  call_and_cancel.push_back(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "cancel-overflow")
          .Set("method", "tools/call")
          .Set("params",
               base::DictValue()
                   .Set("name", "execute_script")
                   .Set("arguments",
                        base::DictValue().Set(
                            "blob", std::string(8 * 1024 * 1024 - 320, 'x')))));
  call_and_cancel.push_back(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("method", "notifications/cancelled")
          .Set("params",
               base::DictValue().Set("requestId", "cancel-overflow")));
  ASSERT_TRUE(helper.SendBatch(std::move(call_and_cancel)));

  std::optional<base::DictValue> response = helper.ReadResponse();
  ASSERT_TRUE(response);
  EXPECT_EQ("cancel-overflow", *response->FindString("id"));
  const base::DictValue* result = response->FindDict("result");
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->FindBool("isError").value_or(false));
  EXPECT_EQ("MCP_DISABLED", *result->FindDict("structuredContent")
                                 ->FindDict("error")
                                 ->FindString("code"));

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(DaoMcpHelperBrowserTest,
       AdaptsScalarImageAndErrorResultsAndSwallowsCancelledResponse) {
  FakeBrowserServer browser(user_data_dir_.GetPath());
  ASSERT_TRUE(browser.Start());
  HelperProcess helper;
  ASSERT_TRUE(helper.Start(user_data_dir_.GetPath()));
  ASSERT_TRUE(helper.Send(InitializeRequest(base::Value(1))));
  ASSERT_TRUE(helper.ReadResponse());
  ASSERT_TRUE(helper.Send(InitializedNotification()));

  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", 10)
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "execute_script")
                             .Set("arguments", base::DictValue()))));
  ASSERT_TRUE(browser.Accept());
  ASSERT_TRUE(browser.ReadRequest());
  std::optional<base::DictValue> scalar_call = browser.ReadRequest();
  ASSERT_TRUE(scalar_call);
  ASSERT_TRUE(
      browser.Send(base::DictValue()
                       .Set("version", 1)
                       .Set("id", "hello")
                       .Set("result", base::DictValue()
                                          .Set("connection_id", "connection-1")
                                          .Set("status", "pending_approval"))));
  ASSERT_TRUE(browser.Send(
      base::DictValue()
          .Set("version", 1)
          .Set("id", *scalar_call->FindString("id"))
          .Set("result", base::DictValue().Set("ok", true).Set("data", 42))));
  std::optional<base::DictValue> scalar = helper.ReadResponse();
  ASSERT_TRUE(scalar);
  EXPECT_EQ(42, scalar->FindDict("result")
                    ->FindDict("structuredContent")
                    ->FindInt("result"));

  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "image")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "take_screenshot")
                             .Set("arguments", base::DictValue()))));
  std::optional<base::DictValue> image_call = browser.ReadRequest();
  ASSERT_TRUE(image_call);
  ASSERT_TRUE(browser.Send(
      base::DictValue()
          .Set("version", 1)
          .Set("id", *image_call->FindString("id"))
          .Set("result", base::DictValue()
                             .Set("ok", true)
                             .Set("data", base::DictValue().Set("width", 10))
                             .Set("media", base::DictValue()
                                               .Set("mime_type", "image/jpeg")
                                               .Set("data", "ZmFrZQ==")))));
  std::optional<base::DictValue> image_response = helper.ReadResponse();
  ASSERT_TRUE(image_response);
  const base::ListValue* image_content =
      image_response->FindDict("result")->FindList("content");
  ASSERT_TRUE(image_content);
  ASSERT_EQ(1u, image_content->size());
  EXPECT_EQ("image/jpeg",
            *(*image_content)[0].GetDict().FindString("mimeType"));

  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "error")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "click")
                             .Set("arguments", base::DictValue()))));
  std::optional<base::DictValue> error_call = browser.ReadRequest();
  ASSERT_TRUE(error_call);
  ASSERT_TRUE(
      browser.Send(base::DictValue()
                       .Set("version", 1)
                       .Set("id", *error_call->FindString("id"))
                       .Set("error", base::DictValue()
                                         .Set("code", "INVALID_ARGUMENT")
                                         .Set("message", "No target")
                                         .Set("retryable", false))));
  std::optional<base::DictValue> error_response = helper.ReadResponse();
  ASSERT_TRUE(error_response);
  EXPECT_TRUE(
      error_response->FindDict("result")->FindBool("isError").value_or(false));

  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("id", "cancel-me")
          .Set("method", "tools/call")
          .Set("params", base::DictValue()
                             .Set("name", "open_tab")
                             .Set("arguments", base::DictValue()))));
  std::optional<base::DictValue> cancelled_call = browser.ReadRequest();
  ASSERT_TRUE(cancelled_call);
  const std::string cancelled_internal_id = *cancelled_call->FindString("id");
  ASSERT_TRUE(helper.Send(
      base::DictValue()
          .Set("jsonrpc", "2.0")
          .Set("method", "notifications/cancelled")
          .Set("params", base::DictValue().Set("requestId", "cancel-me"))));
  std::optional<base::DictValue> cancel = browser.ReadRequest();
  ASSERT_TRUE(cancel);
  EXPECT_EQ("tools/cancel", *cancel->FindString("method"));
  EXPECT_EQ(cancelled_internal_id,
            *cancel->FindDict("params")->FindString("request_id"));
  ASSERT_TRUE(browser.Send(base::DictValue()
                               .Set("version", 1)
                               .Set("id", cancelled_internal_id)
                               .Set("error", base::DictValue()
                                                 .Set("code", "TOOL_CANCELLED")
                                                 .Set("message", "Cancelled")
                                                 .Set("retryable", false))));
  EXPECT_FALSE(helper.HasResponse());

  helper.CloseInput();
  int exit_code = -1;
  EXPECT_TRUE(helper.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

}  // namespace
}  // namespace dao
