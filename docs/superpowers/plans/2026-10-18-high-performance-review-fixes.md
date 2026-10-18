# High Performance Mode Review Fixes

> **For agentic workers:** Use the reviewed task boundaries below; coordinate imports and builds in the primary agent.

**Goal:** Fix the accepted review findings without changing the tab-level UI or weakening discard protection.

**Architecture:** Keep explicit Viz exemptions separate from capture. Resolve effective mode from the outermost WebContents and refresh nested contents when the mode or embedding changes. Subscribe native UI directly to mode changes.

**Tech Stack:** Chromium C++, Views, Viz, canonical unified patches.

**Spec:** The user's accepted twelve-point review and `docs/features.md`, section 5.

## Global Constraints

- Work in the primary checkout; no state-changing Git commands.
- Edit canonical Dao sources and patches, never generated engine files directly.
- Compile only with `npm run rebuild`; batch imports and compilation centrally.
- Preserve lifecycle and eligibility discard guards; remove only the redundant Freezer guard.
- Keep English and Simplified Chinese changelog entries aligned.

## Review Focus

- Capture without explicit exemption retains the undrawn-frame limit.
- Removing an explicit exemption while capture remains restores that limit.
- Fenced frames inherit priority, including frames created after enabling.
- Inner WebContents inherit visibility and priority on attachment, navigation, mode changes, and detachment.
- Switching tabs and externally toggling mode update native UI immediately.

## Tasks

- [x] Add browser regressions for fenced frames, inner WebContents, and synchronous indicator updates; add Viz capture/exemption regressions while preserving the upstream responsiveness scenario.
- [x] Build the focused test targets and confirm the new regressions fail for the reviewed reasons.
- [x] Separate explicit undrawn-frame exemption from capture in Viz and cover propagation/reset.
- [x] Resolve effective mode through outer WebContents, refresh embedding transitions, and cover fenced frames without activating prerender/BFCache pages.
- [x] Replace generic UI invalidation with subscriptions; avoid redundant menu paints and icon reconstruction.
- [x] Repair the mojom patch header, remove whitespace-only patch hunks and the redundant Freezer patch, and consolidate pending changelog notes.
- [x] Update the desktop inventory and regression checklist.
- [x] Run `npm run rebuild -- -- -j 6 --target chrome browser_tests viz_unittests`, focused browser/Viz tests, `npm run docs:check`, and `git diff --check`.
- [x] Review the integrated change and report verified results and remaining limits.

## Execution Notes

- The user already authorized these fixes; no additional design approval is needed.
- The UI and Viz tasks have disjoint file ownership and run in parallel; the primary agent owns WebContents integration, browser tests, documentation, and builds.

- RED: capture regressions failed in 12 parameterized cases; inner visibility/priority, synchronous UI, and existing/new fenced-frame priority regressions failed for the expected reasons.
- GREEN: all 25 focused Viz tests passed, including the unchanged upstream responsiveness test.
- RED: MPArch guest priority failed before and after navigation. The fixture uses an independent WebContents with real GuestViewBase registration because Chromium page-load metrics does not yet support the experimental guest frame type.
- RED: all four shared-descendant Viz cases failed. Reapplying explicit exemptions from every protected root fixes inheritance across multiple parent chains.
- Integrated source review found no further actionable defects after the guest and shared-descendant corrections.

- Final verification: `npm run rebuild -- -- -j 6 --target chrome browser_tests viz_unittests` passed; all 10 browser regressions and 29 focused Viz tests passed with retries disabled.
- Documentation checks and `git diff --check` passed. Canonical WebContents/Viz patches and Dao core/UI/test sources match the imported engine; every patch has a Git header, and Freezer matches upstream.
- Retained necessary discard guards and the existing safe menu-button cast; the latter is a style suggestion rather than a behavior defect. Long-running cloud gaming and OS-level resource pressure were not tested.
