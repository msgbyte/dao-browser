# Agent Sidebar Toggle Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a smooth 180ms ease-out slide-in/slide-out animation to the right-side AI Agent sidebar (`DaoAgentSidebarView`) when toggled, replacing the current instantaneous show/hide.

**Architecture:** Mirror the proven compositor-layer animation pattern from `DaoSidebarView::AnimateLayerSlide` — promote the Agent sidebar to a paint layer, drive an X-axis `gfx::Transform` animation via `ScopedLayerAnimationSettings`, and use `ImplicitAnimationObserver::OnImplicitAnimationsCompleted` to finalize visibility and re-sync macOS NSView frames via `DeprecatedLayoutImmediately()`.

**Tech Stack:** C++17, Chromium Views (`ui::Layer`, `ui::ImplicitAnimationObserver`, `ui::ScopedLayerAnimationSettings`, `gfx::Transform`, `gfx::Tween`), patch-based build (`src/dao/` + `src/patches/`), npm scripts (`npm run rebuild` / `npm run build:debug` — NEVER call `autoninja`/`ninja`/`siso` directly).

**Reference design doc:** `docs/superpowers/specs/2026-05-10-agent-sidebar-toggle-animation-design.md`

---

## File Structure

- **Modify**: `src/dao/browser/ui/views/dao_agent_sidebar_view.h`
  - Add base class `ui::ImplicitAnimationObserver`
  - Add `OnImplicitAnimationsCompleted()` override
  - Add private `AnimateLayerSlide()` method
  - Add private member `bool animation_target_visible_ = false;`
  - Add include: `ui/compositor/layer_animation_observer.h`

- **Modify**: `src/dao/browser/ui/views/dao_agent_sidebar_view.cc`
  - Add includes: `ui/compositor/layer.h`, `ui/compositor/layer_animator.h`, `ui/compositor/scoped_layer_animation_settings.h`, `ui/gfx/animation/tween.h`, `ui/gfx/geometry/transform.h`, `chrome/browser/ui/views/frame/browser_view.h`
  - Rewrite `Toggle()` to drive the new animation path
  - Implement `AnimateLayerSlide(bool incoming, int slide_distance)`
  - Implement `OnImplicitAnimationsCompleted()`
  - Add early-return guard in `OnResize()` while animation is in flight

No new files. No patch files in `src/patches/` need to change. No BUILD.gn changes (the new headers are already pulled in transitively via `views/view.h` / `compositor/layer.h` chains, but we add them explicitly for clarity — they live in `//ui/compositor` and `//ui/gfx`, both already linked by the `dao_browser` target via existing dependencies).

---

## Task 1: Add animation observer base class and member declarations to header

**Files:**
- Modify: `src/dao/browser/ui/views/dao_agent_sidebar_view.h`

- [ ] **Step 1: Add the new include**

Open `src/dao/browser/ui/views/dao_agent_sidebar_view.h`. Locate the existing include block (lines 7-20). Add the following include alphabetically — it belongs between `ui/base/metadata/...` and `ui/native_theme/...`:

```cpp
#include "ui/compositor/layer_animation_observer.h"
```

The include block should now read (showing the relevant slice):

```cpp
#include "content/public/browser/web_contents_delegate.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/controls/resize_area.h"
```

- [ ] **Step 2: Add `ui::ImplicitAnimationObserver` to the base class list**

Locate the class declaration (line 30):

```cpp
class DaoAgentSidebarView : public views::View,
                            public content::WebContentsDelegate,
                            public views::ResizeAreaDelegate,
                            public ui::NativeThemeObserver {
```

Replace it with:

```cpp
class DaoAgentSidebarView : public views::View,
                            public content::WebContentsDelegate,
                            public views::ResizeAreaDelegate,
                            public ui::NativeThemeObserver,
                            public ui::ImplicitAnimationObserver {
```

- [ ] **Step 3: Declare the override and the private helper**

Locate the existing override section (around line 77-78):

```cpp
  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;
```

Immediately AFTER it (and before the `private:` keyword on line 80), add:

```cpp
  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsCompleted() override;
```

- [ ] **Step 4: Declare the new private method and member**

Inside the `private:` section, locate the existing helper declarations (around line 81-87, the `EnsureLoaded()` / `ApplyTheme()` / `TryFlushPendingPrompt()` block). Add this declaration immediately after `TryFlushPendingPrompt`:

```cpp
  // Drives the slide-in / slide-out animation by transforming this view's
  // compositor layer along the X axis. `incoming=true` plays the off-screen
  // -> identity slide; `incoming=false` plays identity -> off-screen.
  // `slide_distance` is the panel width (always positive).
  void AnimateLayerSlide(bool incoming, int slide_distance);
```

Then locate the existing data member section (around lines 89-110). Add the new tracking member just before `current_width_` (line 107):

```cpp
  // Latest commanded direction for the running slide animation. The
  // ImplicitAnimationObserver callback may fire after another Toggle() has
  // already flipped expanded_ — branching on this flag instead of expanded_
  // ensures the callback finalizes the *correct* direction.
  bool animation_target_visible_ = false;
```

- [ ] **Step 5: Build to verify the header compiles**

Run: `npm run rebuild`

Expected: build succeeds. The implementation in the .cc file is incomplete at this point — but because the .cc file does not yet override `OnImplicitAnimationsCompleted` or implement `AnimateLayerSlide`, the build will fail with "undefined reference" or "must implement pure virtual" errors. **This is expected** — proceed to Task 2 to add the implementations. (If you hit a compile error in the *header* itself, e.g. an unknown symbol from the new include, fix that before proceeding.)

If the build is unhappy with `ui::ImplicitAnimationObserver` being abstract (it has a pure virtual `OnImplicitAnimationsCompleted`), that confirms the header changes parsed correctly and you just need to implement the override in Task 2.

- [ ] **Step 6: Commit the header changes**

Skip this commit — keep the header and implementation changes in a single commit at the end of Task 2 so the tree never has a half-implemented state.

---

## Task 2: Implement Toggle/AnimateLayerSlide/OnImplicitAnimationsCompleted in the .cc file

**Files:**
- Modify: `src/dao/browser/ui/views/dao_agent_sidebar_view.cc`

- [ ] **Step 1: Add new includes**

Open `src/dao/browser/ui/views/dao_agent_sidebar_view.cc`. Locate the include block (lines 5-27). Add these includes in the correct alphabetical position (Chromium style sorts by full path within each group):

After the existing `#include "chrome/browser/profiles/profile.h"` line, add:

```cpp
#include "chrome/browser/ui/views/frame/browser_view.h"
```

After the existing `#include "dao/browser/ui/views/dao_colors.h"` line, add:

```cpp
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/geometry/transform.h"
```

- [ ] **Step 2: Replace the body of Toggle()**

Locate the current `Toggle()` implementation at line 207:

```cpp
bool DaoAgentSidebarView::Toggle() {
  expanded_ = !expanded_;

  if (expanded_) {
    EnsureLoaded();
    SetVisible(true);
  } else {
    SetVisible(false);
  }

  // Single layout pass — web content repaints exactly once.
  PreferredSizeChanged();

  if (expanded_) {
    // Route keyboard focus into the WebView so the agent input can auto-focus
    // after its visibilitychange handler runs. Deferred so the focus lands
    // after layout + visibility propagation.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<DaoAgentSidebarView> self) {
              if (!self || !self->expanded_ || !self->web_view_) {
                return;
              }
              self->web_view_->RequestFocus();
            },
            weak_factory_.GetWeakPtr()));
  }

  return expanded_;
}
```

Replace the entire function with:

```cpp
bool DaoAgentSidebarView::Toggle() {
  // Cancel any in-flight slide before starting a new one so the layer
  // transform settles deterministically. Without this, a fast double-toggle
  // can leave residual transforms.
  if (layer() && layer()->GetAnimator()->is_animating()) {
    layer()->GetAnimator()->StopAnimating();
  }

  expanded_ = !expanded_;
  animation_target_visible_ = expanded_;

  if (expanded_) {
    EnsureLoaded();
    SetVisible(true);
    // Commit the final layout immediately so the panel reserves its slot;
    // the slide animation runs on top of the now-final layout.
    PreferredSizeChanged();
    AnimateLayerSlide(/*incoming=*/true, current_width_);

    // Route keyboard focus into the WebView so the agent input can auto-focus
    // after its visibilitychange handler runs. Deferred so the focus lands
    // after layout + visibility propagation.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<DaoAgentSidebarView> self) {
              if (!self || !self->expanded_ || !self->web_view_) {
                return;
              }
              self->web_view_->RequestFocus();
            },
            weak_factory_.GetWeakPtr()));
  } else {
    // Stay visible during the slide-out; OnImplicitAnimationsCompleted will
    // call SetVisible(false) + PreferredSizeChanged() once the animation
    // ends. If we hid + relayouted now, the animation would have nothing to
    // play on.
    AnimateLayerSlide(/*incoming=*/false, current_width_);
  }

  return expanded_;
}
```

- [ ] **Step 3: Implement AnimateLayerSlide()**

Add the following function definition AFTER `Toggle()` and BEFORE `CalculatePreferredSize()` (which begins at line 239):

```cpp
void DaoAgentSidebarView::AnimateLayerSlide(bool incoming,
                                            int slide_distance) {
  // If the view isn't attached to a Widget yet, there is no compositor and
  // no layer animator. Fall back to snap behavior: just finalize visibility
  // and skip the animation. This path is exercised during construction and
  // by tests that call Toggle() before the BrowserView is shown.
  if (!GetWidget()) {
    if (!incoming) {
      SetVisible(false);
      PreferredSizeChanged();
    }
    return;
  }

  // Promote to a paint layer the first time we animate. SetFillsBoundsOpaquely
  // is false because the panel does not have its own opaque background — the
  // child WebView paints its own background, and the resize handle is narrow.
  if (!layer()) {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
  }

  constexpr auto kDuration = base::Milliseconds(180);

  gfx::Transform off_screen_right;
  off_screen_right.Translate(slide_distance, 0);

  if (incoming) {
    // Snap to the off-screen start position before opening the animation
    // settings block, so the interpolation runs from off-screen to identity.
    layer()->SetTransform(off_screen_right);
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(kDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    settings.AddObserver(this);
    layer()->SetTransform(gfx::Transform());
  } else {
    // Layer is currently at identity (panel on-screen). Animate it out to
    // the right.
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(kDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    settings.AddObserver(this);
    layer()->SetTransform(off_screen_right);
  }
}
```

- [ ] **Step 4: Implement OnImplicitAnimationsCompleted()**

Add the following function definition AFTER `AnimateLayerSlide()` and BEFORE `CalculatePreferredSize()`:

```cpp
void DaoAgentSidebarView::OnImplicitAnimationsCompleted() {
  // Always normalize the transform back to identity. Even after a successful
  // slide-in this is needed because residual float-rounded transforms can
  // leave macOS NSView frames stale (NativeViewHost::Layout reads
  // ConvertRectToWidget, which is transform-aware).
  if (layer()) {
    layer()->SetTransform(gfx::Transform());
  }

  if (!animation_target_visible_) {
    // Slide-out finished — actually hide and shrink the layout slot.
    SetVisible(false);
    PreferredSizeChanged();
  }

  // Force NativeViewHost to re-`setFrame:` its NSView synchronously, both
  // for our own WebView and for the BrowserView's main content container
  // whose width just changed.
  if (web_view_) {
    web_view_->InvalidateLayout();
    web_view_->DeprecatedLayoutImmediately();
  }
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  if (bv && bv->contents_container()) {
    bv->contents_container()->InvalidateLayout();
    bv->contents_container()->DeprecatedLayoutImmediately();
  }
}
```

- [ ] **Step 5: Add the resize-during-animation guard**

Locate `OnResize()` at line 266:

```cpp
void DaoAgentSidebarView::OnResize(int resize_amount, bool done_resizing) {
  if (!expanded_) {
    return;
  }
```

Replace the function header + first guard with:

```cpp
void DaoAgentSidebarView::OnResize(int resize_amount, bool done_resizing) {
  if (!expanded_) {
    return;
  }

  // Ignore drag input while a slide animation is playing — the user can't
  // meaningfully aim at the resize handle when it's translating across the
  // screen, and applying the delta would fight the animator.
  if (layer() && layer()->GetAnimator()->is_animating()) {
    return;
  }
```

- [ ] **Step 6: Build to verify implementation compiles**

Run: `npm run rebuild`

Expected: build succeeds with no errors. If you get linker errors about `BrowserView::contents_container()` not being public, check that `chrome/browser/ui/views/frame/browser_view.h` was added to the include list in Step 1 — it is the canonical declaration of that accessor.

If you see a compile error like `undefined reference to ui::ImplicitAnimationObserver::OnImplicitAnimationsCompleted`, it means the override signature doesn't match. Verify your `.h` declaration says exactly:

```cpp
void OnImplicitAnimationsCompleted() override;
```

(no parameters, no const).

- [ ] **Step 7: Manual smoke test**

Run: `npm run start:debug`

In the launched browser, perform this checklist:

1. Press the Agent sidebar toggle (Cmd+Y, or whatever the existing shortcut is — check the menu under View if unsure). The right panel should slide in smoothly from the right edge over ~180ms with ease-out timing.
2. Press toggle again. The panel should slide out to the right over ~180ms.
3. Toggle rapidly (3-4 times in a row). The panel should never get stuck mid-screen, and after the dust settles its visibility should match the final command. There should be no white residual rectangle.
4. With panel hidden, click in the area where it used to be (right side of window). The main WebView should receive the click — if the click is dead, NSView frame sync is broken; revisit Step 4.
5. With panel visible, drag the left edge to resize it. Resize should still work. Then quickly toggle while resizing — resize input should be ignored mid-animation, then re-enabled when animation completes.
6. Trigger via Cmd+L "Ask AI" path (which calls `ExpandAndSubmitPrompt`). The panel should slide in, and the prompt should still submit after slide-in completes (the existing pending-prompt machinery is independent of the animation).
7. Toggle while in dark mode (System Settings → Appearance → Dark). Animation should play identically.

If any item fails, do not commit — fix the issue in this task before proceeding.

- [ ] **Step 8: Run existing browser tests**

Run: `npm run test`

Expected: all `Dao*` tests pass. They assert on visibility / layout state at rest, not on animation frames, so the new animation should not regress them. If a test that previously passed now times out, it may be waiting for `GetVisible() == false` between the Toggle call and the actual SetVisible(false) which is now deferred to `OnImplicitAnimationsCompleted` — review the failing test and decide whether to:
  - update the test to wait for animation completion (use `ui::LayerAnimationStoppedWaiter` or pump the message loop),
  - or use the no-Widget fallback branch (tests typically run before Widget attachment).

If a test fails for this reason, capture the failure output, do NOT modify the test silently — surface it to the user.

- [ ] **Step 9: Commit header + implementation together**

Run:

```bash
git add src/dao/browser/ui/views/dao_agent_sidebar_view.h src/dao/browser/ui/views/dao_agent_sidebar_view.cc
git status
```

Expected: only those two files staged.

Then commit:

```bash
git commit -m "feat(agent-sidebar): add slide-in/out animation on toggle

Replace the instantaneous SetVisible/PreferredSizeChanged toggle with a
180ms ease-out compositor-layer slide. Mirrors the proven pattern from
DaoSidebarView::AnimateLayerSlide, with the panel sliding from the right
edge.

The hide path now defers SetVisible(false) + PreferredSizeChanged() to
OnImplicitAnimationsCompleted so the animation has something to play on.
Fast double-toggles call StopAnimating() before reconfiguring, and
resize input is ignored while an animation is in flight.

macOS NSView frame sync uses the same DeprecatedLayoutImmediately pattern
as the left sidebar to avoid stale ConvertRectToWidget hit-testing after
a residual transform."
```

If the user has previously asked you not to auto-commit, ask before running the commit. Otherwise, only run the commit if the user has explicitly asked you to commit; per CLAUDE.md, **never commit automatically**. Mark this step done after `git add` + `git status`, and stop — let the user run the actual `git commit`.

---

## Self-Review Notes

**Spec coverage:**
- ✅ 180ms ease-out — Task 2 Step 3 sets `kDuration = 180ms`, `Tween::EASE_OUT`.
- ✅ Slide direction (right edge) — Task 2 Step 3 uses `Translate(slide_distance, 0)` for both directions.
- ✅ No content fade — no opacity animation anywhere; only transform.
- ✅ Show: SetVisible + layout commit + animate — Task 2 Step 2 expanded branch.
- ✅ Hide: animate first, finalize in observer — Task 2 Step 2 collapsed branch + Step 4 observer.
- ✅ macOS NSView frame sync — Task 2 Step 4 calls `DeprecatedLayoutImmediately()` on web_view_ and contents_container().
- ✅ Mid-animation re-toggle — Task 2 Step 2 calls `StopAnimating()` at top of Toggle().
- ✅ Resize during animation — Task 2 Step 5 adds the early-return guard.
- ✅ Pre-Widget fallback — Task 2 Step 3 returns early if `!GetWidget()`.
- ✅ Existing browser_tests pass — Task 2 Step 8 verifies.
- ✅ No new browser_tests — by design.

**Placeholder scan:** none. All code blocks are complete and the include paths are concrete.

**Type consistency:** `AnimateLayerSlide(bool incoming, int slide_distance)` declared in Task 1 Step 4, defined in Task 2 Step 3 — same signature. `animation_target_visible_` declared in Task 1 Step 4, read in Task 2 Step 4 — same name. `OnImplicitAnimationsCompleted()` declared in Task 1 Step 3, defined in Task 2 Step 4 — same signature.
