# Dream Duration Weighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Dream Analysis weight report emphasis by real browsing attention time, so long sessions are highlighted and brief visits are minimized.

**Architecture:** Replace the collector's history query with `HistoryService::GetAnnotatedVisits()` so each visit exposes `VisitRow::visit_duration` and `VisitContextAnnotations::total_foreground_duration`. Aggregate per domain with foreground seconds as the primary attention signal, keep privacy boundaries unchanged, and update the dream runner prompt to tell the LLM how to use the new fields.

**Tech Stack:** Chromium C++ history service, Dao browser tests, Lit/WebUI TypeScript, Vitest.

---

### Task 1: C++ Material Pack Duration Signal

**Files:**
- Modify: `src/dao/browser/agent/dao_dream_browsertest.cc`
- Modify: `src/dao/browser/agent/dao_dream_material_collector.h`
- Modify: `src/dao/browser/agent/dao_dream_material_collector.cc`

- [ ] **Step 1: Write the failing browser test**

Add a test named `CollectorAggregatesForegroundDuration` to `dao_dream_browsertest.cc`. It should add two history visits, set one visit's foreground duration to 45 minutes and another to 30 seconds, collect material, and assert:

```cpp
EXPECT_EQ("deep.example", (*domains)[0].GetDict().FindString("domain"));
EXPECT_EQ(2700, (*domains)[0].GetDict().FindInt("foreground_seconds"));
EXPECT_EQ("deep", *(*domains)[0].GetDict().FindString("duration_level"));
EXPECT_EQ("quick.example", (*domains)[1].GetDict().FindString("domain"));
EXPECT_EQ(30, (*domains)[1].GetDict().FindInt("foreground_seconds"));
EXPECT_EQ("light", *(*domains)[1].GetDict().FindString("duration_level"));
```

- [ ] **Step 2: Run the focused browser test to verify it fails**

Run: `engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDreamBrowserTest.CollectorAggregatesForegroundDuration"`

Expected: FAIL because `foreground_seconds` and `duration_level` are not present in the material pack.

- [ ] **Step 3: Implement duration aggregation**

In `dao_dream_material_collector.cc`, call `history->GetAnnotatedVisits(options, false, false, ...)` instead of `QueryHistory()`. Aggregate each `AnnotatedVisit` by host, using `context_annotations.total_foreground_duration` when it is non-negative, otherwise `visit_row.visit_duration` when positive. Add `foreground_seconds`, `total_seconds`, and `duration_level` to each history domain dict. Sort domains by foreground seconds descending, then visit count descending.

- [ ] **Step 4: Run the focused browser test to verify it passes**

Run: `engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDreamBrowserTest.CollectorAggregatesForegroundDuration"`

Expected: PASS.

### Task 2: LLM Prompt Duration Weighting

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_runner.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_dream_runner.ts`

- [ ] **Step 1: Write the failing Vitest assertion**

Add an assertion to the existing prompt test that the system prompt contains:

```ts
expect(systemPrompt).toContain('Use foreground_seconds');
expect(systemPrompt).toContain('deep');
expect(systemPrompt).toContain('light');
```

- [ ] **Step 2: Run the focused Vitest file to verify it fails**

Run: `npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_runner.test.ts`

Expected: FAIL because the prompt does not mention foreground duration weighting.

- [ ] **Step 3: Implement prompt guidance**

Update `SYSTEM_PROMPT` in `dao_dream_runner.ts` to describe the new material fields and require long foreground duration / `duration_level=deep` domains to receive more emphasis, while short `duration_level=light` domains should be brief or omitted unless they reinforce another signal.

- [ ] **Step 4: Run the focused Vitest file to verify it passes**

Run: `npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_runner.test.ts`

Expected: PASS.

### Task 3: Project Verification

**Files:**
- No new source files.

- [ ] **Step 1: Sync canonical Dao files into Chromium**

Run: `npm run import`

Expected: import succeeds and applies Dao-owned files into `engine/src`.

- [ ] **Step 2: Run WebUI verification**

Run: `npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_runner.test.ts`

Expected: PASS.

- [ ] **Step 3: Run compile confirmation**

Run: `npm run rebuild`

Expected: PASS. This is the only allowed compile-confirmation command for C++ changes in this repository.

- [ ] **Step 4: Review diff**

Run: `git diff -- src/dao/browser/agent/dao_dream_material_collector.h src/dao/browser/agent/dao_dream_material_collector.cc src/dao/browser/agent/dao_dream_browsertest.cc src/dao/browser/ui/webui/resources/agent/dao_dream_runner.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_dream_runner.test.ts docs/superpowers/plans/2026-06-19-dream-duration-weighting.md`

Expected: diff is limited to duration weighting, prompt guidance, tests, and this plan. No `engine/` edits are part of the final canonical diff.

## Self-Review

- Spec coverage: The plan covers real duration collection, report emphasis rules, and verification.
- Placeholder scan: No placeholders remain.
- Type consistency: The C++ output fields are consistently named `foreground_seconds`, `total_seconds`, and `duration_level`; the TypeScript prompt references the same names.
- Project override: Commit and branch steps are intentionally omitted because this repository's AGENTS.md forbids state-changing git commands unless explicitly requested.
