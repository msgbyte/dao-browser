# Android Release Implementation Plan

**Goal:** Run local version bump, commit, tag, and push from one command, then
build and publish the signed Android APK in GitHub Actions.

**Approved flow:** `npm run release:android` reads the checked-in Gradle versions,
prompts for increasing values, updates only those values, creates a Conventional
Commit and `android-v<version>` tag, and atomically pushes `main` plus that tag.
The tag workflow builds and verifies the APK and publishes a GitHub Release.
There is no R2 upload or Android latest manifest.

**Stack:** Existing Node.js, Commander, Vitest, Git, Gradle, Android build-tools,
and GitHub CLI. No new dependencies.

## Constraints

- Work in the primary checkout; preserve unrelated changes and never edit `engine/`.
- Implementation does not perform a real version bump, commit, tag, push, build,
  or remote publication in this repository.
- Real releases require clean `main` containing remote `main`. Existing local or
  remote release tags are rejected before edits.
- Commit only `android/app/build.gradle.kts`; push only `main` and the Android
  tag, even when `push.followTags` is configured.
- Reuse the existing three Android signing secrets and persistent signing key.
- Publish ordinary Android releases with `--latest=false`; preserve desktop Latest.
- Recover uploads in drafts; never overwrite a published APK.

## Implementation

- [x] Add interactive and explicit version input, validation, and offline dry-run.
- [x] Preflight Git state; update, commit, tag, and atomically push with actionable
  failure messages that retain local work.
- [x] Add the `android-v*` workflow: validate tag, build signed APK, verify signature,
  package, version and debug flag, then upload APK and SHA256SUMS before publishing.
- [x] Skip published releases; allow failed draft uploads to be retried.
- [x] Update Android release documentation, feature inventory, and checklist.
- [x] Cover the real Git transaction in isolated local repositories and run the
  workflow shell/Python checks with external tools replaced.
- [x] Review the implementation independently and add the `push.followTags` regression.

## Validation

- Five focused test files passed: 40 tests, including real local Git transactions,
  APK validation, signing-secret cleanup, draft retries, and existing release tooling.
- Type checking, documentation checking, whitespace checks, and actionlint passed.
- Real GitHub Actions building, signing, publication, and device installation
  require an authorized release run.
