// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_PAGE_TOOLS_H_
#define DAO_BROWSER_AUTOMATION_DAO_PAGE_TOOLS_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "url/origin.h"

namespace content {
class WebContents;
}

namespace dao {

class DaoHighlightCleanupQueue;

class DaoPageTools {
 public:
  class UiDelegate {
   public:
    virtual ~UiDelegate() = default;

    virtual void MoveCursor(content::WebContents* target,
                            double x,
                            double y,
                            base::OnceCallback<void(bool)> callback) = 0;
    virtual void PlayClickRipple(content::WebContents* target) = 0;
    virtual void CancelCursor(content::WebContents* target) = 0;
    virtual bool IsTargetLocked(content::WebContents* target) = 0;
    virtual void LockTarget(content::WebContents* target) = 0;
    virtual void UnlockTarget(content::WebContents* target) = 0;
  };

  using ResultCallback = base::OnceCallback<void(DaoBrowserToolResult)>;
  using TargetResolver = base::RepeatingCallback<
      base::expected<content::WebContents*, DaoToolError>()>;

  DaoPageTools(DaoDevToolsClient* devtools_client, UiDelegate* ui_delegate);
  ~DaoPageTools();

  DaoPageTools(const DaoPageTools&) = delete;
  DaoPageTools& operator=(const DaoPageTools&) = delete;

  static bool Handles(std::string_view name);

  void Execute(std::string request_id,
               std::string name,
               content::WebContents* target,
               url::Origin committed_origin,
               int64_t document_sequence_number,
               TargetResolver target_resolver,
               base::DictValue arguments,
               ResultCallback callback);
  bool Cancel(std::string_view request_id, DaoToolError error);
  void CancelAll(DaoToolError error);

  size_t operation_count_for_testing() const;
  size_t lock_count_for_testing() const;
  size_t highlight_count_for_testing() const;
  size_t cursor_count_for_testing() const;
  void TrackCursorForTesting(content::WebContents* target);
  void ClearHighlights(content::WebContents* target);
  void ClearAllHighlights();
  void ClearCursors();

 private:
  struct Operation;
  struct LockEntry;
  struct HighlightEntry {
    HighlightEntry(base::WeakPtr<content::WebContents> target,
                   std::string generation);
    ~HighlightEntry();
    HighlightEntry(HighlightEntry&& other) noexcept;
    HighlightEntry& operator=(HighlightEntry&& other) noexcept;

    HighlightEntry(const HighlightEntry&) = delete;
    HighlightEntry& operator=(const HighlightEntry&) = delete;

    base::WeakPtr<content::WebContents> target;
    std::string generation;
  };

  Operation* FindOperation(std::string_view request_id);
  bool CancelInternal(std::string_view request_id, DaoToolError error);
  int SendCommand(std::string_view request_id,
                  std::string method,
                  base::DictValue params,
                  DaoDevToolsClient::ResponseCallback callback);
  void Finish(std::string_view request_id, DaoBrowserToolResult result);
  void FinishSuccess(std::string_view request_id, base::Value data);
  void FinishError(std::string_view request_id, DaoToolError error);
  void CleanupOperation(Operation* operation);
  void CancelCursorState(Operation* operation);
  void TrackCursor(content::WebContents* target);
  bool ValidateOperationTarget(std::string_view request_id);

  bool AcquireLock(Operation* operation);
  void ReleaseLock(Operation* operation);
  void ClearTemporaryHighlight(Operation* operation);
  void QueueHighlightCleanup(base::WeakPtr<content::WebContents> target,
                             std::string generation);

  void ExecuteGetPageInfo(std::string_view request_id);
  void ExecuteGetPageHtml(std::string_view request_id);
  void ExecuteAccessibilityTree(std::string_view request_id);
  void ExecuteCaptureScreenshot(std::string_view request_id);
  void ExecuteClickElement(std::string_view request_id);
  void ExecuteAnimatedClick(std::string_view request_id, std::string selector);
  void OnAnimatedClickBounds(std::string request_id,
                             DaoDevToolsClient::CommandResult result);
  void OnAnimatedCursorMoved(std::string request_id,
                             double x,
                             double y,
                             bool moved);
  void DispatchMouseMove(std::string_view request_id, double x, double y);
  void DispatchMousePress(std::string request_id,
                          double x,
                          double y,
                          DaoDevToolsClient::CommandResult result);
  void DispatchMouseRelease(std::string request_id,
                            double x,
                            double y,
                            DaoDevToolsClient::CommandResult result);
  void FinishAnimatedClick(std::string request_id,
                           DaoDevToolsClient::CommandResult result);
  void ExecuteMoveCursor(std::string_view request_id);
  void ExecuteHighlightElement(std::string_view request_id);
  void ExecuteScroll(std::string_view request_id, bool up);
  void ExecuteScrollToElement(std::string_view request_id);
  void ExecutePressKeyChord(std::string_view request_id);
  void ExecuteTypeText(std::string_view request_id);
  void InsertText(std::string_view request_id);
  void ExecuteScript(std::string_view request_id);

  raw_ptr<DaoDevToolsClient> devtools_client_;
  raw_ptr<UiDelegate> ui_delegate_;
  scoped_refptr<DaoHighlightCleanupQueue> highlight_cleanup_queue_;
  std::map<std::string, std::unique_ptr<Operation>, std::less<>> operations_;
  std::vector<LockEntry> lock_entries_;
  std::vector<HighlightEntry> highlighted_targets_;
  std::vector<base::WeakPtr<content::WebContents>> cursor_targets_;
  bool is_shutting_down_ = false;
  bool is_cancelling_ = false;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoPageTools> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_PAGE_TOOLS_H_
