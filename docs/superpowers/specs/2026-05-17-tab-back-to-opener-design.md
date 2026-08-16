# Tab Back-to-Opener: Parent-aware Back Navigation

**Date**: 2026-05-17
**Status**: Design

## Problem

Pages opened via "Open Link in New Tab" or "Open Link in New Window" from a web page link have no in-tab history. Pressing Back on these tabs does nothing, even though the user's mental model is "go back to where I came from". The expected behavior:

1. When a new tab is opened from a link in another tab, remember the originating tab as its parent.
2. When the Back button is pressed on a tab:
   - If the tab has in-page history, do a normal back navigation.
   - Otherwise, if the tab has a valid parent (parent still exists and has not navigated away from the URL it had when the relationship was established), close the current tab and activate the parent.

## Background

Chromium already implements this exact behavior in `chrome/browser/ui/tabs/back_to_opener/back_to_opener_controller.{h,cc}`. The controller:

- Hooks `ReadyToCommitNavigation` and reads the navigation's initiator frame to identify the opener `WebContents`. Captures the opener's URL at relationship-establishment time.
- Observes the opener `WebContents` via an inner `OpenerWebContentsObserver`. Clears the relationship when the opener is destroyed or navigates away from the captured URL.
- `GoBackToOpener()` calls `WebContents::ClosePage()` then installs a `TabCloseObserver` (a `WebContentsUserData`) on the tab being closed. When the WebContents is actually destroyed (after any `beforeunload` prompts), the observer activates the opener tab.
- `CanGoBackToOpener()` additionally requires the destination tab not be pinned.

`chrome::CanGoBack(WebContents*)` and `chrome::GoBack(WebContents*)` in `browser_commands.cc` already consult `BackToOpenerController` after the regular navigation controller path.

Two things prevent this from working in Dao today:

1. The feature is gated behind `base::Feature kBackToOpener` in `chrome/browser/ui/tabs/features.cc`, currently `FEATURE_DISABLED_BY_DEFAULT`. With the flag off, `BackToOpenerController` is not created for new tabs, so no opener relationship is recorded.
2. `DaoAddressBarView::OnBackButtonPressed()` calls `contents->GetController().GoBack()` directly, bypassing `chrome::GoBack()` and therefore the back-to-opener fallback. `UpdateNavButtonEnabled()` similarly only checks `NavigationController::CanGoBack()`.

## Design

### Enable the upstream feature

Patch `engine/src/chrome/browser/ui/tabs/features.cc` (via `src/patches/chrome/browser/ui/tabs/features.cc.patch`) to flip `kBackToOpener` to `FEATURE_ENABLED_BY_DEFAULT`.

Because the controller is wired up by Chromium's existing `TabFeatures` registration (gated on this flag), no further glue is needed to:

- Capture the opener relationship when a new tab is created from any navigation that has an initiator frame. This covers "Open Link in New Tab", "Open Link in New Window", `Cmd/Ctrl+Click`, middle-click, and `target="_blank"` links — all of which carry an initiator frame on the resulting navigation.
- Clear the relationship when the parent tab is destroyed or navigates away from its original URL.
- Honor pinned-state and other invalidation rules.

### Route Dao's back button through `chrome::GoBack` / `chrome::CanGoBack`

In `src/dao/browser/ui/views/dao_address_bar_view.cc`:

- `OnBackButtonPressed()`: replace the direct `contents->GetController().GoBack()` call with `chrome::GoBack(browser_, WindowOpenDisposition::CURRENT_TAB)`. This will try in-tab back navigation first, then fall back to `BackToOpenerController::GoBackToOpener(web_contents)`.
- `UpdateNavButtonEnabled()`: replace `contents->GetController().CanGoBack()` with `chrome::CanGoBack(browser_)`. This makes the Back button enabled in the "only parent available" case so the user has something to click.

`browser_` (the `Browser*` already held by `DaoAddressBarView`) is the canonical argument for the `chrome::` command helpers. The active WebContents is resolved through `browser_->tab_strip_model()->GetActiveWebContents()` inside the helpers, matching the rest of Chromium.

Forward Button is unaffected — back-to-opener has no forward equivalent.

### Out of scope (explicitly not changed)

- `dao_sidebar_ui.cc` `InsertWebContentsAt` call sites: these handle drag-drop / tab moves between windows or workspaces. No parent semantics there.
- `dao_command_bar_view.cc`, `dao_split_view.cc`, `dao_little_dao_controller.cc` new-tab paths: these create blank tabs from user actions (Cmd+T, command bar, new window, Little Dao). No parent relationship is appropriate.
- Sidebar tab item UI (`dao_tab_item.ts`): no new icon or menu entry. The behavior is invisible until the user presses Back on a no-history child tab.
- Internationalization: no new user-facing strings.

## Testing

Add browser tests to `src/dao/browser/ui/views/dao_browser_browsertest.cc`. Each test uses the embedded test server to load a page with a link, then exercises one path:

1. **Ctrl+Click opens new tab; Back closes it and activates parent.** Open a link with `WindowOpenDisposition::NEW_BACKGROUND_TAB`, switch to it, click `dao_address_bar()->back_button_for_testing()` (or invoke `chrome::GoBack` directly), wait for tab destruction, assert active tab is the parent.
2. **Parent navigates away, then Back on child does nothing destructive.** Open a link in a new tab, navigate the parent to a different URL, switch to the child, assert `chrome::CanGoBack` is false (or Back button disabled), assert pressing Back does not close the child.
3. **Parent closed, then Back on child does nothing destructive.** Open a link in a new tab, close the parent, switch to the child, assert Back does not close the child.
4. **In-tab history takes precedence over parent.** Open a link in a new tab, navigate the child to a second URL, press Back, assert the child stays open and navigates back to the first URL (not closed).
5. **Pinned child does not back-to-opener.** Open a link in a new tab, pin it, assert `chrome::CanGoBack` is false on the pinned child even when parent is valid.

Tests should use `TestNavigationObserver` and `content::WebContentsDestroyedWatcher` to handle the asynchronous `ClosePage()` → destruction sequence used by `TabCloseObserver`.

## Risks

- **Feature flag flip surface**: enabling `kBackToOpener` enables it globally, including for non-Dao code paths (e.g. any non-Dao back-button surface inside Chromium would also start using it). Acceptable — the behavior is upstream-designed and benign.
- **Behavior change on existing back button**: users who currently press Back on a no-history "Open in New Tab" page and expect nothing will now see the tab close. This matches Arc / mobile-browser convention and the user's request.
- **Pinned-tab interaction**: upstream forbids back-to-opener on pinned tabs but preserves the relationship (so unpinning re-enables it). This is the documented upstream behavior; no Dao-specific change.

## Files Touched

- `src/patches/chrome/browser/ui/tabs/features.cc.patch` — new patch flipping `kBackToOpener` default.
- `src/dao/browser/ui/views/dao_address_bar_view.cc` — route Back through `chrome::GoBack` / `chrome::CanGoBack`.
- `src/dao/browser/ui/views/dao_address_bar_view.h` — add `#include "chrome/browser/ui/browser_commands.h"` if not already present (or include only in the .cc).
- `src/dao/browser/ui/views/dao_browser_browsertest.cc` — new test cases.
