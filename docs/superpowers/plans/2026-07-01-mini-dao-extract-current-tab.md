# Mini Dao Current Tab Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Control Center `Mini Dao` action that moves the current live tab into a Little Dao independent window.

**Architecture:** Keep the Control Center as a thin UI caller. Add the live-tab transfer behavior to `DaoLittleDaoController`, reusing Little Dao popup creation, bounds persistence, tracker registration, and the existing `Open in Dao` return path.

**Tech Stack:** Chromium Views C++, `Browser`, `TabStripModel`, `WebContents`, Dao `dao_strings.grd` i18n, browser tests in `src/dao/browser/ui/views/dao_browser_browsertest.cc`, npm project scripts only.

---

## Guardrails

- Work only in the primary checkout on `main`; do not create branches or worktrees.
- Do not edit `engine/` directly.
- Do not run `autoninja`, `ninja`, `siso`, `gn gen`, `npm run build`, `npm run build:debug`, `npm run test:build`, or direct Chromium build tools.
- Use `npm run import` after canonical source edits when a browser test must run against `engine/src`.
- Use focused browser-test filters while iterating. Use `npm run rebuild` only when compile confirmation is required.
- Do not run any state-changing git command unless the latest user message explicitly authorizes that exact action. Optional commit checkpoints below are gated by that project rule.

## File Structure

- Modify `src/dao/browser/ui/views/little_dao/dao_little_dao_controller.h`: declare `ExtractActiveTabToLittleDao(Browser*)`.
- Modify `src/dao/browser/ui/views/little_dao/dao_little_dao_controller.cc`: factor Little Dao browser creation into a helper, implement active-tab extraction, preserve one source tab, and reuse tracker/bounds logic.
- Modify `src/dao/browser/ui/views/dao_control_center_utility_section.h`: add `OnMiniDaoClicked()`.
- Modify `src/dao/browser/ui/views/dao_control_center_utility_section.cc`: add the `Mini Dao` utility button, call the controller, and show a localized failure toast.
- Modify `src/dao/browser/strings/dao_strings.grd`: add localized source strings for the button label and failure toast.
- Modify `src/dao/browser/ui/views/dao_browser_browsertest.cc`: add controller behavior tests and a Control Center button smoke test.

## Task 1: Lock Controller Semantics With Failing Browser Tests

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Add extraction browser tests**

Add this block near the existing `DaoLittleDaoControllerTrackerBrowserTest` section, before `DaoLittleDaoBoundsBrowserTest`:

```cpp
// =============================================================================
// DaoLittleDaoExtractionBrowserTest
//
// Verifies that extracting a tab to Mini Dao moves the live WebContents into a
// Little Dao popup instead of opening a fresh URL copy.
// =============================================================================

using DaoLittleDaoExtractionBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoLittleDaoExtractionBrowserTest,
                       ExtractActiveTabMovesLiveWebContentsToLittleDao) {
  TabStripModel* source_model = browser()->tab_strip_model();
  ASSERT_NE(nullptr, source_model);

  const GURL original_url("data:text/plain,extract");
  chrome::AddTabAt(browser(), original_url, -1, true);
  content::WebContents* original_contents =
      source_model->GetActiveWebContents();
  ASSERT_NE(nullptr, original_contents);
  ASSERT_TRUE(content::WaitForLoadStop(original_contents));
  const int source_count_before = source_model->count();
  ASSERT_GT(source_count_before, 1);

  BrowserAddedRecorder added_recorder;
  Browser* little_dao_browser =
      dao::DaoLittleDaoController::ExtractActiveTabToLittleDao(browser());

  ASSERT_NE(nullptr, little_dao_browser);
  ASSERT_EQ(1u, added_recorder.added_count());
  EXPECT_EQ(little_dao_browser, added_recorder.added_browser_at(0));
  EXPECT_TRUE(
      dao::DaoLittleDaoController::IsLittleDaoWindow(little_dao_browser));
  EXPECT_EQ(Browser::TYPE_POPUP, little_dao_browser->type());
  EXPECT_NE(nullptr,
            GetBrowserView(little_dao_browser)->dao_little_dao_view());

  EXPECT_EQ(source_count_before - 1, source_model->count());
  EXPECT_EQ(TabStripModel::kNoTab,
            source_model->GetIndexOfWebContents(original_contents));
  ASSERT_EQ(1, little_dao_browser->tab_strip_model()->count());
  EXPECT_EQ(original_contents,
            little_dao_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(original_url, original_contents->GetVisibleURL());

  BrowserRemovedWaiter removed(little_dao_browser);
  little_dao_browser->window()->Close();
  removed.Wait();
}

IN_PROC_BROWSER_TEST_F(DaoLittleDaoExtractionBrowserTest,
                       ExtractOnlyTabLeavesReplacementTabInSourceWindow) {
  TabStripModel* source_model = browser()->tab_strip_model();
  ASSERT_NE(nullptr, source_model);
  ASSERT_EQ(1, source_model->count());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<title>only</title>")));
  content::WebContents* original_contents =
      source_model->GetActiveWebContents();
  ASSERT_NE(nullptr, original_contents);

  BrowserAddedRecorder added_recorder;
  Browser* little_dao_browser =
      dao::DaoLittleDaoController::ExtractActiveTabToLittleDao(browser());

  ASSERT_NE(nullptr, little_dao_browser);
  ASSERT_EQ(1u, added_recorder.added_count());
  EXPECT_TRUE(
      dao::DaoLittleDaoController::IsLittleDaoWindow(little_dao_browser));

  ASSERT_EQ(1, source_model->count());
  EXPECT_NE(original_contents, source_model->GetActiveWebContents());
  EXPECT_EQ(TabStripModel::kNoTab,
            source_model->GetIndexOfWebContents(original_contents));
  ASSERT_EQ(1, little_dao_browser->tab_strip_model()->count());
  EXPECT_EQ(original_contents,
            little_dao_browser->tab_strip_model()->GetActiveWebContents());
  EXPECT_TRUE(browser()->window()->IsVisible());

  BrowserRemovedWaiter removed(little_dao_browser);
  little_dao_browser->window()->Close();
  removed.Wait();
}

IN_PROC_BROWSER_TEST_F(DaoLittleDaoExtractionBrowserTest,
                       ExtractFromLittleDaoWindowIsRejected) {
  Browser* little_dao_browser = dao::DaoLittleDaoController::OpenInLittleDao(
      browser()->profile(), GURL("data:text/html,little"));
  ASSERT_NE(nullptr, little_dao_browser);
  ASSERT_TRUE(
      dao::DaoLittleDaoController::IsLittleDaoWindow(little_dao_browser));

  BrowserAddedRecorder added_recorder;
  EXPECT_EQ(nullptr,
            dao::DaoLittleDaoController::ExtractActiveTabToLittleDao(
                little_dao_browser));
  EXPECT_EQ(0u, added_recorder.added_count());
  EXPECT_EQ(1, little_dao_browser->tab_strip_model()->count());

  BrowserRemovedWaiter removed(little_dao_browser);
  little_dao_browser->window()->Close();
  removed.Wait();
}
```

- [ ] **Step 2: Run the focused failing tests**

Run:

```bash
npm run import
npm run test -- --gtest_filter='DaoLittleDaoExtractionBrowserTest.*'
```

Expected: compile fails because `DaoLittleDaoController::ExtractActiveTabToLittleDao` is not declared. This is the intended red state.

- [ ] **Step 3: Optional checkpoint commit**

Only run if the latest user message explicitly authorizes this exact git action:

```bash
git add src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "test(little-dao): cover active tab extraction"
```

## Task 2: Implement Active Tab Extraction In DaoLittleDaoController

**Files:**
- Modify: `src/dao/browser/ui/views/little_dao/dao_little_dao_controller.h`
- Modify: `src/dao/browser/ui/views/little_dao/dao_little_dao_controller.cc`

- [ ] **Step 1: Declare the controller entry point**

In `src/dao/browser/ui/views/little_dao/dao_little_dao_controller.h`, add the method after `OpenInLittleDao()`:

```cpp
  // Moves the active tab from |source_browser| into a new Little Dao popup.
  // The moved WebContents keeps its live page state. If |source_browser| only
  // has one tab, a replacement blank tab is left behind so the source browser
  // stays open. Returns the created Little Dao browser, or nullptr on failure.
  static Browser* ExtractActiveTabToLittleDao(Browser* source_browser);
```

- [ ] **Step 2: Factor Little Dao browser creation into a helper**

In `src/dao/browser/ui/views/little_dao/dao_little_dao_controller.cc`, replace the browser-creation body currently inside `OpenInLittleDao()` with this helper in the anonymous namespace. Place it immediately after the `LittleDaoBrowserTracker` class definition so it can call `LittleDaoBrowserTracker::Get()`:

```cpp
Browser* CreateLittleDaoBrowser(Profile* profile) {
  if (!profile)
    return nullptr;

  Browser::CreateParams params(Browser::TYPE_POPUP, profile,
                               /*user_gesture=*/true);
  const gfx::Rect initial_bounds = GetInitialLittleDaoBounds(profile);
  params.initial_bounds = initial_bounds;
  params.can_resize = true;
  params.omit_from_session_restore = true;

  // Set flag before Browser::Create so BrowserView can detect Little Dao
  // during construction.
  g_creating_little_dao = true;
  Browser* browser = Browser::Create(params);
  g_creating_little_dao = false;
  if (!browser)
    return nullptr;

  LittleDaoBrowserTracker::Get().Insert(browser);
  RestoreLittleDaoWindowBounds(browser, initial_bounds);
  return browser;
}
```

Keep the helper and `LittleDaoBrowserTracker` inside the anonymous namespace.

- [ ] **Step 3: Update OpenInLittleDao() to use the helper**

Replace `DaoLittleDaoController::OpenInLittleDao()` with:

```cpp
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
```

- [ ] **Step 4: Implement ExtractActiveTabToLittleDao()**

Add this method after `OpenInLittleDao()`:

```cpp
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

  if (source_model->count() == 1) {
    chrome::AddTabAt(source_browser, GURL("about:blank"), -1, true);
  }

  const int source_index =
      source_model->GetIndexOfWebContents(original_contents);
  if (source_index == TabStripModel::kNoTab)
    return nullptr;

  std::unique_ptr<content::WebContents> contents =
      source_model->DetachWebContentsAtForInsertion(source_index);
  if (!contents)
    return nullptr;

  Browser* little_dao_browser = CreateLittleDaoBrowser(profile);
  if (!little_dao_browser) {
    source_model->InsertWebContentsAt(-1, std::move(contents),
                                      AddTabTypes::ADD_ACTIVE);
    return nullptr;
  }

  little_dao_browser->tab_strip_model()->InsertWebContentsAt(
      -1, std::move(contents), AddTabTypes::ADD_ACTIVE);
  little_dao_browser->window()->Show();
  little_dao_browser->window()->Activate();
  return little_dao_browser;
}
```

- [ ] **Step 5: Run the focused controller tests**

Run:

```bash
npm run import
npm run test -- --gtest_filter='DaoLittleDaoExtractionBrowserTest.*'
```

Expected: `DaoLittleDaoExtractionBrowserTest.*` passes.

- [ ] **Step 6: Optional checkpoint commit**

Only run if the latest user message explicitly authorizes this exact git action:

```bash
git add src/dao/browser/ui/views/little_dao/dao_little_dao_controller.h \
  src/dao/browser/ui/views/little_dao/dao_little_dao_controller.cc \
  src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "feat(little-dao): extract active tab into popup"
```

## Task 3: Add Localized Control Center Strings

**Files:**
- Modify: `src/dao/browser/strings/dao_strings.grd`

- [ ] **Step 1: Add source strings**

In the `<!-- Control Center -->` section of `src/dao/browser/strings/dao_strings.grd`, add these messages after `IDS_DAO_CONTROL_CENTER_MANAGE_EXTENSIONS`:

```xml
      <message name="IDS_DAO_CONTROL_CENTER_MINI_DAO" desc="Label and accessible name for the Control Center utility button that moves the current tab into a Mini Dao independent window.">
        Mini Dao
      </message>
      <message name="IDS_DAO_CONTROL_CENTER_MINI_DAO_FAILED_TOAST" desc="Toast shown when the user clicks the Control Center Mini Dao button but the current tab cannot be moved into a Mini Dao window.">
        Could not open this tab in Mini Dao
      </message>
```

- [ ] **Step 2: Refresh i18n skeletons**

Run:

```bash
tsx scripts/i18n-bootstrap.ts
```

Expected: the command exits with status 0. It may update generated locale skeleton files; do not run `i18n.sh`.

- [ ] **Step 3: Optional checkpoint commit**

Only run if the latest user message explicitly authorizes this exact git action:

```bash
git add src/dao/browser/strings/dao_strings.grd src/dao/browser/strings/translations
git commit -m "feat(control-center): add mini dao strings"
```

## Task 4: Wire The Control Center Mini Dao Button

**Files:**
- Modify: `src/dao/browser/ui/views/dao_control_center_utility_section.h`
- Modify: `src/dao/browser/ui/views/dao_control_center_utility_section.cc`

- [ ] **Step 1: Add the private click handler declaration**

In `src/dao/browser/ui/views/dao_control_center_utility_section.h`, add:

```cpp
  void OnMiniDaoClicked();
```

Place it between `OnQrClicked()` and `OnLockClicked()`.

- [ ] **Step 2: Add required includes**

In `src/dao/browser/ui/views/dao_control_center_utility_section.cc`, add these includes:

```cpp
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"
#include "ui/base/l10n/l10n_util.h"
```

- [ ] **Step 3: Add the button to the utility row**

In the constructor, insert this block between the QR Code and Security buttons:

```cpp
  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MINI_DAO),
          LucideIcon::kExternalLink,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnMiniDaoClicked,
              base::Unretained(this)))
          .release()));
```

- [ ] **Step 4: Implement the click handler**

Add this method after `OnQrClicked()`:

```cpp
void DaoControlCenterUtilitySection::OnMiniDaoClicked() {
  if (!popup_) {
    return;
  }

  Browser* browser = popup_->browser();
  popup_->Hide();

  Browser* little_dao_browser =
      DaoLittleDaoController::ExtractActiveTabToLittleDao(browser);
  if (little_dao_browser || !browser) {
    return;
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->dao_toast()) {
    return;
  }

  browser_view->dao_toast()->ShowToast(l10n_util::GetStringUTF16(
      IDS_DAO_CONTROL_CENTER_MINI_DAO_FAILED_TOAST));
}
```

- [ ] **Step 5: Keep the existing buttons unchanged**

Confirm the utility row still constructs `Share`, `QR Code`, `Security`, and `More` exactly once each. Do not localize the existing hardcoded labels in this task; that is a separate cleanup outside this feature.

- [ ] **Step 6: Run import**

Run:

```bash
npm run import
```

Expected: source files and strings copy into `engine/src` without patch conflicts.

- [ ] **Step 7: Optional checkpoint commit**

Only run if the latest user message explicitly authorizes this exact git action:

```bash
git add src/dao/browser/ui/views/dao_control_center_utility_section.h \
  src/dao/browser/ui/views/dao_control_center_utility_section.cc \
  src/dao/browser/strings/dao_strings.grd src/dao/browser/strings/translations
git commit -m "feat(control-center): add mini dao utility action"
```

## Task 5: Add Control Center Button Coverage

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Add a generic accessible-name button finder**

Near `FindImageButtonWithAccessibleName()`, add:

```cpp
views::Button* FindButtonWithAccessibleName(
    views::View* root,
    std::u16string_view accessible_name) {
  if (!root) {
    return nullptr;
  }
  if (auto* button = views::AsViewClass<views::Button>(root);
      button && button->GetAccessibleName() == accessible_name) {
    return button;
  }
  for (views::View* child : root->children()) {
    if (auto* button = FindButtonWithAccessibleName(child, accessible_name)) {
      return button;
    }
  }
  return nullptr;
}
```

- [ ] **Step 2: Add a Control Center smoke test for the button**

Add this test in the `DaoControlCenterPopupBrowserTest` section after `PopupExists`:

```cpp
IN_PROC_BROWSER_TEST_F(DaoControlCenterPopupBrowserTest,
                       MiniDaoButtonExistsInMainUtilityRow) {
  auto* popup = GetBrowserView(browser())->dao_control_center_popup();
  ASSERT_NE(nullptr, popup);

  popup->ShowAt(gfx::Point(100, 100));
  views::Button* mini_dao_button = FindButtonWithAccessibleName(
      popup,
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MINI_DAO));
  ASSERT_NE(nullptr, mini_dao_button);
  EXPECT_TRUE(mini_dao_button->GetVisible());

  popup->Hide();
}
```

- [ ] **Step 3: Add a click-through behavior test**

Add this test after `MiniDaoButtonExistsInMainUtilityRow`:

```cpp
IN_PROC_BROWSER_TEST_F(DaoControlCenterPopupBrowserTest,
                       MiniDaoButtonExtractsActiveTabAndHidesPopup) {
  TabStripModel* source_model = browser()->tab_strip_model();
  ASSERT_NE(nullptr, source_model);
  chrome::AddTabAt(browser(), GURL("data:text/html,control-center-mini-dao"),
                   -1, true);
  content::WebContents* original_contents =
      source_model->GetActiveWebContents();
  ASSERT_NE(nullptr, original_contents);
  ASSERT_TRUE(content::WaitForLoadStop(original_contents));

  auto* popup = GetBrowserView(browser())->dao_control_center_popup();
  ASSERT_NE(nullptr, popup);
  popup->ShowAt(gfx::Point(100, 100));
  views::Button* mini_dao_button = FindButtonWithAccessibleName(
      popup,
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MINI_DAO));
  ASSERT_NE(nullptr, mini_dao_button);

  BrowserAddedRecorder added_recorder;
  mini_dao_button->NotifyClick(ui::MouseEvent(
      ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
      ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
      ui::EF_LEFT_MOUSE_BUTTON));

  EXPECT_FALSE(popup->GetVisible());
  ASSERT_EQ(1u, added_recorder.added_count());
  Browser* little_dao_browser = added_recorder.added_browser_at(0);
  ASSERT_NE(nullptr, little_dao_browser);
  EXPECT_TRUE(
      dao::DaoLittleDaoController::IsLittleDaoWindow(little_dao_browser));
  EXPECT_EQ(original_contents,
            little_dao_browser->tab_strip_model()->GetActiveWebContents());

  BrowserRemovedWaiter removed(little_dao_browser);
  little_dao_browser->window()->Close();
  removed.Wait();
}
```

If `views::Button::NotifyClick()` is not accessible in this test build, replace the click line with:

```cpp
views::test::ButtonTestApi(mini_dao_button).NotifyClick(ui::MouseEvent(
    ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
    ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
    ui::EF_LEFT_MOUSE_BUTTON));
```

- [ ] **Step 4: Run focused Control Center tests**

Run:

```bash
npm run import
npm run test -- --gtest_filter='DaoControlCenterPopupBrowserTest.MiniDaoButton*'
```

Expected: the new Control Center Mini Dao tests pass.

- [ ] **Step 5: Optional checkpoint commit**

Only run if the latest user message explicitly authorizes this exact git action:

```bash
git add src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "test(control-center): cover mini dao utility button"
```

## Task 6: Final Focused Verification

**Files:**
- Read-only verification across touched files.

- [ ] **Step 1: Run the focused feature test set**

Run:

```bash
npm run import
npm run test -- --gtest_filter='DaoLittleDaoExtractionBrowserTest.*:DaoControlCenterPopupBrowserTest.MiniDaoButton*'
```

Expected: all selected tests pass.

- [ ] **Step 2: Run compile confirmation when requested**

Run only when compile confirmation is needed:

```bash
npm run rebuild
```

Expected: the command exits with status 0. This is the only allowed compile-confirmation command for this repository.

- [ ] **Step 3: Inspect the diff**

Run:

```bash
git diff -- src/dao/browser/ui/views/little_dao/dao_little_dao_controller.h \
  src/dao/browser/ui/views/little_dao/dao_little_dao_controller.cc \
  src/dao/browser/ui/views/dao_control_center_utility_section.h \
  src/dao/browser/ui/views/dao_control_center_utility_section.cc \
  src/dao/browser/strings/dao_strings.grd \
  src/dao/browser/ui/views/dao_browser_browsertest.cc
```

Expected:

- `DaoLittleDaoController` owns all `WebContents` transfer logic.
- `DaoControlCenterUtilitySection` only wires UI to the controller and toast.
- No source file under `engine/` is edited directly.
- New user-visible text comes from `dao_strings.grd`.
- No unrelated refactors or formatting churn.

- [ ] **Step 4: Optional final commit**

Only run if the latest user message explicitly authorizes this exact git action:

```bash
git add src/dao/browser/ui/views/little_dao/dao_little_dao_controller.h \
  src/dao/browser/ui/views/little_dao/dao_little_dao_controller.cc \
  src/dao/browser/ui/views/dao_control_center_utility_section.h \
  src/dao/browser/ui/views/dao_control_center_utility_section.cc \
  src/dao/browser/strings/dao_strings.grd src/dao/browser/strings/translations \
  src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "feat(control-center): extract tabs to mini dao"
```

## Self-Review Notes

- Spec coverage: the plan covers one-click Control Center entry, live `WebContents` movement, last-tab replacement, Little Dao reuse, `Open in Dao` return compatibility, i18n strings, failure toast, and focused tests.
- Placeholder scan: no `TBD`, `TODO`, or unspecified implementation steps remain.
- Type consistency: the planned public method is consistently named `ExtractActiveTabToLittleDao(Browser*)`; the Control Center calls the same method; tests use the same method and localized string IDs.
