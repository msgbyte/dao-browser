# Dao Agent Proactive Suggestions Design

## Summary

Dao Agent proactive suggestions should move from URL-triggered template prompts
to a local, explainable recommendation pipeline:

```text
candidate generation -> signal extraction -> ranking -> tiered presentation -> feedback learning
```

The goal is to make suggestions more accurate, more useful, and less annoying.
Dao should only interrupt when it has a clear reason to believe the suggestion
helps the user's current browsing task. The system should explain why it is
suggesting something, what context it will use, and what the user will get after
clicking Run.

Before the user clicks Run, the system must stay local: no LLM call and no page
content upload. Clicking Run is the consent boundary for page capture and model
execution.

## Current Problems

The current implementation is too close to a URL matcher:

- `DaoAgentProactiveEngine` waits for dwell, matches a scenario, checks coarse
  content length, then emits a card.
- Seed scenarios always beat personal scenarios, which blocks personalization.
- The confidence threshold mainly affects legacy episode suggestions, not seed
  scenario suggestions.
- The card only shows a title and a cost hint. It does not explain why the
  suggestion appeared, what it will read, or what it will produce.
- Feedback records click and dismiss outcomes, but shown, ignored, completed,
  and failed states are not first-class signals.
- `times_triggered` is not updated when a suggestion is shown, so downstream
  feedback aggregation and Dream Analysis do not have a reliable exposure
  denominator.
- The page-content capture path uses a main-frame routing ID as a tab handle,
  which is not a durable cross-navigation tab identity.
- Personal scenario storage exists, but there is no complete product loop that
  proposes, confirms, ranks, and manages learned scenarios.

## Product Principles

1. Prefer being quiet over being wrong.
   A proactive browser feature loses trust quickly if it feels like a popup.

2. Explain before acting.
   Every visible suggestion must answer:
   - Why am I seeing this?
   - What context will Dao use?
   - What will happen after I click Run?

3. Stay local before Run.
   Candidate generation, signal extraction, ranking, and suppression happen
   locally. Page content capture and LLM execution happen only after Run.

4. Feedback changes behavior immediately.
   Not now, dismiss, accept, complete, and fail outcomes must affect future
   ranking and suppression.

5. Personal learning must be visible and reversible.
   Learned scenarios and preference effects must be inspectable and deletable
   from memory/settings surfaces.

## Modes

The existing Quiet, Balanced, and Active settings should apply to every
proactive source, not only legacy episode suggestions.

| Mode | Agent panel threshold | Sidebar nudge threshold | Learning behavior |
| --- | ---: | ---: | --- |
| Quiet | 0.85 | disabled | No exploratory prompts |
| Balanced | 0.75 | 0.90 | Conservative learning proposals |
| Active | 0.60 | 0.80 | More exploratory learning proposals |

Quiet should be safe as a default for users who dislike interruptions. Active is
for dogfood and users who want Dao to learn faster.

## Candidate Sources

All sources produce a common candidate structure. No source directly owns
presentation.

### Seed Scenarios

Existing seed scenarios remain useful defaults:

- GitHub PR review
- GitHub issue analysis
- Linear project progress
- Documentation summary
- Q&A answer extraction

Seed scenarios become candidates, not guaranteed suggestions. URL match alone
should not be enough to show a suggestion.

### Personal Scenarios

Personal scenarios represent user-confirmed habits, such as:

```text
When I open PRs in this repo, check Chromium patch safety and missing tests.
```

High-confidence personal scenarios can override seed scenarios when they match
the same page. This is necessary for Dao to become personally useful.

### Episode Resume

If the user previously worked with Dao on the same domain or page family, Dao
can suggest resuming that task. This should be lower priority than high-value
page opportunities unless the previous session is recent and unfinished.

### Dream Habits

Dream Analysis should not directly trigger suggestions. It should provide
confirmed preference signals that increase `user_fit`, such as:

- The user often summarizes long technical docs.
- The user often reviews PRs in the morning.
- The user prefers blocker summaries for Linear projects.

Only confirmed or high-confidence habits should affect proactive ranking.

### Page Opportunities

Dao should detect useful page types beyond hard-coded sites:

- Long article or documentation page
- Code diff or review page
- Q&A or forum thread
- Error page or failed workflow
- Complex form
- Search results page
- Dashboard with tables or status cards
- Product or pricing comparison page
- Video/course page

These detections should be based on local page signals, not a model call.

## Local Signals

Signal extraction should be lightweight and local.

Useful signals include:

- URL scheme, domain, path, and query shape
- Page title and meta description
- Text length, character count, language, and content density
- Count of code blocks, tables, forms, buttons, links, headings, comments, and
  diff-like elements
- Keywords from title and visible text, kept local
- Dwell duration
- Whether the user is typing or the page is still changing
- Whether the Agent is currently streaming
- Recent suggestions on the same URL, domain, and action type
- Historical feedback by scenario, domain, and action type
- Confirmed memory and Dream preferences

The extractor should return structured signals, not a final decision.

## Ranking Model

Each candidate receives a score that can be explained:

```text
final_score =
  relevance
+ usefulness
+ user_fit
+ timing
- interruption_cost
- fatigue_penalty
- privacy_sensitivity
```

### Relevance

How well the candidate matches the current page.

Examples:

- URL pattern match: small to medium boost.
- Page title and DOM hints match: larger boost.
- Required page evidence missing: penalty.

### Usefulness

How much time the action is likely to save.

Examples:

- Long PR diff review: high usefulness.
- Long documentation summary: high usefulness.
- Generic "help with this page": low usefulness.

### User Fit

Whether this user tends to accept similar help.

Inputs:

- Accepted/completed rate for this action type
- Dismissed/ignored rate for this domain and action type
- Confirmed Dream preferences
- Personal scenario confidence

### Timing

Whether now is a good moment.

Inputs:

- Dwell time
- Repeated visit
- Long page reading behavior
- Error or blocked state
- Opened from search

### Interruption Cost

Whether a suggestion would disrupt the user.

Penalties:

- User is typing
- Agent is streaming
- Page just loaded
- Page is still changing
- Another suggestion was just shown

### Fatigue Penalty

Avoid repeated suggestions.

Rules:

- Same URL and scenario: once per session.
- Same domain and action type: at most one visible nudge per 10 minutes.
- Two Not now outcomes on the same domain: quiet that domain for the day.
- Two Don't suggest this here outcomes: strongly suppress the domain/action
  pair until settings or memory management changes it.

### Privacy Sensitivity

Sensitive pages should be suppressed by default.

Sensitive categories include:

- Banking and payment
- Checkout
- Email
- Messaging and private social inboxes
- Health and medical
- Password, account, and security settings
- Enterprise admin pages with secrets or credentials

The first version can use conservative URL/domain/form heuristics. False
negatives are more dangerous than false positives here.

## Presentation Tiers

### Silent

No visible UI. The candidate and suppression reason can be recorded in debug
state if debug mode is enabled.

### Agent Panel Card

Default visible tier. It appears at the top of the Agent panel and does not
steal focus.

Card content:

- Title
- Reason
- Expected outcome
- Context disclosure
- Run
- Not now
- Don't suggest this here

Example:

```text
Review this PR
Looks like a GitHub PR with a sizeable diff.
I can summarize the changes and flag likely review risks.
Uses visible page text after you click Run.

[Run] [Not now] [Don't suggest this here]
```

### Sidebar Nudge

High-confidence tier only. This should be a small, non-modal cue near the Agent
entry point, not an overlay that competes with page content.

The nudge opens the Agent panel card. It should not run the suggestion directly.

## Run Behavior

The visible user message should be transparent. It should not be only:

```text
Run suggestion: Review this PR
```

Preferred shape:

```text
Review this PR.

Why Dao suggested this:
- GitHub pull request page
- Long diff/comment content detected
- You have accepted PR review suggestions before

Please summarize the changes, identify likely bugs or risks, and suggest review
comments.
```

The page content can still be attached as a hidden/document attachment, but the
chat transcript should show the task clearly.

## Feedback Events

Feedback should be first-class and recorded with the score breakdown when
available.

Required outcomes:

- `shown`: visible card or nudge appeared.
- `accepted`: user clicked Run.
- `dismissed`: user clicked Don't suggest this here.
- `not_now`: user clicked Not now.
- `ignored`: suggestion was visible long enough and expired without action.
- `completed`: Agent run finished without a model/tool/page-capture failure.
- `failed`: Agent run failed.
- `helpful`: user copied, shared, continued, or explicitly marked useful.
- `not_helpful`: user regenerated, rewound, edited the prompt immediately, or
  explicitly marked not useful.

The existing action feedback table can be extended or complemented by a richer
proactive event table. The design should preserve migration safety for existing
profiles.

## Learning Proposals

Dao should not create strong personal scenarios from one behavior.

When similar accepted/completed behavior repeats 2-3 times, Dao can show a
learning proposal:

```text
You often ask Dao to review PRs in this repo.
Suggest this automatically next time?

[Yes] [Not here] [Never for this]
```

If confirmed, save a personal scenario with:

- Source pattern
- Action type
- User-facing title
- Prompt template
- Reason template
- Evidence count
- Acceptance/completion stats
- Created and last-updated timestamps

Personal scenarios must be visible and deletable from settings or memory UI.

## Failure and Recovery

Failure states should be specific and recoverable:

- Tab not found: offer to use the active tab instead.
- Page content changed: offer to refresh the suggestion.
- Agent busy: offer to run after the current response.
- Sensitive page: explain that Dao avoids reading it automatically and let the
  user manually attach context if they want.
- Page capture failed: allow retry and record `failed`.

## Debugging and Observability

Debug mode should expose recent proactive decisions:

- Candidate source
- Candidate ID and action type
- Signal summary
- Score breakdown
- Presentation tier
- Suppression reason
- Feedback outcome

This can start as a debug section in the memory or agent debug surface. It does
not need to be polished for general users in the first implementation phase.

## Implementation Architecture

Keep the current service boundaries, but split proactive logic into focused
units:

- `DaoAgentProactiveEngine`
  Coordinates navigation, dwell timers, active tab observation, and WebUI
  events.

- `DaoAgentProactiveSignals`
  Extracts local page signals from URL, title, and lightweight DOM analysis.

- `DaoAgentProactiveCandidate`
  Defines the common candidate model and presentation payload.

- `DaoAgentProactiveRanker`
  Scores candidates and returns score breakdown plus presentation tier.

- `DaoAgentProactiveFeedback`
  Records and aggregates proactive events.

- `DaoAgentScenarioRegistry`
  Provides seed and personal scenario candidates. It no longer decides final
  presentation by itself.

The first implementation should avoid a large ML-style abstraction. The ranker
can be deterministic and easy to unit test.

## Data Model Direction

The current `ActionFeedback` model can remain for compatibility, but proactive
learning needs richer events.

Recommended new row shape:

```text
proactive_events
- id
- suggestion_id
- source
- scenario_id
- action_type
- domain
- url_hash or normalized_url
- presentation_tier
- score
- score_breakdown_json
- reason
- outcome
- timestamp
- session_id
```

Privacy note: full URLs should be avoided where possible. If a full URL is
needed for deduplication in the first phase, it should stay local and be covered
by memory clearing.

## Phased Delivery

### P0: Trust and Feedback Repair

- Record shown, ignored, completed, and failed.
- Increment `times_triggered` when a scenario suggestion is shown.
- Make Quiet/Balanced/Active affect all proactive sources.
- Add reason, expected outcome, and context disclosure to the card.
- Split Not now from Don't suggest this here.
- Improve Run prompt transparency.

### P1: Local Ranking Pipeline

- Add candidate, signal, and ranker modules.
- Treat seed scenarios as candidates.
- Add DOM/page-hint based relevance scoring.
- Add fatigue and privacy suppression.
- Add debug score breakdown.

### P2: Personalization

- Allow high-confidence personal scenarios to beat seeds.
- Add learning proposals.
- Use confirmed Dream habits as `user_fit` boosts.
- Add memory/settings management for learned proactive patterns.

### P3: Sidebar Nudge

- Add high-confidence sidebar nudges after ranking and feedback are reliable.
- Keep nudges non-modal and rate-limited.
- Use the Agent panel card as the detailed view.

## Acceptance Criteria

Product behavior:

- A GitHub PR URL alone does not guarantee a suggestion.
- Quiet mode suppresses ordinary seed suggestions.
- Balanced mode shows valuable PR, docs, and Q&A suggestions in the Agent panel
  without frequent sidebar nudges.
- Dismissing a domain/action type reduces future suggestions for that pair.
- Not now creates a short-term timing cooldown without permanently suppressing
  the action type.
- Accepted and completed suggestions increase similar future scores.
- High-confidence personal scenarios can override seed scenarios.
- Sensitive pages do not show automatic proactive suggestions.
- Run does not happen before the user clicks Run.
- Page content is not captured or sent before Run.
- Failure states explain what happened and offer a recovery path.
- Debug output can explain why a suggestion appeared or was suppressed.

Verification:

- C++ tests cover ranker scoring, thresholds, feedback aggregation, sensitive
  suppression, fatigue suppression, and personal-over-seed priority.
- WebUI tests cover card copy, action buttons, Run prompt content, and failure
  states.
- Focused manual QA covers GitHub PR, documentation, StackOverflow/Q&A, Linear
  project, a sensitive page, and a normal page with no useful suggestion.
- Compile confirmation uses `npm run rebuild`, and no other Chromium build path.

## Decisions For The First Implementation

- Proactive event rows should prefer domain, normalized path shape, and a URL
  hash over raw full URLs. If an existing legacy feedback path still stores raw
  URLs, keep that behavior local and cover it with memory clearing until the
  migration is complete.
- Agent settings should own mode toggles and proactive feature preferences.
  Memory/debug surfaces should own inspection and deletion of learned patterns
  and event history.
- `helpful` and `not_helpful` should be inferred in the first implementation.
  Explicit feedback buttons can be added after the card interaction is proven.
- Sidebar nudges should ship behind a separate preference and remain default-off
  until P3 proves suggestion quality and rate limiting.

## Recommended First Implementation Scope

Start with P0 and the minimum P1 ranker:

- This gives immediate product improvement.
- It repairs the data foundation needed by the personalization phases.
- It keeps the first code change bounded and testable.
- It avoids premature sidebar nudges before suggestion quality is proven.
