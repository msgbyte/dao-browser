# Website Release Rollback Implementation Plan

This records the initial website-only scope. The subsequent [Windows website updates plan](2026-10-23-windows-website-updates.md) extends rollback to restore signed Windows metadata and synchronize the legacy update source.

The later user-requested revision replaces numbered input with an arrow-key menu and stops after a local commit. Remote synchronization checks and automatic push are removed; the implementation record below describes the original behavior.

**Goal:** Let maintainers select a previous website download release and publish its metadata through the existing main-branch website deployment.

**Design:** Read distinct versions from the committed history of `website/public/info.json`; desktop tags can precede their metadata commit. Restore the selected snapshot, including actual platform URLs and release dates, after checking installer availability. Only this file enters the rollback commit. Keep browser versions, update feeds, tags, binaries, and changelogs unchanged at runtime.

**Approaches:** Prefer historical website snapshots over reconstructing URLs from version tags: snapshots preserve platform availability and versions. Prefer the existing GitHub website deployment over adding a second deployment mechanism.

**Constraints:** No new dependencies, worktrees, or branches. Implement in the main checkout without committing or publishing this development change. CLI diagnostics follow the existing English developer-tool conventions; no browser UI copy is added.

## Task 1: Command and verification

- [x] Add focused tests in `scripts/commands/__tests__/release-rollback.test.ts` for historical selection, invalid input, exact snapshot restoration, dry runs, unavailable artifacts, dirty/diverged checkouts, commit failure, and push failure. Use disposable repositories and stub external HTTP.
- [x] Implement `scripts/commands/release-rollback.ts`, register it in `scripts/cli.ts`, and add `release:rollback` to `package.json`. Offer an interactive numbered list, `--version`, and `--dry-run`.
- [x] Require a clean main checkout exactly matching origin/main before publication, check all selected installer URLs, and reuse `ReleaseTransaction` to protect edits. Commit only info.json and push main without tags. Keep a failed push's local commit and show a retry command.
- [x] Document the command and its website-only boundary in the development guide, feature inventory, and checklist. This developer-only command needs no changelog entry.
- [x] Run focused release/download tests, TypeScript checking, documentation checking, and a real CLI dry run. Review the complete change before completion.

## Review focus

- Tags may contain the preceding version's metadata.
- Historical Windows downloads may have a different version or be absent.
- Missing artifacts must not change files or Git history.
- Local unpublished work must never be included in an automatic push.
- Commit failure must preserve prior files; push failure must retain the recovery commit.

## Execution record

- Implementation is authorized by the user's request; publication behavior defaults to automatic commit/push based on “push it as latest,” pending their optional preference response.
- Review found that retrying a floating HEAD could publish subsequent unrelated work. Added failing tests for that recovery sequence and hook-added files; publication now verifies and pins the rollback commit SHA.
- Verification: 124 tests passed across six release/download suites; TypeScript, documentation checks, and diff whitespace checks passed. Explicit-version and interactive CLI dry runs selected the historical 1.0.116 download metadata without writing it. No production commit, push, or deployment was performed.
