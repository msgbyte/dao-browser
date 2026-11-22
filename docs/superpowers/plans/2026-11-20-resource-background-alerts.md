# Background Resource Alert Implementation Plan

**Goal:** Notify once while Dao is inactive and retain a dismissible, aggregated reminder inside Dao.
**Spec:** docs/superpowers/specs/2026-11-20-resource-alert-controls-design.md
**Execution:** Inline using superpowers:executing-plans; one final independent review.

## Constraints

Work in the primary checkout without git mutations or direct engine edits. Compile only through `npm run rebuild`. Preserve the default-on global setting, thresholds, passive queries and single fallback timer. Use localized strings and the existing Dao toast and Chromium notification services. Preserve media/capture/high-performance exclusions and profile isolation.

## Tasks

- [x] Add browser regressions for inactive selected tabs, foreground suppression, retained reminders, aggregation, notification deduplication and disable cleanup; observe failure.
- [x] Extend the global monitor with browser activation observation and retained per-page incidents. Group reminders per profile, send one generic system notification per incident when all windows are inactive, and expose safe inspect/dismiss actions.
- [x] Render one persistent summary in the profile's active or most recently active window; reuse existing media state and Task Manager. Localize new copy.
- [x] Compile and run focused browser/state/toast tests, exercise settings and persistent reminders, update inventories and changelogs, and complete one independent review.

## Progress and rulings

- The user approved one system notification plus a retained in-browser reminder. No further permission is needed.
- Reuse collection activation/deactivation and page visibility events; no additional polling timer.
- Group individually high-usage tabs; do not change detection into summed usage across healthy tabs.
- Keep OS dismissal separate from the retained in-browser reminder; either explicit inspect or in-browser dismissal acknowledges the current incident.

- Initial minimized-window regression failed against the previous implementation (no system notification).
- Native click regression exposed that Show does not restore minimized macOS windows; explicit Restore fixes it.
- Headless macOS gives multiple windows key status concurrently. Activation-event tests explicitly deliver native activation/deactivation events; an attempted interactive run could not acquire OS focus in this session and timed out. Do not claim interactive activation validation.
- Independent review found that macOS rejects TRANSIENT notifications at the native bridge, and retained hidden task pages could be exposed by inspection. Both new regressions failed; the fix uses a dedicated native-supported notification type with a registered handler and rechecks hidden-window eligibility before inspection.
- Ruling: ordinary resource reminders exclude hidden Agent Space task windows so inspection cannot bypass task window management. No Agent Space service edits are required.

- Final verification: app and dedicated tests compile; all 20 native tests pass; Lit lint passes (233 files); the actual Settings flow passes default-on, live clearing, disabled behavior, fresh re-enable and restart persistence. The Chinese setting screenshot was inspected.
- A final 100-tab off/on sample completed without process churn: tree CPU +0.007348 percentage points of one core, CPU-provider queries 3 to 5, memory-provider queries unchanged at 5. Memory varied and wakeups rose; strict memory and power bounds are not established. See the validation report and raw follow-up sample.
- Feature inventory, checklist, matching English/Chinese changelog and design records are updated. Changes remain unstaged; no git mutations were performed.


## Review follow-up — 2026-11-22

- Fixed all three confirmed review findings: notification incidents held open by
  excluded pages, overlap with Chromium transient toasts, and long-title overflow.
- Preserved one notification through temporary media suppression and restored
  reminders only after the Chromium toast queue clears. Mini Dao now keeps the
  entry animation inside the content area as well.
- Three new browser regressions failed before fixes. Final compilation and all
  23 focused tests passed; inventories, English/Chinese changelog, and the
  validation report were updated. No new queries or timers, new benchmark run,
  direct engine edits, or state-changing git commands.
