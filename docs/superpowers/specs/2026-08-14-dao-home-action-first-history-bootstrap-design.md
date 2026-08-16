# Dao Home Action-First History Bootstrap Design

Status: Approved product direction; implementation pending

Date: 2026-08-14

## Summary

The browsing-history bootstrap must create a useful browser start surface, not a report about the user's browsing. The first published Home should provide recognizable launch actions and, where supported, automatically connected live information feeds.

Browsing history is private, ephemeral ranking input. It may help choose, rank, and group destinations and identify candidate live sources, but its titles, counts, time buckets, and inferred behavioral summaries must not become persisted Home content.

The bootstrap uses one trusted, consolidated permission approval. After approval, Dao configures and tests the selected connectors, generates the final Home from successful live samples, previews it, and publishes it without further user intervention. Each approved connector still receives an independent, exact authorization receipt; the single dialog is a grouped presentation, not blanket authorization.

## Problem

The current history-guided generation can produce a polished analytics-style summary containing activity counts, topic distributions, trends, and productivity suggestions. That output describes the user instead of helping the user start browsing. It also tends to render static or synthetic content rather than automatically wiring useful live sources.

The desired Home is closer to a personal browser launchpad:

- Common destinations are immediately available as actions.
- A small number of useful sources can show current information.
- Live content comes from real, explicitly approved connectors.
- Unsupported, sensitive, or failed sources remain useful launch actions.
- The generated page remains a Glass Box project made of inspectable HTML, CSS, JavaScript, connector modules, and a manifest.

## Goals

1. Generate an action-first browser start surface from privacy-minimized browsing signals.
2. Automatically connect up to three high-value live sources after one grouped approval, targeting two or three when eligible sources exist.
3. Keep raw history and history-derived analytics out of the persisted project.
4. Preserve exact, least-privilege connector authorization despite the grouped approval experience.
5. Complete the flow automatically after approval, including connector testing, final generation, preview, and publish.
6. Publish a useful launchpad even when approval is rejected or every connector fails.
7. Preserve generated layout freedom while enforcing start-surface semantics.
8. Cancel safely and clean up all temporary state when active Home ownership is lost.

## Non-Goals

- Persisting or displaying a browsing-history dashboard.
- Showing visit counts, time-of-day patterns, raw visited titles, history timelines, topic-trend charts, or behavioral advice.
- Connecting every frequently visited site.
- Reading private account content without explicit connector permission.
- Treating login, mail, messaging, or account-management pages as automatic feed sources.
- Defining a fixed visual component schema for generated Home pages.
- Adding background refresh or durable live-source caches outside an active Home session.
- Allowing the model to decide whether authorization, validation, or ownership checks are required.

## Product Outcome

After the user chooses to create a Home from browsing activity:

1. Dao derives a privacy-minimized start-surface brief in native code.
2. The Agent proposes a Home with launch actions and a small set of eligible live sources.
3. Dao presents one trusted dialog listing every proposed connector and its exact access.
4. The user may deselect individual sources or approve the selected set once.
5. Dao tests approved sources automatically.
6. The Agent builds the final Home using successful live samples and explicit fallback states.
7. Dao validates the result as a start surface, previews it in isolation, and publishes one final version.
8. The visible Home provides actions immediately and fills connected feed slots from session-scoped live data.

No second prompt such as "continue publishing" is required after approval.

## Considered Approaches

### A. Improve the generation prompt only

This is insufficient. A prompt can encourage action-first output, but it cannot enforce privacy boundaries, exact connector authorization, semantic validation, transaction cleanup, or automatic testing.

### B. Generate and publish a launchpad, then add connectors one at a time

This produces intermediate versions, repeated approval interruptions, and partial states in project history. It also makes the first-run experience feel like connector setup rather than Home creation.

### C. Plan first, approve once, then build and publish once

This is the selected approach. Dao creates provisional connector definitions, presents them in one trusted approval, tests approved connectors, and only then generates and publishes the final Home. It provides one coherent user decision while preserving per-connector authorization and a single final project revision.

## Privacy-Minimized Bootstrap Brief

Raw browsing history remains in native code and is used only during the active bootstrap transaction. The Agent receives an action-oriented `HomeBootstrapBrief`, not report-shaped aggregates.

Conceptual shape:

```text
HomeBootstrapBrief
  launch_targets[]
    id
    label_hint
    root_https_url
    category_hint
    source_eligibility
  source_candidates[]
    launch_target_id
    connector_kind_hint
    eligibility_reason
  locale
```

The brief must not contain:

- Visit counts or frequency numbers.
- Time buckets or recent-activity timestamps.
- Raw browsing-history titles.
- Ordered visit sequences.
- Topic totals or trend scores intended for display.
- Account identifiers or page-specific private content.

Native code may use those signals transiently to rank candidates, but it emits only the ordered result and coarse hints required to build a start surface.

`label_hint` is derived from a trusted destination catalog or the normalized registrable domain. It is never copied from a browsing-history page title.

URLs are normalized to safe HTTPS roots or other explicitly supported launch destinations. Page-specific paths, query strings, fragments, and embedded identifiers are removed unless an existing trusted policy explicitly permits them for that destination type.

## Source Eligibility

Each launch target is classified before the Agent proposes connectors:

| Class | Intended behavior | Examples |
| --- | --- | --- |
| `launch_and_feed` | Launch action and eligible automatic live source | GitHub activity, Bilibili subscriptions, supported forums or content streams |
| `launch_only` | Launch action without automatic data extraction | Search, translation, documentation, general tools |
| `sensitive_launch_only` | Launch action; never auto-connect from history alone | Mail, messaging, account, login, financial, health, and other sensitive surfaces |
| `unsupported` | Launch action because no supported connector exists | Any destination without a tested connector path |

The initial bootstrap proposes at most three connectors. Candidate selection favors utility, connector reliability, diversity, and least privilege over raw browsing frequency.

Sensitive sources may be connected later only through an explicit user request and the ordinary connector permission flow. Their appearance in browsing history is never sufficient authorization.

## Generated Experience Contract

The generated project remains ordinary inspectable web code. Dao constrains semantics rather than prescribing a layout.

The final Home must satisfy all of the following:

- The first viewport is recognizably a browser start surface.
- At least `min(4, launch_targets.length)` launch actions are visible and keyboard-focusable when that many targets exist.
- Every launch action uses the trusted `dao.navigation.open` capability.
- Live cards read only from approved and successfully tested connectors.
- A source without a successful connector appears as a launch action or a clearly disconnected state, never as fabricated live data.
- Loading, empty, authentication-required, and per-source failure states are present for every live slot.
- The persisted project contains no visit counts, time buckets, history-title lists, trend summaries, or browsing-derived productivity advice.
- A pure statistics dashboard does not pass preview validation.

History-bootstrap projects must declare semantic intent in `experience.json`:

```json
{
  "kind": "start_surface",
  "primary_actions": ["github", "linear", "bilibili"],
  "source_slots": ["github_activity", "bilibili_feed"]
}
```

Generated markup identifies the declared semantics with `data-dao-action` and `data-dao-source-slot`. These attributes support deterministic validation and do not impose styling or component structure.

## Agent Contract

The history-bootstrap system instruction must state that:

- Browsing material is private design input and must never become page content.
- The result is an action-first browser start surface.
- History may only choose, rank, and group destinations and identify connector candidates.
- The project must not show counts, time buckets, history titles, trend charts, browsing summaries, or productivity judgments.
- Recognizable launch actions belong in the first viewport.
- Live cards require approved connectors; otherwise the project must render an explicit disconnected or launch-only state.
- Connector definitions in the provisional plan are not yet authorized.
- The Agent must wait for native test results before generating the final live layout.

The Agent is responsible for creative layout and connector code generation. Native code remains responsible for eligibility, authorization, connector test execution, semantic validation, ownership, and the final revision kind.

## Bootstrap Transaction

The flow is owned by a native `HomeBootstrapTransaction` rather than a sequence of loosely related Agent tool calls.

Conceptual state machine:

```text
planning
  -> drafting_connectors
  -> awaiting_batch_approval
  -> testing_sources
  -> building_final_home
  -> previewing
  -> publishing
  -> complete
```

Any non-success terminal path enters `cancelled` or `failed`, cleans up temporary drafts and source pages, invalidates unused receipts, and releases all session payloads.

The transaction owns:

- The active Home document and Agent turn lease.
- The base project revision.
- The ephemeral `HomeBootstrapBrief`.
- Proposed and selected source candidates.
- Per-source connector fingerprints.
- Per-source approval, test, and error states.
- Successful bounded connector samples.
- The final project draft.
- Cancellation and cleanup hooks.

Only one history bootstrap may mutate a Home document at a time. A new bootstrap cancels the existing one before acquiring ownership.

## Grouped Connector Approval

The existing single-connector permission request is extended with a batch request for the bootstrap flow.

The trusted dialog shows one row per proposed connector, including:

- Human-readable source name.
- Origin and allowed paths.
- Capabilities.
- Resource budgets.
- Whether authentication may be required.
- A selected checkbox, enabled by default.

The dialog offers one confirm action for all selected rows and one reject action for the group. The user may deselect any row before confirming.

Confirmation creates one independent authorization receipt per selected connector. It does not create a batch-wide capability.

### Connector Fingerprint

Each receipt is bound to a canonical fingerprint covering every authorization-relevant field:

```text
connector_fingerprint = hash(
  origin,
  normalized_paths,
  capabilities,
  module_digest,
  schema_digest,
  resource_budgets
)
```

Normalization and hashing occur in trusted native code. Any change to module code, output schema, origin, path scope, capability, or resource budget invalidates that connector's receipt. Unchanged connector receipts remain transferable from the provisional plan to the final draft.

This transfer is what allows the final Home to be generated after source tests without displaying another permission dialog.

## Automatic Source Testing

After grouped approval, Dao tests selected connectors automatically and sequentially. Sequential execution limits resource pressure and makes cancellation deterministic.

Each test:

1. Opens the source in the existing isolated connector execution environment.
2. Applies the exact authorization receipt.
3. Runs the connector under its declared budgets.
4. Validates the output against the declared schema.
5. Returns a bounded, ephemeral sample or a structured failure.
6. Tears down source execution state unless it is required for the active session.

Successful samples are supplied to final generation only to shape the live card and verify that its state handling matches real output. Samples are not persisted in project files or project history.

### Per-Source Outcomes

| Outcome | Final Home behavior |
| --- | --- |
| Success | Add a live feed slot backed by the tested connector |
| Authentication required | Keep the launch action and show a session-time sign-in/disconnected state |
| Runtime or DOM failure | Keep the launch action; omit or disable the live slot with a clear fallback |
| Schema failure | Treat as connector failure and do not expose the invalid payload |
| User deselected | Keep the launch action only |
| Unsupported | Keep the launch action only |

One source failure never blocks unrelated successful sources.

## Final Generation, Preview, and Publish

The Agent receives:

- The original action-oriented brief.
- The selected connector definitions.
- Per-source test outcomes.
- Bounded successful samples.
- The exact experience contract.

It then generates the final project once. The final project may reuse an approved connector only if its trusted fingerprint is unchanged.

Native preview performs both ordinary project validation and start-surface semantic validation. It must execute the app in the isolated preview environment; a syntactically valid project that throws at runtime cannot pass.

If semantic validation fails, the Agent may repair the final draft within the same transaction. Repair is bounded by the normal iteration and resource limits. Any connector fingerprint change during repair requires approval for that connector before publish; the system must not silently broaden or replace approved access.

On success, native code publishes exactly one revision with kind `history_bootstrap`. The revision kind is forced by the native transaction and cannot be supplied or overridden by the model. Intermediate connector plans and final-generation drafts are not added to visible project history.

## Best-Effort Completion

The bootstrap prioritizes publishing a useful Home:

- If the user rejects the grouped approval, Dao continues with a launchpad-only project.
- If the user deselects every connector, Dao continues with a launchpad-only project.
- If some connectors fail, successful sources become live feeds and failed sources remain launch actions.
- If all connectors fail, Dao publishes a launchpad with explicit disconnected states where appropriate.
- If final generation cannot satisfy the start-surface contract after bounded repair, the transaction fails without replacing the current published Home.

Permission rejection is therefore not a bootstrap failure. Ownership loss, invalid final output, or an unrecoverable storage error is a failure.

## Ownership, Cancellation, and Cleanup

The transaction is valid only while all of the following remain true:

- The originating Home document is active.
- Its `WebContents` is visible.
- The originating Agent turn is active.
- The mutation lease remains valid.
- The base revision has not changed incompatibly.

Tab navigation, switching away from Home, hiding the window, document replacement, Agent turn completion or cancellation, or lease invalidation cancels the transaction before any further mutation.

Cancellation must clean up independently of the WebUI handler lifetime:

- Temporary project drafts.
- Provisional connectors.
- Pending batch approvals.
- Unused authorization receipts.
- Connector executors and hidden source pages.
- Test samples and other live payloads.
- Pending preview and publish continuations.

Every mutation boundary, especially the final store commit, revalidates the transaction lease.

## Runtime Live Data

Published project files contain connector definitions and presentation logic, not browsing-history data or captured feed results. While Home is active, the trusted host runs approved connectors and supplies bounded session-scoped results to the generated app.

Runtime requirements:

- No connector runs before its exact receipt is validated.
- Completed results and media remain bounded and share executor lifecycle or explicit eviction.
- Source navigation or document teardown invalidates dependent results.
- No background refresh continues after Home loses visibility or ownership.
- No live payload is written into revision history.
- Refresh cadence remains within the declared resource budgets.

## Trusted Validation

Validation is layered:

1. **Project validation** verifies manifest, file, schema, CSP, and size constraints.
2. **Connector validation** verifies fingerprint, authorization receipt, schema, budgets, and successful test status.
3. **Runtime preview** executes the app and rejects syntax errors, top-level exceptions, and failed initialization.
4. **Experience validation** verifies the semantic start-surface contract.
5. **Ownership validation** guards every asynchronous continuation and final commit.

Experience validation reads `experience.json`, checks corresponding semantic DOM markers in preview, and rejects forbidden bootstrap fields or known history-derived material carried into persisted project data. It does not attempt to classify every arbitrary sentence or number in generated page copy. Native code rejects a `start_surface` project when the declaration and rendered semantics disagree.

The validation signal is not exposed to generated code as a writable verdict. Browser-owned preview and transaction code determine success.

## API and Type Direction

The implementation is expected to introduce or extend the following conceptual interfaces:

- `HomeBootstrapBrief` for action-oriented native history reduction.
- `HomeSourceEligibility` for source classification.
- `HomeBootstrapTransaction` for state, ownership, and cleanup.
- `HomePermissionBatchRequest` with selectable connector rows.
- `HomeConnectorFingerprint` and per-connector authorization receipts.
- Structured connector test outcomes.
- `experience.json` project validation.
- A native-only `history_bootstrap` publish path.

Existing singular permission APIs remain available for ordinary later edits. Bootstrap batching is an orchestration layer over independent connector grants, not a replacement for least-privilege connector permissions.

## Test Strategy

### Native Unit Tests

- The history reducer emits ordered root launch targets without counts, time buckets, raw titles, queries, fragments, or identifiers.
- Sensitive targets are always `sensitive_launch_only` for history bootstrap.
- Source selection caps automatic candidates at three and favors supported diverse sources.
- Batch confirmation creates receipts only for selected connectors.
- Each receipt matches only its exact canonical fingerprint.
- Changing code, schema, scope, capability, or budget invalidates the affected receipt.
- Rejecting the batch yields a valid launchpad-only continuation.
- Native code forces the published revision kind to `history_bootstrap`.

### Service and Store Tests

- A transaction with two successful sources and one failed source publishes two live slots and preserves all three launch actions.
- A transaction with all sources rejected or failed still publishes a launchpad.
- Intermediate drafts are discarded and do not appear in project history.
- Ownership loss at every asynchronous boundary prevents publish and deletes temporary state.
- WebUI teardown cannot leak an imported or generated temporary draft.
- Connector receipt transfer succeeds only for unchanged fingerprints.
- Live samples are not persisted in files, revisions, or history metadata.

### WebUI Tests

- The trusted dialog renders all proposed connectors, access scopes, capabilities, and budgets.
- Individual rows can be deselected before one grouped confirmation.
- Reject and confirm paths report structured results to the native transaction.
- The app renders action, loading, empty, authentication, and per-source failure states.
- Hidden Home cannot create a new mutation lease or continue a bootstrap.

### Semantic Preview Tests

- A valid start surface with actions and approved live slots passes.
- A statistics dashboard with counts, trend bars, and browsing summaries fails.
- Declared actions without matching focusable DOM markers fail.
- A live slot without a tested connector fails.
- Hardcoded sample feed content fails when presented as live data.
- A syntactically valid app with a top-level runtime exception fails.

### Browser End-to-End Test

From an empty Home:

1. Start history bootstrap with a deterministic test history set.
2. Approve two eligible sources in the grouped dialog.
3. Verify the flow completes without another user prompt.
4. Verify the published Home exposes clickable trusted launch actions.
5. Verify live slots receive data only from the approved test connectors.
6. Hide or navigate away during a second run and verify cancellation and cleanup.

## Rollout and Compatibility

- Ordinary manual Home creation and later single-connector edits keep their existing permission flow.
- Existing published projects remain valid; `experience.json` is required only for the new history-bootstrap generation contract at first.
- The native history reducer and source allowlist may roll out behind the existing Home feature gate.
- Connector kinds should be enabled incrementally after deterministic test coverage exists.
- Telemetry, if added, records only coarse transaction states and error classes. It must not record launch targets, connector payloads, browsing titles, or source-specific private data.

## Documentation Impact

Implementation must update `docs/features.md` to describe the action-first history bootstrap, grouped connector approval, automatic best-effort source connection, and ephemeral history boundary. It must also update `docs/feature-checklist.md` with first-run, partial failure, permission rejection, owner-loss, and no-history-report regression checks.

This design document itself does not change runtime behavior, so those inventory changes belong to the implementation change rather than this specification-only step.

## Acceptance Criteria

The feature is complete when all of the following are true:

1. Starting from no project, the history bootstrap can publish a usable Home in one flow.
2. The first viewport contains recognizable, keyboard-accessible launch actions.
3. Eligible sources are automatically tested and connected after one grouped trusted approval.
4. The user can deselect individual connector rows before approval.
5. Every connector is authorized by an independent exact fingerprint receipt.
6. Connector failures degrade independently and do not prevent a launchpad from publishing.
7. Rejecting connector access still produces a launchpad-only Home.
8. Persisted files and revisions contain no browsing-history report material or captured live payloads.
9. Live slots receive content only through approved, successfully tested session connectors.
10. A report-style statistics dashboard fails start-surface validation.
11. Ownership loss prevents subsequent mutation and cleans all temporary resources.
12. Exactly one visible `history_bootstrap` revision is published on success.
