// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/cli/dao_home_cli.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <set>
#include <utility>

#include "base/base64.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/process/launch.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "dao/browser/cli/dao_home_guide.h"
#include "dao/browser/cli/dao_home_help.h"
#include "dao/browser/cli/dao_home_runtime_guide.h"
#include "dao/browser/cli/dao_home_sources_guide.h"
#include "dao/browser/cli/dao_home_workspace.h"
#include "dao/browser/local_ipc/dao_local_ipc_client.h"
#include "dao/browser/local_ipc/dao_local_ipc_config.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#elif BUILDFLAG(IS_MAC)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace dao {
namespace {

std::string String(const base::DictValue& value, std::string_view key) {
  const auto* text = value.FindString(key);
  return text ? *text : std::string();
}

base::DictValue Failure(std::string_view code) {
  return base::DictValue()
      .Set("ok", false)
      .Set("error", base::DictValue().Set("code", code));
}

bool Ok(const base::DictValue& value) {
  return value.FindBool("ok").value_or(false);
}
std::string ErrorCode(const base::DictValue& value) {
  const auto* code = value.FindStringByDottedPath("error.code");
  return code ? *code : "PROTOCOL_MISMATCH";
}

void Redact(base::DictValue& value) {
  value.Remove("token");
  value.Remove("nonce");
  for (auto [key, child] : value) {
    if (auto* dict = child.GetIfDict())
      Redact(*dict);
    if (auto* list = child.GetIfList()) {
      for (auto& item : *list)
        if (auto* dict = item.GetIfDict())
          Redact(*dict);
    }
  }
}

int Emit(base::DictValue result, const std::string& command) {
  Redact(result);
  result.Set("command", "home." + command);
  std::cout << base::WriteJson(result).value_or(
                   "{\"ok\":false,\"error\":{\"code\":\"LOCAL_IO_ERROR\"}}")
            << '\n';
  if (Ok(result))
    return 0;
  const auto code = ErrorCode(result);
  if (code == "USAGE")
    return 64;
  if (code == "BROWSER_UNAVAILABLE" || code == "PROTOCOL_MISMATCH")
    return 3;
  if (code == "SESSION_REVOKED" || code == "SESSION_EXPIRED" ||
      code == "AUTHORIZATION_DENIED" || code == "CANCELLED")
    return 2;
  return 1;
}

bool ChineseLocale() {
  auto env = base::Environment::Create();
  constexpr base::cstring_view keys[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
  for (const auto key : keys) {
    const auto value = env->GetVar(key);
    if (value && !value->empty())
      return value->starts_with("zh");
  }
#if BUILDFLAG(IS_WIN)
  return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
#elif BUILDFLAG(IS_MAC)
  CFArrayRef languages = CFLocaleCopyPreferredLanguages();
  bool chinese = false;
  if (languages && CFArrayGetCount(languages)) {
    const auto language =
        static_cast<CFStringRef>(CFArrayGetValueAtIndex(languages, 0));
    chinese = CFStringHasPrefix(language, CFSTR("zh"));
  }
  if (languages)
    CFRelease(languages);
  return chinese;
#else
  return false;
#endif
}

std::string Switch(const base::CommandLine& command, const char* key) {
#if BUILDFLAG(IS_WIN)
  return base::WideToUTF8(command.GetSwitchValueNative(key));
#else
  return command.GetSwitchValueNative(key);
#endif
}

class HomeConnection {
 public:
  HomeConnection(base::FilePath directory,
                 std::string client,
                 std::string version)
      : directory_(std::move(directory)),
        client_name_(std::move(client)),
        version_(std::move(version)) {}

  const std::string& client_name() const { return client_name_; }
  const std::string& error_code() const { return error_code_; }

  bool Connect(bool launch, const std::optional<base::FilePath>& browser_path) {
    if (TryConnect())
      return true;
    if (error_code_ == "PROTOCOL_MISMATCH" || !launch || !browser_path)
      return false;
#if BUILDFLAG(IS_WIN)
    base::CommandLine command(*browser_path);
    command.AppendSwitchPath("user-data-dir", directory_);
#else
    base::CommandLine command(base::FilePath("/usr/bin/open"));
    command.AppendArg("-g");
    command.AppendArgPath(*browser_path);
    command.AppendArg("--args");
    command.AppendSwitchPath("user-data-dir", directory_);
#endif
    base::LaunchProcess(command, base::LaunchOptions());
    const auto deadline = base::TimeTicks::Now() + base::Seconds(20);
    while (base::TimeTicks::Now() < deadline) {
      base::PlatformThread::Sleep(base::Milliseconds(500));
      if (TryConnect())
        return true;
    }
    return false;
  }

  void CloseUnretainedSession(const std::string& token) {
    if (token.empty())
      return;
    auto result = Call("session.close", base::DictValue().Set("token", token),
                       base::Seconds(5));
    if (!Ok(result) && ErrorCode(result) == "BROWSER_UNAVAILABLE" &&
        TryConnect())
      Call("session.close", base::DictValue().Set("token", token),
           base::Seconds(5));
  }

  base::DictValue Call(const std::string& method,
                       base::DictValue params,
                       base::TimeDelta timeout = base::Seconds(330)) {
    const auto id = base::NumberToString(next_id_++);
    if (!client_ ||
        !client_->QueueRequest(base::DictValue()
                                   .Set("version", 1)
                                   .Set("id", id)
                                   .Set("method", method)
                                   .Set("params", std::move(params))))
      return Failure("BROWSER_UNAVAILABLE");
    return Await(id, timeout);
  }

 private:
  bool TryConnect() {
    client_ = std::make_unique<DaoLocalIpcClient>(directory_,
                                                  DaoLocalIpcConfig::Home());
    error_code_ = "BROWSER_UNAVAILABLE";
    if (!client_->Connect(client_name_, version_))
      return false;
    auto hello = Await("hello", base::Seconds(5));
    if (!Ok(hello))
      error_code_ = ErrorCode(hello);
    return Ok(hello);
  }
  base::DictValue Await(const std::string& id, base::TimeDelta timeout) {
    const auto deadline = base::TimeTicks::Now() + timeout;
    while (base::TimeTicks::Now() < deadline) {
      std::vector<base::DictValue> responses;
      if (!client_->FlushWrites() || !client_->ReadResponses(&responses))
        return Failure("BROWSER_UNAVAILABLE");
      for (auto& response : responses) {
        if (String(response, "id") != id)
          continue;
        if (response.FindInt("version") != 1)
          return Failure("PROTOCOL_MISMATCH");
        if (auto* result = response.FindDict("result"))
          return std::move(*result);
        if (auto* error = response.FindDict("error"))
          return base::DictValue()
              .Set("ok", false)
              .Set("error", std::move(*error));
        return Failure("PROTOCOL_MISMATCH");
      }
      const auto left = deadline - base::TimeTicks::Now();
      if (!left.is_positive())
        break;
      if (!client_->WaitForIO(true, client_->wants_write(),
                              static_cast<int>(std::min<int64_t>(
                                  1000, left.InMilliseconds() + 1))))
        return Failure("BROWSER_UNAVAILABLE");
    }
    return Failure("BROWSER_UNAVAILABLE");
  }
  base::FilePath directory_;
  std::string client_name_;
  std::string version_;
  std::string error_code_ = "BROWSER_UNAVAILABLE";
  std::unique_ptr<DaoLocalIpcClient> client_;
  int next_id_ = 1;
};

base::DictValue SessionParams(DaoHomeWorkspace& workspace) {
  return base::DictValue().Set("token", String(workspace.state(), "token"));
}

base::DictValue WorkspaceResult(DaoHomeWorkspace& workspace) {
  return base::DictValue()
      .Set("ok", true)
      .Set("workspace_dir", workspace.path().AsUTF8Unsafe())
      .Set("project_dir",
           workspace.path().AppendASCII("project").AsUTF8Unsafe())
      .Set("base_revision", String(workspace.state(), "base_revision"))
      .Set("profile_id", String(workspace.state(), "profile_id"))
      .Set("expires_at_ms",
           workspace.state().FindDouble("expires_at_ms").value_or(0));
}

base::DictValue CleanWorkspace(DaoHomeWorkspace& workspace,
                               bool discard,
                               bool session_closed) {
  const auto path = workspace.path().AsUTF8Unsafe();
  auto cleaned = workspace.Clean(discard);
  return cleaned.has_value() ? base::DictValue()
                                   .Set("ok", true)
                                   .Set("workspace_dir", path)
                                   .Set("session_closed", session_closed)
                             : Failure(cleaned.error());
}

base::DictValue Screenshot(HomeConnection& connection,
                           DaoHomeWorkspace& workspace) {
  auto response =
      connection.Call("preview.screenshot", SessionParams(workspace));
  if (!Ok(response))
    return response;
  std::string bytes;
  if (!base::Base64Decode(String(response, "data"), &bytes) ||
      bytes.size() > 20 * 1024 * 1024)
    return Failure("PROTOCOL_MISMATCH");
  const auto mime = String(response, "mime_type");
  auto artifact = workspace.SaveArtifact(mime == "image/png"    ? "png"
                                         : mime == "image/jpeg" ? "jpg"
                                                                : "",
                                         bytes);
  if (!artifact.has_value())
    return Failure(artifact.error());
  return base::DictValue()
      .Set("ok", true)
      .Set("mime_type", mime)
      .Set("artifact", artifact->AsUTF8Unsafe());
}

// A pending request is never reissued with another ID. Only browser provenance
// can establish a commit after transport failure or a local metadata failure.
base::DictValue Recover(HomeConnection& connection,
                        DaoHomeWorkspace& workspace) {
  const auto* pending = workspace.state().FindDict("pending_operation");
  if (!pending)
    return base::DictValue().Set("ok", true);
  const auto operation_id = String(*pending, "operation_id");
  const auto kind = String(*pending, "kind");
  auto params = SessionParams(workspace);
  params.Set("operation_id", operation_id);
  auto response = connection.Call("session.status", std::move(params));
  if (!Ok(response))
    return response;
  const auto* operation = response.FindDict("operation");
  if (!operation || String(*operation, "status") != "committed" ||
      String(*operation, "operation_id") != operation_id ||
      String(*operation, "revision").empty())
    return Failure("OUTCOME_UNKNOWN")
        .Set("operation_id", operation_id)
        .Set("workspace_dir", workspace.path().AsUTF8Unsafe())
        .Set("fresh_pull_available", true);
  base::DictValue outcome = operation->Clone();
  outcome.Set("ok", true);
  outcome.Set("recovered", true);
  const auto recovered = workspace.RecoverOperation(*operation);
  if (!recovered.has_value()) {
    if (recovered.error() == "OUTCOME_UNKNOWN" ||
        recovered.error() == "WORKSPACE_INVALID")
      return Failure(recovered.error()).Set("operation_id", operation_id);
    outcome.Set("local_recovery_needed", true);
  }
  if (kind == "rollback")
    outcome.Set("fresh_pull_required", true);
  return outcome;
}

base::DictValue Diff(DaoHomeWorkspace& workspace) {
  auto files = workspace.Capture();
  auto before = workspace.ReadBase();
  if (!files.has_value())
    return Failure(files.error());
  if (!before.has_value())
    return Failure(before.error());
  base::ListValue added, changed, removed;
  for (const auto& [path, bytes] : *files) {
    const auto it = before->find(path);
    if (it == before->end())
      added.Append(path);
    else if (it->second != bytes)
      changed.Append(path);
  }
  for (const auto& [path, bytes] : *before)
    if (!files->contains(path))
      removed.Append(path);
  auto result = WorkspaceResult(workspace);
  result.Set("content_digest", HomeFileSnapshotDigest(*files));
  result.Set("added", std::move(added));
  result.Set("modified", std::move(changed));
  result.Set("removed", std::move(removed));
  return result;
}

bool ValidSession(const base::DictValue& session) {
  return !String(session, "token").empty() &&
         !String(session, "profile_id").empty() &&
         session.FindDouble("expires_at_ms").has_value();
}

base::DictValue Pull(HomeConnection& connection,
                     const base::CommandLine& command,
                     const base::FilePath& user_data_dir,
                     DaoHomeWorkspace* from) {
  base::DictValue session;
  base::ScopedClosureRunner new_session_cleanup;
  const auto reason = Switch(command, "reason");
  if (from) {
    auto status = connection.Call("session.status", SessionParams(*from));
    if (Ok(status))
      session = from->state().Clone();
    else if (ErrorCode(status) != "SESSION_EXPIRED")
      return status;
    else if (reason.empty())
      return status;
  }
  if (session.empty()) {
    if (reason.empty())
      return Failure("USAGE");
    base::DictValue params;
    params.Set("reason", reason);
    const auto profile =
        from ? String(from->state(), "profile_id") : Switch(command, "profile");
    if (!profile.empty())
      params.Set("profile", profile);
    session =
        connection.Call("session.open", std::move(params), base::Seconds(65));
    if (!Ok(session))
      return session;
    new_session_cleanup.ReplaceClosure(base::BindOnce(
        &HomeConnection::CloseUnretainedSession, base::Unretained(&connection),
        String(session, "token")));
  }
  if (!ValidSession(session))
    return Failure("PROTOCOL_MISMATCH");
  auto snapshot =
      connection.Call("project.snapshot",
                      base::DictValue().Set("token", String(session, "token")));
  if (!Ok(snapshot))
    return snapshot;
  const auto* encoded = snapshot.FindDict("files");
  if (!encoded)
    return Failure("PROTOCOL_MISMATCH");
  auto files = DecodeHomeFileSnapshot(*encoded);
  if (!files.has_value() ||
      HomeFileSnapshotDigest(*files) != String(snapshot, "digest"))
    return Failure("PROTOCOL_MISMATCH");
  const auto revision = String(snapshot, "revision");
  auto source = *files;
  if (source.empty() && revision.empty()) {
    source = {{"manifest.json",
               "{\"format_version\":1,\"entry\":\"index."
               "html\",\"routes\":[\"/\"],\"connectors\":[]}"},
              {"index.html",
               "<!doctype html><html><head><meta charset=\"utf-8\"><meta "
               "name=\"viewport\" content=\"width=device-width, "
               "initial-scale=1\"><link rel=\"stylesheet\" "
               "href=\"src/styles.css\"></head><body><main "
               "data-dao-node-id=\"main\"></main><script src=\"src/app.js\" "
               "defer></script></body></html>"},
              {"src/styles.css",
               "body { margin: 0; font-family: system-ui, sans-serif; }\n"},
              {"src/app.js", "'use strict';\n"}};
  }
  base::DictValue state;
  state.Set("token", String(session, "token"));
  state.Set("profile_id", String(session, "profile_id"));
  state.Set("expires_at_ms", *session.FindDouble("expires_at_ms"));
  state.Set("client_name", connection.client_name());
  state.Set("user_data_dir", user_data_dir.AsUTF8Unsafe());
  state.Set("base_revision", revision);
  auto workspace = DaoHomeWorkspace::Create(source, std::move(state));
  if (!workspace.has_value())
    return Failure(workspace.error());
  if (source != *files) {
    auto saved = (*workspace)->AdvanceBase(revision, *files);
    if (!saved.has_value())
      return Failure(saved.error())
          .Set("workspace_dir", (*workspace)->path().AsUTF8Unsafe());
  }
  new_session_cleanup.ReplaceClosure(base::OnceClosure());
  auto result = WorkspaceResult(**workspace);
  if (from && from->state().Find("pending_operation")) {
    result.Set("source_workspace_dir", from->path().AsUTF8Unsafe());
    result.Set("source_operation_unresolved", true);
  }
  return result;
}

base::DictValue Stage(HomeConnection& connection,
                      DaoHomeWorkspace& workspace,
                      const HomeFileSnapshot& files) {
  const auto digest = HomeFileSnapshotDigest(files);
  workspace.state().Remove("preview_digest");
  workspace.state().Remove("publishable");
  auto saved = workspace.SaveState();
  if (!saved.has_value())
    return Failure(saved.error());
  auto params = SessionParams(workspace);
  params.Set("base_revision", String(workspace.state(), "base_revision"));
  params.Set("files", EncodeHomeFileSnapshot(files));
  params.Set("digest", digest);
  auto staged = connection.Call("draft.stage", std::move(params));
  if (!Ok(staged))
    return staged;
  const auto draft = String(staged, "draft_id");
  if (draft.empty() || String(staged, "digest") != digest ||
      String(staged, "base_revision") !=
          String(workspace.state(), "base_revision"))
    return Failure("PROTOCOL_MISMATCH");
  workspace.state().Set("draft_id", draft);
  workspace.state().Set("candidate_digest", digest);
  saved = workspace.SaveSnapshot(files);
  if (saved.has_value())
    saved = workspace.SaveState();
  if (!saved.has_value())
    return Failure(saved.error());
  return staged;
}

base::DictValue Preview(HomeConnection& connection,
                        DaoHomeWorkspace& workspace,
                        bool screenshot) {
  if (workspace.state().Find("pending_operation"))
    return Recover(connection, workspace);
  auto files = workspace.Capture();
  if (!files.has_value())
    return Failure(files.error());
  const auto digest = HomeFileSnapshotDigest(*files);
  auto staged = Stage(connection, workspace, *files);
  if (!Ok(staged))
    return staged;
  const auto draft = String(staged, "draft_id");
  auto params = SessionParams(workspace);
  params.Set("draft_id", draft);
  auto result = connection.Call("draft.preview", std::move(params));
  if (!Ok(result))
    return result;
  auto current = workspace.Capture();
  if (!current.has_value() || HomeFileSnapshotDigest(*current) != digest)
    return Failure("FILES_CHANGED");
  workspace.state().Set("preview_digest", digest);
  workspace.state().Set("publishable",
                        result.FindBool("publishable").value_or(false));
  auto saved = workspace.SaveState();
  if (!saved.has_value())
    return Failure(saved.error());
  result.Set("workspace_dir", workspace.path().AsUTF8Unsafe());
  result.Set("draft_id", draft);
  result.Set("content_digest", digest);
  result.Set("base_revision", String(workspace.state(), "base_revision"));
  base::ListValue artifacts;
  if (screenshot) {
    auto capture = Screenshot(connection, workspace);
    if (Ok(capture))
      artifacts.Append(String(capture, "artifact"));
    result.Set("screenshot", std::move(capture));
  }
  result.Set("artifacts", std::move(artifacts));
  return result;
}

base::DictValue Publish(HomeConnection& connection,
                        DaoHomeWorkspace& workspace,
                        const std::string& summary,
                        const std::string& rollback) {
  if (summary.size() > kMaxHomeSummaryBytes || !base::IsStringUTF8(summary) ||
      summary.find('\0') != std::string::npos ||
      base::TrimWhitespaceASCII(summary, base::TRIM_ALL).empty()) {
    return Failure("INVALID_ARGUMENT")
        .Set("max_summary_bytes", static_cast<int>(kMaxHomeSummaryBytes));
  }
  if (workspace.state().Find("pending_operation"))
    return Recover(connection, workspace);
  auto files = workspace.Capture();
  if (!files.has_value())
    return Failure(files.error());
  const auto digest = HomeFileSnapshotDigest(*files);
  if (rollback.empty() &&
      (!workspace.state().FindBool("publishable").value_or(false) ||
       digest != String(workspace.state(), "preview_digest") ||
       String(workspace.state(), "draft_id").empty()))
    return Failure(digest != String(workspace.state(), "preview_digest") &&
                           workspace.state().Find("preview_digest")
                       ? "FILES_CHANGED"
                       : "PREVIEW_REQUIRED");
  const auto id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  auto params = SessionParams(workspace);
  params.Set("base_revision", String(workspace.state(), "base_revision"));
  params.Set("operation_id", id);
  params.Set("summary", summary);
  if (rollback.empty()) {
    params.Set("draft_id", String(workspace.state(), "draft_id"));
    params.Set("digest", digest);
  } else {
    params.Set("revision", rollback);
  }
  auto saved = workspace.SaveSnapshot(*files);
  if (!saved.has_value())
    return Failure(saved.error());
  auto pending = params.Clone();
  pending.Remove("token");
  pending.Set("kind", rollback.empty() ? "publish" : "rollback");
  pending.Set("digest", digest);
  workspace.state().Set("pending_operation", std::move(pending));
  saved = workspace.SaveState();
  if (!saved.has_value())
    return Failure(saved.error());
  auto response =
      connection.Call(rollback.empty() ? "draft.publish" : "project.rollback",
                      std::move(params));
  if (!Ok(response)) {
    if (ErrorCode(response) == "BROWSER_UNAVAILABLE" ||
        ErrorCode(response) == "PROTOCOL_MISMATCH")
      return Failure("OUTCOME_UNKNOWN").Set("operation_id", id);
    workspace.state().Remove("pending_operation");
    saved = workspace.SaveState();
    if (!saved.has_value())
      response.Set("local_recovery_needed", true);
    return response;
  }
  const auto revision = String(response, "revision");
  if (revision.empty() || String(response, "operation_id") != id ||
      (rollback.empty() && String(response, "digest") != digest))
    return Failure("OUTCOME_UNKNOWN").Set("operation_id", id);
  workspace.state().Set("last_publication", response.Clone());
  if (rollback.empty())
    saved = workspace.AdvanceBase(revision, *files);
  else {
    workspace.state().Remove("pending_operation");
    workspace.state().Remove("preview_digest");
    workspace.state().Remove("draft_id");
    saved = workspace.SaveState();
    response.Set("fresh_pull_required", true);
  }
  if (!saved.has_value())
    response.Set("local_recovery_needed", true);
  response.Set("workspace_dir", workspace.path().AsUTF8Unsafe());
  return response;
}

}  // namespace

int RunDaoHomeCommand(const base::CommandLine& command_line,
                      const std::vector<std::string>& args,
                      base::FilePath user_data_dir,
                      const std::string& client_name,
                      const std::optional<base::FilePath>& browser_path,
                      const std::string& version) {
  const std::string command = args.size() > 1 ? args[1] : "help";
  if (command_line.HasSwitch("help") || command == "help") {
    std::cout << (ChineseLocale() ? kDaoHomeHelpZh : kDaoHomeHelpEn) << '\n';
    return 0;
  }
  if (command == "guide") {
    const auto topic = Switch(command_line, "topic");
    if (!topic.empty() && topic != "runtime" && topic != "sources")
      return Emit(Failure("USAGE"), command);
    std::cout << "<!-- dao-browser home guide " << version << " -->\n"
              << (topic == "runtime"   ? kDaoHomeRuntimeGuide
                  : topic == "sources" ? kDaoHomeSourcesGuide
                                       : kDaoHomeGuide)
              << '\n';
    return 0;
  }
  const std::set<std::string> commands{
      "pull",    "status",     "diff",     "preview",  "publish", "diagnostics",
      "sources", "screenshot", "versions", "rollback", "resume",  "clean"};
  if (!commands.contains(command))
    return Emit(Failure("USAGE"), command);
  std::set<std::string> switches{"client", "user-data-dir", "no-launch"};
  if (command == "pull")
    switches.insert({"from", "profile", "reason"});
  else
    switches.insert("workspace");
  if (command == "preview")
    switches.insert("screenshot");
  if (command == "publish" || command == "rollback")
    switches.insert("summary");
  if (command == "resume")
    switches.insert("reason");
  if (command == "clean")
    switches.insert({"discard", "close-session"});
  for (const auto& [name, value] : command_line.GetSwitches())
    if (!switches.contains(name))
      return Emit(Failure("USAGE"), command);
  const bool source_test =
      command == "sources" && args.size() == 4 && args[2] == "test";
  if ((command == "pull" && !command_line.HasSwitch("from") &&
       base::TrimWhitespaceASCII(Switch(command_line, "reason"), base::TRIM_ALL)
           .empty()) ||
      (command == "rollback" && args.size() == 3 && args[2].empty()) ||
      (source_test && args[3].empty()))
    return Emit(Failure("USAGE"), command);
  if (args.size() != (command == "rollback" ? 3u
                      : source_test         ? 4u
                                            : 2u) ||
      ((command == "publish" || command == "rollback") &&
       base::TrimWhitespaceASCII(Switch(command_line, "summary"),
                                 base::TRIM_ALL)
           .empty()) ||
      (command == "resume" &&
       base::TrimWhitespaceASCII(Switch(command_line, "reason"), base::TRIM_ALL)
           .empty()) ||
      (command_line.HasSwitch("profile") && command_line.HasSwitch("from")))
    return Emit(Failure("USAGE"), command);
  std::string effective_client = client_name;
  std::unique_ptr<DaoHomeWorkspace> workspace;
  const bool from = command == "pull" && command_line.HasSwitch("from");
  if (from || command_line.HasSwitch("workspace")) {
    auto opened = DaoHomeWorkspace::Open(
        command_line.GetSwitchValuePath(from ? "from" : "workspace"));
    if (!opened.has_value())
      return Emit(Failure(opened.error()), command);
    workspace = std::move(*opened);
    const auto stored_dir = base::FilePath::FromUTF8Unsafe(
        String(workspace->state(), "user_data_dir"));
    if (!stored_dir.IsAbsolute() ||
        (command_line.HasSwitch("user-data-dir") &&
         base::MakeAbsoluteFilePath(user_data_dir) != stored_dir))
      return Emit(Failure("WORKSPACE_INVALID"), command);
    user_data_dir = stored_dir;
    const auto stored_client = String(workspace->state(), "client_name");
    if (stored_client.empty() ||
        (command_line.HasSwitch("client") && client_name != stored_client))
      return Emit(Failure("WORKSPACE_INVALID"), command);
    effective_client = stored_client;
  } else if (command != "pull" && command != "status")
    return Emit(Failure("USAGE"), command);
  if (command == "diff")
    return Emit(Diff(*workspace), command);
  if (command == "clean" && !command_line.HasSwitch("close-session"))
    return Emit(
        CleanWorkspace(*workspace, command_line.HasSwitch("discard"), false),
        command);
  if (command == "clean" && !command_line.HasSwitch("discard")) {
    auto clean = workspace->IsClean();
    if (!clean.has_value())
      return Emit(Failure(clean.error()), command);
    if (!*clean || workspace->state().Find("pending_operation"))
      return Emit(Failure("UNPUBLISHED_CHANGES"), command);
  }
  user_data_dir = base::MakeAbsoluteFilePath(user_data_dir);
  if (user_data_dir.empty())
    return Emit(Failure("BROWSER_UNAVAILABLE"), command);
  HomeConnection connection(user_data_dir, effective_client, version);
  const bool connected =
      connection.Connect(!command_line.HasSwitch("no-launch") &&
                             command != "status" && command != "clean",
                         browser_path);
  if (command == "clean") {
    if (!connected)
      return Emit(Failure(connection.error_code()), command);
    auto response = connection.Call("session.close", SessionParams(*workspace));
    if (!Ok(response) && ErrorCode(response) != "SESSION_REVOKED" &&
        ErrorCode(response) != "SESSION_EXPIRED")
      return Emit(std::move(response), command);
    return Emit(
        CleanWorkspace(*workspace, command_line.HasSwitch("discard"), true),
        command);
  }
  if (!connected)
    return Emit(Failure(connection.error_code()), command);
  if (command == "pull")
    return Emit(Pull(connection, command_line, user_data_dir, workspace.get()),
                command);
  if (!workspace)
    return Emit(base::DictValue()
                    .Set("ok", true)
                    .Set("reachable", true)
                    .Set("version", version),
                command);
  if (command == "resume") {
    auto status = connection.Call("session.status", SessionParams(*workspace));
    if (Ok(status)) {
      if (workspace->state().Find("pending_operation"))
        return Emit(Recover(connection, *workspace), command);
      auto result = WorkspaceResult(*workspace);
      result.Merge(std::move(status));
      return Emit(std::move(result), command);
    }
    if (ErrorCode(status) != "SESSION_EXPIRED")
      return Emit(std::move(status), command);
    auto params = base::DictValue()
                      .Set("reason", Switch(command_line, "reason"))
                      .Set("profile", String(workspace->state(), "profile_id"));
    auto session =
        connection.Call("session.open", std::move(params), base::Seconds(65));
    if (!Ok(session))
      return Emit(std::move(session), command);
    base::ScopedClosureRunner new_session_cleanup(base::BindOnce(
        &HomeConnection::CloseUnretainedSession, base::Unretained(&connection),
        String(session, "token")));
    if (!ValidSession(session) || String(session, "profile_id") !=
                                      String(workspace->state(), "profile_id"))
      return Emit(Failure("PROTOCOL_MISMATCH"), command);
    workspace->state().Set("token", String(session, "token"));
    workspace->state().Set("expires_at_ms",
                           *session.FindDouble("expires_at_ms"));
    workspace->state().Remove("preview_digest");
    workspace->state().Remove("draft_id");
    workspace->state().Remove("publishable");
    auto saved = workspace->SaveState();
    if (!saved.has_value())
      return Emit(Failure(saved.error()), command);
    new_session_cleanup.ReplaceClosure(base::OnceClosure());
    if (workspace->state().Find("pending_operation"))
      return Emit(Recover(connection, *workspace), command);
    return Emit(WorkspaceResult(*workspace), command);
  }
  if (command == "preview")
    return Emit(
        Preview(connection, *workspace, command_line.HasSwitch("screenshot")),
        command);
  if (command == "publish" || command == "rollback")
    return Emit(Publish(connection, *workspace, Switch(command_line, "summary"),
                        command == "rollback" ? args[2] : ""),
                command);
  if (command == "screenshot")
    return Emit(Screenshot(connection, *workspace), command);
  if (command == "status" && workspace->state().Find("pending_operation")) {
    auto recovered = Recover(connection, *workspace);
    if (!Ok(recovered) ||
        recovered.FindBool("local_recovery_needed").value_or(false))
      return Emit(std::move(recovered), command);
  }
  auto params = SessionParams(*workspace);
  std::string method = "session.status";
  if (command == "diagnostics")
    method = "project.diagnostics";
  if (command == "versions")
    method = "project.versions";
  if (command == "sources") {
    if (workspace->state().Find("pending_operation"))
      return Emit(Failure("OUTCOME_UNKNOWN"), command);
    auto candidate = workspace->Capture();
    if (!candidate.has_value())
      return Emit(Failure(candidate.error()), command);
    const bool changed = HomeFileSnapshotDigest(*candidate) !=
                         String(workspace->state(), "candidate_digest");
    if (source_test && changed)
      return Emit(Failure("PREVIEW_REQUIRED"), command);
    if (!source_test &&
        (changed || String(workspace->state(), "draft_id").empty())) {
      auto staged = Stage(connection, *workspace, *candidate);
      if (!Ok(staged))
        return Emit(std::move(staged), command);
    }
    method = source_test ? "draft.test_source" : "draft.sources";
    const auto draft = String(workspace->state(), "draft_id");
    if (!draft.empty())
      params.Set("draft_id", draft);
    if (source_test) {
      if (draft.empty())
        return Emit(Failure("PREVIEW_REQUIRED"), command);
      params.Set("connector_id", args[3]);
      workspace->state().Remove("preview_digest");
      workspace->state().Remove("publishable");
      auto saved = workspace->SaveState();
      if (!saved.has_value())
        return Emit(Failure(saved.error()), command);
    }
  }
  auto response = connection.Call(method, std::move(params));
  if (Ok(response)) {
    auto local = WorkspaceResult(*workspace);
    if (const auto* outcome = workspace->state().FindDict("last_publication"))
      local.Set("last_publication", outcome->Clone());
    local.Merge(std::move(response));
    response = std::move(local);
  }
  return Emit(std::move(response), command);
}

}  // namespace dao
