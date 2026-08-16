# Rebuild Auto-Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automatically repair failed Chromium patches only during `npm run rebuild`.

**Architecture:** Add an opt-in repair orchestration helper to the import command, run it before later import mutations, and wire only the rebuild package script to pass `--repair`. Reuse the existing targeted shell repair script and verify its result rather than duplicating its reset logic.

**Tech Stack:** TypeScript, Commander, Node child processes, Vitest, npm scripts, POSIX shell.

## Global Constraints

- Plain `npm run import` must retain manual repair behavior.
- Never use `--force` for automatic repair.
- Repair exactly the patches that failed in the current import attempt.
- Attempt repair once and fail closed with the existing manual command.
- Never edit generated files under `engine/` directly.

---

### Task 1: Lock the command contract with failing tests

**Files:**
- Modify: `scripts/commands/__tests__/import.test.ts`
- Modify: `package.json`

**Interfaces:**
- Consumes: `package.json` scripts and the import command option list.
- Produces: assertions that rebuild passes `--repair` and plain import does not.

- [ ] Add a test that reads `package.json` and expects `scripts.rebuild` to be `npm run import -- --repair && npm run build:debug` while `scripts.import` remains `tsx scripts/cli.ts import`.
- [ ] Run `npx vitest run scripts/commands/__tests__/import.test.ts` and confirm the new assertion fails on the rebuild script.

### Task 2: Add targeted repair orchestration

**Files:**
- Modify: `scripts/commands/import.ts`
- Modify: `scripts/commands/__tests__/import.test.ts`

**Interfaces:**
- Produces: `repairFailedPatches(srcDir: string, patchPaths: string[], repairScriptPath?: string): Promise<boolean>`.
- Consumes: the existing `fix-import-patches.sh` and reverse patch check.

- [ ] Add an integration-style helper test using a temporary Chromium Git fixture and copied repair script; verify stale applied content is repaired and the helper returns `true`.
- [ ] Add a failure test whose unsafe or invalid target makes repair return `false` without reporting success.
- [ ] Run the focused tests and confirm they fail because the helper does not exist.
- [ ] Implement the helper with one `sh` execution and reverse-check every requested patch.
- [ ] Add `--repair` to the import command and call the helper only when failures exist and the flag is set, before generated rewrites.
- [ ] On failure, retain the failed count and existing manual repair hint; on success, clear the failed set and continue import.

### Task 3: Wire rebuild and update workflow documentation

**Files:**
- Modify: `package.json`
- Modify: `docs/chromium-upgrade-guide.md`

**Interfaces:**
- Consumes: import's `--repair` flag.
- Produces: rebuild-only opt-in behavior and documented distinction from plain import.

- [ ] Update `scripts.rebuild` to pass `--repair`.
- [ ] Document that rebuild performs one targeted auto-repair attempt, while plain import preserves the manual conflict workflow.
- [ ] Run the focused import tests and TypeScript check.

### Task 4: Verify the complete path

**Files:**
- Verify only.

**Interfaces:**
- Consumes: completed implementation.
- Produces: evidence that scripts and import behavior are valid.

- [ ] Run `npx vitest run scripts/commands/__tests__/import.test.ts`.
- [ ] Run `npm run typecheck`.
- [ ] Run `git diff --check`.
- [ ] Run `npm run rebuild`; confirm it either compiles successfully or, if a real patch remains unrepairable, exits with the precise manual repair command and no retry loop.

