# Experimental Command Bar Ranking Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship an independent, default-off suggestion-ordering experiment that preserves typed input and default submission.

**Architecture:** Reuse Chromium's existing autocomplete candidates and Dao's tab targets. Gate final row ordering, inline completion, selection previews, and implicit submission with one profile preference; leave the legacy path unchanged.

**Tech Stack:** Chromium C++ Views, Settings WebUI, GRIT localization, browser tests.

**Spec:** `docs/superpowers/specs/2026-11-05-command-bar-ranking-design.md`

## Global Constraints

- Work in the primary checkout on `main`; no state-changing Git commands.
- Never edit `engine/` directly; compile only through `npm run rebuild`.
- Default off, independent of enhanced suggestions and `>` mode.
- Hand-author English/Simplified Chinese only; do not invoke paid translation.

## Review Focus

- Late provider completion must not modify typed text or explicit selection.
- Same-address open tabs must not replace the typed Open/Search action.
- Exact commands must not silently become the normal-input Enter action.
- Disabling the preference restores legacy behavior without restarting.
- Both enhanced-provider modes retain the same experimental contract.

## Task 1: Preference, ordering, and original-input submission

**Files:** `dao_pref_names.{h,cc}`, `ui/views/dao_command_bar_view.{h,cc}`, `ui/views/dao_browser_browsertest.cc`, Dao Settings HTML/localized-string/allowlist/test patches, `strings/dao_strings.grd`, `strings/translations/dao_strings_zh-CN.xtb`, feature inventory/checklist and desktop changelogs.

**Interfaces:** `kDaoExperimentalCommandBarRankingEnabled` stores `dao.experimental_command_bar_ranking_enabled`; `ExperimentalRankingEnabled()` gates behavior; `RankExperimentalSuggestions()` transforms displayed rows while preserving deletion metadata; `CreateTypedInputMatch()` uses Chromium's input parser for the experiment's displayed and submitted typed action.

- [x] Add focused browser tests for opt-in/rollback, provider combinations, async selection, literal searches, typed URLs, and commands; add the Settings preference binding test.
- [x] Compile the tests with `npm run rebuild -- -- --target browser_tests` and run `DaoCommandBarBrowserTest.ExperimentalRanking*`; expect failure because the preference/behavior is absent.
- [x] Register/allowlist the preference, add localized Settings toggle, and implement the scoped behavior.
- [x] Update feature documentation and matching desktop Unreleased notes.
- [x] Run `npm run rebuild -- -- --target chrome browser_tests`, then the narrow command-bar tests and relevant Settings/localization checks. Record actual results and any environmental blockers.
- [x] Review the complete change with a fresh reviewer and fix material findings. Do not commit.

## Verification Record

- Red phase: the initial three experimental tests compiled and failed because
  the new preference was not registered.
- Settings localization checks: 16 tests passed. Documentation checks passed.
- Final compile: `npm run rebuild -- -- --target chrome browser_tests` passed.
- Initial browser regression run: `DaoCommandBarBrowserTest.*` passed all 87 tests,
  including the five new experimental-ordering tests, with one launcher job and
  retries disabled.
- English/Simplified Chinese message IDs and `git diff --check` passed.
- Review fixes preserve selected action identity across provider subtype changes,
  provider-backed deletion after deduplication, and local/non-HTTP address parsing.
- The Settings Mocha suite is already disabled in this checkout because its
  loader assumes the upstream WebUI scheme. Its new test compiles; runtime
  execution of that suite is not claimed.
