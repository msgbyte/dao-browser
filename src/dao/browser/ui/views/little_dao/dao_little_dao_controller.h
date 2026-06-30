// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_LITTLE_DAO_CONTROLLER_H_
#define DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_LITTLE_DAO_CONTROLLER_H_

#include "url/gurl.h"

class Browser;
class Profile;

namespace dao {

// Controller for Little Dao windows — simplified browser windows used to
// open external URLs (e.g. from Terminal, Slack). Provides a minimal UI
// with an address bar and "Open in Dao" button to transfer the page to
// the main browser.
class DaoLittleDaoController {
 public:
  // Creates a Little Dao popup window, navigates to |url|, and returns the
  // created Browser. Returns nullptr when no suitable profile is available.
  static Browser* OpenInLittleDao(Profile* profile, const GURL& url);

  // Moves the active tab from |source_browser| into a new Little Dao popup.
  // The moved WebContents keeps its live page state. If |source_browser| only
  // has one tab, a replacement blank tab is left behind so the source browser
  // stays open. Returns the created Little Dao browser, or nullptr on failure.
  static Browser* ExtractActiveTabToLittleDao(Browser* source_browser);

  // Returns true if |browser| is a Little Dao window.
  static bool IsLittleDaoWindow(const Browser* browser);

  // Returns true if a Little Dao window is currently being created.
  // Used by BrowserView to detect Little Dao during construction.
  static bool IsCreatingLittleDao();

  // Transfers the active WebContents from the Little Dao |browser| to the
  // main tabbed browser window, then closes the Little Dao window.
  static void TransferToMainBrowser(Browser* little_dao_browser);
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_LITTLE_DAO_CONTROLLER_H_
