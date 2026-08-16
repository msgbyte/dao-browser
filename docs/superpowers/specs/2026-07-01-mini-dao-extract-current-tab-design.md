# Mini Dao Current Tab Extraction Design

## Summary

Add a first-class `Mini Dao` action to the Control Center utility row. When clicked, Dao moves the currently active tab into a Little Dao popup window instead of opening a fresh copy of the URL. The extracted page keeps its live `WebContents` state, including navigation history, scroll position, in-page state, media state, and session context.

The product-facing label is `Mini Dao`, while the implementation should reuse the existing `DaoLittleDaoController` and Little Dao window infrastructure.

## Goals

- Add a one-click Control Center utility action for extracting the active tab into a Mini Dao independent window.
- Preserve the live page instance by moving the active `WebContents`, not by navigating a new popup to the same URL.
- Keep the main browser window open. If the active tab is the source window's only tab, leave a replacement new tab behind.
- Reuse the existing Little Dao popup UI, bounds persistence, tracker, and `Open in Dao` return path.
- Keep UI wiring in the Control Center and tab/window lifecycle logic in `DaoLittleDaoController`.

## Non-Goals

- Do not create a second Mini Dao window system separate from Little Dao.
- Do not duplicate tabs or clone session state.
- Do not redesign the Little Dao top bar.
- Do not add a confirmation dialog for the normal one-click extraction path.
- Do not change external URL handling that already opens Little Dao windows.

## User Experience

The Control Center utility row becomes:

```text
Share / QR Code / Mini Dao / Security / More
```

Clicking `Mini Dao`:

1. Closes the Control Center popup.
2. Removes the active tab from the main browser sidebar.
3. Opens a Little Dao popup with the exact same live page instance.
4. Activates the Little Dao popup.
5. Leaves the original browser window open. If the source had one tab, a new blank tab remains in the main window.

The Little Dao window keeps its current top bar behavior. The existing `Open in Dao` button transfers the page back into a tabbed browser by using the existing `TransferToMainBrowser()` flow.

User-visible strings must be added through Dao's i18n path, not hardcoded in C++.

## Recommended Technical Approach

Add a controller entry point:

```cpp
static Browser* ExtractActiveTabToLittleDao(Browser* source_browser);
```

This method should live on `DaoLittleDaoController` because it owns Little Dao window creation, tracking, and return-to-browser behavior.

High-level flow:

1. Validate `source_browser`.
2. Reject extraction if `source_browser` is already a Little Dao window.
3. Read the active `WebContents` from `source_browser->tab_strip_model()`.
4. If the source browser has one tab, create a replacement new tab in the source browser before detaching the original page. Re-resolve the original `WebContents` index after adding the replacement tab.
5. Detach the original `WebContents` with `DetachWebContentsAtForInsertion()`.
6. Create a `Browser::TYPE_POPUP` using the same Little Dao creation path as `OpenInLittleDao()`.
7. Register the popup with the Little Dao tracker and restore its persisted bounds.
8. Insert the detached `WebContents` into the popup as the active tab.
9. Show and activate the popup.

The existing `OpenInLittleDao(Profile*, GURL)` should continue to serve external URL flows. If useful, factor shared creation code into a private helper so both URL-open and extraction paths use the same bounds, tracker, and `g_creating_little_dao` behavior.

## Control Center Integration

Extend `DaoControlCenterUtilitySection` with:

- A fifth `UtilityButton` labeled through `dao_strings.grd`.
- Use the existing `LucideIcon::kExternalLink` icon for the first implementation. It communicates opening the current page into a separate window without adding new icon maintenance work.
- `OnMiniDaoClicked()` that calls `DaoLittleDaoController::ExtractActiveTabToLittleDao(popup_->browser())`.

The Control Center should not manipulate `WebContents` directly. It should close the popup, call the controller, and surface user-triggered extraction failure through existing Dao toast infrastructure with localized copy.

The current utility buttons are 56 px wide and the Control Center card is 320 px wide. Five buttons still fit inside the current card padding, but labels should stay short. `Mini Dao` is the recommended visible label.

## Edge Cases

- No browser, no tab model, no active tab: do nothing. If the user explicitly clicked the Control Center action and extraction fails, show a localized failure toast.
- Source browser is already Little Dao: do nothing. The existing `Open in Dao` button is the supported direction from Little Dao back to the main browser.
- Source has one tab: create a replacement new tab before moving the original page, then detach the original page by re-resolved `WebContents` index.
- Popup creation fails after detach: reinsert the detached `WebContents` into the source browser if possible.
- Source profile is off-the-record: allow extraction if a normal Little Dao popup can already be created for that profile; do not persist bounds if prefs are unavailable, matching existing Little Dao preference behavior.
- Pinned or grouped tab: extraction should move the page out of the source tab strip. Do not attempt to preserve source tab grouping in the popup.

## Testing Plan

Controller browser tests should cover:

- Extracting an active tab creates a Little Dao popup and removes the tab from the source browser.
- The moved `WebContents` keeps the same pointer identity across extraction.
- Extracting the only source tab leaves a replacement tab in the source window.
- Extracting from a Little Dao window is rejected.
- The extracted Little Dao page can return to the main browser through `TransferToMainBrowser()`.

Control Center coverage should verify that the utility row can include the new button without breaking existing Share, QR Code, Security, and More actions.

For implementation verification, use focused browser tests first. If compilation confirmation is needed, run only `npm run rebuild` per project rules.

## Decisions

- Use `LucideIcon::kExternalLink` for the first implementation.
- Show a localized Dao toast when a user-triggered extraction attempt fails after the Control Center button is clicked.
