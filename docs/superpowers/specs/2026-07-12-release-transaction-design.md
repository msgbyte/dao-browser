# Release Transaction and Failure Rollback Design

## Goal

Make `npm run release` preserve the pre-release canonical state when any
controlled release step fails. A retry after failure must calculate the same
candidate version instead of consuming another version number.

For example, if the repository starts at `1.0.70`, a failed attempt to release
`1.0.71` must restore the repository to `1.0.70`. The next normal
`npm run release` must attempt `1.0.71` again.

## Current Problem

The release command currently writes `dao.json.version.display` and creates an
annotated Git tag before import, build, signing, notarization, appcast
generation, and upload. Most failures terminate through `process.exit(1)`, so
there is no shared cleanup path.

This leaves one or more of the following after a failed run:

- an incremented product version in `dao.json`;
- a tag for an incomplete release;
- a regenerated `website/public/appcast.xml`;
- updated download metadata in `website/public/info.json`.

Generated build state and artifacts are separate from this problem. They are
useful for diagnosis and recovery and do not define the canonical released
version.

## Selected Approach

Wrap the existing release orchestration in an in-process release transaction.
The transaction snapshots canonical release metadata before the first write,
restores that metadata after a controlled failure, and commits the new state
only after every release step succeeds.

This approach preserves the existing import, build, package, notarization,
Sparkle, and R2 commands. It avoids the broader alternative of teaching each
command to accept a separate candidate-version override.

## Transaction Scope

The transaction owns these canonical files:

- `dao.json`
- `website/public/appcast.xml`
- `website/public/info.json`

The transaction snapshots their exact byte contents rather than restoring from
Git. This preserves any pre-existing staged or unstaged user changes.

The transaction also tracks whether it created the candidate release tag. It
must never delete a tag that existed before the transaction began.

The transaction does not roll back:

- `engine/` or Chromium build output;
- files under `dist/`;
- Apple notarization submissions;
- objects already uploaded to R2.

Keeping generated artifacts makes failures easier to inspect and allows an
identical-version retry. Restoring `engine/` or compiled output is unnecessary
because those directories are generated state, and the next release will use
the same candidate version.

## Release Flow

### 1. Read-only preflight

Before mutating files, the command will:

1. Load the current configuration and calculate the candidate version.
2. Validate credentials, required binaries, bucket configuration, and required
   source files as it does today.
3. Resolve `HEAD` and validate the candidate tag state.
4. Snapshot the three transaction-owned files.

Tag validation must fail before mutation when the candidate tag already points
to a different commit. If a matching tag already points to `HEAD`, it is
treated as pre-existing and must not become rollback-owned.

### 2. Transactional execution

Inside the transaction, the command will:

1. Write the candidate version to `dao.json`.
2. Import Dao sources and patches.
3. Build the release application.
4. Package and sign the artifact.
5. Notarize and staple the DMG.
6. Generate and stamp the Sparkle appcast.
7. Copy the appcast into `website/public` and update `info.json`.
8. Upload the DMG and newly generated deltas to R2.
9. Create the annotated release tag.
10. Commit the transaction.

The tag is deliberately moved to the end of the flow. The read-only preflight
removes predictable tag failures before upload, while delayed creation avoids
leaving tags for build, signing, or upload failures.

### 3. Success

After transaction commit, the new version, public appcast source, download
metadata, and tag remain in place. Existing manual guidance for committing,
pushing, and deploying the website remains unchanged.

### 4. Failure

Any controlled failure before commit will:

1. Restore the exact pre-release contents of all transaction-owned files.
2. Delete the candidate tag only if the current transaction created it.
3. Keep generated files and release artifacts.
4. Report the failed phase, restored version, retained artifact location, and
   retry behavior.
5. Exit with a non-zero status after rollback completes.

The expected message shape is:

```text
Release 1.0.71 failed during notarization.
Restored release metadata to 1.0.70.
Generated artifacts were kept in dist/.
The next npm run release will retry 1.0.71.
```

If rollback itself cannot restore a file, the command must report the exact
path and leave a non-zero exit status. It must not claim that the old state was
restored completely.

## Error Model

Release helpers in `scripts/commands/release.ts` must stop calling
`process.exit()` from inside the orchestration. They will throw a typed
`ReleaseError` containing the release phase and a human-readable cause.

The outer command action will own exit behavior:

```text
preflight
snapshot
try release steps
commit on success
catch error
rollback
set process.exitCode = 1
```

Subcommands executed as child processes may continue to exit non-zero. The
parent command runner will convert their exit code into `ReleaseError`, which
keeps rollback under the parent process's control.

`SIGINT` and `SIGTERM` will request termination of the active child process and
then enter the same rollback path. Abrupt termination that cannot run process
cleanup, including `SIGKILL`, machine shutdown, or runtime corruption, is out
of scope for the first implementation. A durable recovery journal can be added
later if those failures become operationally significant.

## Safe Restoration

The transaction snapshot is captured in memory before release-owned writes.
Restoration writes those original bytes directly and does not invoke
`git restore`, `git checkout`, or `git reset`.

To avoid overwriting concurrent edits made during a long release, each managed
write records the bytes written by the transaction. During rollback, a file is
automatically restored only when its current content still matches the last
transaction-owned content. If it differs, rollback reports a concurrent-edit
conflict and does not overwrite that file.

This rule protects both initially dirty files and edits made while the release
is running.

## External Side Effects

Apple notarization and R2 uploads are not transactional. They are safe to keep
after local rollback for this workflow:

- notarization does not publish a Dao version to users;
- R2 objects are not discoverable through Sparkle until the website appcast is
  deployed;
- retries use the same candidate version and object names, so they can replace
  or complete a partial upload;
- website deployment remains a manual step after a successful release.

The release command must describe this distinction accurately: canonical local
metadata is rolled back, while generated and remote intermediate artifacts may
remain.

## Resume Options

Existing explicit recovery options such as `--skip-bump`, `--skip-build`, and
`--resume-from-staple` remain available for operator-directed recovery.

The default failure behavior no longer depends on `--skip-bump`. After an
automatic rollback, a normal `npm run release` calculates the same candidate
version. To reuse a manually notarized and stapled candidate DMG after
rollback, the recovery command is:

```bash
npm run release -- --resume-from-staple
```

It deliberately omits `--skip-bump`: `dao.json` has been restored to the old
version, so the normal bump is what reconstructs the retained artifact's
candidate version. `--skip-bump` remains available only for legacy or manual
recovery cases where the candidate version is intentionally still present in
`dao.json`.

## Implementation Boundaries

The orchestration should expose a testable `runRelease(options, dependencies)`
function. Process execution, filesystem operations, Git tag operations, and
time should be dependency boundaries so tests can simulate every failure phase
without building Chromium, contacting Apple, or uploading to R2.

The transaction should be a small unit with these responsibilities:

- snapshot and track managed files;
- record transaction-owned writes;
- record whether the transaction created a tag;
- commit or restore state exactly once;
- report incomplete rollback.

It must not contain build, signing, appcast, or upload business logic.

## Testing

Focused Vitest coverage will verify:

- failure during import, build, packaging, notarization, stapling, appcast
  generation, metadata update, or upload restores all canonical files;
- a retry after failure calculates the same candidate version;
- pre-existing staged or unstaged contents are restored byte-for-byte;
- concurrent edits are detected and never overwritten silently;
- a failed run does not leave a newly created tag;
- rollback never deletes a pre-existing tag;
- a tag conflict fails before any file mutation;
- partial upload failure rolls back local metadata while retaining artifacts;
- successful execution retains the new version, appcast, download metadata,
  and tag;
- `--dry-run` does not create a transaction-owned mutation;
- `SIGINT` and `SIGTERM` follow the rollback path;
- rollback failure is visible and results in a non-zero exit status.

Tests will be written and observed failing before implementation code is
changed.

## Documentation

`docs/release-signing.md` will be updated with the automatic rollback behavior,
the distinction between canonical metadata and retained artifacts, and the
revised retry guidance.

This changes release tooling only. It does not add or materially change a Dao
Browser product feature, so `docs/features.md` and
`docs/feature-checklist.md` do not require updates.

## Acceptance Criteria

- A controlled `npm run release` failure leaves canonical release metadata
  exactly as it was before the command began.
- The next normal release attempt retries the same candidate version.
- No failed run leaves a transaction-created Git tag.
- Existing user changes and pre-existing tags are never discarded.
- Successful release behavior and manual post-release steps remain unchanged.
- Failure output distinguishes restored local state from retained generated or
  remote artifacts.
