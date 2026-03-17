// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/rect.h"

namespace dao {

const char LittleDaoMarker::kKey[] = "dao_little_dao_marker";
bool DaoLittleDaoController::creating_little_dao_ = false;

// static
void DaoLittleDaoController::OpenInLittleDao(Profile* profile,
                                             const GURL& url) {
  if (!profile)
    return;

  Browser::CreateParams params(Browser::TYPE_POPUP, profile,
                               /*user_gesture=*/true);
  params.initial_bounds = gfx::Rect(0, 0, 900, 640);
  params.can_resize = true;
  params.omit_from_session_restore = true;

  // Set flag before Browser::Create so BrowserView can detect Little Dao
  // during construction (UserData isn't available yet at that point).
  creating_little_dao_ = true;
  Browser* browser = Browser::Create(params);
  creating_little_dao_ = false;

  browser->SetUserData(LittleDaoMarker::kKey,
                       std::make_unique<LittleDaoMarker>());

  // Navigate to the URL in the popup's single tab.
  NavigateParams nav_params(browser, url, ui::PAGE_TRANSITION_LINK);
  nav_params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  nav_params.window_action = NavigateParams::SHOW_WINDOW;
  ::Navigate(&nav_params);
}

// static
bool DaoLittleDaoController::IsLittleDaoWindow(const Browser* browser) {
  if (!browser)
    return false;
  return browser->GetUserData(LittleDaoMarker::kKey) != nullptr;
}

// static
bool DaoLittleDaoController::IsCreatingLittleDao() {
  return creating_little_dao_;
}

// static
void DaoLittleDaoController::TransferToMainBrowser(
    Browser* little_dao_browser) {
  if (!little_dao_browser)
    return;

  TabStripModel* source_model = little_dao_browser->tab_strip_model();
  if (!source_model || source_model->empty())
    return;

  // Detach the WebContents from the Little Dao window.
  std::unique_ptr<content::WebContents> contents =
      source_model->DetachWebContentsAtForInsertion(0);
  if (!contents)
    return;

  Profile* profile = little_dao_browser->profile();

  // Find an existing tabbed browser, or create one.
  Browser* main_browser = chrome::FindTabbedBrowser(profile, false);
  if (!main_browser) {
    Browser::CreateParams main_params(profile, /*user_gesture=*/true);
    main_browser = Browser::Create(main_params);
    main_browser->window()->Show();
  }

  // Insert the WebContents into the main browser.
  main_browser->tab_strip_model()->InsertWebContentsAt(
      -1, std::move(contents), AddTabTypes::ADD_ACTIVE);

  // Activate the main browser window.
  main_browser->window()->Activate();

  // Close the Little Dao window.
  little_dao_browser->window()->Close();
}

}  // namespace dao
