# Command Bar System Commands Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the eight approved browser actions to Dao's existing Command Bar.

**Architecture:** Use a static command catalog and explicit display-row identities alongside Chromium autocomplete matches and Ask AI. Reuse browser command dispatch, Settings subpage navigation, the tab-copy action, and DaoToastView; keep command-mode input out of autocomplete providers.

**Tech Stack:** Chromium C++, Views, GRIT, existing Chromium browser tests.

**Spec:** `docs/command-bar-suggestion-quality.md`, Phase 6: First System-Command Batch.

## Global Constraints

- Add exactly eight system commands to the existing command bar.
- This batch works in both existing suggestion preference modes.
- Reuse the current five-row scrollable viewport and row height.
- In normal mode, require at least two characters for prefix suggestions.
- Other matches appear immediately after the first existing suggestion, with at most two command rows in normal mode.
- Keep the existing non-command rows in relative order and retain the independent exact-input Search action.
- Do not send command-mode input to search providers or show Search / Ask AI rows in this mode.
- Bind Copy Current Link to the tab present when the command bar opened, including when opened with Cmd+T.
- Revalidate that target at execution; if it disappeared, keep the clipboard unchanged and show localized feedback.
- Never edit files under `engine/` directly.
- For compile confirmation, use only `npm run rebuild`.
- Batch related C++, header, resource, and test edits before building.
- Do not create git worktrees or feature branches or run state-changing git commands.
- Keep English source, comments, tests, and documentation; localize all UI text with hand-authored zh-CN translations. Do not run `i18n.sh`.
- Fetch current upstream SVG child nodes verbatim when adding Lucide icons.
- Use the shared `dao::DaoToastView` for lightweight native feedback.
- No additional dependency, ranking framework, preference, zero-suggest, or usage-based ranking.

---

### Task 1: Integrate the first system-command batch

**Files:**

- Modify: `src/dao/browser/ui/views/dao_command_bar_view.{h,cc}` for the static catalog, matching, display rows, target lifetime, and execution.
- Modify: `src/dao/browser/ui/views/dao_suggestion_item_view.{h,cc}` for command icons, labels, enabled state, and accessibility.
- Modify: `src/dao/browser/ui/views/dao_tab_commands.h` and `src/patches/chrome/browser/ui/views/frame/browser_native_widget_mac.mm.patch` to share the existing copy action with the command bar.
- Modify: `src/dao/browser/ui/views/dao_lucide_icons.{h,cc}` only for missing command icons.
- Modify: `src/dao/browser/strings/dao_strings.grd` and `src/dao/browser/strings/translations/dao_strings_zh-CN.xtb` for titles, aliases, hint, command label, disabled reasons, and copy feedback.
- Test: `src/dao/browser/ui/views/dao_browser_browsertest.cc` using the existing `DaoCommandBarBrowserTest` fixture.
- Modify: `docs/features.md`, `docs/feature-checklist.md`, and the Phase 6 status in `docs/command-bar-suggestion-quality.md`.

**Interfaces:**

- Consumes `chrome::IsCommandEnabled`, `chrome::ExecuteCommand`, `chrome::ShowSettingsSubPage`, `content::WebContents::GetWeakPtr`, and existing Command Bar selection/preview helpers.
- Produces system-command rows within `DaoCommandBarView`; no new external service or registry.
- `DaoSuggestionItemView::SetCommand` takes the localized title, Lucide icon, localized disabled reason, and enabled state. `SetMatch` and `SetAskAiPrompt` must reset command-specific state when reusing a row.
- A shared tab-copy helper takes the explicit `WebContents*` target and browser for toast feedback, validates the target before touching the clipboard, and returns whether it copied.

- [x] **Step 1: Write focused browser tests before implementation.**

Use existing testing setters and visible labels, avoiding a parallel matching implementation in the tests. A command-mode provider-isolation test starts as follows:

```cpp
IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest,
                       SystemCommandsBypassAutocompleteProviders) {
  auto* command_bar = GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);
  command_bar->ShowForNewTab();
  const int starts = command_bar->GetAutocompleteStartCountForTesting();
  command_bar->ContentsChanged(nullptr, u">");
  EXPECT_EQ(starts, command_bar->GetAutocompleteStartCountForTesting());
  EXPECT_EQ(8, command_bar->GetVisibleSuggestionCountForTesting());
  EXPECT_EQ(-1, command_bar->GetAskAiRowIndexForTesting());
  command_bar->ContentsChanged(nullptr, u"> task");
  EXPECT_EQ(starts, command_bar->GetAutocompleteStartCountForTesting());
  EXPECT_EQ(1, command_bar->GetVisibleSuggestionCountForTesting());
}
```

Cover the same behavior with both enhanced-suggestion preference values. Additional tests must observe: normalized exact/prefix matching, rejected sentence/URL matches, original Search preservation, six safe defaults versus copy/restore explicit selection, Tab/Right accepting without execution, stable command identity across provider updates, disabled keyboard skipping, no extra tab on execution, original-tab copy after active-tab changes, unchanged clipboard when the target closes, and Task Manager opening/focusing its existing UI. Use English or localized resource lookups in tests; localized matching can exercise a resource alias without embedding Chinese text in C++.

The existing `browser_tests` binary may be stale. Check the requested test list before executing it. Record an unavailable red/green run honestly; do not treat zero matching tests as a pass. The repository requires batching C++ edits before the single permitted compile path, so do not build a separate red-only browser-test target.

- [x] **Step 2: Implement catalog, matching, and row identity.**

Read Phase 6 in the spec for the exact command order, English aliases, and dispatch mapping. Keep matching deterministic: Unicode lowercase, collapse whitespace, compare whole normalized titles/aliases for equality or prefix. Exact matches rank before prefixes, with catalog order as the tiebreaker. Localized aliases supplement English aliases in every locale.

Represent displayed row kinds explicitly instead of increasing the existing Ask AI offset arithmetic. A compact structure can be:

```cpp
struct SuggestionRow {
  enum class Kind { kAutocomplete, kAskAi, kCommand };
  Kind kind;
  int index;  // Match index or stable command catalog index.
};
```

Maintain the existing `visible_matches_` for provider results. Route rendering, selected-match lookup, preview, acceptance, and execution through the displayed row kind. `>` lists all commands and filters without calling `StartAutocomplete`; normal empty input still clears suggestions. Normal matches are capped at two, while the five-row viewport scrolls over the full result list. Insert safe exact UI actions at the front; insert remaining commands after the first existing row, preserving all non-command order.

- [x] **Step 3: Implement command execution and keyboard behavior.**

Capture the live tab on both opening paths, before any new-tab UI state changes:

```cpp
command_target_ = contents ? contents->GetWeakPtr()
                           : base::WeakPtr<content::WebContents>();
```

Guard against asynchronous provider callbacks after entering command mode. Preserve explicitly selected command identity, not its old display position. If the selected command vanishes or becomes unavailable, clear executable selection until the user selects another row. Normal-mode copy/restore never become implicit defaults, including after result list changes.

Enter/click dispatches only an enabled selected command and only once; Tab/Right fills the localized title, retaining `>` in command mode. Arrow navigation skips disabled entries. Recheck enabled state immediately before executing, retain Chromium restore-service loading behavior, and use a window-restore title for a window entry. Hide/reset the Command Bar before focusing a destination, including the pre-new-tab highlight and native content-event interception. Use the browser's normal Profile/Guest/incognito routing.

Factor the native menu's existing copy implementation into the shared tab helper, replacing its hardcoded toast with a localized message. Pass the opening-time target from the Command Bar and the current target from the native menu. A lost target must never fall back to the new active tab.

- [x] **Step 4: Localize and update regression documentation.**

Provide complete localized command titles, pipe-delimited localized matching aliases, the `Command` intent, `Type > for commands` placeholder hint, and action-specific unavailable/error feedback. Add hand-authored zh-CN translations using GRIT's message fingerprint helper. Reuse Lucide icons where semantically suitable; fetch upstream SVG data for missing icons. Disabled rows need muted styling and accessible disabled state/name; reset both when reused as ordinary suggestions.

Document normal input versus `>` behavior and eight commands in `docs/features.md`. Correct its stale `ShowForNewTab(prev)` statement: the current code defers creating a tab until actual navigation. Add one focused checklist row for command-mode matching, both preference modes, keyboard behavior, original-tab copy, disabled rows, routing, and Task Manager. Mark Phase 6 implemented only after code exists; distinguish validation limitations.

- [x] **Step 5: Verify and review without committing.**

Run canonical diff/format and resource checks before requesting the controller's build. The controller runs:

```bash
npm run rebuild
git diff --check
```

Run only the relevant `DaoCommandBarBrowserTest.*` cases from a binary that actually contains the new tests. A missing/stale binary is a validation limitation, not permission to use another build command. Report changed files, test commands and observed outcomes, red/green limitations, and self-review findings to the controller. The controller performs independent task review and final whole-change review before delivery. No commit, push, PR, engine edit, or forced import is authorized.

**Verification record (2026-09-05):** The final `npm run rebuild` passed after
the complete fix wave (23.18 seconds, six build steps, exit 0). Scoped formatting,
resource parsing and identifier checks, documentation checks, and
`git diff --check` passed. Independent task review and final whole-change review
completed; the three final Important findings were fixed in one wave and the
scoped re-review found all three addressed with no new Important or Critical
issues.

All 13 new browser tests remain uncompiled and unrun because the available test
binary is stale and a separate test build is outside the permitted compile
path. Manual UI behavior remains unverified; desktop control was stopped at
the user's request. Review covered uncommitted working-tree changes. No commit,
push, or PR was created.
