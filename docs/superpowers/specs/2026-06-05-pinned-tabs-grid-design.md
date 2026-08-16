# Pinned Tabs Grid Design

## Summary

Dao Browser will add an Arc-style pinned area above the vertical tab list. A pinned item is a persistent sidebar tile, not just a currently open Chromium pinned tab. When its tab is open, the tile behaves like the tab itself: it can be active, clicked to activate, closed, or unpinned. When the tab is closed, the tile remains in the grid as a dormant pinned item and can reopen its URL as a Chromium pinned tab.

The implementation should reuse Chromium's native pinned tab state for open tabs, while adding a Dao-owned persistent pinned item model for dormant items and ordering.

## Goals

- Show pinned items in a top sidebar grid, above the Today tab section.
- Use a labeled tile layout: favicon plus short title.
- Preserve pinned items after their backing tab is closed.
- Keep selected/active behavior identical to normal tabs when the pinned tab is open.
- Reopen dormant pinned items as Chromium pinned tabs.
- Keep normal Today tabs and folder behavior scoped to unpinned tabs.

## Non-Goals

- Do not implement a fully independent shortcut system that ignores Chromium pinned tabs.
- Do not support dragging normal tabs into the pinned grid in the first version.
- Do not support dragging pinned tiles into Today or folders in the first version.
- Do not change Chromium's session restore semantics beyond reconciling restored pinned tabs into Dao's pinned model.

## Existing Context

The sidebar already receives `pinnedTabs` and `unpinnedTabs` from `DaoSidebarUIHandler::PushFullState()`, based on `TabStripModel::IsTabPinned(i)`. The current WebUI renders pinned tabs through `dao-favorites-view`, but that component only shows open pinned tabs as icon buttons. It does not represent dormant items, expose pin/unpin actions, render active state clearly, or own persistent ordering.

The unpinned tab list and folder model already operate only on `unpinnedTabs`. That separation should be preserved.

## Recommended Approach

Add a Dao-owned pinned item model layered on top of Chromium pinned tabs.

Each pinned item is persistent data. When it has a matching open Chromium pinned tab, the WebUI treats it as an open tab tile. When it has no open tab, the tile is dormant but remains visible. Clicking a dormant tile opens its URL and pins the new tab using Chromium's native tab pin state.

This keeps the behavior aligned with user expectations: the pinned element itself stays in the grid, while its open tab state can come and go.

## Data Model

Persist pinned items per profile in a small JSON file named `dao_pinned_tabs.json`, managed by a dedicated Dao-owned model class. This matches the sidebar's existing file-backed folder persistence style and keeps the growing persistence logic out of `DaoSidebarUIHandler`.

Suggested persistent shape:

```json
{
  "version": 1,
  "items": [
    {
      "id": "stable-id",
      "title": "GitHub",
      "url": "https://github.com/",
      "faviconUrl": "data:image/png;base64,...",
      "createdAt": 1780642728,
      "updatedAt": 1780642728
    }
  ]
}
```

Suggested WebUI shape:

```ts
interface PinnedItemData {
  id: string;
  title: string;
  url: string;
  faviconUrl: string;
  isOpen: boolean;
  openTabIndex: number;
  isActive: boolean;
  isFaviconLight?: boolean;
}
```

`openTabIndex` should be `-1` for dormant items.

## Reconciliation

On full sidebar state pushes, reconcile the persistent pinned item model with `TabStripModel`.

- If a pinned item matches an open Chromium pinned tab, mark it open and attach the tab index.
- If a pinned item has no matching open pinned tab, keep it as dormant.
- If Chromium/session restore produces a pinned tab that is missing from the Dao pinned model, add a pinned item automatically.
- If a pinned tab navigates to a new URL, update the pinned item title, URL, favicon, and timestamp so dormant reopen uses the latest page.
- If duplicate URLs exist, prefer an already pinned tab. Do not bind a pinned item to an unpinned normal tab.

Matching can start with exact URL. If later product work wants origin-level behavior, that should be a separate design because it changes user expectations.

## WebUI Layout

Replace or rename `dao_favorites_view.ts` with a pinned-specific component, preferably `dao_pinned_tabs_grid.ts`.

The grid appears below `dao-new-tab-button` and above the Today section. Tiles use a fixed height and responsive columns:

- Default: 3 columns.
- Narrow sidebar: 2 columns.
- Wide sidebar: 4 columns if space allows.

Each tile shows favicon and a short title. Text must ellipsize inside stable tile dimensions. The component should not use Tailwind classes.

Tile states:

- Active: active surface plus Dao accent ring.
- Open inactive: normal tile surface.
- Dormant: muted surface or slight opacity reduction, still readable and clickable.

## User Interactions

### Pin

Right-clicking a normal tab shows `Pin Tab`. Executing it should:

1. Add or update the persistent pinned item.
2. Set the Chromium tab pinned state.
3. Push a sidebar state update so the tab leaves Today and appears in the grid.

### Activate Or Open

Clicking an open pinned tile activates its backing tab.

Clicking a dormant pinned tile opens the stored URL in the current window, pins the new tab, and activates it.

### Close

Closing an open pinned tile closes the tab only. The pinned item remains in the grid as dormant.

### Unpin

Unpinning a dormant tile removes the persistent pinned item.

Unpinning an open tile removes the persistent pinned item and sets the Chromium tab pinned state to false. The tab should remain open and reappear in Today rather than being closed.

### Reorder

Pinned tiles support drag reorder within the pinned grid in the first version. Reordering persists to the pinned item model.

Dragging normal tabs into the pinned grid and dragging pinned tiles into Today/folders are out of scope for the first version.

## Context Menus

Use localized strings in `src/dao/browser/strings/dao_strings.grd` for new user-facing text.

Normal tab menu:

- `Pin Tab`

Open pinned tile menu:

- `Unpin`
- `Close Tab`
- `Copy Link`

Dormant pinned tile menu:

- `Open`
- `Unpin`
- `Copy Link`

Existing menu strings in this area are currently hardcoded English. New strings should not extend that pattern.

## Bridge Commands

Add WebUI-to-C++ commands:

- `pinTab(index)`
- `unpinPinnedItem(id)`
- `activateOrOpenPinnedItem(id)`
- `closePinnedItemTab(id)`
- `movePinnedItem(id, toIndex)`
- `showPinnedItemContextMenu(id, screenX, screenY)`

Add `pinnedItems` to `SidebarState`. The old `pinnedTabs` field can remain temporarily for migration or be unused by the new component.

## Files

Expected implementation touch points:

- `src/dao/browser/ui/webui/dao_sidebar_ui.h`
- `src/dao/browser/ui/webui/dao_sidebar_ui.cc`
- `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`
- `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`
- `src/dao/browser/ui/webui/resources/sidebar/dao_pinned_tabs_grid.ts`
- `src/dao/browser/ui/webui/resources/sidebar/BUILD.gn`
- `src/dao/browser/strings/dao_strings.grd`
- `src/dao/browser/ui/webui/dao_pinned_tab_model.h`
- `src/dao/browser/ui/webui/dao_pinned_tab_model.cc`

## Error Handling

- Invalid or unsupported pinned item URLs should not crash the sidebar. Keep the tile visible, disable open behavior, and allow unpin.
- If persistence load fails, render open Chromium pinned tabs and avoid deleting stored data until a successful load.
- If persistence save fails, keep the in-memory model and retry on the next mutation or full state update.
- If a pinned item references a tab index that is no longer valid, reconcile it back to dormant.

## Testing

### WebUI

- Render open, active, and dormant pinned tile states.
- Clicking an open tile sends `activateOrOpenPinnedItem`.
- Clicking a dormant tile sends `activateOrOpenPinnedItem`.
- Dragging tiles within the grid sends `movePinnedItem`.
- Long titles ellipsize without changing tile dimensions.

### Browser Tests

- Pinning a normal tab moves it from Today into the pinned grid and sets `TabStripModel::IsTabPinned()` true.
- Closing an open pinned tab leaves a dormant pinned tile.
- Clicking a dormant tile reopens the URL and pins the new tab.
- Unpinning an open pinned tile keeps the tab open and sets `IsTabPinned()` false.
- Unpinning a dormant pinned tile removes it from the grid.
- Restored Chromium pinned tabs are added to the Dao pinned model if missing.
- Folder model reconciliation continues to ignore pinned tabs.

## Verification

For WebUI-only changes, run:

```bash
npm run test:webui
npm run lint:lit
```

For C++ or integration changes, compile confirmation must use:

```bash
npm run rebuild
```

Do not use alternate Chromium build commands.
