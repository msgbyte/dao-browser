# Import Chromium Version Validation Design

## Goal

Prevent `npm run import` from modifying an incompatible Chromium checkout by
verifying that the checked-out Chromium version matches
`dao.json.version.version` before import work begins.

## Design

Add an exported helper in `scripts/commands/import.ts` that reads
`engine/src/chrome/VERSION`, requires the `MAJOR`, `MINOR`, `BUILD`, and `PATCH`
fields, and returns the normalized four-part version string.

The import command will load `dao.json` immediately after confirming that
`engine/src` exists. It will compare the parsed Chromium version with
`config.version.version` before `--force` reset, package-file creation, patch
application, generated rewrites, or source copying.

## Failure Behavior

If `chrome/VERSION` is missing or malformed, import will report that the
Chromium version cannot be determined and exit with a non-zero status.

If the versions differ, import will report both the expected and actual
versions, recommend running `npm run download`, and exit with a non-zero
status. A matching version will emit a short success message and continue with
the existing import flow.

## Testing

Focused Vitest coverage in `scripts/commands/__tests__/import.test.ts` will
verify:

- a complete version file is parsed into the expected four-part version;
- a mismatch is rejected with expected and actual versions;
- a missing or malformed field is rejected before import work can proceed.

Tests will be written and observed failing before production code is changed.

## Scope

This changes build tooling only. It does not add or materially change a Dao
Browser product feature, so `docs/features.md` and
`docs/feature-checklist.md` do not require updates.
