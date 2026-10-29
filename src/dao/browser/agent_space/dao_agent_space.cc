// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent_space/dao_agent_space.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"
#include "chrome/browser/tab_list/constants.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_manager.h"
#include "chrome/browser/ui/exclusive_access/fullscreen_controller.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/download/public/common/download_item.h"
#include "components/javascript_dialogs/tab_modal_dialog_manager.h"
#include "components/permissions/permission_request_manager.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/agent_space/dao_agent_space_service.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_tool_executor.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "dao/browser/ui/views/dao_agent_cursor_view.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "ui/base/base_window.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "dao/browser/ui/views/dao_native_util_mac.h"
#endif

namespace dao {
namespace {

constexpr size_t kMaxEvents = 200;
constexpr size_t kMaxSerializedEvents = 40;
constexpr size_t kMaxDialogReports = 20;
constexpr size_t kMaxDownloads = 50;
constexpr size_t kMaxRememberedRefs = 500;
constexpr size_t kMaxTargetNameChars = 60;
constexpr size_t kMaxDialogMessageChars = 500;
constexpr gfx::Size kSpaceViewportSize(1280, 800);

std::string_view DialogTypeToString(content::JavaScriptDialogType type) {
  switch (type) {
    case content::JAVASCRIPT_DIALOG_TYPE_ALERT:
      return "alert";
    case content::JAVASCRIPT_DIALOG_TYPE_CONFIRM:
      return "confirm";
    case content::JAVASCRIPT_DIALOG_TYPE_PROMPT:
      return "prompt";
  }
  return "alert";
}

std::string TruncateDialogText(const std::u16string& text) {
  return base::UTF16ToUTF8(text.size() > kMaxDialogMessageChars
                               ? text.substr(0, kMaxDialogMessageChars)
                               : text);
}

double ToJsTime(base::Time time) {
  return time.InMillisecondsFSinceUnixEpochIgnoringNull();
}

std::vector<tabs::TabInterface*> GetTabs(BrowserWindowInterface* browser) {
  TabListInterface* tab_list =
      browser ? TabListInterface::From(browser) : nullptr;
  return tab_list ? tab_list->GetAllTabs() : std::vector<tabs::TabInterface*>();
}

// A never-shown macOS window keeps its compositor suspended, which would
// freeze the active tab of a hidden task window, including while the user
// watches it from the sidebar. An idle compositor only ticks on damage.
void KeepCompositorActive(BrowserWindowInterface* browser, bool active) {
#if BUILDFLAG(IS_MAC)
  ui::BaseWindow* window = browser ? browser->GetWindow() : nullptr;
  if (window) {
    dao::SetCompositorActiveWhileHidden(window->GetNativeWindow(), active);
  }
#endif
}

BrowserView* GetVisibleBrowserView(content::WebContents* target) {
  BrowserWindowInterface* browser =
      target
          ? GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(target)
          : nullptr;
  return browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
}

}  // namespace

std::string_view DaoAgentSpaceStateToString(DaoAgentSpaceState state) {
  switch (state) {
    case DaoAgentSpaceState::kRunning:
      return "running";
    case DaoAgentSpaceState::kUserInControl:
      return "user_in_control";
    case DaoAgentSpaceState::kFinished:
      return "finished";
    case DaoAgentSpaceState::kStopped:
      return "stopped";
  }
  return "stopped";
}

std::string_view DaoAgentSpaceHandoffReasonToString(
    DaoAgentSpaceHandoffReason reason) {
  switch (reason) {
    case DaoAgentSpaceHandoffReason::kNone:
      return "";
    case DaoAgentSpaceHandoffReason::kHelpRequested:
      return "help_requested";
    case DaoAgentSpaceHandoffReason::kPermissionPrompt:
      return "permission_prompt";
    case DaoAgentSpaceHandoffReason::kFileChooser:
      return "file_chooser";
    case DaoAgentSpaceHandoffReason::kDeviceChooser:
      return "device_chooser";
    case DaoAgentSpaceHandoffReason::kBrowserDialog:
      return "browser_dialog";
    case DaoAgentSpaceHandoffReason::kManualTakeover:
      return "manual_takeover";
  }
  return "";
}

// Hands the space to the user as soon as a page asks for a permission, since
// the prompt is invisible while the task window is off screen.
class DaoAgentSpace::PermissionWatcher
    : public permissions::PermissionRequestManager::Observer {
 public:
  PermissionWatcher(DaoAgentSpace* space, content::WebContents* contents)
      : space_(space), contents_(contents->GetWeakPtr()) {
    auto* manager =
        permissions::PermissionRequestManager::FromWebContents(contents);
    if (manager) {
      observation_.Observe(manager);
    }
  }
  ~PermissionWatcher() override = default;

  // permissions::PermissionRequestManager::Observer:
  void OnPromptAdded() override { Report(); }
  void OnPromptCreationFailedHiddenTab() override { Report(); }
  void OnPromptRecreateViewFailed() override { Report(); }
  void OnPermissionRequestManagerDestructed() override { observation_.Reset(); }

 private:
  void Report() {
    if (contents_) {
      space_->OnPermissionPrompt(contents_.get());
    }
  }

  const raw_ptr<DaoAgentSpace> space_;
  base::WeakPtr<content::WebContents> contents_;
  base::ScopedObservation<permissions::PermissionRequestManager,
                          permissions::PermissionRequestManager::Observer>
      observation_{this};
};

// Tab-modal browser dialogs, such as HTTP sign-in, security keys and
// certificate pickers, need the user. They stay open and show once the user
// opens the window.
class DaoAgentSpace::ModalDialogWatcher
    : public web_modal::WebContentsModalDialogManager::Observer {
 public:
  ModalDialogWatcher(DaoAgentSpace* space, content::WebContents* contents)
      : space_(space), contents_(contents->GetWeakPtr()) {
    if (auto* manager =
            web_modal::WebContentsModalDialogManager::FromWebContents(
                contents)) {
      observation_.Observe(manager);
    }
  }
  ~ModalDialogWatcher() override = default;

  // web_modal::WebContentsModalDialogManager::Observer:
  void OnWillShow() override {
    if (contents_) {
      space_->OnModalDialog(contents_.get());
    }
  }

 private:
  const raw_ptr<DaoAgentSpace> space_;
  base::WeakPtr<content::WebContents> contents_;
  base::ScopedObservation<web_modal::WebContentsModalDialogManager,
                          web_modal::WebContentsModalDialogManager::Observer>
      observation_{this};
};

// Follows one download from a page in the space so the agent can find the
// file and the timeline shows when it finished.
class DaoAgentSpace::DownloadWatcher : public download::DownloadItem::Observer {
 public:
  DownloadWatcher(DaoAgentSpace* space, download::DownloadItem* item)
      : space_(space), item_(item), url_(item->GetURL().spec()) {
    observation_.Observe(item);
    Update();
    event_serial_ = space_->AddEvent("download", file_name_);
  }
  ~DownloadWatcher() override = default;

  base::DictValue ToDict() const {
    return base::DictValue()
        .Set("file_name", file_name_)
        .Set("path", path_)
        .Set("url", url_)
        .Set("state", state_);
  }

  // download::DownloadItem::Observer:
  void OnDownloadUpdated(download::DownloadItem* item) override {
    const std::string previous = state_;
    const std::string previous_name = file_name_;
    Update();
    if (file_name_ != previous_name) {
      space_->SetEventLabel(event_serial_, file_name_);
    }
    if (state_ != previous && state_ != "in_progress") {
      space_->CompleteEvent(event_serial_, state_ == "complete",
                            state_ == "complete" ? std::string() : state_);
    }
    space_->NotifyChanged();
  }
  void OnDownloadDestroyed(download::DownloadItem* item) override {
    observation_.Reset();
    item_ = nullptr;
  }

 private:
  void Update() {
    if (!item_) {
      return;
    }
    const base::FilePath path = item_->GetTargetFilePath();
    if (!path.empty()) {
      path_ = path.AsUTF8Unsafe();
      file_name_ = path.BaseName().AsUTF8Unsafe();
    } else if (file_name_.empty()) {
      file_name_ = item_->GetFileNameToReportUser().AsUTF8Unsafe();
    }
    if (item_->IsDangerous() || item_->IsInsecure()) {
      // Only the user can keep a file the browser flagged.
      state_ = "needs_review";
      return;
    }
    switch (item_->GetState()) {
      case download::DownloadItem::IN_PROGRESS:
        state_ = "in_progress";
        break;
      case download::DownloadItem::COMPLETE:
        state_ = "complete";
        break;
      case download::DownloadItem::CANCELLED:
        state_ = "cancelled";
        break;
      case download::DownloadItem::INTERRUPTED:
        state_ = "interrupted";
        break;
      case download::DownloadItem::MAX_DOWNLOAD_STATE:
        break;
    }
  }

  const raw_ptr<DaoAgentSpace> space_;
  raw_ptr<download::DownloadItem> item_;
  const std::string url_;
  std::string file_name_;
  std::string path_;
  std::string state_ = "in_progress";
  size_t event_serial_ = 0;
  base::ScopedObservation<download::DownloadItem,
                          download::DownloadItem::Observer>
      observation_{this};
};

DaoAgentSpace::DaoAgentSpace(DaoAgentSpaceService* service,
                             std::string id,
                             std::string name,
                             DaoToolClient client,
                             std::string owner_label,
                             BrowserWindowInterface* browser)
    : service_(service),
      id_(std::move(id)),
      name_(std::move(name)),
      client_(client),
      owner_label_(std::move(owner_label)),
      browser_(browser ? browser->GetWeakPtr() : nullptr),
      devtools_client_(std::make_unique<DaoDevToolsClient>()),
      created_at_(base::Time::Now()),
      last_activity_(base::TimeTicks::Now()) {
  executor_ =
      std::make_unique<DaoBrowserToolExecutor>(devtools_client_.get(), this);
  DaoAgentSpaceService::RegisterSpaceBrowser(browser, this);
}

DaoAgentSpace::~DaoAgentSpace() {
  DetachWindow();
  if (browser_) {
    DaoAgentSpaceService::UnregisterSpaceBrowser(browser_.get());
  }
}

BrowserWindowInterface* DaoAgentSpace::browser() const {
  return browser_.get();
}

bool DaoAgentSpace::IsWindowVisible() const {
  return browser_ && browser_->GetWindow() &&
         browser_->GetWindow()->IsVisible();
}

content::WebContents* DaoAgentSpace::GetCurrentContents() const {
  if (!session_) {
    return nullptr;
  }
  auto target = session_->ResolveTarget();
  return target.has_value() ? target.value() : nullptr;
}

content::WebContents* DaoAgentSpace::GetActiveContents() const {
  TabListInterface* tab_list =
      browser_ ? TabListInterface::From(browser_.get()) : nullptr;
  tabs::TabInterface* tab = tab_list ? tab_list->GetActiveTab() : nullptr;
  return tab ? tab->GetContents() : nullptr;
}

int DaoAgentSpace::GetTabCount() const {
  TabListInterface* tab_list =
      browser_ ? TabListInterface::From(browser_.get()) : nullptr;
  return tab_list ? tab_list->GetTabCount() : 0;
}

bool DaoAgentSpace::Initialize(const GURL& url) {
  TabListInterface* tab_list =
      browser_ ? TabListInterface::From(browser_.get()) : nullptr;
  if (!tab_list) {
    return false;
  }
  tab_list_observation_.Observe(tab_list);
  tab_list->OpenTab(url, tab_list::kNoTabIndex, /*foreground=*/true);
  tabs::TabInterface* first = tab_list->GetActiveTab();
  if (!first || !browser_) {
    return false;
  }
  for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
    if (!opened_by_.contains(GetOrCreateSidebarTabId(tab->GetContents()))) {
      TrackTab(tab, "agent");
    }
  }
  session_ = std::make_unique<DaoBrowserAutomationSession>(
      browser_.get(), first->GetContents(),
      DaoBrowserAutomationSession::TargetPolicy::kAgentSpace);
  UpdateTabControl();
  return session_->ResolveTarget().has_value();
}

void DaoAgentSpace::EnsureSessionTarget() {
  if (!session_ || session_->ResolveTarget().has_value()) {
    return;
  }
  if (content::WebContents* active = GetActiveContents()) {
    session_->SetTarget(active);
  }
}

base::DictValue DaoAgentSpace::ToDict(bool include_events) const {
  base::DictValue dict;
  dict.Set("space_id", id_);
  dict.Set("name", name_);
  dict.Set("state", DaoAgentSpaceStateToString(state_));
  dict.Set("owner", owner_label_);
  dict.Set("status", status_text_);
  dict.Set("created_at", ToJsTime(created_at_));
  dict.Set("tab_count", GetTabCount());
  dict.Set("logins", uses_logins_);
  dict.Set("window_visible", IsWindowVisible());
  if (state_ == DaoAgentSpaceState::kUserInControl) {
    dict.Set("handoff_reason",
             DaoAgentSpaceHandoffReasonToString(handoff_reason_));
    dict.Set("handoff_message", handoff_message_);
  }
  if (!summary_.empty()) {
    dict.Set("summary", summary_);
  }
  if (!downloads_.empty()) {
    dict.Set("downloads", SerializeDownloads());
  }
  if (pending_dialog_) {
    dict.Set("dialog", base::DictValue()
                           .Set("tab_id", pending_dialog_->tab_id)
                           .Set("type", pending_dialog_->type)
                           .Set("message", pending_dialog_->message));
  }
  if (stopped_for_inactivity_) {
    dict.Set("stop_reason", "inactive");
  }
  if (content::WebContents* contents = GetCurrentContents()) {
    dict.Set("tab_id", GetOrCreateSidebarTabId(contents));
    dict.Set("url", contents->GetVisibleURL().spec());
    dict.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
  }
  if (include_events) {
    if (cursor_) {
      dict.Set("cursor",
               base::DictValue().Set("x", cursor_->x()).Set("y", cursor_->y()));
    }
    base::ListValue events;
    const size_t start = events_.size() > kMaxSerializedEvents
                             ? events_.size() - kMaxSerializedEvents
                             : 0;
    for (size_t i = start; i < events_.size(); ++i) {
      const DaoAgentSpaceEvent& event = events_[i];
      base::DictValue item;
      item.Set("time", ToJsTime(event.time));
      item.Set("tool", event.tool);
      item.Set("label", event.label);
      if (!event.target.empty()) {
        item.Set("target", event.target);
      }
      item.Set("url", event.url);
      item.Set("pending", event.pending);
      item.Set("ok", event.ok);
      if (!event.error_code.empty()) {
        item.Set("error_code", event.error_code);
      }
      events.Append(std::move(item));
    }
    dict.Set("events", std::move(events));
  }
  return dict;
}

base::DictValue DaoAgentSpace::ToRecord() const {
  base::DictValue record;
  record.Set("space_id", id_);
  record.Set("name", name_);
  record.Set("client", static_cast<int>(client_));
  record.Set("owner", owner_label_);
  record.Set("state", DaoAgentSpaceStateToString(state_));
  record.Set("status", status_text_);
  record.Set("summary", summary_);
  record.Set("created_at", ToJsTime(created_at_));
  record.Set("inactive", stopped_for_inactivity_);
  record.Set("downloads", SerializeDownloads());
  return record;
}

void DaoAgentSpace::RestoreRecord(const base::DictValue& record) {
  const std::string* state = record.FindString("state");
  state_ = state && *state == "finished" ? DaoAgentSpaceState::kFinished
                                         : DaoAgentSpaceState::kStopped;
  if (const std::string* status = record.FindString("status")) {
    status_text_ = *status;
  }
  if (const std::string* summary = record.FindString("summary")) {
    summary_ = *summary;
  }
  if (std::optional<double> created = record.FindDouble("created_at")) {
    created_at_ = base::Time::FromMillisecondsSinceUnixEpoch(*created);
  }
  stopped_for_inactivity_ = record.FindBool("inactive").value_or(false);
  if (const base::ListValue* downloads = record.FindList("downloads")) {
    restored_downloads_ = downloads->Clone();
  }
}

base::ListValue DaoAgentSpace::SerializeTabs() const {
  base::ListValue list;
  TabListInterface* tab_list =
      browser_ ? TabListInterface::From(browser_.get()) : nullptr;
  tabs::TabInterface* active = tab_list ? tab_list->GetActiveTab() : nullptr;
  for (tabs::TabInterface* tab : GetTabs(browser_.get())) {
    content::WebContents* contents = tab->GetContents();
    const std::string tab_id = GetOrCreateSidebarTabId(contents);
    auto opened_by = opened_by_.find(tab_id);
    list.Append(base::DictValue()
                    .Set("tab_id", tab_id)
                    .Set("url", contents->GetVisibleURL().spec())
                    .Set("title", base::UTF16ToUTF8(contents->GetTitle()))
                    .Set("active", tab == active)
                    .Set("opened_by", opened_by == opened_by_.end()
                                          ? std::string("agent")
                                          : opened_by->second));
  }
  return list;
}

void DaoAgentSpace::AnnotateTabs(base::Value& list_tabs_data) const {
  base::DictValue* data = list_tabs_data.GetIfDict();
  base::ListValue* tabs = data ? data->FindList("tabs") : nullptr;
  if (!tabs) {
    return;
  }
  for (base::Value& tab : *tabs) {
    base::DictValue* dict = tab.GetIfDict();
    const std::string* tab_id = dict ? dict->FindString("tab_id") : nullptr;
    if (!tab_id) {
      continue;
    }
    auto opened_by = opened_by_.find(*tab_id);
    dict->Set("opened_by", opened_by == opened_by_.end() ? std::string("agent")
                                                         : opened_by->second);
  }
}

bool DaoAgentSpace::IsUnclaimedUserTab(std::string_view tab_id) const {
  auto opened_by = opened_by_.find(std::string(tab_id));
  return opened_by != opened_by_.end() && opened_by->second == "user" &&
         !claimed_user_tabs_.contains(tab_id);
}

void DaoAgentSpace::ClaimTab(std::string_view tab_id) {
  if (IsUnclaimedUserTab(tab_id)) {
    claimed_user_tabs_.emplace(tab_id);
  }
}

base::ListValue DaoAgentSpace::TakeClosedPopupReports() {
  return std::exchange(closed_popup_reports_, base::ListValue());
}

void DaoAgentSpace::SetStatus(std::string text) {
  status_text_ = std::move(text);
  TouchActivity();
  NotifyChanged();
}

void DaoAgentSpace::TouchActivity() {
  last_activity_ = base::TimeTicks::Now();
}

size_t DaoAgentSpace::AddEvent(std::string tool, std::string label) {
  DaoAgentSpaceEvent event;
  event.time = base::Time::Now();
  event.tool = std::move(tool);
  event.label = std::move(label);
  if (content::WebContents* contents = GetCurrentContents()) {
    event.url = contents->GetVisibleURL().spec();
  }
  const size_t serial = first_event_serial_ + events_.size();
  events_.push_back(std::move(event));
  while (events_.size() > kMaxEvents) {
    events_.pop_front();
    ++first_event_serial_;
  }
  NotifyChanged();
  return serial;
}

void DaoAgentSpace::CompleteEvent(size_t serial,
                                  bool ok,
                                  std::string error_code) {
  if (serial < first_event_serial_ ||
      serial - first_event_serial_ >= events_.size()) {
    return;
  }
  DaoAgentSpaceEvent& event = events_[serial - first_event_serial_];
  event.pending = false;
  event.ok = ok;
  event.error_code = std::move(error_code);
  if (content::WebContents* contents = GetCurrentContents()) {
    event.url = contents->GetVisibleURL().spec();
  }
  NotifyChanged();
}

void DaoAgentSpace::SetEventLabel(size_t serial, std::string label) {
  if (serial < first_event_serial_ ||
      serial - first_event_serial_ >= events_.size()) {
    return;
  }
  events_[serial - first_event_serial_].label = std::move(label);
  NotifyChanged();
}

void DaoAgentSpace::SetEventTarget(size_t serial, std::string target) {
  if (serial < first_event_serial_ ||
      serial - first_event_serial_ >= events_.size()) {
    return;
  }
  events_[serial - first_event_serial_].target = std::move(target);
}

void DaoAgentSpace::RememberRefs(const base::Value& query_result) {
  const base::DictValue* data = query_result.GetIfDict();
  const base::ListValue* matches = data ? data->FindList("matches") : nullptr;
  if (!matches) {
    return;
  }
  if (ref_targets_.size() > kMaxRememberedRefs) {
    ref_targets_.clear();
  }
  for (const base::Value& match : *matches) {
    const base::DictValue* dict = match.GetIfDict();
    const std::string* ref_id = dict ? dict->FindString("ref_id") : nullptr;
    if (!ref_id) {
      continue;
    }
    const std::string* role = dict->FindString("role");
    const std::string* name = dict->FindString("name");
    std::u16string short_name = base::UTF8ToUTF16(name ? *name : "");
    if (short_name.size() > kMaxTargetNameChars) {
      short_name = short_name.substr(0, kMaxTargetNameChars) + u"…";
    }
    std::string target = role ? *role : std::string();
    if (!short_name.empty()) {
      target += (target.empty() ? "\"" : " \"") +
                base::UTF16ToUTF8(short_name) + "\"";
    }
    ref_targets_[*ref_id] = std::move(target);
  }
}

std::string DaoAgentSpace::DescribeRef(std::string_view ref_id) const {
  auto it = ref_targets_.find(ref_id);
  return it == ref_targets_.end() ? std::string() : it->second;
}

void DaoAgentSpace::HandOffToUser(DaoAgentSpaceHandoffReason reason,
                                  std::string message) {
  if (is_closed()) {
    return;
  }
  const bool was_running = state_ == DaoAgentSpaceState::kRunning;
  state_ = DaoAgentSpaceState::kUserInControl;
  // Keep the strongest reason when a second request arrives mid-handoff.
  if (was_running || reason != DaoAgentSpaceHandoffReason::kManualTakeover) {
    handoff_reason_ = reason;
    handoff_message_ = std::move(message);
  }
  if (was_running) {
    handoff_snapshot_ = SnapshotTabUrls();
  }
  UpdateTabControl();
  base::WeakPtr<DaoAgentSpace> weak_this = weak_factory_.GetWeakPtr();
  if (was_running && executor_) {
    executor_->CancelAll(MakeDaoToolError(
        DaoToolErrorCode::kSpaceUserInControl,
        "The user took control of this space while the tool was running."));
  }
  if (weak_this) {
    NotifyChanged();
  }
}

base::DictValue DaoAgentSpace::ReturnToAgent() {
  base::DictValue changes;
  if (state_ != DaoAgentSpaceState::kUserInControl) {
    return changes;
  }
  const std::map<std::string, std::string> now = SnapshotTabUrls();
  base::ListValue opened;
  base::ListValue navigated;
  base::ListValue closed;
  for (const auto& [tab_id, url] : now) {
    auto before = handoff_snapshot_.find(tab_id);
    if (before == handoff_snapshot_.end()) {
      opened.Append(base::DictValue().Set("tab_id", tab_id).Set("url", url));
    } else if (before->second != url) {
      navigated.Append(base::DictValue()
                           .Set("tab_id", tab_id)
                           .Set("from", before->second)
                           .Set("to", url));
    }
  }
  for (const auto& [tab_id, url] : handoff_snapshot_) {
    if (!now.contains(tab_id)) {
      closed.Append(tab_id);
    }
  }
  changes.Set("opened_tabs", std::move(opened));
  changes.Set("navigated_tabs", std::move(navigated));
  changes.Set("closed_tab_ids", std::move(closed));
  if (content::WebContents* active = GetActiveContents()) {
    changes.Set("active_tab_id", GetOrCreateSidebarTabId(active));
  }

  state_ = DaoAgentSpaceState::kRunning;
  handoff_reason_ = DaoAgentSpaceHandoffReason::kNone;
  handoff_message_.clear();
  handoff_snapshot_.clear();
  TouchActivity();
  UpdateTabControl();
  NotifyChanged();
  return changes;
}

void DaoAgentSpace::MarkClosed(DaoAgentSpaceState state, std::string summary) {
  if (is_closed()) {
    return;
  }
  state_ = state;
  summary_ = std::move(summary);
  handoff_reason_ = DaoAgentSpaceHandoffReason::kNone;
  handoff_message_.clear();
  cursor_.reset();
  for (DaoAgentSpaceEvent& event : events_) {
    event.pending = false;
  }
  DetachWindow();
  NotifyChanged();
}

void DaoAgentSpace::DetachWindow() {
  hide_window_callback_.Cancel();
  if (executor_) {
    std::unique_ptr<DaoBrowserToolExecutor> executor = std::move(executor_);
    executor->CancelAll(MakeDaoToolError(DaoToolErrorCode::kSpaceClosed,
                                         "This Agent Space was closed."));
    executor->ClearSessionState(session_.get());
  }
  session_.reset();
  if (devtools_client_) {
    devtools_client_->Detach();
  }
  keepalive_.clear();
  permission_watchers_.clear();
  modal_dialog_watchers_.clear();
  tab_list_observation_.Reset();
  for (tabs::TabInterface* tab : GetTabs(browser_.get())) {
    DaoAgentLockTabHelper::UnlockContents(tab->GetContents());
  }
}

void DaoAgentSpace::OnDialogOpened(content::WebContents* contents,
                                   content::JavaScriptDialogType type,
                                   const std::u16string& message,
                                   const std::u16string& default_prompt) {
  if (is_closed() || !contents) {
    return;
  }
  const std::string tab_id = GetOrCreateSidebarTabId(contents);
  const std::string type_name(DialogTypeToString(type));
  const std::string text = TruncateDialogText(message);
  CompleteEvent(AddEvent("dialog", text), true, std::string());
  if (type == content::JAVASCRIPT_DIALOG_TYPE_ALERT) {
    // The page already continued; tell the agent with its next result.
    dialog_reports_.Append(base::DictValue()
                               .Set("tab_id", tab_id)
                               .Set("type", type_name)
                               .Set("message", text)
                               .Set("handled", "accepted"));
    while (dialog_reports_.size() > kMaxDialogReports) {
      dialog_reports_.erase(dialog_reports_.begin());
    }
    return;
  }
  pending_dialog_ = DaoAgentSpaceDialog{tab_id, type_name, text,
                                        TruncateDialogText(default_prompt)};
  NotifyChanged();
  service_->OnSpaceDialogOpened(this);
}

void DaoAgentSpace::OnDialogClosed(content::WebContents* contents) {
  if (!pending_dialog_ || !contents ||
      GetSidebarTabId(contents) != pending_dialog_->tab_id) {
    return;
  }
  pending_dialog_.reset();
  NotifyChanged();
}

bool DaoAgentSpace::AnswerDialog(bool accept, const std::string& prompt_text) {
  if (!pending_dialog_) {
    return false;
  }
  content::WebContents* contents = FindTab(pending_dialog_->tab_id);
  auto* manager =
      contents
          ? javascript_dialogs::TabModalDialogManager::FromWebContents(contents)
          : nullptr;
  const std::u16string input = base::UTF8ToUTF16(prompt_text);
  if (!manager || !manager->HandleJavaScriptDialog(contents, accept, &input)) {
    // The page went away with its dialog.
    pending_dialog_.reset();
    NotifyChanged();
    return false;
  }
  // Closing the dialog reports back through OnDialogClosed().
  pending_dialog_.reset();
  NotifyChanged();
  return true;
}

void DaoAgentSpace::TrackDownload(download::DownloadItem* item) {
  if (is_closed() || !item) {
    return;
  }
  downloads_.push_back(std::make_unique<DownloadWatcher>(this, item));
  if (downloads_.size() > kMaxDownloads) {
    downloads_.erase(downloads_.begin());
  }
  NotifyChanged();
}

base::ListValue DaoAgentSpace::SerializeDownloads() const {
  base::ListValue list = restored_downloads_.Clone();
  for (const auto& download : downloads_) {
    list.Append(download->ToDict());
  }
  return list;
}

base::ListValue DaoAgentSpace::TakeDialogReports() {
  return std::exchange(dialog_reports_, base::ListValue());
}

void DaoAgentSpace::ShowWindow() {
  hide_window_callback_.Cancel();
  if (!browser_ || !browser_->GetWindow()) {
    return;
  }
  ui::BaseWindow* window = browser_->GetWindow();
  if (!has_been_shown_) {
    has_been_shown_ = true;
    const gfx::Rect work_area =
        display::Screen::Get()->GetPrimaryDisplay().work_area();
    gfx::Rect bounds(kSpaceViewportSize.width() + 240,
                     kSpaceViewportSize.height() + 60);
    bounds.Intersect(gfx::Rect(work_area.size()));
    bounds.set_origin(work_area.CenterPoint() -
                      gfx::Vector2d(bounds.width() / 2, bounds.height() / 2));
    window->SetBounds(bounds);
  }
  window->Show();
  window->Activate();
  NotifyChanged();
}

void DaoAgentSpace::HideWindow() {
  if (!browser_ || !browser_->GetWindow() || is_closed()) {
    return;
  }
  hide_window_callback_.Reset(
      base::BindOnce(&DaoAgentSpace::HideWindowAfterFullscreenExit,
                     weak_factory_.GetWeakPtr()));
  auto hide_window = hide_window_callback_.callback();
  auto weak_this = weak_factory_.GetWeakPtr();
  auto* fullscreen_controller =
      ExclusiveAccessManager::From(browser_.get())->fullscreen_controller();
  if (browser_->GetWindow()->IsFullscreen()) {
    // Hiding a fullscreen macOS window leaves an empty fullscreen Space.
    fullscreen_controller->ToggleBrowserFullscreenMode(/*user_initiated=*/false);
  }
  if (weak_this && browser_ && !is_closed()) {
    fullscreen_controller->RunOrDeferUntilTransitionIsComplete(
        std::move(hide_window));
  }
}

void DaoAgentSpace::HideWindowAfterFullscreenExit() {
  if (!browser_ || !browser_->GetWindow()) {
    return;
  }
  if (browser_->GetWindow()->IsFullscreen()) {
    return;
  }
  browser_->GetWindow()->Hide();
  NotifyChanged();
}

content::WebContents* DaoAgentSpace::FindTab(std::string_view tab_id) const {
  for (tabs::TabInterface* tab : GetTabs(browser_.get())) {
    if (GetSidebarTabId(tab->GetContents()) == tab_id) {
      return tab->GetContents();
    }
  }
  return nullptr;
}

void DaoAgentSpace::ActivateTab(std::string_view tab_id) {
  TabListInterface* tab_list =
      browser_ ? TabListInterface::From(browser_.get()) : nullptr;
  if (!tab_list) {
    return;
  }
  for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
    if (GetSidebarTabId(tab->GetContents()) == tab_id) {
      tab_list->ActivateTab(tab->GetHandle());
      return;
    }
  }
}

void DaoAgentSpace::MoveCursor(content::WebContents* target,
                               double x,
                               double y,
                               std::optional<gfx::RectF> target_bounds,
                               base::OnceCallback<void(bool)> callback) {
  // Remember where the agent points, relative to the viewport, so the peek
  // views can draw it even while the window is off screen.
  if (target) {
    gfx::Size viewport =
        target->GetRenderWidgetHostView()
            ? target->GetRenderWidgetHostView()->GetViewBounds().size()
            : target->GetContainerBounds().size();
    if (viewport.IsEmpty()) {
      viewport = kSpaceViewportSize;
    }
    cursor_ = gfx::PointF(std::clamp(x / viewport.width(), 0.0, 1.0),
                          std::clamp(y / viewport.height(), 0.0, 1.0));
    NotifyChanged();
  }

  BrowserView* browser_view = GetVisibleBrowserView(target);
  DaoAgentCursorView* cursor_view =
      browser_view ? browser_view->dao_agent_cursor() : nullptr;
  if (!target || !cursor_view || !CanAnimateAgentCursorForTarget(target)) {
    std::move(callback).Run(false);
    return;
  }
  AnimateAgentCursorInViewport(browser_view, target, x, y,
                               std::move(target_bounds), std::move(callback));
}

void DaoAgentSpace::PlayClickRipple(content::WebContents* target) {
  BrowserView* browser_view = GetVisibleBrowserView(target);
  if (browser_view && browser_view->dao_agent_cursor() &&
      CanAnimateAgentCursorForTarget(target)) {
    browser_view->dao_agent_cursor()->PlayClickRipple();
  }
}

void DaoAgentSpace::CancelCursor(content::WebContents* target) {
  BrowserView* browser_view = GetVisibleBrowserView(target);
  if (browser_view && browser_view->dao_agent_cursor()) {
    browser_view->dao_agent_cursor()->CancelAnimation();
  }
}

bool DaoAgentSpace::IsTargetLocked(content::WebContents* target) {
  // The space locks every tab while the agent runs, so tools never take or
  // release a lock of their own.
  return true;
}

void DaoAgentSpace::LockTarget(content::WebContents* target) {}

void DaoAgentSpace::UnlockTarget(content::WebContents* target) {}

bool DaoAgentSpace::GrantsUserActivation() {
  // The agent clicks on the user's behalf in its own window; popups it opens
  // stay in that window.
  return true;
}

void DaoAgentSpace::OnTabAdded(TabListInterface& tab_list,
                               tabs::TabInterface* tab,
                               int index) {
  TrackTab(tab,
           state_ == DaoAgentSpaceState::kUserInControl ? "user" : "agent");
  // Pages can open popups faster than the agent closes them. Past the limit,
  // new tabs close and the agent hears about them with its next result.
  if (state_ == DaoAgentSpaceState::kRunning &&
      tab_list.GetTabCount() > DaoAgentSpaceService::kMaxTabsPerSpace) {
    content::WebContents* contents = tab->GetContents();
    closed_popup_reports_.Append(
        base::DictValue()
            .Set("url", contents->GetVisibleURL().spec())
            .Set("reason", "tab_limit"));
    CompleteEvent(AddEvent("close_tab", std::string()), false,
                  "SPACE_LIMIT_REACHED");
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<DaoAgentSpace> space, tabs::TabHandle handle) {
              TabListInterface* tabs =
                  space && space->browser()
                      ? TabListInterface::From(space->browser())
                      : nullptr;
              if (tabs && handle.Get()) {
                tabs->CloseTab(handle);
              }
            },
            weak_factory_.GetWeakPtr(), tab->GetHandle()));
  }
  NotifyChanged();
}

void DaoAgentSpace::OnTabRemoved(TabListInterface& tab_list,
                                 tabs::TabInterface* tab,
                                 TabRemovedReason removed_reason) {
  if (!tab) {
    return;
  }
  keepalive_.erase(tab->GetHandle());
  permission_watchers_.erase(tab->GetHandle());
  modal_dialog_watchers_.erase(tab->GetHandle());
  DaoAgentLockTabHelper::UnlockContents(tab->GetContents());
  NotifyChanged();
}

void DaoAgentSpace::OnActiveTabChanged(TabListInterface& tab_list,
                                       tabs::TabInterface* tab) {
  NotifyChanged();
}

void DaoAgentSpace::OnWebContentsReplaced(TabListInterface& tab_list,
                                          tabs::TabInterface* tab,
                                          content::WebContents* old_contents,
                                          content::WebContents* new_contents) {
  keepalive_.erase(tab->GetHandle());
  permission_watchers_.erase(tab->GetHandle());
  modal_dialog_watchers_.erase(tab->GetHandle());
  const std::string tab_id = GetOrCreateSidebarTabId(new_contents);
  TrackTab(tab, opened_by_.contains(tab_id) ? opened_by_[tab_id] : "agent");
}

void DaoAgentSpace::OnTabListDestroyed(TabListInterface& tab_list) {
  keepalive_.clear();
  permission_watchers_.clear();
  modal_dialog_watchers_.clear();
  tab_list_observation_.Reset();
}

void DaoAgentSpace::OnPermissionPrompt(content::WebContents* contents) {
  if (state_ != DaoAgentSpaceState::kRunning) {
    return;
  }
  service_->HandOffForPage(this, contents,
                           DaoAgentSpaceHandoffReason::kPermissionPrompt);
}

void DaoAgentSpace::OnModalDialog(content::WebContents* contents) {
  if (!IsWindowVisible()) {
    OnPageNeedsUser(contents, DaoAgentSpaceHandoffReason::kBrowserDialog);
  }
}

void DaoAgentSpace::OnPageNeedsUser(content::WebContents* contents,
                                    DaoAgentSpaceHandoffReason reason) {
  if (state_ == DaoAgentSpaceState::kRunning) {
    service_->HandOffForPage(this, contents, reason);
  }
}

void DaoAgentSpace::TrackTab(tabs::TabInterface* tab, std::string opened_by) {
  content::WebContents* contents = tab->GetContents();
  const std::string tab_id = GetOrCreateSidebarTabId(contents);
  if (!opened_by_.contains(tab_id)) {
    opened_by_[tab_id] = std::move(opened_by);
  }
  permission_watchers_[tab->GetHandle()] =
      std::make_unique<PermissionWatcher>(this, contents);
  modal_dialog_watchers_[tab->GetHandle()] =
      std::make_unique<ModalDialogWatcher>(this, contents);
  ApplyTabControl(contents);
}

void DaoAgentSpace::UpdateTabControl() {
  KeepCompositorActive(browser_.get(), !is_closed());
  for (tabs::TabInterface* tab : GetTabs(browser_.get())) {
    ApplyTabControl(tab->GetContents());
  }
}

void DaoAgentSpace::ApplyTabControl(content::WebContents* contents) {
  tabs::TabInterface* tab =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  if (!tab) {
    return;
  }
  if (state_ == DaoAgentSpaceState::kRunning) {
    DaoAgentLockTabHelper::LockContents(contents);
    // Hidden windows are throttled like background tabs. Holding a capturer
    // tells the renderer it is user-visible, so pages render and run timers
    // exactly as they would on screen.
    if (!keepalive_.contains(tab->GetHandle())) {
      keepalive_.emplace(tab->GetHandle(),
                         contents->IncrementCapturerCount(
                             kSpaceViewportSize, /*stay_hidden=*/false,
                             /*stay_awake=*/true, /*is_activity=*/false));
    }
    return;
  }
  DaoAgentLockTabHelper::UnlockContents(contents);
  keepalive_.erase(tab->GetHandle());
}

void DaoAgentSpace::NotifyChanged() {
  service_->NotifySpaceChanged();
}

std::map<std::string, std::string> DaoAgentSpace::SnapshotTabUrls() const {
  std::map<std::string, std::string> urls;
  for (tabs::TabInterface* tab : GetTabs(browser_.get())) {
    urls[GetOrCreateSidebarTabId(tab->GetContents())] =
        tab->GetContents()->GetVisibleURL().spec();
  }
  return urls;
}

}  // namespace dao
