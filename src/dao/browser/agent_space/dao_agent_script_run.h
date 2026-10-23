// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SCRIPT_RUN_H_
#define DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SCRIPT_RUN_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

class Profile;

namespace content {
class DevToolsAgentHost;
class WebContents;
}  // namespace content

namespace dao {

class DaoDevToolsClient;

// Runs one model-written script for run_browser_script. The script executes
// in a hidden about:blank page that has no WebUI bindings; its only way out
// is a CDP binding that turns `dao.call()` into ordinary tool calls, so a
// script can do exactly what the calling agent could do one call at a time,
// in a single round trip.
class DaoAgentScriptRun : public content::WebContentsObserver {
 public:
  using ToolCallback = base::OnceCallback<void(DaoBrowserToolResult)>;
  // Executes one tool on behalf of the script. `request_id` is unique within
  // the run.
  using ToolCaller = base::RepeatingCallback<void(std::string request_id,
                                                  std::string name,
                                                  base::DictValue arguments,
                                                  ToolCallback callback)>;
  using ToolCanceller = base::RepeatingCallback<void(std::string request_id)>;
  using DoneCallback = base::OnceCallback<void(DaoBrowserToolResult)>;

  static constexpr base::TimeDelta kDefaultTimeout = base::Minutes(2);
  static constexpr base::TimeDelta kMaxTimeout = base::Minutes(10);

  DaoAgentScriptRun(Profile* profile,
                    std::string run_id,
                    std::string code,
                    std::string default_space_id,
                    base::TimeDelta timeout,
                    ToolCaller tool_caller,
                    ToolCanceller tool_canceller,
                    DoneCallback done);
  ~DaoAgentScriptRun() override;

  DaoAgentScriptRun(const DaoAgentScriptRun&) = delete;
  DaoAgentScriptRun& operator=(const DaoAgentScriptRun&) = delete;

  void Start();
  void Cancel(DaoToolError error);

  // content::WebContentsObserver:
  void DidStopLoading() override;
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;

 private:
  void Prepare();
  // Sends DevTools setup commands in order, then installs the runtime.
  void SendSetupCommands(
      std::deque<std::pair<std::string, base::DictValue>> commands);
  void InstallRuntime();
  void RunScript();
  void OnScriptFinished(base::expected<base::Value, DaoToolError> result);
  void OnDevToolsEvent(content::DevToolsAgentHost* host,
                       const std::string& method,
                       const base::DictValue& params);
  void OnToolResult(int64_t call_id, DaoBrowserToolResult result);
  void Finish(DaoBrowserToolResult result);

  std::unique_ptr<content::WebContents> contents_;
  std::unique_ptr<DaoDevToolsClient> devtools_client_;
  const std::string run_id_;
  const std::string code_;
  const std::string default_space_id_;
  const base::TimeDelta timeout_;
  ToolCaller tool_caller_;
  ToolCanceller tool_canceller_;
  DoneCallback done_;
  bool started_script_ = false;
  std::set<std::string> outstanding_calls_;
  int tool_calls_ = 0;
  base::OneShotTimer deadline_;
  base::WeakPtrFactory<DaoAgentScriptRun> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SCRIPT_RUN_H_
