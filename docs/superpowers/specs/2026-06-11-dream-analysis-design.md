# Dream Analysis System — Design

**Date:** 2026-06-11
**Status:** Approved design, pending implementation plan

## Summary

A nightly batch process ("dreaming") that collects the user's daily browsing
signals, asks the user-configured LLM to summarize behavior patterns, writes
learned habits into the existing long-term memory (`preferences` /
`episodes`), and presents a human-readable morning report inside the Agent
panel where the user can confirm or reject each learned habit.

Off by default. Triggered automatically at night when the system is idle, by
a daytime catch-up path, or manually from Agent settings.

## Decisions Made During Brainstorming

| Question | Decision |
|---|---|
| Output form | **B** — background learning + morning report (no standalone dream-diary UI) |
| Material sources | **A+B+C+D** — browsing history, agent conversations, action feedback, search keywords |
| Missed nights | **B** — daytime catch-up run (no persistent background agent) |
| Night window | 22:00–06:00 local time, constant, not user-configurable |
| Domain filtering | **A** — no sensitive-category blocklist in v1; mitigated by off-by-default + explicit disclosure at the toggle |
| Privacy granularity (hard rule) | Only `domain + title + time bucket / count` leaves the machine. Full URLs never enter the material pack. Search queries are extracted in C++, then the source URL is discarded. |
| Report surface | **A** — card embedded at the top of the Agent panel chat view |
| Manual trigger | **i** — button in the Agent settings section |

## Architecture (Approach 1: C++ orchestration + resident Agent WebUI for LLM)

```
                    ┌─────────────────────────────────────────────┐
                    │ DaoDreamService (new, profile KeyedService) │
                    │                                             │
  ui::CalculateIdleTime ──► DreamScheduler (trigger decision)     │
                    │         │                                   │
  HistoryService ─────────► DreamMaterialCollector (collect +     │
  DaoAgentMemoryService ──►   sanitize)                           │
                    │         ▼                                   │
                    │   material pack (base::Value::Dict)         │
                    └─────────┬────────────────▲──────────────────┘
                              │ FireWebUIListener │ RegisterMessageCallback
                              ▼                 │
                    ┌─────────────────────────┴───────────────────┐
                    │ agent WebUI (resident, preloaded ~3s after  │
                    │ startup in hidden WebView)                  │
                    │   dao_dream_runner.ts (new)                 │
                    │   └─ reuses pi_llm_stream + llm_config      │
                    └─────────────────────────────────────────────┘
                              │ structured result (habits + report md)
                              ▼
          preferences / episodes (existing) + dream_reports (new table)
```

Rationale: all LLM infrastructure (provider configs, API keys in WebUI
localStorage, streaming client) already lives in the agent WebUI, which is
preloaded and resident even when the panel is closed. C++ does what it is
good at: scheduling, system idle detection, HistoryService, SQLite.

Rejected alternatives:
- **Pure WebUI**: WebUI cannot see system-level idle; scheduling inside a
  reloadable page is unreliable for an unattended nightly job.
- **Pure C++ (own LLM client)**: would duplicate provider adapters for 7
  providers and dig API keys out of WebUI localStorage.

## Components

| Component | Location | Responsibility |
|---|---|---|
| `DaoDreamService` (+ Factory) | `src/dao/browser/agent/dao_dream_service.{h,cc}` | KeyedService shell; owns scheduler + collector; orchestrates the dream pipeline; state machine |
| `DreamScheduler` | inner class of the service | 5-minute `base::RepeatingTimer` poll; nightly / catch-up / manual trigger decisions |
| `DreamMaterialCollector` | `src/dao/browser/agent/dao_dream_material_collector.{h,cc}` | 4 parallel queries, sanitization, aggregation into one Dict |
| `dao_dream_runner.ts` | `src/dao/browser/ui/webui/resources/agent/` | listens for `dream-run`, builds the prompt, calls `pi_llm_stream`, parses/validates JSON, returns via `chrome.send('dreamComplete', …)` |
| `dream_reports` table | added in `dao_agent_memory_store.cc` | report archive (schema below) |
| Dream card + settings | `dao_chat_view.ts` / `dao_settings_view.ts` | morning report card, habit confirm/reject, toggle + "Dream now" button |

### Pref

- New `kDaoDreamEnabled` (`dao.dream_enabled`), boolean, **default false**, in
  `dao_pref_names.{h,cc}`.
- New `kDaoDreamDebug` (`dao.dream_debug`), boolean, **default false** — when
  true, dream runs persist their material pack JSON for inspection (debug
  section on the dream card).
- Gated twice: if `kDaoAgentMemoryEnabled` is false the dream service is not
  created at all; if `kDaoDreamEnabled` is false the scheduler never fires.

### `dream_reports` schema

```sql
CREATE TABLE IF NOT EXISTS dream_reports (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dream_date TEXT NOT NULL,          -- "YYYY-MM-DD" (dream day attribution)
  report_markdown TEXT NOT NULL DEFAULT '',
  habit_candidates TEXT NOT NULL DEFAULT '[]',   -- JSON array
  material_stats TEXT NOT NULL DEFAULT '{}',     -- JSON: counts per source
  status TEXT NOT NULL,              -- 'completed' | 'failed'
  attempt_count INTEGER NOT NULL DEFAULT 0,
  trigger_kind TEXT NOT NULL,        -- 'nightly' | 'catchup' | 'manual'
  debug_material_json TEXT NOT NULL DEFAULT '',  -- material pack JSON when debug mode was on
  viewed_at INTEGER,                 -- NULL = unread
  created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_dream_reports_date ON dream_reports(dream_date);
```

No FTS on this table (plain queries only; see the FTS5 test-poisoning issue
in project memory).

## Trigger Logic and State Machine

### Scheduler tick (every 5 minutes)

```
tick():
  if (!kDaoDreamEnabled || !kDaoAgentMemoryEnabled) return;
  if (state_ != kIdle) return;

  // Path 1: nightly dream
  if (IsNightTime()                      // local 22:00–06:00
      && ui::CalculateIdleTime() >= 1h
      && !HasDreamForDate(TargetDate()))
    StartDream(TargetDate(), kNightly);

  // Path 2: daytime catch-up
  else if (!HasDreamForDate(Yesterday())
      && ui::CalculateIdleTime() >= 10min
      && HasMaterial(Yesterday()))
    StartDream(Yesterday(), kCatchUp);
```

### Semantics

- **Dream day attribution**: a "dream day" covers material from
  `day D 06:00 → day D+1 06:00`. A dream run between 22:00–24:00 belongs to
  today; a run between 00:00–06:00 belongs to yesterday's dream day. An
  early-morning run therefore summarizes the day that just ended.
- **HasDreamForDate**: a `completed` row exists for that date. `failed` rows
  are retryable: max 2 retries per night, 30 minutes apart, tracked via
  `attempt_count`.
- **Catch-up covers yesterday only.** Older missed days are skipped forever.
- **Manual trigger** (settings button): `StartDream(today-so-far, kManual)`,
  bypassing night/idle/already-dreamed checks. Re-running replaces the same
  day's previous report. Also the primary dev/debug entry point.
- **Idle source**: `ui::CalculateIdleTime()` — system-wide input idle
  (macOS: `CGEventSourceSecondsSinceLastEventType`), cheap synchronous call
  on the UI thread. "User inactive" means no system-wide keyboard/mouse
  input, which matches the "asleep" semantic better than browser-level
  inactivity.

### State machine

```
kIdle ─StartDream─► kCollecting ─materials ready─► kDreaming ─result─► kSaving ─► kIdle
  ▲                    │ collect failed              │ LLM failed / 5min timeout │
  └────────────────────┴─────────────────────────────┴── MarkFailed(attempt+1) ──┘
```

- **No interruption on user return**: if the user becomes active mid-dream,
  the run completes anyway (collection is milliseconds; LLM ~30–60s; it runs
  in the hidden WebUI and does not disturb the user).
- **Process death**: no intermediate state is persisted. If the browser
  quits mid-dream, no `completed` row exists, so the catch-up path redoes it
  next launch. Stateless batch by design.
- **Empty material**: skip silently; do NOT write a `failed` row.

## Material Collection (C++, DreamMaterialCollector)

Four parallel queries joined by `base::BarrierClosure`, producing one
`base::Value::Dict` capped at roughly 8K tokens:

1. **Browsing history** — `HistoryService::QueryHistory` over the material
   window. Aggregate per domain: `{domain, visit_count, titles (≤5 deduped),
   time_buckets}` where `time_buckets` is 4 coarse counts
   (morning/afternoon/evening/night). Sort by visit count, keep **top 50**.
   **Hard rule: full URLs never enter the pack.**
2. **Search keywords** — from the same history results, recognize major
   search-engine domains (google/bing/baidu/duckduckgo/kagi), extract the
   query parameter (`q`/`wd`/…) in C++, then discard the URL. ≤30 deduped
   query strings.
3. **Agent conversations** — `LoadRecentMessages`, filter to the material
   window, take the **first 2 user messages** per session (user questions
   carry the intent), ≤10 sessions.
4. **Action feedback** — aggregate `action_feedback` rows in the window per
   `scenario_id` into `{shown, clicked, dismissed}` counts.

## LLM Summarization (WebUI, dao_dream_runner.ts)

Single-shot non-streamed call through `pi_llm_stream` using the user's
active provider from `llm_config.ts`.

- **System prompt**: "You are the browser's dream analyzer; summarize the
  user's day and infer behavior habits from the material; output strict
  JSON." + output schema + the UI language (the report is written in the
  user's language).
- **User prompt**: material pack JSON + existing high-confidence
  `preferences` (so the LLM avoids re-emitting known habits and can emit
  reinforce/contradict signals instead).

### Required output schema

```json
{
  "report_markdown": "morning report, 200–400 words: themes, time distribution, observations",
  "habits": [{
    "key": "preference key, e.g. interest.rust_async",
    "value": "habit description",
    "confidence": 0.6,
    "evidence": "one-line justification",
    "relation": "new | reinforce | contradict"
  }],
  "scenario_adjustments": [{
    "scenario_id": "...",
    "suggestion": "lower_confidence | raise_confidence"
  }]
}
```

Invalid JSON → retry once with the parse error appended; second failure
counts as a run failure.

## Persistence Rules (C++, on result)

1. **Report** → `dream_reports`, `status=completed`.
2. **Habits** → existing `MergePreference(key, value, confidence)`:
   - `new`: write with LLM self-assessed confidence **capped at 0.8** —
     dream inference is indirect evidence; confidence ≥0.9 is reserved for
     explicit user confirmation.
   - `reinforce`: `MergePreference`'s existing evidence_count accumulation
     handles it.
   - `contradict`: never auto-rewrites; surfaced in the report for the user
     to adjudicate.
3. **Scenario adjustments**: recorded in the report only. v1 does **not**
   auto-modify scenario confidence (silent auto-demotion could kill a
   feature the user likes).
4. **Dreams only add and present. No deletion or overwrite of existing
   memories without explicit user action.**

## Morning Report UI (Agent panel)

- On opening `chrome://dao-agent`, `dao_chat_view.ts` checks for an unread
  report (`viewed_at IS NULL`) and inserts a **dream card** at the top of
  the session list: 🌙 title + collapsed markdown preview, click to expand.
- **Habit confirmation rows** at the bottom of the card, one per
  `relation=new` habit, with ✓ / ✗ buttons:
  - ✓ → `MergePreference` with confidence 0.95 (explicit user confirmation).
  - ✗ → `DeletePreference` + an `action_feedback` row with
    `outcome=dismissed`; future dream prompts include user-rejected habits
    so the LLM stops re-emitting them.
  - No action → keep the LLM-written medium confidence. Users are never
    forced to triage.
- `contradict` items render as "I noticed a change: you used to prefer X,
  but yesterday…" with the same two buttons.
- Expanding the card sets `viewed_at`; the card then leaves the top slot.
  No historical-report browsing UI in v1 (YAGNI — that was direction C).
- **No system notification** on report completion; fully passive.

## Settings (Agent settings, dao_settings_view.ts)

New "Dream Analysis" section:

1. **Master toggle** (default off). When enabled, a persistent caption under
   the toggle: "Each night, the domains you visited, page titles and search
   keywords from your day are sent to your configured AI provider for
   analysis." Disclosure lives at the decision point; no separate dialog.
2. **"Dream now" button** — manual trigger; loading state while running;
   success refreshes the chat-view card; failure shows a toast.
3. **Debug mode toggle** (default off, below the "Dream now" button) — when
   enabled, each dream run persists its full material pack (the exact JSON
   sent to the LLM) into the report row, and the dream card shows a
   collapsible "Debug: inputs" section rendering that JSON, so the user can
   inspect exactly which history domains / search keywords / conversation
   excerpts / feedback stats were summarized. Stored in the
   `debug_material_json` column of `dream_reports` (empty when debug mode
   is off). Debug state itself lives in a `kDaoDreamDebug` pref.
4. Toggle state read/written through new message handlers backed by
   `kDaoDreamEnabled` / `kDaoDreamDebug` (C++ prefs are the single source
   of truth; no localStorage copy).

All new strings go through both i18n pipelines (`IDS_DAO_DREAM_*` via grd
for any C++ strings; `dream.*` keys in `en.ts` for WebUI). No hardcoded
English.

## Error Handling

| Failure | Behavior |
|---|---|
| No LLM API key configured | Scheduler skips silently (not a failure); manual trigger shows a "configure provider first" toast |
| LLM call fails / 5-minute timeout | `status=failed`, attempt+1; retry after 30 min, max 2 retries per night; otherwise next-day catch-up |
| LLM output invalid JSON | One retry with the error appended; then treat as run failure |
| Agent WebUI not yet loaded | Call its EnsureLoaded equivalent, retry the tick after 30s |
| Empty material | Skip; no failed row |
| Browser quits mid-dream | No intermediate state; catch-up redoes it next launch |

## Testing

- **browser_tests** (new `src/dao/browser/agent/dao_dream_browsertest.cc`,
  added to the existing `dao_browser_tests` source_set in
  `chrome/browser/ui/BUILD.gn.patch`):
  - `DreamSchedulerTest` — injected mock clock + mock idle source; verify
    nightly / catch-up / already-dreamed / disabled paths.
  - `DreamMaterialCollectorTest` — seed the test profile's HistoryService
    with known visits; assert domain aggregation order, time bucketing,
    search-keyword extraction, and **that no full URL appears anywhere in
    the material pack** (the privacy rule is a test assertion).
  - `DreamStoreTest` — `dream_reports` read/write, `HasDreamForDate`,
    attempt counting. Plain SQL, no FTS (avoids the known FTS5 poisoning
    issue in tests).
- **WebUI tests** — `dao_dream_runner.ts` JSON parsing/schema validation
  covered in the existing `agent/__tests__` pipeline; no real LLM calls in
  browser_tests.
- **Testability**: `DreamScheduler` takes an injectable `base::Clock*` and
  an idle-time callback; production wires the real ones.

## Out of Scope (v1)

- Sensitive-domain blocklist (UT1/Bloom filter) — deferred; revisit after
  the feature proves valuable.
- Dream diary / historical report browsing UI (direction C).
- Automatic scenario confidence adjustment from feedback stats.
- Persistent background agent (dreaming while the browser is closed).
- Configurable night window.
- System notifications for report completion.
