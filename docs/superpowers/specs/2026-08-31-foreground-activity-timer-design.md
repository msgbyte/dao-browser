# Foreground Activity Timer — Design

**Date:** 2026-08-31
**Status:** Approved
**Platform:** Desktop Chromium (macOS arm64 first)

## Summary

Dao will add a profile-scoped native service that measures how long the
selected HTTP(S) tab is genuinely in the foreground. The service is shared
browser infrastructure: it starts for every regular Profile, does not depend
on Dream Analysis or Agent Memory, and never runs for an off-the-record
Profile.

The first consumer is Dream Analysis. Dream will continue to use Chromium
History for visits, titles, and search queries, but will use the new service as
the only foreground-duration source for dates covered by native tracking. This
removes the current unsafe fallback from missing History foreground duration
to `visit_duration`, which can overlap visits and produce totals such as more
than 24 hours in one ordinary day.

## Goals

- Measure actual elapsed foreground time without per-second polling.
- Count reading and video playback while the browser remains foreground, even
  when there is no keyboard or pointer input.
- Count only the selected tab in the single active Dao browser window, and
  only when its last committed URL is HTTP or HTTPS.
- Persist privacy-minimized aggregates by local date, time bucket, and host.
- Make the data available to any future local Dao feature through one small
  range-snapshot API.
- Give Dream an explicit, non-overlapping native/legacy source policy.
- Retain 371 local dates of aggregate detail.

## Non-Goals

- Per-URL, page-title, path, query-string, or content tracking.
- Keyboard/mouse idle detection or engagement scoring.
- Counting background audio/video or multiple visible split panes.
- Incognito tracking, cloud sync, export, settings, or a standalone activity
  dashboard.
- A generic observer/plugin framework for hypothetical consumers.
- Rewriting previously persisted Dream reports automatically.

## Decisions Made During Brainstorming

| Question | Decision |
|---|---|
| Infrastructure ownership | Shared native browser service, not Dream-owned |
| Profile lifecycle | Eager for regular Profiles; disabled for off-the-record Profiles |
| Eligible schemes | HTTP and HTTPS only |
| Activity definition | Selected tab in the active, visible, non-minimized Dao window |
| Input idle | Does not stop counting; reading and foreground video still count |
| Measurement | Event-driven monotonic clock; no one-second polling |
| Persistence | Dedicated profile-local SQLite database |
| Stored granularity | Local date x time bucket x canonical host |
| Time buckets | Morning 06:00–12:00; afternoon 12:00–18:00; evening 18:00–22:00; night 22:00–06:00 |
| Retention | 371 local dates including the current date |
| Crash-loss ceiling | Up to approximately 60 seconds |
| Dream native coverage | Native only; never add History duration for the same date |
| Tracking start date | Native measured data, explicitly marked partial |
| Dates before tracking start | Existing History duration behavior as a legacy fallback |
| Native query failure | Mark unavailable; do not silently substitute History duration |

## Current Problem

Both daily and weekly Dream collectors currently read
`AnnotatedVisit.context_annotations.total_foreground_duration`. When that
value is negative or unavailable, they fall back to
`VisitRow.visit_duration`. Visit duration is the lifetime between visit
transitions, not proof that the page stayed selected in a foreground browser
window. Several visits can therefore overlap and inflate per-domain, bucket,
and daily totals.

The Dream WebUI only formats the persisted `foreground_seconds` and
`foreground_seconds_by_bucket` values. It is not the source of the inflated
time. The correction must happen at the shared measurement/source boundary.

## Architecture

```text
Browser/window/tab/navigation/power events
                    |
                    v
       DaoForegroundActivityService
       - selects at most one eligible target
       - measures with base::TimeTicks
       - splits by local date and bucket
       - checkpoints about every 60 seconds
                    |
                    v
        DaoForegroundActivityStore
        DaoForegroundActivity.db
        (date, bucket, host, elapsed_ms)
                    |
                    v
       range snapshot + coverage metadata
                    |
          +---------+---------+
          |                   |
          v                   v
  daily Dream collector   weekly Dream collector
          |                   |
          +---------+---------+
                    v
      existing Dream report persistence/WebUI
```

### Components

| Component | Responsibility |
|---|---|
| `DaoForegroundActivityService` | Profile-scoped UI-thread state machine, event observation, eligibility, monotonic measurement, segment splitting, checkpointing, and queries |
| `DaoForegroundActivityServiceFactory` | Creates the service eagerly for regular Profiles and rejects off-the-record Profiles |
| `DaoForegroundActivityStore` | Opens the dedicated SQLite database on a sequenced background runner, upserts aggregates, stores tracking metadata, queries ranges, and prunes retention |
| Dream foreground resolver | Small shared helper used by daily and weekly collectors to select native, legacy, mixed, or unavailable foreground data without double counting |

These are concrete Chromium ownership boundaries, not extension points. There
is no interface with multiple implementations and no consumer observer API in
the first version.

## Foreground Eligibility

At any instant the service has either zero or one eligible target. A target is
eligible only when all of the following are true:

1. The Profile is regular, not off the record.
2. Dao is not suspended and the application has an active browser window.
3. The owning `BrowserView` is active, visible, and not minimized.
4. The `WebContents` is the selected tab for that browser window.
5. In Split View, it is the interaction-focused/selected pane rather than every
   simultaneously visible pane.
6. The last committed URL passes `GURL::SchemeIsHTTPOrHTTPS()`.
7. The canonical `GURL::host()` is non-empty.

The existing Dao active-window predicate is reused rather than introducing a
second definition. If window state is ambiguous, the service counts nothing.
This fail-closed rule prevents two windows or split panes from accumulating
more than one unit of elapsed time simultaneously.

The stored host is GURL's canonical host, without scheme, credentials, port,
path, query, fragment, URL, or title. Existing Dream domain exclusion rules are
not applied during capture because the store is shared infrastructure. A
consumer such as Dream applies its own exclusions when reading the rows.

## Timing State Machine

The service reacts to existing Chromium browser-list, tab-strip,
`WebContents`, widget activation/visibility, power suspend/resume, and shutdown
events. Every relevant event calls one reconciliation operation:

```text
reconcile(now_wall, now_ticks):
  settle the previous eligible target through now_ticks
  recompute the one current eligible target
  start a new segment if a target is eligible
```

An active segment stores:

- canonical host;
- wall-clock start, used only for local date/bucket attribution;
- monotonic start (`base::TimeTicks`), used for elapsed duration.

Elapsed time is always `now_ticks - start_ticks`. System clock adjustments can
change where future elapsed time is labeled, but cannot create elapsed time.
Each transition or checkpoint reanchors the next segment to the current wall
clock. A timezone-change notification settles immediately. Previously
persisted rows are not rewritten.

### Segment splitting

Settlement projects the monotonic elapsed interval from its wall-clock start
and splits it at every crossed boundary:

- local midnight;
- 06:00;
- 12:00;
- 18:00;
- 22:00.

Each slice adds milliseconds to exactly one `(local_date, bucket, host)` row.
Storing milliseconds avoids losing many short foreground segments; Dream
converts aggregates to whole seconds only at its material/report boundary.

Local days can be 23 or 25 hours around daylight-saving transitions. The
invariant is no duplicate counting, not a hard 24-hour cap.

### Events that settle or restart a segment

- active browser window changes;
- window activation, visibility, or minimized state changes;
- selected tab or focused Split View pane changes;
- the selected `WebContents` commits a navigation, including host or scheme
  changes;
- the selected tab/window closes;
- system suspend starts;
- system resume completes;
- local timezone changes;
- the periodic persistence checkpoint fires;
- a snapshot query starts;
- profile/browser shutdown starts.

No user-input event is required. A foreground HTTP(S) tab continues counting
while the user reads or watches a video without touching the computer.

### Suspend and shutdown

The service settles before suspend and has no active segment while suspended.
It reconciles after resume, so sleep time is never added. Normal shutdown
settles and submits a final store write. The background store uses the same
sequenced shutdown-safe pattern already used by Dao profile stores.

## Persistence

The database lives under the regular Profile path as
`DaoForegroundActivity.db`. It is separate from Agent Memory so capture does
not inherit Dream or memory feature gates.

### Schema

```sql
CREATE TABLE IF NOT EXISTS foreground_activity (
  local_date TEXT NOT NULL,       -- YYYY-MM-DD in the local timezone at capture
  bucket INTEGER NOT NULL,        -- 0 morning, 1 afternoon, 2 evening, 3 night
  host TEXT NOT NULL,
  foreground_ms INTEGER NOT NULL CHECK (foreground_ms >= 0),
  PRIMARY KEY (local_date, bucket, host)
);

CREATE TABLE IF NOT EXISTS metadata (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
```

`tracking_started_at` is written once, after the database is initialized
successfully. This timestamp is the source boundary used by consumers. A new
database starts at schema version 1; no migration from History is performed.

Upserts add elapsed milliseconds atomically to the composite key. The UI-thread
service holds only the current pending aggregate batch. Approximately every 60
seconds it settles the current segment, sends the batch to the store sequence,
and immediately resumes measurement if the same target is still eligible.
Normal transitions can accumulate into the same batch.

A snapshot query checkpoints first and serializes its upsert before the range
query on the same store sequence. Therefore a report sees activity through the
query time rather than only the last periodic flush.

The store keeps the current local date and the preceding 370 local dates.
Pruning runs at successful initialization and after a local-date rollover, not
on every write. A native-covered date that has already been pruned is
`unavailable`, not zero and not legacy History.

An abrupt process crash can lose the current unflushed interval, bounded to
approximately 60 seconds. No attempt is made to reconstruct that time from
History because doing so would reintroduce overlapping duration.

## Shared Query Contract

The first version exposes one asynchronous local-date range snapshot. It
returns:

- initialization/query status;
- `tracking_started_at`;
- retained range boundary;
- aggregate rows containing local date, bucket, host, and foreground
  milliseconds.

The API does not expose URLs, titles, live observers, exports, or Dream types.
It checkpoints the active segment before querying. Future consumers can apply
their own domain filtering and presentation without changing capture.

## Dream Integration

Daily and weekly Dream collection keeps History queries for:

- visit counts;
- page titles;
- coarse visit-time buckets;
- search query extraction;
- total visit duration where that existing non-foreground statistic is still
  required.

Foreground time is resolved independently per local date.

### Per-date source selection

| Date state | Foreground source | Coverage | Behavior |
|---|---|---|---|
| Before `tracking_started_at` | `chromium_history_legacy` | `legacy` | Keep the existing History foreground/visit-duration behavior |
| Tracking start date | `dao_active_tab_v1` | `partial` | Use only measured native rows from the start timestamp |
| Current unfinished date | `dao_active_tab_v1` | `partial` | Use only native rows through query time |
| Completed retained date after start | `dao_active_tab_v1` | `full` | Use only native rows; no rows is a valid zero |
| Pruned native date or store/query failure | no duration source | `unavailable` | Do not substitute History foreground duration |

The resolver never adds native and History foreground duration for the same
date. A weekly range can combine legacy dates before tracking started with
native dates after it started; its summary source/coverage is `mixed`.

For native rows, Dream applies the existing excluded-domain predicate and then
recomputes:

- total `foreground_seconds`;
- per-domain `foreground_seconds`;
- `foreground_seconds_by_bucket`;
- duration-level labels derived from the filtered foreground time.

This ordering ensures excluded hosts cannot remain in the total or bucket
figures.

### Persisted report metadata

Newly generated daily and weekly reports include:

- `foreground_source`: `dao_active_tab_v1`,
  `chromium_history_legacy`, or `mixed`;
- `foreground_coverage`: `full`, `partial`, `legacy`, `mixed`, or
  `unavailable`;
- `coverage_seconds`: elapsed wall-clock coverage represented by the native
  portion of the requested local-date range, not active browsing seconds.

Existing numeric foreground fields remain for compatibility. When coverage is
`unavailable`, they must not be interpreted as a measured zero: the runner is
told not to infer time-based behavior, and the WebUI marks the time data as
unavailable. Partial reports display one localized “partial statistics” note.

Previously persisted reports are read as before and are not rewritten. A
manual rerun or a newly generated report uses the new source policy.

## Failure Handling

- Database initialization failure leaves the service unavailable. It does not
  fall back to Preferences, Agent Memory, or History.
- Write/query failure is surfaced in the snapshot status and produces
  `foreground_coverage=unavailable` for native-required dates.
- An invalid/empty/non-HTTP(S) target stops the current segment.
- Duplicate or out-of-order UI events remain safe because every event first
  settles one current segment and then recomputes one target.
- Negative monotonic deltas are discarded.
- Invalid database rows or integer overflow fail the operation rather than
  wrapping duration.
- Dream can still generate a report from non-time History and Agent signals
  when foreground timing is unavailable, but it must not describe zero time as
  observed behavior.

## Privacy

- Local-only SQLite data under the regular Profile.
- No off-the-record service or database.
- No full URL, title, path, query, fragment, page content, or interaction log.
- Only canonical host and coarse aggregate time are retained.
- No network request is introduced by the timer.
- Dream retains its existing privacy boundary: only filtered aggregate
  material is eligible to enter its LLM prompt.

## Testing

Keep verification focused on the new contracts:

1. A small timing test with controllable wall and monotonic clocks covers
   HTTP(S) eligibility, active/inactive transitions, no double counting,
   midnight and all four bucket boundaries, and suspend/resume.
2. A store test covers additive upsert, range query, start metadata, and
   371-date pruning.
3. Focused Dream collector tests cover native full, native partial, legacy,
   unavailable, excluded-domain recomputation, and a weekly range crossing the
   tracking boundary.
4. A focused WebUI test covers partial/unavailable presentation and existing
   reports without the new metadata.

Compile confirmation uses only `npm run rebuild`. Relevant WebUI changes use
`npm run test:webui` and `npm run lint:lit`. Browser/native tests run only the
new focused filter; the full Dao test sweep is unnecessary unless focused
verification reveals cross-cutting failures.

## Documentation and Internationalization

- Update `docs/features.md` with the shared foreground service, eligibility,
  privacy, retention, and Dream source-selection behavior.
- Update `docs/feature-checklist.md` with active/background/minimized,
  navigation, multi-window, Split View, suspend/resume, bucket-boundary,
  retention, and native/legacy Dream regression checks.
- Add English source text and the hand-authored `zh-CN` tone reference for the
  partial/unavailable indicators through the existing Agent WebUI i18n system.
- Do not run `i18n.sh`; other locale generation remains a manual user action.

## Rejected Alternatives

### Per-second polling

Simple to picture, but delayed tasks, suspend, and process load make tick counts
an unreliable clock. Event-driven state plus `base::TimeTicks` is both more
accurate and less work while idle.

### Chromium History duration only

History does not provide the required selected-tab/window foreground contract,
and `visit_duration` can overlap. It remains only as an explicit compatibility
source for dates before native tracking existed.

### Preferences storage

An ever-growing date/host JSON dictionary is the wrong shape for Preferences,
has poor range/pruning behavior, and risks large synchronous rewrites.

### Agent Memory database

It would incorrectly couple browser-wide capture to Agent Memory and Dream
feature gates. A dedicated profile-local database keeps ownership and failure
domains clear.

### URL-level event log

It would store more sensitive data, require more retention and compaction code,
and provide no value for the approved aggregate use case.

## Implementation Boundary

The first implementation ends at:

- one eagerly created regular-Profile timer service;
- one aggregate SQLite store;
- one local-date range snapshot API;
- daily/weekly Dream integration and coverage metadata;
- one minimal partial/unavailable UI treatment;
- focused tests and the required feature documentation.

Settings, dashboards, export, live observers, configurable buckets, and
additional consumers are deferred until a real requirement exists.
