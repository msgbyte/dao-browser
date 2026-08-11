# Browser Migration Layout Regression QA

## Source visual truth

- User-reported broken state: `/var/folders/0l/4dc990md3yn_g3b46dtmhp880000gn/T/orca-paste-1786372826229-081b4a4d-c6af-4262-8884-3f09e49acc37.png`
- Source pixels: 1940 x 1610.
- State: dark theme, migration step 2, category selection screen.

## Implementation evidence

- Browser-rendered screenshot: `/var/folders/0l/4dc990md3yn_g3b46dtmhp880000gn/T/orca-computer-use/1c00cbd7-8d01-4d07-b35d-db9c98d4b199-screenshot.png`
- Before/after comparison: `/tmp/dao-import-layout-before-after.png`
- Window viewport: 1398 x 909 CSS pixels at device scale factor 2.
- Implementation pixels: 2796 x 1818.
- State: dark theme, `dao://import`, Google Chrome selected, migration step 2, all supported categories selected.

## Full-view comparison evidence

The pre-fix capture shows the first completed progress segment stretched vertically through the header, making the header consume most of the shell. The post-fix browser capture shows the intended compact header, four horizontal progress segments, the category list inside the scrollable main region, and the persistent footer at the bottom of the shell.

The source and implementation captures use different crops, so the comparison image normalizes both to 900 pixels high while preserving aspect ratio. The comparison is used for the layout regression rather than pixel-level typography matching.

## Focused region comparison evidence

The header and progress rail are clearly readable in the full-view comparison, so a separate crop was not needed. The affected progress segment changed from approximately 390 CSS pixels tall to the intended 4 CSS pixels tall. The header no longer inherits completion-view sizing.

## Required fidelity surfaces

- Fonts and typography: unchanged by the fix; hierarchy, weights, wrapping, and localized copy remain consistent with the existing implementation.
- Spacing and layout rhythm: passed; the header, scrollable main region, and footer return to their intended three-row shell layout.
- Colors and visual tokens: unchanged; the existing dark theme and Dao accent tokens remain intact.
- Image quality and asset fidelity: unchanged; existing browser logos remain sharp and correctly scaled.
- Copy and content: unchanged; localized step, category, authorization tip, and action copy remain present.

## Primary interactions and console checks

- Selected the Google Chrome source and advanced to step 2 in the rebuilt Dao Debug application.
- Confirmed category toggles, password authorization tip, Back action, and Start action remain visible.
- No import WebUI error appeared in debug stderr. Repeated Touch ID keychain entitlement diagnostics are unrelated to this layout change.

## Findings and comparison history

- Iteration 1 — P1 fixed: `.done` was shared by completed progress segments and the completion view. The generic completion selector applied `min-height: 390px` and flex-column sizing to the rail segment.
- Fix: renamed the completion-view class to `.completion`, leaving `.rail span.done` as the progress-state selector.
- Post-fix evidence: the focused regression test reports no 390px minimum height on the completed segment, and the rebuilt browser capture shows a compact horizontal rail.

## Verification

- Focused import WebUI tests: 7 passed.
- Full WebUI suite: 64 files and 776 tests passed.
- Lit reactive-field lint: 205 files scanned, 0 violations.
- `npm run rebuild`: succeeded in 16 incremental steps.
- `git diff --check`: passed.

No actionable P0, P1, or P2 visual findings remain for this regression.

final result: passed
