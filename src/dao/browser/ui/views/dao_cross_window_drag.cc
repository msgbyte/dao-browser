// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_cross_window_drag.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/sessions/core/session_id.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/view.h"

namespace dao {

bool ParseDaoTabDragPayload(const std::string& payload,
                            int* source_session_id,
                            int* tab_index) {
  if (!base::StartsWith(payload, kDaoTabDragPrefix,
                        base::CompareCase::SENSITIVE)) {
    return false;
  }
  const std::string body =
      payload.substr(std::string(kDaoTabDragPrefix).size());
  const size_t colon = body.find(':');
  if (colon == std::string::npos || colon == 0 ||
      colon + 1 >= body.size()) {
    return false;
  }
  int sid = 0;
  int idx = 0;
  if (!base::StringToInt(body.substr(0, colon), &sid) ||
      !base::StringToInt(body.substr(colon + 1), &idx)) {
    return false;
  }
  if (source_session_id) {
    *source_session_id = sid;
  }
  if (tab_index) {
    *tab_index = idx;
  }
  return true;
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
                               int target_insert_index) {
  if (!target_browser) {
    return false;
  }

  ProfileBrowserCollection* collection =
      ProfileBrowserCollection::GetForProfile(target_browser->profile());
  BrowserWindowInterface* source_browser_window =
      collection ? collection->FindBrowserWithID(
                       SessionID::FromSerializedValue(source_session_id))
                 : nullptr;
  Browser* source_browser = source_browser_window
                                ? source_browser_window
                                      ->GetBrowserForMigrationOnly()
                                : nullptr;
  if (!source_browser || source_browser == target_browser) {
    return false;
  }

  TabStripModel* source_model = source_browser->tab_strip_model();
  if (source_tab_index < 0 ||
      source_tab_index >= source_model->count()) {
    return false;
  }

  std::unique_ptr<content::WebContents> detached =
      source_model->DetachWebContentsAtForInsertion(source_tab_index);
  if (!detached) {
    return false;
  }

  TabStripModel* target_model = target_browser->tab_strip_model();
  int insert_at = target_insert_index;
  if (insert_at < 0) {
    insert_at = 0;
  }
  if (insert_at > target_model->count()) {
    insert_at = target_model->count();
  }
  target_model->InsertWebContentsAt(insert_at, std::move(detached),
                                    AddTabTypes::ADD_ACTIVE);

  if (source_model->count() == 0) {
    source_browser->window()->Close();
  }
  return true;
}

}  // namespace dao
