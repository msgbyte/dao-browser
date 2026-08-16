# Sidebar Stale Tabs Folder Design

## Status

Approved for design by the user on 2026-06-21.

## Goal

Add a sidebar context menu action that organizes stale ordinary tabs into a
folder named `stale`.

When the user right-clicks the sidebar background or any ordinary tab item, the
menu should include an action to move stale tabs into `stale`. If the folder
does not exist, Dao creates it. If it already exists, Dao reuses it.

## Definition Of Stale

A tab is stale when all of these are true:

- It is an ordinary unpinned tab.
- It is not the active tab.
- It has not been activated in the last 24 hours.
- It is not currently audible or muted as an active media surface.
- It is not locked by the Dao agent.

Pinned items, pinned Chromium tabs, favorites, and dormant pinned entries are
out of scope. The first version should not move the current active tab, even if
its last recorded activation timestamp is old because of restore or missing
metadata.

## Existing Context

The sidebar is a hybrid C++ Views plus Lit WebUI surface:

- C++ builds tab state in `DaoSidebarUIHandler::BuildSidebarState()`.
- `dao_sidebar_app.ts` receives `pinnedItems`, `pinnedTabs`, and
  `unpinnedTabs`.
- `FolderModel` owns sidebar folder mutations and JSON serialization.
- The folder model already only organizes `unpinnedTabs`.
- Tab item right-clicks flow from `dao-tab-item` to `dao-tab-list`, then to
  C++ through `showTabContextMenu`.
- Sidebar background right-clicks flow directly from `dao-sidebar-app` to C++
  through `showSidebarContextMenu`.

The current WebUI tab payload does not expose a last-activated timestamp, so
C++ needs to provide a reliable timestamp before the WebUI can classify stale
tabs.

## Recommended Approach

Use C++ for stale metadata and WebUI for folder mutation.

C++ should track or expose each tab's most recent activation time and include it
in `TabData`, for example as `lastActiveTimeMs`. The WebUI should filter the
current `unpinnedTabs_` list using the 24-hour threshold, then mutate
`FolderModel` by finding or creating the `stale` folder and moving matching tab
refs into it.

This keeps responsibilities aligned with the current architecture:

- C++ knows tab lifecycle and activation events.
- WebUI already owns folder structure, reconciliation, and persistence.
- The folder JSON format stays owned by the WebUI model instead of being parsed
  and rewritten from native menu code.

## Alternatives Considered

### Frontend-Only Activation Tracking

The WebUI could listen to `activeTabChanged` and store timestamps itself. This
is simple, but it breaks when the sidebar WebUI reloads, the browser restores a
session, or multiple windows are involved. It also cannot reliably classify
tabs that existed before the sidebar page loaded.

### Native Folder JSON Mutation

C++ could execute the menu action by editing the folder JSON directly. This
would make the menu command self-contained, but it would duplicate
`FolderModel` logic in C++ and couple native code to a WebUI-owned data shape.
That is a poor fit for the current sidebar design.

## User Experience

Add one menu item to both relevant menus:

- `Move Stale Tabs to "stale"`

The item should appear in:

- Sidebar background context menu.
- Ordinary tab item context menu.

Executing the item should immediately organize matching tabs. The first version
does not need a confirmation dialog. The menu item should stay enabled; if no
tabs match when the WebUI handles the request, it should no-op quietly and must
not create a `stale` folder.

The `stale` folder should be expanded after the move so the user can see what
changed. Existing folder order should be respected: if a `stale` folder already
exists, leave it where it is. If Dao creates it, append it to the end of the
folder model.

## Data Model

Extend `TabData` in `sidebar_bridge.ts`:

```ts
export interface TabData {
  tabId: string;
  index: number;
  title: string;
  url: string;
  faviconUrl: string;
  isActive: boolean;
  isPinned: boolean;
  isAudible: boolean;
  isMuted: boolean;
  isAgentLocked?: boolean;
  isInSplit?: boolean;
  isFaviconLight?: boolean;
  lastActiveTimeMs?: number;
}
```

`lastActiveTimeMs` should use milliseconds since the Unix epoch so TypeScript
can compare it with `Date.now()` without Chromium time conversion code in the
WebUI.

For tabs that do not yet have a recorded timestamp, C++ should use the current
time when the tab first appears in the sidebar state. This prevents restored or
new tabs from being moved to `stale` immediately because of missing metadata.

## Native Responsibilities

`DaoSidebarUIHandler` should maintain the last activation time for tabs in the
current browser window.

Suggested behavior:

- When building state for a tab without an entry, initialize its timestamp to
  now.
- When the active tab changes, update that tab's timestamp to now before
  pushing sidebar state.
- When a tab is removed, drop its timestamp entry.
- When a tab is replaced or restored under the same `WebContents`, keep the
  timestamp if the tab identity still maps cleanly; otherwise initialize to now.

The stored key should prefer the existing stable sidebar tab ID from
`GetSidebarTabId(contents)`, because WebUI folder logic already relies on that
identity.

Native menu execution should not directly move folders. It should fire a WebUI
listener such as `moveStaleTabsRequested`.

C++ should keep the action enabled. The WebUI is the source of truth for stale
candidate filtering and should no-op when no tabs match. This avoids duplicating
the filtering logic in native menu enablement code.

## WebUI Responsibilities

`dao_sidebar_app.ts` should listen for `moveStaleTabsRequested` and call a local
method such as `moveStaleTabsToFolder_()`.

That method should:

1. Return early if folder data has not loaded.
2. Compute `threshold = Date.now() - 24 * 60 * 60 * 1000`.
3. Filter `unpinnedTabs_` to tabs where:
   - `!tab.isActive`
   - `!tab.isPinned`
   - `!tab.isAudible`
   - `!tab.isMuted`
   - `!tab.isAgentLocked`
   - `tab.lastActiveTimeMs !== undefined`
   - `tab.lastActiveTimeMs < threshold`
4. Find a folder whose name is exactly `stale`.
5. If missing, create the folder with name `stale`.
6. Move each stale tab into the folder using existing folder movement semantics.
7. Save folders and trigger a render.

`FolderModel` should expose a small helper to keep this logic testable and out
of the app component, for example:

```ts
findOrCreateFolderByName(name: string): FolderData
moveTabsToFolder(tabs: Array<Pick<TabData, 'tabId'|'url'|'title'>>, folderId: string): void
```

`moveTabsToFolder` should move tabs from their current folder if needed. A stale
tab already inside the `stale` folder should not be duplicated.

## Context Menus And Strings

New user-facing menu text should be localized through
`src/dao/browser/strings/dao_strings.grd`. Use a new string such as:

- `IDS_DAO_SIDEBAR_CONTEXT_MOVE_STALE_TABS`

The English source text should be:

```text
Move Stale Tabs to "stale"
```

Existing tab context menu items include hardcoded English, but new Dao-owned
user-facing text should not extend that pattern.

## Edge Cases

- No stale candidates: no folder should be created.
- Existing `stale` folder with children: append newly stale tabs to its children
  without disturbing existing children.
- Existing folder with different casing, such as `Stale`: do not treat it as
  the same folder in the first version. Exact name matching avoids surprising
  reuse of user-created folders with different intent.
- Stale tab already in another folder: move it from that folder into `stale`.
- Stale tab already in `stale`: leave it there without adding a duplicate.
- Stale folder collapsed: expand it after the move.
- Duplicate URLs: use `tabId` first, then URL/title fallback through existing
  `FolderModel` matching.
- Session restore with missing timestamps: initialize timestamps to now so
  restored tabs are not instantly swept into `stale`.
- Cross-window tabs: operate only on the current window's sidebar state.

## Files

Expected implementation touch points:

- `src/dao/browser/ui/webui/dao_sidebar_ui.h`
- `src/dao/browser/ui/webui/dao_sidebar_ui.cc`
- `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`
- `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`
- `src/dao/browser/ui/webui/resources/sidebar/dao_folder_model.ts`
- `src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_model.test.ts`
- `src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts`
- `src/dao/browser/strings/dao_strings.grd`

No direct edits should be made under `engine/`.

## Testing

### WebUI

Add focused Vitest coverage for `FolderModel`:

- Finds an existing folder named `stale`.
- Creates `stale` when missing.
- Moves multiple tabs into `stale`.
- Moves a tab from another folder into `stale`.
- Does not duplicate a tab already in `stale`.

Add `dao_sidebar_app` coverage:

- `moveStaleTabsRequested` creates `stale` only when at least one candidate
  exists.
- Tabs activated less than 24 hours ago are ignored.
- Tabs older than 24 hours are moved.
- Active, audible, muted, and agent-locked tabs are ignored.
- The folder save bridge is called after a successful move.

### Native

Use the smallest relevant C++ or browser-test coverage for the native surface:

- `BuildSidebarState()` includes `lastActiveTimeMs` for unpinned tabs.
- Activating a tab refreshes its last-active timestamp.
- The sidebar and tab context menus include the stale action.
- Executing the menu action fires the WebUI listener instead of mutating folder
  JSON directly.

## Verification

For implementation verification:

```bash
npm run test:webui
npm run lint:lit
```

For compile confirmation after C++ changes, use only:

```bash
npm run rebuild
```

Do not use direct Chromium build tools or alternate build commands.
