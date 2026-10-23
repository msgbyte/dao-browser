// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent_space/dao_agent_script_run.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "dao/browser/mcp/dao_mcp_protocol.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace dao {
namespace {

constexpr char kBindingName[] = "__daoBridge";
constexpr int kMaxToolCalls = 500;
constexpr size_t kMaxResultBytes = 2 * 1024 * 1024;
constexpr size_t kMaxResponseBytes = 4 * 1024 * 1024;
// User code starts on this zero-based line of the evaluated expression.
constexpr int kUserCodeLineOffset = 4;

// The script runtime. `%SPACE%` becomes the JSON default space id or null.
// Keep the API in sync with the dao-browser guide.
constexpr char kRuntimeSource[] = R"JS((() => {
  'use strict';
  if (globalThis.dao) {
    return true;
  }
  const bridge = globalThis.__daoBridge;
  try {
    delete globalThis.__daoBridge;
  } catch (e) {}
  const defaultSpaceId = %SPACE%;
  const pending = new Map();
  const logs = [];
  const maxLogChars = 64 * 1024;
  let logChars = 0;
  // Files for the dao-browser CLI to write locally after the run.
  const files = [];
  const maxFileChars = 3 * 1024 * 1024;
  let fileChars = 0;
  let nextId = 1;
  let stopped = null;

  const format = (value) => {
    if (typeof value === 'string') {
      return value;
    }
    if (value instanceof Error) {
      return value.stack || String(value);
    }
    try {
      return JSON.stringify(value);
    } catch (e) {
      return String(value);
    }
  };
  for (const level of ['log', 'info', 'debug', 'warn', 'error']) {
    console[level] = (...args) => {
      if (logChars >= maxLogChars) {
        return;
      }
      let line = Array.from(args, format).join(' ');
      if (level === 'warn' || level === 'error') {
        line = `${level}: ${line}`;
      }
      logChars += line.length;
      logs.push(logChars > maxLogChars ? `${line.slice(0, 200)} [logs truncated]`
                                       : line);
    };
  }

  class DaoToolError extends Error {
    constructor(error) {
      super(error.message || error.code || 'Browser tool failed.');
      this.name = 'DaoToolError';
      this.code = error.code;
      this.retryable = !!error.retryable;
    }
  }

  Object.defineProperty(globalThis, '__daoSettle', {
    value: (id, envelope) => {
      const settle = pending.get(id);
      if (settle) {
        pending.delete(id);
        settle(envelope);
      }
    },
  });
  Object.defineProperty(globalThis, '__daoReport', {
    value: () => ({logs, stopped, files}),
  });

  const call = (name, args) => {
    if (typeof name !== 'string' || !name) {
      return Promise.reject(new TypeError('dao.call(name, args) needs a tool name.'));
    }
    if (stopped) {
      return Promise.reject(new DaoToolError(stopped));
    }
    const id = nextId++;
    return new Promise((resolve, reject) => {
      pending.set(id, (envelope) => {
        if (!envelope || envelope.error) {
          const error = envelope && envelope.error ||
              {code: 'INTERNAL_ERROR', message: 'Browser tool failed.'};
          if (error.code === 'SPACE_USER_IN_CONTROL' ||
              error.code === 'SPACE_CLOSED') {
            stopped = error;
          }
          reject(new DaoToolError(error));
          return;
        }
        const data = envelope.data;
        if (envelope.media && data && typeof data === 'object') {
          data.media = envelope.media;
        }
        resolve(data);
      });
      bridge(JSON.stringify({id, name, args: args || {}}));
    });
  };

  const withRefs = (result) => {
    if (result && Array.isArray(result.matches)) {
      for (const match of result.matches) {
        if (match && typeof match === 'object') {
          match.document_id = result.document_id;
          match.snapshot_id = result.snapshot_id;
        }
      }
    }
    return result;
  };
  const refArgs = (match) => {
    if (!match || typeof match.ref_id !== 'string') {
      throw new TypeError('Pass one of the matches returned by find() or waitFor().');
    }
    return {
      ref_id: match.ref_id,
      document_id: match.document_id,
      snapshot_id: match.snapshot_id,
    };
  };

  class Space {
    constructor(id) {
      this.id = id;
      Object.freeze(this);
    }
    call(name, args) {
      return call(name, {...(args || {}), space_id: this.id});
    }
    status(text) {
      return call('set_space_status', {space_id: this.id, text: String(text)});
    }
    open(url, options = {}) {
      return this.call('open_tab', {url, label: options.label});
    }
    async tabs() {
      return (await this.call('list_tabs')).tabs;
    }
    switchTo(tabId) {
      return this.call('switch_tab', {tab_id: tabId});
    }
    info() {
      return this.call('get_page_info');
    }
    async find(query = {}, options = {}) {
      return withRefs(await this.call('query_elements',
                                      {...query, label: options.label}));
    }
    async waitFor(query = {}, options = {}) {
      return withRefs(await this.call('wait_for_element', {
        ...query,
        timeout_ms: options.timeoutMs,
        label: options.label,
      }));
    }
    click(match, options = {}) {
      return this.call('click_by_ref', {...refArgs(match), label: options.label});
    }
    fill(match, text, options = {}) {
      return this.call('fill_by_ref',
                       {...refArgs(match), text: String(text), label: options.label});
    }
    type(text, options = {}) {
      return this.call('type_text',
                       {text: String(text), clear: !!options.clear, label: options.label});
    }
    press(keys, options = {}) {
      return this.call('press_key_chord', {keys: String(keys), label: options.label});
    }
    async eval(code, options = {}) {
      const result = await this.call('execute_script',
                                     {code: String(code), label: options.label});
      return result && typeof result === 'object' && 'result' in result ?
          result.result : result;
    }
    screenshot() {
      return this.call('capture_screenshot');
    }
    help(reason, options = {}) {
      return call('request_user_help',
                  {space_id: this.id, reason: String(reason), tab_id: options.tabId});
    }
    dialog(accept, promptText) {
      return call('handle_dialog', {
        space_id: this.id,
        accept: !!accept,
        prompt_text: promptText === undefined ? undefined : String(promptText),
      });
    }
    finish(options = {}) {
      return call('finish_space', {
        space_id: this.id,
        keep: options.keep || [],
        summary: options.summary,
      });
    }
  }

  const dao = Object.freeze({
    call,
    async createSpace(name, url, options = {}) {
      const result = await call('create_space', {
        name: String(name),
        url,
        logins: options.logins === undefined ? undefined : !!options.logins,
      });
      return new Space(result.space_id);
    },
    space(id) {
      if (!id) {
        throw new TypeError('dao.space(id) needs a space_id.');
      }
      return new Space(String(id));
    },
    async spaces() {
      return (await call('list_spaces')).spaces;
    },
    async siteSkills(url) {
      return (await call('get_site_skills', {url: String(url)})).skills;
    },
    sleep(ms) {
      return new Promise(
          resolve => setTimeout(resolve, Math.max(0, Math.min(Number(ms) || 0, 60000))));
    },
    saveFile(path, data, options = {}) {
      if (typeof path !== 'string' || !path) {
        throw new TypeError('dao.saveFile(path, data) needs a file path.');
      }
      const text = typeof data === 'string' ? data : JSON.stringify(data, null, 2);
      fileChars += text.length;
      if (fileChars > maxFileChars) {
        throw new RangeError('dao.saveFile can return at most 3 MiB per run.');
      }
      files.push({path, data: text, base64: !!options.base64});
    },
  });
  Object.defineProperty(globalThis, 'dao', {value: dao});
  Object.defineProperty(globalThis, 'space', {
    value: defaultSpaceId ? new Space(defaultSpaceId) : null,
  });
  return true;
})())JS";

std::string BuildScriptExpression(const std::string& code) {
  // Keep kUserCodeLineOffset in sync with the lines before the user code.
  return base::StrCat({R"JS((async () => {
  try {
    const __daoValue = await (async () => {
"use strict";
)JS",
                       code, R"JS(
    })();
    const report = globalThis.__daoReport();
    return {ok: true, value: __daoValue === undefined ? null : __daoValue,
            logs: report.logs, stopped: report.stopped, files: report.files};
  } catch (error) {
    const report = globalThis.__daoReport();
    return {ok: false,
            error: {
              name: String(error && error.name || 'Error'),
              message: String(error && error.message || error),
              code: error && error.code || null,
              stack: String(error && error.stack || ''),
            },
            logs: report.logs, stopped: report.stopped, files: report.files};
  }
})())JS"});
}

DaoBrowserToolResult ErrorResult(DaoToolError error) {
  DaoBrowserToolResult result;
  result.error = std::move(error);
  return result;
}

}  // namespace

DaoAgentScriptRun::DaoAgentScriptRun(Profile* profile,
                                     std::string run_id,
                                     std::string code,
                                     std::string default_space_id,
                                     base::TimeDelta timeout,
                                     ToolCaller tool_caller,
                                     ToolCanceller tool_canceller,
                                     DoneCallback done)
    : devtools_client_(std::make_unique<DaoDevToolsClient>()),
      run_id_(std::move(run_id)),
      code_(std::move(code)),
      default_space_id_(std::move(default_space_id)),
      timeout_(timeout),
      tool_caller_(std::move(tool_caller)),
      tool_canceller_(std::move(tool_canceller)),
      done_(std::move(done)) {
  content::WebContents::CreateParams params(profile);
  contents_ = content::WebContents::Create(params);
  Observe(contents_.get());
}

DaoAgentScriptRun::~DaoAgentScriptRun() {
  weak_factory_.InvalidateWeakPtrs();
  for (const std::string& request_id : outstanding_calls_) {
    tool_canceller_.Run(request_id);
  }
  Observe(nullptr);
  devtools_client_.reset();
  contents_.reset();
}

void DaoAgentScriptRun::Start() {
  deadline_.Start(
      FROM_HERE, timeout_,
      base::BindOnce(&DaoAgentScriptRun::Cancel, weak_factory_.GetWeakPtr(),
                     MakeDaoToolError(DaoToolErrorCode::kToolTimeout,
                                      "The script did not finish before "
                                      "timeout_ms. Split long work into "
                                      "several runs.",
                                      true)));
  contents_->GetController().LoadURL(
      GURL(url::kAboutBlankURL), content::Referrer(),
      ui::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string());
}

void DaoAgentScriptRun::Cancel(DaoToolError error) {
  Finish(ErrorResult(std::move(error)));
}

void DaoAgentScriptRun::DidStopLoading() {
  if (!started_script_) {
    Prepare();
  }
}

void DaoAgentScriptRun::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  Cancel(MakeDaoToolError(DaoToolErrorCode::kInternalError,
                          "The script sandbox stopped unexpectedly.", true));
}

void DaoAgentScriptRun::Prepare() {
  started_script_ = true;
  if (!devtools_client_->AttachTo(contents_.get())) {
    Cancel(MakeDaoToolError(DaoToolErrorCode::kDevToolsAttachFailed,
                            "Could not start the script sandbox."));
    return;
  }
  devtools_client_->SetEventCallback(base::BindRepeating(
      &DaoAgentScriptRun::OnDevToolsEvent, weak_factory_.GetWeakPtr()));
  // The sandbox page has no network: scripts reach pages only through the
  // space tools, never with the user's cookies from here.
  std::deque<std::pair<std::string, base::DictValue>> commands;
  commands.emplace_back("Runtime.enable", base::DictValue());
  commands.emplace_back("Network.enable", base::DictValue());
  commands.emplace_back("Network.emulateNetworkConditions",
                        base::DictValue()
                            .Set("offline", true)
                            .Set("latency", 0)
                            .Set("downloadThroughput", -1)
                            .Set("uploadThroughput", -1));
  commands.emplace_back("Runtime.addBinding",
                        base::DictValue().Set("name", kBindingName));
  SendSetupCommands(std::move(commands));
}

void DaoAgentScriptRun::SendSetupCommands(
    std::deque<std::pair<std::string, base::DictValue>> commands) {
  if (commands.empty()) {
    InstallRuntime();
    return;
  }
  auto [method, params] = std::move(commands.front());
  commands.pop_front();
  devtools_client_->SendCommand(
      method, std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentScriptRun> self,
             std::deque<std::pair<std::string, base::DictValue>> rest,
             DaoDevToolsClient::CommandResult result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->Cancel(std::move(result).error());
              return;
            }
            self->SendSetupCommands(std::move(rest));
          },
          weak_factory_.GetWeakPtr(), std::move(commands)));
}

void DaoAgentScriptRun::InstallRuntime() {
  std::string space = "null";
  if (!default_space_id_.empty()) {
    base::JSONWriter::Write(base::Value(default_space_id_), &space);
  }
  std::string source = kRuntimeSource;
  const size_t marker = source.find("%SPACE%");
  if (marker != std::string::npos) {
    source.replace(marker, 7, space);
  }
  devtools_client_->SendCommand(
      "Runtime.evaluate",
      base::DictValue()
          .Set("expression", std::move(source))
          .Set("returnByValue", true),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentScriptRun> self,
             DaoDevToolsClient::CommandResult result) {
            if (!self) {
              return;
            }
            if (!result.has_value() || !result->is_dict() ||
                result->GetDict().Find("exceptionDetails")) {
              self->Cancel(
                  MakeDaoToolError(DaoToolErrorCode::kInternalError,
                                   "Could not prepare the script runtime."));
              return;
            }
            self->RunScript();
          },
          weak_factory_.GetWeakPtr()));
}

void DaoAgentScriptRun::RunScript() {
  devtools_client_->SendCommand(
      "Runtime.evaluate",
      base::DictValue()
          .Set("expression", BuildScriptExpression(code_))
          .Set("awaitPromise", true)
          .Set("returnByValue", true),
      base::BindOnce(&DaoAgentScriptRun::OnScriptFinished,
                     weak_factory_.GetWeakPtr()),
      kMaxResponseBytes);
}

void DaoAgentScriptRun::OnScriptFinished(
    base::expected<base::Value, DaoToolError> result) {
  if (!result.has_value()) {
    Cancel(std::move(result).error());
    return;
  }
  const base::DictValue* response = result->GetIfDict();
  if (!response) {
    Cancel(MakeDaoToolError(DaoToolErrorCode::kInternalError,
                            "The script returned an invalid response."));
    return;
  }
  // A syntax error rejects the whole expression before the try block runs.
  if (const base::DictValue* exception =
          response->FindDict("exceptionDetails")) {
    const base::DictValue* thrown = exception->FindDict("exception");
    const std::string* description =
        thrown ? thrown->FindString("description") : nullptr;
    const int line =
        exception->FindInt("lineNumber").value_or(0) - kUserCodeLineOffset + 1;
    const int column = exception->FindInt("columnNumber").value_or(0) + 1;
    Cancel(MakeDaoToolError(
        DaoToolErrorCode::kInvalidArgument,
        base::StrCat({"The script could not run",
                      line > 0
                          ? base::StrCat({" (line ", base::NumberToString(line),
                                          ", column ",
                                          base::NumberToString(column), ")"})
                          : std::string(),
                      ": ",
                      description ? *description
                                  : "syntax error. Correct it and retry."})));
    return;
  }
  const base::DictValue* remote = response->FindDict("result");
  const base::DictValue* outcome = remote ? remote->FindDict("value") : nullptr;
  if (!outcome) {
    Cancel(MakeDaoToolError(DaoToolErrorCode::kInternalError,
                            "The script result could not be read."));
    return;
  }
  // Hard stop: once the user is in control or stopped the space, drop the
  // script's output so the model cannot act on a half-finished run.
  if (const base::DictValue* stopped = outcome->FindDict("stopped")) {
    const std::string* code = stopped->FindString("code");
    const std::string* message = stopped->FindString("message");
    Cancel(MakeDaoToolError(
        code && *code == "SPACE_CLOSED" ? DaoToolErrorCode::kSpaceClosed
                                        : DaoToolErrorCode::kSpaceUserInControl,
        message ? *message
                : "The user is in control of this space. Stop and wait."));
    return;
  }

  base::DictValue data;
  data.Set("success", outcome->FindBool("ok").value_or(false));
  if (const base::ListValue* logs = outcome->FindList("logs")) {
    data.Set("logs", logs->Clone());
  }
  if (const base::ListValue* files = outcome->FindList("files");
      files && !files->empty()) {
    data.Set("files", files->Clone());
  }
  data.Set("tool_calls", tool_calls_);
  if (outcome->FindBool("ok").value_or(false)) {
    const base::Value* value = outcome->Find("value");
    std::string json;
    if (value && base::JSONWriter::Write(*value, &json) &&
        json.size() > kMaxResultBytes) {
      data.Set("result_truncated", true);
      data.Set("result", json.substr(0, kMaxResultBytes));
    } else {
      data.Set("result", value ? value->Clone() : base::Value());
    }
  } else if (const base::DictValue* error = outcome->FindDict("error")) {
    data.Set("error", error->Clone());
  }
  DaoBrowserToolResult tool_result;
  tool_result.ok = true;
  tool_result.data = base::Value(std::move(data));
  Finish(std::move(tool_result));
}

void DaoAgentScriptRun::OnDevToolsEvent(content::DevToolsAgentHost* host,
                                        const std::string& method,
                                        const base::DictValue& params) {
  if (method != "Runtime.bindingCalled") {
    return;
  }
  const std::string* name = params.FindString("name");
  const std::string* payload = params.FindString("payload");
  if (!name || *name != kBindingName || !payload) {
    return;
  }
  std::optional<base::Value> request =
      base::JSONReader::Read(*payload, base::JSON_PARSE_RFC);
  const base::DictValue* dict = request ? request->GetIfDict() : nullptr;
  const std::optional<double> raw_id =
      dict ? dict->FindDouble("id") : std::nullopt;
  const std::string* tool = dict ? dict->FindString("name") : nullptr;
  const base::DictValue* args = dict ? dict->FindDict("args") : nullptr;
  if (!raw_id) {
    return;
  }
  const int64_t call_id = static_cast<int64_t>(*raw_id);
  if (!tool || !args) {
    OnToolResult(call_id, ErrorResult(MakeDaoToolError(
                              DaoToolErrorCode::kInvalidArgument,
                              "dao.call(name, args) needs a tool name and an "
                              "arguments object.")));
    return;
  }
  if (*tool == "run_browser_script") {
    OnToolResult(call_id, ErrorResult(MakeDaoToolError(
                              DaoToolErrorCode::kInvalidArgument,
                              "Scripts cannot start other scripts.")));
    return;
  }
  if (++tool_calls_ > kMaxToolCalls) {
    OnToolResult(call_id, ErrorResult(MakeDaoToolError(
                              DaoToolErrorCode::kInvalidArgument,
                              "A script can make at most 500 tool calls.")));
    return;
  }
  const std::string request_id =
      base::StrCat({run_id_, ":", base::NumberToString(call_id)});
  outstanding_calls_.insert(request_id);
  tool_caller_.Run(
      request_id, *tool, args->Clone(),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentScriptRun> self, std::string request_id,
             int64_t call_id, DaoBrowserToolResult result) {
            if (!self) {
              return;
            }
            self->outstanding_calls_.erase(request_id);
            self->OnToolResult(call_id, std::move(result));
          },
          weak_factory_.GetWeakPtr(), request_id, call_id));
}

void DaoAgentScriptRun::OnToolResult(int64_t call_id,
                                     DaoBrowserToolResult result) {
  if (!devtools_client_ || !devtools_client_->agent_host()) {
    return;
  }
  std::string envelope;
  if (!base::JSONWriter::Write(SerializeDaoBrowserToolResult(std::move(result)),
                               &envelope)) {
    envelope = R"({"ok":false,"error":{"code":"INTERNAL_ERROR",)"
               R"("message":"The tool result could not be serialized."}})";
  }
  devtools_client_->SendCommand(
      "Runtime.evaluate",
      base::DictValue().Set(
          "expression",
          base::StrCat({"globalThis.__daoSettle(",
                        base::NumberToString(call_id), ", ", envelope, ")"})),
      base::DoNothing());
}

void DaoAgentScriptRun::Finish(DaoBrowserToolResult result) {
  if (!done_) {
    return;
  }
  deadline_.Stop();
  for (const std::string& request_id : outstanding_calls_) {
    tool_canceller_.Run(request_id);
  }
  outstanding_calls_.clear();
  // The owner deletes this run asynchronously, since Finish() may run inside
  // a DevTools dispatch.
  std::move(done_).Run(std::move(result));
}

}  // namespace dao
