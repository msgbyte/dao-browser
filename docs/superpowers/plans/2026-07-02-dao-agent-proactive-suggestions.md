# Dao Agent Proactive Suggestions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Dao Agent proactive suggestions accurate, useful, explainable, and quiet by default. A visible suggestion must be locally ranked, must explain why it appeared, must disclose what context it will use, and must not capture page content or call the model until the user clicks Run.

**Architecture:** Keep `DaoAgentProactiveEngine` as the browser/navigation coordinator, but move decision quality into deterministic candidate, signal, and ranker modules. The engine generates candidates, extracts local page signals, asks for per-candidate feedback cooldown, ranks candidates, emits only the best eligible panel card, and records first-class feedback through the existing memory service.

**Tech Stack:** Chromium C++ in `src/dao/browser/agent/` and `src/dao/browser/ui/webui/dao_agent_ui.cc`; Lit/TypeScript WebUI in `src/dao/browser/ui/webui/resources/agent/`; existing SQLite-backed `ActionFeedback`; Vitest for WebUI tests; Dao browser tests through `browser_tests`.

---

## Constraints

- Do not edit `engine/` directly. Make canonical changes under `src/dao/` and `src/patches/` only.
- Do not run `autoninja`, `ninja`, `siso`, direct Chromium build tools, or direct `gn gen`.
- Use `npm run rebuild` as the only compile-confirmation command.
- Do not run bare `npm run export`.
- Do not edit generated files under `src/dao/browser/ui/webui/resources/agent/vendor/`.
- Do not run `i18n.sh`; add only English and hand-authored `zh-CN` strings for new Agent WebUI copy.
- Do not run state-changing git commands unless the latest user message explicitly authorizes the exact action.
- Keep the first implementation on the primary checkout on `main`; do not create a branch or worktree.

## Source Files

Create:

- `src/dao/browser/agent/dao_agent_proactive_types.h`
- `src/dao/browser/agent/dao_agent_proactive_ranker.h`
- `src/dao/browser/agent/dao_agent_proactive_ranker.cc`
- `src/dao/browser/agent/dao_agent_proactive_ranker_unittest.cc`

Modify:

- `src/dao/browser/agent/BUILD.gn`
- `src/dao/browser/agent/dao_agent_memory_store.cc`
- `src/dao/browser/agent/dao_agent_memory_types.h`
- `src/dao/browser/agent/dao_agent_proactive_engine.h`
- `src/dao/browser/agent/dao_agent_proactive_engine.cc`
- `src/dao/browser/agent/dao_agent_scenario_registry.h`
- `src/dao/browser/agent/dao_agent_scenario_registry.cc`
- `src/dao/browser/ui/webui/dao_agent_ui.cc`
- `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
- `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`
- `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`
- `src/dao/browser/ui/views/dao_browser_browsertest.cc`

Reference:

- `docs/superpowers/specs/2026-07-02-dao-agent-proactive-suggestions-design.md`

## Implementation Tasks

- [ ] 1. Add proactive decision types.

  Define shared, local-only decision structures in `dao_agent_proactive_types.h`.

  Required shapes:

  ```cpp
  enum class ProactivePresentationTier {
    kSilent,
    kAgentPanelCard,
    kSidebarNudge,
  };

  enum class ProactiveCandidateSource {
    kSeedScenario,
    kPersonalScenario,
    kEpisodeResume,
  };

  struct ProactivePageSignals {
    std::string url;
    std::string domain;
    std::string title;
    std::string meta_description;
    std::string language;
    int word_count = 0;
    int char_count = 0;
    int code_block_count = 0;
    int table_count = 0;
    int form_count = 0;
    int button_count = 0;
    int link_count = 0;
    int heading_count = 0;
    int password_input_count = 0;
    int payment_input_count = 0;
    bool has_code = false;
    bool has_significant_content = false;
    bool is_cjk = false;
    bool is_typing = false;
    bool is_sensitive = false;
  };

  struct ProactiveCandidate {
    ProactiveCandidateSource source = ProactiveCandidateSource::kSeedScenario;
    ScenarioDefinition scenario;
  };

  struct ProactiveFeedbackSignals {
    double cooldown_score = 0.0;
    base::Time last_domain_action_shown;
  };

  struct ProactiveScoreBreakdown {
    double relevance = 0.0;
    double usefulness = 0.0;
    double user_fit = 0.0;
    double timing = 0.0;
    double interruption_cost = 0.0;
    double fatigue_penalty = 0.0;
    double privacy_sensitivity = 0.0;
    double final_score = 0.0;
  };

  struct ProactiveDecision {
    ProactiveCandidate candidate;
    ProactiveScoreBreakdown score;
    ProactivePresentationTier tier = ProactivePresentationTier::kSilent;
    std::string reason;
    std::string expected_outcome;
    std::string context_disclosure;
    std::string suppression_reason;
    std::string score_debug_json;
  };
  ```

  Add `reason`, `expected_outcome`, `context_disclosure`, `suppression_reason`, `score_debug_json`, `url`, and `domain` to `ProactiveSuggestion` in `dao_agent_memory_types.h`. These are presentation and feedback fields; they must not contain page body text.

- [ ] 2. Add scenario candidate matching without removing the compatibility matcher.

  In `DaoAgentScenarioRegistry`, add:

  ```cpp
  std::vector<ScenarioDefinition> GetMatchingScenarios(
      const std::string& url) const;
  ```

  Implementation rules:

  - Return every matching seed and personal scenario.
  - Sort seed scenarios by descending URL pattern length.
  - Sort personal scenarios by descending acceptance rate, using `0.5` for scenarios with no trigger count.
  - Keep `Match()` as a compatibility helper that returns the first seed-specific match or first personal match when no seed matches.
  - Update new engine code to use `GetMatchingScenarios()`, not `Match()`.

  Update `DaoAgentScenarioRegistryTest` in `src/dao/browser/ui/views/dao_browser_browsertest.cc`:

  - Keep `MatchesSeedPrPattern`, `MatchesSeedIssuePattern`, `NoMatchForUnrelatedUrl`, and `AddAndRemovePersonal`.
  - Replace `SeedBeatsPersonalOnConflict` with `GetMatchingScenariosIncludesSeedAndPersonalOnConflict`.
  - Assert that a GitHub PR URL returns both `seed_github_pr` and the matching personal scenario.

- [ ] 3. Implement the deterministic ranker.

  Add `DaoAgentProactiveRanker` in `dao_agent_proactive_ranker.h/.cc`.

  Public API:

  ```cpp
  class DaoAgentProactiveRanker {
   public:
    struct Options {
      double panel_threshold = 0.75;
      double sidebar_threshold = 0.90;
      base::TimeDelta min_domain_action_gap = base::Minutes(10);
    };

    explicit DaoAgentProactiveRanker(Options options);

    ProactiveDecision Rank(
        const ProactiveCandidate& candidate,
        const ProactivePageSignals& signals,
        const ProactiveFeedbackSignals& feedback,
        base::Time now) const;
  };
  ```

  Scoring rules for the first version:

  - `relevance`
    - `+0.35` for URL pattern match already represented by the candidate.
    - `+0.20` when a scenario requires page content and `signals.has_significant_content` is true.
    - `+0.15` when docs/Q&A/PR/issue/Linear-specific DOM evidence is present through code blocks, tables, headings, or large text.
    - `-0.35` when page content is required and significant content is missing.
  - `usefulness`
    - `+0.20` for PR, issue, Linear, Q&A, or docs seed actions.
    - `+0.10` for long content above 1500 words or 3000 CJK chars.
    - `+0.10` for code blocks or tables that make summarization/review valuable.
  - `user_fit`
    - `+0.20` for personal scenarios with acceptance rate at least `0.60`.
    - `+0.10` for personal scenarios with no trigger history.
    - `-0.10` when scenario dismiss rate is above accept rate and there are at least three triggers.
  - `timing`
    - `+0.10` after dwell has fired.
    - `-0.30` when `signals.is_typing` is true.
  - `interruption_cost`
    - `0.20` when the domain/action was shown less than 10 minutes ago.
    - `0.10` for pages with forms or many inputs.
  - `fatigue_penalty`
    - `min(0.45, feedback.cooldown_score * 0.15)`.
    - Suppress with reason `fatigue` when `feedback.cooldown_score >= 3.0`.
  - `privacy_sensitivity`
    - `1.0` when `signals.is_sensitive` is true.
    - Suppress with reason `sensitive_page` when privacy sensitivity is `1.0`.

  Clamp `final_score` to `[0.0, 1.0]`. `kAgentPanelCard` requires `final_score >= panel_threshold`. `kSidebarNudge` exists in the model but should not be emitted in this first implementation; keep the tier at `kAgentPanelCard` even above the sidebar threshold.

  Generate user-visible metadata from the decision:

  - `reason`: one sentence, no raw page body.
  - `expected_outcome`: one sentence describing the output.
  - `context_disclosure`: "Uses visible page text after you click Run." for page-content scenarios; "No page text is captured before Run." when no page content is needed.
  - `score_debug_json`: compact JSON with numeric score fields and suppression reason.

- [ ] 4. Add ranker tests.

  Add `dao_agent_proactive_ranker_unittest.cc` to `src/dao/browser/agent/BUILD.gn` under `source_set("unit_tests")`.

  Test cases:

  - `BalancedShowsLargeGithubPr`: PR candidate with significant content scores at or above `0.75` and returns `kAgentPanelCard`.
  - `QuietSuppressesGenericShortDoc`: docs candidate with short content scores below `0.85` and returns `kSilent`.
  - `SensitivePageSuppresses`: payment or password signals return `kSilent` with `suppression_reason == "sensitive_page"`.
  - `FatigueSuppressesAfterRepeatedDismissals`: cooldown `3.0` returns `kSilent` with `suppression_reason == "fatigue"`.
  - `PersonalScenarioGetsUserFitBoost`: a personal scenario with strong accept history scores above the same seed scenario.
  - `TypingAddsInterruptionPenalty`: typing lowers the score enough to suppress a borderline candidate.

- [ ] 5. Expand local signal extraction in the proactive engine.

  Replace the current coarse content analysis result with structured local signals. The JavaScript must stay local and must not call the model or send page content.

  Add DOM counts:

  ```js
  const body = document.body;
  const text = body ? body.innerText || '' : '';
  const count = (selector) => body ? body.querySelectorAll(selector).length : 0;
  const active = document.activeElement;
  const isTyping = !!active && (
    active.tagName === 'TEXTAREA' ||
    active.tagName === 'INPUT' ||
    active.getAttribute('contenteditable') === 'true'
  );
  const metaDescription =
    document.querySelector('meta[name="description"]')?.content || '';

  return {
    wordCount,
    charCount,
    lang,
    title: document.title || '',
    url: location.href,
    metaDescription,
    hasCode: count('pre, code, .blob-code, .diff, [class*="diff"]') > 0,
    codeBlockCount: count('pre, code, .blob-code, .diff, [class*="diff"]'),
    tableCount: count('table, [role="table"], [class*="table"]'),
    formCount: count('form'),
    buttonCount: count('button, [role="button"]'),
    linkCount: count('a[href]'),
    headingCount: count('h1,h2,h3,h4,h5,h6'),
    passwordInputCount: count('input[type="password"]'),
    paymentInputCount: count('input[autocomplete*="cc-"], input[name*="card"], input[id*="card"]'),
    isTyping,
  };
  ```

  Add C++ parsing helpers in `dao_agent_proactive_engine.cc` or a small anonymous-namespace helper in `dao_agent_proactive_ranker.cc`:

  - `BuildSignalsFromAnalysisResult(url, domain, result)`
  - `IsSensitivePage(signals)`
  - `IsSignificantContent(signals)`

  Sensitive heuristics for the first version:

  - URL host or path contains `bank`, `billing`, `checkout`, `payment`, `password`, `security`, `account`, `settings`, `inbox`, `mail`, `messages`, `health`, `medical`, or `patient`.
  - `password_input_count > 0`.
  - `payment_input_count > 0`.

- [ ] 6. Change engine flow from single match to candidate ranking.

  In `DaoAgentProactiveEngine`:

  - Replace `scenario_registry_.Match(url)` with `scenario_registry_.GetMatchingScenarios(url)`.
  - If there are no matching scenarios, keep the existing legacy episode fallback.
  - If there are matching scenarios, run local signal extraction once.
  - Convert scenarios into `ProactiveCandidate` objects.
  - Query `GetCooldownScore(domain, scenario.id)` for each candidate.
  - Rank every candidate with the same signals and its own feedback signals.
  - Select the highest final score decision with `tier == kAgentPanelCard`.
  - Do not fall back to legacy episode suggestions when scenario candidates exist but are suppressed; suppression should keep the UI quiet.
  - Keep the session dedup rule for `(url, scenario_id)`.
  - Add an in-memory map keyed by `(domain, action_label)` to enforce the 10-minute domain/action gap.

  Replace the unstable main-frame routing ID tab handle with Chromium's session tab ID:

  ```cpp
  #include "components/sessions/content/session_tab_helper.h"

  int tab_id = sessions::SessionTabHelper::IdForTab(web_contents).id();
  ```

  Update `HandleGetPageContentForScenario` to find tabs by `SessionTabHelper::IdForTab(wc).id()` instead of `GetPrimaryMainFrame()->GetRoutingID()`.

  When emitting a `ProactiveSuggestion`, populate:

  - `text`
  - `confidence`
  - `action_type`
  - `scenario_id`
  - `scenario_name`
  - `action_label`
  - `action_prompt`
  - `requires_page_content`
  - `tab_id`
  - `reason`
  - `expected_outcome`
  - `context_disclosure`
  - `score_debug_json`
  - `url`
  - `domain`

- [ ] 7. Make thresholds apply to scenario suggestions.

  Keep the existing `SetConfidenceThreshold(double threshold)` API, but use it as `panel_threshold` for all ranked scenario suggestions.

  Update `CONFIDENCE_THRESHOLD_MAP` in `agent_bridge.ts`:

  ```ts
  export const CONFIDENCE_THRESHOLD_MAP: Record<string, number> = {
    quiet: 0.85,
    balanced: 0.75,
    active: 0.60,
  };
  ```

  The engine default remains balanced, but should become `0.75`.

- [ ] 8. Repair feedback semantics with the existing `action_feedback` table.

  Do not add a new `proactive_events` table in this first implementation. Use the existing `ActionFeedback` table and broaden outcome handling safely.

  Update `GetCooldownScore()`:

  - Treat `accepted`, `clicked`, `completed`, and `helpful` as positive outcomes for the last-positive timestamp.
  - Apply contributions since the last positive:
    - `dismissed`: `1.5`
    - `not_now`: `1.0`
    - `ignored`: `0.5`
    - `not_helpful`: `1.0`
    - `failed`: `0.25`
  - Keep the existing 7-day decay.

  Update `HandleRecordActionFeedback()` in `dao_agent_ui.cc`:

  - If `outcome == "shown"` and `scenario_id` is non-empty, call `UpdateScenarioStats(scenario_id, "times_triggered")`.
  - Record the feedback after the stat bump request is dispatched.

  Update `HandleAcceptSuggestion()`:

  - Store outcome `accepted` instead of `clicked`.
  - Keep compatibility in `GetCooldownScore()` for old `clicked` rows.
  - Continue bumping `times_accepted`.

  Keep `HandleDismissSuggestion()` as the strong suppression path:

  - Store outcome `dismissed`.
  - Continue bumping `times_dismissed`.

- [ ] 9. Serialize the richer suggestion payload to WebUI.

  Update `DaoAgentMemoryHandler::OnProactiveSuggestion()` to include:

  - `reason`
  - `expectedOutcome`
  - `contextDisclosure`
  - `suppressionReason`
  - `scoreDebugJson`
  - `url`
  - `domain`

  Update `ProactiveSuggestionData` in `agent_bridge.ts` with the same fields.

  Update `normalizeProactiveSuggestion_()` in `dao_chat_view.ts`:

  - Preserve the current legacy episode behavior.
  - Accept scenario suggestions only when they have either `scenarioName` or `text`.
  - Default missing `reason`, `expectedOutcome`, and `contextDisclosure` to empty strings.
  - Use native-provided `url` and `domain` in feedback. Keep `fetchCurrentPageInfo()` only as a fallback when these fields are empty.

- [ ] 10. Redesign the Agent panel card copy and actions.

  Update the proactive card in `dao_chat_view.ts`:

  - Show title.
  - Show reason when present.
  - Show expected outcome when present.
  - Show context disclosure.
  - Keep the cost hint, but make it secondary.
  - Buttons:
    - `Run`
    - `Not now`
    - `Don't suggest this here`

  Add separate handlers:

  - `notNowProactiveSuggestion_()`
    - Clears the visible card.
    - Calls `recordActionFeedback` with `outcome: "not_now"`.
    - Does not send a message.
    - Does not bump `times_dismissed`.
  - `dismissProactiveSuggestion_()`
    - Clears the visible card.
    - Calls `dismissSuggestion` with structured feedback.
    - Does not send a message.
  - `recordProactiveFeedback_(suggestion, outcome)`
    - Uses `recordActionFeedback` for `shown`, `not_now`, `ignored`, `completed`, and `failed`.

  Add ignored tracking:

  - When a new suggestion is stored, clear any previous ignored timer.
  - Record `shown` immediately after storing the card.
  - Start a 120-second timer.
  - If the same suggestion is still visible when the timer fires, record `ignored` and clear the card.
  - Clear the timer when the user clicks Run, Not now, or Don't suggest this here.

- [ ] 11. Make Run prompt transparent.

  Update `buildProactivePayload_()` so the visible user message is no longer only `Run suggestion: {title}`.

  Required visible shape:

  ```text
  Review this PR.

  Why Dao suggested this:
  - Looks like a GitHub PR with a sizeable diff.

  Expected outcome:
  - Summarize the changes and flag likely review risks.
  ```

  Use localized string pieces for the visible transcript. Do not hardcode user-facing English or Chinese directly in the TypeScript body.

  Keep page content in the document attachment. The attachment may contain the full prompt and truncated page content, but it must still be built only after Run.

  On success:

  - Call `acceptSuggestion` before sending, as today.
  - After `iface.sendMessage(...)` resolves, record `completed`.

  On failure:

  - Record `failed`.
  - Show the existing proactive failure toast.
  - Keep the suggestion visible only if the failure happened before page content capture or send. Clear it after a successful send.

- [ ] 12. Add i18n strings.

  Add English strings in `en.ts` and hand-authored Chinese strings in `zh-CN.ts`.

  Keys:

  - `chat.proactive.not_now`
  - `chat.proactive.not_now_aria`
  - `chat.proactive.never_here`
  - `chat.proactive.never_here_aria`
  - `chat.proactive.reason_label`
  - `chat.proactive.expected_outcome_label`
  - `chat.proactive.context_label`
  - `chat.proactive.visible_prompt_reason_header`
  - `chat.proactive.visible_prompt_expected_header`
  - `chat.proactive.default_context_disclosure`
  - `chat.proactive.default_expected_outcome`

  Keep existing keys for compatibility where tests or legacy suggestions still use them.

- [ ] 13. Update WebUI tests.

  In `dao_chat_view.test.ts`, update existing proactive tests and add focused cases:

  - `stores a proactive suggestion without running native page capture`
    - Assert no `getPageContentForScenario` call until Run.
    - Assert `recordActionFeedback` is called with `outcome: "shown"`.
  - `renders proactive reason outcome context and three actions`
    - Render card text and assert localized reason/outcome/context labels and action keys.
  - `not now records timing feedback without sending a message`
    - Click or call `notNowProactiveSuggestion_()`.
    - Assert `recordActionFeedback` with `outcome: "not_now"`.
    - Assert `originalSend` not called.
  - `dismisses a proactive scenario as strong suppression`
    - Keep existing `dismissSuggestion` assertion.
    - Assert outcome is not `not_now`.
  - `runs a proactive scenario only after click and truncates page text`
    - Update expected visible user text to include reason and expected outcome.
    - Keep page-content truncation assertion.
  - `records failed when page capture fails`
    - Mock `getPageContentForScenario` returning `{error: "tab not found"}`.
    - Assert `recordActionFeedback` with `outcome: "failed"`.
  - `records ignored when the card expires`
    - Use fake timers.
    - Assert `recordActionFeedback` with `outcome: "ignored"`.

- [ ] 14. Update C++ tests.

  Add or update tests in `dao_browser_browsertest.cc`:

  - `GetMatchingScenariosIncludesSeedAndPersonalOnConflict`
  - `GetMatchingScenariosOrdersPersonalByAcceptanceRate`

  Add ranker tests in `dao_agent_proactive_ranker_unittest.cc` as listed in Task 4.

  If `DaoAgentMemoryStoreTest` has an active test fixture suitable for SQLite feedback tests, add:

  - `CooldownTreatsAcceptedAsPositiveReset`
  - `CooldownWeightsNotNowDismissedAndIgnored`

  If the existing store fixture is disabled because of profile-wide SQLite state, keep the cooldown behavior covered through ranker unit tests and a narrow browser test only after the store fixture is repaired.

- [ ] 15. Update BUILD integration.

  In `src/dao/browser/agent/BUILD.gn`:

  - Add new `.cc` files to the agent source target that currently owns proactive engine code.
  - Add `dao_agent_proactive_ranker_unittest.cc` to `source_set("unit_tests")`.
  - Add any required deps for JSON serialization if `score_debug_json` uses `base::JSONWriter`.

  Do not edit generated `engine/src/.../BUILD.gn` directly. `npm run rebuild` will run import.

- [ ] 16. Run verification.

  Run WebUI tests:

  ```bash
  npm run test:webui
  ```

  Expected output:

  ```text
  Test Files ... passed
  Tests ... passed
  ```

  Run Lit lint:

  ```bash
  npm run lint:lit
  ```

  Expected output: command exits with status `0`.

  Run compile confirmation:

  ```bash
  npm run rebuild
  ```

  Expected output: import completes and the debug build command exits with status `0`.

  Run focused browser tests after rebuild:

  ```bash
  engine/src/out/dao-debug/browser_tests --gtest_filter="DaoAgentScenarioRegistryTest.*:DaoAgentProactiveRankerTest.*:DaoAgentMemoryStoreTest.*"
  ```

  Expected output:

  ```text
  [  PASSED  ] ...
  ```

  Run whitespace check:

  ```bash
  git diff --check
  ```

  Expected output: no output and exit status `0`.

## Manual QA Checklist

- [ ] GitHub PR with substantial diff: Balanced shows one Agent panel card after dwell; card explains reason, expected outcome, and context disclosure; no page content is captured before Run.
- [ ] GitHub PR with minimal visible content: Balanced suppresses the card or gives a lower score in debug output.
- [ ] Documentation page with long text: Balanced shows a summary card; Quiet suppresses it.
- [ ] StackOverflow/Q&A page: card appears only when enough question/answer content is visible.
- [ ] Linear project page: card appears only with dashboard-like content and not repeatedly after Not now.
- [ ] Sensitive payment/password/account page: no proactive suggestion appears.
- [ ] Not now: same domain/action is quiet for the short term without increasing dismiss count.
- [ ] Don't suggest this here: future suggestions for that domain/action are strongly suppressed.
- [ ] Run: visible chat message explains the task; page content is attached only after click.
- [ ] Page capture failure: toast appears and `failed` feedback is recorded.

## First-Phase Non-Goals

- No sidebar nudge UI yet, even though the ranker has a tier enum.
- No new `proactive_events` table yet; the existing `ActionFeedback` table is enough for the first feedback repair.
- No automatic Dream habit triggers yet; Dream can consume better shown/accepted/dismissed counts after this phase.
- No automatic learning proposal UI yet; personal scenario override is supported by ranking, but proposal creation remains a later feature.
