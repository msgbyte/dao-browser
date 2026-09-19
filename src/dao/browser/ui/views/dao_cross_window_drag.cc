// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_cross_window_drag.h"

#include <algorithm>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace dao {

bool ParseDaoTabDragPayload(const std::string& payload,
                            int* source_session_id,
                            int* tab_index,
                            std::string* tab_id) {
  if (!base::StartsWith(payload, kDaoTabDragPrefix,
                        base::CompareCase::SENSITIVE)) {
    return false;
  }
  const std::string body =
      payload.substr(std::string(kDaoTabDragPrefix).size());
  const size_t colon = body.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= body.size()) {
    return false;
  }
  int sid = 0;
  int idx = 0;
  const size_t identity_colon = body.find(':', colon + 1);
  const std::string identity = identity_colon == std::string::npos
                                   ? std::string()
                                   : body.substr(identity_colon + 1);
  const std::string session_part = body.substr(0, colon);
  const std::string index_part =
      body.substr(colon + 1, identity_colon == std::string::npos
                                 ? std::string::npos
                                 : identity_colon - colon - 1);
  if (!base::ContainsOnlyChars(session_part, "0123456789") ||
      !base::ContainsOnlyChars(index_part, "0123456789") ||
      !base::StringToInt(session_part, &sid) ||
      !base::StringToInt(index_part, &idx) || sid <= 0 || idx < 0 ||
      (identity_colon != std::string::npos &&
       (identity.empty() ||
        identity.find_first_of(": \t\r\n\f\v") != std::string::npos))) {
    return false;
  }
  if (source_session_id) {
    *source_session_id = sid;
  }
  if (tab_index) {
    *tab_index = idx;
  }
  if (tab_id) {
    *tab_id = identity;
  }
  return true;
}

int ResolveDraggedTabIndex(Browser* browser,
                           int tab_index,
                           const std::string& tab_id) {
  if (!browser) {
    return TabStripModel::kNoTab;
  }
  TabStripModel* model = browser->tab_strip_model();
  if (!tab_id.empty()) {
    for (int i = 0; i < model->count(); ++i) {
      if (GetSidebarTabId(model->GetWebContentsAt(i)) == tab_id) {
        return i;
      }
    }
    return TabStripModel::kNoTab;
  }
  return model->ContainsIndex(tab_index) ? tab_index : TabStripModel::kNoTab;
}

Browser* DetachTabToNewWindow(Browser* source_browser,
                              const std::string& tab_id,
                              const gfx::Point& screen_point) {
  const int index = ResolveDraggedTabIndex(source_browser, -1, tab_id);
  if (index == TabStripModel::kNoTab || !source_browser->is_type_normal()) {
    return nullptr;
  }
  gfx::Rect bounds = source_browser->window()->GetBounds();
  bounds.set_origin(
      gfx::Point(screen_point.x() - bounds.width() / 4, screen_point.y() - 40));
  if (source_browser->tab_strip_model()->count() == 1) {
    source_browser->window()->SetBounds(bounds);
    source_browser->window()->Activate();
    return source_browser;
  }
  Browser::CreateParams params(source_browser->profile(), true);
  params.initial_bounds = bounds;
  Browser* target = Browser::Create(params);
  if (!target) {
    return nullptr;
  }
  chrome::MoveTabsToExistingWindow(source_browser, target, {index});
  target->window()->Show();
  target->window()->Activate();
  return target;
}

namespace {

DaoSplitView* SplitViewFor(Browser* browser) {
  if (!browser) {
    return nullptr;
  }
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser);
  return bv ? bv->dao_split_view() : nullptr;
}

}  // namespace

void UpdateSplitDropIndicator(Browser* target_browser,
                              const gfx::Point& point_in_browser_view) {
  DaoSplitView* sv = SplitViewFor(target_browser);
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(target_browser);
  if (!sv || !bv) {
    return;
  }
  gfx::Point pt_in_split = point_in_browser_view;
  views::View::ConvertPointToTarget(bv, sv, &pt_in_split);
  if (!sv->GetLocalBounds().Contains(pt_in_split)) {
    sv->HideNativeDropIndicator();
    return;
  }
  sv->UpdateNativeDropIndicator(pt_in_split);
}

void HideSplitDropIndicator(Browser* target_browser) {
  if (DaoSplitView* sv = SplitViewFor(target_browser)) {
    sv->HideNativeDropIndicator();
  }
}

bool PerformSplitTabDrop(Browser* target_browser,
                         const gfx::Point& point_in_browser_view,
                         const std::string& payload) {
  DaoSplitView* sv = SplitViewFor(target_browser);
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(target_browser);
  if (!sv || !bv) {
    return false;
  }
  gfx::Point pt_in_split = point_in_browser_view;
  views::View::ConvertPointToTarget(bv, sv, &pt_in_split);
  if (!sv->GetLocalBounds().Contains(pt_in_split)) {
    return false;
  }
  return sv->ProcessNativeTabDrop(pt_in_split, payload);
}

bool ExecuteCrossWindowTabMove(Browser* target_browser,
                               int source_session_id,
                               int source_tab_index,
                               int target_insert_index,
                               const std::string& tab_id) {
  if (!target_browser) {
    return false;
  }

  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(target_browser->profile());
  BrowserWindowInterface* source_browser_window =
      collection ? collection->FindBrowserWithID(
                       SessionID::FromSerializedValue(source_session_id))
                 : nullptr;
  Browser* source_browser =
      source_browser_window
          ? source_browser_window->GetBrowserForMigrationOnly()
          : nullptr;
  if (!source_browser || source_browser == target_browser) {
    return false;
  }

  TabStripModel* source_model = source_browser->tab_strip_model();
  source_tab_index =
      ResolveDraggedTabIndex(source_browser, source_tab_index, tab_id);
  if (source_tab_index < 0 || source_tab_index >= source_model->count()) {
    return false;
  }

  content::WebContents* contents =
      source_model->GetWebContentsAt(source_tab_index);
  TabStripModel* target_model = target_browser->tab_strip_model();
  const int insert_at =
      target_insert_index < 0
          ? target_model->count()
          : std::min(target_insert_index, target_model->count());
  chrome::MoveTabsToExistingWindow(source_browser, target_browser,
                                   {source_tab_index});
  const int moved_index = target_model->GetIndexOfWebContents(contents);
  if (moved_index == TabStripModel::kNoTab) {
    return false;
  }
  target_model->MoveWebContentsAt(moved_index, insert_at, true);
  return true;
}

}  // namespace dao
