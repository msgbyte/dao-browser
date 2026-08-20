# Create DMG Temporary File Cleanup

## Problem

When `create-dmg` fails during Finder styling, it leaves a writable image named
`rw.<pid>.<artifact>.dmg` in `dist/`. Dao retries packaging without Finder
styling and successfully creates the final DMG, but the abandoned writable
image remains. Sparkle later scans the whole directory and rejects the two
images because they contain the same bundle version.

## Design

Add a focused cleanup helper in `scripts/commands/package.ts` that removes only
files in the target DMG directory whose names match
`rw.<numeric-pid>.<target-dmg-name>`. Invoke it after every `create-dmg`
attempt, including the failed Finder-styling attempt and the fallback attempt.
Do not remove unrelated writable images or other release artifacts.

Keep the existing Finder-access fallback behavior unchanged. Cleanup failures
should surface as packaging failures rather than allowing a contaminated
`dist/` directory to proceed to appcast generation.

## Testing

Export the cleanup helper for a focused unit test. The regression test creates
matching and unrelated temporary files, calls the helper, and verifies that
only matching `create-dmg` leftovers are removed. Run the test before the
implementation to confirm it fails for the missing behavior, then run the
focused test suite after implementation.

## Existing Artifact

Remove the confirmed abandoned file
`dist/rw.60269.dao-browser-1.0.78-mac-arm64.dmg`. Preserve the signed final
`dist/dao-browser-1.0.78-mac-arm64.dmg` and every other release artifact.

## Documentation Scope

This changes release tooling only. It does not add or materially change a Dao
Browser product feature, so `docs/features.md` and
`docs/feature-checklist.md` do not require updates.
