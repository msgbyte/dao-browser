# Tab Back-to-Opener Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pressing Back on a tab opened from a link in another tab — when the child has no in-tab history — closes the child and activates the parent tab.

**Architecture:** Reuse Chromium's existing `back_to_opener::BackToOpenerController` (already wired into `TabFeatures` and consulted by `chrome::CanGoBack` / `chrome::GoBack`). Two changes unblock it for Dao: (1) flip the upstream `kBackToOpener` feature flag to enabled-by-default via a patch; (2) route Dao's address-bar Back button through `chrome::GoBack` / `chrome::CanGoBack` instead of calling `NavigationController` directly, so the opener-fallback path runs.

**Tech Stack:** C++ (Chromium views), `chrome::` browser commands, `base::Feature`, `browser_tests` (`InProcessBrowserTest` + `embedded_test_server` + `TabAddedWaiter` + `WebContentsDestroyedWatcher`).

**Spec:** [`docs/superpowers/specs/2026-05-17-tab-back-to-opener-design.md`](../specs/2026-05-17-tab-back-to-opener-design.md)

---

## File Structure

- **Create** `src/patches/chrome/browser/ui/tabs/features.cc.patch` — single-line flip of `kBackToOpener` default to `FEATURE_ENABLED_BY_DEFAULT`.
- **Modify** `src/dao/browser/ui/views/dao_address_bar_view.cc` — add `#include "chrome/browser/ui/browser_commands.h"`; route `OnBackButtonPressed()` and the back-button enabled state in `UpdateNavButtonEnabled()` through `chrome::GoBack` / `chrome::CanGoBack`. Forward button stays as-is.
- **Modify** `src/dao/browser/ui/views/dao_browser_browsertest.cc` — add a new test fixture `DaoBackToOpenerBrowserTest` with five `IN_PROC_BROWSER_TEST_F` cases covering the spec's testing matrix.

No header changes are needed: `dao_address_bar_view.h` already exposes `browser_` (a `raw_ptr<Browser>`), and `dao_browser_browsertest.cc` already includes `browser_commands.h`, `tab_strip_model.h`, `ui_test_utils.h`, `browser_test_utils.h`, `embedded_test_server.h`, and `mock_host_resolver.h`.

---

## Task 1: Enable upstream `kBackToOpener` feature flag

**Files:**
- Create: `src/patches/chrome/browser/ui/tabs/features.cc.patch`

The patch flips a single `BASE_FEATURE` default. The original line in `engine/src/chrome/browser/ui/tabs/features.cc` reads:

```cpp
BASE_FEATURE(kBackToOpener, base::FEATURE_DISABLED_BY_DEFAULT);
```

It must become:

```cpp
BASE_FEATURE(kBackToOpener, base::FEATURE_ENABLED_BY_DEFAULT);
```

- [ ] **Step 1: Create the patch file**

Write `src/patches/chrome/browser/ui/tabs/features.cc.patch` with the following contents (a unified diff; context lines are the surrounding comment and adjacent BASE_FEATURE lines so `git apply` can locate the hunk reliably):

```diff
diff --git a/chrome/browser/ui/tabs/features.cc b/chrome/browser/ui/tabs/features.cc
--- a/chrome/browser/ui/tabs/features.cc
+++ b/chrome/browser/ui/tabs/features.cc
@@
 // Enables Back-to-Opener behavior, allowing users to press the back button in a
 // newly opened tab to close that tab and return focus to the opener tab.
-BASE_FEATURE(kBackToOpener, base::FEATURE_DISABLED_BY_DEFAULT);
+// Dao: enabled by default so newly opened tabs from initiator-frame
+// navigations (right-click "Open in New Tab", middle-click, target="_blank")
+// can be closed via the Back button to return to the opener.
+BASE_FEATURE(kBackToOpener, base::FEATURE_ENABLED_BY_DEFAULT);
```

Note: the `@@` hunk header has no line numbers — `git apply` accepts this form. If `npm run import` fails locating the hunk, regenerate the patch from `engine/src` with `git diff > .../features.cc.patch` after a manual edit (see Memory: "Patch export pitfall — use raw `git diff`").

- [ ] **Step 2: Apply patches and confirm the patch is clean**

Run: `npm run import`
Expected: completes without "patch does not apply" errors. The script auto-detects already-applied patches via reverse-check, so re-running is safe.

- [ ] **Step 3: Verify the flag is enabled in the built binary's source tree**

Run: `grep -n "kBackToOpener" engine/src/chrome/browser/ui/tabs/features.cc`
Expected: the line in `engine/src` now reads `BASE_FEATURE(kBackToOpener, base::FEATURE_ENABLED_BY_DEFAULT);`.

- [ ] **Step 4: Build to confirm no compile regression from the flag flip**

Run: `npm run build:debug`
Expected: build succeeds. (No code change yet to the Dao Back button — at this point pressing Back on a no-history child still does nothing because the Dao address bar bypasses `chrome::GoBack`. That is fixed in Task 3.)

- [ ] **Step 5: Commit**

```bash
git add src/patches/chrome/browser/ui/tabs/features.cc.patch
git commit -m "feat(tabs): enable upstream kBackToOpener feature by default"
```

---

## Task 2: Write failing browser tests for Back-to-Opener via Dao back button

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

These tests intentionally exercise the wiring through Dao's address-bar Back button by invoking `chrome::GoBack` / `chrome::CanGoBack` (which is what the Dao back-button code will call after Task 3). Writing them first lets Task 3 prove the wire-up works.

The fixture serves files from `chrome/test/data` and reuses the existing upstream `back_to_opener_opener.html` (a page containing `<a id="link" href="/title1.html" target="_blank">`), so no new fixture HTML is needed.

- [ ] **Step 1: Add the fixture and tests at the end of `dao_browser_browsertest.cc`**

Append the following block to the file (use the existing `namespace dao` if the file wraps tests in one; otherwise keep these tests in the global namespace like the other fixtures in this file). The block is self-contained — all the includes it needs (`browser_commands.h`, `tab_strip_model.h`, `ui_test_utils.h`, `browser_test_utils.h`, `embedded_test_server.h`, `mock_host_resolver.h`, `web_contents.h`, `browser_test.h`) are already present at the top of the file.

```cpp
// =============================================================================
// Back-to-Opener: pressing Back on a tab with no in-tab history but a valid
// opener closes the tab and activates the opener.
// =============================================================================

class DaoBackToOpenerBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  // Loads back_to_opener_opener.html in the current active tab, clicks the
  // target="_blank" link, and waits for the resulting child tab to load.
  // Returns the child WebContents.
  content::WebContents* OpenChildFromLink(content::WebContents* opener) {
    EXPECT_TRUE(content::WaitForLoadStop(opener));
    ui_test_utils::TabAddedWaiter tab_waiter(browser());
    EXPECT_TRUE(content::ExecJs(opener,
                                "document.getElementById('link').click();"));
    content::WebContents* child = tab_waiter.Wait();
    EXPECT_TRUE(content::WaitForLoadStop(child));
    return child;
  }
};

// 1. target="_blank" link opens a new tab; Back on the child closes it and
//    activates the parent.
IN_PROC_BROWSER_TEST_F(DaoBackToOpenerBrowserTest,
                       BackClosesChildAndActivatesParent) {
  GURL opener_url =
      embedded_test_server()->GetURL("/back_to_opener_opener.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), opener_url));
  content::WebContents* opener =
      browser()->tab_strip_model()->GetActiveWebContents();
  int opener_index =
      browser()->tab_strip_model()->GetIndexOfWebContents(opener);

  content::WebContents* child = OpenChildFromLink(opener);
  ASSERT_NE(child, opener);

  // Activate the child tab (TabAddedWaiter does not switch focus).
  int child_index = browser()->tab_strip_model()->GetIndexOfWebContents(child);
  browser()->tab_strip_model()->ActivateTabAt(child_index);

  // Child has no in-tab history, so Back must fall back to opener.
  EXPECT_TRUE(chrome::CanGoBack(browser()));

  content::WebContentsDestroyedWatcher close_watcher(child);
  chrome::GoBack(browser(), WindowOpenDisposition::CURRENT_TAB);
  close_watcher.Wait();

  EXPECT_EQ(opener, browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(opener_index,
            browser()->tab_strip_model()->GetIndexOfWebContents(opener));
}

// 2. After parent navigates away from its original URL, Back on the child is
//    a no-op (CanGoBack returns false, child is not closed).
IN_PROC_BROWSER_TEST_F(DaoBackToOpenerBrowserTest,
                       ParentNavigatedAwayDisablesBack) {
  GURL opener_url =
      embedded_test_server()->GetURL("/back_to_opener_opener.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), opener_url));
  content::WebContents* opener =
      browser()->tab_strip_model()->GetActiveWebContents();

  content::WebContents* child = OpenChildFromLink(opener);
  ASSERT_NE(child, opener);

  // Navigate the parent away (still active in the strip).
  int opener_index =
      browser()->tab_strip_model()->GetIndexOfWebContents(opener);
  browser()->tab_strip_model()->ActivateTabAt(opener_index);
  GURL new_parent_url =
      embedded_test_server()->GetURL("other.com", "/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), new_parent_url));

  // Switch back to child and assert Back is disabled and a no-op.
  int child_index = browser()->tab_strip_model()->GetIndexOfWebContents(child);
  browser()->tab_strip_model()->ActivateTabAt(child_index);

  EXPECT_FALSE(chrome::CanGoBack(browser()));

  // Invoking GoBack with no valid back target must not close the child.
  chrome::GoBack(browser(), WindowOpenDisposition::CURRENT_TAB);
  // Brief flush in case GoBack posted any async work.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(child, browser()->tab_strip_model()->GetActiveWebContents());
}

// 3. After parent is closed, Back on the child is a no-op.
IN_PROC_BROWSER_TEST_F(DaoBackToOpenerBrowserTest,
                       ParentClosedDisablesBack) {
  GURL opener_url =
      embedded_test_server()->GetURL("/back_to_opener_opener.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), opener_url));
  content::WebContents* opener =
      browser()->tab_strip_model()->GetActiveWebContents();

  content::WebContents* child = OpenChildFromLink(opener);
  ASSERT_NE(child, opener);

  int opener_index =
      browser()->tab_strip_model()->GetIndexOfWebContents(opener);
  browser()->tab_strip_model()->CloseWebContentsAt(opener_index,
                                                   TabCloseTypes::CLOSE_NONE);

  int child_index = browser()->tab_strip_model()->GetIndexOfWebContents(child);
  browser()->tab_strip_model()->ActivateTabAt(child_index);

  EXPECT_FALSE(chrome::CanGoBack(browser()));

  chrome::GoBack(browser(), WindowOpenDisposition::CURRENT_TAB);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(child, browser()->tab_strip_model()->GetActiveWebContents());
}

// 4. In-tab history takes precedence: Back navigates back within the child,
//    does not close it.
IN_PROC_BROWSER_TEST_F(DaoBackToOpenerBrowserTest,
                       InTabHistoryTakesPrecedence) {
  GURL opener_url =
      embedded_test_server()->GetURL("/back_to_opener_opener.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), opener_url));
  content::WebContents* opener =
      browser()->tab_strip_model()->GetActiveWebContents();

  content::WebContents* child = OpenChildFromLink(opener);
  ASSERT_NE(child, opener);

  int child_index = browser()->tab_strip_model()->GetIndexOfWebContents(child);
  browser()->tab_strip_model()->ActivateTabAt(child_index);

  // Navigate child to a second URL so it has in-tab history.
  GURL child_second_url = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), child_second_url));
  ASSERT_TRUE(content::WaitForLoadStop(child));

  EXPECT_TRUE(chrome::CanGoBack(browser()));

  content::TestNavigationObserver nav_observer(child);
  chrome::GoBack(browser(), WindowOpenDisposition::CURRENT_TAB);
  nav_observer.Wait();

  // Child is still active and has navigated back to title1.html (the link
  // target from the opener page).
  EXPECT_EQ(child, browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(embedded_test_server()->GetURL("/title1.html"),
            child->GetLastCommittedURL());
}

// 5. Pinned child does not back-to-opener.
IN_PROC_BROWSER_TEST_F(DaoBackToOpenerBrowserTest, PinnedChildDoesNotGoBack) {
  GURL opener_url =
      embedded_test_server()->GetURL("/back_to_opener_opener.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), opener_url));
  content::WebContents* opener =
      browser()->tab_strip_model()->GetActiveWebContents();

  content::WebContents* child = OpenChildFromLink(opener);
  ASSERT_NE(child, opener);

  int child_index = browser()->tab_strip_model()->GetIndexOfWebContents(child);
  browser()->tab_strip_model()->SetTabPinned(child_index, true);
  // SetTabPinned may move the tab; re-fetch the index.
  child_index = browser()->tab_strip_model()->GetIndexOfWebContents(child);
  browser()->tab_strip_model()->ActivateTabAt(child_index);

  EXPECT_FALSE(chrome::CanGoBack(browser()));
}
```

Add `#include "content/public/test/test_navigation_observer.h"` and `#include "base/run_loop.h"` near the existing test includes if they are not yet present.

- [ ] **Step 2: Build the tests (they should compile but fail at runtime)**

Run: `npm run test:build`
Expected: build succeeds. The new `DaoBackToOpenerBrowserTest.*` symbols are linked into `browser_tests`.

- [ ] **Step 3: Run the new tests and watch them fail**

Run: `./engine/src/out/dao-debug/browser_tests --gtest_filter="DaoBackToOpenerBrowserTest.*"`
Expected at this point: **all five fail.** Reason: even with `kBackToOpener` enabled (Task 1), the active code path for Back is still inside `BackToOpenerController` only when the caller is `chrome::GoBack`. The tests above DO call `chrome::GoBack` directly, so once Task 1 is in they should actually pass — except that the *Dao back button* itself is not yet routed there. The tests here verify the wiring contract (`chrome::CanGoBack` / `chrome::GoBack` work end-to-end), which is exactly what Task 3 will plug into.

If they already pass after Task 1 — that is a green signal that the upstream feature works against our build. Move directly to Task 3.

- [ ] **Step 4: Commit the tests (even if green)**

```bash
git add src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "test: add Dao back-to-opener browser tests"
```

---

## Task 3: Route Dao address bar Back button through `chrome::GoBack`

**Files:**
- Modify: `src/dao/browser/ui/views/dao_address_bar_view.cc:1-40` (include block)
- Modify: `src/dao/browser/ui/views/dao_address_bar_view.cc:628-637` (`OnBackButtonPressed`)
- Modify: `src/dao/browser/ui/views/dao_address_bar_view.cc:682-696` (`UpdateNavButtonEnabled`)

The header (`dao_address_bar_view.h`) already declares `raw_ptr<Browser> browser_;`. No header change needed.

- [ ] **Step 1: Add the `chrome::` browser_commands include**

In `src/dao/browser/ui/views/dao_address_bar_view.cc`, add this include in alphabetical order with the other `chrome/browser/ui/...` includes near the top of the file:

```cpp
#include "chrome/browser/ui/browser_commands.h"
```

- [ ] **Step 2: Rewrite `OnBackButtonPressed` to use `chrome::GoBack`**

Replace the existing body of `DaoAddressBarView::OnBackButtonPressed()`:

```cpp
void DaoAddressBarView::OnBackButtonPressed() {
  if (!tab_strip_model_) {
    return;
  }
  content::WebContents* contents =
      tab_strip_model_->GetActiveWebContents();
  if (contents && contents->GetController().CanGoBack()) {
    contents->GetController().GoBack();
  }
}
```

with:

```cpp
void DaoAddressBarView::OnBackButtonPressed() {
  if (!browser_) {
    return;
  }
  // Routes through chrome::GoBack so that BackToOpenerController's fallback
  // (close child tab + activate opener) runs when the active tab has no
  // in-tab history but does have a valid opener relationship.
  if (chrome::CanGoBack(browser_)) {
    chrome::GoBack(browser_, WindowOpenDisposition::CURRENT_TAB);
  }
}
```

- [ ] **Step 3: Rewrite the back-button enabled state in `UpdateNavButtonEnabled`**

Replace the existing body of `DaoAddressBarView::UpdateNavButtonEnabled()`:

```cpp
void DaoAddressBarView::UpdateNavButtonEnabled() {
  if (!tab_strip_model_) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetActiveWebContents();
  bool can_back = contents && contents->GetController().CanGoBack();
  bool can_forward = contents && contents->GetController().CanGoForward();

  if (back_button_) {
    static_cast<NavIconButton*>(back_button_.get())->SetNavEnabled(can_back);
  }
  if (forward_button_) {
    static_cast<NavIconButton*>(forward_button_.get())->SetNavEnabled(can_forward);
  }
}
```

with:

```cpp
void DaoAddressBarView::UpdateNavButtonEnabled() {
  if (!tab_strip_model_) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetActiveWebContents();
  // Back is enabled when either in-tab history OR a valid opener fallback is
  // available. chrome::CanGoBack consults BackToOpenerController for the
  // latter. Forward has no opener equivalent.
  bool can_back = browser_ && chrome::CanGoBack(browser_);
  bool can_forward = contents && contents->GetController().CanGoForward();

  if (back_button_) {
    static_cast<NavIconButton*>(back_button_.get())->SetNavEnabled(can_back);
  }
  if (forward_button_) {
    static_cast<NavIconButton*>(forward_button_.get())->SetNavEnabled(can_forward);
  }
}
```

- [ ] **Step 4: Rebuild**

Run: `npm run rebuild`
Expected: import + debug build succeed.

- [ ] **Step 5: Re-run the new tests and confirm they pass**

Run: `./engine/src/out/dao-debug/browser_tests --gtest_filter="DaoBackToOpenerBrowserTest.*"`
Expected: all five pass.

- [ ] **Step 6: Run the full Dao test suite to confirm no regressions**

Run: `npm run test`
Expected: all `Dao*` tests green.

- [ ] **Step 7: Manual smoke test in the running browser**

Run: `npm run start`
In the launched window:
1. Navigate to `https://en.wikipedia.org/wiki/Main_Page`.
2. Right-click any in-page link → "Open Link in New Tab".
3. Switch to the new tab. Confirm the Back button is enabled.
4. Click Back. Expected: the new tab closes and focus returns to the Wikipedia parent tab.
5. Repeat with a middle-click and with a `target="_blank"` link to confirm both paths.
6. Try with Cmd+T (which has no initiator frame) — Back should remain disabled, the new blank tab should NOT close on Back. This verifies we didn't regress the no-parent case.

- [ ] **Step 8: Commit**

```bash
git add src/dao/browser/ui/views/dao_address_bar_view.cc
git commit -m "feat(address-bar): route Back button through chrome::GoBack for back-to-opener"
```

---

## Self-Review

Spec coverage:
- Enable upstream feature flag → Task 1.
- Route Dao Back button (`OnBackButtonPressed`) through `chrome::GoBack` → Task 3 step 2.
- Route Dao Back button enabled state (`UpdateNavButtonEnabled`) through `chrome::CanGoBack` → Task 3 step 3.
- Out-of-scope items (sidebar insert paths, command bar / split-view / little dao new-tab paths, sidebar tab item UI, i18n) → intentionally not touched in any task.
- Tests for the five scenarios → Task 2.
- Files Touched list (features.cc.patch, dao_address_bar_view.cc, dao_browser_browsertest.cc) → all covered. (Spec mentioned `dao_address_bar_view.h` as a maybe-include — confirmed not needed because the include lives in the .cc.)

Placeholder scan: no TBD / TODO / "add appropriate handling" placeholders. Every code-change step includes the full code.

Type consistency: `chrome::CanGoBack(Browser*)` and `chrome::GoBack(Browser*, WindowOpenDisposition)` signatures used consistently across Tasks 2 and 3. `tabs::TabInterface` / `BackToOpenerController` are not referenced by Dao code directly — we only consume them indirectly via `chrome::CanGoBack` / `chrome::GoBack`. `WindowOpenDisposition::CURRENT_TAB` is the canonical disposition used by Chromium's own Back button.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-17-tab-back-to-opener.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
