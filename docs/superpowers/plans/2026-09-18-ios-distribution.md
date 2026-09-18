# iOS Distribution Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan inline. Do not create branches, worktrees, commits, or pushes without the user's explicit authorization.

**Goal:** Upload a signed Dao iOS build to TestFlight and reuse it for App Store review.

**Architecture:** A manual GitHub Actions workflow uses a hosted macOS runner, a distribution certificate/profile, and an App Store Connect API key. The existing iOS rebuild command archives and exports through Xcode. Apple account setup, beta acceptance, store metadata, and review remain explicit steps.

**Tech Stack:** Xcode 26+, XcodeGen, TypeScript/Commander, GitHub Actions, Python plistlib.

**Scope:** The user's TestFlight-to-App-Store request and `ios/README.md`. No desktop or Android behavior changes. No computer UI automation.

## Tasks

- [x] Add focused behavioral checks for signed archive arguments, invalid configuration, build failure propagation, and provisioning-profile validation in `scripts/commands/__tests__/ios.test.ts`.
- [x] Extend `scripts/commands/ios.ts` with `rebuild --archive`; keep unsigned development builds working.
- [x] Add `.github/workflows/publish-ios-testflight.yml` with secret validation, temporary signing assets, archive/export/upload, artifacts, and cleanup.
- [x] Add the app's UserDefaults privacy manifest and an explicit Release archive scheme in `ios/`.
- [x] Document first-time Apple/GitHub setup and release acceptance in `ios/README.md`, `docs/features-ios.md`, and `docs/feature-checklist.md`.
- [x] Run the focused script tests, TypeScript/doc checks, workflow lint, plist validation, and `npm run rebuild -- --core-only` from `ios/`. Also run the complete unsigned `npm run rebuild` from `ios/` and verify its bundled privacy manifest.
- [x] Unify the iOS bundle ID with Android as `com.msgbyte.dao`, update export/profile validation, and verify that old-bundle profiles are rejected. Prepare draft store metadata and release prerequisites in `ios/APP_STORE.md`.
- [ ] With the user's account information and explicit Git publication authorization, configure credentials, dispatch CI, verify TestFlight installation, and prepare the same build for App Store review. Do not mark live distribution complete from local checks.

## Verification on September 18, 2026

- The archive behavior check failed first because `--archive` did not exist, then passed after implementation. Both focused distribution tests pass.
- TypeScript, documentation, actionlint, and plist checks pass.
- Xcode 16.3 unsigned iPhone build and all four portable Swift tests pass. The resulting app includes the exact tracked privacy manifest.
- After unifying the bundle ID, both distribution tests and another complete unsigned iPhone build pass. The built `Info.plist` contains `com.msgbyte.dao`, and its privacy manifest passes `plutil` validation.
- Xcode 26 signing, hosted CI execution, Apple upload/processing, TestFlight installation, and store review are not yet verified. Apple team/account setup and distribution credentials remain outstanding; changes are local and uncommitted.
