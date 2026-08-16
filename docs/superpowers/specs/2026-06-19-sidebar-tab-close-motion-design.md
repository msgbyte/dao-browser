# Sidebar Tab Close Motion Design

## Status

Approved for design by the user on 2026-06-19.

## Goal

Make sidebar tab removal feel smoother and less abrupt. When a tab disappears
from the current sidebar, the remaining visible tabs should move into their new
positions with a short, quiet transition.

The chosen motion direction is lightweight repositioning: the closed tab should
not perform a dramatic exit. The important effect is that surviving tabs glide
from their previous visual positions to their new positions.

## Scope

Apply the motion to every path that removes a tab from the current sidebar
surface, including:

- Clicking the tab close button.
- Keyboard close commands such as Cmd+W.
- Context menu close actions.
- A tab being dragged out of the current window.
- A tab disappearing from a folder.
- A pinned tab or pinned item disappearing from the pinned area.
- Multi-tab removal where more than one tab disappears in the same state update.

Do not apply this close motion to unrelated state updates:

- Initial sidebar render.
- Title, URL, favicon, audible, muted, or active-state updates.
- Folder expand/collapse.
- Drag placeholder movement while a drag operation is active.
- New-tab insertion motion.

## Experience

The motion should feel fast, calm, and almost invisible unless the user is
looking at the list at the moment of close.

Recommended parameters:

- Duration: 120-150 ms.
- Easing: `cubic-bezier(0.2, 0, 0, 1)`, matching a restrained ease-out feel.
- Animated property: `transform` only.
- Opacity: do not change opacity for surviving tabs.
- Reduced motion: respect `prefers-reduced-motion: reduce` by skipping the
  transition and rendering the final state immediately.

The default first implementation should animate surviving items only. A ghost
for the removed item is intentionally out of scope unless later testing shows
that the disappearance itself feels too abrupt.

## Architecture

Use a parent-container FLIP animation pattern:

1. First: capture existing visible item bounds before Lit commits a new render.
2. Last: after Lit finishes rendering the new state, capture the surviving item
   bounds.
3. Invert: compute the delta from the old bounds to the new bounds.
4. Play: set the item to the old visual position with `transform`, then animate
   back to identity.

This keeps native tab close behavior immediate and avoids delaying Chromium's
real tab model changes. The animation is purely a visual smoothing layer in the
sidebar WebUI.

## Component Boundaries

### `dao-tab-list`

Owns FLIP motion for normal unpinned tabs rendered in the Today section,
including split groups and loose tabs. It should animate only the `dao-tab-item`
elements that survive across a removal update.

### `dao-folder-item`

Owns FLIP motion for tabs rendered inside a folder children container. Folder
expand/collapse should keep using the existing max-height animation and should
not be combined with tab close FLIP.

### `dao-pinned-tabs-grid`

Owns FLIP motion for pinned tiles. It should use the same concept as the normal
tab list, but support two-dimensional grid movement because pinned items may
move horizontally and vertically after removal.

### `dao-tab-item`

Should stay mostly presentation-focused. It may expose stable DOM identity for
animation lookup, but it should not own list-level reorder calculations.

### `dao-sidebar-app`

Should not calculate animation positions. It should continue receiving native
sidebar state and passing arrays to child components.

## Identity

Use stable runtime identity to match items between renders:

- Normal tabs and folder tabs: prefer `tab.tabId`.
- Pinned items: prefer `item.id`.
- Fallbacks such as `index + url + title` may be used only defensively when a
  stable identifier is missing.

Only items present in both the old and new render should animate. Removed items
do not need to remain in the DOM for the first implementation.

## Trigger Rules

A container should run close FLIP only when its own visible item set loses one
or more identities between renders.

Examples:

- `[A, B, C] -> [A, C]`: animate `C` upward.
- `[A, B, C, D] -> [A, D]`: animate `D` to its final position once.
- `[A, B] -> [A, B]` with title changes: no close FLIP.
- Folder children `[A, B, C] -> [A, C]`: animate within the folder only.
- Pinned grid `[A, B, C] -> [A, C]`: animate surviving pinned tiles in the grid.

If a drag is active in the same container, skip close FLIP so existing drop
indicators and placeholders remain authoritative.

## Data Flow

For each animated container:

1. Before an update caused by new data commits, record visible item bounds by
   stable identity.
2. Let Lit render the new state normally.
3. After `updateComplete`, read the new bounds for visible items.
4. Compare identity sets. If no identity was removed, exit.
5. For surviving identities with changed positions, compute:
   - `deltaX = old.left - new.left`
   - `deltaY = old.top - new.top`
6. Apply `transform: translate(deltaX, deltaY)` synchronously.
7. On the next animation frame, animate back to `transform: none`.
8. Clean up inline animation styles after completion or cancellation.

## Edge Cases

- Initial render: no animation because there are no previous bounds.
- Offscreen items: only animate elements that are currently mounted and have
  readable bounds.
- Scroll position: do not force scroll changes during close motion.
- Sidebar collapse: if the sidebar is collapsed or width animation is active,
  skip close FLIP.
- Folder collapse: if the folder is collapsing or collapsed, skip children FLIP.
- Rapid removals: cancel any in-flight FLIP animations in that container and
  animate from the latest committed bounds to the newest final bounds.
- Reduced motion: skip all FLIP setup and render final positions.

## Implementation Notes

Prefer a small shared helper for the FLIP bookkeeping if the three containers
would otherwise duplicate meaningful logic. The helper should stay WebUI-only
and should not introduce native C++ changes.

The helper can expose a compact API such as:

- `snapshot(container, selector, getIdentity)`
- `animateRemovedItems(previousSnapshot, currentElements, options)`
- `cancel()`

This is a design direction, not a required exact API. Implementation should
follow local Lit patterns and keep the changed surface narrow.

## Testing And Verification

Automated WebUI coverage should include:

- `dao-tab-list` animates surviving items when a tab identity is removed.
- `dao-tab-list` does not animate title, favicon, or active-state-only updates.
- `dao-tab-list` skips animation while drag state is active.
- `dao-folder-item` animates surviving children after a child tab disappears.
- `dao-pinned-tabs-grid` animates surviving tiles after a pinned item disappears.
- Reduced motion skips the animation path.

Manual QA should cover:

- Close button in the normal tab list.
- Cmd+W or equivalent keyboard close.
- Context menu close.
- Closing a tab inside a folder.
- Removing or closing a pinned tab.
- Dragging a tab out of the current window.
- Closing multiple tabs quickly.

For implementation verification, run the smallest relevant WebUI checks:

- `npm run test:webui`
- `npm run lint:lit` when Lit changes are involved.
