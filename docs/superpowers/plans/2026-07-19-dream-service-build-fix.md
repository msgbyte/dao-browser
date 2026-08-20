# Dream Service Build Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the Dao Browser release build by replacing unsafe native-array indexing and removing an obsolete Chromium GN argument.

**Architecture:** Keep the calendar calculation local to `dao_dream_service.cc`, but represent the fixed month table with `std::array` and reject invalid month values before indexing. Remove `enable_nacl` from Dao's canonical GN arguments because the current Chromium checkout no longer declares it.

**Tech Stack:** Chromium C++23, GN, npm Dao build CLI

## Global Constraints

- Treat `src/dao/` and `configs/` as canonical sources.
- Do not edit `engine/` directly.
- Use `npm run rebuild` as the only compile-confirmation command.
- Do not run state-changing git commands.

---

### Task 1: Reproduce the build failure

**Files:**
- Inspect: `src/dao/browser/agent/dao_dream_service.cc`
- Inspect: `configs/common.gn`

**Interfaces:**
- Consumes: Existing `DaysInMonth(int year, int month)` implementation and release GN arguments.
- Produces: Fresh evidence that the current source fails `-Wunsafe-buffer-usage` and that `enable_nacl` is obsolete.

- [ ] **Step 1: Run the allowed compile confirmation**

Run: `npm run rebuild`

Expected: FAIL at `dao_dream_service.cc` with `unsafe buffer access` before the fix.

### Task 2: Make the minimal source and configuration fix

**Files:**
- Modify: `src/dao/browser/agent/dao_dream_service.cc:7-47`
- Modify: `configs/common.gn:12`

**Interfaces:**
- Consumes: `DaysInMonth(int year, int month)` callers that provide calendar month values.
- Produces: The same `int` day count for months 1 through 12, with a fatal invariant check for invalid months.

- [ ] **Step 1: Replace the native array with a checked `std::array` lookup**

Add `<array>`, declare `kDaysPerMonth` as `constexpr std::array<int, 12>`, and add `CHECK_GE(month, 1)` plus `CHECK_LE(month, 12)` before indexing.

- [ ] **Step 2: Remove the obsolete GN argument**

Delete `enable_nacl = false` from `configs/common.gn`.

- [ ] **Step 3: Review the diff**

Run: `git diff --check && git diff -- src/dao/browser/agent/dao_dream_service.cc configs/common.gn`

Expected: No whitespace errors and only the scoped safety/configuration changes.

### Task 3: Import and verify

**Files:**
- Import canonical changes through the project CLI.

**Interfaces:**
- Consumes: Updated Dao-owned source and GN configuration.
- Produces: An updated generated Chromium build tree and a successful release compile.

- [ ] **Step 1: Import canonical changes**

Run: `npm run import`

Expected: Import completes without requiring `--force`.

- [ ] **Step 2: Compile the browser**

Run: `npm run rebuild`

Expected: PASS with no `enable_nacl` warning and no unsafe-buffer error in `dao_dream_service.cc`.

- [ ] **Step 3: Inspect final state**

Run: `git status --short && git diff --check`

Expected: Only intended tracked changes plus any pre-existing user changes; no whitespace errors.
