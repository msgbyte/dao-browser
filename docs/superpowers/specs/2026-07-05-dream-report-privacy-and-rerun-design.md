# Dream Report Privacy And Rerun Design

## Summary

Dream Analysis should let users exclude sensitive domains from dream material
collection and manually rerun the dream report for a chosen local dream date.

The domain exclusion list is a profile-level Dream setting. Excluded history
visits are filtered inside the native material collector before aggregation, so
excluded domains, titles, search queries, and debug material never leave the C++
collector. Manual reruns reuse the existing Dream pipeline with an explicit
`YYYY-MM-DD` date. A successful rerun replaces the report for that date; a
failed rerun preserves any existing completed report.

## Current Architecture

Dream Analysis is already implemented as a pipeline:

1. `DaoDreamService` decides whether to run nightly, catch-up, or manual dream
   analysis.
2. `DreamMaterialCollector` collects one dream-day window of material:
   browsing history at domain/title/time-bucket granularity, search keywords,
   agent conversation excerpts, high-confidence preferences, and proactive
   feedback stats.
3. `DaoAgentDreamHandler` registers as the resident WebUI runner and sends the
   material pack to `dao_dream_runner.ts`.
4. `dao_dream_runner.ts` calls the user's configured LLM provider and returns a
   structured dream result.
5. `DaoDreamService` merges habit candidates into preferences and persists the
   report into `dream_reports`.

The existing privacy invariant is that full URLs never appear in the material
pack. This feature strengthens that invariant by removing user-selected domains
before material aggregation.

## Goals

- Let users maintain a Dream Analysis domain exclusion list.
- Exclude matching history visits before any Dream material pack is produced.
- Ensure excluded domains are absent from generated reports, debug material,
  search query extraction, and runner payloads.
- Let users manually rerun Dream Analysis for a specific local dream date.
- Preserve the existing report for a date if a manual rerun fails.
- Keep the implementation aligned with the existing Dream scheduler, memory
  service, WebUI bridge, and report storage patterns.

## Non-Goals

- This does not delete browser history.
- This does not block the Agent or other memory features from using a domain.
  It applies only to Dream Analysis material collection.
- This does not add multiple saved versions of a dream report.
- This does not add sync or enterprise policy for the exclusion list.
- This does not change the nightly or catch-up schedule.

## Domain Exclusion Behavior

Add a new profile preference:

```text
dao.dream_excluded_domains
```

The pref stores a list of normalized domain strings. Normalization happens in
native code before storing:

- Trim whitespace.
- Lowercase.
- Accept a raw domain or a URL-like input and extract the host.
- Strip a trailing dot.
- Strip a port if present.
- Reject empty values, hosts without a registrable domain, IP literals, and
  public-suffix-only values.

Matching is suffix-based on DNS label boundaries:

- `example.com` matches `example.com`.
- `example.com` matches `app.example.com`.
- `example.com` does not match `notexample.com`.

The first version treats every excluded domain as "domain plus subdomains".
That keeps the UI simple and matches the privacy expectation users usually
have when they hide a site family from analysis.

## Material Collection

`DreamMaterialCollector` should read the normalized exclusion set at the start
of each collection. While iterating annotated history visits:

1. Extract the visit host.
2. If the host matches the exclusion set, skip the visit immediately.
3. Only for non-excluded visits, extract search queries and aggregate domain
   stats.

This placement is intentional. Filtering after aggregation or in TypeScript
would allow excluded data to appear in the native-to-WebUI payload and debug
material. Filtering before search query extraction also avoids leaking searches
made on an excluded search domain.

The material stats should include aggregate-only exclusion counts:

```json
{
  "excluded_history_visits": 3
}
```

Stats must not include the excluded domain names. Debug mode should show that
some visits were filtered without revealing which domains were filtered.

## Manual Rerun Behavior

Extend the manual Dream API to accept an optional local dream date:

```text
startManualDream({ date: "YYYY-MM-DD" })
```

Rules:

- Missing date keeps existing behavior and runs the current dream date.
- Provided dates must parse as local `YYYY-MM-DD`.
- Future dates are rejected.
- Very old dates are allowed as long as Chromium history still has data.
- Manual reruns bypass the existing "completed report already exists" skip.
- Manual reruns still require Dream Analysis and Agent Memory to be enabled.
- Only one Dream run may be active at a time.

The material window continues to use the existing dream-day model:

- `date 06:00` to `date + 1 day 06:00`.
- For the current dream date, clamp the end to now.

## Persistence Semantics

The `dream_reports` table already has `dream_date TEXT NOT NULL UNIQUE`, and
`SaveDreamReport` already uses replace-by-date semantics. Keep that behavior.

On successful rerun:

- Replace the report row for the date.
- Set `trigger_kind` to `manual`.
- Reset `viewed_at` to unread through the existing replace path.
- Use fresh material stats and debug material for the rerun.

On failed rerun:

- If no completed report exists for the date, save or update a failed row so
  retry limits and diagnostics still work.
- If a completed report already exists for the date, do not replace it with a
  failed row. The user's historical report should remain visible.

This avoids the surprising behavior where a failed rerun makes a valid old
report disappear from the history page.

## WebUI And UX

### Agent Settings

In the Dream Analysis settings area:

- Keep the existing enable, debug, "Dream now", and history controls.
- Add a compact domain exclusion editor:
  - Text input for a domain.
  - Add on Enter or add button.
  - Render saved domains as removable rows or chips.
  - Show validation failures through the existing toast/status pattern.
- Update the Dream enable description so it states that excluded domains are
  omitted before analysis.

### Dream History Page

On `dao://dream`:

- Add a refresh icon action for each history item to rerun that report date.
- Add a small date input and run action near the history header so users can
  run a date that does not already have a report.
- Disable rerun controls while a dream run is in progress.
- After success, reload the history list and select the rerun date.
- After failure, show a localized failure message and leave the selected report
  unchanged.

The history page currently has report-only handlers. To run the LLM from
`dao://dream`, extract the Dream runner bridge into a shared handler that can
be registered by both `dao://agent` and `dao://dream`. Do not register the
current `DaoAgentDreamHandler` directly on `dao://dream`, because message names
such as `markDreamReportViewed` overlap with `DaoDreamReportHandler`.

## Native API Shape

Add native WebUI messages for exclusion settings:

```text
getDreamExcludedDomains() -> string[]
addDreamExcludedDomain(string) -> { domain: string }
removeDreamExcludedDomain(string) -> true
```

The first implementation should use granular `add/remove` from the UI. A batch
setter is not required for the first version. Normalization and validation
should live in native code so all WebUI callers share the same rules.

Extend manual dream:

```text
startManualDream({ date?: string }) -> true
```

The existing no-argument call remains supported for settings-page compatibility.

## Error Handling

- Invalid domain: reject the native promise with a localized-friendly error
  code/message.
- Duplicate domain: treat as success and return the normalized domain.
- Invalid date: reject before changing Dream service state.
- Future date: reject before collecting material.
- Dream already running: preserve existing "dream already running" failure.
- No runner available: preserve existing "agent webui unavailable" failure.
- No material collected: report a failure to the caller without deleting any
  existing completed report.

## Testing

### C++ Browser Tests

Add or extend `DaoDreamBrowserTest` coverage:

- Excluded exact domain is absent from `history`.
- Excluded subdomain is absent from `history`.
- Boundary matching does not exclude `notexample.com` for `example.com`.
- Search queries from an excluded search engine visit are absent.
- Serialized material pack and debug material do not contain excluded titles,
  domains, query text, or URL fragments.
- Manual rerun for a specified date uses that date's material window.
- Failed manual rerun preserves an existing completed report.

### WebUI Tests

Add or extend agent WebUI tests:

- Settings page loads, adds, displays, and removes excluded domains.
- Duplicate and invalid domain responses are handled without corrupting state.
- Dream history rerun button calls `startManualDream` with the selected date.
- Date input rerun calls `startManualDream` with the entered date.
- Successful rerun reloads history and selects the rerun date.
- Failed rerun leaves the current report visible.

### Verification Commands

Use the smallest relevant checks while developing:

```bash
npm run test:webui
npm run lint:lit
```

For C++/Chromium compile confirmation after implementation, use only:

```bash
npm run rebuild
```

Do not use direct Chromium build tools or alternate compile paths.

## Rollout Notes

This is a privacy-sensitive Dream Analysis change. The important invariant for
review is not only that excluded domains are absent from the final report, but
that they never enter the material pack sent to the WebUI runner. The most
important code review path is therefore:

```text
history visit -> exclusion match -> search extraction / domain aggregation
```

The rerun feature should be reviewed separately for persistence behavior. A
failed rerun must not hide or overwrite a previously completed report for the
same dream date.
