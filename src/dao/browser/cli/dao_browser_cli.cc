// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// dao-browser: the command line entry point local agents use to work in Dao.
// It ships with Dao, so the CLI, its guide and the browser always have
// the same version.

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/at_exit.h"
#include "base/base64.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/common/chrome_paths.h"
#include "dao/browser/cli/dao_browser_guide.h"
#include "dao/browser/cli/dao_lite_guide.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"
#include "dao/browser/mcp/helper/dao_mcp_browser_client.h"
#include "dao/browser/mcp/helper/dao_mcp_stdio_server.h"

#if BUILDFLAG(IS_WIN)
#include <fcntl.h>
#include <io.h>
#endif

#ifndef DAO_MCP_VERSION
#define DAO_MCP_VERSION "0.0.0.0"
#endif

namespace dao {
namespace {

constexpr char kDefaultClientName[] = "dao-browser";
constexpr char kDefaultReason[] =
    "Run browser automation in a background task window.";
constexpr base::TimeDelta kConnectTimeout = base::Seconds(5);
constexpr base::TimeDelta kLaunchWait = base::Seconds(20);
constexpr int kDefaultScriptSeconds = 120;
constexpr int kMaxScriptSeconds = 600;

enum ExitCode {
  kExitOk = 0,
  kExitFailed = 1,
  kExitUserInControl = 2,
  kExitUnavailable = 3,
  kExitUsage = 64,
};

constexpr char kUsage[] =
    "Usage: dao-browser <command> [options]\n"
    "\n"
    "Commands:\n"
    "  skill [--lite]        Print the existing-page or Dao Lite guide.\n"
    "  run [FILE|-]          Run a JavaScript snippet (stdin when FILE is -\n"
    "                        or omitted).\n"
    "      --space ID        Preset `space` to an existing Agent Space.\n"
    "      --timeout SEC     Script time limit, default 120, max 600.\n"
    "  call TOOL [JSON]      Call one browser tool with JSON arguments.\n"
    "  status                Check whether Dao is reachable.\n"
    "  mcp                   Run the MCP stdio server.\n"
    "\n"
    "Options:\n"
    "  --client NAME         Name shown to the user (or DAO_BROWSER_CLIENT).\n"
    "  --reason TEXT         Why you need the browser; shown on first use.\n"
    "  --user-data-dir PATH  Dao profile directory.\n"
    "  --no-launch           Do not start Dao when it is not running.\n"
    "  --version             Print the version.\n";

void PrintError(const std::string& message) {
  std::cerr << "dao-browser: " << message << std::endl;
}

std::string PrettyJson(const base::Value& value) {
  return base::WriteJsonWithOptions(value,
                                    base::JSONWriter::OPTIONS_PRETTY_PRINT)
      .value_or("null");
}

std::optional<base::FilePath> FindBrowser() {
  base::FilePath exe;
  if (!base::PathService::Get(base::FILE_EXE, &exe)) {
    return std::nullopt;
  }
  base::FilePath resolved = base::MakeAbsoluteFilePath(exe);
  if (resolved.empty()) {
    resolved = exe;
  }
#if BUILDFLAG(IS_WIN)
  base::FilePath app = resolved.DirName().AppendASCII("dao.exe");
  if (!base::PathExists(app)) {
    app = resolved.DirName().DirName().AppendASCII("dao.exe");
  }
  if (!base::PathExists(app)) {
#else
  // Resolve the ~/.local/bin link back to Dao.app/Contents/Helpers.
  base::FilePath app = resolved.DirName().DirName().DirName();
  if (app.Extension() != FILE_PATH_LITERAL(".app")) {
#endif
    return std::nullopt;
  }
  return app;
}

// A small synchronous wrapper over the browser socket client.
class SyncBrowser {
 public:
  explicit SyncBrowser(base::FilePath user_data_dir)
      : client_(std::move(user_data_dir)) {}

  bool Connect(const std::string& client_name, std::string* error) {
    if (!client_.Connect(client_name, DAO_MCP_VERSION)) {
      *error = client_.last_error();
      return false;
    }
    std::optional<base::DictValue> hello =
        Await("hello", kConnectTimeout, error);
    if (!hello) {
      return false;
    }
    if (const base::DictValue* failure = hello->FindDict("error")) {
      const std::string* message = failure->FindString("message");
      *error = message ? *message : "the browser rejected the connection";
      return false;
    }
    update_ready_ =
        hello->FindBoolByDottedPath("result.update_ready").value_or(false);
    return true;
  }

  // A downloaded Dao update waits for a restart.
  bool update_ready() const { return update_ready_; }

  std::optional<base::DictValue> Request(const std::string& method,
                                         base::DictValue params,
                                         base::TimeDelta timeout,
                                         std::string* error) {
    const std::string id = base::NumberToString(next_id_++);
    if (!client_.QueueRequest(base::DictValue()
                                  .Set("version", kDaoMcpIpcVersion)
                                  .Set("id", id)
                                  .Set("method", method)
                                  .Set("params", std::move(params)))) {
      *error = client_.last_error();
      return std::nullopt;
    }
    return Await(id, timeout, error);
  }

 private:
  std::optional<base::DictValue> Await(const std::string& id,
                                       base::TimeDelta timeout,
                                       std::string* error) {
    const base::TimeTicks deadline = base::TimeTicks::Now() + timeout;
    while (true) {
      if (!client_.FlushWrites()) {
        *error = client_.last_error();
        return std::nullopt;
      }
      std::vector<base::DictValue> responses;
      if (!client_.ReadResponses(&responses)) {
        *error = client_.last_error();
        return std::nullopt;
      }
      for (base::DictValue& response : responses) {
        const std::string* response_id = response.FindString("id");
        if (response_id && *response_id == id) {
          return std::move(response);
        }
      }
      const base::TimeDelta remaining = deadline - base::TimeTicks::Now();
      if (!remaining.is_positive()) {
        *error = "timed out waiting for Dao";
        return std::nullopt;
      }
      const int wait_ms = static_cast<int>(
          std::min<int64_t>(remaining.InMilliseconds() + 1, 1000));
      if (!client_.WaitForIO(true, client_.wants_write(), wait_ms)) {
        *error = client_.last_error();
        return std::nullopt;
      }
    }
  }

  DaoMcpBrowserClient client_;
  int next_id_ = 1;
  bool update_ready_ = false;
};

struct Options {
  std::string client_name = kDefaultClientName;
  std::string reason = kDefaultReason;
  base::FilePath user_data_dir;
  bool launch = true;
};

// Connects to Dao, starting it in the background when it is not running.
std::unique_ptr<SyncBrowser> ConnectToDao(const Options& options) {
  std::string error;
  auto browser = std::make_unique<SyncBrowser>(options.user_data_dir);
  if (browser->Connect(options.client_name, &error)) {
    return browser;
  }
  std::optional<base::FilePath> app = FindBrowser();
  if (options.launch && app) {
#if BUILDFLAG(IS_WIN)
    base::CommandLine launch_command(*app);
    launch_command.AppendSwitchPath("user-data-dir", options.user_data_dir);
#else
    base::CommandLine launch_command(base::FilePath("/usr/bin/open"));
    launch_command.AppendArg("-g");
    launch_command.AppendArgPath(*app);
#endif
    base::LaunchProcess(launch_command, base::LaunchOptions());
    const base::TimeTicks deadline = base::TimeTicks::Now() + kLaunchWait;
    while (base::TimeTicks::Now() < deadline) {
      base::PlatformThread::Sleep(base::Milliseconds(500));
      browser = std::make_unique<SyncBrowser>(options.user_data_dir);
      if (browser->Connect(options.client_name, &error)) {
        return browser;
      }
    }
  }
  PrintError(
      "Dao is not reachable (" + error +
      "). Open Dao and turn on Settings > You and Dao > MCP server, which "
      "also lets local agents use dao-browser, then retry.");
  return nullptr;
}

bool IsHardStop(const std::string* code) {
  return code && (*code == "SPACE_USER_IN_CONTROL" || *code == "SPACE_CLOSED");
}

// Prints a tool error. Hard stops print only fixed guidance, so the agent
// never continues from partial output.
int ReportToolError(const base::DictValue& error) {
  const std::string* code = error.FindString("code");
  const std::string* message = error.FindString("message");
  if (IsHardStop(code)) {
    std::cout << "[dao-browser:stop] " << (message ? *message : "") << "\n"
              << "Do not retry and do not try to take control back. Tell the "
                 "user what you were doing and wait for them."
              << std::endl;
    return kExitUserInControl;
  }
  std::cerr << "Error [" << (code ? *code : "UNKNOWN")
            << "]: " << (message ? *message : "browser tool failed")
            << std::endl;
  return kExitFailed;
}

std::optional<base::DictValue> CallTool(SyncBrowser& browser,
                                        const Options& options,
                                        const std::string& tool,
                                        base::DictValue arguments,
                                        base::TimeDelta timeout,
                                        int* exit_code) {
  arguments.Set("reason", options.reason);
  std::string error;
  std::optional<base::DictValue> response =
      browser.Request("tools/call",
                      base::DictValue()
                          .Set("name", tool)
                          .Set("arguments", std::move(arguments)),
                      timeout, &error);
  if (!response) {
    PrintError(error);
    *exit_code = kExitUnavailable;
    return std::nullopt;
  }
  if (const base::DictValue* failure = response->FindDict("error")) {
    *exit_code = ReportToolError(*failure);
    return std::nullopt;
  }
  base::DictValue* result = response->FindDict("result");
  if (!result) {
    PrintError("Dao returned an unexpected response.");
    *exit_code = kExitFailed;
    return std::nullopt;
  }
  if (const base::DictValue* failure = result->FindDict("error")) {
    *exit_code = ReportToolError(*failure);
    return std::nullopt;
  }
  *exit_code = kExitOk;
  return std::move(*result);
}

// Remembers which guide the agent read, so later runs can tell it to read
// the guide again after Dao updates.
base::FilePath GuideVersionFile(const Options& options) {
  return options.user_data_dir.AppendASCII("dao_browser_guide_version");
}

int RunSkill(const Options& options, bool lite) {
  std::cout << "<!-- dao-browser guide " << DAO_MCP_VERSION << " -->\n"
            << (lite ? kDaoLiteGuide : kDaoBrowserGuide)
            << "\n<!-- end of dao-browser guide "
            << DAO_MCP_VERSION << " -->" << std::endl;
  if (!options.user_data_dir.empty() &&
      base::DirectoryExists(options.user_data_dir)) {
    base::WriteFile(GuideVersionFile(options), DAO_MCP_VERSION);
  }
  return kExitOk;
}

// Printed to stderr after a successful run or call, never after a hard
// stop, so stdout stays the result alone.
void PrintNotices(const Options& options, const SyncBrowser& browser) {
  std::string read_version;
  if (!base::ReadFileToString(GuideVersionFile(options), &read_version) ||
      base::TrimWhitespaceASCII(read_version, base::TRIM_ALL) !=
          DAO_MCP_VERSION) {
    std::cerr << "[dao-browser:notice] This Dao version's guide has not been "
                 "read yet. Run `dao-browser skill --lite` for background tasks, or "
                 "`dao-browser skill` for existing pages."
              << std::endl;
  }
  if (browser.update_ready()) {
    std::cerr << "[dao-browser:notice] A Dao update is ready. When the task "
                 "is done, tell the user they can restart Dao to finish "
                 "updating, then read the relevant `dao-browser skill` guide again."
              << std::endl;
  }
}

int RunStatus(const Options& options) {
  Options no_launch = options;
  no_launch.launch = false;
  std::unique_ptr<SyncBrowser> browser = ConnectToDao(no_launch);
  if (!browser) {
    return kExitUnavailable;
  }
  std::cout << "Dao " << DAO_MCP_VERSION << " is running and reachable."
            << std::endl;
  return kExitOk;
}

// Writes the files a script returned with dao.saveFile(). The browser only
// sends bytes; this process, which runs as the agent, decides where they go.
void SaveScriptFiles(const base::ListValue& files) {
  for (const base::Value& value : files) {
    const base::DictValue* file = value.GetIfDict();
    const std::string* path = file ? file->FindString("path") : nullptr;
    const std::string* data = file ? file->FindString("data") : nullptr;
    if (!path || !data) {
      continue;
    }
    std::string bytes = *data;
    if (file->FindBool("base64").value_or(false) &&
        !base::Base64Decode(*data, &bytes)) {
      PrintError("could not decode " + *path + " as base64.");
      continue;
    }
    const base::FilePath target = base::FilePath::FromUTF8Unsafe(*path);
    if (!base::CreateDirectory(target.DirName()) ||
        !base::WriteFile(target, bytes)) {
      PrintError("could not save " + *path);
      continue;
    }
    std::cout << "[dao-browser] saved " << *path << " (" << bytes.size()
              << " bytes)\n";
  }
}

int RunScript(const Options& options,
              const std::vector<std::string>& args,
              const base::CommandLine& command_line) {
  std::string code;
  const bool from_stdin = args.size() < 2 || args[1] == "-";
  if (from_stdin) {
    code.assign(std::istreambuf_iterator<char>(std::cin),
                std::istreambuf_iterator<char>());
  } else if (!base::ReadFileToString(base::FilePath::FromUTF8Unsafe(args[1]),
                                    &code)) {
    PrintError("could not read " + args[1]);
    return kExitUsage;
  }
  if (base::TrimWhitespaceASCII(code, base::TRIM_ALL).empty()) {
    PrintError("the script is empty.");
    return kExitUsage;
  }
  int seconds = kDefaultScriptSeconds;
  if (command_line.HasSwitch("timeout") &&
      (!base::StringToInt(command_line.GetSwitchValueASCII("timeout"),
                          &seconds) ||
       seconds < 1 || seconds > kMaxScriptSeconds)) {
    PrintError("--timeout must be between 1 and 600 seconds.");
    return kExitUsage;
  }
  base::DictValue arguments;
  arguments.Set("code", std::move(code));
  arguments.Set("timeout_ms", seconds * 1000);
  if (command_line.HasSwitch("space")) {
    arguments.Set("space_id", command_line.GetSwitchValueASCII("space"));
  }

  std::unique_ptr<SyncBrowser> browser = ConnectToDao(options);
  if (!browser) {
    return kExitUnavailable;
  }
  int exit_code = kExitOk;
  std::optional<base::DictValue> result =
      CallTool(*browser, options, "run_browser_script", std::move(arguments),
               base::Seconds(seconds + 30), &exit_code);
  if (!result) {
    return exit_code;
  }
  const base::DictValue* data = result->FindDict("data");
  if (!data) {
    std::cout << PrettyJson(base::Value(std::move(*result))) << std::endl;
    return kExitOk;
  }
  if (const base::ListValue* logs = data->FindList("logs")) {
    for (const base::Value& line : *logs) {
      if (line.is_string()) {
        std::cout << line.GetString() << "\n";
      }
    }
  }
  if (const base::ListValue* files = data->FindList("files")) {
    SaveScriptFiles(*files);
  }
  if (data->FindBool("success").value_or(false)) {
    const base::Value* value = data->Find("result");
    if (value && !value->is_none()) {
      std::cout << PrettyJson(*value) << "\n";
    }
    PrintNotices(options, *browser);
    std::cout.flush();
    return kExitOk;
  }
  const base::DictValue* error = data->FindDict("error");
  const std::string* name = error ? error->FindString("name") : nullptr;
  const std::string* message = error ? error->FindString("message") : nullptr;
  const std::string* code_name = error ? error->FindString("code") : nullptr;
  const std::string* stack = error ? error->FindString("stack") : nullptr;
  std::cerr << (name ? *name : "Error") << ": "
            << (message ? *message : "the script failed");
  if (code_name && !code_name->empty()) {
    std::cerr << " [" << *code_name << "]";
  }
  std::cerr << "\n";
  if (stack && !stack->empty()) {
    std::cerr << *stack << "\n";
  }
  return kExitFailed;
}

int RunCall(const Options& options,
            const std::vector<std::string>& args) {
  if (args.size() < 2) {
    PrintError("call needs a tool name, e.g. dao-browser call list_spaces");
    return kExitUsage;
  }
  base::DictValue arguments;
  if (args.size() > 2) {
    std::optional<base::Value> parsed =
        base::JSONReader::Read(args[2], base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      PrintError("arguments must be a JSON object.");
      return kExitUsage;
    }
    arguments = std::move(*parsed).TakeDict();
  }
  std::unique_ptr<SyncBrowser> browser = ConnectToDao(options);
  if (!browser) {
    return kExitUnavailable;
  }
  int exit_code = kExitOk;
  std::optional<base::DictValue> result =
      CallTool(*browser, options, args[1], std::move(arguments),
               base::Minutes(16), &exit_code);
  if (result) {
    std::cout << PrettyJson(base::Value(std::move(*result))) << std::endl;
    PrintNotices(options, *browser);
  }
  return exit_code;
}

}  // namespace
}  // namespace dao

namespace dao {
namespace {

// Options that take a value. Agents write both `--space ID` and
// `--space=ID`; base::CommandLine only understands the second form.
constexpr std::basic_string_view<base::CommandLine::CharType> kValueSwitches[] = {
    FILE_PATH_LITERAL("--space"), FILE_PATH_LITERAL("--timeout"),
    FILE_PATH_LITERAL("--client"), FILE_PATH_LITERAL("--reason"),
    FILE_PATH_LITERAL("--user-data-dir")};

base::CommandLine::StringVector NormalizeArguments(
    const base::CommandLine::StringVector& argv) {
  base::CommandLine::StringVector args;
  for (auto it = argv.begin(); it != argv.end(); ++it) {
    if (*it == FILE_PATH_LITERAL("--")) {
      args.insert(args.end(), it, argv.end());
      break;
    }
    if (std::next(it) != argv.end() &&
        std::ranges::contains(kValueSwitches, *it)) {
      args.push_back(*it + FILE_PATH_LITERAL("=") + *std::next(it));
      ++it;
    } else {
      args.push_back(*it);
    }
  }
  return args;
}

std::string ArgumentToUtf8(const base::CommandLine::StringType& argument) {
#if BUILDFLAG(IS_WIN)
  return base::WideToUTF8(argument);
#else
  return argument;
#endif
}

}  // namespace
}  // namespace dao

#if BUILDFLAG(IS_WIN)
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
  base::AtExitManager at_exit_manager;
  // SAFETY: main() receives `argc` valid entries in `argv`.
  const base::span<base::CommandLine::CharType*> raw_argv = UNSAFE_BUFFERS(
      base::span<base::CommandLine::CharType*>(argv, static_cast<size_t>(argc)));
#if BUILDFLAG(IS_WIN)
  base::CommandLine::Init(0, nullptr);
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#else
  base::CommandLine::Init(argc, argv);
#endif
  base::CommandLine::ForCurrentProcess()->InitFromArgv(dao::NormalizeArguments(
      base::CommandLine::StringVector(raw_argv.begin(), raw_argv.end())));
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  std::vector<std::string> args;
  for (const auto& argument : command_line.GetArgs()) {
    args.push_back(dao::ArgumentToUtf8(argument));
  }

  if (command_line.HasSwitch("version")) {
    std::cout << DAO_MCP_VERSION << std::endl;
    return dao::kExitOk;
  }
  if (args.empty() || command_line.HasSwitch("help")) {
    std::cout << dao::kUsage;
    return command_line.HasSwitch("help") ? dao::kExitOk : dao::kExitUsage;
  }
  const std::string& command = args[0];
  dao::Options options;
  if (command_line.HasSwitch("user-data-dir")) {
    options.user_data_dir = command_line.GetSwitchValuePath("user-data-dir");
  } else {
    chrome::RegisterPathProvider();
    base::PathService::Get(chrome::DIR_USER_DATA, &options.user_data_dir);
  }
  if (command == "skill") {
    return dao::RunSkill(options, command_line.HasSwitch("lite"));
  }
  if (options.user_data_dir.empty()) {
    dao::PrintError("could not find the Dao profile directory.");
    return dao::kExitUnavailable;
  }
  std::unique_ptr<base::Environment> env = base::Environment::Create();
  if (command_line.HasSwitch("client")) {
    // Names and reasons are shown to the user, often in their language.
    options.client_name =
        dao::ArgumentToUtf8(command_line.GetSwitchValueNative("client"));
  } else if (std::optional<std::string> client =
                 env->GetVar("DAO_BROWSER_CLIENT");
             client && !client->empty()) {
    options.client_name = *client;
  }
  if (command_line.HasSwitch("reason")) {
    options.reason =
        dao::ArgumentToUtf8(command_line.GetSwitchValueNative("reason"));
  }
  options.launch = !command_line.HasSwitch("no-launch");

  if (command == "mcp") {
    dao::DaoMcpStdioServer server(options.user_data_dir, DAO_MCP_VERSION);
    return server.Run();
  }
  if (command == "status") {
    return dao::RunStatus(options);
  }
  if (command == "run") {
    return dao::RunScript(options, args, command_line);
  }
  if (command == "call") {
    return dao::RunCall(options, args);
  }
  std::cerr << dao::kUsage;
  return dao::kExitUsage;
}
