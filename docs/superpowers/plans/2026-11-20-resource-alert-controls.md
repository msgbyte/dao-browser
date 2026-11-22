# Resource Alert Controls Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for the settings change. The subsequently approved background behavior is implemented by [the follow-up plan](2026-11-20-resource-background-alerts.md).

**Goal:** Make the default-on Settings > Performance switch actually stop and restart Dao resource monitoring.
**Architecture:** Observe Chromium's existing global performance-alert preference only while sidebar observers exist. One reconciliation method starts or stops the monitor; stopping clears UI state and rejects late samples.
**Tech Stack:** Chromium C++, PrefChangeRegistrar, Settings Lit, GRIT, native browser tests and Node CDP.
**Spec:** docs/superpowers/specs/2026-11-20-resource-alert-controls-design.md

## Global Constraints

- Preserve existing user edits. No git mutations, branches, worktrees, or direct engine edits.
- Compile only with `npm run rebuild`. Keep the already validated thresholds and sampling frequency.
- Reuse the existing default-true Local State preference, preserving any user-selected false value.
- New visible strings use Dao GRIT and hand-authored zh-CN.

## Review Focus

- Disabling clears state in every window, not only the Settings window.
- Late callbacks and suspend/resume cannot restart a disabled monitor.
- Re-enabling requires fresh sustained samples.
- Removing the last UI observer releases preference observation and query subscriptions.
- The Settings control continues to address the existing global preference.

## Task 1: Runtime preference integration

- [x] Add a real browser regression test that raises an alert, disables the existing preference, observes alert removal, rejects late results, and re-enables with fresh history; run it and observe failure.
- [x] Add preference observation and shared start/stop reconciliation to `dao_resource_usage_monitor.{h,cc}`.
- [x] Run the regression and existing resource/toast tests through the dedicated test target.

## Task 2: Settings and validation

- [x] Patch `performance_page.html.ts` to use localized background-resource label and description; register English and zh-CN strings through the existing Settings provider.
- [x] Compile app and tests using `npm run rebuild -- -- -j 4 --target chrome dao_resource_usage_tests`.
- [x] Exercise the actual Settings toggle, persistence and immediate state clearing through the isolated CDP harness.
- [x] Update desktop feature inventory, shared checklist and matching changelog entries; review and leave changes unstaged.

## Progress

- Ruling: implement the explicitly requested switch without another approval round; keep the background notification UX as a design proposal pending the user's preference.
- Ruling: reuse the existing global preference instead of adding a competing Dao-specific switch.

- First regression failed against the old implementation, then all nine native tests passed after preference integration.
- Independent review found pending query observer lists outlive query destruction. A real queued-query regression reproduced callbacks crossing a disable/re-enable cycle; detach both observers before resetting the queries. Both the queued-callback regression and all prior native tests pass (10 total).
- The real Settings test passed live state clearing, timer-deadline suppression, re-enable reset and persisted Local State. Its restart step encountered stale DevTools discovery files; the isolated test harness now clears those files before relaunch.

- Final validation: app and dedicated tests compile; all 10 native tests pass; Lit lint passes (233 files); the real Settings script passes default-on UI, live clearing, disabled behavior beyond the former timer deadline, fresh re-enable, saved Local State, and restart persistence.
- The settings screenshot was inspected with Chinese copy and the enabled control. At completion of this settings-only plan, the background notification choice was still unanswered. The user subsequently approved system notifications and retained reminders; see the follow-up plan above. No git mutations were performed.
