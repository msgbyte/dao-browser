# Command Bar Suggestion Quality Design

Status: Draft  
Last updated: 2026-06-28  
Owner: Dao Browser UI

## Summary

Dao's command bar is visually close to an Arc-style command surface, but its suggestion behavior still feels closer to a narrow Chromium omnibox wrapper. It can navigate and search, but it does not yet consistently understand tabs, site-search shortcuts, browser commands, AI prompts, and current-page context as first-class suggestion types.

This design upgrades command bar suggestions in stages. The low-risk first stage keeps existing behavior unchanged by default and introduces an enhanced suggestion mode behind a default-off preference. The enhanced mode expands Chromium omnibox provider coverage, uses the current page as autocomplete context, makes Ask Dao conditional instead of always inserting it near the top, aligns Enter behavior with the visible selected row, and adds intent labels such as `Search`, `Open`, `Switch Tab`, and `Ask Dao`.

The longer-term design adds a Dao-owned suggestion aggregation layer. That layer normalizes omnibox matches, tabs, browser commands, site-search shortcuts, and agent actions into one ranked list with predictable execution semantics.

## Goals

- Make command bar suggestions feel context-aware, action-capable, and keyboard-trustworthy.
- Preserve Dao's AI-first identity without making Ask Dao noisy for navigational input.
- Use Chromium omnibox providers where they are strong instead of reimplementing search/history ranking.
- Add Dao-owned ranking and rendering rules where Chromium does not know Dao concepts.
- Keep the rollout safe through a default-off enhanced suggestion preference.
- Keep all user-visible text localized through `dao_strings.grd`.

## Non-Goals

- Do not clone Arc one-to-one.
- Do not replace Chromium's omnibox stack.
- Do not introduce ML ranking in this phase.
- Do not edit `engine/` directly.
- Do not add user-visible strings directly in C++.
- Do not change default command bar behavior until enhanced mode is validated.

## Arc Reference Behaviors

Arc's public docs show Command-T as more than URL/search autocomplete:

- Site Search runs from Command-T through custom shortcuts followed by Tab.
- Arc Max can add ChatGPT to search suggestions and supports Instant Links with Shift-Enter.
- Command-T can launch browser actions such as View Archive, clearing archive, new blank window, Developer Mode, and Split View actions.
- Arc privacy docs describe search suggestions coming from the default search engine and ChatGPT command bar sharing only when enabled.

References:

- https://resources.arc.net/hc/en-us/articles/20855018192791-Site-Search-Directly-Search-any-Website
- https://resources.arc.net/hc/en-us/articles/19335160678679-Arc-Max-Boost-Your-Browsing-with-AI
- https://resources.arc.net/hc/en-us/articles/19228855311127-Auto-Archive-Clean-as-you-go
- https://resources.arc.net/hc/en-us/articles/20468488031511-Developer-Mode-Instant-Dev-Tools
- https://resources.arc.net/hc/en-us/articles/19335393146775-Split-View-View-Multiple-Tabs-at-Once
- https://arc.net/privacy

## Current Dao Findings

Relevant files:

- `src/dao/browser/ui/views/dao_command_bar_view.h`
- `src/dao/browser/ui/views/dao_command_bar_view.cc`
- `src/dao/browser/ui/views/dao_suggestion_item_view.h`
- `src/dao/browser/ui/views/dao_suggestion_item_view.cc`
- `src/dao/browser/ui/views/dao_browser_browsertest.cc`
- `src/dao/browser/strings/dao_strings.grd`

Current command bar suggestions rely on a restricted provider set:

- `TYPE_HISTORY_QUICK`
- `TYPE_HISTORY_URL`
- `TYPE_BOOKMARK`
- `TYPE_SEARCH`
- `TYPE_SHORTCUTS`
- `TYPE_OPEN_TAB`

Chromium's default desktop omnibox provider set is broader and can include keyword/site search, zero-suggest, built-in commands, most-visited sites, calculator, contextual search, history fuzzy, and optional history embeddings.

Observed gaps:

- Empty input hides suggestions instead of showing useful contextual suggestions.
- The first visible row can look selected, but Enter does not always submit that row.
- Ask AI is inserted near the top for all non-URL input, including short navigational prefixes.
- Suggestion rows do not clearly communicate whether the action is search, open, switch tab, site search, command, or Ask Dao.
- Browser actions are not represented as command bar suggestions.
- Current-page context is underused when starting autocomplete.

## Product Principles

### One command surface, many object types

The command bar should search across pages, tabs, browser actions, Dao agent actions, site-search shortcuts, and settings. Users should not need to remember which surface owns a browser action.

### The visual default is the execution default

If a row is highlighted, Enter should execute that row. If typed text will be submitted instead, the UI should avoid implying a row will win.

### AI should be intentional

Ask Dao should rank high when the user writes a question or task, but it should stay out of the way for short navigational prefixes such as `gi`, `yt`, `fig`, or exact tab/site-search matches.

### Chromium ranks the web, Dao ranks Dao

Chromium should continue to provide search/history/bookmark/open-tab intelligence. Dao should add the product-specific layer: commands, agent actions, split view, sidebar objects, intent labels, and final cross-source ranking.

### Roll out safely

Enhanced suggestions should start behind a default-off preference. The existing command bar behavior remains the fallback until the enhanced path has enough test and dogfood confidence.

## Proposed Design

### Phase 0: Enhanced Mode Behind a Preference

Add a Dao-owned preference:

```text
dao.enhanced_command_bar_suggestions_enabled
```

Default: `false`

When disabled, the current command bar behavior remains unchanged. When enabled, the command bar uses the enhanced suggestion path:

- Broader omnibox provider set.
- Current URL/title context in autocomplete input.
- Empty-input autocomplete in new-tab mode.
- Conditional Ask Dao insertion.
- Enter executes the selected visible suggestion.
- Intent labels are shown in suggestion rows.

The preference should be exposed in `dao://settings/dao` under the existing Dao settings surface so dogfooding can be turned on without command-line flags.

### Phase 1: Better Use of Chromium Omnibox

Use `AutocompleteClassifier::DefaultOmniboxProviders()` for enhanced mode instead of the hand-picked narrow set.

When starting autocomplete in enhanced mode:

- Set the focus type to `metrics::OmniboxFocusType::INTERACTION_FOCUS`.
- Provide the active page URL through `AutocompleteInput::set_current_url`.
- Provide the active page title through `AutocompleteInput::set_current_title`.
- Preserve existing inline-completion behavior and deletion suppression rules.

This gives Dao access to provider behavior Chromium already implements, including keyword/site-search and contextual result types, while allowing Dao to filter or render them more carefully later.

### Phase 2: Conditional Ask Dao

Replace fixed-position Ask AI insertion with query-shape classification.

Ask Dao should be visible or high-ranked for:

- Questions.
- Task-like requests.
- Long natural-language phrases.
- Explicit AI/Dao commands.

Ask Dao should be hidden or low-ranked for:

- Likely URLs.
- Very short navigational prefixes.
- Strong tab/title/domain matches.
- Exact site-search keyword matches.

The first implementation can use deterministic heuristics:

- Question marks, including full-width question marks.
- Whitespace-separated phrases.
- English question/task words such as `how`, `what`, `why`, `summarize`, `compare`, `explain`, and `write`.
- URL-like detection already present in `DaoCommandBarView`.

This is intentionally simple and testable. It can be replaced by a richer classifier later if needed.

### Phase 3: Selection Semantics

The command bar should obey one rule:

```text
If a visible row is selected, Enter executes that row.
```

Typed-text navigation/search is the fallback only when there is no selected visible suggestion. This removes the current mismatch where the UI can highlight a row but submit raw input instead.

Right Arrow and Tab behavior for inline completion should remain separate from Enter submission.

### Phase 4: Intent-Aware Rendering

Update `DaoSuggestionItemView` so each row can show:

- Left icon.
- Primary title.
- Secondary description.
- Right-aligned intent label.
- Optional future keyboard hint.

Initial intent labels:

- `Search`
- `Open`
- `Switch Tab`
- `Ask Dao`

Future labels:

- `Command`
- `Site Search`
- `Setting`

Intent labels must come from `src/dao/browser/strings/dao_strings.grd`.

Layout requirements:

- Row height should remain stable.
- Title gets primary width.
- Description truncates cleanly.
- Intent label never overlaps title or description.
- Default mode should not suddenly change row density or labels unless explicitly desired.

### Phase 5: Normalized Suggestion Model

Add a Dao-owned model representing every row independently of source.

Proposed file:

- `src/dao/browser/ui/views/dao_command_suggestion.h`

Core fields:

- `id`: stable per-refresh identifier for tests and click handling.
- `source`: `kOmnibox`, `kDaoAction`, `kSiteSearch`, `kAgent`, `kOpenTab`, `kHistoryZeroSuggest`.
- `action`: `kNavigate`, `kSearch`, `kSwitchToTab`, `kRunCommand`, `kAskAgent`, `kEnterKeywordMode`.
- `title`: primary row text.
- `description`: secondary row text.
- `intent_label`: short localized label.
- `icon`: favicon, Lucide icon, or omnibox vector icon.
- `score`: Dao-owned rank score after source-specific normalization.
- `omnibox_match_index`: index into `AutocompleteResult` when wrapping Chromium.
- `destination_url`: target URL for navigation/search suggestions.
- `command_id`: Dao command id for browser actions.
- `payload`: small structured payload for command-specific execution.

Once this model exists, `DaoCommandBarView` should render and execute normalized suggestions rather than manually mapping visible rows back into omnibox results.

### Phase 6: Dao Command Registry

Add a command registry for browser actions.

Proposed files:

- `src/dao/browser/ui/views/dao_command_registry.h`
- `src/dao/browser/ui/views/dao_command_registry.cc`

Initial command candidates:

- Open settings.
- Open downloads.
- Copy current URL.
- Copy current URL as Markdown.
- Add right split.
- Add left split.
- View archive.
- Clear archive.
- New blank window.
- Toggle Developer Mode for current site.

Matching can start with deterministic lowercased token prefix matching and common aliases:

- `settings`
- `downloads`
- `archive`
- `view archive`
- `clear archive`
- `split right`
- `split left`
- `developer mode`
- `copy url`

Command names and aliases that are user-visible must be localized. Non-visible matching aliases may be internal, but should be documented.

### Phase 7: Site Search and Context Suggestions

Use Chromium `TemplateURLService` and keyword provider behavior for site-search mode.

Expected user flow:

```text
shortcut + Tab + query
```

The command bar should make mode transitions obvious through row labels and keyboard hints.

Context suggestions for empty input or Cmd-L over an active page can include:

- Current page URL/title.
- Ask Dao about this page.
- Copy current URL.
- Add split with this page.
- Site-specific commands when available.

Zero-suggest should be handled carefully because it can show suggestions without typed input. It should be covered by tests and dogfooding before becoming default behavior.

## Ranking Design

Start with a deterministic ranker. Do not introduce ML.

Suggested rank order:

1. Exact command match.
2. Exact active/open tab title or URL match.
3. Exact site-search keyword match.
4. Omnibox default match.
5. Strong prefix match for pinned/favorite tabs.
6. Strong natural-language Ask Dao match.
7. Search suggestions.
8. History/bookmark tail matches.
9. Secondary commands.

Ranking features:

- Exact prefix bonus.
- Recently used command bonus.
- Active window/tab bonus.
- Pinned/favorite bonus.
- Question/task-shape bonus for Ask Dao.
- Short-input penalty for Ask Dao.
- Deletion penalty for inline autocompletion should continue to apply only within the current query lifetime.

## File Plan

Create later:

- `src/dao/browser/ui/views/dao_command_suggestion.h`
- `src/dao/browser/ui/views/dao_command_suggestion_ranker.h`
- `src/dao/browser/ui/views/dao_command_suggestion_ranker.cc`
- `src/dao/browser/ui/views/dao_command_registry.h`
- `src/dao/browser/ui/views/dao_command_registry.cc`

Modify:

- `src/dao/browser/ui/views/dao_command_bar_view.h`
- `src/dao/browser/ui/views/dao_command_bar_view.cc`
- `src/dao/browser/ui/views/dao_suggestion_item_view.h`
- `src/dao/browser/ui/views/dao_suggestion_item_view.cc`
- `src/dao/browser/ui/views/dao_browser_browsertest.cc`
- `src/dao/browser/dao_pref_names.h`
- `src/dao/browser/dao_pref_names.cc`
- `src/dao/browser/strings/dao_strings.grd`
- Relevant `src/patches/` files for settings UI and Chromium integration.

If new `.cc` files are introduced, update the tracked BUILD patch that owns Dao Views source inclusion.

## Rollout Plan

### Milestone 1: Safe Enhanced Mode

Ship the default-off preference and low-risk behavior improvements:

- Broader provider set in enhanced mode.
- Current page autocomplete context.
- Conditional Ask Dao.
- Enter matches visible selected row in enhanced mode.
- Intent labels in enhanced mode.
- Focused browser tests.

### Milestone 2: Normalized Suggestion Pipeline

Introduce `DaoCommandSuggestion` and route rendering/execution through it.

This milestone should make later sources easier to add without expanding row-index mapping logic in `DaoCommandBarView`.

### Milestone 3: Commands and Site Search

Add `DaoCommandRegistry`, command rows, and site-search mode UI.

This is where Dao starts to feel like a true browser command surface rather than only a smarter omnibox.

### Milestone 4: Contextual and Zero-Suggest Surfaces

Add empty-input and current-page suggestions after privacy, ranking, and keyboard behavior are proven in dogfood.

## Verification

Use the smallest relevant verification first.

Compile confirmation:

```bash
npm run rebuild
```

Focused command bar browser tests:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoCommandBarBrowserTest.*"
```

Focused examples:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoCommandBarBrowserTest.*Ask*:DaoCommandBarBrowserTest.*Rank*"
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoCommandBarBrowserTest.*Selection*:DaoCommandBarBrowserTest.*Enter*"
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoCommandBarBrowserTest.*Suggestion*:DaoCommandBarBrowserTest.*Intent*"
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoCommandBarBrowserTest.*Command*"
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoCommandBarBrowserTest.*Zero*:DaoCommandBarBrowserTest.*Context*"
```

Broader Dao smoke only when touching shared browser command, split-view, sidebar, or settings behavior:

```bash
npm run test
```

`npm run test` is test verification, not compile confirmation. If compilation must be confirmed, always run `npm run rebuild`.

## Risks

- More omnibox providers can surface match types the current row UI does not render well.
- Zero-suggest can expose suggestions without typed input, so privacy expectations must be clear.
- Changing Enter semantics can affect muscle memory; the selected row must visually communicate the action.
- Command rows can span many owners: sidebar, split view, downloads, settings, agent, and browser core.
- Localization work can lag behind UI work if strings are added outside the normal Dao string path.

## Open Questions

- Should enhanced suggestions eventually become default-on, or remain a user-facing experimental toggle?
- How much should Ask Dao rely on deterministic query-shape heuristics versus agent-side intent classification?
- Should command aliases be localized, or should aliases remain English-only until there is a broader command localization design?
- Which zero-suggest sources are acceptable before user typing: current tab only, history, search provider suggestions, or Dao commands?
- Should `Shift Enter` map to an Instant Link-style action in Dao, and if so, should it use search-result navigation, agent browsing, or a separate service?

## Design Decision Record

- Use Chromium omnibox providers as the web/search/history source.
- Add Dao-owned ranking and rendering instead of forcing all Dao concepts into omnibox providers.
- Keep enhanced behavior behind a default-off preference for safe dogfooding.
- Make Ask Dao conditional so AI feels helpful rather than intrusive.
- Make the selected visible row authoritative for Enter.
- Use localized intent labels to make action type scannable.

