# Document PiP Persistent Bounds Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist Dao Document Picture-in-Picture window bounds per site origin through a Dao-owned profile dictionary pref.

**Architecture:** Add a focused Dao helper in `src/dao/browser/pip/` that serializes, validates, reads, and writes PiP bounds keyed by origin. Patch Chromium's PiP window manager so initial Document PiP placement uses Chromium's in-memory cache first, then Dao's persisted pref, and user move/resize updates both caches.

**Tech Stack:** Chromium C++17, Dao-owned `src/dao/` sources, Chromium patch files under `src/patches/`, `PrefService` dictionary prefs, `url::Origin`, `gfx::Rect`, `display::Display`.

**Git Policy:** This repository's AGENTS.md forbids state-changing git commands unless the latest user message explicitly authorizes the exact action. This plan uses checkpoint steps instead of commit steps.

---

## File Structure

- Modify `src/dao/browser/dao_pref_names.h`: define `dao::prefs::kDaoPipWindowBoundsByOrigin`.
- Modify `src/dao/browser/dao_pref_names.cc`: register the dictionary pref.
- Create `src/dao/browser/pip/dao_pip_bounds_prefs.h`: public helper API for persisted PiP bounds.
- Create `src/dao/browser/pip/dao_pip_bounds_prefs.cc`: origin keying, pref serialization, display validation, off-the-record guard.
- Modify `src/dao/browser/ui/dao_ui_sources.gni`: add the new helper files to Dao browser UI sources.
- Modify `src/dao/browser/ui/views/dao_browser_browsertest.cc`: add focused tests for pref registration and helper behavior.
- Modify `src/patches/chrome/browser/picture_in_picture/picture_in_picture_window_manager.cc.patch`: include Dao helper and wire persistent read/write through Chromium PiP bounds flow.
- Create `src/patches/chrome/browser/picture_in_picture/picture_in_picture_window_manager.h.patch`: add the current requested content size member.
- Review `src/patches/chrome/browser/ui/views/frame/picture_in_picture_browser_frame_view.cc.patch`: leave unchanged because it already forwards user resize/move notifications through `UpdateCachedBounds()`.

## Task 1: Add Failing Tests For Dao PiP Bounds Prefs

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`
- Future create: `src/dao/browser/pip/dao_pip_bounds_prefs.h`

- [ ] **Step 1: Add the include that should fail before the helper exists**

Add these includes beside the existing base/url and Dao PiP includes near the top of `src/dao/browser/ui/views/dao_browser_browsertest.cc`:

```cpp
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "dao/browser/pip/dao_pip_bounds_prefs.h"
#include "url/origin.h"
```

- [ ] **Step 2: Add failing tests near the existing Dao PiP tests**

Insert this block after `DaoPipOverlayResizeTest.TopCornerResizeClampsToMinimumSize` and before `class ReenteringUpdateObserver`:

```cpp
class DaoPipBoundsPrefsBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }
};

IN_PROC_BROWSER_TEST_F(DaoPipBoundsPrefsBrowserTest,
                       RegistersDictionaryPref) {
  PrefService* prefs = browser()->profile()->GetPrefs();
  ASSERT_TRUE(
      prefs->FindPreference(dao::prefs::kDaoPipWindowBoundsByOrigin));
  EXPECT_TRUE(
      prefs->GetDict(dao::prefs::kDaoPipWindowBoundsByOrigin).empty());
}

IN_PROC_BROWSER_TEST_F(DaoPipBoundsPrefsBrowserTest,
                       StoresAndRestoresBoundsForSameOriginAndRequestedSize) {
  const GURL url =
      embedded_test_server()->GetURL("bilibili.com", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);

  display::Display opener_display(1001);
  opener_display.set_bounds(gfx::Rect(0, 0, 1440, 900));
  opener_display.set_work_area(gfx::Rect(0, 0, 1440, 900));

  display::Display pip_display(1001);
  pip_display.set_bounds(gfx::Rect(0, 0, 1440, 900));
  pip_display.set_work_area(gfx::Rect(0, 0, 1440, 900));

  const gfx::Rect stored_bounds(700, 420, 640, 360);
  const gfx::Size requested_size(800, 450);
  dao::UpdatePersistedPipBoundsForSite(browser()->profile(), contents,
                                       stored_bounds, opener_display,
                                       pip_display, requested_size);

  std::optional<gfx::Rect> restored = dao::GetPersistedPipBoundsForSite(
      browser()->profile(), contents, opener_display, requested_size);

  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(stored_bounds, restored.value());
}

IN_PROC_BROWSER_TEST_F(DaoPipBoundsPrefsBrowserTest,
                       DoesNotRestoreBoundsForDifferentOrigin) {
  const GURL bilibili_url =
      embedded_test_server()->GetURL("bilibili.com", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), bilibili_url));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);

  display::Display opener_display(1001);
  opener_display.set_bounds(gfx::Rect(0, 0, 1440, 900));
  opener_display.set_work_area(gfx::Rect(0, 0, 1440, 900));

  const gfx::Size requested_size(800, 450);
  dao::UpdatePersistedPipBoundsForSite(
      browser()->profile(), contents, gfx::Rect(700, 420, 640, 360),
      opener_display, opener_display, requested_size);

  const GURL example_url =
      embedded_test_server()->GetURL("example.com", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), example_url));

  std::optional<gfx::Rect> restored = dao::GetPersistedPipBoundsForSite(
      browser()->profile(), contents, opener_display, requested_size);

  EXPECT_FALSE(restored.has_value());
}

IN_PROC_BROWSER_TEST_F(DaoPipBoundsPrefsBrowserTest,
                       DoesNotRestoreBoundsForRequestedSizeMismatch) {
  const GURL url =
      embedded_test_server()->GetURL("bilibili.com", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);

  display::Display opener_display(1001);
  opener_display.set_bounds(gfx::Rect(0, 0, 1440, 900));
  opener_display.set_work_area(gfx::Rect(0, 0, 1440, 900));

  dao::UpdatePersistedPipBoundsForSite(
      browser()->profile(), contents, gfx::Rect(700, 420, 640, 360),
      opener_display, opener_display, gfx::Size(800, 450));

  std::optional<gfx::Rect> restored = dao::GetPersistedPipBoundsForSite(
      browser()->profile(), contents, opener_display, gfx::Size(1024, 576));

  EXPECT_FALSE(restored.has_value());
}

IN_PROC_BROWSER_TEST_F(DaoPipBoundsPrefsBrowserTest,
                       DoesNotRestoreFullyOffscreenBounds) {
  const GURL url =
      embedded_test_server()->GetURL("bilibili.com", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);

  display::Display opener_display(1001);
  opener_display.set_bounds(gfx::Rect(0, 0, 1440, 900));
  opener_display.set_work_area(gfx::Rect(0, 0, 1440, 900));

  const gfx::Size requested_size(800, 450);
  base::DictValue entry;
  entry.Set("x", 5000);
  entry.Set("y", 5000);
  entry.Set("width", 640);
  entry.Set("height", 360);
  entry.Set("opener_display_id", base::NumberToString(opener_display.id()));
  entry.Set("pip_display_id", base::NumberToString(opener_display.id()));
  entry.Set("requested_width", requested_size.width());
  entry.Set("requested_height", requested_size.height());

  base::DictValue all_bounds;
  all_bounds.Set(url::Origin::Create(url).Serialize(), std::move(entry));
  browser()->profile()->GetPrefs()->SetDict(
      dao::prefs::kDaoPipWindowBoundsByOrigin, std::move(all_bounds));

  std::optional<gfx::Rect> restored = dao::GetPersistedPipBoundsForSite(
      browser()->profile(), contents, opener_display, requested_size);

  EXPECT_FALSE(restored.has_value());
}
```

- [ ] **Step 3: Run the focused tests and verify they fail for missing symbols**

Run:

```bash
npm run test -- --gtest_filter='DaoPipBoundsPrefs*'
```

Expected before implementation:

```text
fatal error: 'dao/browser/pip/dao_pip_bounds_prefs.h' file not found
```

If the test runner does not compile before running, this failure can also appear as an undeclared identifier error for `kDaoPipWindowBoundsByOrigin`, `GetPersistedPipBoundsForSite`, or `UpdatePersistedPipBoundsForSite`.

## Task 2: Register The Dao Profile Pref

**Files:**
- Modify: `src/dao/browser/dao_pref_names.h`
- Modify: `src/dao/browser/dao_pref_names.cc`

- [ ] **Step 1: Add the pref constant**

In `src/dao/browser/dao_pref_names.h`, add this after `kDaoSplitLayout`:

```cpp
// Dictionary pref storing the most recent Document PiP outer window bounds
// per site origin.
inline constexpr char kDaoPipWindowBoundsByOrigin[] =
    "dao.pip_window_bounds_by_origin";
```

- [ ] **Step 2: Register the dictionary pref**

In `src/dao/browser/dao_pref_names.cc`, update `RegisterProfilePrefs()` to register the new dictionary after `kDaoSplitLayout`:

```cpp
void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(kDaoAgentMemoryEnabled, false);
  registry->RegisterDictionaryPref(kDaoSplitLayout);
  registry->RegisterDictionaryPref(kDaoPipWindowBoundsByOrigin);
  registry->RegisterBooleanPref(kDaoWelcomeShown, false);
  registry->RegisterBooleanPref(kDaoLittleDaoEnabled, true);
  registry->RegisterBooleanPref(kDaoDreamEnabled, false);
  registry->RegisterBooleanPref(kDaoDreamDebug, false);
}
```

- [ ] **Step 3: Run the focused test and verify the helper is still missing**

Run:

```bash
npm run test -- --gtest_filter='DaoPipBoundsPrefsTest.RegistersDictionaryPref'
```

Expected at this point:

```text
fatal error: 'dao/browser/pip/dao_pip_bounds_prefs.h' file not found
```

The pref constant should no longer be the failure once the header exists in Task 3.

## Task 3: Add The Dao PiP Bounds Pref Helper

**Files:**
- Create: `src/dao/browser/pip/dao_pip_bounds_prefs.h`
- Create: `src/dao/browser/pip/dao_pip_bounds_prefs.cc`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

- [ ] **Step 1: Create the helper header**

Create `src/dao/browser/pip/dao_pip_bounds_prefs.h`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_PIP_DAO_PIP_BOUNDS_PREFS_H_
#define DAO_BROWSER_PIP_DAO_PIP_BOUNDS_PREFS_H_

#include <optional>

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace display {
class Display;
}  // namespace display

namespace dao {

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

}  // namespace dao

#endif  // DAO_BROWSER_PIP_DAO_PIP_BOUNDS_PREFS_H_
```

- [ ] **Step 2: Create the helper implementation**

Create `src/dao/browser/pip/dao_pip_bounds_prefs.cc`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/pip/dao_pip_bounds_prefs.h"

#include <string>

#include "base/strings/string_number_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/dao_pref_names.h"
#include "ui/display/display.h"
#include "url/origin.h"

namespace dao {

namespace {

constexpr char kXKey[] = "x";
constexpr char kYKey[] = "y";
constexpr char kWidthKey[] = "width";
constexpr char kHeightKey[] = "height";
constexpr char kOpenerDisplayIdKey[] = "opener_display_id";
constexpr char kPipDisplayIdKey[] = "pip_display_id";
constexpr char kRequestedWidthKey[] = "requested_width";
constexpr char kRequestedHeightKey[] = "requested_height";

std::optional<std::string> OriginKeyForWebContents(
    content::WebContents* web_contents) {
  if (!web_contents || !web_contents->GetPrimaryMainFrame()) {
    return std::nullopt;
  }

  url::Origin origin =
      web_contents->GetPrimaryMainFrame()->GetLastCommittedOrigin();
  if (origin.opaque()) {
    return std::nullopt;
  }

  std::string serialized = origin.Serialize();
  if (serialized.empty()) {
    return std::nullopt;
  }
  return serialized;
}

bool CanPersistForProfile(Profile* profile) {
  return profile && !profile->IsOffTheRecord() && profile->GetPrefs();
}

bool RequestedSizeMatches(const base::Value::Dict& entry,
                          std::optional<gfx::Size> requested_content_size) {
  const std::optional<int> stored_width = entry.FindInt(kRequestedWidthKey);
  const std::optional<int> stored_height = entry.FindInt(kRequestedHeightKey);
  if (!requested_content_size.has_value()) {
    return !stored_width.has_value() && !stored_height.has_value();
  }

  return stored_width == requested_content_size->width() &&
         stored_height == requested_content_size->height();
}

bool IsUsableBounds(const gfx::Rect& bounds,
                    const display::Display& opener_display) {
  return !bounds.IsEmpty() && bounds.width() > 0 && bounds.height() > 0 &&
         bounds.Intersects(opener_display.work_area());
}

std::optional<gfx::Rect> BoundsFromEntry(const base::Value::Dict& entry) {
  const std::optional<int> x = entry.FindInt(kXKey);
  const std::optional<int> y = entry.FindInt(kYKey);
  const std::optional<int> width = entry.FindInt(kWidthKey);
  const std::optional<int> height = entry.FindInt(kHeightKey);
  if (!x || !y || !width || !height) {
    return std::nullopt;
  }
  return gfx::Rect(*x, *y, *width, *height);
}

}  // namespace

std::optional<gfx::Rect> GetPersistedPipBoundsForSite(
    Profile* profile,
    content::WebContents* web_contents,
    const display::Display& opener_display,
    std::optional<gfx::Size> requested_content_size) {
  if (!CanPersistForProfile(profile)) {
    return std::nullopt;
  }

  std::optional<std::string> origin_key = OriginKeyForWebContents(web_contents);
  if (!origin_key) {
    return std::nullopt;
  }

  const base::Value::Dict& all_bounds =
      profile->GetPrefs()->GetDict(prefs::kDaoPipWindowBoundsByOrigin);
  const base::Value::Dict* entry = all_bounds.FindDict(*origin_key);
  if (!entry || !RequestedSizeMatches(*entry, requested_content_size)) {
    return std::nullopt;
  }

  const std::string* stored_opener_display_id =
      entry->FindString(kOpenerDisplayIdKey);
  const std::string* stored_pip_display_id =
      entry->FindString(kPipDisplayIdKey);
  if (!stored_opener_display_id || !stored_pip_display_id) {
    return std::nullopt;
  }

  const std::string opener_display_id =
      base::NumberToString(opener_display.id());
  if (*stored_opener_display_id != opener_display_id &&
      *stored_pip_display_id != opener_display_id) {
    return std::nullopt;
  }

  std::optional<gfx::Rect> bounds = BoundsFromEntry(*entry);
  if (!bounds || !IsUsableBounds(*bounds, opener_display)) {
    return std::nullopt;
  }

  return bounds;
}

void UpdatePersistedPipBoundsForSite(
    Profile* profile,
    content::WebContents* web_contents,
    const gfx::Rect& most_recent_bounds,
    const display::Display& opener_display,
    const display::Display& pip_display,
    std::optional<gfx::Size> requested_content_size) {
  if (!CanPersistForProfile(profile) ||
      !IsUsableBounds(most_recent_bounds, pip_display)) {
    return;
  }

  std::optional<std::string> origin_key = OriginKeyForWebContents(web_contents);
  if (!origin_key) {
    return;
  }

  base::Value::Dict entry;
  entry.Set(kXKey, most_recent_bounds.x());
  entry.Set(kYKey, most_recent_bounds.y());
  entry.Set(kWidthKey, most_recent_bounds.width());
  entry.Set(kHeightKey, most_recent_bounds.height());
  entry.Set(kOpenerDisplayIdKey, base::NumberToString(opener_display.id()));
  entry.Set(kPipDisplayIdKey, base::NumberToString(pip_display.id()));
  if (requested_content_size.has_value()) {
    entry.Set(kRequestedWidthKey, requested_content_size->width());
    entry.Set(kRequestedHeightKey, requested_content_size->height());
  }

  ScopedDictPrefUpdate update(profile->GetPrefs(),
                              prefs::kDaoPipWindowBoundsByOrigin);
  update.Get().Set(*origin_key, std::move(entry));
}

}  // namespace dao
```

- [ ] **Step 3: Add the helper files to Dao UI sources**

In `src/dao/browser/ui/dao_ui_sources.gni`, add the new files after `dao_pip_interceptor.h`:

```gn
  "//dao/browser/pip/dao_pip_bounds_prefs.cc",
  "//dao/browser/pip/dao_pip_bounds_prefs.h",
```

- [ ] **Step 4: Run the focused tests**

Run:

```bash
npm run test -- --gtest_filter='DaoPipBoundsPrefs*'
```

Expected after Task 3:

```text
[  PASSED  ] 5 tests.
```

## Task 4: Wire Persistent Bounds Into Chromium PiP Window Manager Patch

**Files:**
- Modify: `src/patches/chrome/browser/picture_in_picture/picture_in_picture_window_manager.cc.patch`
- Create: `src/patches/chrome/browser/picture_in_picture/picture_in_picture_window_manager.h.patch`

- [ ] **Step 1: Add Dao includes to the patch**

Extend the patch so `chrome/browser/picture_in_picture/picture_in_picture_window_manager.cc` includes these Dao/Profile headers:

```diff
 #include "chrome/browser/picture_in_picture/picture_in_picture_bounds_cache.h"
+#include "chrome/browser/profiles/profile.h"
 #include "chrome/browser/ui/browser_navigator_params.h"
+#include "dao/browser/pip/dao_pip_bounds_prefs.h"
```

- [ ] **Step 2: Add a helper for requested content size inside the anonymous namespace**

Patch the anonymous namespace in `picture_in_picture_window_manager.cc` with:

```cpp
std::optional<gfx::Size> GetRequestedContentSize(
    const blink::mojom::PictureInPictureWindowOptions& pip_options) {
  if (pip_options.width > 0 && pip_options.height > 0) {
    return gfx::Size(base::saturated_cast<int>(pip_options.width),
                     base::saturated_cast<int>(pip_options.height));
  }
  return std::nullopt;
}
```

Place this helper after `GetMaximumSiteRequestedWindowArea()` inside the non-Android code path where PiP window bounds are calculated.

- [ ] **Step 3: Persistently restore only when Chromium's memory cache misses**

Patch `PictureInPictureWindowManager::CalculateOuterWindowBounds()` by replacing the duplicated local requested-size construction with the helper and adding the Dao fallback:

```cpp
  std::optional<gfx::Size> requested_content_bounds =
      GetRequestedContentSize(pip_options);

  if (pip_window_controller_) {
    auto* const web_contents = pip_window_controller_->GetWebContents();
    auto cached_window_bounds =
        PictureInPictureBoundsCache::GetBoundsForNewWindow(
            web_contents, opener_display, requested_content_bounds);
    // Ignore the result if we're asked to do so.  Note that we still have to
    // ask the cache, so that it's set up to accept position updates later for
    // this request.
    if (cached_window_bounds && !pip_options.prefer_initial_window_placement) {
      // Cache hit!  Just return it as the window bounds.
      return *cached_window_bounds;
    }

    if (!pip_options.prefer_initial_window_placement) {
      Profile* profile =
          Profile::FromBrowserContext(web_contents->GetBrowserContext());
      std::optional<gfx::Rect> persisted_window_bounds =
          dao::GetPersistedPipBoundsForSite(
              profile, web_contents, opener_display, requested_content_bounds);
      if (persisted_window_bounds) {
        return *persisted_window_bounds;
      }
    }
  }
```

Keep this before the existing `if (pip_options.width > 0 && pip_options.height > 0)` size calculation.

- [ ] **Step 4: Persist user move/resize through `UpdateCachedBounds()`**

Patch `PictureInPictureWindowManager::UpdateCachedBounds()` after the existing `PictureInPictureBoundsCache::UpdateCachedBounds(...)` call:

```cpp
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  dao::UpdatePersistedPipBoundsForSite(
      profile, web_contents, most_recent_bounds, opener_display_.value(),
      pip_display, last_document_pip_requested_content_size_);
```

- [ ] **Step 5: Add a manager member to remember the current requested content size**

Create `src/patches/chrome/browser/picture_in_picture/picture_in_picture_window_manager.h.patch` and add this private member near `opener_display_` in `chrome/browser/picture_in_picture/picture_in_picture_window_manager.h`:

```cpp
  // The site-requested inner size for the active Document PiP window, if any.
  // Dao uses this to persist user-resized outer bounds against the same
  // requested content size that Chromium's in-memory bounds cache keys on.
  std::optional<gfx::Size> last_document_pip_requested_content_size_;
```

Also patch `CalculateOuterWindowBounds()` to assign it before cache lookup:

```cpp
  last_document_pip_requested_content_size_ = requested_content_bounds;
```

- [ ] **Step 6: Review the frame-view resize hook without editing it**

Confirm that `src/patches/chrome/browser/ui/views/frame/picture_in_picture_browser_frame_view.cc.patch` still contains the existing call from `PictureInPictureBrowserFrameView::OnWidgetBoundsChanged()` to `PictureInPictureWindowManager::UpdateCachedBounds(new_bounds, pip_display)`. Leave that patch unchanged; the persistent write belongs in `UpdateCachedBounds()`.

## Task 5: Apply Patches Into `engine/src`

**Files:**
- Generated by import: `engine/src/chrome/browser/picture_in_picture/picture_in_picture_window_manager.cc`
- Generated by import: `engine/src/chrome/browser/picture_in_picture/picture_in_picture_window_manager.h`

- [ ] **Step 1: Run the Dao import pipeline**

Run:

```bash
npm run import
```

Expected: the command exits with status 0 and produces no patch reject or conflict output.

- [ ] **Step 2: Inspect generated engine files, read-only**

Run:

```bash
rg -n "dao_pip_bounds_prefs|last_document_pip_requested_content_size|GetPersistedPipBoundsForSite|UpdatePersistedPipBoundsForSite" engine/src/chrome/browser/picture_in_picture
```

Expected: both generated files contain at least one match:

```text
engine/src/chrome/browser/picture_in_picture/picture_in_picture_window_manager.cc
engine/src/chrome/browser/picture_in_picture/picture_in_picture_window_manager.h
```

There should be no direct hand edits under `engine/`.

## Task 6: Run Focused Verification

**Files:**
- Test target: `src/dao/browser/ui/views/dao_browser_browsertest.cc`
- Compile path: `npm run rebuild`

- [ ] **Step 1: Run focused Dao PiP bounds tests**

Run:

```bash
npm run test -- --gtest_filter='DaoPipBoundsPrefs*'
```

Expected:

```text
[  PASSED  ] 5 tests.
```

- [ ] **Step 2: Run existing focused PiP tests to catch regressions**

Run:

```bash
npm run test -- --gtest_filter='DaoPipOverlayResizeTest.*:DaoPipSiteRulesTest.*:DaoPipInterceptorTest.*'
```

Expected: every selected non-disabled test passes and no new failure is introduced in the existing Dao PiP tests.

- [ ] **Step 3: Confirm compilation through the only allowed command**

Run:

```bash
npm run rebuild
```

Expected: the command exits with status 0. Do not use `npm run build`, `npm run build:debug`, `npm run test:build`, direct `ninja`, `autoninja`, `siso`, or `gn gen`.

- [ ] **Step 4: Check the final diff**

Run:

```bash
git diff -- src/dao/browser/dao_pref_names.h src/dao/browser/dao_pref_names.cc src/dao/browser/pip/dao_pip_bounds_prefs.h src/dao/browser/pip/dao_pip_bounds_prefs.cc src/dao/browser/ui/dao_ui_sources.gni src/dao/browser/ui/views/dao_browser_browsertest.cc src/patches/chrome/browser/picture_in_picture/picture_in_picture_window_manager.cc.patch src/patches/chrome/browser/picture_in_picture/picture_in_picture_window_manager.h.patch
```

Expected: the diff contains only the pref helper, test coverage, build source registration, and the PiP manager patch wiring.

## Coverage Matrix

- Per-origin persistence: Task 1 tests, Task 3 helper.
- Requested content size matching: Task 1 tests, Task 3 helper, Task 4 manager member.
- Offscreen invalid bounds: Task 1 tests, Task 3 helper.
- Profile pref registration: Task 1 and Task 2.
- Chromium cache first, persistent fallback second: Task 4.
- User move/resize write path: Task 4 through existing `UpdateCachedBounds()` call.
- Import pipeline compliance: Task 5.
- Allowed compile confirmation: Task 6.
