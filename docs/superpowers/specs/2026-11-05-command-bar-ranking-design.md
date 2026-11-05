# Experimental Command Bar Suggestion Ordering

The user requested an independent, default-off Settings toggle after discussing
tab prioritization and preserving typed input. Implementation is authorized in
the primary checkout; no branch, worktree, or Git mutation is authorized.

## Behavior

- Off: retain existing ordering, completion, and acceptance behavior.
- On: keep the typed action first (search text or open the typed address), then
  matching open tabs, available commands, Ask AI when enabled, and remaining
  Chromium suggestions. Preserve provider order within each category.
- Never automatically append completion text or replace input while browsing
  suggestions. Right Arrow remains caret movement. Tab explicitly accepts a
  suggestion after the user selects it; Enter/click executes it.
- Enter without explicit selection always submits the user's original input.
  Preserve explicit selection across asynchronous updates by action identity;
  a disappeared action falls back to the typed action.
- Preserve the separate exact-input Search action for address input. An existing
  tab at the typed address must not turn the default Open action into a switch.
  Use Chromium's input parser for the experiment's typed action, including local
  addresses and non-HTTP schemes. Preserve history deletion when merging rows.
- Keep `>` command mode explicit and independent. Enhanced suggestions continue
  to control Chromium providers, page context, and intent labels; all four
  combinations of enhanced suggestions and experimental ordering must work.

## Approaches

The earlier global relevance-boost proposal does not protect the user's default
action and can be overridden by Chromium grouping. The requested approach is a
Dao-only presentation and acceptance policy behind a separate preference,
reusing Chromium candidates and existing tab identity handling. It does not add
a second tab search provider or change Chromium's candidate limits.

## Delivery

Add localized English/Simplified Chinese settings copy, preference registration
and allowlisting, focused browser/settings tests, feature inventory/checklist
updates, and matching desktop Unreleased entries. Compile only with
`npm run rebuild`; never edit `engine/` directly or run paid translation.
