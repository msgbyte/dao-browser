# Shared Foreground Activity Timer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure the selected HTTP(S) tab's real foreground time once at the Profile level and make Dream daily/weekly reports consume it without overlapping Chromium History durations.

**Architecture:** Add one eager regular-Profile `DaoForegroundActivityService`, backed by one sequenced SQLite store. The service reuses Dao's existing active-target predicate, records monotonic elapsed time into local date/bucket/host aggregates, and exposes one checkpoint-before-query snapshot callback. A small Dream policy helper makes the per-date native/legacy/unavailable decision once for both collectors; the existing report JSON and WebUI remain the persistence and presentation path.

**Tech Stack:** Chromium C++, `KeyedService`, `ProfileBrowserCollection`, tab/widget/WebContents observers, `base::TimeTicks`, device-service timezone notifications, `base::SequenceBound`, Chromium `sql`, Lit/TypeScript, Vitest, browser_tests.

**Spec:** `docs/superpowers/specs/2026-08-31-foreground-activity-timer-design.md`

## Global Constraints

- [ ] Keep canonical changes under `src/dao/`, `src/patches/`, and `docs/`; never edit `engine/` directly.
- [ ] Use only `npm run rebuild` for native compile confirmation. Do not run direct Chromium build tools or `npm run test:build`.
- [ ] Batch native edits so this plan pays for one expected-failing rebuild and one final successful rebuild.
- [ ] Do not run `i18n.sh`; add only English and hand-authored `zh-CN` strings.
- [ ] Do not add a third-party/library dependency, generic observer API, consumer interface, setting, export path, or dashboard.
- [ ] Do not run `git add`, `git commit`, `git push`, or any other state-changing Git command without a fresh user instruction authorizing that exact action.
- [ ] Preserve unrelated worktree changes and inspect intended paths explicitly before handoff.

---

## Task 1: Lock the native contracts with one focused failing test target

**Files:**

- Create: `src/dao/browser/activity/dao_foreground_activity_browsertest.cc`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

- [ ] Add the new test source to `dao_browser_ui_test_sources`.
- [ ] Put plain store/splitting tests and browser lifecycle tests in this one file so no extra test target or fixture layer is introduced.
- [ ] Name every suite with the `DaoForegroundActivity` prefix so one filter runs the complete native contract:

  ```cpp
  TEST(DaoForegroundActivityStoreTest, UpsertsQueriesAndPrunes371Dates);
  TEST(DaoForegroundActivityIntervalTest, SplitsAtLocalBucketAndMidnightBoundaries);
  TEST(DaoForegroundActivityDreamPolicyTest, SelectsExactlyOneSourcePerDate);
  TEST(DaoForegroundActivityDreamPolicyTest, FiltersHostsBeforeRecomputingTotals);
  IN_PROC_BROWSER_TEST_F(DaoForegroundActivityBrowserTest,
                         CountsOnlySelectedHttpTabInForegroundWindow);
  IN_PROC_BROWSER_TEST_F(DaoForegroundActivityBrowserTest,
                         StopsForNavigationMinimizeSuspendAndClose);
  ```

- [ ] Cover these assertions without real-time sleeps:

  - HTTP and HTTPS with a non-empty host count; internal, file, empty, and invalid URLs do not.
  - Only the selected tab of the active, visible, non-minimized window accumulates.
  - Tab selection, Split View focus selection, committed navigation, activation, visibility, minimize, close, suspend, and resume settle exactly once.
  - Injected wall and tick clocks show no double counting and no sleep accumulation.
  - One interval crossing 06:00, 12:00, 18:00, 22:00, and midnight is partitioned with the same total milliseconds.
  - Additive upsert, ordered range query, one-time `tracking_started_at`, invalid/overflow rejection, and retention of today plus 370 previous local dates.
  - Dream source decisions cover legacy, start-day partial, current-day partial, retained full, unavailable, and mixed week; native rows excluded by the existing domain predicate do not remain in totals or buckets.

- [ ] Run the single native red check:

  ```bash
  npm run rebuild
  ```

  Expected: compilation fails because the test includes the not-yet-created activity store/service/policy headers. This confirms the test file is wired into `browser_tests`; do not run another native build until Tasks 2–5 are complete.

## Task 2: Add the aggregate SQLite store

**Files:**

- Create: `src/dao/browser/activity/dao_foreground_activity_store.h`
- Create: `src/dao/browser/activity/dao_foreground_activity_store.cc`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

- [ ] Define only the shared values needed by the store, service, and consumers in the store header:

  ```cpp
  enum class DaoForegroundActivityBucket {
    kMorning = 0,
    kAfternoon = 1,
    kEvening = 2,
    kNight = 3,
  };

  struct DaoForegroundActivityRow {
    std::string local_date;
    DaoForegroundActivityBucket bucket;
    std::string host;
    int64_t foreground_ms = 0;
  };

  struct DaoForegroundActivitySnapshot {
    bool available = false;
    base::Time tracking_started_at;
    std::string retained_from_date;
    std::vector<DaoForegroundActivityRow> rows;
  };
  ```

- [ ] Implement `DaoForegroundActivityStore` as a concrete sequence-bound object, following `DaoHomeProjectStore` and `DaoAgentMemoryStore` for database opening, WAL configuration, transactions, cached statements, and sequence checking.
- [ ] Open `<profile path>/DaoForegroundActivity.db`; create exactly the approved `foreground_activity` and `metadata` tables.
- [ ] Write `tracking_started_at` only after schema initialization succeeds, using Chromium's ISO-8601 time conversion. On reopen, return the existing value unchanged.
- [ ] Use a transaction for each batch. For every row, read the current value, add with `base::CheckedNumeric<int64_t>`, and insert/update only if the result is valid and non-negative. Roll back the whole batch on invalid input, overflow, or SQL failure.
- [ ] Implement one combined `ApplyDeltasAndQuery(start_date, end_date, today, deltas)` operation so a checkpoint write and its query cannot be reordered on the store sequence.
- [ ] Query `local_date >= ? AND local_date <= ?` ordered by date, bucket, then host. Treat malformed rows as operation failure rather than partial data.
- [ ] Prune with lexically ordered `YYYY-MM-DD` keys at successful initialization and local-date rollover, retaining the current date and the preceding 370 local dates. Return the earliest retained key as `retained_from_date`.
- [ ] Add only the store files and existing `//sql` dependency to the source list; do not introduce a schema framework or migration layer for version 1.

## Task 3: Implement the eager Profile foreground service

**Files:**

- Create: `src/dao/browser/activity/dao_foreground_activity_service.h`
- Create: `src/dao/browser/activity/dao_foreground_activity_service.cc`
- Create: `src/dao/browser/activity/dao_foreground_activity_service_factory.h`
- Create: `src/dao/browser/activity/dao_foreground_activity_service_factory.cc`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`
- Modify: `src/patches/chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc.patch`

- [ ] Make the service a concrete `KeyedService` and the smallest set of existing Chromium observers needed for correctness:

  ```cpp
  class DaoForegroundActivityService final
      : public KeyedService,
        public BrowserCollectionObserver,
        public TabStripModelObserver,
        public views::WidgetObserver,
        public content::WebContentsObserver,
        public base::PowerSuspendObserver,
        public device::mojom::TimeZoneMonitorClient {
   public:
    using SnapshotCallback =
        base::OnceCallback<void(DaoForegroundActivitySnapshot)>;

    void GetSnapshot(std::string start_date,
                     std::string end_date,
                     SnapshotCallback callback);
    void Shutdown() override;
  };
  ```

- [ ] Accept `base::Clock` and `base::TickClock` through the constructor with production defaults. This is the only testing seam; do not create clock or store interfaces.
- [ ] Create the store with `base::SequenceBound` on one sequenced `MayBlock`, `USER_VISIBLE`, `BLOCK_SHUTDOWN` task runner, matching Dao's existing profile-store pattern.
- [ ] Observe `ProfileBrowserCollection::GetForProfile(profile)`, each relevant tab strip/widget, the selected `WebContents`, power suspend/resume, and `device::mojom::TimeZoneMonitor` through `content::GetDeviceService()`.
- [ ] Select at most one candidate from the profile's last active browser. Reuse `CanAnimateAgentCursorForTarget()` for selected/active/visible/non-minimized validation, then require matching Profile, a last committed HTTP(S) URL, and non-empty canonical `GURL::host()`.
- [ ] Rely on `DaoSplitView::SetActivePane()` selecting the corresponding tab; do not add Split View-specific timing state or count both panes.
- [ ] Keep one active segment and one pending aggregate map. Every observer callback calls the same reconciliation path:

  ```text
  settle old host using tick-clock elapsed
  split elapsed by projected local wall boundaries
  recompute one eligible host
  reanchor wall and tick starts
  ```

- [ ] Implement the boundary splitter inside the service, not as another class. Split at local midnight, 06:00, 12:00, 18:00, and 22:00; discard non-positive tick deltas; preserve the exact input millisecond total across slices, including 23/25-hour local days.
- [ ] Start a 60-second repeating checkpoint only after successful store initialization. A checkpoint settles, moves the pending map into a batch, writes it, and immediately reanchors an eligible unchanged target.
- [ ] On suspend, settle and clear the segment. On resume and timezone change, reconcile from fresh wall/tick values. On shutdown, stop observers/timer, settle, and submit the final batch before releasing the sequence-bound store.
- [ ] Make `GetSnapshot()` checkpoint first and call the store's combined write/query operation on the same sequence. Return `available=false` on initialization, write, query, parse, or overflow failure; never consult History.
- [ ] Make the factory return `nullptr` for off-the-record contexts and override eager creation:

  ```cpp
  bool ServiceIsCreatedWithBrowserContext() const override { return true; }
  ```

- [ ] Register the factory beside the existing Dao factories in `chrome_browser_main_extra_parts_profiles.cc.patch`.
- [ ] Add the four service/factory files plus explicit device mojom dependency to `dao_ui_sources.gni`; use the existing content/browser dependencies rather than a macOS notification shim.

## Task 4: Resolve Dream foreground data once per local date

**Files:**

- Create: `src/dao/browser/agent/dao_dream_foreground_policy.h`
- Create: `src/dao/browser/agent/dao_dream_foreground_policy.cc`
- Modify: `src/dao/browser/agent/dao_dream_material_collector.h`
- Modify: `src/dao/browser/agent/dao_dream_material_collector.cc`
- Modify: `src/dao/browser/agent/dao_weekly_dream_material_collector.h`
- Modify: `src/dao/browser/agent/dao_weekly_dream_material_collector.cc`
- Modify: `src/dao/browser/agent/dao_dream_browsertest.cc`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

- [ ] Put the one shared decision table in `dao_dream_foreground_policy.*`; do not duplicate it in the two collectors:

  ```cpp
  enum class DreamForegroundSource {
    kDaoActiveTabV1,
    kChromiumHistoryLegacy,
    kMixed,
  };

  enum class DreamForegroundCoverage {
    kFull,
    kPartial,
    kLegacy,
    kMixed,
    kUnavailable,
  };

  struct DreamForegroundDatePolicy {
    DreamForegroundSource source;
    DreamForegroundCoverage coverage;
    bool use_native = false;
    bool use_legacy = false;
    int64_t coverage_seconds = 0;
  };
  ```

- [ ] Resolve each requested local date using `tracking_started_at`, query time, today's local date, `retained_from_date`, and snapshot availability:

  - dates before tracking start use legacy History;
  - the tracking-start date and current unfinished date use native partial data;
  - completed retained dates after start use native full data, where no row means measured zero;
  - pruned native dates and native query/store failures are unavailable;
  - a range containing more than one source/coverage becomes mixed.

  Unavailable is a coverage state, not a fourth persisted source. Keep the
  expected native source (`dao_active_tab_v1`), or `mixed` for a range that
  also contains legacy dates, while setting `foreground_coverage` to
  `unavailable`.

- [ ] Compute `coverage_seconds` from the native calendar-window intersection with `[tracking_started_at, query_time]`; never derive it from active duration.
- [ ] Add the activity snapshot as one parallel input to both collectors. Daily collection can retain its bounded History aggregation; weekly collection retains annotated visits until all parts arrive.
- [ ] Separate History's visit/title/search/coarse-visit-bucket/visit-duration aggregation from foreground selection. Keep the current `ForegroundDurationFor()` fallback only for dates whose policy says `use_legacy`; never let it contribute to native or unavailable dates.
- [ ] For native dates, filter rows with the existing `IsDreamDomainExcluded()` predicate first, then rebuild domain `foreground_seconds`, total `foreground_seconds`, bucket totals, and `duration_level`. Add a bounded domain record with zero visits/empty titles when a measured host has no History visit in the window.
- [ ] For unavailable dates, leave foreground numeric fields empty/zero only as compatibility storage and set explicit unavailable metadata so callers cannot treat them as observations.
- [ ] For weekly material, resolve every visit/native row by its local date before aggregation. Never add native and legacy foreground time for one date. Preserve URL-bearing `local_sources` from History only; native host aggregates must not synthesize URLs.
- [ ] Write these exact keys into the existing `stats`/`material_stats` dictionary used by both report paths, with no Agent Memory schema change:

  ```json
  {
    "foreground_source": "dao_active_tab_v1",
    "foreground_coverage": "partial",
    "coverage_seconds": 12345
  }
  ```

- [ ] Extend focused Dream browser tests to prove:

  - native duration replaces, rather than adds to, overlapping History foreground duration;
  - a completed native date with no rows is full measured zero;
  - partial and unavailable metadata survive report persistence;
  - exclusions are applied before domain/total/bucket recomputation;
  - a weekly range crossing the start boundary reports mixed and sums legacy/native dates once each;
  - existing persisted reports without the keys remain byte-for-byte unchanged when merely read.

## Task 5: Finish the native green cycle

**Files:**

- Verify all native files from Tasks 1–4.

- [ ] Run the only successful native compile confirmation:

  ```bash
  npm run rebuild
  ```

  Expected: import/repair and the debug rebuild complete successfully.

- [ ] Run only the new shared timer contract:

  ```bash
  engine/src/out/dao-debug/browser_tests --gtest_filter='DaoForegroundActivity*'
  ```

  Expected: store, interval, source-policy, and browser lifecycle tests pass.

- [ ] Run only the Dream cases added or changed in Task 4, using their final exact names in one colon-separated filter:

  ```bash
  engine/src/out/dao-debug/browser_tests --gtest_filter='DaoDreamBrowserTest.NativeForeground*:DaoDreamBrowserTest.ForegroundCoverage*:DaoDreamBrowserTest.WeeklyForeground*'
  ```

  Expected: the focused daily and weekly source-selection tests pass. If final test names differ, update this command in the plan before execution rather than broadening to all Dao tests.

## Task 6: Make the Dream runner and WebUI coverage-aware

**Files:**

- Modify: `src/dao/browser/ui/webui/resources/agent/dao_dream_runner.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_runner.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_dream_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`

- [ ] First add focused Vitest assertions for:

  - unavailable coverage tells the runner not to infer zero foreground time or time-of-day habits;
  - partial coverage allows measured values but labels them partial;
  - daily and weekly reports render localized partial/unavailable notes;
  - an old report without metadata renders exactly as before.

- [ ] Run the WebUI red check:

  ```bash
  npx vitest run src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_runner.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts
  ```

  Expected: only the newly added coverage assertions fail.

- [ ] Extend existing TypeScript report/stat types with the three persisted fields. Parse them in the existing `normalizeMaterialStats_()` path and one same-file weekly helper; do not add a state store or parser module.
- [ ] Treat missing metadata as legacy compatibility. Treat `unavailable` as unknown: ignore measured bucket fields for behavior inference and do not manufacture an all-zero rhythm claim.
- [ ] Add one compact coverage note near the daily rhythm and weekly time-pattern sections. Render it even when all numeric buckets are zero, so unavailable is distinguishable from a measured zero day.
- [ ] Add localized Agent WebUI keys in `en.ts` and `zh-CN.ts`, for example English “Foreground time is partially measured” / “Foreground time is unavailable” and natural Chinese equivalents. Do not touch generated vendor or other locale files.
- [ ] Add runner instructions that native full/partial values are the only measured foreground evidence, unavailable means no time conclusion, and mixed weekly evidence must retain its coverage qualifier.
- [ ] Run the focused WebUI green check:

  ```bash
  npx vitest run src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_runner.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_app.test.ts
  ```

  Expected: both files pass.

- [ ] Run the relevant WebUI sweep and Lit field lint:

  ```bash
  npm run test:webui
  npm run lint:lit
  ```

  Expected: both commands pass.

## Task 7: Document the shared feature and perform final consistency checks

**Files:**

- Modify: `docs/features.md`
- Modify: `docs/feature-checklist.md`

- [ ] Update the Dream section in `docs/features.md` to describe the shared regular-Profile timer, selected HTTP(S) active-tab predicate, monotonic/event-driven measurement, host-only SQLite aggregation, 371-date retention, and per-date native/legacy/unavailable source policy.
- [ ] Update `docs/feature-checklist.md` with focused checks for active/background/minimized windows, multiple windows, tab/navigation/Split View selection, suspend/resume, timezone and bucket boundaries, retention, off-the-record absence, and daily/weekly no-double-counting behavior.
- [ ] Do not claim a new setting, dashboard, export, cloud sync, or retroactive rewrite of old reports.
- [ ] Scan the implementation plan for unresolved markers:

  ```bash
  rg -n '\b(TO[D]O|TB[D]|FIXM[E]|XX[X])\b' docs/superpowers/plans/2026-08-31-foreground-activity-timer.md
  ```

  Expected: no output.

- [ ] Check cross-language key/type consistency:

  ```bash
  rg -n 'foreground_source|foreground_coverage|coverage_seconds|DaoForegroundActivitySnapshot' src/dao/browser/activity src/dao/browser/agent src/dao/browser/ui/webui/resources/agent
  ```

  Expected: snake-case JSON keys match exactly across C++ and TypeScript; the snapshot type appears only in native code.

- [ ] Check formatting and inspect only intended changes:

  ```bash
  git diff --check
  git status --short
  git diff -- src/dao src/patches/chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc.patch docs/features.md docs/feature-checklist.md docs/superpowers
  ```

  Expected: no whitespace errors; only the timer, Dream integration, WebUI/i18n, tests, and required documentation are changed. Do not stage or commit.

## Spec Coverage Review

- [ ] Shared ownership: eager regular-Profile service, no off-the-record instance, independent of Dream and Agent Memory.
- [ ] Eligibility: one selected HTTP(S) tab in one active visible non-minimized window; focused Split View pane only; fail closed.
- [ ] Timing: monotonic elapsed time, event-driven settlement, timezone notification, suspend exclusion, 60-second crash-loss ceiling.
- [ ] Storage: local date/bucket/canonical-host milliseconds, dedicated SQLite database, ordered checkpoint-before-query, 371-date retention.
- [ ] Dream: per-date source selection, no native/History overlap, exclusion before recomputation, full/partial/legacy/mixed/unavailable metadata, coverage seconds.
- [ ] Compatibility: existing report storage schema and old report rendering preserved; only newly generated reports use the new policy.
- [ ] Privacy: no URL/path/query/title/content in the activity database, no incognito capture, no new network request.
- [ ] Delivery: focused native/WebUI tests, required feature inventory/checklist updates, no automatic translation, no unauthorized Git mutation.
