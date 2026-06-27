// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_TAB_COMMANDS_H_
#define DAO_BROWSER_UI_VIEWS_DAO_TAB_COMMANDS_H_

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"

class Browser;

namespace dao {

inline content::WebContents* DuplicateTabAt(Browser* browser, int index) {
  if (!browser || !chrome::CanDuplicateTabAt(browser, index)) {
    return nullptr;
  }

  TabStripModel* model = browser->tab_strip_model();
  const bool source_was_pinned = model->IsTabPinned(index);
  content::WebContents* duplicate = chrome::DuplicateTabAt(browser, index);
  if (!duplicate || !source_was_pinned) {
    return duplicate;
  }

  const int duplicate_index = model->GetIndexOfWebContents(duplicate);
  if (duplicate_index != TabStripModel::kNoTab &&
      model->IsTabPinned(duplicate_index)) {
    model->SetTabPinned(duplicate_index, false);
  }
  return duplicate;
}

inline bool DuplicateActiveTab(Browser* browser) {
  if (!browser) {
    return false;
  }
  return DuplicateTabAt(browser, browser->tab_strip_model()->active_index()) !=
         nullptr;
}

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_TAB_COMMANDS_H_
