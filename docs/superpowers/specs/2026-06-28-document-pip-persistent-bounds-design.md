# Document PiP Persistent Bounds Design

## Goal

Persist Dao's Document Picture-in-Picture window bounds per site so a user-resized PiP window opens at the same size and position after the PiP window is closed, the tab is reopened, or the browser restarts.

The first version targets Dao's Document PiP interception flow and Chromium's Document PiP browser window. It does not add a user-facing settings UI.

## Current Behavior

Dao's `DaoPipInterceptor` injects JavaScript that computes an initial Document PiP size from the video dimensions and calls `documentPictureInPicture.requestWindow({ width, height })`.

Chromium already caches recent PiP bounds through `PictureInPictureBoundsCache`, and `PictureInPictureBrowserFrameView::OnWidgetBoundsChanged()` updates that cache when the user moves or resizes the window. That cache is in memory only and explicitly not persisted.

## Selected Approach

Use a Dao-owned profile pref keyed by site origin.

Add a dictionary pref in `dao::prefs`:

```text
dao.pip_window_bounds_by_origin
```

Each entry is keyed by serialized origin, for example `https://www.bilibili.com`, and stores the most recent valid PiP outer window bounds:

```json
{
  "https://www.bilibili.com": {
    "x": 1200,
    "y": 620,
    "width": 640,
    "height": 360,
    "opener_display_id": 1,
    "pip_display_id": 1,
    "requested_width": 800,
    "requested_height": 450
  }
}
```

Only origin is used as the site key. Full URLs, paths, query strings, and titles are not stored.

## Components

Add `src/dao/browser/pip/dao_pip_bounds_prefs.{h,cc}` with a small API:

```cpp
std::optional<gfx::Rect> GetPersistedPipBoundsForSite(
    Profile* profile,
    content::WebContents* web_contents,
    const display::Display& opener_display,
    std::optional<gfx::Size> requested_content_size);

void UpdatePersistedPipBoundsForSite(
    Profile* profile,
    content::WebContents* web_contents,
    const gfx::Rect& most_recent_bounds,
    const display::Display& opener_display,
    const display::Display& pip_display,
    std::optional<gfx::Size> requested_content_size);
```

The helper owns serialization, validation, and origin matching. Callers should not know the pref dictionary shape.

Persisted bounds are outer window bounds, matching Chromium's
`PictureInPictureWindowManager::CalculateOuterWindowBounds()` return value. The
stored `requested_width` and `requested_height` are content-size inputs from
`PictureInPictureWindowOptions`, not the final outer window size.

## Read Flow

When Chromium calculates Document PiP outer window bounds:

1. Keep existing Chromium in-memory cache behavior first.
2. If Chromium cache returns bounds, use them.
3. If Chromium cache misses, query Dao's persisted bounds helper.
4. Use persisted bounds only when:
   - profile is available,
   - origin is valid and not opaque,
   - requested content size matches the stored requested content size,
   - the stored bounds are non-empty,
   - the opener display condition is still valid.
5. If validation fails, fall back to Chromium's current size calculation.

This keeps active-session behavior unchanged and only fills the gap after restart or cache loss.

## Write Flow

When `PictureInPictureBrowserFrameView::OnWidgetBoundsChanged()` fires:

1. Keep the existing `PictureInPictureWindowManager::UpdateCachedBounds()` call.
2. Also update Dao's persisted bounds for Document PiP windows.
3. Store only valid outer bounds:
   - width and height must be at least Chromium's minimum inner PiP size after accounting for current outer bounds,
   - bounds must intersect a known display work area,
   - origin must be valid and not opaque.

This captures both user resize and user move. It also captures Dao's custom overlay-corner resize path because that ultimately changes the main PiP widget bounds.

The write path must pass the requested content size used for the current
Document PiP window. Prefer deriving this from the window's
`PictureInPictureWindowOptions` / `BrowserView::GetDocumentPictureInPictureOptions()`.
If those options are unavailable, store no requested size and only match future
requests that also have no requested size.

## Matching Rules

Persisted bounds are per origin and requested content size.

If a site requests `{ width: 800, height: 450 }`, the stored entry should only be reused for future requests with the same requested size. This mirrors Chromium's short-term cache and avoids applying a Bilibili player-sized window to a different Document PiP surface from the same origin.

If a future Dao rule intentionally changes its requested size, the old persisted entry should be ignored until the user resizes the new window.

## Display Rules

Store both opener display ID and PiP display ID.

On restore:

- If the opener is still on the same display, restore exact bounds when they fit a current display work area.
- If the opener moved to the PiP display, restoring exact bounds is allowed.
- If display IDs no longer exist or the bounds are fully offscreen, ignore the persisted bounds and let Chromium compute a fresh placement.

Do not try to migrate offscreen bounds in the first version. Ignoring invalid bounds is safer than guessing.

## Privacy

The pref stores origin strings and geometry only. It must not store full URLs, page titles, CSS selectors, video metadata, or timestamps.

The pref is profile-scoped. Incognito or off-the-record profiles must not persist
into the regular profile.

## Integration Points

Tracked Dao-owned changes:

- `src/dao/browser/dao_pref_names.h`
- `src/dao/browser/dao_pref_names.cc`
- `src/dao/browser/pip/dao_pip_bounds_prefs.h`
- `src/dao/browser/pip/dao_pip_bounds_prefs.cc`
- `src/dao/browser/ui/dao_ui_sources.gni`

Chromium integration patches:

- `src/patches/chrome/browser/picture_in_picture/picture_in_picture_window_manager.cc.patch`
- `src/patches/chrome/browser/ui/views/frame/picture_in_picture_browser_frame_view.cc.patch`

The implementation should update tracked patch files first, then run `npm run import` to apply them into `engine/src/`.

## Testing

Add focused browser coverage near existing Dao PiP tests:

- Same origin: resize/move Document PiP, close, reopen, bounds restore.
- Different origin: stored bounds are not reused.
- Requested size mismatch: stored bounds are ignored.
- Invalid/offscreen bounds: stored bounds are ignored.
- Pref registration: `dao.pip_window_bounds_by_origin` exists as a dictionary pref.

Run the smallest relevant test filter for Dao PiP behavior. For compile confirmation after C++ edits, use `npm run rebuild`.

## Open Decisions

No open product decisions remain for the first version. The selected behavior is per-site origin persistence through a Dao-owned profile pref.
