// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_BROWSER_TOOL_EXECUTOR_H_
#define DAO_BROWSER_AUTOMATION_DAO_BROWSER_TOOL_EXECUTOR_H_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/automation/dao_page_tools.h"
#include "url/origin.h"

namespace content {
class WebContents;
}

namespace dao {

class DaoBrowserAutomationSession;
class DaoBrowserToolCatalog;
class DaoDevToolsClient;
class DaoDevToolsTools;
class DaoTabTools;

class DaoBrowserToolExecutor {
 public:
  using ResultCallback = base::OnceCallback<void(DaoBrowserToolResult)>;

  DaoBrowserToolExecutor(DaoDevToolsClient* devtools_client,
                         DaoPageTools::UiDelegate* ui_delegate);
  DaoBrowserToolExecutor(DaoDevToolsClient* devtools_client,
                         DaoPageTools::UiDelegate* ui_delegate,
                         const DaoBrowserToolCatalog* catalog);
  ~DaoBrowserToolExecutor();

  DaoBrowserToolExecutor(const DaoBrowserToolExecutor&) = delete;
  DaoBrowserToolExecutor& operator=(const DaoBrowserToolExecutor&) = delete;

  void Execute(DaoBrowserAutomationSession* session,
               DaoToolClient client,
               DaoBrowserToolCall call,
               ResultCallback callback);
  void Cancel(std::string_view request_id,
              DaoToolError error =
                  MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                   "Browser tool call was cancelled."));
  void CancelAll(DaoToolError error);
  void ClearPageState(DaoBrowserAutomationSession* session);
  void ClearSessionState(DaoBrowserAutomationSession* session);

  size_t pending_count_for_testing() const { return pending_.size(); }
  size_t page_operation_count_for_testing() const {
    return page_tools_->operation_count_for_testing();
  }
  size_t page_lock_count_for_testing() const {
    return page_tools_->lock_count_for_testing();
  }
  size_t page_highlight_count_for_testing() const {
    return page_tools_->highlight_count_for_testing();
  }
  size_t page_cursor_count_for_testing() const {
    return page_tools_->cursor_count_for_testing();
  }
  void TrackPageCursorForTesting(content::WebContents* target) {
    page_tools_->TrackCursorForTesting(target);
  }

 private:
  struct PendingRequest;

  void Dispatch(std::string request_id,
                std::string name,
                DaoBrowserAutomationSession* session,
                content::WebContents* target,
                url::Origin committed_origin,
                int64_t document_sequence_number,
                DaoPageTools::TargetResolver target_resolver,
                base::DictValue arguments);
  void Complete(std::string request_id, DaoBrowserToolResult result);
  void ReplyError(ResultCallback callback, DaoToolError error);
  void OnDeadline(std::string request_id);

  raw_ptr<const DaoBrowserToolCatalog> catalog_;
  std::unique_ptr<DaoPageTools> page_tools_;
  std::unique_ptr<DaoTabTools> tab_tools_;
  std::unique_ptr<DaoDevToolsTools> devtools_tools_;
  std::map<std::string, std::unique_ptr<PendingRequest>, std::less<>> pending_;
  bool is_shutting_down_ = false;
  bool is_cancelling_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoBrowserToolExecutor> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_BROWSER_TOOL_EXECUTOR_H_
