// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SPACE_H_
#define DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SPACE_H_

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/tab_list/tab_list_interface_observer.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/common/javascript_dialog_type.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/automation/dao_page_tools.h"
#include "ui/gfx/geometry/point_f.h"

class BrowserWindowInterface;
class GURL;
class TabListInterface;

namespace content {
class WebContents;
}  // namespace content

namespace download {
class DownloadItem;
}  // namespace download

namespace dao {

class DaoAgentSpaceService;
class DaoBrowserAutomationSession;
class DaoBrowserToolExecutor;
class DaoDevToolsClient;

enum class DaoAgentSpaceState {
  // The agent controls the space.
  kRunning,
  // The user controls the space; agent tools are rejected until handback.
  kUserInControl,
  // Terminal states. The window is gone and the record is kept for the UI.
  kFinished,
  kStopped,
};

enum class DaoAgentSpaceHandoffReason {
  kNone,
  kHelpRequested,
  kPermissionPrompt,
  // A page opened browser UI nobody can see off screen.
  kFileChooser,
  kDeviceChooser,
  kBrowserDialog,
  kManualTakeover,
};

std::string_view DaoAgentSpaceStateToString(DaoAgentSpaceState state);
std::string_view DaoAgentSpaceHandoffReasonToString(
    DaoAgentSpaceHandoffReason reason);

// One step in a space's timeline, shown to the user while they watch.
struct DaoAgentSpaceEvent {
  base::Time time;
  std::string tool;
  std::string label;
  // The element an action targeted, e.g. `button "Add to cart"`.
  std::string target;
  std::string url;
  bool pending = true;
  bool ok = false;
  std::string error_code;
};

// A confirm or prompt dialog waiting in one of the space's pages.
struct DaoAgentSpaceDialog {
  std::string tab_id;
  std::string type;
  std::string message;
  std::string default_prompt;
};

// A background task window owned by one agent. The window is a normal
// browser window that is never shown unless the user opens it, so all
// existing browser tools work unchanged against its automation session.
class DaoAgentSpace : public DaoPageTools::UiDelegate,
                      public TabListInterfaceObserver {
 public:
  DaoAgentSpace(DaoAgentSpaceService* service,
                std::string id,
                std::string name,
                DaoToolClient client,
                std::string owner_label,
                BrowserWindowInterface* browser);
  ~DaoAgentSpace() override;

  DaoAgentSpace(const DaoAgentSpace&) = delete;
  DaoAgentSpace& operator=(const DaoAgentSpace&) = delete;

  const std::string& id() const { return id_; }
  const std::string& name() const { return name_; }
  DaoToolClient client() const { return client_; }
  const std::string& owner_label() const { return owner_label_; }
  // False for clean spaces in a temporary profile without the user's
  // cookies and sign-ins.
  bool uses_logins() const { return uses_logins_; }
  void set_uses_logins(bool uses_logins) { uses_logins_ = uses_logins; }
  DaoAgentSpaceState state() const { return state_; }
  bool is_closed() const {
    return state_ == DaoAgentSpaceState::kFinished ||
           state_ == DaoAgentSpaceState::kStopped;
  }
  DaoAgentSpaceHandoffReason handoff_reason() const { return handoff_reason_; }
  const std::string& handoff_message() const { return handoff_message_; }
  const std::string& status_text() const { return status_text_; }
  const std::string& summary() const { return summary_; }
  // True when Dao stopped the space because its agent went quiet.
  bool stopped_for_inactivity() const { return stopped_for_inactivity_; }
  void set_stopped_for_inactivity() { stopped_for_inactivity_ = true; }
  base::Time created_at() const { return created_at_; }
  base::TimeTicks last_activity() const { return last_activity_; }
  const std::deque<DaoAgentSpaceEvent>& events() const { return events_; }
  const std::optional<gfx::PointF>& cursor() const { return cursor_; }
  const std::optional<DaoAgentSpaceDialog>& pending_dialog() const {
    return pending_dialog_;
  }

  BrowserWindowInterface* browser() const;
  DaoBrowserAutomationSession* session() const { return session_.get(); }
  DaoBrowserToolExecutor* executor() const { return executor_.get(); }
  bool IsWindowVisible() const;
  content::WebContents* GetCurrentContents() const;
  content::WebContents* GetActiveContents() const;
  int GetTabCount() const;

  // Opens the first page and binds the automation session to it.
  bool Initialize(const GURL& url);
  // Points the session at the active tab if its target was closed, e.g. by
  // the user while they had control.
  void EnsureSessionTarget();

  // Serializes the space for list_spaces and the sidebar.
  base::DictValue ToDict(bool include_events) const;
  // A closed space as kept across restarts, and the reverse.
  base::DictValue ToRecord() const;
  void RestoreRecord(const base::DictValue& record);
  base::ListValue SerializeTabs() const;
  // Adds `opened_by` to list_tabs results for this space's tabs.
  void AnnotateTabs(base::Value& list_tabs_data) const;
  // Tabs the user opened while in control stay theirs until the agent
  // switches to one explicitly.
  bool IsUnclaimedUserTab(std::string_view tab_id) const;
  void ClaimTab(std::string_view tab_id);
  // Tabs closed because the space was full, since the agent last heard.
  base::ListValue TakeClosedPopupReports();

  void SetStatus(std::string text);
  void TouchActivity();
  size_t AddEvent(std::string tool, std::string label);
  void CompleteEvent(size_t serial, bool ok, std::string error_code);
  void SetEventLabel(size_t serial, std::string label);
  void SetEventTarget(size_t serial, std::string target);
  // Remembers what the refs in a query result point at, so later actions on
  // them can name their target in the timeline.
  void RememberRefs(const base::Value& query_result);
  std::string DescribeRef(std::string_view ref_id) const;

  // Moves control to the user. In-flight agent calls are cancelled.
  void HandOffToUser(DaoAgentSpaceHandoffReason reason, std::string message);
  // Returns control to the agent and reports what the user changed.
  base::DictValue ReturnToAgent();
  // Marks the space closed. Callers close the window separately.
  void MarkClosed(DaoAgentSpaceState state, std::string summary);
  void DetachWindow();

  // Reported by Chromium's dialog manager for pages in this space.
  void OnDialogOpened(content::WebContents* contents,
                      content::JavaScriptDialogType type,
                      const std::u16string& message,
                      const std::u16string& default_prompt);
  void OnDialogClosed(content::WebContents* contents);
  // A page opened browser UI that only the user can answer.
  void OnPageNeedsUser(content::WebContents* contents,
                       DaoAgentSpaceHandoffReason reason);
  // Answers the pending dialog. Returns false when none is open.
  bool AnswerDialog(bool accept, const std::string& prompt_text);
  // Dialogs the agent has not been told about yet, oldest first.
  base::ListValue TakeDialogReports();

  // Downloads started by the space's pages, oldest first.
  void TrackDownload(download::DownloadItem* item);
  base::ListValue SerializeDownloads() const;

  void ShowWindow();
  void HideWindow();
  void ActivateTab(std::string_view tab_id);
  content::WebContents* FindTab(std::string_view tab_id) const;

  // DaoPageTools::UiDelegate:
  void MoveCursor(content::WebContents* target,
                  double x,
                  double y,
                  base::OnceCallback<void(bool)> callback) override;
  void PlayClickRipple(content::WebContents* target) override;
  void CancelCursor(content::WebContents* target) override;
  bool IsTargetLocked(content::WebContents* target) override;
  void LockTarget(content::WebContents* target) override;
  void UnlockTarget(content::WebContents* target) override;
  bool GrantsUserActivation() override;

  // TabListInterfaceObserver:
  void OnTabAdded(TabListInterface& tab_list,
                  tabs::TabInterface* tab,
                  int index) override;
  void OnTabRemoved(TabListInterface& tab_list,
                    tabs::TabInterface* tab,
                    TabRemovedReason removed_reason) override;
  void OnActiveTabChanged(TabListInterface& tab_list,
                          tabs::TabInterface* tab) override;
  void OnWebContentsReplaced(TabListInterface& tab_list,
                             tabs::TabInterface* tab,
                             content::WebContents* old_contents,
                             content::WebContents* new_contents) override;
  void OnTabListDestroyed(TabListInterface& tab_list) override;

 private:
  class PermissionWatcher;
  class ModalDialogWatcher;
  class DownloadWatcher;

  void OnPermissionPrompt(content::WebContents* contents);
  void OnModalDialog(content::WebContents* contents);
  void TrackTab(tabs::TabInterface* tab, std::string opened_by);
  void UpdateTabControl();
  void ApplyTabControl(content::WebContents* contents);
  void NotifyChanged();
  std::map<std::string, std::string> SnapshotTabUrls() const;

  const raw_ptr<DaoAgentSpaceService> service_;
  const std::string id_;
  const std::string name_;
  const DaoToolClient client_;
  const std::string owner_label_;
  base::WeakPtr<BrowserWindowInterface> browser_;
  std::unique_ptr<DaoDevToolsClient> devtools_client_;
  std::unique_ptr<DaoBrowserAutomationSession> session_;
  std::unique_ptr<DaoBrowserToolExecutor> executor_;

  DaoAgentSpaceState state_ = DaoAgentSpaceState::kRunning;
  DaoAgentSpaceHandoffReason handoff_reason_ =
      DaoAgentSpaceHandoffReason::kNone;
  std::string handoff_message_;
  std::string status_text_;
  std::string summary_;
  base::Time created_at_;
  base::TimeTicks last_activity_;
  std::deque<DaoAgentSpaceEvent> events_;
  size_t first_event_serial_ = 0;
  std::optional<gfx::PointF> cursor_;
  std::optional<DaoAgentSpaceDialog> pending_dialog_;
  base::ListValue dialog_reports_;
  bool has_been_shown_ = false;
  bool stopped_for_inactivity_ = false;
  bool uses_logins_ = true;

  // Tab id -> "agent" or "user".
  std::map<std::string, std::string> opened_by_;
  std::set<std::string, std::less<>> claimed_user_tabs_;
  std::map<std::string, std::string, std::less<>> ref_targets_;
  base::ListValue closed_popup_reports_;
  // Tab id -> URL when the user took control.
  std::map<std::string, std::string> handoff_snapshot_;
  std::map<tabs::TabHandle, base::ScopedClosureRunner> keepalive_;

  std::map<tabs::TabHandle, std::unique_ptr<PermissionWatcher>>
      permission_watchers_;
  std::map<tabs::TabHandle, std::unique_ptr<ModalDialogWatcher>>
      modal_dialog_watchers_;
  std::vector<std::unique_ptr<DownloadWatcher>> downloads_;
  // Downloads of a space restored from an earlier session.
  base::ListValue restored_downloads_;

  base::ScopedObservation<TabListInterface, TabListInterfaceObserver>
      tab_list_observation_{this};

  base::WeakPtrFactory<DaoAgentSpace> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SPACE_H_
