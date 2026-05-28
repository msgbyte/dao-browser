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
                                              │ AND at ExtensionManagement::Refresh fallback
                                              ▼
       ┌──────────────────────────────────────────────────────────────────────┐
       │ pref_names::kManifestV2Availability                                  │
       │  feature OFF (Dao default) → kEnabled (=2)  ← MV2 fully exempted     │
       │  feature ON  (user opt-out) → kDefault (=0) ← Chromium's deprecation │
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

## A second gate: experiment-stage features

The pref pathway above isn't the only thing Chromium consults. Independently,
`ManifestV2ExperimentManager::CalculateCurrentExperimentStage()` reads two
upstream `BASE_FEATURE`s before allowing CRX installs:

- `kExtensionManifestV2Unsupported` — when enabled, stage = `kUnsupported`.
- `kExtensionManifestV2Disabled` — when enabled (and the above is off),
  stage = `kDisableWithReEnable`.

Both ship `ENABLED_BY_DEFAULT` upstream. With either active the experiment
manager silently blocks new MV2 `.crx` installs even when the policy pref
says they should be allowed (the pref controls *exemption*; the experiment
manager controls *the experiment stage itself*).

`extensions/common/extension_features.cc.patch` flips both flags to
`DISABLED_BY_DEFAULT` so the stage stays at `kWarning`. This was discovered
while wiring up `DefaultPolicy_AllowsCRXInstall` in the canary suite.

## Patch inventory

Run `grep -r "Dao MV2 Support" src/patches` from the repo root for an
authoritative list. As of initial implementation it includes:

- `extensions/browser/extension_prefs.cc.patch` — pref-registration default.
- `extensions/common/extension_features.cc.patch` — disables the two MV2
  experiment-stage features so the experiment manager doesn't pre-block CRX
  installs.
- `chrome/browser/extensions/extension_management.cc.patch` — `Refresh()`
  fallback that actually applies the Dao default.
- `chrome/browser/about_flags.cc.patch` — chrome://flags entry.
- `chrome/browser/flag_descriptions.h.patch` — flag strings.
- `chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch` —
  legacy-MV2 notice above the permission list.
- `extensions/browser/BUILD.gn.patch`, `chrome/browser/BUILD.gn.patch`,
  `chrome/browser/extensions/BUILD.gn.patch`,
  `chrome/browser/ui/BUILD.gn.patch`, `chrome/test/BUILD.gn.patch` — wire
  `dao_legacy_mv2` (and the test source_set) into the surrounding source_sets.

A future helper `scripts/list-mv2-patches.ts` will auto-generate this
section on each Chromium upgrade. Out of scope for the initial landing.

## Known limitations

- **Install notice is skipped for zero-permission MV2 extensions.** The
  notice is rendered inside `ExtensionInstallDialogView::CreateExtensionInfoContainer()`,
  which Chromium only calls when the extension has visible permissions or
  a justification block. An MV2 extension with no permissions (rare; mostly
  themes / minimal devtools shims) will install with no banner. Acceptable
  trade-off — the MV2-vs-MV3 distinction matters most for permission-heavy
  extensions like ad blockers and userscript managers, which always trigger
  the container.

## Upgrade workflow

When `dao.json` bumps the Chromium version:

1. Run `npm run import`. Patches that fail to apply are inspected against the
   "Dao MV2 Support" header convention to confirm whether they are MV2-related.
2. Run `npm run test -- --gtest_filter='Dao*MV2*'`. Failures in the canary
   tests indicate which Chromium-side capability changed:
   - **"still works upstream, just code moved"** — re-target the patch.
   - **"subtly changed"** — adjust the patch.
   - **"removed upstream"** — escalate to fork. Add a new `legacy_mv2/`
     implementation of the affected capability, route to it through
     `DaoMV2APIRouter`, and update the corresponding test from a
     pass-through check to a behavior check.

## When to fork (decision tree)

```
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
         Fork. Add the Dao implementation under `legacy_mv2/`,
         add a router method to DaoMV2APIRouter, switch the test
         from pass-through to behavior.
```

## Sunset policy

If maintaining MV2 support requires reimplementing more than two distinct
extension APIs at once, the project will reassess. Options at that point
include (1) accepting the maintenance cost, (2) freezing Dao's Chromium at
the last MV2-capable release, or (3) dropping MV2 support outright. No
specific Chromium version is pinned as the sunset point in advance — that
decision is reactive, not preemptive.

## Files

- `dao_mv2_features.{h,cc}` — the `kRestoreManifestV2Deprecation` feature.
- `dao_mv2_pref_defaults.{h,cc}` — the Dao default for
  `kManifestV2Availability`.
- `dao_mv2_api_router.{h,cc}` — the compat seam, pass-through today.
- `dao_mv2_install_notice.{h,cc}` — install-dialog notice helpers.
- `dao_mv2_browsertest.cc` — the canary regression suite.
- `test_data/` — minimal MV2 extensions for the canary tests.
