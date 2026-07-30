// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_tab_tools.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/types/expected.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/tab_list/tab_list_interface_observer.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_target_policy.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "url/gurl.h"

namespace dao {
namespace {

struct ResolvedTab {
  tabs::TabHandle handle;
  int index = -1;
};

std::vector<content::WebContents*> GetTabContentsInOrder(
    TabListInterface* tab_list) {
  std::vector<content::WebContents*> contents;
  for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
    contents.push_back(tab->GetContents());
  }
  return contents;
}

DaoBrowserToolResult ErrorResult(DaoToolErrorCode code, std::string message) {
  DaoBrowserToolResult result;
  result.error = MakeDaoToolError(code, std::move(message));
  return result;
}

DaoToolTarget MakeTarget(content::WebContents* contents) {
  return {
      .tab_id = GetOrCreateSidebarTabId(contents),
      .url = contents ? contents->GetVisibleURL().spec() : std::string(),
  };
}

base::DictValue SerializeTab(tabs::TabInterface* tab,
                             int index,
                             tabs::TabInterface* active_tab) {
  content::WebContents* contents = tab->GetContents();
  return base::DictValue()
      .Set("tab_id", GetOrCreateSidebarTabId(contents))
      .Set("index", index)
      .Set("url", contents->GetURL().spec())
      .Set("title", base::UTF16ToUTF8(contents->GetTitle()))
      .Set("active", tab == active_tab);
}

base::expected<ResolvedTab, DaoToolError> ResolveSelectedTab(
    TabListInterface* tab_list,
    content::WebContents* default_target,
    const base::DictValue& arguments,
    bool require_selector) {
  if (const std::string* tab_id = arguments.FindString("tab_id")) {
    if (tab_id->empty()) {
      return base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kInvalidArgument, "Tab id must not be empty."));
    }
    std::vector<tabs::TabInterface*> all_tabs = tab_list->GetAllTabs();
    std::optional<ResolvedTab> selected;
    for (size_t i = 0; i < all_tabs.size(); ++i) {
      tabs::TabInterface* tab = all_tabs[i];
      if (GetOrCreateSidebarTabId(tab->GetContents()) == *tab_id) {
        if (selected.has_value()) {
          return base::unexpected(MakeDaoToolError(
              DaoToolErrorCode::kInvalidArgument,
              "Tab id is ambiguous in the authorized browser window."));
        }
        selected = ResolvedTab{.handle = tab->GetHandle(),
                               .index = static_cast<int>(i)};
      }
    }
    if (selected.has_value()) {
      return *selected;
    }
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kInvalidArgument,
        "Tab id is not present in the authorized browser window."));
  }

  if (arguments.contains("index")) {
    std::optional<int> index = arguments.FindInt("index");
    tabs::TabInterface* tab = index ? tab_list->GetTab(*index) : nullptr;
    if (!index || !tab) {
      return base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kInvalidArgument,
          "Tab index is invalid for the authorized browser window."));
    }
    return ResolvedTab{.handle = tab->GetHandle(), .index = *index};
  }

  if (require_selector) {
    return base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                         "At least one of tab_id or index is required."));
  }

  std::vector<tabs::TabInterface*> all_tabs = tab_list->GetAllTabs();
  for (size_t i = 0; i < all_tabs.size(); ++i) {
    tabs::TabInterface* tab = all_tabs[i];
    if (tab->GetContents() == default_target) {
      return ResolvedTab{.handle = tab->GetHandle(),
                         .index = static_cast<int>(i)};
    }
  }
  return base::unexpected(MakeDaoToolError(
      DaoToolErrorCode::kTargetGone,
      "The pinned tab is no longer in the authorized browser window."));
}

DaoBrowserToolResult SuccessResult(base::DictValue data,
                                   content::WebContents* target) {
  DaoBrowserToolResult result;
  result.ok = true;
  result.data = base::Value(std::move(data));
  result.target = MakeTarget(target);
  return result;
}

base::DictValue MakeSelectedTabResult(tabs::TabInterface* tab, int index) {
  content::WebContents* contents = tab->GetContents();
  return base::DictValue()
      .Set("success", true)
      .Set("tab_id", GetOrCreateSidebarTabId(contents))
      .Set("index", index)
      .Set("url", contents->GetURL().spec())
      .Set("title", base::UTF16ToUTF8(contents->GetTitle()));
}

}  // namespace

class DaoTabTools::PendingClose : public TabListInterfaceObserver,
                                  public content::WebContentsObserver {
 public:
  PendingClose(base::WeakPtr<DaoTabTools> owner,
               std::string request_id,
               base::WeakPtr<DaoBrowserAutomationSession> session,
               TabListInterface* tab_list,
               tabs::TabInterface* closing_tab,
               int closing_index,
               std::string closing_tab_id,
               bool closes_pinned_target,
               DaoToolClient client,
               ResultCallback callback)
      : owner_(std::move(owner)),
        request_id_(std::move(request_id)),
        session_(std::move(session)),
        tab_list_(tab_list),
        closing_tab_(closing_tab),
        closing_handle_(closing_tab->GetHandle()),
        closing_index_(closing_index),
        closing_tab_id_(std::move(closing_tab_id)),
        closes_pinned_target_(closes_pinned_target),
        client_(client),
        callback_(std::move(callback)),
        tab_list_observation_(this) {}

  ~PendingClose() override = default;

  void Start() {
    tab_list_observation_.Observe(tab_list_);
    Observe(closing_tab_->GetContents());
    starting_ = true;
    tab_list_->CloseTab(closing_handle_);
    starting_ = false;
    if (!deferred_result_.has_value()) {
      return;
    }
    DaoBrowserToolResult result = std::move(*deferred_result_);
    deferred_result_.reset();
    Deliver(std::move(result));
  }

  void Cancel(DaoToolError error) {
    DaoBrowserToolResult result;
    result.error = std::move(error);
    Complete(std::move(result));
  }

  ResultCallback TakeCallback() { return std::move(callback_); }

  void OnTabRemoved(TabListInterface& tab_list,
                    tabs::TabInterface* tab,
                    TabRemovedReason removed_reason) override {
    if (tab != closing_tab_) {
      return;
    }
    tab_list_observation_.Reset();
    Observe(nullptr);

    if (!TabRemoveReasonUtils::WillDeleteTab(removed_reason)) {
      Complete(
          ErrorResult(DaoToolErrorCode::kTargetGone,
                      "The selected tab left the authorized browser window."));
      return;
    }
    if (!session_) {
      Complete(
          ErrorResult(DaoToolErrorCode::kTargetGone,
                      "Browser automation session is no longer available."));
      return;
    }
    if (closes_pinned_target_) {
      tabs::TabInterface* replacement = tab_list.GetActiveTab();
      if (!replacement || replacement == closing_tab_) {
        base::WeakPtr<PendingClose> weak_this = weak_factory_.GetWeakPtr();
        session_->SetTarget(nullptr);
        if (!weak_this) {
          return;
        }
        Complete(ErrorResult(
            DaoToolErrorCode::kTargetGone,
            "Closing the pinned tab left no target in the authorized window."));
        return;
      }
      base::WeakPtr<PendingClose> weak_this = weak_factory_.GetWeakPtr();
      session_->SetTarget(replacement->GetContents());
      if (!weak_this) {
        return;
      }
      if (client_ == DaoToolClient::kMcp) {
        BrowserWindowInterface* browser_window = session_->browser_window();
        Browser* browser = browser_window
                               ? browser_window->GetBrowserForMigrationOnly()
                               : nullptr;
        auto policy = ValidateExternalTarget(browser, session_->profile(),
                                        replacement->GetContents());
        if (!policy.has_value()) {
          DaoBrowserToolResult result;
          result.error = std::move(policy).error();
          Complete(std::move(result));
          return;
        }
      }
    }

    if (!session_) {
      Complete(
          ErrorResult(DaoToolErrorCode::kTargetGone,
                      "Browser automation session is no longer available."));
      return;
    }
    base::WeakPtr<PendingClose> weak_this = weak_factory_.GetWeakPtr();
    auto current_target = session_->ResolveTarget();
    if (!weak_this) {
      return;
    }
    if (!session_) {
      Complete(
          ErrorResult(DaoToolErrorCode::kTargetGone,
                      "Browser automation session is no longer available."));
      return;
    }
    if (!current_target.has_value()) {
      DaoBrowserToolResult result;
      result.error = std::move(current_target).error();
      Complete(std::move(result));
      return;
    }
    base::DictValue data;
    data.Set("success", true);
    data.Set("closed_tab_id", closing_tab_id_);
    data.Set("closed_index", closing_index_);
    Complete(SuccessResult(std::move(data), *current_target));
  }

  void OnWebContentsReplaced(TabListInterface& tab_list,
                             tabs::TabInterface* tab,
                             content::WebContents* old_contents,
                             content::WebContents* new_contents) override {
    if (tab == closing_tab_) {
      Observe(new_contents);
    }
  }

  void OnTabListDestroyed(TabListInterface& tab_list) override {
    tab_list_observation_.Reset();
    tab_list_ = nullptr;
    Complete(
        ErrorResult(DaoToolErrorCode::kTargetGone,
                    "The authorized browser window is no longer available."));
  }

  void BeforeUnloadDialogCancelled() override {
    Complete(ErrorResult(DaoToolErrorCode::kToolCancelled,
                         "Tab close was cancelled by the page."));
  }

 private:
  void Complete(DaoBrowserToolResult result) {
    if (completed_) {
      return;
    }
    completed_ = true;
    if (starting_) {
      deferred_result_ = std::move(result);
      return;
    }
    Deliver(std::move(result));
  }

  void Deliver(DaoBrowserToolResult result) {
    base::WeakPtr<DaoTabTools> owner = owner_;
    std::string request_id = request_id_;
    if (owner) {
      owner->CompleteClose(std::move(request_id), std::move(result));
    }
  }

  base::WeakPtr<DaoTabTools> owner_;
  std::string request_id_;
  base::WeakPtr<DaoBrowserAutomationSession> session_;
  raw_ptr<TabListInterface> tab_list_;
  raw_ptr<tabs::TabInterface> closing_tab_;
  tabs::TabHandle closing_handle_;
  int closing_index_;
  std::string closing_tab_id_;
  bool closes_pinned_target_;
  DaoToolClient client_;
  ResultCallback callback_;
  base::ScopedObservation<TabListInterface, TabListInterfaceObserver>
      tab_list_observation_;
  bool starting_ = false;
  bool completed_ = false;
  std::optional<DaoBrowserToolResult> deferred_result_;
  base::WeakPtrFactory<PendingClose> weak_factory_{this};
};

DaoTabTools::DaoTabTools() = default;

DaoTabTools::~DaoTabTools() {
  DCHECK(pending_closes_.empty());
}

// static
bool DaoTabTools::Handles(std::string_view name) {
  return name == "list_tabs" || name == "switch_tab" || name == "open_tab" ||
         name == "close_tab";
}

void DaoTabTools::Execute(std::string request_id,
                          DaoBrowserAutomationSession* session,
                          DaoToolClient client,
                          std::string_view name,
                          const base::DictValue& arguments,
                          ResultCallback callback) {
  if (name == "close_tab") {
    ExecuteClose(std::move(request_id), session, client, arguments,
                 std::move(callback));
    return;
  }
  std::move(callback).Run(ExecuteSync(session, client, name, arguments));
}

bool DaoTabTools::Cancel(std::string_view request_id, DaoToolError error) {
  auto it = pending_closes_.find(request_id);
  if (it == pending_closes_.end()) {
    return false;
  }
  it->second->Cancel(std::move(error));
  return true;
}

DaoBrowserToolResult DaoTabTools::ExecuteSync(
    DaoBrowserAutomationSession* session,
    DaoToolClient client,
    std::string_view name,
    const base::DictValue& arguments) {
  if (!session) {
    return ErrorResult(DaoToolErrorCode::kTargetGone,
                       "Browser automation session is unavailable.");
  }
  base::WeakPtr<DaoTabTools> weak_this = weak_factory_.GetWeakPtr();
  base::WeakPtr<DaoBrowserAutomationSession> session_weak =
      session->GetWeakPtr();
  auto pinned_target = session->ResolveTarget();
  if (!weak_this || !session_weak) {
    return ErrorResult(
        DaoToolErrorCode::kToolCancelled,
        "Browser tab tool ownership changed while resolving the target.");
  }
  if (!pinned_target.has_value()) {
    DaoBrowserToolResult result;
    result.error = std::move(pinned_target).error();
    return result;
  }
  BrowserWindowInterface* browser_window = session_weak->browser_window();
  TabListInterface* tab_list =
      browser_window ? TabListInterface::From(browser_window) : nullptr;
  if (!browser_window || !tab_list) {
    return ErrorResult(DaoToolErrorCode::kTargetGone,
                       "The authorized browser window is no longer available.");
  }

  if (name == "list_tabs") {
    RepairDuplicateSidebarTabIds(GetTabContentsInOrder(tab_list));
    base::ListValue serialized_tabs;
    std::vector<tabs::TabInterface*> all_tabs = tab_list->GetAllTabs();
    tabs::TabInterface* active_tab = tab_list->GetActiveTab();
    for (size_t i = 0; i < all_tabs.size(); ++i) {
      serialized_tabs.Append(
          SerializeTab(all_tabs[i], static_cast<int>(i), active_tab));
    }
    const int count = static_cast<int>(serialized_tabs.size());
    return SuccessResult(base::DictValue()
                             .Set("tabs", std::move(serialized_tabs))
                             .Set("count", count),
                         *pinned_target);
  }

  if (name == "switch_tab") {
    if (!arguments.FindString("tab_id")) {
      RepairDuplicateSidebarTabIds(GetTabContentsInOrder(tab_list));
    }
    auto selected =
        ResolveSelectedTab(tab_list, *pinned_target, arguments, true);
    if (!selected.has_value()) {
      DaoBrowserToolResult result;
      result.error = std::move(selected).error();
      return result;
    }
    tabs::TabInterface* selected_tab = selected->handle.Get();
    if (!selected_tab) {
      return ErrorResult(DaoToolErrorCode::kTargetGone,
                         "The selected tab is no longer available.");
    }
    if (client == DaoToolClient::kMcp) {
      Browser* browser = browser_window->GetBrowserForMigrationOnly();
      auto policy = ValidateExternalTarget(browser, session_weak->profile(),
                                      selected_tab->GetContents());
      if (!policy.has_value()) {
        DaoBrowserToolResult result;
        result.error = std::move(policy).error();
        return result;
      }
    }
    tab_list->ActivateTab(selected->handle);
    if (!weak_this || !session_weak) {
      return ErrorResult(
          DaoToolErrorCode::kToolCancelled,
          "Browser tab tool ownership changed while activating a target.");
    }
    browser_window = session_weak->browser_window();
    tab_list =
        browser_window ? TabListInterface::From(browser_window) : nullptr;
    selected_tab = selected->handle.Get();
    if (!tab_list || !selected_tab) {
      return ErrorResult(DaoToolErrorCode::kTargetGone,
                         "The selected tab is no longer available.");
    }
    if (tab_list->GetActiveTab() != selected_tab) {
      return ErrorResult(DaoToolErrorCode::kTargetGone,
                         "The selected tab could not be activated.");
    }
    content::WebContents* new_target = selected_tab->GetContents();
    session_weak->SetTarget(new_target);
    if (!weak_this || !session_weak) {
      return ErrorResult(
          DaoToolErrorCode::kToolCancelled,
          "Browser tab tool ownership changed while switching targets.");
    }
    auto resolved_target = session_weak->ResolveTarget();
    if (!weak_this || !session_weak) {
      return ErrorResult(
          DaoToolErrorCode::kToolCancelled,
          "Browser tab tool ownership changed while resolving the new "
          "target.");
    }
    if (!resolved_target.has_value()) {
      return ErrorResult(
          DaoToolErrorCode::kTargetGone,
          "The selected tab left the authorized browser window.");
    }
    selected_tab = selected->handle.Get();
    if (!selected_tab || selected_tab->GetContents() != *resolved_target) {
      return ErrorResult(
          DaoToolErrorCode::kTargetGone,
          "The selected tab left the authorized browser window.");
    }
    return SuccessResult(MakeSelectedTabResult(selected_tab, selected->index),
                         *resolved_target);
  }

  if (name == "open_tab") {
    RepairDuplicateSidebarTabIds(GetTabContentsInOrder(tab_list));
    const std::string* requested_url = arguments.FindString("url");
    GURL url(requested_url ? *requested_url : std::string());
    if (!url.is_valid()) {
      url = GURL("about:blank");
    }
    if (!IsAutomationUrlEligible(url)) {
      return ErrorResult(
          DaoToolErrorCode::kTargetForbidden,
          "The requested URL is not eligible for browser automation.");
    }
    std::vector<tabs::TabHandle> handles_before;
    for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
      handles_before.push_back(tab->GetHandle());
    }
    const int active_index = tab_list->GetActiveIndex();
    tab_list->OpenTab(url, active_index, true);
    if (!weak_this || !session_weak) {
      return ErrorResult(
          DaoToolErrorCode::kToolCancelled,
          "Browser tab tool ownership changed while opening a target.");
    }
    browser_window = session_weak->browser_window();
    tab_list =
        browser_window ? TabListInterface::From(browser_window) : nullptr;
    if (!tab_list) {
      return ErrorResult(
          DaoToolErrorCode::kTargetGone,
          "The authorized browser window is no longer available.");
    }

    tabs::TabInterface* opened = nullptr;
    for (tabs::TabInterface* tab : tab_list->GetAllTabs()) {
      if (std::ranges::find(handles_before, tab->GetHandle()) ==
          handles_before.end()) {
        if (opened) {
          return ErrorResult(
              DaoToolErrorCode::kInternalError,
              "Opening one tab created an ambiguous browser state.");
        }
        opened = tab;
      }
    }
    if (!opened || tab_list->GetActiveTab() != opened) {
      return ErrorResult(
          DaoToolErrorCode::kTargetGone,
          "The new tab was not created in the authorized browser window.");
    }
    content::WebContents* new_target = opened->GetContents();
    const tabs::TabHandle opened_handle = opened->GetHandle();
    session_weak->SetTarget(new_target);
    if (!weak_this || !session_weak) {
      return ErrorResult(
          DaoToolErrorCode::kToolCancelled,
          "Browser tab tool ownership changed while opening a target.");
    }
    auto resolved_target = session_weak->ResolveTarget();
    if (!weak_this || !session_weak) {
      return ErrorResult(
          DaoToolErrorCode::kToolCancelled,
          "Browser tab tool ownership changed while resolving the new "
          "target.");
    }
    if (!resolved_target.has_value()) {
      DaoBrowserToolResult result;
      result.error = std::move(resolved_target).error();
      return result;
    }
    opened = opened_handle.Get();
    if (!opened || opened->GetContents() != *resolved_target) {
      return ErrorResult(DaoToolErrorCode::kTargetGone,
                         "The new tab left the authorized browser window.");
    }
    return SuccessResult(
        MakeSelectedTabResult(opened,
                              tab_list->GetIndexOfTab(opened->GetHandle())),
        new_target);
  }

  return ErrorResult(DaoToolErrorCode::kInternalError,
                     "Browser tab tool does not have a native handler.");
}

void DaoTabTools::ExecuteClose(std::string request_id,
                               DaoBrowserAutomationSession* session,
                               DaoToolClient client,
                               const base::DictValue& arguments,
                               ResultCallback callback) {
  auto reply = [&callback](DaoBrowserToolResult result) {
    std::move(callback).Run(std::move(result));
  };
  if (!session) {
    reply(ErrorResult(DaoToolErrorCode::kTargetGone,
                      "Browser automation session is unavailable."));
    return;
  }
  base::WeakPtr<DaoTabTools> weak_this = weak_factory_.GetWeakPtr();
  base::WeakPtr<DaoBrowserAutomationSession> session_weak =
      session->GetWeakPtr();
  auto pinned_target = session->ResolveTarget();
  if (!weak_this || !session_weak) {
    return;
  }
  if (!pinned_target.has_value()) {
    DaoBrowserToolResult result;
    result.error = std::move(pinned_target).error();
    reply(std::move(result));
    return;
  }
  BrowserWindowInterface* browser_window = session_weak->browser_window();
  TabListInterface* tab_list =
      browser_window ? TabListInterface::From(browser_window) : nullptr;
  if (!browser_window || !tab_list) {
    reply(ErrorResult(DaoToolErrorCode::kTargetGone,
                      "The authorized browser window is no longer available."));
    return;
  }
  if (tab_list->GetTabCount() <= 1) {
    reply(ErrorResult(DaoToolErrorCode::kInvalidArgument,
                      "Cannot close the last tab in the authorized window."));
    return;
  }
  if (!arguments.FindString("tab_id")) {
    RepairDuplicateSidebarTabIds(GetTabContentsInOrder(tab_list));
  }
  auto selected =
      ResolveSelectedTab(tab_list, *pinned_target, arguments, false);
  if (!selected.has_value()) {
    DaoBrowserToolResult result;
    result.error = std::move(selected).error();
    reply(std::move(result));
    return;
  }
  tabs::TabInterface* selected_tab = selected->handle.Get();
  if (!selected_tab) {
    reply(ErrorResult(DaoToolErrorCode::kTargetGone,
                      "The selected tab is no longer available."));
    return;
  }
  if (pending_closes_.contains(request_id)) {
    reply(ErrorResult(DaoToolErrorCode::kInvalidArgument,
                      "Browser tab close request id is already active."));
    return;
  }
  content::WebContents* closing_contents = selected_tab->GetContents();
  auto pending = std::make_unique<PendingClose>(
      weak_this, request_id, session_weak, tab_list, selected_tab,
      selected->index, GetOrCreateSidebarTabId(closing_contents),
      closing_contents == *pinned_target, client, std::move(callback));
  PendingClose* pending_ptr = pending.get();
  pending_closes_.emplace(std::move(request_id), std::move(pending));
  pending_ptr->Start();
}

void DaoTabTools::CompleteClose(std::string request_id,
                                DaoBrowserToolResult result) {
  auto it = pending_closes_.find(request_id);
  if (it == pending_closes_.end()) {
    return;
  }
  std::unique_ptr<PendingClose> pending = std::move(it->second);
  pending_closes_.erase(it);
  ResultCallback callback = pending->TakeCallback();
  pending.reset();
  std::move(callback).Run(std::move(result));
}

}  // namespace dao
