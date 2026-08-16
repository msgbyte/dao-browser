# Mini Dao Download Card Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a bottom-left download progress card in Mini Dao for downloads triggered by that Mini Dao window only.

**Architecture:** Add a Mini Dao-only C++ Views overlay that observes the profile `DownloadManager`, filters downloads by the original triggering `WebContents`, and renders active progress rows. Hook the overlay into the existing Little Dao `BrowserView` construction and popup layout path.

**Tech Stack:** Chromium C++ Views, `download::AllDownloadItemNotifier`, `content::DownloadItemUtils`, Dao i18n strings, focused browser tests.

## Global Constraints

- Communicate with the user in Chinese; source code, comments, tests, and docs stay English.
- Do not edit `engine/` directly; update canonical `src/dao/` and `src/patches/`, then run `npm run import`.
- Do not run direct Chromium build tools; compile confirmation is only `npm run rebuild`.
- Do not run state-changing git commands without explicit latest-user authorization.
- Do not hardcode user-facing copy; use `dao_strings.grd`.

---

### Task 1: Add Mini Dao Download Card Tests

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

**Interfaces:**
- Consumes: future `BrowserView::dao_mini_dao_download_card()`.
- Produces: browser tests that fail before the card exists and pass after implementation.

- [ ] **Step 1: Write failing tests**

Add tests near `DaoLittleDaoViewBrowserTest`:

```cpp
IN_PROC_BROWSER_TEST_F(DaoLittleDaoViewBrowserTest,
                       MiniDaoCreatesDownloadCard) {
  Browser* little_dao_browser = dao::DaoLittleDaoController::OpenInLittleDao(
      browser()->profile(), GURL("data:text/html,mini-download-card"));
  ASSERT_NE(nullptr, little_dao_browser);

  BrowserView* little_browser_view = GetBrowserView(little_dao_browser);
  ASSERT_NE(nullptr, little_browser_view);
  EXPECT_NE(nullptr, little_browser_view->dao_mini_dao_download_card());
  EXPECT_FALSE(little_browser_view->dao_mini_dao_download_card()->GetVisible());

  BrowserRemovedWaiter removed(little_dao_browser);
  little_dao_browser->window()->Close();
  removed.Wait();
}

IN_PROC_BROWSER_TEST_F(DaoLittleDaoViewBrowserTest,
                       RegularBrowserHasNoMiniDaoDownloadCard) {
  EXPECT_EQ(nullptr, GetBrowserView(browser())->dao_mini_dao_download_card());
}
```

- [ ] **Step 2: Verify red**

Run a compile/test check when available:

```bash
npm run import
npm run rebuild
```

Expected: compile fails because `dao_mini_dao_download_card()` is not declared.

### Task 2: Implement Download Card View

**Files:**
- Create: `src/dao/browser/ui/views/little_dao/dao_mini_dao_download_card_view.h`
- Create: `src/dao/browser/ui/views/little_dao/dao_mini_dao_download_card_view.cc`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`
- Modify: `src/dao/browser/strings/dao_strings.grd`
- Modify: `src/dao/browser/strings/translations/dao_strings_zh-CN.xtb`

**Interfaces:**
- Produces: `dao::DaoMiniDaoDownloadCardView`, with constructor `explicit DaoMiniDaoDownloadCardView(Browser* browser)`, `bool HasActiveDownloadsForTesting() const`, and `void CancelDownloadForTesting(int id)`.

- [ ] **Step 1: Add view skeleton and source list entries**

Create the class as a `views::View` and add it to `dao_browser_ui_sources`.

- [ ] **Step 2: Add download observation and filtering**

Use `download::AllDownloadItemNotifier` against `browser_->profile()->GetDownloadManager()`. Include an item only when it is `IN_PROGRESS` and `content::DownloadItemUtils::GetOriginalWebContents(item)` equals the active Mini Dao `WebContents`; if original is null, fall back to `GetWebContents(item)`.

- [ ] **Step 3: Add card rendering**

Render up to two rows with filename, speed, percent bar, and cancel button. Hide the view when no matching rows exist.

- [ ] **Step 4: Add localized strings**

Add English strings for title, cancel accessible name, and overflow summary. Add hand-authored zh-CN translations.

### Task 3: Wire Into Mini Dao BrowserView

**Files:**
- Modify: `src/patches/chrome/browser/ui/views/frame/browser_view.h.patch`
- Modify: `src/patches/chrome/browser/ui/views/frame/browser_view.cc.patch`
- Modify: `src/patches/chrome/browser/ui/views/frame/layout/browser_view_popup_layout_impl.cc.patch`

**Interfaces:**
- Consumes: `DaoMiniDaoDownloadCardView`.
- Produces: `BrowserView::dao_mini_dao_download_card()` and Mini Dao overlay layout.

- [ ] **Step 1: Add forward declaration, getter, and raw_ptr**

Patch `BrowserView` with `dao::DaoMiniDaoDownloadCardView* dao_mini_dao_download_card()`.

- [ ] **Step 2: Create the view only in Little Dao windows**

In the existing Little Dao construction branch, add the download card before the site center and command bar overlays.

- [ ] **Step 3: Layout the card**

In `BrowserViewPopupLayoutImpl`, lay it out over `browser_params.visual_client_area`, so the view can position its inner card in the bottom-left corner.

### Task 4: Verify

**Files:**
- Generated/synced: `engine/src/...` via import only.

**Interfaces:**
- Consumes: all prior tasks.
- Produces: imported patch state and verification output.

- [ ] **Step 1: Apply canonical changes into engine**

Run:

```bash
npm run import
```

Expected: import completes without patch failures.

- [ ] **Step 2: Compile confirmation**

Run:

```bash
npm run rebuild
```

Expected: build completes. If it fails with C++ errors, fix them in canonical files/patches and rerun `npm run import` then `npm run rebuild`.

- [ ] **Step 3: Focused behavior test**

Run the narrowest available browser test filter after rebuild:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoLittleDaoViewBrowserTest.*MiniDaoDownloadCard*"
```

Expected: Mini Dao card tests pass.
