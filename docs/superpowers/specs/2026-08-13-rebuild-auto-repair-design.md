# Rebuild Auto-Repair Design

## Goal

Make `npm run rebuild` automatically repair only the Chromium patch targets
that failed during its import step, while preserving the existing manual and
non-destructive behavior of plain `npm run import`.

## Design

The import command gains an opt-in `--repair` flag. When enabled and one or
more patches fail, import invokes `scripts/fix-import-patches.sh` once with
exactly those failed patch files. It then verifies each repaired patch with a
reverse `git apply --check` before continuing. The repair happens before
generated Chromium rewrites, version injection, source synchronization, and
branding synchronization so those later canonical import steps remain intact.

`npm run rebuild` passes `--repair`; `npm run import` does not. Neither path
uses `--force`. If repair execution fails or any patch remains unverifiable,
import exits non-zero and prints the existing copyable manual repair command.
There is no retry loop.

## Verification

Focused tests cover successful targeted repair, failed repair, the rebuild
script contract, and the unchanged plain-import boundary. Run the import test
file and TypeScript check. Use `npm run rebuild` only after the focused checks
pass; do not run any direct Chromium build command.

