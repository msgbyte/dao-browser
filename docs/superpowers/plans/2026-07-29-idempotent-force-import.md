# Idempotent Force Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every release forced import reconstruct the Dao overlay without manual cleanup while preserving unrelated untracked files.

**Architecture:** Parse every target section in each Dao patch and identify targets created from `/dev/null`. Before forced import, reset tracked Chromium files and remove only exact untracked files that current patches declare as new. Update the repair shell script to reset or remove every target in a multi-file patch before applying it.

**Tech Stack:** TypeScript, Node.js filesystem and child-process APIs, POSIX shell, Git, Vitest

## Global Constraints

- Never invoke broad `git clean`.
- Never remove directories recursively.
- Never remove untracked paths not declared as new files by current Dao patches.
- Reject absolute paths and paths containing `..`.
- Do not run release, rebuild, or Chromium build tools.
- Do not run state-changing root-repository git commands without separate authorization.

---

### Task 1: Parse and clean patch-created files safely

**Files:**
- Modify: `scripts/commands/__tests__/import.test.ts`
- Modify: `scripts/commands/import.ts`

**Interfaces:**
- Consumes: Unified patch text and patch file paths under `src/patches`.
- Produces: `PatchTarget`, `parsePatchTargets(content)`, and `cleanupPatchCreatedFiles(srcDir, patchPaths)`.

- [ ] **Step 1: Add failing parser and cleanup tests**

Add tests importing the new helpers and asserting:

```ts
expect(parsePatchTargets(multiFilePatch)).toEqual([
  {path: 'tracked.txt', isNewFile: false},
  {path: 'generated/new-file.d.ts', isNewFile: true},
]);
```

Create a temporary Git repository containing an existing patch-created
untracked file and an unrelated untracked file. Call:

```ts
cleanupPatchCreatedFiles(repoDir, [patchPath]);
cleanupPatchCreatedFiles(repoDir, [patchPath]);
```

Assert the declared new file is absent after both calls and the unrelated file
still exists. Add rejection assertions for `../escape` and an existing tracked
path declared as a new file.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
npx vitest run scripts/commands/__tests__/import.test.ts
```

Expected: FAIL because `parsePatchTargets` and
`cleanupPatchCreatedFiles` are not exported.

- [ ] **Step 3: Implement the parser and exact cleanup**

Add:

```ts
export interface PatchTarget {
  path: string;
  isNewFile: boolean;
}

export function parsePatchTargets(content: string): PatchTarget[] {
  // Track each diff section's --- source and +++ target.
  // Return all modified/deleted/new targets and reject unsafe paths.
}

export function cleanupPatchCreatedFiles(
  srcDir: string,
  patchPaths: string[]
): void {
  // Deduplicate new-file targets, reject tracked collisions, and unlink only
  // exact existing files beneath srcDir.
}
```

Use `execFileSync("git", ["ls-files", "--error-unmatch", "--", target])`
to detect tracked collisions and `unlinkSync()` for exact files. Fail if an
exact target is a directory.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run:

```bash
npx vitest run scripts/commands/__tests__/import.test.ts
```

Expected: PASS.

### Task 2: Integrate exact cleanup into forced import

**Files:**
- Modify: `scripts/commands/__tests__/import.test.ts`
- Modify: `scripts/commands/import.ts`

**Interfaces:**
- Consumes: Sorted current patch paths and the reset `engine/src` checkout.
- Produces: A forced import baseline with only current patch-created untracked files removed.

- [ ] **Step 1: Add a failing forced-preparation regression test**

Extract and test:

```ts
prepareForcedImport(srcDir, patchPaths);
prepareForcedImport(srcDir, patchPaths);
```

The temporary repository starts with a tracked modified file, a stale
patch-created file, and an unrelated untracked file. Assert both calls restore
the tracked file, remove the stale declared file, and preserve the unrelated
file.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
npx vitest run scripts/commands/__tests__/import.test.ts
```

Expected: FAIL because `prepareForcedImport` is not exported.

- [ ] **Step 3: Implement and wire forced preparation**

Add:

```ts
export function prepareForcedImport(
  srcDir: string,
  patchPaths: string[]
): void {
  execFileSync("git", ["checkout", "--", "."], {cwd: srcDir});
  cleanupPatchCreatedFiles(srcDir, patchPaths);
}
```

Discover and sort patch paths before the `--force` block, then call this helper
instead of the current shell-string `git checkout -- .`. Keep existing log and
error behavior.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run:

```bash
npx vitest run scripts/commands/__tests__/import.test.ts
```

Expected: PASS.

### Task 3: Repair every target in a multi-file patch

**Files:**
- Modify: `scripts/commands/__tests__/import.test.ts`
- Modify: `scripts/fix-import-patches.sh`

**Interfaces:**
- Consumes: One or more repo-contained unified patch paths.
- Produces: Every patch target reset to Chromium HEAD or precisely removed before one atomic `git apply`.

- [ ] **Step 1: Add a failing multi-file repair test**

Create a patch that modifies `BUILD.gn` and creates
`eslint_plugin_lit.d.ts` as its second target. Seed the new target as an
untracked stale file, run the repair script, and assert:

```ts
expect(readFileSync(buildPath, 'utf-8')).toContain('definitions =');
expect(readFileSync(definitionPath, 'utf-8')).toBe('new definition\n');
expect(existsSync(unrelatedPath)).toBe(true);
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
npx vitest run scripts/commands/__tests__/import.test.ts
```

Expected: FAIL with `already exists in working directory`.

- [ ] **Step 3: Update the repair script**

Replace the single-target `awk` parser with an all-section parser that emits
`tracked<TAB>path` or `new<TAB>path`. Validate every emitted path. For each
target:

```sh
case "$target_kind" in
  new)
    if git -C "$ENGINE_SRC" ls-files --error-unmatch "$target_rel" \
        >/dev/null 2>&1; then
      echo "error: patch new-file target is tracked: $target_rel" >&2
      exit 1
    fi
    rm -f "$ENGINE_SRC/$target_rel"
    ;;
  tracked)
    git -C "$ENGINE_SRC" checkout -- "$target_rel"
    ;;
esac
```

Apply the patch only after all of its targets are prepared.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run:

```bash
npx vitest run scripts/commands/__tests__/import.test.ts
```

Expected: PASS, including existing single-file repair cases.

### Task 4: Verify the release import workflow

**Files:**
- Verify: `scripts/commands/import.ts`
- Verify: `scripts/fix-import-patches.sh`
- Verify: `scripts/commands/__tests__/import.test.ts`

**Interfaces:**
- Consumes: The current Chromium 149 engine and eslint multi-file patch.
- Produces: Evidence that repeated forced preparation is safe and normal import remains operational.

- [ ] **Step 1: Run all focused import tests**

Run:

```bash
npx vitest run scripts/commands/__tests__/import.test.ts
```

Expected: All tests pass.

- [ ] **Step 2: Run static checks**

Run:

```bash
sh -n scripts/fix-import-patches.sh
git diff --check
```

Expected: Both commands exit successfully.

- [ ] **Step 3: Run normal import**

Run:

```bash
npm run import
```

Expected: Import completes with zero failed patches. Do not run `--force`,
release, rebuild, or any Chromium build command.

- [ ] **Step 4: Review final scope**

Run:

```bash
git status --short
git diff -- scripts/commands/import.ts scripts/fix-import-patches.sh \
  scripts/commands/__tests__/import.test.ts
```

Expected: Only the approved tooling, regression tests, and ignored Superpowers
documents differ, plus any pre-existing user changes.
