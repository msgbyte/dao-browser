// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_LITTLE_DAO_CONTROLLER_H_
#define DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_LITTLE_DAO_CONTROLLER_H_

#include "base/supports_user_data.h"
#include "url/gurl.h"

class Browser;
class Profile;

namespace dao {

// Marker attached to Browser via SupportsUserData to identify Little Dao
// windows. Distinguishes them from regular JS popups which also use
// TYPE_POPUP.
class LittleDaoMarker : public base::SupportsUserData::Data {
 public:
  static const char kKey[];
  LittleDaoMarker() = default;
  ~LittleDaoMarker() override = default;
};

// Controller for Little Dao windows — simplified browser windows used to
// open external URLs (e.g. from Terminal, Slack). Provides a minimal UI
// with an address bar and "Open in Dao" button to transfer the page to
// the main browser.
class DaoLittleDaoController {
 public:
  // Creates a Little Dao popup window and navigates to |url|.
  static void OpenInLittleDao(Profile* profile, const GURL& url);

  // Returns true if |browser| is a Little Dao window.
  static bool IsLittleDaoWindow(const Browser* browser);

  // Returns true if a Little Dao window is currently being created.
  // Used by BrowserView to detect Little Dao during construction,
  // before UserData is set on the Browser.
  static bool IsCreatingLittleDao();

  // Transfers the active WebContents from the Little Dao |browser| to the
  // main tabbed browser window, then closes the Little Dao window.
  static void TransferToMainBrowser(Browser* little_dao_browser);

 private:
  static bool creating_little_dao_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_LITTLE_DAO_CONTROLLER_H_
