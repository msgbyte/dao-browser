// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_native_util_mac.h"

#include <windows.h>

#include <map>
#include <memory>

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "ui/aura/scoped_window_event_targeting_blocker.h"

namespace dao {
namespace {

using EventBlockers =
    std::map<aura::Window*,
             std::unique_ptr<aura::ScopedWindowEventTargetingBlocker>>;

EventBlockers& GetEventBlockers() {
  static base::NoDestructor<EventBlockers> blockers;
  return *blockers;
}

base::RepeatingTimer& TabDragWatchdogTimer() {
  static base::NoDestructor<base::RepeatingTimer> timer;
  return *timer;
}

DaoSplitView* GetSplitView(BrowserWindowInterface* browser_window) {
  BrowserView* view = browser_window
                         ? BrowserView::GetBrowserViewForBrowser(browser_window)
                         : nullptr;
  return view ? view->dao_split_view() : nullptr;
}

void TabDragWatchdogTick() {
  const bool button_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ||
                           (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ||
                           (GetAsyncKeyState(VK_MBUTTON) & 0x8000);
  if (!button_down || (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
    EndTabDragNativeEvents();
  }
}

}  // namespace

void BlockWebContentNativeEvents(content::WebContents* web_contents) {
  if (!web_contents || !web_contents->GetNativeView()) {
    return;
  }
  auto& blockers = GetEventBlockers();
  // The scoped blocker observes native-window destruction and drops its pointer.
  // Remove those entries before an address can be reused by a new window.
  std::erase_if(blockers, [](const auto& entry) {
    return !entry.second->window();
  });
  auto* window = web_contents->GetNativeView();
  if (!blockers.contains(window)) {
    blockers.emplace(
        window, std::make_unique<aura::ScopedWindowEventTargetingBlocker>(window));
  }
}

void UnblockWebContentNativeEvents(content::WebContents* web_contents) {
  if (web_contents) {
    GetEventBlockers().erase(web_contents->GetNativeView());
  }
}

void ArmTabDragWatchdog() {
  if (!TabDragWatchdogTimer().IsRunning()) {
    TabDragWatchdogTimer().Start(FROM_HERE, base::Milliseconds(100),
                                 base::BindRepeating(&TabDragWatchdogTick));
  }
}

void StopTabDragWatchdog() {
  for (auto* browser_window : GetAllBrowserWindowInterfaces()) {
    auto* split = GetSplitView(browser_window);
    if (split && split->tab_drag_active()) {
      return;
    }
  }
  TabDragWatchdogTimer().Stop();
}

void EndTabDragNativeEvents() {
  for (auto* browser_window : GetAllBrowserWindowInterfaces()) {
    if (auto* split = GetSplitView(browser_window)) {
      split->SetTabDragActive(false);
    }
  }
  TabDragWatchdogTimer().Stop();
  GetEventBlockers().clear();
}

}  // namespace dao
