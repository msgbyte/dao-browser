// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_JEV_TASK_H_
#define DAO_BROWSER_AUTOMATION_DAO_JEV_TASK_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/weak_document_ptr.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

namespace content {
class WebContents;
}
namespace network {
class SimpleURLLoader;
}

namespace dao {
class DaoBrowserAutomationSession;
class DaoBrowserToolExecutor;

// One bounded task, owned by its executor request. Child actions use the same
// client, session and target lease as the parent; no WebUI is needed.
class DaoJevTask {
 public:
  using Callback = base::OnceCallback<void(DaoBrowserToolResult)>;
  DaoJevTask(base::WeakPtr<DaoBrowserToolExecutor> executor,
             base::WeakPtr<DaoBrowserAutomationSession> session,
             content::WebContents* target,
             DaoToolClient client,
             base::DictValue arguments,
             Callback callback);
  ~DaoJevTask();
  // Returns false when the task finished synchronously before any browser
  // work, so callers can treat it like a rejected call.
  bool Start();
  void Cancel(std::string status);

 private:
  bool CheckTarget();
  bool Check();
  bool Allowed(std::string_view tool) const;
  bool DocumentChanged() const;
  void Observe();
  void OnObserved(DaoBrowserToolResult result);
  void OnDecision(std::optional<std::string> body);
  void OnAction(std::string operation,
                std::string target,
                DaoBrowserToolResult result);
  void Remember(const std::string& operation);
  void Call(std::string name, base::DictValue arguments, Callback callback);
  void Finish(std::string status);

  base::WeakPtr<DaoBrowserToolExecutor> executor_;
  base::WeakPtr<DaoBrowserAutomationSession> session_;
  base::WeakPtr<content::WebContents> target_;
  content::WeakDocumentPtr observed_document_;
  DaoToolClient client_;
  base::DictValue arguments_;
  Callback callback_;
  base::DictValue plugin_;
  base::DictValue page_;
  base::DictValue questions_;
  base::ListValue inputs_;
  base::ListValue actions_;
  base::ListValue recent_;
  base::ListValue verified_;
  std::string child_id_;
  std::string last_state_;
  int steps_ = 0;
  int unchanged_ = 0;
  int stale_ = 0;
  int observations_ = 0;
  int service_requests_ = 0;
  double observation_ms_ = 0;
  double service_ms_ = 0;
  double action_ms_ = 0;
  base::TimeTicks started_ = base::TimeTicks::Now();
  base::TimeTicks phase_started_;
  base::OneShotTimer deadline_;
  base::OneShotTimer wait_;
  base::RepeatingTimer target_check_;
  PrefChangeRegistrar settings_observer_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::WeakPtrFactory<DaoJevTask> weak_factory_{this};
};
}  // namespace dao
#endif  // DAO_BROWSER_AUTOMATION_DAO_JEV_TASK_H_
