# Idempotent Force Import Design

## Goal

Ensure every release starts its forced import from a consistent Chromium
baseline and reconstructs the complete Dao overlay without manual deletion or
patch repair.

## Problem

Dao patch files can modify multiple tracked Chromium files and create new files.
The current forced import resets only tracked files with
`git checkout -- .`. Files created by an earlier patch application remain
untracked, so the next `git apply` fails with `already exists in working
directory`.

The repair script parses only the first target in each patch. It therefore
cannot reset all tracked targets or remove later new-file targets in a
multi-file patch.

## Design

Introduce one patch-target parser in the TypeScript import implementation. It
will inspect every `diff --git` section and return every target path together
with whether that section creates a new file.

Before a forced import applies patches:

1. Reset tracked Chromium files with the existing `git checkout -- .`.
2. Parse all Dao patch files.
3. For paths explicitly declared as new files by a Dao patch, check whether the
   path is untracked.
4. Remove only those exact untracked paths.
5. Apply patches through the existing reverse-check and batch/fallback flow.

The cleanup must never invoke broad `git clean`, remove tracked files, remove
directories recursively, or touch untracked paths not declared as new files by
the current patch set. Path traversal and absolute patch targets must be
rejected.

Update `fix-import-patches.sh` to enumerate every target in each supplied patch.
For tracked targets it restores the Chromium version. For new-file targets it
removes only the exact untracked file before applying the patch. This makes
manual repair consistent with forced import.

## Error Handling

- Reject unsafe target paths before filesystem mutation.
- Fail clearly when a declared new-file target exists but is tracked.
- Preserve all unrelated untracked files, including build outputs and manual
  debugging artifacts.
- Preserve the current failed-patch command and reporting behavior.

## Verification

Add focused regression tests that:

- parse every target in a multi-file patch;
- identify a later `--- /dev/null` target as a new file;
- clean only patch-declared untracked files during force preparation;
- preserve unrelated untracked files;
- repair a multi-file patch whose later target is an existing untracked file;
- prove the forced-import preparation is idempotent across repeated runs.

Run the focused import helper tests and then exercise normal import against the
current eslint patch. Do not run release, rebuild, or Chromium build tools as
part of this tooling fix.

## Documentation

This is release/import tooling behavior rather than a Dao Browser feature, so
`docs/features.md` and `docs/feature-checklist.md` do not change. Update the
Chromium upgrade guide only if the operator workflow changes; the intended
workflow remains the same, so no guide change is expected.
