# Resource Usage Alerts Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Detect sustained high resource usage with measured low overhead and persistent dismissible feedback.
**Architecture:** A shared C++ monitor consumes Chromium resource attribution samples, applies a small per-page state machine, and publishes changes to sidebar handlers. DaoToastView retains a persistent reminder beneath transient feedback.
**Tech Stack:** Chromium C++, Lit, GRIT, Vitest, Node CDP and macOS process metrics.
**Spec:** docs/superpowers/specs/2026-11-20-resource-usage-alerts-design.md

## Global Constraints

- Work on main without state-changing git commands, worktrees, or direct engine edits.
- Compile only through npm run rebuild; batch native source and test edits.
- All new UI copy uses GRIT, with hand-authored English and zh-CN.
- CPU 30 s / 80% / 90 s, recovery 40% / 60 s; memory 120 s / two fresh readings / 2 GiB or 1 GiB on <=8 GiB machines.
- Persistent native reminders use DaoToastView; foreground tabs only show markers.

## Review Focus

- Navigation and missing measurements cannot retain another document's usage.
- Shared processes and fresh sample timestamps must not count cached data twice.
- Closing or moving tabs/windows cannot leave callbacks targeting destroyed views.
- Transient feedback must restore a retained reminder without resurrecting one dismissed by the user.
- Sampling and UI updates must remain bounded with 100 tabs and many renderer processes.

### Task 1: Detection and sidebar state

Files: new src/dao/browser/performance/dao_resource_usage_monitor.{h,cc}, dao_resource_usage_state.{h,cc}, dao_resource_usage_unittest.cc; src/dao/browser/ui/dao_ui_sources.gni; dao_sidebar_ui.{h,cc}; sidebar_bridge.ts; dao_tab_item.ts and its test; GRIT and zh-CN.
- [x] Add marker regression test, run npm run test:webui -- <tab item test> and observe failure.
- [x] Implement sustained-usage state, passive queries with global fallback cadence, bounded page state and state-change callbacks.
- [x] Add state-machine tests for sustained load, recovery, stale/duplicate timestamps and resets.
- [x] Wire static accessible markers with localized reasons and no per-sample UI refresh.

### Task 2: Persistent shared reminder

Files: dao_toast_view.{h,cc}, dao_resource_usage_unittest.cc, dao_sidebar_ui.{h,cc}.
- [x] Add retained notification, explicit close and repeatable view-tab action beneath transient feedback.
- [x] Connect per-window eligible background pages and per-episode dismissal.
- [x] Test persistence, replacement, restoration, dismissal and eligibility.

### Task 3: Runtime validation and inventory

Files: scripts/checks/resource-usage.mjs; docs/features.md; docs/feature-checklist.md; docs/changelog/{en,zh-CN}.md; measurement report under docs/superpowers/specs/.
- [x] Run npm run rebuild -- -- -j 4 --target chrome dao_resource_usage_tests, narrow native tests, npm run test:webui and npm run lint:lit.
- [x] Exercise real CPU load, foreground/background behavior, persistent reminder and dismissal in an isolated browser profile.
- [x] Compare enabled/disabled process-tree CPU and RSS on 100 idle tabs; record wakeup data if obtainable and measurement limits.
- [x] Update feature inventories and matched changelog entries, review final diff, leave git state unstaged.

## Progress

- Design approved in conversation; persistent reminder amendment incorporated. Native execution continues within the user's authorization. No commits or worktrees.
- Initial marker test failed because the marker was absent, then passed (14 tests). Pinned-grid warning coverage also added.
- App compiled successfully with `npm run rebuild -- -- -j 4 --target chrome`.
- Full WebUI sweep passed with Python 3.12 and two workers: 1394 passed, 7 skipped. Initial run exposed a missing localization mock (fixed), Python 3.9 lacking hashlib.file_digest (used existing Python 3.12), and an overloaded parallel test timeout (narrow rerun and bounded full run passed).
- Independent review found asynchronous sample jitter and muted-video eligibility gaps. Added bounded timing tolerance and media/capture checks; scoped review confirmed CPU fix and identified preexisting silent playback on sidebar reconnection, covered with WebContents' current video count.
- Reused the repository's dedicated browser-test target pattern in performance/BUILD.gn. All eight native tests passed, including actual widget painting. The Views fixture uses SetUpForInteractiveTests for GL and a test-only localized string override.
- Real rendering exposed a transparent-layer text assertion in the dismiss button. Added a painting regression test, observed the failure, disabled subpixel rendering for that button, and confirmed the test passes.
- Real-browser functional checks passed: sustained CPU marker, foreground suppression, background persistence across a sample, explicit dismissal surviving the next sample, marker retention after dismissal, and automatic recovery. Native button selection is independent of the macOS UI locale.
- Final app and dedicated test targets compile successfully. The real-load functional test and two reversed-order A/B pairs with 100 idle tabs completed; all isolated browser processes were stopped.
- A/B result: no stable CPU increase was resolved (approximately +0.00087 percentage points after excluding the interval with process churn). Main RSS averaged +4.19 MiB, main footprint +10.86 MiB; the strict 5 MiB memory target remains unvalidated. Results, limits, and raw samples are saved beside the design spec.
- Feature inventories and matching English/zh-CN desktop changelog entries are updated. Git remains unstaged.
