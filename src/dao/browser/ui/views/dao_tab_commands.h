// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_TAB_COMMANDS_H_
#define DAO_BROWSER_UI_VIEWS_DAO_TAB_COMMANDS_H_

#include "chrome/browser/ui/browser_commands.h"

class Browser;

namespace dao {

inline bool DuplicateActiveTab(Browser* browser) {
  if (!chrome::CanDuplicateTab(browser)) {
    return false;
  }
  chrome::DuplicateTab(browser);
  return true;
}

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_TAB_COMMANDS_H_
