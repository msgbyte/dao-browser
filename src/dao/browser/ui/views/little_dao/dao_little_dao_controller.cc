// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"

#include <optional>

#include "base/auto_reset.h"
#include "base/containers/flat_set.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/dao_pref_names.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace dao {

namespace {

constexpr int kDefaultLittleDaoWindowWidth = 900;
constexpr int kDefaultLittleDaoWindowHeight = 640;
constexpr char kXKey[] = "x";
constexpr char kYKey[] = "y";
constexpr char kWidthKey[] = "width";
constexpr char kHeightKey[] = "height";

bool g_creating_little_dao = false;

PrefService* GetValidLittleDaoPrefs(Profile* profile) {
  if (!profile || profile->IsOffTheRecord()) {
    return nullptr;
  }

  PrefService* prefs = profile->GetPrefs();
  if (!prefs ||
      !prefs->FindPreference(dao::prefs::kDaoLittleDaoWindowSize)) {
    return nullptr;
  }

  return prefs;
}

std::optional<gfx::Size> GetPersistedLittleDaoWindowSize(Profile* profile) {
  PrefService* prefs = GetValidLittleDaoPrefs(profile);
  if (!prefs) {
    return std::nullopt;
  }

  const base::DictValue& size_pref =
      prefs->GetDict(dao::prefs::kDaoLittleDaoWindowSize);
  const std::optional<int> width = size_pref.FindInt(kWidthKey);
  const std::optional<int> height = size_pref.FindInt(kHeightKey);
  if (!width || !height || *width <= 0 || *height <= 0) {
    return std::nullopt;
  }

  return gfx::Size(*width, *height);
}

std::optional<gfx::Rect> GetPersistedLittleDaoWindowBounds(Profile* profile) {
  PrefService* prefs = GetValidLittleDaoPrefs(profile);
  if (!prefs) {
    return std::nullopt;
  }

  const base::DictValue& bounds_pref =
      prefs->GetDict(dao::prefs::kDaoLittleDaoWindowSize);
  const std::optional<int> x = bounds_pref.FindInt(kXKey);
  const std::optional<int> y = bounds_pref.FindInt(kYKey);
  const std::optional<int> width = bounds_pref.FindInt(kWidthKey);
  const std::optional<int> height = bounds_pref.FindInt(kHeightKey);
  if (!x || !y || !width || !height || *width <= 0 || *height <= 0) {
    return std::nullopt;
  }

  return gfx::Rect(*x, *y, *width, *height);
}

gfx::Rect AdjustLittleDaoBoundsToWorkArea(const gfx::Rect& bounds) {
  display::Screen* screen = display::Screen::Get();
  if (!screen || bounds.IsEmpty()) {
    return bounds;
  }

  gfx::Rect adjusted_bounds = bounds;
  const gfx::Rect work_area = screen->GetDisplayMatching(bounds).work_area();
  if (!work_area.IsEmpty()) {
    adjusted_bounds.AdjustToFit(work_area);
  }
  return adjusted_bounds;
}

gfx::Rect CenterLittleDaoBoundsInTargetDisplay(Profile* profile,
                                               const gfx::Size& size) {
  display::Screen* screen = display::Screen::Get();
  if (!screen || size.IsEmpty()) {
    return gfx::Rect(gfx::Point(), size);
  }

  display::Display display = screen->GetPrimaryDisplay();
  if (Browser* last_active_browser = chrome::FindLastActiveWithProfile(profile);
      last_active_browser && last_active_browser->window()) {
    display = screen->GetDisplayNearestWindow(
        last_active_browser->window()->GetNativeWindow());
  }

  gfx::Rect bounds = display.work_area();
  if (bounds.IsEmpty()) {
    return gfx::Rect(gfx::Point(), size);
  }
  bounds.ClampToCenteredSize(size);
  return bounds;
}

gfx::Rect GetInitialLittleDaoBounds(Profile* profile) {
  if (std::optional<gfx::Rect> persisted_bounds =
          GetPersistedLittleDaoWindowBounds(profile)) {
    return AdjustLittleDaoBoundsToWorkArea(*persisted_bounds);
  }

  gfx::Size size(kDefaultLittleDaoWindowWidth, kDefaultLittleDaoWindowHeight);
  if (std::optional<gfx::Size> persisted_size =
          GetPersistedLittleDaoWindowSize(profile)) {
    size = *persisted_size;
  }

  return CenterLittleDaoBoundsInTargetDisplay(profile, size);
}

void RestoreLittleDaoWindowBounds(Browser* browser, const gfx::Rect& bounds) {
  if (!browser || !browser->window() || bounds.IsEmpty()) {
    return;
  }

  browser->window()->SetBounds(bounds);
}

void UpdatePersistedLittleDaoWindowBounds(Browser* browser) {
  if (!browser || !browser->window()) {
    return;
  }

  PrefService* prefs = GetValidLittleDaoPrefs(browser->profile());
  if (!prefs) {
    return;
  }

  const gfx::Rect bounds = browser->window()->GetBounds();
  if (bounds.width() <= 0 || bounds.height() <= 0) {
    return;
  }

  ScopedDictPrefUpdate update(prefs, dao::prefs::kDaoLittleDaoWindowSize);
  update->Set(kXKey, bounds.x());
  update->Set(kYKey, bounds.y());
  update->Set(kWidthKey, bounds.width());
  update->Set(kHeightKey, bounds.height());
}

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
    if (browsers_.contains(browser)) {
      UpdatePersistedLittleDaoWindowBounds(browser);
    }
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

Browser* CreateLittleDaoBrowser(Profile* profile) {
  if (!profile)
    return nullptr;

  Browser::CreateParams params(Browser::TYPE_POPUP, profile,
                               /*user_gesture=*/true);
  const gfx::Rect initial_bounds = GetInitialLittleDaoBounds(profile);
  params.initial_bounds = initial_bounds;
  params.can_resize = true;
  params.can_fullscreen = false;
  params.omit_from_session_restore = true;

  // Set flag before Browser::Create so BrowserView can detect Little Dao
  // during construction.
  base::AutoReset<bool> creating_little_dao(&g_creating_little_dao, true);
  Browser* browser = Browser::Create(params);
  if (!browser)
    return nullptr;

  LittleDaoBrowserTracker::Get().Insert(browser);
  RestoreLittleDaoWindowBounds(browser, initial_bounds);
  return browser;
}

}  // namespace

// static
Browser* DaoLittleDaoController::OpenInLittleDao(Profile* profile,
                                                 const GURL& url) {
  Browser* browser = CreateLittleDaoBrowser(profile);
  if (!browser)
    return nullptr;

  // Navigate to the URL in the popup's single tab.
  NavigateParams nav_params(browser, url, ui::PAGE_TRANSITION_LINK);
  nav_params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  nav_params.window_action = NavigateParams::WindowAction::kShowWindow;
  ::Navigate(&nav_params);
  return browser;
}

// static
Browser* DaoLittleDaoController::ExtractActiveTabToLittleDao(
    Browser* source_browser) {
  if (!source_browser || IsLittleDaoWindow(source_browser))
    return nullptr;

  TabStripModel* source_model = source_browser->tab_strip_model();
  if (!source_model || source_model->empty())
    return nullptr;

  content::WebContents* original_contents =
      source_model->GetActiveWebContents();
  if (!original_contents)
    return nullptr;

  Profile* profile = source_browser->profile();
  if (!profile)
    return nullptr;

  Browser* little_dao_browser = CreateLittleDaoBrowser(profile);
  if (!little_dao_browser)
    return nullptr;

  content::WebContents* replacement_contents = nullptr;
  if (source_model->count() == 1) {
    chrome::AddTabAt(source_browser, GURL("about:blank"), -1, true);
    replacement_contents = source_model->GetActiveWebContents();
    if (replacement_contents == original_contents)
      replacement_contents = nullptr;
  }

  auto close_replacement = [&]() {
    if (!replacement_contents)
      return;

    const int replacement_index =
        source_model->GetIndexOfWebContents(replacement_contents);
    if (replacement_index != TabStripModel::kNoTab) {
      source_model->CloseWebContentsAt(replacement_index,
                                       TabCloseTypes::CLOSE_NONE);
    }
  };

  const int source_index =
      source_model->GetIndexOfWebContents(original_contents);
  if (source_index == TabStripModel::kNoTab) {
    close_replacement();
    little_dao_browser->window()->Close();
    return nullptr;
  }

  std::unique_ptr<content::WebContents> contents =
      source_model->DetachWebContentsAtForInsertion(source_index);
  if (!contents) {
    close_replacement();
    little_dao_browser->window()->Close();
    return nullptr;
  }

  little_dao_browser->tab_strip_model()->InsertWebContentsAt(
      -1, std::move(contents), AddTabTypes::ADD_ACTIVE);
  little_dao_browser->window()->Show();
  little_dao_browser->window()->Activate();
  return little_dao_browser;
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
