// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"

#include "base/containers/flat_set.h"
#include "base/no_destructor.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/rect.h"

namespace dao {

namespace {

bool g_creating_little_dao = false;

// Observer that evicts Little Dao browsers from the tracking set when they
// are destroyed. Without this, raw Browser* pointers in the set would
// dangle after the window closes and could match freshly-allocated Browser
// objects at the same address, making IsLittleDaoWindow() return true for
// unrelated windows.
class LittleDaoBrowserTracker : public BrowserListObserver {
 public:
  static LittleDaoBrowserTracker& Get() {
    static base::NoDestructor<LittleDaoBrowserTracker> instance;
    return *instance;
  }

  void Insert(const Browser* browser) {
    browsers_.insert(browser);
  }

  bool Contains(const Browser* browser) const {
    return browsers_.contains(browser);
  }

  // BrowserListObserver:
  void OnBrowserRemoved(Browser* browser) override {
    // Called when a Browser window is closed and about to be destroyed.
    // Erase even if it's not a Little Dao browser — the set operation is
    // cheap and this keeps the invariant simple.
    browsers_.erase(browser);
  }

 private:
  friend class base::NoDestructor<LittleDaoBrowserTracker>;
  LittleDaoBrowserTracker() { BrowserList::AddObserver(this); }
  // BrowserList outlives this no-destructor singleton; destructor is never
  // called in practice.
  ~LittleDaoBrowserTracker() override = default;

  base::flat_set<const Browser*> browsers_;
};

}  // namespace

// static
Browser* DaoLittleDaoController::OpenInLittleDao(Profile* profile,
                                                 const GURL& url) {
  if (!profile)
    return nullptr;

  Browser::CreateParams params(Browser::TYPE_POPUP, profile,
                               /*user_gesture=*/true);
  params.initial_bounds = gfx::Rect(0, 0, 900, 640);
  params.can_resize = true;
  params.omit_from_session_restore = true;

  // Set flag before Browser::Create so BrowserView can detect Little Dao
  // during construction.
  g_creating_little_dao = true;
  Browser* browser = Browser::Create(params);
  g_creating_little_dao = false;

  LittleDaoBrowserTracker::Get().Insert(browser);

  // Navigate to the URL in the popup's single tab.
  NavigateParams nav_params(browser, url, ui::PAGE_TRANSITION_LINK);
  nav_params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  nav_params.window_action = NavigateParams::WindowAction::kShowWindow;
  ::Navigate(&nav_params);
  return browser;
}

// static
bool DaoLittleDaoController::IsLittleDaoWindow(const Browser* browser) {
  if (!browser)
    return false;
  return LittleDaoBrowserTracker::Get().Contains(browser);
}

// static
bool DaoLittleDaoController::IsCreatingLittleDao() {
  return g_creating_little_dao;
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

  // Close the Little Dao window. The tracker's OnBrowserRemoved observer
  // will erase the entry from the set when the Browser is destroyed, so no
  // explicit erase is needed here.
  little_dao_browser->window()->Close();
}

}  // namespace dao
