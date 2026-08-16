# Long-term Manifest V2 (MV2) Extension Support — Design

Date: 2026-05-26
Status: Approved (pending implementation plan)

## Summary

Dao Browser keeps Manifest V2 extensions installable and runnable while
upstream Chromium dismantles them. Support is enabled by default — Dao users
should not have to know what MV2 is to use uBlock Origin, Tampermonkey, and
similar legacy extensions. A single `chrome://flags` entry lets technical
users opt back into Chromium's standard MV2 deprecation behavior.

The mechanism uses Chromium's own enterprise-policy exemption pathway
(`pref_names::kManifestV2Availability` set to `kEnabled`), not feature-flag
overrides. This is the most stable hook we have access to because Google
designed it to remain working for enterprise deployments. A dedicated
`legacy_mv2/` subtree keeps every MV2-related file discoverable, and a
forward-looking compatibility router (today a pass-through) lays the
groundwork for forking individual MV2 APIs into Dao if Chromium removes them
upstream.

## Goals

- MV2 extensions install, load, and run by default. uBlock Origin, NoScript,
  Tampermonkey, Violentmonkey, and similar are usable from `.crx` files,
  unpacked sources, or any path Chromium currently supports.
- A single `chrome://flags` toggle named `restore-manifest-v2-deprecation`
  reverts to Chromium's standard MV2 deprecation behavior.
- Drag-and-drop `.crx` install on `chrome://extensions` works without manual
  workarounds, with a clear "this is a legacy MV2 extension" notice in the
  install confirmation dialog.
- All MV2 support lives in one obvious place under `src/dao/browser/extensions/legacy_mv2/`
  and patches under `src/patches/...` carry a `// Dao MV2 Support:` header so
  upgrade auditing is fast.
- A baseline browser-test suite verifies representative MV2 capabilities
  (webRequest blocking, background page persistence, install gating). When a
  Chromium upgrade breaks one of these, the failing test names the broken
  capability immediately rather than waiting for a user to file the bug.
- The architecture leaves a clear seam (`DaoMV2APIRouter`) where, in a future
  Chromium version that removes an MV2 API, we can fork that single API into
  Dao without restructuring everything.

## Non-goals

- No work to keep the Chrome Web Store install button visible on MV2
  listings. Web Store delisting is a server-side decision; Dao does not try
  to spoof it. Users who want delisted extensions install from `.crx` or
  unpacked.
- No bundled curation of approved MV2 extensions. Any MV2 extension Chromium
  could install (modulo deprecation) installs the same way in Dao.
- No third-party extension store integrations (CRX4Chrome, crxsoso, etc.).
  Out of scope; can be added later with separate design docs.
- No UI affordance in `chrome://settings`. The toggle is `chrome://flags`-only
  per the brainstorm decision; settings UI patches are not introduced for this
  feature.
- No immediate fork of any Chromium MV2 implementation. The compat router is
  pass-through today; forking is reactive, not preemptive.
- No backend that lets users re-enable MV2 on a per-extension basis when the
  global flag is off. The `chrome://flags` toggle is global.

## Background

In Chromium 147 (the version Dao is pinned to via `dao.json`), MV2 enforcement
funnels through three layers:

1. **Feature flags** in `extensions/common/extension_features.cc` —
   `kExtensionManifestV2Unsupported` (default ENABLED) and
   `kExtensionManifestV2Disabled` (default ENABLED) determine the deprecation
   "stage" returned by `CalculateCurrentExperimentStage()`. Default stage is
   `kUnsupported`.
2. **Stage gating** in `chrome/browser/extensions/manifest_v2_experiment_manager.cc` —
   functions like `ShouldDisableLegacyExtensions`, `ShouldBlockExtensionInstallation`,
   and `ShouldBlockUnpackedExtensions` read the stage and decide whether MV2
   extensions are blocked.
3. **Policy exemption** in `chrome/browser/extensions/mv2_deprecation_impact_checker.cc` —
   `MV2DeprecationImpactChecker::IsExtensionAffected` short-circuits to `false`
   if `ExtensionManagement::IsExemptFromMV2DeprecationByPolicy` returns true,
   which it does when the per-profile pref `kManifestV2Availability` is set to
   `ManifestV2Setting::kEnabled` (=2). When `IsExtensionAffected` returns
   `false`, all gating in layer 2 skips the extension entirely.

The third layer is the cleanest hook because it is part of the enterprise
policy contract Google maintains for managed deployments. Setting the pref's
default value at registration time gives every Dao profile the "exempt"
treatment without modifying feature-flag defaults or gating logic.

There is also a feature `kAllowLegacyMV2Extensions` (added in 147), but it
only exempts unpacked extensions — `.crx` and store-installed MV2 still block.
We do not use it as the primary mechanism.

## Architecture

### Control flow

```
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
                                              ▼
       ┌──────────────────────────────────────────────────────────────────────┐
       │ pref_names::kManifestV2Availability default value                    │
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
```

### File layout

```
dao-browser/
├── src/dao/browser/extensions/legacy_mv2/                 [NEW]
│   ├── README.md                                          # maintenance contract
│   ├── BUILD.gn                                           # `dao_legacy_mv2` source_set,
│   │                                                      # following the dao_telemetry pattern
│   │                                                      # (standalone source_set + deps wire-up)
│   ├── dao_mv2_features.h/.cc                             # kRestoreManifestV2Deprecation
│   ├── dao_mv2_pref_defaults.h/.cc                        # pref-default helper
│   ├── dao_mv2_api_router.h/.cc                           # pass-through compat seam
│   ├── dao_mv2_install_notice.h/.cc                       # install dialog helper
│   ├── dao_mv2_browsertest.cc                             # baseline regression suite
│   └── test_data/                                         # minimal MV2 test extensions
│       ├── webrequest_blocking/                           # canary fixture for webRequest
│       └── persistent_background/                         # canary fixture for bg pages
│
├── src/patches/                                           [NEW patches]
│   ├── extensions/browser/extension_prefs.cc.patch
│   │       # At extension_prefs.cc:2218 (RegisterIntegerPref for
│   │       # kManifestV2Availability), replace the literal `0` default with
│   │       # `dao::DaoMV2PrefDefaults::DefaultManifestV2Availability()`.
│   ├── chrome/browser/about_flags.cc.patch
│   │       # Adds the `restore-manifest-v2-deprecation` entry to kFeatureEntries[].
│   ├── chrome/browser/flag_descriptions.h.patch
│   ├── chrome/browser/flag_descriptions.cc.patch
│   │       # Strings for the flag entry. flag_descriptions is en-only by design.
│   ├── chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch
│   │       # Inserts the legacy-MV2 notice label above the permission list
│   │       # when the prompt's extension has manifest_version == 2.
│   └── extensions/browser/BUILD.gn.patch
│           # Adds //dao/browser/extensions/legacy_mv2:dao_legacy_mv2 to deps
│           # so extension_prefs.cc can call DefaultManifestV2Availability().
│           # Plus chrome/test/BUILD.gn.patch update to wire dao_mv2_browsertest.cc
│           # and its test_data fixtures into browser_tests.
│
└── src/dao/browser/strings/dao_strings.grd                [MODIFIED]
        # New IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE message (translated
        # via the same i18n.sh pipeline as other Dao strings).
```

Every new patch begins with a header comment:

```
// Dao MV2 Support: <one-line description of the change>
```

This makes `grep -r "Dao MV2 Support" src/patches` an authoritative inventory
of the surface area touched, which is the single most important property when
upgrading Chromium.

### Component contracts

| Component | Responsibility | Key interface |
|---|---|---|
| `dao::kRestoreManifestV2Deprecation` | `BASE_FEATURE`, `FEATURE_DISABLED_BY_DEFAULT`. Sole purpose: be flippable from `chrome://flags`. | C++ feature constant in `dao::` namespace. |
| `DaoMV2PrefDefaults` | Single function `DaoMV2PrefDefaults::DefaultManifestV2Availability()` returning `int`. Reads `dao::kRestoreManifestV2Deprecation`; returns `2` (`kEnabled`) when the feature is OFF (Dao default), `0` (`kDefault`) when the feature is ON (user opt-out). Substituted into the literal default of `RegisterIntegerPref(pref_names::kManifestV2Availability, …)` in `extensions/browser/extension_prefs.cc`. | Static, returns `int`, no parameters; safe to call before `FeatureList` initialization (returns Dao default). |
| `DaoMV2APIRouter` | Forward-looking compat seam. Today: every method delegates to upstream Chromium. Future: when Chromium removes a specific MV2 API, that method's body switches to a Dao-maintained implementation. Singleton, lazily constructed. | `DaoMV2APIRouter::Get()`; methods like `WebRequestBlockingEnabled()`, `BackgroundPagePersistenceEnabled()` returning bool today. Method list grows reactively. |
| `DaoMV2InstallNotice` | Helpers for the install dialog: `ShouldShowLegacyMV2Notice(extension)` returns true iff `manifest_version == 2`; `GetLegacyMV2NoticeText()` returns the localized string from `IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE`. | Static functions; no state. |

### Why a router with no work to do today?

The router exists so that, when a Chromium upgrade removes a single MV2 API
(say, `webRequest` blocking), we have a place to put the Dao reimplementation
without restructuring the rest of the codebase. The router has zero runtime
cost when its methods all return `true` / pass-through, and its presence
forces every future MV2 patch through one obvious entry point. This is the D2
position: foundation now, fork only when forced.

The router does **not** abstract away Chromium's extension system. It is a
narrow seam at the points where Chromium's MV2 support sits. Currently those
points are notional (no API has been removed yet), so the router's method
list is small. When/if a removal happens, we add a method, fork the
implementation, and update the corresponding browser test.

## chrome://flags entry

- **Internal name**: `restore-manifest-v2-deprecation`
- **Feature**: `dao::kRestoreManifestV2Deprecation`, `FEATURE_DISABLED_BY_DEFAULT`
- **Display name** (en, in `flag_descriptions.cc`): `"Restore Chrome's Manifest V2 deprecation behavior"`
- **Description**: `"By default, Dao Browser keeps Manifest V2 extensions enabled and installable, exempting them from Chrome's deprecation. Enable this flag to fall back to Chrome's standard behavior, which disables and eventually blocks MV2 extensions. Restart required."`
- **Platforms**: `kOsAll`
- **Value type**: `FEATURE_VALUE_TYPE`

The flag's "Default" state is the Dao default (MV2 enabled). "Enabled" means
"opt back into Chrome's deprecation" — wording deliberately reverse-named so
the description reads naturally next to "Default" in the `chrome://flags` UI.

## Install dialog notice

When `ExtensionInstallDialogView` is constructed for an extension with
`manifest_version == 2`, a single-line `views::Label` is inserted above the
permission list. Text:

> "This is a legacy (Manifest V2) extension. Dao supports it; Chrome and most
> other browsers no longer do."

Visual treatment matches existing dialog accent text — secondary text color,
no icon, no background. The label is added unconditionally for MV2 extensions
regardless of the feature flag state, because the user is in the install
flow either way (when the flag is on and Chromium would block, the dialog
never appears at all).

The string ID is `IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE`. It is added to
`src/dao/browser/strings/dao_strings.grd` and goes through the standard
i18n.sh translation pipeline like every other Dao string.

## Testing

Test file: `src/dao/browser/extensions/legacy_mv2/dao_mv2_browsertest.cc`
(flat, not under a `test/` subdirectory, matching `dao_browser_browsertest.cc`).

Wire-up: `src/patches/chrome/test/BUILD.gn.patch` (already extended for
existing Dao tests) gains the new `dao_mv2_browsertest.cc` source.

### Coverage matrix

| Test | Verifies |
|---|---|
| `MV2_DefaultPolicy_AllowsCRXInstall` | Drag-drop a fixture MV2 `.crx`, confirm install completes and extension is enabled, with default settings. |
| `MV2_DefaultPolicy_AllowsUnpackedLoad` | Load an unpacked MV2 extension via `LoadExtension`, confirm it's enabled. |
| `MV2_FlagOn_FallsBackToChromiumDeprecation` | Enable `kRestoreManifestV2Deprecation`, attempt MV2 install, confirm Chromium's MV2 gating now blocks (extension is in disabled or blocked state, not enabled). |
| `MV2_InstallDialog_ShowsLegacyNotice` | Trigger the install prompt for an MV2 extension; assert the notice label is present and bound to `IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE`. |
| `MV2_InstallDialog_NoNoticeForMV3` | Same dialog with an MV3 fixture; assert no notice label. |
| `MV2_WebRequestBlocking_StillIntercepts` | Load an MV2 extension that registers a blocking `webRequest.onBeforeRequest` listener; navigate to a target URL; assert the listener cancelled the request. **This is the canary test for upstream removal of webRequest blocking.** |
| `MV2_BackgroundPage_PersistsAcrossNav` | Load an MV2 extension whose background page sets a global; navigate the active tab; from another extension page, assert the global is still there. **Canary for upstream removal of persistent background pages.** |
| `MV2_APIRouter_DefaultsArePassThrough` | Direct unit test of `DaoMV2APIRouter::Get()` — every public bool method returns `true` (i.e., upstream is in charge). When this fails after a Chromium upgrade, it means we deliberately changed a routing decision. |

The two "still works" tests (`WebRequestBlocking`, `BackgroundPage`) are the
core early-warning system. Test fixtures (minimal real MV2 extensions) live
under `src/dao/browser/extensions/legacy_mv2/test_data/` and are wired into
`browser_tests` via the existing `dao_browser_tests` source_set's `data`
field, mirroring how Chromium's own `extensions/test/data/` directory feeds
its browser tests. When a future Chromium upgrade silently disables one of
these capabilities, the test fails and points directly at the affected
capability.

## Maintenance contract

### Patch-header convention

Every patch under `src/patches/...` whose purpose is MV2 support starts with:

```
// Dao MV2 Support: <one-line description>
```

`grep -r "Dao MV2 Support" src/patches` is the authoritative inventory.

### Upgrade workflow

When `dao.json` Chromium version is bumped:

1. Run `npm run import` against the new Chromium tree. Patches that fail to
   apply are inspected with grep against the patch-header convention to
   confirm whether they are MV2-related.
2. Run `npm run test -- --gtest_filter='*MV2*'`. Failures in the canary
   tests indicate which Chromium-side capability changed. The README in
   `legacy_mv2/` lists the decision matrix:
   - "still works upstream, just code moved" → re-target the patch.
   - "subtly changed" → adjust the patch.
   - "removed upstream" → escalate to fork: add a `legacy_mv2/` implementation
     of the affected capability, route to it through `DaoMV2APIRouter`.

### `legacy_mv2/README.md` contents

The README is part of the deliverable, not an afterthought. It contains:

- Purpose statement (one paragraph).
- The control-flow diagram (copied from this design doc).
- A current inventory of patches with `Dao MV2 Support` headers, regenerated
  on each Chromium upgrade by a small helper (`scripts/list-mv2-patches.ts`,
  out of scope for this design but a planned follow-up).
- "When to fork" section: a concrete decision tree for whether a removed
  Chromium capability warrants a Dao reimplementation.
- "Sunset policy": a deliberately vague statement that says, in effect, "if
  the cost of supporting MV2 outweighs the benefit, the project may freeze
  Chromium at the last supportable version". No specific version is named —
  this is intentional; pinning a version now would either be wrong or commit
  the project too early.

### Auto-memory update

After implementation, an auto-memory entry is added at:
`/Users/moonrailgun/.claude/projects/-Users-moonrailgun-Develop-dao-browser/memory/project_mv2_long_term_support.md`
to record:
- Why MV2 support exists (calm minimalism: extension power users get the
  extensions they expect)
- The pref-default override mechanism (so future debugging starts in the
  right place)
- Pointer to `legacy_mv2/README.md` as the source of truth.

## Error handling

This design has very few new failure modes because it composes with
Chromium's existing extension system rather than replacing parts of it.
Specific cases:

- **Pref registration race**: `DefaultManifestV2Availability()` is called from
  `RegisterIntegerPref` at profile-pref-registry construction. If
  `FeatureList` is not yet initialized (rare, but possible during very early
  startup), `base::FeatureList::IsEnabled` returns the feature's static
  default (`false`), which gives Dao behavior — the safe default.
- **`.crx` install dialog fails to find manifest_version**: if the extension
  data lacks a manifest_version, the helper returns "no notice" (treats
  unknown as not-MV2). The extension then either installs as MV3 or fails to
  parse — neither path requires the notice.
- **Chromium removes `kManifestV2Availability` pref**: the
  `extension_prefs.cc.patch` will fail to apply on `npm run import`. The
  upgrade workflow surfaces this and the team decides whether to fork.

## Open questions

None. All design choices made during brainstorm:
- Toggle granularity: B (default-on with hidden flag).
- Toggle UI: A (`chrome://flags` only).
- `.crx` install path: B (improved dialog with MV2 note).
- Install dialog scope: B (notice line above permissions; no second-chance
  bypass for the off-state flag).
- Maintenance organization: B (isolated directory + patch-header convention).
- Compat layer: D2 (foundation now, fork reactively).

## Out of scope (for follow-ups)

- `scripts/list-mv2-patches.ts` (auto-generates the patch inventory section
  of the README on each upgrade).
- `chrome://settings` UI for the toggle if user demand emerges.
- Bundled list of recommended/audited MV2 extensions for non-technical users.
- Drag-drop bypass when the flag is off.
- Third-party store integrations.
