# Create DMG Temporary File Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent abandoned `create-dmg` writable images from reaching Sparkle appcast generation.

**Architecture:** Add one narrow filesystem cleanup helper to the existing package command and call it in a `finally` block around each `create-dmg` attempt. Match only `rw.<numeric-pid>.<target-dmg-name>` files so unrelated release artifacts remain untouched.

**Tech Stack:** TypeScript, Node.js filesystem APIs, Vitest.

## Global Constraints

- Work in the primary checkout on `main`; do not create a worktree or branch.
- Do not run state-changing git commands.
- Do not run Chromium build commands for this TypeScript-only release-tooling fix.
- Preserve all final DMGs, deltas, and unrelated writable images.
- `docs/features.md` and `docs/feature-checklist.md` remain unchanged because this is release tooling, not a Dao Browser product feature.

---

### Task 1: Clean abandoned create-dmg images

**Files:**
- Modify: `scripts/commands/__tests__/package_scripts.test.ts`
- Modify: `scripts/commands/package.ts`
- Delete: `dist/rw.60269.dao-browser-1.0.78-mac-arm64.dmg`

**Interfaces:**
- Produces: `cleanupCreateDmgTemporaryImages(dmgPath: string): string[]`, returning the removed paths.
- Consumes: Node.js `readdirSync`, `rmSync`, `path.dirname`, and `path.basename`.

- [ ] **Step 1: Write the failing regression test**

Import `cleanupCreateDmgTemporaryImages`. Create a temporary directory containing the target DMG, two matching numeric-PID temporary images, a nonnumeric `rw` filename, and a temporary image for another target. Call the helper and assert that only the two matching paths are returned and removed.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
npx vitest run scripts/commands/__tests__/package_scripts.test.ts
```

Expected: FAIL because `cleanupCreateDmgTemporaryImages` is not exported.

- [ ] **Step 3: Implement the minimal cleanup helper**

In `scripts/commands/package.ts`, escape the target DMG basename for a regular expression, list its directory, and remove only names matching:

```text
^rw\.\d+\.<escaped-target-dmg-name>$
```

Return the removed absolute paths. Wrap each `spawnCapture(createDmgBin, ...)` invocation in a small local attempt function with `try/finally`, calling the helper in `finally` so both Finder-styling failure and fallback completion clean their temporary files.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run:

```bash
npx vitest run scripts/commands/__tests__/package_scripts.test.ts
```

Expected: all tests in the file pass with exit code 0.

- [ ] **Step 5: Remove the confirmed abandoned image**

Delete only:

```text
dist/rw.60269.dao-browser-1.0.78-mac-arm64.dmg
```

Verify the signed final `dist/dao-browser-1.0.78-mac-arm64.dmg` still exists and no `dist/rw.*.dmg` files remain.

- [ ] **Step 6: Run final verification**

Run:

```bash
npx vitest run scripts/commands/__tests__/package_scripts.test.ts scripts/commands/__tests__/release.test.ts
```

Expected: both suites pass with exit code 0.

Run:

```bash
git diff --check
```

Expected: exit code 0 with no output.
