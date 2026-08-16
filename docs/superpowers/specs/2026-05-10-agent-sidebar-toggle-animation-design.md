# Agent Sidebar Toggle Animation — Design

## Background

The right-side AI Agent sidebar (`DaoAgentSidebarView`) currently toggles
visibility instantly:

- `Toggle()` flips `expanded_`, calls `SetVisible(true|false)`, then
  `PreferredSizeChanged()` → one layout pass; the panel pops in/out in a single
  frame.
- The 360px-wide panel snapping into existence is visually jarring.

The left main sidebar (`DaoSidebarView`) already solves the analogous problem
via GPU compositor-layer transforms in `AnimateLayerSlide()` — apply a reverse
translation as the start state, then animate to identity inside a
`ScopedLayerAnimationSettings` block. We will port that exact pattern to the
right sidebar, mirrored for the opposite slide direction, and tuned to a
slightly longer duration that suits the wider panel.

## Goals

- Smooth slide-in/slide-out animation when the Agent sidebar is toggled.
- 180ms duration with `Tween::EASE_OUT` — the request was "fast and silky".
- Slide direction: panel enters from the right edge, exits to the right edge.
- No content fade — the slide alone gives enough motion vocabulary; an
  additional opacity ramp would muddy the perceived speed.
- No regressions in:
  - macOS NSView frame sync (hit-testing of the WebView after animation ends)
  - Resize-handle drag behavior
  - Toggling rapidly mid-animation
  - Existing `DaoBrowser*` browser_tests (which assert on visibility/layout
    state, not on animation frames)

## Non-Goals

- No spring/elastic easing.
- No animation of any sibling layers (toolbar, main content, left sidebar).
  The Agent sidebar is the right-most child; the layout already handles the
  main content shrinking/growing — we don't need to slide other elements.
- No content fade.
- No new browser_tests — the state-machine surface change is small and visual
  motion quality isn't usefully asserted in unit tests.

## Approach

### Reuse the proven left-sidebar pattern

The same compositor-layer technique works here, with two structural
differences from the left sidebar:

1. **No sibling layers to translate.** The left sidebar slides itself plus the
   address bar, corner overlay, and contents container so they all move as one
   visual unit. The right sidebar sits at the right edge of the layout — when
   it appears/disappears, the layout already shrinks/grows the contents area.
   We only animate the Agent sidebar's own layer.
2. **Mirrored slide direction.** Start translation is positive `width` on the
   X axis (off-screen right), animating to identity for show; reverse for
   hide.

### Animation cycle

**Show (`expanded_` flips false → true)**

1. `EnsureLoaded()` (current behavior).
2. `SetVisible(true)`.
3. `PreferredSizeChanged()` — commits the final layout instantly, so the
   panel reserves its 360px slot.
4. `AnimateLayerSlide(/*incoming=*/true, target_width=current_width_)`:
   - Set layer transform to `Translate(target_width, 0)` (panel starts off the
     right edge).
   - Open a `ScopedLayerAnimationSettings` with `kDuration=180ms`,
     `Tween::EASE_OUT`, register `this` as observer.
   - Set layer transform back to identity → animator interpolates.

**Hide (`expanded_` flips true → false)**

1. Capture `old_width = current_width_` *before* mutating state.
2. Keep `SetVisible(true)` (do NOT hide yet — the panel must remain on screen
   to be animated out).
3. Do NOT call `PreferredSizeChanged()` yet — if we change the preferred size
   to 0 now, layout collapses the panel before the animation can play.
4. `AnimateLayerSlide(/*incoming=*/false, target_width=old_width)`:
   - Layer is currently at identity.
   - Open `ScopedLayerAnimationSettings` with the same duration/tween/observer.
   - Set layer transform to `Translate(old_width, 0)` → animator interpolates.
5. `OnImplicitAnimationsCompleted()` (animation done):
   - If the latest target state is "hidden":
     - Reset transform to identity.
     - `SetVisible(false)`.
     - `PreferredSizeChanged()` — final layout pass, content area expands.
   - Otherwise (show animation finished): just reset transform to identity and
     run the macOS layout-sync sequence (see below).

### macOS NSView frame sync

`DaoSidebarView::OnImplicitAnimationsCompleted` already documents and works
around a known macOS bug: a residual layer transform can leave the WebView's
NSView frame stale (`ConvertRectToWidget` reads the transform), breaking
hit-testing. The fix is to:

- Reset all involved layer transforms to identity.
- `InvalidateLayout()` + `DeprecatedLayoutImmediately()` on the relevant
  containers to force `NativeViewHost::Layout()` to re-`setFrame:` synchronously.

We mirror the same teardown for the Agent sidebar: reset our own layer
transform; invalidate + immediate-layout `web_view_` and the BrowserView's
`contents_container()` so the main WebView's NSView re-syncs after our slot
size changes (during hide) or remains correctly sized (during show).

### Mid-animation re-toggle

If the user calls `Toggle()` while a previous animation is still running:

- Stop the in-flight animation by calling
  `layer()->GetAnimator()->StopAnimating()` (or
  `StopAnimatingProperty(LayerAnimationElement::TRANSFORM)`) before
  reconfiguring the next animation.
- Snap the layer transform to whatever the *current state* should be at this
  instant — for simplicity, treat it as "start from identity if we are now
  showing, or identity if we are now hiding" (the previous animation's
  observer-completion handler may not have fired; the new animation will
  drive from wherever the layer currently is).
- The new animation begins immediately with the new direction.

This is identical to the left sidebar's strategy: it just calls
`collapse_animation_.Stop()` and starts a fresh animation; we use the
compositor-layer animator's `StopAnimating()` because we're driving
implicitly via `ScopedLayerAnimationSettings`, not a `LinearAnimation`.

### Resize while animating

`OnResize()` already early-returns when `!expanded_`. We additionally
short-circuit it when an animation is in flight: the resize handle is hit
only on the visible panel, but we don't want an in-flight slide animation
arguing with a user drag. Practical guard: if
`layer()->GetAnimator()->is_animating()` for `TRANSFORM`, return without
applying the resize delta. (Alternatively: `StopAnimating()` and let the
resize take effect immediately — choose the one that feels less janky in
manual QA; default to the early-return.)

## Components

### `DaoAgentSidebarView` header changes

```cpp
// new include
#include "ui/compositor/layer_animation_observer.h"

class DaoAgentSidebarView : public views::View,
                            public content::WebContentsDelegate,
                            public views::ResizeAreaDelegate,
                            public ui::NativeThemeObserver,
                            public ui::ImplicitAnimationObserver {  // NEW
  ...
  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsCompleted() override;

 private:
  void AnimateLayerSlide(bool incoming, int slide_distance);

  // Track the latest commanded direction so the animation-end callback knows
  // whether to set SetVisible(false). The animator may complete *after*
  // another Toggle() has flipped expanded_ again, so we cannot simply read
  // expanded_ in the callback.
  bool animation_target_visible_ = false;
};
```

### `DaoAgentSidebarView::Toggle()` rewrite

```cpp
bool DaoAgentSidebarView::Toggle() {
  // Stop any in-flight slide before starting a new one so the layer
  // transform settles deterministically.
  if (layer() && layer()->GetAnimator()->is_animating()) {
    layer()->GetAnimator()->StopAnimating();
  }

  expanded_ = !expanded_;
  animation_target_visible_ = expanded_;

  if (expanded_) {
    EnsureLoaded();
    SetVisible(true);
    PreferredSizeChanged();          // layout reserves the 360px slot now
    AnimateLayerSlide(/*incoming=*/true, current_width_);
  } else {
    // Stay visible during the slide-out; finalize in the animation-end
    // observer.
    AnimateLayerSlide(/*incoming=*/false, current_width_);
  }

  if (expanded_) {
    // Existing focus-deferral logic — unchanged.
    ...
  }
  return expanded_;
}
```

### `DaoAgentSidebarView::AnimateLayerSlide()`

```cpp
void DaoAgentSidebarView::AnimateLayerSlide(bool incoming, int slide_distance) {
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  constexpr auto kDuration = base::Milliseconds(180);

  gfx::Transform off_screen_right;
  off_screen_right.Translate(slide_distance, 0);

  if (incoming) {
    layer()->SetTransform(off_screen_right);     // start: off-screen right
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(kDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    settings.AddObserver(this);
    layer()->SetTransform(gfx::Transform());     // end: identity
  } else {
    // Layer is currently at identity (panel on-screen).
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(kDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    settings.AddObserver(this);
    layer()->SetTransform(off_screen_right);     // end: off-screen right
  }
}
```

### `DaoAgentSidebarView::OnImplicitAnimationsCompleted()`

```cpp
void DaoAgentSidebarView::OnImplicitAnimationsCompleted() {
  // Always normalize the transform.
  if (layer()) {
    layer()->SetTransform(gfx::Transform());
  }

  if (!animation_target_visible_) {
    // Slide-out finished — actually hide and shrink the layout slot.
    SetVisible(false);
    PreferredSizeChanged();
  }

  // macOS NSView frame sync: force NativeViewHost to re-setFrame: now that
  // the layer transform is reset and the layout has settled.
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  if (bv && bv->contents_container()) {
    bv->contents_container()->InvalidateLayout();
    bv->contents_container()->DeprecatedLayoutImmediately();
  }
  if (web_view_) {
    web_view_->InvalidateLayout();
    web_view_->DeprecatedLayoutImmediately();
  }
}
```

### `DaoAgentSidebarView::OnResize()` guard

Insert near the top:

```cpp
if (layer() && layer()->GetAnimator()->is_animating()) {
  return;  // ignore resize input while a slide animation is playing
}
```

## Data flow

1. User triggers Toggle (Cmd+Y / control-center button / Cmd+L into Agent /
   external prompt expansion).
2. `Toggle()` flips state, optionally `EnsureLoaded()`, then commits a layout
   pass and starts the layer animation (or for hide: starts the layer
   animation first).
3. Compositor drives the transform interpolation on the GPU thread for 180ms.
4. `OnImplicitAnimationsCompleted()` fires on the UI thread:
   - resets transform,
   - if hiding: `SetVisible(false)` + `PreferredSizeChanged()`,
   - syncs macOS NSView frames via `DeprecatedLayoutImmediately()`.

## Edge cases

- **Toggle during animation:** `StopAnimating()` at the top of `Toggle()`
  cancels the prior animation; observer's pending callback may still fire
  with the *new* `animation_target_visible_` — that's correct because the
  callback only branches on the latest target.
- **Toggle while not yet attached to a Widget:** Skip animation entirely
  (no compositor available); fall back to current snap behavior. Guard:
  `if (!GetWidget()) { /* old path */ }`.
- **First-time expand triggers `EnsureLoaded()`** — WebContents creation may
  briefly stall; the slide still plays correctly because the WebView paints
  on top of the already-translated layer.
- **Dark/light theme change mid-animation:** `OnNativeThemeUpdated` only
  re-applies WebView background — orthogonal to layer transforms.

## Risks

- macOS NSView frame staleness has bitten us before (see
  `DaoSidebarView::OnImplicitAnimationsCompleted` rationale comment). We
  mitigate via the same `DeprecatedLayoutImmediately()` pattern.
- `SetPaintToLayer()` on the Agent sidebar promotes it to a layer if it
  wasn't already. The `views::WebView` child has its own native layer; the
  promotion of the parent should not cause double-painting because we set
  `SetFillsBoundsOpaquely(false)` and the panel has no opaque background of
  its own (the WebView paints its own background).
- If anything in the system was relying on the sidebar NOT having a
  compositor layer, that contract changes. Most BrowserView children already
  paint to layers; this should be safe but worth a manual smoke pass on
  shadow/clipping.

## Testing plan

- **Manual QA on macOS** (mandatory):
  1. Toggle via Cmd+Y → smooth slide-in/out, no flicker.
  2. Rapid double-toggle → no stuck panel, no white residue.
  3. After hide animation completes, click area where panel was → main
     WebView correctly receives clicks (NSView frame synced).
  4. Resize the panel via the left edge during normal use → still works.
  5. Trigger via `ExpandAndSubmitPrompt` (Cmd+L Ask AI) → prompt still
     submits after slide-in completes.
  6. Toggle in dark mode → animation still smooth, theme correct.
- **Existing browser_tests** (`DaoBrowser*`) — must continue to pass. They
  assert on `GetVisible()` and final layout, not on animation frames.
- **No new tests added.** Layer-animation visual fidelity isn't usefully
  testable in browser_tests; the underlying state machine is exercised by
  existing visibility-toggle tests.

## Open questions

None — design is locked in.
