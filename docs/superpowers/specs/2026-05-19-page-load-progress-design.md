# Page Load Progress Bar — Design

Date: 2026-05-19
Status: Approved (pending implementation plan)

## Summary

Add a thin progress bar to Dao Browser that visualizes real page load progress
along the top edge of the rounded content card. The bar tracks the active
tab's `WebContents::GetLoadProgress()`, animates smoothly between updates,
fills to 100% on completion and then fades out. It is a passive, non-
interactive decoration painted on top of the content area.

## Goals

- Give users a clear, calm signal that a page is loading and roughly how
  far along it is.
- Reflect real load progress, not a simulated curve.
- Stay visually consistent with Dao's design language (soft, blue accent,
  minimal chrome).
- Not interfere with input — the bar must be event-transparent.

## Non-goals

- No per-tab indicators in the sidebar tab list (favicon spinners stay as-is).
- No textual status (no "Loading example.com…").
- No reflection of background tabs' progress while another tab is active.
- No error-state styling (errors are shown by error pages / other UI).

## Visual Spec

- **Position**: along the top edge of the content card. The bar sits flush
  with the top of the rounded content area, inside the `kContentShadowMargin`
  inset on left and right so it never extends over the sidebar or past the
  window edge.
- **Height**: 2 px main bar.
- **Color**: brand blue `SkColorSetRGB(70, 120, 190)` in both light and dark
  mode (matching the existing accent in `dao_colors.h`). No state-color
  variants — errors and stops just fade out.
- **Glow**: 1 px halo above and below the main bar at ~40/255 alpha of the
  same color, producing a soft glow.
- **Opacity**: 100% while loading; animates 1→0 over ~200 ms on completion
  or cancellation.
- **Width fill**: `width() * displayed_progress_`, where `displayed_progress_`
  is the locally-animated value (see Animation).

## Signal Source

Progress is driven by Chromium's real load events on the active tab's
`content::WebContents`:

- `WebContentsObserver::LoadProgressChanged(double progress)` — values in
  `[0.0, 1.0]`.
- `WebContentsObserver::DidStartLoading()` — entry transition.
- `WebContentsObserver::DidStopLoading()` — completion transition.
- `WebContentsObserver::PrimaryPageChanged()` and
  `WebContentsObserver::WebContentsDestroyed()` — used as safety nets to
  detach observation and reset.

We do not simulate or interpolate beyond what Chromium reports. We do smooth
abrupt jumps with a short local animation so the bar never appears to teleport
across the screen — but it never moves past the most recently reported value.

## State Machine

States carried by `DaoLoadProgressView`:

```
Hidden        — default, layer opacity 0, not painted
Loading       — visible, displayed_progress_ animates toward target
Completing    — target pinned to 1.0, brief hold (~150 ms)
FadingOut     — layer opacity animates 1 → 0 (~200 ms), then → Hidden
```

Transitions:

- `Hidden → Loading`: `DidStartLoading` on the observed `WebContents`, or
  becoming active on a tab whose `IsLoading()` is true. `displayed_progress_`
  resets to the current `GetLoadProgress()` snapshot.
- `Loading → Loading`: `LoadProgressChanged(p)` updates the target; the
  view animates `displayed_progress_` toward `p` over ≤120 ms with an
  ease-out curve. Targets only ever move forward; a smaller `p` (rare; e.g.
  due to a new navigation) snaps without easing.
- `Loading → Completing`: either `DidStopLoading` fires or progress hits 1.0.
  Target is pinned at 1.0; animation completes; then a 150 ms hold timer
  starts.
- `Completing → FadingOut`: hold timer elapses; start opacity animation.
- `FadingOut → Hidden`: opacity animation finishes. `displayed_progress_`
  resets to 0.
- Any state `→ Hidden` (no fade): `WebContentsDestroyed`, the controller
  losing its `WebContents`, or active tab switching to a non-loading tab
  while the bar is already in `Hidden` / `FadingOut` (in that case we just
  cancel).

Tab switching:

- On active-tab change, the controller detaches from the old `WebContents`
  and attaches to the new one. The view is then forced into a state
  consistent with the new tab's `IsLoading()` + `GetLoadProgress()`, with no
  fade animation — instant set, so switching feels immediate.

## Components

Two new files plus a small set of patches.

### `DaoLoadProgressView` (`views::View`)

- Paints to its own layer (`SetPaintToLayer`, `SetFillsBoundsOpaquely(false)`)
  so opacity animations don't repaint the content area.
- `SetCanProcessEventsWithinSubtree(false)` — purely decorative.
- Public API (called by the controller):
  - `void SetTargetProgress(double p, bool animate);`
  - `void StartLoading();`
  - `void FinishLoading();` // triggers Completing → FadingOut
  - `void HideImmediately();`
- Owns:
  - `displayed_progress_` (double, current animation value)
  - A `gfx::LinearAnimation` for `displayed_progress_` (≤120 ms, ease-out)
  - A timer for the 150 ms hold
  - A `ui::Layer` opacity animation for fade-out (via
    `ui::ScopedLayerAnimationSettings`)
- `OnPaint`:
  - Computes `fill_width = std::round(width() * displayed_progress_)`.
  - Draws halo first: two horizontal lines (or a 4 px tall rect) above and
    below the main fill, at alpha ~40.
  - Draws the main 2 px fill on top.
- `OnBoundsChanged` triggers a repaint but does not reset progress.

### `DaoLoadProgressController`

- Plain C++ class, not a `View`. Owned by `BrowserView`.
- Implements `TabStripModelObserver` and `content::WebContentsObserver`.
- Wired to a `TabStripModel*` and a `DaoLoadProgressView*` at construction.
- Responsibilities:
  - On active-tab change, swap `WebContents` observation and resync view
    state.
  - On `LoadProgressChanged` → `view_->SetTargetProgress(p, /*animate=*/true)`.
  - On `DidStartLoading` → `view_->StartLoading()`.
  - On `DidStopLoading` → `view_->FinishLoading()`.
  - On `WebContentsDestroyed` → detach + `view_->HideImmediately()`.
- Pointers held with `raw_ptr<>`.
- Owns no UI state of its own; the view is the source of truth for the
  visible state machine.

### Why split into View + Controller?

Keeps the view focused on painting + animation, and the controller focused on
Chromium observation + lifecycle. The view can be unit-/pixel-tested without a
real `WebContents`. The controller can be reasoned about without touching
paint code. This is the same split used elsewhere in the project (e.g.
`dao_address_bar_view` has a controller-ish handling of tab strip and
content events but is one file; here we keep them separate because the bar
has more interesting animation state).

## Integration with Chromium (`BrowserView`)

Patches to `chrome/browser/ui/views/frame/browser_view.{h,cc}`:

- Header: add `raw_ptr<dao::DaoLoadProgressView> dao_load_progress_` and
  `std::unique_ptr<dao::DaoLoadProgressController> dao_load_progress_controller_`.
- Source:
  - In the same block that creates `dao_corner_overlay_`, add
    `dao_load_progress_ = AddChildView(std::make_unique<dao::DaoLoadProgressView>());`
    immediately after the corner overlay so it stencils above the corners
    visually.
  - Construct the controller in `BrowserView`'s constructor (or once the
    `TabStripModel` is known), passing `browser_->tab_strip_model()` and
    `dao_load_progress_`.
  - In `BrowserView::Layout()` (inside the existing `if (dao_sidebar_ ...)`
    block that lays out Dao overlays), compute the progress bar bounds:
    - x = `dao_sidebar_->bounds().right() + kContentShadowMargin`
    - y = top of content area (same y as the top edge of the rounded
      content card, i.e. `kContentShadowMargin` from the top of the
      contents container's frame in BrowserView coordinates)
    - width = `BrowserView width - x - kContentShadowMargin`
    - height = 2 px (plus 2 px halo space → set the bar's own height to ~4 px
      and paint the main fill centered, so the halo doesn't clip)

BUILD changes:

- `src/patches/chrome/browser/ui/BUILD.gn.patch` — add
  `dao_load_progress_view.cc/h` and `dao_load_progress_controller.cc/h` to
  the existing dao sources list.

## File Layout

New:

- `src/dao/browser/ui/views/dao_load_progress_view.h`
- `src/dao/browser/ui/views/dao_load_progress_view.cc`
- `src/dao/browser/ui/views/dao_load_progress_controller.h`
- `src/dao/browser/ui/views/dao_load_progress_controller.cc`

Modified (via patches):

- `src/patches/chrome/browser/ui/views/frame/browser_view.cc.patch`
- `src/patches/chrome/browser/ui/views/frame/browser_view.h.patch`
- `src/patches/chrome/browser/ui/BUILD.gn.patch`

## Error & Edge Cases

- **No active tab** (last tab closed): controller sees `nullptr` from
  `tab_strip_model_->GetActiveWebContents()`, calls `view_->HideImmediately()`.
- **Tab destroyed mid-load**: `WebContentsDestroyed` detaches; view hides.
- **Cross-origin navigation while loading**: `LoadProgressChanged` will
  naturally restart from a smaller value; we snap `displayed_progress_` back
  without easing for these backward steps.
- **Crashed renderer (sad tab)**: `DidStopLoading` fires; bar fades out
  normally. No special crash handling — the sad tab UI is what tells the user.
- **Subframe-only load activity**: rely on Chromium's `LoadProgressChanged`
  semantics; if it doesn't fire for these, we simply don't show the bar.
- **Sidebar collapse/expand**: handled by `BrowserView::Layout` re-running;
  the bar's x and width recompute. Visual state is preserved.
- **Picture-in-Picture / split view**: bar tracks the active tab in the
  primary browser window only; no special multi-pane handling.

## Animation Details

- `displayed_progress_` uses `gfx::LinearAnimation` driving a manual ease-out
  cubic over 120 ms per target update. We cap the duration so rapid bursts of
  `LoadProgressChanged` don't queue up.
- The 150 ms post-complete hold uses `base::OneShotTimer`.
- Fade-out uses `ui::ScopedLayerAnimationSettings` on the view's layer with
  `Tween::EASE_OUT` and 200 ms duration.
- Total worst-case time from "page done loading" to "bar fully invisible" is
  ~350 ms.

## Testing

Add `DaoLoadProgressBrowserTest` to
`src/dao/browser/ui/views/dao_browser_browsertest.cc`:

1. **Bar appears during real load**: navigate to a slow test URL via
   `EmbeddedTestServer`; assert `dao_load_progress_->GetVisible()` and
   `layer()->opacity() > 0` while loading; assert it returns to hidden /
   opacity 0 within a short timeout after `DidStopLoading`.
2. **Bar reflects active-tab switch**: open two tabs both loading; switch
   active; assert progress immediately reflects the new tab's
   `GetLoadProgress()` (within tolerance) without an interim fade.
3. **Switching to a finished tab hides the bar**: with one finished tab and
   one loading tab, switch from loading → finished; assert bar transitions to
   hidden.
4. **Stop command hides the bar**:
   `chrome::ExecuteCommand(browser(), IDC_STOP)` mid-load → bar fades out
   without a 100% fill state lingering.
5. **Sidebar collapse/expand re-lays out the bar**: toggle sidebar; assert
   the bar's x and width match the new content area.

These mirror the existing layout-driven assertions in the file (no pixel
diffing — just bounds, visibility, and opacity).

## Open Questions / Future Work

None blocking. Possible follow-ups (not in scope):

- Reduced-motion support (skip the fade animation when the OS asks for
  reduced motion).
- Per-tab progress indicators in the sidebar tab list.
- Theming the accent color when the page background is high-contrast against
  blue.
