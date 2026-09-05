# Command Bar Suggestion Quality Design

Status: Draft  
Last updated: 2026-09-05

Owner: Dao Browser UI

The September 5 update defines the first system-command batch in Phase 6.
That section takes precedence over the broader draft's command candidates,
ranking, and rollout dependencies. Other sections retain the June proposal;
their findings are historical, not an inventory of current behavior. The first
command batch is implemented in source; validation status is recorded in Phase
6.

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

### Phase 6: First System-Command Batch

Status: Implemented in source on 2026-09-05; `npm run rebuild` passed after
the final fixes. The added browser tests have not been compiled or run, and
manual UI validation remains unverified.

Add exactly eight system commands to the existing command bar. The order below
is the initial discovery order, preserving the selected priority list and
appending Task Manager. Matching relevance takes precedence while filtering.

| Order | English title | English aliases | Execution |
|---|---|---|---|
| 1 | Open Settings | `settings`, `preferences` | Open the existing Settings overview through `IDC_OPTIONS`. |
| 2 | Reopen Closed Tab | `reopen closed tab`, `reopen tab`, `restore tab`, `undo close tab` | Use `IDC_RESTORE_TAB`, including the existing restoration behavior for closed windows. |
| 3 | Copy Current Link | `copy link`, `copy url`, `copy current link` | Reuse the existing Dao copy-link action and localized `DaoToastView` feedback. |
| 4 | Open Downloads | `downloads`, `download history` | Open the full downloads page through `IDC_SHOW_DOWNLOADS`. |
| 5 | Open History | `history`, `browsing history` | Open browsing history through `IDC_SHOW_HISTORY`. |
| 6 | Manage Extensions | `extensions`, `manage extensions`, `addons` | Open extension management through `IDC_MANAGE_EXTENSIONS`. |
| 7 | Open Agent Settings | `agent settings`, `ai settings`, `model settings` | Open the existing `dao://settings/agent` page. |
| 8 | Open Task Manager | `task manager`, `taskmanager`, `process manager` | Open or focus Chromium's browser Task Manager through `IDC_TASK_MANAGER`. |

Task Manager uses the existing browser process UI, including its resource
columns and process controls. This command only opens that UI; it does not
terminate a process. Downloads opens the full record, not the sidebar flyout.
The restore command follows Chromium's existing restore stack; if the next
entry is a window or group, use the corresponding localized restoration title
without menu mnemonics. The displayed title also participates in matching, so
accepting it with Tab or Right Arrow keeps the restore command available.

#### Discovery and matching

- Normal input mixes matching commands into the existing suggestions.
- The **Enable > command mode** toggle in **You and Dao** settings defaults to
  off and is independent of enhanced suggestions. When off, `>` remains
  ordinary input and normal command suggestions remain available.
- When the toggle is enabled, a leading `>` enters explicit command mode:
  `>` lists all eight commands;
  `> settings` filters commands only. Do not send command-mode input to search
  providers or show Search / Ask AI rows in this mode.
- Blank and whitespace-only normal input continues to show zero suggestions.
  Keep the URL/search placeholder; explain the `>` prefix in the setting.
- Match the complete localized title and curated aliases, with English aliases
  available in every locale. Normalize case and surrounding/repeated whitespace.
  Use exact and whole-phrase prefix matching, without fuzzy or AI matching.
- In normal mode, require at least two characters for prefix suggestions.
  `set` can suggest Settings; `task m` can suggest Task Manager;
  `settings sync tutorial` remains a search. URL-shaped input retains navigation
  priority and does not trigger a command from a word inside the URL.
- Provide localized titles, aliases, intent labels, placeholders, and disabled
  reasons through `dao_strings.grd`. Hand-author the Simplified Chinese entries;
  keep the existing manual translation workflow for other locales.

#### Ordering and keyboard behavior

- A complete title/alias match for one of the six UI-opening commands is first
  and automatically selected. For example, `settings` + Enter opens Settings,
  and `task manager` + Enter opens Task Manager.
- Other matches appear immediately after the first existing suggestion, with
  at most two command rows in normal mode. Keep the existing non-command rows
  in relative order and retain the independent exact-input Search action.
- Reopen Closed Tab and Copy Current Link do not become the automatic selection
  in normal mode. Execute them after explicit selection or in command mode.
- In command mode, rank exact matches before prefix matches, break ties by the
  table order, and select the first enabled command. There is no two-row cap.
- Reuse the current five-row scrollable viewport and row height. Each command
  row shows a Lucide icon, localized action title, and localized `Command` label.
- Arrow keys select; Enter or a click executes the selected enabled command
  once; Tab / Right Arrow fills its title without executing; Escape dismisses.
  Accepting a title keeps `>` when already in command mode.
- Preserve explicitly selected rows during async autocomplete updates,
  including Search, URL, and Ask AI rows. Commands use their stable command ID.
  If the selected command becomes unavailable, do not silently substitute
  another executable command under the selection.

#### Execution and availability

- Reuse existing browser command dispatch, enabled-state checks, page-opening
  helpers, and Dao feedback. Do not build a second settings or process UI.
- Page-opening commands follow the existing Profile, Guest, and incognito
  routing rules. Reuse an eligible destination tab through existing helpers
  where supported; avoid a custom cross-window tab registry. Agent Settings
  should use the same Settings navigation helper and command availability
  check for its subpage.
- Bind Copy Current Link to the tab present when the command bar opened,
  including when opened with Cmd+T. Revalidate that target at execution; if it
  disappeared, keep the clipboard unchanged and show localized feedback.
- Executing a command exits new-tab mode without creating an extra blank tab.
  Clear the command bar and its sidebar highlight before opening/focusing the
  destination, so command-bar cleanup cannot steal destination focus.
- Hide unavailable commands in normal mode. In command mode, show them disabled
  with a short reason, skip them in keyboard selection, and block execution.
  Reuse Chromium's restore-service loading behavior rather than treating a
  not-yet-loaded restore stack as empty. Recheck availability on execution.

#### Implementation boundary and acceptance

Use a small static command catalog with stable ID, localized title/aliases,
icon, availability, and execution mapping. Extend the existing suggestion
rendering and selection path only as needed. This batch works in both existing
suggestion preference modes; it does not depend on Phase 5's general pipeline,
site search, zero-suggest, or usage-based ranking. Normal command suggestions
are always available; only the explicit `>` mode requires its opt-in preference.

Focused acceptance cases:

- Exact English and localized keywords open the intended UI; `settings` and
  `agent settings` resolve independently; prefix and sentence queries retain
  normal search behavior and the exact-input Search row.
- Command mode defaults off: `> task` uses ordinary autocomplete. Enabling the
  setting activates command mode; disabling it restores ordinary input. Verify
  both enhanced suggestion modes and normal command matching in every state.
- With command mode enabled, `>` exposes all eight commands within the
  five-row scrolling viewport;
  command-mode typing makes no autocomplete-provider request, and removing `>`
  restores normal suggestions.
- Copy and restore require explicit selection in normal mode; unavailable
  commands cannot run; async results cannot change a selected command's identity.
- Cmd+L and Cmd+T execute the intended command without an extra blank tab or
  focus theft. Copy uses the original live tab and shows the existing toast.
- Task Manager opens/focuses its existing window; Downloads opens the full
  page; management commands respect existing browsing-mode restrictions.
- Verify localized labels, keyboard navigation, and disabled reasons in light
  and dark themes. Update `docs/features.md` and `docs/feature-checklist.md`
  when implementing the behavior, then use the focused command-bar tests and
  `npm run rebuild` for compile confirmation.

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
