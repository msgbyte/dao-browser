# Page Load Progress Bar — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a thin animated load-progress bar along the top edge of the
rounded content card, driven by the active tab's real
`WebContents::GetLoadProgress()`.

**Architecture:** A passive `DaoLoadProgressView` paints a 2 px blue bar with
soft halo, animates its width via `gfx::LinearAnimation`, and fades out via
layer opacity animation. A separate `DaoLoadProgressController` owns the
`TabStripModelObserver` + `WebContentsObserver` plumbing and drives the
view's state machine (Hidden → Loading → Completing → FadingOut). Layout is
handled by `BrowserViewTabbedLayoutImpl`, matching the existing pattern used
for `dao_corner_overlay_` and `dao_address_bar_`.

**Tech Stack:** C++17, Chromium views (`views::View`, `ui::Layer`,
`gfx::LinearAnimation`, `gfx::Canvas`), `content::WebContentsObserver`,
`TabStripModelObserver`, `dao::` namespace, GN build, browser_tests.

**Spec:** `docs/superpowers/specs/2026-05-19-page-load-progress-design.md`

---

## Background notes for the implementer

Read these once before starting; they save time later:

- **Never edit `engine/` directly.** All Chromium-side changes go through
  `src/patches/*.patch`. All Dao C++ code goes under `src/dao/`. Use
  `npm run import` to apply, `npm run export -- <file>` to capture changes
  back. Bare `npm run export` is destructive — always scope it.
- **Build only with `npm run rebuild` or `npm run build:debug`.** Never run
  `autoninja`/`ninja`/`siso`/`gn gen` directly. If build state corrupts:
  `gn clean out/dao-debug` then `npm run build:debug`.
- **Accessible name rule:** every focusable view must have one. Our view is
  not focusable (it's pure decoration, event-transparent), so this doesn't
  apply, but be aware.
- **Run only Dao tests during dev:**
  `./engine/src/out/dao-debug/browser_tests --gtest_filter="DaoLoadProgress*"`
  (after `npm run test:build` once).

---

## File Structure

**Create:**
- `src/dao/browser/ui/views/dao_load_progress_view.h`
- `src/dao/browser/ui/views/dao_load_progress_view.cc`
- `src/dao/browser/ui/views/dao_load_progress_controller.h`
- `src/dao/browser/ui/views/dao_load_progress_controller.cc`

**Modify (via patches in `src/patches/`):**
- `chrome/browser/ui/BUILD.gn.patch` — register new sources
- `chrome/browser/ui/views/frame/browser_view.h.patch` — declare members + accessor
- `chrome/browser/ui/views/frame/browser_view.cc.patch` — construct view + controller
- `chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc.patch` — lay out the bar

**Modify (test):**
- `src/dao/browser/ui/views/dao_browser_browsertest.cc` — add `DaoLoadProgress*` tests

---

## Task 1: Skeleton `DaoLoadProgressView` (header + empty impl, painted nothing)

Establish the view class, its layer, and the build wiring. No animation yet —
we just need it to exist, be event-transparent, and compile.

**Files:**
- Create: `src/dao/browser/ui/views/dao_load_progress_view.h`
- Create: `src/dao/browser/ui/views/dao_load_progress_view.cc`
- Modify: `src/patches/chrome/browser/ui/BUILD.gn.patch`

- [ ] **Step 1: Write the header**

Create `src/dao/browser/ui/views/dao_load_progress_view.h`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_VIEW_H_

#include <memory>

#include "base/timer/timer.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/views/view.h"

namespace dao {

// Thin (2 px) blue progress bar with a soft halo, painted along the top edge
// of the rounded content card. Driven by DaoLoadProgressController which
// pushes real load progress values from the active tab's WebContents.
//
// Visual states (internal, not exposed):
//   Hidden     — opacity 0, no paint
//   Loading    — opacity 1, displayed_progress_ eases toward target_progress_
//   Completing — target pinned to 1.0, brief hold timer running
//   FadingOut  — layer opacity animates 1 → 0
class DaoLoadProgressView : public views::View,
                            public gfx::AnimationDelegate {
  METADATA_HEADER(DaoLoadProgressView, views::View)

 public:
  DaoLoadProgressView();
  DaoLoadProgressView(const DaoLoadProgressView&) = delete;
  DaoLoadProgressView& operator=(const DaoLoadProgressView&) = delete;
  ~DaoLoadProgressView() override;

  // Called by DaoLoadProgressController. `animate=false` snaps without easing
  // (used on tab switch and on backward progress).
  void SetTargetProgress(double progress, bool animate);
  // Reset to 0, enter Loading state, make visible.
  void StartLoading();
  // Pin to 1.0, hold ~150 ms, then fade out.
  void FinishLoading();
  // Cancel everything and hide instantly (no fade).
  void HideImmediately();

  // For tests.
  double displayed_progress_for_testing() const { return displayed_progress_; }
  bool is_loading_for_testing() const { return state_ == State::kLoading; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationCanceled(const gfx::Animation* animation) override;

 private:
  enum class State {
    kHidden,
    kLoading,
    kCompleting,
    kFadingOut,
  };

  void StartProgressAnimation(double from, double to);
  void StartFadeOut();
  void EnterHidden();

  State state_ = State::kHidden;
  double displayed_progress_ = 0.0;
  double target_progress_ = 0.0;
  double animation_start_progress_ = 0.0;

  std::unique_ptr<gfx::LinearAnimation> progress_animation_;
  base::OneShotTimer hold_timer_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_VIEW_H_
```

- [ ] **Step 2: Write the minimal implementation (no animation, no paint)**

Create `src/dao/browser/ui/views/dao_load_progress_view.cc`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_load_progress_view.h"

#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"

namespace dao {

BEGIN_METADATA(DaoLoadProgressView)
END_METADATA

DaoLoadProgressView::DaoLoadProgressView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0.0f);
  SetCanProcessEventsWithinSubtree(false);
}

DaoLoadProgressView::~DaoLoadProgressView() = default;

void DaoLoadProgressView::SetTargetProgress(double progress, bool animate) {}
void DaoLoadProgressView::StartLoading() {}
void DaoLoadProgressView::FinishLoading() {}
void DaoLoadProgressView::HideImmediately() {}

void DaoLoadProgressView::OnPaint(gfx::Canvas* canvas) {}

void DaoLoadProgressView::AnimationProgressed(const gfx::Animation*) {}
void DaoLoadProgressView::AnimationEnded(const gfx::Animation*) {}
void DaoLoadProgressView::AnimationCanceled(const gfx::Animation*) {}

void DaoLoadProgressView::StartProgressAnimation(double, double) {}
void DaoLoadProgressView::StartFadeOut() {}
void DaoLoadProgressView::EnterHidden() {}

}  // namespace dao
```

- [ ] **Step 3: Register new sources in BUILD.gn.patch**

Edit `src/patches/chrome/browser/ui/BUILD.gn.patch`. Locate the line
`"//dao/browser/ui/views/dao_corner_overlay_view.cc",` and add two new
entries immediately after the corner overlay pair, keeping alphabetic-ish
order within the dao block (the existing block doesn't strictly sort, so
keeping new entries clustered together is fine):

```diff
     "//dao/browser/ui/views/dao_corner_overlay_view.cc",
     "//dao/browser/ui/views/dao_corner_overlay_view.h",
+    "//dao/browser/ui/views/dao_load_progress_controller.cc",
+    "//dao/browser/ui/views/dao_load_progress_controller.h",
+    "//dao/browser/ui/views/dao_load_progress_view.cc",
+    "//dao/browser/ui/views/dao_load_progress_view.h",
     "//dao/browser/ui/views/dao_toast_view.cc",
     "//dao/browser/ui/views/dao_toast_view.h",
```

(The controller files are added now even though they're created in Task 4 —
adding them upfront avoids a second BUILD.gn round-trip. We'll create
placeholder controller files in this task too so the build is not broken.)

- [ ] **Step 4: Create placeholder controller files so the build stays green**

Create `src/dao/browser/ui/views/dao_load_progress_controller.h`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_
#define DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_

namespace dao {

// Placeholder — full declaration lands in Task 4.
class DaoLoadProgressController {};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_
```

Create `src/dao/browser/ui/views/dao_load_progress_controller.cc`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_load_progress_controller.h"

namespace dao {}  // namespace dao
```

- [ ] **Step 5: Build to verify skeleton compiles**

Run: `npm run rebuild`
Expected: build succeeds, no link errors. Nothing yet appears in the UI.

- [ ] **Step 6: Commit**

```bash
git add src/dao/browser/ui/views/dao_load_progress_view.h \
        src/dao/browser/ui/views/dao_load_progress_view.cc \
        src/dao/browser/ui/views/dao_load_progress_controller.h \
        src/dao/browser/ui/views/dao_load_progress_controller.cc \
        src/patches/chrome/browser/ui/BUILD.gn.patch
git commit -m "feat(load-progress): scaffold DaoLoadProgressView skeleton"
```

---

## Task 2: Paint the bar + glow

Implement `OnPaint` so a static (non-animated) bar can be observed. We'll
manually drive `displayed_progress_` from a test next.

**Files:**
- Modify: `src/dao/browser/ui/views/dao_load_progress_view.cc`

- [ ] **Step 1: Write a failing test (asserts the bar paints at a given progress)**

Edit `src/dao/browser/ui/views/dao_browser_browsertest.cc`. Near the bottom
of the file (after the last existing test class but before any final
`namespace` close brace if present — match the file's current layout), add:

```cpp
class DaoLoadProgressBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoLoadProgressBrowserTest, ViewExistsAndIsHiddenByDefault) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* progress = browser_view->dao_load_progress();
  ASSERT_TRUE(progress);
  // The view exists but layer opacity should be 0 (hidden state).
  EXPECT_EQ(progress->layer()->opacity(), 0.0f);
  EXPECT_EQ(progress->displayed_progress_for_testing(), 0.0);
}
```

(This test references `browser_view->dao_load_progress()` and the testing
accessor on the view — both will be added in Task 3 and this Task 2.
We are intentionally writing the test ahead of those wires so it fails for
a real reason. Add `#include "dao/browser/ui/views/dao_load_progress_view.h"`
near the other dao view includes at the top of the test file if not already
there.)

- [ ] **Step 2: Run the test and confirm it fails to compile**

Run: `npm run test:build`
Expected: compile error — `dao_load_progress()` is not a member of
`BrowserView`. Good. Leave the test in place; Task 3 will fix it.

- [ ] **Step 3: Implement `OnPaint`**

Edit `src/dao/browser/ui/views/dao_load_progress_view.cc`. Add includes near
the top:

```cpp
#include <algorithm>
#include <cmath>

#include "cc/paint/paint_flags.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "third_party/skia/include/core/SkColor.h"
```

Replace the empty `OnPaint` with:

```cpp
void DaoLoadProgressView::OnPaint(gfx::Canvas* canvas) {
  if (state_ == State::kHidden) {
    return;
  }

  // Visual constants — keep in sync with the design spec.
  constexpr int kBarHeight = 2;
  constexpr int kHaloHeight = 1;       // halo extends kHaloHeight above and below
  constexpr int kHaloAlpha = 40;       // out of 255

  const SkColor accent =
      SkColorSetRGB(70, 120, 190);  // matches dao::AccentColor()

  const double p = std::clamp(displayed_progress_, 0.0, 1.0);
  const int fill_w = static_cast<int>(std::round(width() * p));
  if (fill_w <= 0) {
    return;
  }

  // The view's height is kBarHeight + 2*kHaloHeight = 4 px. The main fill is
  // centered vertically.
  const int center_y = height() / 2;
  const int main_top = center_y - kBarHeight / 2;

  cc::PaintFlags halo_flags;
  halo_flags.setAntiAlias(false);
  halo_flags.setStyle(cc::PaintFlags::kFill_Style);
  halo_flags.setColor(SkColorSetA(accent, kHaloAlpha));
  // Halo above
  canvas->DrawRect(gfx::Rect(0, main_top - kHaloHeight, fill_w, kHaloHeight),
                   halo_flags);
  // Halo below
  canvas->DrawRect(
      gfx::Rect(0, main_top + kBarHeight, fill_w, kHaloHeight),
      halo_flags);

  cc::PaintFlags fill_flags;
  fill_flags.setAntiAlias(false);
  fill_flags.setStyle(cc::PaintFlags::kFill_Style);
  fill_flags.setColor(accent);
  canvas->DrawRect(gfx::Rect(0, main_top, fill_w, kBarHeight), fill_flags);
}
```

- [ ] **Step 4: Build (test will still fail to compile — that's expected at this stage)**

Run: `npm run rebuild`
Expected: build succeeds (the production code compiles). The test still
fails to compile because the `dao_load_progress()` accessor doesn't exist
yet — Task 3 adds it. Don't run tests now.

- [ ] **Step 5: Commit**

```bash
git add src/dao/browser/ui/views/dao_load_progress_view.cc \
        src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "feat(load-progress): paint bar + halo in DaoLoadProgressView"
```

---

## Task 3: Wire `DaoLoadProgressView` into `BrowserView` + layout

Add the view as a child of `BrowserView`, expose an accessor, and lay it out
above the content card top edge.

**Files:**
- Modify: `src/patches/chrome/browser/ui/views/frame/browser_view.h.patch`
- Modify: `src/patches/chrome/browser/ui/views/frame/browser_view.cc.patch`
- Modify: `src/patches/chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc.patch`

- [ ] **Step 1: Edit `browser_view.h.patch` — add the accessor and member**

In `src/patches/chrome/browser/ui/views/frame/browser_view.h.patch`, find
the line `+  dao::DaoCornerOverlayView* dao_corner_overlay() { return dao_corner_overlay_; }`
and add an accessor immediately after it:

```diff
+  dao::DaoCornerOverlayView* dao_corner_overlay() { return dao_corner_overlay_; }
+  dao::DaoLoadProgressView* dao_load_progress() { return dao_load_progress_; }
```

Then find `+  raw_ptr<dao::DaoCornerOverlayView> dao_corner_overlay_ = nullptr;`
and add a member immediately after it:

```diff
+  raw_ptr<dao::DaoCornerOverlayView> dao_corner_overlay_ = nullptr;
+  raw_ptr<dao::DaoLoadProgressView> dao_load_progress_ = nullptr;
+  std::unique_ptr<dao::DaoLoadProgressController>
+      dao_load_progress_controller_;
```

Find the forward declarations for `DaoCornerOverlayView` and
`DaoLoadProgressView` (look near the top of the diff where other dao
forward decls live). Add:

```diff
 namespace dao {
 class DaoCornerOverlayView;
+class DaoLoadProgressController;
+class DaoLoadProgressView;
```

(If the patch instead pulls in full headers via `#include`, add the includes
the same way the corner overlay one is added — match the surrounding style.
Check `src/patches/chrome/browser/ui/views/frame/browser_view.h.patch` first
to see which style is used.)

- [ ] **Step 2: Edit `browser_view.cc.patch` — include the header and create the view**

In `src/patches/chrome/browser/ui/views/frame/browser_view.cc.patch`, find
the line `+#include "dao/browser/ui/views/dao_corner_overlay_view.h"` and add:

```diff
 +#include "dao/browser/ui/views/dao_corner_overlay_view.h"
++#include "dao/browser/ui/views/dao_load_progress_controller.h"
++#include "dao/browser/ui/views/dao_load_progress_view.h"
```

Then find the block:

```cpp
+    // Dao: Corner overlay painted on top of web contents
+    dao_corner_overlay_ = AddChildView(
+        std::make_unique<dao::DaoCornerOverlayView>());
```

Add immediately after:

```diff
 +    // Dao: Corner overlay painted on top of web contents
 +    dao_corner_overlay_ = AddChildView(
 +        std::make_unique<dao::DaoCornerOverlayView>());
++
++    // Dao: Load progress bar across the top edge of the content card.
++    dao_load_progress_ = AddChildView(
++        std::make_unique<dao::DaoLoadProgressView>());
++    dao_load_progress_controller_ =
++        std::make_unique<dao::DaoLoadProgressController>(
++            browser_->tab_strip_model(), dao_load_progress_);
```

(The controller is currently a placeholder empty class — passing those args
won't compile. We'll fix that in Task 4 by giving the controller a real
constructor. To keep this task's build green, also update the placeholder
controller header now.)

- [ ] **Step 3: Update the placeholder controller to accept the args**

This is a transitional placeholder — the full implementation lands in
Task 4. For now, edit `src/dao/browser/ui/views/dao_load_progress_controller.h`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_
#define DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_

#include "base/memory/raw_ptr.h"

class TabStripModel;

namespace dao {

class DaoLoadProgressView;

// Placeholder accepting its real ctor signature so BrowserView can wire it
// in now. Real observation logic lands in Task 4.
class DaoLoadProgressController {
 public:
  DaoLoadProgressController(TabStripModel* tab_strip_model,
                            DaoLoadProgressView* view);
  DaoLoadProgressController(const DaoLoadProgressController&) = delete;
  DaoLoadProgressController& operator=(const DaoLoadProgressController&) =
      delete;
  ~DaoLoadProgressController();

 private:
  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<DaoLoadProgressView> view_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_
```

And `dao_load_progress_controller.cc`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_load_progress_controller.h"

namespace dao {

DaoLoadProgressController::DaoLoadProgressController(
    TabStripModel* tab_strip_model,
    DaoLoadProgressView* view)
    : tab_strip_model_(tab_strip_model), view_(view) {}

DaoLoadProgressController::~DaoLoadProgressController() = default;

}  // namespace dao
```

- [ ] **Step 4: Edit `browser_view_tabbed_layout_impl.cc.patch` — lay out the bar**

In `src/patches/chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc.patch`,
add an include near the other dao view includes:

```diff
 +#include "dao/browser/ui/views/dao_corner_overlay_view.h"
++#include "dao/browser/ui/views/dao_load_progress_view.h"
```

Then find the block that lays out `dao_address_bar`:

```cpp
+    if (auto* const address_bar = dao_bv->dao_address_bar()) {
+      if (dao_sidebar_active && !dao_true_fullscreen && !dao_split_active) {
+        const int bar_height = dao::DaoAddressBarView::kBarHeight;
+        layout.AddChild(
+            address_bar,
+            gfx::Rect(dao_content_area.x(), dao_content_area.y(),
+                      dao_content_area.width(), bar_height),
+            true);
+        dao_content_area.Inset(gfx::Insets::TLBR(bar_height - 1, 0, 0, 0));
+      } else {
+        layout.AddChild(address_bar, gfx::Rect(), false);
+      }
+    }
```

Add a new block immediately after it (the progress bar goes on top of the
remaining content area, after the address bar has carved its own slice):

```diff
 +      } else {
 +        layout.AddChild(address_bar, gfx::Rect(), false);
 +      }
 +    }
++
++    // Load progress bar pinned to the top edge of the remaining content area.
++    // Total view height is 4 px (2 px main bar + 1 px halo on each side);
++    // the main fill is centered vertically inside that.
++    if (auto* const load_progress = dao_bv->dao_load_progress()) {
++      if (dao_sidebar_active && !dao_true_fullscreen) {
++        constexpr int kProgressViewHeight = 4;
++        // Center the 4 px tall view on the top edge of dao_content_area so
++        // the halo extends both up (over the strip color) and down (over
++        // the page).
++        layout.AddChild(
++            load_progress,
++            gfx::Rect(dao_content_area.x(),
++                      dao_content_area.y() - kProgressViewHeight / 2,
++                      dao_content_area.width(), kProgressViewHeight),
++            true);
++      } else {
++        layout.AddChild(load_progress, gfx::Rect(), false);
++      }
++    }
```

- [ ] **Step 5: Build**

Run: `npm run rebuild`
Expected: build succeeds.

- [ ] **Step 6: Run the existence test from Task 2**

Run:
```bash
npm run test:build
./engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoLoadProgressBrowserTest.ViewExistsAndIsHiddenByDefault"
```
Expected: PASS.

- [ ] **Step 7: Smoke-check visually**

Run: `npm run start:debug`
Navigate to e.g. `https://example.com`. The bar isn't driven yet (controller
is still a stub), so you should NOT see any visible progress bar. Confirm
that nothing else regressed visually (sidebar, address bar, corner shadow
still look correct). Quit the browser.

- [ ] **Step 8: Commit**

```bash
git add src/patches/chrome/browser/ui/views/frame/browser_view.h.patch \
        src/patches/chrome/browser/ui/views/frame/browser_view.cc.patch \
        src/patches/chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc.patch \
        src/dao/browser/ui/views/dao_load_progress_controller.h \
        src/dao/browser/ui/views/dao_load_progress_controller.cc
git commit -m "feat(load-progress): wire DaoLoadProgressView into BrowserView + layout"
```

---

## Task 4: Real `DaoLoadProgressController` (TabStripModel + WebContents observation)

Replace the placeholder with the real controller. It observes the active
`WebContents` and calls into the view. The view's state methods are still
no-ops at this point — they get filled in in Task 5.

**Files:**
- Modify: `src/dao/browser/ui/views/dao_load_progress_controller.h`
- Modify: `src/dao/browser/ui/views/dao_load_progress_controller.cc`

- [ ] **Step 1: Write the real header**

Replace `src/dao/browser/ui/views/dao_load_progress_controller.h`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_
#define DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"

class TabStripModel;

namespace content {
class WebContents;
}

namespace dao {

class DaoLoadProgressView;

// Owns the plumbing that drives DaoLoadProgressView from the active tab's
// real load events.
class DaoLoadProgressController : public TabStripModelObserver,
                                  public content::WebContentsObserver {
 public:
  DaoLoadProgressController(TabStripModel* tab_strip_model,
                            DaoLoadProgressView* view);
  DaoLoadProgressController(const DaoLoadProgressController&) = delete;
  DaoLoadProgressController& operator=(const DaoLoadProgressController&) =
      delete;
  ~DaoLoadProgressController() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void LoadProgressChanged(double progress) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void WebContentsDestroyed() override;

 private:
  // Re-point at `new_contents`; resync the view to its current state.
  void AttachToWebContents(content::WebContents* new_contents);

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<DaoLoadProgressView> view_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_
```

- [ ] **Step 2: Write the real implementation**

Replace `src/dao/browser/ui/views/dao_load_progress_controller.cc`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_load_progress_controller.h"

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_load_progress_view.h"

namespace dao {

DaoLoadProgressController::DaoLoadProgressController(
    TabStripModel* tab_strip_model,
    DaoLoadProgressView* view)
    : tab_strip_model_(tab_strip_model), view_(view) {
  tab_strip_model_->AddObserver(this);
  AttachToWebContents(tab_strip_model_->GetActiveWebContents());
}

DaoLoadProgressController::~DaoLoadProgressController() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
  // WebContentsObserver auto-detaches in its destructor.
}

void DaoLoadProgressController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    AttachToWebContents(selection.new_contents);
  }
}

void DaoLoadProgressController::AttachToWebContents(
    content::WebContents* new_contents) {
  // Detach from previous (no-op if same).
  Observe(new_contents);

  if (!new_contents) {
    view_->HideImmediately();
    return;
  }

  if (new_contents->IsLoading()) {
    // Sync the view to the new tab's current progress without animating.
    view_->StartLoading();
    view_->SetTargetProgress(new_contents->GetLoadProgress(),
                             /*animate=*/false);
  } else {
    view_->HideImmediately();
  }
}

void DaoLoadProgressController::LoadProgressChanged(double progress) {
  view_->SetTargetProgress(progress, /*animate=*/true);
}

void DaoLoadProgressController::DidStartLoading() {
  view_->StartLoading();
}

void DaoLoadProgressController::DidStopLoading() {
  view_->FinishLoading();
}

void DaoLoadProgressController::WebContentsDestroyed() {
  view_->HideImmediately();
}

}  // namespace dao
```

- [ ] **Step 3: Build**

Run: `npm run rebuild`
Expected: build succeeds. Still no visible bar because the view's state
methods are no-ops.

- [ ] **Step 4: Commit**

```bash
git add src/dao/browser/ui/views/dao_load_progress_controller.h \
        src/dao/browser/ui/views/dao_load_progress_controller.cc
git commit -m "feat(load-progress): real DaoLoadProgressController observing tabs + WebContents"
```

---

## Task 5: View state machine + progress animation

Implement the view's `SetTargetProgress`, `StartLoading`, `FinishLoading`,
`HideImmediately`, the `gfx::LinearAnimation` integration, and the hold
timer. After this task the bar should be visible and animate during real
page loads, but it won't fade out yet — completion just snaps to hidden.

**Files:**
- Modify: `src/dao/browser/ui/views/dao_load_progress_view.cc`

- [ ] **Step 1: Add a test for StartLoading**

Add to `dao_browser_browsertest.cc`:

```cpp
IN_PROC_BROWSER_TEST_F(DaoLoadProgressBrowserTest,
                       StartLoadingMakesBarVisible) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* progress = browser_view->dao_load_progress();
  ASSERT_TRUE(progress);

  progress->StartLoading();
  EXPECT_TRUE(progress->is_loading_for_testing());
  EXPECT_GT(progress->layer()->opacity(), 0.0f);
  EXPECT_EQ(progress->displayed_progress_for_testing(), 0.0);
}
```

- [ ] **Step 2: Run the test to confirm it fails**

Run:
```bash
npm run test:build
./engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoLoadProgressBrowserTest.StartLoadingMakesBarVisible"
```
Expected: FAIL — `StartLoading` is a no-op, so `is_loading_for_testing()`
returns false (the default state is `kHidden`).

- [ ] **Step 3: Implement the state methods + animation**

Edit `src/dao/browser/ui/views/dao_load_progress_view.cc`. Add includes at
the top alongside the existing ones:

```cpp
#include "base/time/time.h"
#include "ui/gfx/animation/tween.h"
```

Replace the stub state methods (`SetTargetProgress`, `StartLoading`,
`FinishLoading`, `HideImmediately`, `StartProgressAnimation`,
`StartFadeOut`, `EnterHidden`, `AnimationProgressed`, `AnimationEnded`,
`AnimationCanceled`) with:

```cpp
namespace {

constexpr base::TimeDelta kProgressAnimDuration = base::Milliseconds(120);
constexpr base::TimeDelta kCompleteHoldDuration = base::Milliseconds(150);

}  // namespace

void DaoLoadProgressView::SetTargetProgress(double progress, bool animate) {
  progress = std::clamp(progress, 0.0, 1.0);
  // Backward progress (e.g. a new navigation reset to 0) snaps to avoid an
  // odd shrinking animation.
  const bool snap = !animate || progress < displayed_progress_;
  target_progress_ = progress;
  if (snap) {
    if (progress_animation_) {
      progress_animation_->Stop();
    }
    displayed_progress_ = progress;
    SchedulePaint();
    return;
  }
  StartProgressAnimation(displayed_progress_, progress);
}

void DaoLoadProgressView::StartLoading() {
  hold_timer_.Stop();
  state_ = State::kLoading;
  displayed_progress_ = 0.0;
  target_progress_ = 0.0;
  animation_start_progress_ = 0.0;
  if (progress_animation_) {
    progress_animation_->Stop();
  }
  // Snap to opaque — cancel any in-flight fade.
  layer()->GetAnimator()->AbortAllAnimations();
  layer()->SetOpacity(1.0f);
  SchedulePaint();
}

void DaoLoadProgressView::FinishLoading() {
  if (state_ == State::kHidden) {
    return;  // Stray DidStopLoading before any load — ignore.
  }
  state_ = State::kCompleting;
  // Animate the fill to 1.0 if not already there.
  if (displayed_progress_ < 1.0) {
    StartProgressAnimation(displayed_progress_, 1.0);
  }
  // Schedule fade-out after the hold.
  hold_timer_.Start(
      FROM_HERE, kCompleteHoldDuration,
      base::BindOnce(&DaoLoadProgressView::StartFadeOut,
                     base::Unretained(this)));
}

void DaoLoadProgressView::HideImmediately() {
  hold_timer_.Stop();
  if (progress_animation_) {
    progress_animation_->Stop();
  }
  layer()->GetAnimator()->AbortAllAnimations();
  EnterHidden();
}

void DaoLoadProgressView::EnterHidden() {
  state_ = State::kHidden;
  displayed_progress_ = 0.0;
  target_progress_ = 0.0;
  animation_start_progress_ = 0.0;
  layer()->SetOpacity(0.0f);
  SchedulePaint();
}

void DaoLoadProgressView::StartProgressAnimation(double from, double to) {
  animation_start_progress_ = from;
  target_progress_ = to;
  if (!progress_animation_) {
    progress_animation_ =
        std::make_unique<gfx::LinearAnimation>(kProgressAnimDuration,
                                               gfx::LinearAnimation::kDefaultFrameRate,
                                               this);
  } else {
    progress_animation_->Stop();
  }
  progress_animation_->Start();
}

void DaoLoadProgressView::StartFadeOut() {
  if (state_ != State::kCompleting) {
    return;
  }
  state_ = State::kFadingOut;
  // Fade-out is a layer opacity animation; handled in Task 6 (next task
  // wires the real animator). For now, snap to hidden so completion still
  // resets state correctly.
  EnterHidden();
}

void DaoLoadProgressView::AnimationProgressed(const gfx::Animation* animation) {
  if (animation != progress_animation_.get()) {
    return;
  }
  const double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT,
                                              animation->GetCurrentValue());
  displayed_progress_ =
      animation_start_progress_ + (target_progress_ - animation_start_progress_) * t;
  SchedulePaint();
}

void DaoLoadProgressView::AnimationEnded(const gfx::Animation* animation) {
  if (animation != progress_animation_.get()) {
    return;
  }
  displayed_progress_ = target_progress_;
  SchedulePaint();
}

void DaoLoadProgressView::AnimationCanceled(const gfx::Animation* animation) {
  // Leave displayed_progress_ where it is.
}
```

- [ ] **Step 4: Build + run the test**

Run:
```bash
npm run rebuild
./engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoLoadProgressBrowserTest.StartLoadingMakesBarVisible"
```
Expected: PASS.

- [ ] **Step 5: Smoke-check visually**

Run: `npm run start:debug`
Navigate to a real URL (`https://news.ycombinator.com` is a good choice
because it loads with multiple progress steps). The 2 px blue bar should
appear along the top of the content card and animate. On completion, it
should snap away (we haven't implemented fade-out yet — the abrupt
disappearance is expected at this stage).

- [ ] **Step 6: Commit**

```bash
git add src/dao/browser/ui/views/dao_load_progress_view.cc \
        src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "feat(load-progress): state machine + progress animation"
```

---

## Task 6: Fade-out animation

Replace the snap-to-hidden in `StartFadeOut` with a real layer opacity
animation. Add an animation observer so we know when the fade completes and
can reset state.

**Files:**
- Modify: `src/dao/browser/ui/views/dao_load_progress_view.h`
- Modify: `src/dao/browser/ui/views/dao_load_progress_view.cc`

- [ ] **Step 1: Update the header to implement `ui::ImplicitAnimationObserver`**

Edit `src/dao/browser/ui/views/dao_load_progress_view.h`:

Add include near the others:

```cpp
#include "ui/compositor/layer_animation_observer.h"
```

Change the class declaration to also inherit from
`ui::ImplicitAnimationObserver`:

```cpp
class DaoLoadProgressView : public views::View,
                            public gfx::AnimationDelegate,
                            public ui::ImplicitAnimationObserver {
```

Add the override in the public section:

```cpp
  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsCompleted() override;
```

- [ ] **Step 2: Implement fade-out using ScopedLayerAnimationSettings**

Edit `src/dao/browser/ui/views/dao_load_progress_view.cc`. Add includes:

```cpp
#include "ui/compositor/scoped_layer_animation_settings.h"
```

Add `kFadeOutDuration` to the anonymous namespace:

```cpp
namespace {

constexpr base::TimeDelta kProgressAnimDuration = base::Milliseconds(120);
constexpr base::TimeDelta kCompleteHoldDuration = base::Milliseconds(150);
constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(200);

}  // namespace
```

Replace `StartFadeOut`:

```cpp
void DaoLoadProgressView::StartFadeOut() {
  if (state_ != State::kCompleting) {
    return;
  }
  state_ = State::kFadingOut;
  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(kFadeOutDuration);
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  settings.AddObserver(this);
  layer()->SetOpacity(0.0f);
}
```

Add the observer impl below the existing animation methods:

```cpp
void DaoLoadProgressView::OnImplicitAnimationsCompleted() {
  if (state_ == State::kFadingOut) {
    EnterHidden();
  }
}
```

- [ ] **Step 3: Make `HideImmediately` remove the observer too**

In `HideImmediately`, the `AbortAllAnimations()` call will trigger
`OnImplicitAnimationsCompleted` on any pending observer. To prevent the
observer from then re-entering `EnterHidden` (double-entry), guard by
setting state first:

Replace `HideImmediately`:

```cpp
void DaoLoadProgressView::HideImmediately() {
  hold_timer_.Stop();
  if (progress_animation_) {
    progress_animation_->Stop();
  }
  // Stop observing any in-flight layer animation before aborting it, so
  // the abort doesn't reenter EnterHidden with state already set to kHidden.
  layer()->GetAnimator()->StopAnimating();
  EnterHidden();
}
```

- [ ] **Step 4: Build + run all existing DaoLoadProgress tests**

Run:
```bash
npm run rebuild
./engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoLoadProgressBrowserTest.*"
```
Expected: all PASS.

- [ ] **Step 5: Smoke-check visually**

Run: `npm run start:debug`. Navigate to a page. The bar should now fade out
smoothly after the page finishes loading.

- [ ] **Step 6: Commit**

```bash
git add src/dao/browser/ui/views/dao_load_progress_view.h \
        src/dao/browser/ui/views/dao_load_progress_view.cc
git commit -m "feat(load-progress): smooth fade-out on completion"
```

---

## Task 7: End-to-end browser tests

Add the remaining browser tests from the spec: real-load lifecycle, tab
switching, stop command, sidebar collapse layout.

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Add an EmbeddedTestServer-based load lifecycle test**

Add to the test file (the existing tests already use `EmbeddedTestServer`
via `embedded_test_server()` — see how `DaoTabBrowserTest` uses it for
reference). Replace the existing
`DaoLoadProgressBrowserTest` class to add `SetUpOnMainThread` and add new
tests after the two existing ones:

```cpp
class DaoLoadProgressBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }
};
```

(Move the two existing `DaoLoadProgressBrowserTest` tests below this class
declaration — they already use the class, just need it to declare
`SetUpOnMainThread`.)

- [ ] **Step 2: Add the lifecycle test**

```cpp
IN_PROC_BROWSER_TEST_F(DaoLoadProgressBrowserTest, RealLoadShowsThenHides) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* progress = browser_view->dao_load_progress();
  ASSERT_TRUE(progress);

  const GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  // After NavigateToURL returns, DidStopLoading has fired and the view
  // is in Completing/FadingOut/Hidden. Pump the message loop until it's
  // back to Hidden (opacity 0).
  base::RunLoop loop;
  base::OneShotTimer poller;
  auto check = [&]() {
    if (progress->layer()->opacity() <= 0.01f) {
      loop.Quit();
    } else {
      poller.Start(FROM_HERE, base::Milliseconds(50),
                   base::BindLambdaForTesting([&]() { loop.Quit(); }));
    }
  };
  // Give it up to 1s total to settle.
  base::OneShotTimer timeout;
  timeout.Start(FROM_HERE, base::Seconds(1),
                base::BindLambdaForTesting([&]() { loop.Quit(); }));
  poller.Start(FROM_HERE, base::Milliseconds(50),
               base::BindLambdaForTesting([&]() { check(); }));
  loop.Run();

  EXPECT_LE(progress->layer()->opacity(), 0.01f);
  EXPECT_FALSE(progress->is_loading_for_testing());
}
```

- [ ] **Step 3: Add the tab-switch test**

```cpp
IN_PROC_BROWSER_TEST_F(DaoLoadProgressBrowserTest,
                       SwitchingToFinishedTabHidesBar) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* progress = browser_view->dao_load_progress();

  // Tab 0: load and finish.
  const GURL url_a = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url_a));

  // Tab 1: open and load, leave foregrounded.
  const GURL url_b = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(
      AddTabAtIndex(1, url_b, ui::PageTransition::PAGE_TRANSITION_TYPED));

  // Switch back to tab 0 (which is fully loaded) — bar should be hidden.
  browser()->tab_strip_model()->ActivateTabAt(0);
  EXPECT_LE(progress->layer()->opacity(), 0.01f);
}
```

- [ ] **Step 4: Add the stop-command test**

```cpp
IN_PROC_BROWSER_TEST_F(DaoLoadProgressBrowserTest, StopCommandHidesBar) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* progress = browser_view->dao_load_progress();

  // Start a navigation, then immediately stop. We don't await
  // NavigateToURL — instead we kick off a navigation and call Stop
  // before it completes.
  const GURL url = embedded_test_server()->GetURL("/slow?2");
  browser()->OpenURL(content::OpenURLParams(
      url, content::Referrer(), WindowOpenDisposition::CURRENT_TAB,
      ui::PAGE_TRANSITION_TYPED, /*is_renderer_initiated=*/false));
  // Process the start-of-load message so DidStartLoading fires.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(progress->is_loading_for_testing());

  chrome::Stop(browser());
  // Pump until DidStopLoading + fade-out completes.
  base::RunLoop run_loop;
  base::OneShotTimer timer;
  timer.Start(FROM_HERE, base::Milliseconds(500), run_loop.QuitClosure());
  run_loop.Run();

  EXPECT_LE(progress->layer()->opacity(), 0.01f);
}
```

- [ ] **Step 5: Add the sidebar-collapse layout test**

```cpp
IN_PROC_BROWSER_TEST_F(DaoLoadProgressBrowserTest,
                       LayoutFollowsSidebarCollapse) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* progress = browser_view->dao_load_progress();
  auto* sidebar = browser_view->dao_sidebar();
  ASSERT_TRUE(progress);
  ASSERT_TRUE(sidebar);

  // Force a layout pass with expanded sidebar.
  ASSERT_FALSE(sidebar->collapsed());
  browser_view->Layout();
  const int expanded_x = progress->bounds().x();
  const int expanded_w = progress->bounds().width();

  // Collapse the sidebar and re-layout.
  sidebar->ToggleCollapsed();
  // Some animations may run; pump and wait.
  base::RunLoop run_loop;
  base::OneShotTimer timer;
  timer.Start(FROM_HERE, base::Milliseconds(300), run_loop.QuitClosure());
  run_loop.Run();
  browser_view->Layout();

  EXPECT_NE(progress->bounds().x(), expanded_x);
  EXPECT_NE(progress->bounds().width(), expanded_w);
}
```

(If `DaoSidebarView::ToggleCollapsed` is not the public API name, check
`src/dao/browser/ui/views/sidebar/dao_sidebar_view.h` and use the
equivalent. The existing `DaoSidebarBrowserTest, SidebarToggleCollapse`
test in this file demonstrates the right call.)

- [ ] **Step 6: Build + run all `DaoLoadProgress*` tests**

Run:
```bash
npm run rebuild
./engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoLoadProgressBrowserTest.*"
```
Expected: all PASS.

- [ ] **Step 7: Run the full Dao test suite to catch regressions**

Run:
```bash
./engine/src/out/dao-debug/browser_tests --gtest_filter="Dao*"
```
Expected: all PASS (or the same baseline pass/fail set as before — the
`DISABLED_` tests noted in memory remain disabled).

- [ ] **Step 8: Commit**

```bash
git add src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "test(load-progress): lifecycle, tab-switch, stop, sidebar-collapse"
```

---

## Task 8: Final polish + manual verification

Verify dark mode, sidebar collapse, multiple-tab interactions, and the
visual fidelity matches the spec.

- [ ] **Step 1: Visual review — light mode**

Run: `npm run start:debug`

Navigate to:
1. `https://example.com` — short load, bar should briefly appear and fade.
2. `https://news.ycombinator.com` — multi-phase load.
3. A slow URL in DevTools throttled mode — bar should stay visible and
   animate.
4. Cancel a load mid-flight (press Stop / Esc) — bar should fade out
   cleanly, not snap.

- [ ] **Step 2: Visual review — dark mode**

Switch macOS system appearance to Dark (System Settings → Appearance →
Dark). Repeat scenarios 1–3. The bar is the same blue in both modes
(matching the spec). Confirm the halo is still visible against the dark
content card.

- [ ] **Step 3: Visual review — sidebar collapsed**

Collapse the sidebar (toggle button or its keyboard shortcut). Load a
page. The bar should now extend from the collapsed sidebar's right edge
(4 px from the BrowserView left edge) across to the right inset of the
content card.

- [ ] **Step 4: Visual review — multiple tabs**

Open three tabs, navigate each to a slow URL, then switch between them.
The bar should immediately reflect the active tab's progress. Switching
to a finished tab hides it. Switching to a still-loading tab shows it at
that tab's current progress without an interim fade.

- [ ] **Step 5: Spec sanity check**

Open `docs/superpowers/specs/2026-05-19-page-load-progress-design.md` and
walk through each section. Confirm the implementation matches:

- Position, height (2 px), color (`70,120,190`), halo (1 px each side,
  alpha 40)
- Animation durations: 120 ms progress, 150 ms hold, 200 ms fade
- State machine transitions
- Tab switching behaviour (snap, no fade)
- Error cases: stop command, tab destroyed, no active tab

- [ ] **Step 6: Commit (if any polish changes)**

If any tweaks were needed during manual review, commit them with a clear
message. Otherwise skip.

---

## Done.

Final state:

- Two new view files + two new controller files under `src/dao/browser/ui/views/`
- Four patch files modified
- Five new browser tests under `DaoLoadProgressBrowserTest`
- Bar shows during real loads, animates smoothly, fades out on completion,
  is event-transparent, and lays out correctly across sidebar collapse +
  tab switches.
