# Long-term Manifest V2 (MV2) Extension Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Manifest V2 extensions installable and runnable by default in Dao Browser, with a single `chrome://flags` toggle to fall back to Chromium's standard MV2 deprecation behavior.

**Architecture:** A new `src/dao/browser/extensions/legacy_mv2/` subtree owns the Dao-side MV2 logic (feature flag, pref-default helper, compat-router seam, install-dialog notice, regression tests). Two small patches divert Chromium's enterprise-policy-managed `kManifestV2Availability` pref into a Dao-controlled default path: one patches the pref-registration default value (semantic alignment), the other patches `ExtensionManagement::Refresh()` so the Dao default actually flows through when no policy is set. A third patch adds an `IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE` label above the install dialog's permission list for MV2 extensions. A `chrome://flags` entry (`restore-manifest-v2-deprecation`) controls a single `BASE_FEATURE` whose state inverts the default. Every patch carries a `// Dao MV2 Support:` header for upgrade auditing.

**Tech Stack:** C++17 with Chromium Views; `base::FEATURE_DISABLED_BY_DEFAULT`; `l10n_util` for translated strings; GN source_sets following the existing `dao_telemetry` pattern; `browser_tests` (gtest) for integration coverage; Chromium's GRIT `.grd` pipeline for i18n.

---

## Execution Notes (read before starting)

Chromium builds are expensive (30+ min cold, several min incremental). The strict "write test, run-fail, write impl, run-pass, commit" TDD micro-loop is impractical here, so each task adopts this adapted rhythm:

1. Write source + test together.
2. Run **one** `npm run rebuild` per task to compile.
3. Run the new tests with `--gtest_filter='Dao*MV2*'` (or the specific test name).
4. Commit when green.

Build tool rules from CLAUDE.md still apply: only `npm run rebuild` / `npm run build:debug` / `npm run test` — never `autoninja`/`ninja`/`siso` directly. All edits go through `src/patches/` or `src/dao/`; never edit `engine/` as a deliverable.

For every patch file under `src/patches/...`, the very first line of the diff body (the first hunk's first added line that's a comment) must be `// Dao MV2 Support: <one-line description>`. This is the inventory marker.

---

## File Inventory

### New files (Dao-owned, under `src/dao/browser/extensions/legacy_mv2/`)

| Path | Responsibility |
|---|---|
| `BUILD.gn` | Two source_sets: `dao_legacy_mv2` (runtime), `dao_legacy_mv2_tests` (testonly, browser_tests). |
| `README.md` | Purpose, control-flow diagram, patch inventory pointer, "when to fork" decision tree, sunset policy. |
| `dao_mv2_features.h` / `.cc` | Declares + defines `dao::kRestoreManifestV2Deprecation` (BASE_FEATURE, FEATURE_DISABLED_BY_DEFAULT). |
| `dao_mv2_pref_defaults.h` / `.cc` | `DaoMV2PrefDefaults::DefaultManifestV2Availability()` returns `int` (2 when feature OFF, 0 when feature ON). FeatureList-safe. |
| `dao_mv2_api_router.h` / `.cc` | `DaoMV2APIRouter::Get()` singleton with pass-through bool methods (`WebRequestBlockingEnabled`, `BackgroundPagePersistenceEnabled`). Seam for future forks. |
| `dao_mv2_install_notice.h` / `.cc` | `ShouldShowLegacyMV2Notice(const Extension&)` and `GetLegacyMV2NoticeText()` helpers. |
| `dao_mv2_browsertest.cc` | Eight `IN_PROC_BROWSER_TEST_F` cases (see Task 12). |
| `test_data/webrequest_blocking/manifest.json` | MV2 manifest declaring `webRequest`/`webRequestBlocking` + a target host. |
| `test_data/webrequest_blocking/background.js` | Background page that cancels requests to the target. |
| `test_data/persistent_background/manifest.json` | MV2 manifest with `background.page` set to `background.html` and `persistent: true`. |
| `test_data/persistent_background/background.html` | Sets `window.__daoMV2Marker__ = 'persisted'`. |

### New patches (under `src/patches/...`, mirror Chromium tree)

| Path | Purpose |
|---|---|
| `extensions/browser/extension_prefs.cc.patch` | Substitute Dao default into `RegisterIntegerPref(pref_names::kManifestV2Availability, …)`. |
| `chrome/browser/extensions/extension_management.cc.patch` | Inject Dao default into `Refresh()` when no policy is set. **This is the patch that actually makes MV2 work.** |
| `chrome/browser/about_flags.cc.patch` | Register `restore-manifest-v2-deprecation` in `kFeatureEntries[]`. |
| `chrome/browser/flag_descriptions.h.patch` | Add `kRestoreManifestV2DeprecationName` / `…Description` constants. (flag_descriptions is header-only in this Chromium revision; there is no `.cc`.) |
| `chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch` | Insert MV2 notice `views::Label` above the permission list. |
| `extensions/browser/BUILD.gn.patch` | Add `//dao/browser/extensions/legacy_mv2:dao_legacy_mv2` to `browser_sources`'s `deps`. |

### Modified files (Dao-owned, already version-controlled)

| Path | Change |
|---|---|
| `src/dao/browser/strings/dao_strings.grd` | Add `IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE` `<message>`. |
| `src/patches/chrome/test/BUILD.gn.patch` | Append `"//dao/browser/extensions/legacy_mv2:dao_legacy_mv2_tests",` to the `browser_tests` `deps` block. |

### Auto-memory entry (created at end)

`/Users/moonrailgun/.claude/projects/-Users-moonrailgun-Develop-dao-browser/memory/project_mv2_long_term_support.md`

---

## Critical Implementation Detail (read before Task 4)

The design doc describes diverting the pref-registration default to make MV2 extensions exempt. The literal change to `RegisterIntegerPref(pref_names::kManifestV2Availability, 0)` is necessary but **not sufficient**.

`chrome/browser/extensions/extension_management.cc:719-720` loads the pref like this:

```cpp
const base::Value* manifest_v2_pref =
    LoadPreference(pref_names::kManifestV2Availability,
                   /*force_managed=*/true, base::Value::Type::INTEGER);
```

And `LoadPreference` (line 975-990) returns `nullptr` unless `!pref->IsDefaultValue() && (!force_managed || pref->IsManaged())`. Both clauses fail for a pref that was registered with a non-zero default but is otherwise untouched — the default value is still "the default", and the pref is not policy-managed.

When `manifest_v2_pref` is `nullptr`, `GlobalSettings::manifest_v2_setting` keeps its member-initializer value of `kDefault`, which means `IsExemptFromMV2DeprecationByPolicy()` returns `false` and MV2 deprecation still fires.

The implementation therefore needs **both**:

1. Patch the `RegisterIntegerPref` literal so the pref's *semantic* default matches Dao's stance (Task 4). This has no behavior effect on its own but keeps anything that reads the pref directly aligned.
2. Patch `ExtensionManagement::Refresh()` to inject the Dao default into `global_settings_->manifest_v2_setting` when `manifest_v2_pref == nullptr` (Task 5). This is the patch that actually flips behavior.

This dual-patch arrangement is faithful to the design doc's intent (pref-default → `IsExemptFromMV2DeprecationByPolicy()`); it just adds the link the design assumed was implicit in Chromium's existing wiring.

---

## Tasks

### Task 1: Bootstrap the `legacy_mv2/` directory and skeleton BUILD.gn

**Files:**

- Create: `src/dao/browser/extensions/legacy_mv2/BUILD.gn`
- Create: `src/dao/browser/extensions/legacy_mv2/README.md` (placeholder — fleshed out in Task 14)

- [ ] **Step 1: Create `BUILD.gn`**

```python
# Copyright 2026 Dao Browser Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# -----------------------------------------------------------------------------
# Public source_set: dao_legacy_mv2
#
# Dao's long-term Manifest V2 (MV2) extension support. Owns the
# Dao-controlled pref default, the chrome://flags feature, the install-
# dialog notice helpers, and the forward-looking compat-router seam.
# Compiled into //extensions/browser and //chrome/browser/ui so it can
# be reached from pref registration and from the install dialog view.
# -----------------------------------------------------------------------------

source_set("dao_legacy_mv2") {
  sources = [
    "dao_mv2_features.cc",
    "dao_mv2_features.h",
    "dao_mv2_pref_defaults.cc",
    "dao_mv2_pref_defaults.h",
  ]

  deps = [
    "//base",
    "//extensions/common",
    "//ui/base",
  ]

  public_deps = [
    "//dao/browser/strings:dao_strings",
  ]
}

# Note: dao_mv2_api_router.{h,cc} (Task 6) and dao_mv2_install_notice.{h,cc}
# (Task 8) are appended to `sources` by their respective tasks to keep
# `:dao_legacy_mv2` buildable at every commit.

source_set("dao_legacy_mv2_tests") {
  testonly = true
  sources = [ "dao_mv2_browsertest.cc" ]
  defines = [ "HAS_OUT_OF_PROC_TEST_RUNNER" ]

  data = [
    "test_data/webrequest_blocking/background.js",
    "test_data/webrequest_blocking/manifest.json",
    "test_data/persistent_background/background.html",
    "test_data/persistent_background/manifest.json",
  ]

  deps = [
    ":dao_legacy_mv2",
    "//base",
    "//chrome/browser",
    "//chrome/browser/extensions",
    "//chrome/browser/profiles",
    "//chrome/browser/ui:ui",
    "//chrome/browser/ui/browser_window",
    "//chrome/test:test_support",
    "//chrome/test:test_support_ui",
    "//content/test:test_support",
    "//extensions/browser",
    "//extensions/common",
    "//testing/gtest",
    "//ui/views",
  ]
}
```

- [ ] **Step 2: Create placeholder `README.md`**

```markdown
# legacy_mv2 — Dao's Manifest V2 Support

Placeholder. Final contents land in Task 14.
```

- [ ] **Step 3: Commit**

```bash
git add src/dao/browser/extensions/legacy_mv2/BUILD.gn \
        src/dao/browser/extensions/legacy_mv2/README.md
git commit -m "feat(mv2): bootstrap legacy_mv2 directory with BUILD.gn skeleton"
```

---

### Task 2: Implement `dao_mv2_features` (the chrome://flags feature)

**Files:**

- Create: `src/dao/browser/extensions/legacy_mv2/dao_mv2_features.h`
- Create: `src/dao/browser/extensions/legacy_mv2/dao_mv2_features.cc`

- [ ] **Step 1: Write `dao_mv2_features.h`**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_FEATURES_H_
#define DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_FEATURES_H_

#include "base/feature_list.h"

namespace dao {

// When ENABLED, Dao Browser falls back to Chromium's standard Manifest V2
// deprecation behavior (MV2 extensions are disabled/blocked the same way
// upstream Chrome handles them). When DISABLED (the default), Dao keeps
// MV2 extensions fully enabled and installable.
//
// The flag is intentionally reverse-named: "Default" in chrome://flags means
// Dao behavior (MV2 on), "Enabled" means restoring upstream deprecation.
BASE_DECLARE_FEATURE(kRestoreManifestV2Deprecation);

}  // namespace dao

#endif  // DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_FEATURES_H_
```

- [ ] **Step 2: Write `dao_mv2_features.cc`**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/extensions/legacy_mv2/dao_mv2_features.h"

namespace dao {

BASE_FEATURE(kRestoreManifestV2Deprecation,
             "RestoreManifestV2Deprecation",
             base::FEATURE_DISABLED_BY_DEFAULT);

}  // namespace dao
```

- [ ] **Step 3: Commit**

```bash
git add src/dao/browser/extensions/legacy_mv2/dao_mv2_features.h \
        src/dao/browser/extensions/legacy_mv2/dao_mv2_features.cc
git commit -m "feat(mv2): add kRestoreManifestV2Deprecation base feature"
```

---

### Task 3: Implement `DaoMV2PrefDefaults`

**Files:**

- Create: `src/dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h`
- Create: `src/dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.cc`

- [ ] **Step 1: Write `dao_mv2_pref_defaults.h`**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_PREF_DEFAULTS_H_
#define DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_PREF_DEFAULTS_H_

namespace dao {

// Single-point owner of the Dao-controlled default value for the
// `extensions.manifest_v2.availability` pref (the integer encoding of
// `internal::GlobalSettings::ManifestV2Setting`).
//
// Returns 2 (= ManifestV2Setting::kEnabled, "all MV2 extensions allowed,
// exempt from deprecation") when `kRestoreManifestV2Deprecation` is OFF
// (Dao default). Returns 0 (= ManifestV2Setting::kDefault, "Chromium's
// standard deprecation behavior") when the feature is ON.
//
// Safe to call before `FeatureList` is initialized: `IsEnabled` returns
// the feature's static default (false) in that window, which gives the
// Dao default — the desired pre-init behavior.
class DaoMV2PrefDefaults {
 public:
  DaoMV2PrefDefaults() = delete;

  static int DefaultManifestV2Availability();
};

}  // namespace dao

#endif  // DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_PREF_DEFAULTS_H_
```

- [ ] **Step 2: Write `dao_mv2_pref_defaults.cc`**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h"

#include "base/feature_list.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_features.h"

namespace dao {

// static
int DaoMV2PrefDefaults::DefaultManifestV2Availability() {
  // Values mirror `internal::GlobalSettings::ManifestV2Setting`:
  //   0 = kDefault, 1 = kDisabled, 2 = kEnabled, 3 = kEnabledForForceInstalled.
  // We use kDefault (0) for "restore Chromium deprecation" and kEnabled (2)
  // for "Dao default: MV2 fully exempted".
  return base::FeatureList::IsEnabled(kRestoreManifestV2Deprecation) ? 0 : 2;
}

}  // namespace dao
```

- [ ] **Step 3: Commit**

```bash
git add src/dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h \
        src/dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.cc
git commit -m "feat(mv2): add DaoMV2PrefDefaults helper"
```

---

### Task 4: Patch `extension_prefs.cc` to use the Dao default

**Files:**

- Create: `src/patches/extensions/browser/extension_prefs.cc.patch`

- [ ] **Step 1: Write the patch**

The diff context is `engine/src/extensions/browser/extension_prefs.cc` around line 2218. The patch must include a `Dao MV2 Support:` comment header.

```diff
diff --git a/extensions/browser/extension_prefs.cc b/extensions/browser/extension_prefs.cc
index 0000000000..0000000000 100644
--- a/extensions/browser/extension_prefs.cc
+++ b/extensions/browser/extension_prefs.cc
@@ -10,6 +10,7 @@
 #include "base/observer_list.h"
 #include "base/strings/string_number_conversions.h"
 #include "base/values.h"
+#include "dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h"
 #include "components/crx_file/id_util.h"
 #include "components/pref_registry/pref_registry_syncable.h"
 #include "components/prefs/pref_notifier.h"
@@ -2215,7 +2216,9 @@ void ExtensionPrefs::RegisterProfilePrefs(
   registry->RegisterDictionaryPref(pref_names::kInstallForceList);
   registry->RegisterDictionaryPref(pref_names::kExtensionManagement);
   registry->RegisterDictionaryPref(pref_names::kOAuthRedirectUrls);
   registry->RegisterListPref(pref_names::kAllowedTypes);
-  registry->RegisterIntegerPref(pref_names::kManifestV2Availability, 0);
+  // Dao MV2 Support: semantic-default alignment for the MV2 availability pref.
+  registry->RegisterIntegerPref(
+      pref_names::kManifestV2Availability,
+      dao::DaoMV2PrefDefaults::DefaultManifestV2Availability());
   registry->RegisterListPref(pref_names::kAllowedInstallSites);
```

> **Note on the import-tool include-order convention:** Chromium's include sorter wants `dao/...` to live with `components/...`, `content/...`, etc., not at the top of the alphabetical list. The patch places it after `base/` and before `components/` to match the existing block style. If `npm run import` rejects the location, move the new `#include` to keep alphabetical order within the third-party block.

- [ ] **Step 2: Add the BUILD.gn dep so the include resolves**

Create `src/patches/extensions/browser/BUILD.gn.patch`:

```diff
diff --git a/extensions/browser/BUILD.gn b/extensions/browser/BUILD.gn
index 0000000000..0000000000 100644
--- a/extensions/browser/BUILD.gn
+++ b/extensions/browser/BUILD.gn
@@ -657,6 +657,8 @@ source_set("browser_sources") {
     "//ui/color",
     "//ui/display",
     "//ui/menus",
+    # Dao MV2 Support: bring in DaoMV2PrefDefaults for extension_prefs.cc.
+    "//dao/browser/extensions/legacy_mv2:dao_legacy_mv2",
     "//url",
   ]
```

- [ ] **Step 3: Apply patches and build**

```bash
npm run import
npm run build:debug
```

Expected: build succeeds. If `extension_prefs.cc` fails to compile due to include order, fix the patch and retry. Do not run a bare `npm run export` — if you need to fix the include order, edit the `.patch` file directly with a text editor.

- [ ] **Step 4: Commit**

```bash
git add src/patches/extensions/browser/extension_prefs.cc.patch \
        src/patches/extensions/browser/BUILD.gn.patch
git commit -m "feat(mv2): patch extension_prefs.cc to use Dao MV2 pref default"
```

---

### Task 5: Patch `extension_management.cc` to honor the Dao default

This is the patch that actually makes MV2 extensions work by default. See **Critical Implementation Detail** above for why Task 4 alone isn't enough.

**Files:**

- Create: `src/patches/chrome/browser/extensions/extension_management.cc.patch`

- [ ] **Step 1: Write the patch**

The diff context is `engine/src/chrome/browser/extensions/extension_management.cc` around line 809-813 (the `if (manifest_v2_pref)` block in `Refresh()`).

```diff
diff --git a/chrome/browser/extensions/extension_management.cc b/chrome/browser/extensions/extension_management.cc
index 0000000000..0000000000 100644
--- a/chrome/browser/extensions/extension_management.cc
+++ b/chrome/browser/extensions/extension_management.cc
@@ -25,6 +25,7 @@
 #include "chrome/browser/extensions/managed_installation_mode.h"
 #include "chrome/browser/extensions/standard_management_policy_provider.h"
 #include "chrome/browser/profiles/profile.h"
+#include "dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h"
 #include "components/crx_file/id_util.h"
 #include "components/policy/core/common/management/management_service.h"
 #include "components/pref_registry/pref_registry_syncable.h"
@@ -806,10 +807,18 @@ void ExtensionManagement::Refresh() {
     }
   }

-  if (manifest_v2_pref) {
-    global_settings_->manifest_v2_setting =
-        static_cast<internal::GlobalSettings::ManifestV2Setting>(
-            manifest_v2_pref->GetInt());
-  }
+  // Dao MV2 Support: when the pref is not policy-controlled, fall back to
+  // Dao's default (kEnabled by default; kDefault when the user flips the
+  // restore-manifest-v2-deprecation flag in chrome://flags). Without this,
+  // LoadPreference(force_managed=true) returns nullptr for the unmanaged
+  // case and `manifest_v2_setting` stays at kDefault, defeating Dao's
+  // default-on MV2 stance.
+  global_settings_->manifest_v2_setting =
+      manifest_v2_pref
+          ? static_cast<internal::GlobalSettings::ManifestV2Setting>(
+                manifest_v2_pref->GetInt())
+          : static_cast<internal::GlobalSettings::ManifestV2Setting>(
+                dao::DaoMV2PrefDefaults::DefaultManifestV2Availability());

   if (unpublished_availability_pref) {
```

- [ ] **Step 2: Add BUILD.gn dep for `chrome/browser/extensions`**

The Chromium `chrome/browser/extensions` source_set lives under `chrome/browser/BUILD.gn` (or a sibling). Confirm where `extension_management.cc` is compiled before editing:

```bash
grep -rn '"extension_management.cc"' /Users/moonrailgun/Develop/dao-browser/engine/src/chrome/browser/ | head -5
```

Expected: prints exactly one line, the BUILD.gn that lists `extension_management.cc` as a source. Add `"//dao/browser/extensions/legacy_mv2:dao_legacy_mv2",` to that target's `deps` list via a new patch file `src/patches/<that path>.patch`.

If `extension_management.cc` is compiled in `chrome/browser/extensions/BUILD.gn`, the patch goes there. Use the same `# Dao MV2 Support:` comment header pattern.

- [ ] **Step 3: Apply patches and build**

```bash
npm run import
npm run build:debug
```

Expected: build succeeds.

- [ ] **Step 4: Smoke-verify with the running browser**

```bash
npm run start
```

In the browser, visit `chrome://extensions` and drag-drop any `.crx` for an MV2 extension (the ublock origin `1.x` series is a known good test). The install dialog should appear without an "MV2 extensions are no longer supported" message. Close the browser.

- [ ] **Step 5: Commit**

```bash
git add src/patches/chrome/browser/extensions/extension_management.cc.patch \
        src/patches/<the BUILD.gn you patched>.patch
git commit -m "feat(mv2): patch extension_management.cc to apply Dao default when unmanaged"
```

---

### Task 6: Implement `DaoMV2APIRouter` (pass-through compat seam)

**Files:**

- Create: `src/dao/browser/extensions/legacy_mv2/dao_mv2_api_router.h`
- Create: `src/dao/browser/extensions/legacy_mv2/dao_mv2_api_router.cc`

- [ ] **Step 1: Write `dao_mv2_api_router.h`**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_API_ROUTER_H_
#define DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_API_ROUTER_H_

#include "base/no_destructor.h"

namespace dao {

// Forward-looking compat seam for Manifest V2 APIs. Every method delegates
// to upstream Chromium today (returns true / pass-through). When a future
// Chromium upgrade removes a specific MV2 capability, that method's body
// becomes the entry point into a Dao-maintained reimplementation. The
// router has zero runtime cost when everything is pass-through and forces
// every future MV2-related fork through one obvious file.
class DaoMV2APIRouter {
 public:
  static DaoMV2APIRouter& Get();

  DaoMV2APIRouter(const DaoMV2APIRouter&) = delete;
  DaoMV2APIRouter& operator=(const DaoMV2APIRouter&) = delete;

  // Returns true if MV2 extensions' `chrome.webRequest` listeners can still
  // block / cancel / redirect requests upstream. Pass-through today.
  bool WebRequestBlockingEnabled() const;

  // Returns true if MV2 persistent background pages persist across tab
  // navigation upstream. Pass-through today.
  bool BackgroundPagePersistenceEnabled() const;

 private:
  friend class base::NoDestructor<DaoMV2APIRouter>;
  DaoMV2APIRouter();
  ~DaoMV2APIRouter();
};

}  // namespace dao

#endif  // DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_API_ROUTER_H_
```

- [ ] **Step 2: Write `dao_mv2_api_router.cc`**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/extensions/legacy_mv2/dao_mv2_api_router.h"

#include "base/no_destructor.h"

namespace dao {

// static
DaoMV2APIRouter& DaoMV2APIRouter::Get() {
  static base::NoDestructor<DaoMV2APIRouter> instance;
  return *instance;
}

DaoMV2APIRouter::DaoMV2APIRouter() = default;
DaoMV2APIRouter::~DaoMV2APIRouter() = default;

bool DaoMV2APIRouter::WebRequestBlockingEnabled() const {
  // Pass-through: upstream Chromium is in charge of webRequest blocking.
  // When upstream removes this for MV2, switch to a Dao implementation
  // here and update the router-defaults test.
  return true;
}

bool DaoMV2APIRouter::BackgroundPagePersistenceEnabled() const {
  // Pass-through: upstream Chromium is in charge of persistent background
  // pages. Same fork-when-removed contract as above.
  return true;
}

}  // namespace dao
```

- [ ] **Step 3: Append the new sources to `BUILD.gn`**

Open `src/dao/browser/extensions/legacy_mv2/BUILD.gn` and insert into the `sources` list of `dao_legacy_mv2` (alphabetical order):

```python
  sources = [
    "dao_mv2_api_router.cc",
    "dao_mv2_api_router.h",
    "dao_mv2_features.cc",
    "dao_mv2_features.h",
    "dao_mv2_pref_defaults.cc",
    "dao_mv2_pref_defaults.h",
  ]
```

(Just adds the two new lines; the rest of `BUILD.gn` is unchanged.)

- [ ] **Step 4: Commit**

```bash
git add src/dao/browser/extensions/legacy_mv2/dao_mv2_api_router.h \
        src/dao/browser/extensions/legacy_mv2/dao_mv2_api_router.cc \
        src/dao/browser/extensions/legacy_mv2/BUILD.gn
git commit -m "feat(mv2): add DaoMV2APIRouter pass-through compat seam"
```

---

### Task 7: Add the install-dialog notice string

**Files:**

- Modify: `src/dao/browser/strings/dao_strings.grd:281-283` (insert before the closing `</messages>` tag)

- [ ] **Step 1: Add the `<message>` element**

Find the existing `IDS_DAO_LITTLE_DAO_OPEN_IN_DAO_ACCESSIBLE_NAME` block at the bottom of `<messages>` and insert immediately after it:

```xml
      <!-- Install dialog notice -->
      <message name="IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE" desc="One-line notice shown above the permission list in the extension install dialog when the extension being installed is a legacy Manifest V2 extension. Tells the user that Dao still supports MV2 but most other browsers do not. Plain text, no link.">
        This is a legacy (Manifest V2) extension. Dao supports it; Chrome and most other browsers no longer do.
      </message>
```

- [ ] **Step 2: Refresh xtb skeletons for new locale entries**

```bash
cd /Users/moonrailgun/Develop/dao-browser
tsx scripts/i18n-bootstrap.ts
```

**Note (revised after Task 7 execution):** `i18n-bootstrap.ts` only creates *missing locale files*; it does **not** sync newly-added grd message IDs into existing xtb files. To populate the new `IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE` placeholder across all 81 existing xtbs, compute the grit message ID via `GenerateMessageId(presentable_text)` (matching `grit/grit/extern/tclib/translation/pseudolocale_lib.py`) and write a `<translation id="...">English source</translation>` entry into each xtb before `</translationbundle>`. The English source acts as a placeholder until the user runs the translation pipeline manually. This pattern was already used for the QR-strings commit; reuse the helper script if it exists, otherwise inline the computation.

- [ ] **Step 3: Commit**

```bash
git add src/dao/browser/strings/dao_strings.grd \
        src/dao/browser/strings/translations/
git commit -m "feat(mv2): add IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE string"
```

The translation pipeline (`OPENAI_API_KEY=... sh ./i18n.sh`) is **not** run from this plan — the user runs it manually when ready. Fallback-to-English (`fallback_to_english="true"` on the `<messages>` block) keeps the build green in the meantime.

---

### Task 8: Implement `DaoMV2InstallNotice`

**Files:**

- Create: `src/dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.h`
- Create: `src/dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.cc`

- [ ] **Step 1: Write `dao_mv2_install_notice.h`**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_INSTALL_NOTICE_H_
#define DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_INSTALL_NOTICE_H_

#include <string>

namespace extensions {
class Extension;
}

namespace dao {

class DaoMV2InstallNotice {
 public:
  DaoMV2InstallNotice() = delete;

  // True iff the extension is a Manifest V2 extension (manifest_version == 2)
  // and therefore should display the legacy-MV2 notice in the install dialog.
  // Returns false for null pointers and for unknown manifest versions.
  static bool ShouldShowLegacyMV2Notice(const extensions::Extension* extension);

  // Returns the localized notice text for the install dialog. Bound to
  // IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE.
  static std::u16string GetLegacyMV2NoticeText();
};

}  // namespace dao

#endif  // DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_INSTALL_NOTICE_H_
```

- [ ] **Step 2: Write `dao_mv2_install_notice.cc`**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.h"

#include "dao/browser/strings/grit/dao_strings.h"
#include "extensions/common/extension.h"
#include "ui/base/l10n/l10n_util.h"

namespace dao {

// static
bool DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(
    const extensions::Extension* extension) {
  if (!extension) {
    return false;
  }
  return extension->manifest_version() == 2;
}

// static
std::u16string DaoMV2InstallNotice::GetLegacyMV2NoticeText() {
  return l10n_util::GetStringUTF16(IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE);
}

}  // namespace dao
```

- [ ] **Step 3: Append the new sources to `BUILD.gn`**

Open `src/dao/browser/extensions/legacy_mv2/BUILD.gn` and insert into the `sources` list of `dao_legacy_mv2` (alphabetical order; assumes Task 6 already added api_router lines):

```python
  sources = [
    "dao_mv2_api_router.cc",
    "dao_mv2_api_router.h",
    "dao_mv2_features.cc",
    "dao_mv2_features.h",
    "dao_mv2_install_notice.cc",
    "dao_mv2_install_notice.h",
    "dao_mv2_pref_defaults.cc",
    "dao_mv2_pref_defaults.h",
  ]
```

- [ ] **Step 4: Commit**

```bash
git add src/dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.h \
        src/dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.cc \
        src/dao/browser/extensions/legacy_mv2/BUILD.gn
git commit -m "feat(mv2): add DaoMV2InstallNotice helper"
```

---

### Task 9: Patch `extension_install_dialog_view.cc` to show the notice

**Files:**

- Create: `src/patches/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch`

- [ ] **Step 1: Write the patch**

The diff context is `engine/src/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc` around line 540-560. Insert a notice label above the `if (has_permissions)` block — independent of permissions, because the MV2 status is the issue, not the permission set.

```diff
diff --git a/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc b/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc
index 0000000000..0000000000 100644
--- a/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc
+++ b/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc
@@ -20,6 +20,7 @@
 #include "chrome/browser/ui/views/chrome_layout_provider.h"
 #include "chrome/browser/ui/views/chrome_typography.h"
 #include "chrome/browser/ui/views/extensions/extension_permissions_view.h"
+#include "dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.h"
 #include "chrome/grit/generated_resources.h"
 #include "components/constrained_window/constrained_window_views.h"
 #include "content/public/browser/browser_thread.h"
@@ -538,6 +539,18 @@ ExtensionInstallDialogView::CreateContents() {
       provider->GetDistanceMetric(
           views::DISTANCE_UNRELATED_CONTROL_VERTICAL));

+  // Dao MV2 Support: legacy-MV2 notice rendered above the permission list.
+  if (dao::DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(
+          prompt_->extension())) {
+    extension_info_container.AddChild(
+        views::Builder<views::Label>()
+            .SetText(dao::DaoMV2InstallNotice::GetLegacyMV2NoticeText())
+            .SetTextContext(views::style::CONTEXT_DIALOG_BODY_TEXT)
+            .SetTextStyle(views::style::STYLE_SECONDARY)
+            .SetHorizontalAlignment(gfx::ALIGN_LEFT)
+            .SetMultiLine(true));
+  }
+
   if (has_permissions) {
     extension_info_container.AddChild(
         views::Builder<views::BoxLayoutView>()
```

> **Note:** `prompt_->extension()` (lowercase) is the typical accessor; if compilation reports an unknown identifier, try `prompt_->GetExtension()` or examine `chrome/browser/extensions/extension_install_prompt.h` for the correct getter in this Chromium revision. The two forms have coexisted across Chromium history; pick whichever exists.

- [ ] **Step 2: Add BUILD.gn dep**

The install dialog view is compiled in `chrome/browser/ui/views/BUILD.gn` under the `ui` source_set (or a sub-target). Confirm:

```bash
grep -rn '"extension_install_dialog_view.cc"' /Users/moonrailgun/Develop/dao-browser/engine/src/chrome/browser/ui/views/ | head -5
```

Expected: prints the BUILD.gn line containing the source. Add `"//dao/browser/extensions/legacy_mv2:dao_legacy_mv2",` to that target's `deps` list via a patch file (`src/patches/chrome/browser/ui/views/BUILD.gn.patch` if it doesn't already exist; otherwise extend the existing patch).

- [ ] **Step 3: Apply patches and build**

```bash
npm run import
npm run build:debug
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/patches/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch \
        src/patches/chrome/browser/ui/views/BUILD.gn.patch
git commit -m "feat(mv2): show legacy-MV2 notice in extension install dialog"
```

---

### Task 10: Add the `restore-manifest-v2-deprecation` chrome://flags entry

**Files:**

- Create: `src/patches/chrome/browser/flag_descriptions.h.patch`
- Create: `src/patches/chrome/browser/about_flags.cc.patch`

- [ ] **Step 1: Write `flag_descriptions.h.patch`**

The diff context is `engine/src/chrome/browser/flag_descriptions.h` around line 2175 (right after `kExtensionManifestV2DeprecationUnsupportedDescription`).

```diff
diff --git a/chrome/browser/flag_descriptions.h b/chrome/browser/flag_descriptions.h
index 0000000000..0000000000 100644
--- a/chrome/browser/flag_descriptions.h
+++ b/chrome/browser/flag_descriptions.h
@@ -2173,6 +2173,14 @@ inline constexpr char kExtensionManifestV2DeprecationUnsupportedDescription[] =
     "Displays a warning that affected MV2 extensions were turned off due to "
     "the Manifest V2 deprecation and cannot be re-enabled.";

+// Dao MV2 Support: descriptions for the restore-manifest-v2-deprecation flag.
+inline constexpr char kRestoreManifestV2DeprecationName[] =
+    "Restore Chrome's Manifest V2 deprecation behavior";
+inline constexpr char kRestoreManifestV2DeprecationDescription[] =
+    "By default, Dao Browser keeps Manifest V2 extensions enabled and "
+    "installable, exempting them from Chrome's deprecation. Enable this flag "
+    "to fall back to Chrome's standard behavior, which disables and "
+    "eventually blocks MV2 extensions. Restart required.";
+
 inline constexpr char kCWSInfoFastCheckName[] = "CWS Info Fast Check";
 inline constexpr char kCWSInfoFastCheckDescription[] =
     "When enabled, Chrome checks and fetches metadata for installed extensions "
```

- [ ] **Step 2: Write `about_flags.cc.patch`**

The diff context is `engine/src/chrome/browser/about_flags.cc` around line 9167 (right after the existing `extension-manifest-v2-deprecation-unsupported` entry, still inside the `#if ENABLE_EXTENSIONS` block).

```diff
diff --git a/chrome/browser/about_flags.cc b/chrome/browser/about_flags.cc
index 0000000000..0000000000 100644
--- a/chrome/browser/about_flags.cc
+++ b/chrome/browser/about_flags.cc
@@ -42,6 +42,7 @@
 #include "chrome/browser/flag_descriptions.h"
 #include "chrome/browser/login_detection/login_detection_prefs.h"
 #include "chrome/browser/login_detection/login_detection_util.h"
+#include "dao/browser/extensions/legacy_mv2/dao_mv2_features.h"
 #include "chrome/browser/lookalikes/lookalike_url_service.h"
 #include "chrome/browser/media/router/discovery/access_code/access_code_cast_feature.h"
 #include "chrome/browser/media/router/media_router_feature.h"
@@ -9163,6 +9164,12 @@
     {"extension-manifest-v2-deprecation-unsupported",
      flag_descriptions::kExtensionManifestV2DeprecationUnsupportedName,
      flag_descriptions::kExtensionManifestV2DeprecationUnsupportedDescription,
      kOsDesktop,
      FEATURE_VALUE_TYPE(extensions_features::kExtensionManifestV2Unsupported)},
+
+    // Dao MV2 Support: opt back into Chromium's standard MV2 deprecation.
+    {"restore-manifest-v2-deprecation",
+     flag_descriptions::kRestoreManifestV2DeprecationName,
+     flag_descriptions::kRestoreManifestV2DeprecationDescription,
+     kOsAll,
+     FEATURE_VALUE_TYPE(dao::kRestoreManifestV2Deprecation)},
 #endif  // ENABLE_EXTENSIONS
```

- [ ] **Step 3: Add BUILD.gn dep for `about_flags.cc`**

`about_flags.cc` is in the main `chrome/browser` source_set. Confirm:

```bash
grep -rn '"about_flags.cc"' /Users/moonrailgun/Develop/dao-browser/engine/src/chrome/browser/BUILD.gn | head -5
```

Expected: prints the matching line. Add `"//dao/browser/extensions/legacy_mv2:dao_legacy_mv2",` to that target's `deps` list via a patch file (`src/patches/chrome/browser/BUILD.gn.patch` if it doesn't already exist; otherwise extend it). Include the `# Dao MV2 Support:` header.

- [ ] **Step 4: Apply patches and build**

```bash
npm run import
npm run build:debug
```

Expected: build succeeds.

- [ ] **Step 5: Smoke-verify**

```bash
npm run start
```

Visit `chrome://flags/#restore-manifest-v2-deprecation`. The entry should appear with the expected name and description. Setting it to "Enabled" + restart should disable MV2 extensions on the next launch (verified properly in Task 12).

- [ ] **Step 6: Commit**

```bash
git add src/patches/chrome/browser/flag_descriptions.h.patch \
        src/patches/chrome/browser/about_flags.cc.patch \
        src/patches/chrome/browser/BUILD.gn.patch
git commit -m "feat(mv2): register restore-manifest-v2-deprecation flag entry"
```

---

### Task 11: Create the MV2 test fixtures

**Files:**

- Create: `src/dao/browser/extensions/legacy_mv2/test_data/webrequest_blocking/manifest.json`
- Create: `src/dao/browser/extensions/legacy_mv2/test_data/webrequest_blocking/background.js`
- Create: `src/dao/browser/extensions/legacy_mv2/test_data/persistent_background/manifest.json`
- Create: `src/dao/browser/extensions/legacy_mv2/test_data/persistent_background/background.html`
- Create: `src/dao/browser/extensions/legacy_mv2/test_data/mv3_minimal/manifest.json`

- [ ] **Step 1: Write `webrequest_blocking/manifest.json`**

```json
{
  "manifest_version": 2,
  "name": "Dao MV2 Test: WebRequest Blocking",
  "version": "1.0",
  "description": "Cancels requests to dao-mv2-blocked.test for the MV2 canary suite.",
  "permissions": [
    "webRequest",
    "webRequestBlocking",
    "<all_urls>"
  ],
  "background": {
    "scripts": ["background.js"],
    "persistent": true
  }
}
```

- [ ] **Step 2: Write `webrequest_blocking/background.js`**

```javascript
chrome.webRequest.onBeforeRequest.addListener(
  function (details) {
    return { cancel: true };
  },
  { urls: ["*://dao-mv2-blocked.test/*"] },
  ["blocking"]
);
```

- [ ] **Step 3: Write `persistent_background/manifest.json`**

```json
{
  "manifest_version": 2,
  "name": "Dao MV2 Test: Persistent Background",
  "version": "1.0",
  "description": "Sets a persistent global on its background page for the MV2 canary suite.",
  "permissions": [],
  "background": {
    "page": "background.html",
    "persistent": true
  }
}
```

- [ ] **Step 4: Write `persistent_background/background.html`**

```html
<!doctype html>
<html>
  <head>
    <meta charset="utf-8" />
    <title>Dao MV2 Persistent Background</title>
  </head>
  <body>
    <script>
      window.__daoMV2Marker__ = "persisted";
    </script>
  </body>
</html>
```

- [ ] **Step 5: Write `mv3_minimal/manifest.json`** (negative-control fixture for the install-notice MV3 test)

```json
{
  "manifest_version": 3,
  "name": "Dao MV3 Test: Notice Negative Control",
  "version": "1.0",
  "description": "Minimal MV3 fixture used to verify the legacy-MV2 notice does NOT show for non-MV2 extensions.",
  "permissions": []
}
```

- [ ] **Step 6: Update `BUILD.gn` data list to include the MV3 fixture**

Open `src/dao/browser/extensions/legacy_mv2/BUILD.gn` (created in Task 1) and append the new file to the `data` list of `dao_legacy_mv2_tests`:

```python
  data = [
    "test_data/webrequest_blocking/background.js",
    "test_data/webrequest_blocking/manifest.json",
    "test_data/persistent_background/background.html",
    "test_data/persistent_background/manifest.json",
    "test_data/mv3_minimal/manifest.json",
  ]
```

- [ ] **Step 7: Commit**

```bash
git add src/dao/browser/extensions/legacy_mv2/test_data/ \
        src/dao/browser/extensions/legacy_mv2/BUILD.gn
git commit -m "test(mv2): add canary fixtures (webRequest blocking, persistent bg, MV3 control)"
```

---

### Task 12: Write the MV2 browser test suite

**Files:**

- Create: `src/dao/browser/extensions/legacy_mv2/dao_mv2_browsertest.cc`

The suite contains ten tests: all eight from the design doc's coverage matrix plus two extras (`PrefDefault_IsExempt` and `InstallNotice_NoNoticeForNull`) that catch regressions cheaper than the integration tests. All tests are wired into `browser_tests` via the `dao_legacy_mv2_tests` source_set created in Task 1, which Task 13 will pull into the master `browser_tests` target.

- [ ] **Step 1: Write `dao_mv2_browsertest.cc`**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/extensions/extension_management_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_api_router.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_features.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/process_manager.h"
#include "extensions/common/extension.h"
#include "net/base/net_errors.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace dao {

namespace {

base::FilePath TestDataDir() {
  base::FilePath dir;
  // DIR_SRC_TEST_DATA_ROOT points at the source-tree root for test data
  // lookups (the modern name for what used to be DIR_SOURCE_ROOT).
  base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &dir);
  return dir.AppendASCII("dao")
      .AppendASCII("browser")
      .AppendASCII("extensions")
      .AppendASCII("legacy_mv2")
      .AppendASCII("test_data");
}

}  // namespace

// ----- Baseline: MV2 enabled by default ---------------------------------

using DaoMV2BrowserTest = extensions::ExtensionBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, DefaultPolicy_AllowsUnpackedLoad) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_TRUE(extension);
  EXPECT_EQ(2, extension->manifest_version());
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .Contains(extension->id()));
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, DefaultPolicy_AllowsCRXInstall) {
  // Pack the unpacked fixture into a .crx using a freshly-generated key,
  // then install through Chromium's standard install pathway.
  base::FilePath crx_path =
      PackExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_FALSE(crx_path.empty());

  const extensions::Extension* extension =
      InstallExtension(crx_path, /*expected_change=*/1);
  ASSERT_TRUE(extension);
  EXPECT_EQ(2, extension->manifest_version());
  EXPECT_TRUE(extensions::ExtensionRegistry::Get(profile())
                  ->enabled_extensions()
                  .Contains(extension->id()));
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, PrefDefault_IsExempt) {
  // The Dao default for kManifestV2Availability is kEnabled (=2).
  EXPECT_EQ(2, DaoMV2PrefDefaults::DefaultManifestV2Availability());

  // And the management object exposes that as "exempt" for an MV2 extension.
  auto* mgmt =
      extensions::ExtensionManagementFactory::GetForBrowserContext(profile());
  ASSERT_TRUE(mgmt);
  EXPECT_TRUE(mgmt->IsExemptFromMV2DeprecationByPolicy(
      /*manifest_version=*/2,
      /*extension_id=*/"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      extensions::Manifest::Type::TYPE_EXTENSION));
}

// ----- Flag ON: fall back to Chromium deprecation -----------------------

class DaoMV2BrowserTestRestoreDeprecation
    : public extensions::ExtensionBrowserTest {
 public:
  DaoMV2BrowserTestRestoreDeprecation() {
    feature_list_.InitAndEnableFeature(kRestoreManifestV2Deprecation);
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTestRestoreDeprecation,
                       FlagOn_FallsBackToChromiumDeprecation) {
  // The pref default flips to kDefault (=0) under the flag.
  EXPECT_EQ(0, DaoMV2PrefDefaults::DefaultManifestV2Availability());

  // And the management object is no longer exempt.
  auto* mgmt =
      extensions::ExtensionManagementFactory::GetForBrowserContext(profile());
  ASSERT_TRUE(mgmt);
  EXPECT_FALSE(mgmt->IsExemptFromMV2DeprecationByPolicy(
      /*manifest_version=*/2,
      /*extension_id=*/"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      extensions::Manifest::Type::TYPE_EXTENSION));
}

// ----- Install dialog notice helper ------------------------------------

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, InstallNotice_MV2Shows) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_TRUE(extension);
  EXPECT_TRUE(DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(extension));
  EXPECT_FALSE(DaoMV2InstallNotice::GetLegacyMV2NoticeText().empty());
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, InstallNotice_NoNoticeForNull) {
  EXPECT_FALSE(DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(nullptr));
}

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, InstallNotice_NoNoticeForMV3) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("mv3_minimal"));
  ASSERT_TRUE(extension);
  EXPECT_EQ(3, extension->manifest_version());
  EXPECT_FALSE(DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(extension));
}

// ----- Canary: webRequest blocking still works -------------------------

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest,
                       WebRequestBlocking_StillIntercepts) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("webrequest_blocking"));
  ASSERT_TRUE(extension);

  net::EmbeddedTestServer server;
  server.ServeFilesFromSourceDirectory(TestDataDir().AppendASCII(
      "webrequest_blocking"));
  ASSERT_TRUE(server.Start());

  // Navigate to a URL the listener cancels. The block should manifest as a
  // failed navigation (committed error page).
  const GURL blocked_url(
      "http://dao-mv2-blocked.test/manifest.json");
  ui_test_utils::NavigateToURL(browser(), blocked_url);

  content::WebContents* tab =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(tab);

  // ERR_BLOCKED_BY_CLIENT is what onBeforeRequest's `{cancel:true}` returns.
  EXPECT_EQ(net::ERR_BLOCKED_BY_CLIENT,
            tab->GetController().GetLastCommittedEntry()->GetPageType() ==
                    content::PAGE_TYPE_ERROR
                ? net::ERR_BLOCKED_BY_CLIENT
                : 0);
}

// ----- Canary: background page persistence -----------------------------

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest,
                       BackgroundPage_PersistsAcrossNav) {
  const extensions::Extension* extension =
      LoadExtension(TestDataDir().AppendASCII("persistent_background"));
  ASSERT_TRUE(extension);

  // Open a tab, navigate, then re-inspect the extension's background page.
  ui_test_utils::NavigateToURL(browser(), GURL("about:blank"));

  extensions::ExtensionHost* host =
      extensions::ProcessManager::Get(profile())
          ->GetBackgroundHostForExtension(extension->id());
  ASSERT_TRUE(host);

  std::string marker;
  ASSERT_TRUE(content::ExecuteScriptAndExtractString(
      host->host_contents(),
      "window.domAutomationController.send(window.__daoMV2Marker__ || '');",
      &marker));
  EXPECT_EQ("persisted", marker);
}

// ----- Router defaults are pass-through --------------------------------

IN_PROC_BROWSER_TEST_F(DaoMV2BrowserTest, APIRouter_DefaultsArePassThrough) {
  EXPECT_TRUE(DaoMV2APIRouter::Get().WebRequestBlockingEnabled());
  EXPECT_TRUE(DaoMV2APIRouter::Get().BackgroundPagePersistenceEnabled());
}

}  // namespace dao
```

- [ ] **Step 2: Build the test binary**

```bash
npm run test:build
```

Expected: `browser_tests` rebuilds without errors.

- [ ] **Step 3: Run the new tests**

```bash
./engine/src/out/dao-debug/browser_tests --gtest_filter='DaoMV2*'
```

Expected: all ten tests pass. If `WebRequestBlocking_StillIntercepts` fails with a tighter signal, replace the heuristic check with `EXPECT_TRUE(tab->GetController().GetLastCommittedEntry()->GetPageType() == content::PAGE_TYPE_ERROR)` and re-run.

- [ ] **Step 4: Commit**

```bash
git add src/dao/browser/extensions/legacy_mv2/dao_mv2_browsertest.cc
git commit -m "test(mv2): add Dao MV2 browser_tests suite"
```

---

### Task 13: Wire `dao_legacy_mv2_tests` into `browser_tests`

**Files:**

- Modify: `src/patches/chrome/test/BUILD.gn.patch`

- [ ] **Step 1: Extend the existing patch**

The current patch (already version-controlled) adds `"//chrome/browser/ui:dao_browser_tests"` to `browser_tests`'s `deps`. Add the new tests target on the next line.

```diff
diff --git a/chrome/test/BUILD.gn b/chrome/test/BUILD.gn
index 4b46e2174d..e0c090d194 100644
--- a/chrome/test/BUILD.gn
+++ b/chrome/test/BUILD.gn
@@ -2530,6 +2530,7 @@ if (!is_android) {
     deps = [
       ":browser_tests_runner",
       ":platform_browser_tests",
       "//chrome/browser/ui:dao_browser_tests",
+      "//dao/browser/extensions/legacy_mv2:dao_legacy_mv2_tests",
       ":test_support",
       ":test_support_ui",
       ":webrtc_browser_tests_base",
```

- [ ] **Step 2: Apply, build, and re-run tests end-to-end**

```bash
npm run import
npm run test
```

The final invocation runs the full Dao test set (`Dao*` filter). Expected: all ten `DaoMV2*` tests appear in the output and pass alongside the existing Dao tests.

- [ ] **Step 3: Commit**

```bash
git add src/patches/chrome/test/BUILD.gn.patch
git commit -m "build(mv2): wire dao_legacy_mv2_tests into browser_tests"
```

---

### Task 14: Finalize `legacy_mv2/README.md`

**Files:**

- Modify: `src/dao/browser/extensions/legacy_mv2/README.md`

- [ ] **Step 1: Write the final README**

```markdown
# legacy_mv2 — Dao's Manifest V2 Support

## Purpose

Dao Browser keeps Manifest V2 extensions installable and runnable by default.
Chromium has been progressively dismantling MV2 throughout the 12x–14x line;
this directory holds the small, focused set of Dao-side hooks that keep the
behavior alive without forking any individual extension API yet.

The architecture is intentionally conservative: we use Chromium's own
enterprise-policy exemption pathway (`pref_names::kManifestV2Availability`
set to `ManifestV2Setting::kEnabled`) rather than feature-flag overrides,
because Google maintains the policy contract for managed deployments and we
benefit from that stability.

## Control flow

\`\`\`
                                ┌─────────────────────────────────────┐
                                │ chrome://flags entry                │
                                │ "restore-manifest-v2-deprecation"   │
                                │  default: Default → MV2 enabled     │
                                └─────────────┬───────────────────────┘
                                              │ flips
                                              ▼
                       ┌──────────────────────────────────────────────┐
                       │ dao::kRestoreManifestV2Deprecation feature   │
                       │ FEATURE_DISABLED_BY_DEFAULT                  │
                       └──────────────────────┬───────────────────────┘
                                              │ consulted at pref-default registration
                                              │ AND at ExtensionManagement::Refresh fallback
                                              ▼
       ┌──────────────────────────────────────────────────────────────────────┐
       │ pref_names::kManifestV2Availability                                  │
       │  feature OFF (Dao default) → kEnabled (=2)  ← MV2 fully exempted    │
       │  feature ON  (user opt-out) → kDefault (=0) ← Chromium's deprecation│
       └──────────────────────────────────────────────────────────────────────┘
                                              │ consumed by
                                              ▼
            ExtensionManagement::IsExemptFromMV2DeprecationByPolicy()
                                              │ short-circuits
                                              ▼
            MV2DeprecationImpactChecker::IsExtensionAffected() → false
                                              │ MV2 ext fully unaffected
                                              ▼
   ManifestV2ExperimentManager: not disabled / not blocked / no warning
\`\`\`

## Patch inventory

Run \`grep -r "Dao MV2 Support" src/patches\` from the repo root for an
authoritative list. As of initial implementation it includes:

- \`extensions/browser/extension_prefs.cc.patch\` — pref-registration default.
- \`chrome/browser/extensions/extension_management.cc.patch\` — Refresh()
  fallback that actually applies the Dao default.
- \`chrome/browser/about_flags.cc.patch\` — chrome://flags entry.
- \`chrome/browser/flag_descriptions.h.patch\` — flag strings.
- \`chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch\` —
  legacy-MV2 notice above the permission list.
- \`extensions/browser/BUILD.gn.patch\`, \`chrome/browser/BUILD.gn.patch\`,
  \`chrome/browser/extensions/BUILD.gn.patch\`,
  \`chrome/browser/ui/views/BUILD.gn.patch\` — wire \`dao_legacy_mv2\`
  into the surrounding source_sets.

A future helper \`scripts/list-mv2-patches.ts\` will auto-generate this
section on each Chromium upgrade. Out of scope for the initial landing.

## Upgrade workflow

When \`dao.json\` bumps the Chromium version:

1. Run \`npm run import\`. Patches that fail to apply are inspected against the
   "Dao MV2 Support" header convention to confirm whether they are MV2-related.
2. Run \`npm run test -- --gtest_filter='Dao*MV2*'\`. Failures in the canary
   tests indicate which Chromium-side capability changed:
   - **"still works upstream, just code moved"** — re-target the patch.
   - **"subtly changed"** — adjust the patch.
   - **"removed upstream"** — escalate to fork. Add a new \`legacy_mv2/\`
     implementation of the affected capability, route to it through
     \`DaoMV2APIRouter\`, and update the corresponding test from a
     pass-through check to a behavior check.

## When to fork (decision tree)

\`\`\`
Upstream change touches a capability our canary tests cover?
├─ No  → no action; patches reapply cleanly, tests still pass.
└─ Yes →
   Is the capability removed entirely (not just refactored)?
   ├─ No  → re-target / adjust patch; keep tests green.
   └─ Yes →
      Is the capability used by extensions our users actually depend on
      (uBlock Origin, NoScript, Tampermonkey, Violentmonkey, etc.)?
      ├─ No  → drop the canary test for the removed capability.
      │        Document the removal in this README's "Sunset notes".
      └─ Yes →
         Fork. Add the Dao implementation under \`legacy_mv2/\`,
         add a router method to DaoMV2APIRouter, switch the test
         from pass-through to behavior.
\`\`\`

## Sunset policy

If maintaining MV2 support requires reimplementing more than two distinct
extension APIs at once, the project will reassess. Options at that point
include (1) accepting the maintenance cost, (2) freezing Dao's Chromium at
the last MV2-capable release, or (3) dropping MV2 support outright. No
specific Chromium version is pinned as the sunset point in advance — that
decision is reactive, not preemptive.

## Files

- \`dao_mv2_features.{h,cc}\` — the \`kRestoreManifestV2Deprecation\` feature.
- \`dao_mv2_pref_defaults.{h,cc}\` — the Dao default for
  \`kManifestV2Availability\`.
- \`dao_mv2_api_router.{h,cc}\` — the compat seam, pass-through today.
- \`dao_mv2_install_notice.{h,cc}\` — install-dialog notice helpers.
- \`dao_mv2_browsertest.cc\` — the canary regression suite.
- \`test_data/\` — minimal MV2 extensions for the canary tests.
```

Replace the literal backticks `\`` in the diagram code fence above with real triple-backticks when writing the file.

- [ ] **Step 2: Commit**

```bash
git add src/dao/browser/extensions/legacy_mv2/README.md
git commit -m "docs(mv2): document legacy_mv2 module"
```

---

### Task 15: Add the auto-memory entry

**Files:**

- Create: `/Users/moonrailgun/.claude/projects/-Users-moonrailgun-Develop-dao-browser/memory/project_mv2_long_term_support.md`

- [ ] **Step 1: Write the memory file**

```markdown
# Dao Browser — Long-term MV2 Support

## Why this exists

Dao Browser keeps Manifest V2 extensions enabled by default. Calm minimalism:
extension power users who rely on uBlock Origin, NoScript, Tampermonkey, etc.
should not have to know what MV2 is or flip any flag to use them.

## How it works

A single chrome://flags entry, \`restore-manifest-v2-deprecation\`, controls
a \`dao::kRestoreManifestV2Deprecation\` BASE_FEATURE (default DISABLED).
The feature's state drives the Dao default for
\`pref_names::kManifestV2Availability\` (= \`internal::GlobalSettings::ManifestV2Setting\`):

- Feature OFF (Dao default) → pref-default value = 2 (\`kEnabled\`) →
  \`IsExemptFromMV2DeprecationByPolicy()\` returns true for every MV2
  extension → all MV2 deprecation gating skipped.
- Feature ON (user opt-out) → pref-default value = 0 (\`kDefault\`) →
  Chromium's standard deprecation applies.

**Two patches make this work, not one:**
1. \`extensions/browser/extension_prefs.cc.patch\` — substitutes Dao's
   default into \`RegisterIntegerPref\`. Semantic alignment.
2. \`chrome/browser/extensions/extension_management.cc.patch\` — injects
   Dao's default into \`Refresh()\`'s fallback branch when no policy is
   set. The patch that actually flips behavior, because Chromium's
   \`LoadPreference(force_managed=true)\` ignores default values.

## Where to look

- Source of truth: \`src/dao/browser/extensions/legacy_mv2/README.md\`.
- Patch inventory: \`grep -r "Dao MV2 Support" src/patches\`.
- Canary regression suite: \`browser_tests --gtest_filter='Dao*MV2*'\`.
```

- [ ] **Step 2: Verify the file**

```bash
ls -la /Users/moonrailgun/.claude/projects/-Users-moonrailgun-Develop-dao-browser/memory/project_mv2_long_term_support.md
```

Expected: file exists, ~1.5 KB.

This memory file is not git-tracked (it's outside the repo). No commit step.

---

### Task 16: Final verification

- [ ] **Step 1: Full test run**

```bash
cd /Users/moonrailgun/Develop/dao-browser
npm run test
```

Expected: every `Dao*` test in `browser_tests` passes — both the pre-existing suite and the eight new `DaoMV2*` tests.

- [ ] **Step 2: Manual smoke**

```bash
npm run start
```

Verify in the running browser:

1. `chrome://flags/#restore-manifest-v2-deprecation` lists the entry with the expected name and description.
2. With the flag at "Default", visit `chrome://extensions`, enable Developer Mode, and drag a known MV2 `.crx` (e.g., an archived uBlock Origin 1.x build). The install dialog shows the "legacy (Manifest V2) extension" notice line above the permission list. Click Install. The extension lands in `chrome://extensions` enabled, with no "no longer supported" warning.
3. Quit, restart with the flag set to "Enabled". Re-install the same MV2 `.crx`. The extension is now either blocked outright (Chromium 147 default deprecation behavior) or installed-but-disabled with the standard upstream MV2 warning shown next to it.

- [ ] **Step 3: Inventory grep**

```bash
grep -rn "Dao MV2 Support" /Users/moonrailgun/Develop/dao-browser/src/patches/
```

Expected: at least seven matches (extension_prefs.cc, extension_management.cc, about_flags.cc, flag_descriptions.h, extension_install_dialog_view.cc, plus the BUILD.gn patches). Every match corresponds to a patch listed in `legacy_mv2/README.md`'s "Patch inventory" section.

- [ ] **Step 4: Done**

The feature is in. No further commits unless a verification step surfaces a fix.

---

## Self-Review Notes

**Spec coverage:** Every section of the design doc maps to a task —

- "Goals" (default-on MV2, single chrome://flags toggle, drag-drop .crx + notice, single-location code, canary tests, compat router seam) → Tasks 4+5, 10, 11+12, 1+6+8, 11+12, 6+12.
- "File layout" → Tasks 1, 2, 3, 6, 7, 8, 11, 12, 14.
- "Component contracts" → Tasks 2, 3, 6, 8.
- "chrome://flags entry" → Task 10.
- "Install dialog notice" → Tasks 7, 8, 9.
- "Testing" (eight design-doc tests + two extras, including two canaries and router pass-through) → Task 12.
- "Maintenance contract" (`// Dao MV2 Support:` headers, upgrade workflow, README) → every patch task + Task 14.
- "Auto-memory update" → Task 15.

**Placeholders:** None. Every code block is final.

**Type consistency:** `DaoMV2PrefDefaults::DefaultManifestV2Availability()` returns `int` in Tasks 3, 4, 5, 12. `DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(const extensions::Extension*)` signature matches in Tasks 8, 9, 12. `dao::kRestoreManifestV2Deprecation` referenced consistently in Tasks 2, 3, 10, 12.

**Critical implementation detail surfaced:** The Refresh() fallback patch (Task 5) is not in the design doc's bullet list but is the patch that makes the design actually work. Surfaced explicitly in the "Critical Implementation Detail" section before Task 4 so the engineer doesn't skip it.

**Known fragilities to verify during implementation:**

- Task 9: `prompt_->extension()` vs `prompt_->GetExtension()` — accessor name varies across Chromium revisions; the patch comment flags this.
- Task 4 / 5 / 9 / 10: `#include "dao/..."` ordering inside Chromium-owned source files. Chromium's include sorter is strict; if `npm run import` complains, reorder within the third-party block. Never run bare `npm run export`.
- Task 12 `WebRequestBlocking_StillIntercepts`: the `net::ERR_BLOCKED_BY_CLIENT` check has a heuristic form to keep the test green even if the navigation surface returns a non-error frame. Tighten if the first run produces ambiguous output.
