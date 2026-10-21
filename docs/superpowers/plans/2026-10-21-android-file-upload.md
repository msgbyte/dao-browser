# Android Web File Upload Implementation Plan

**Goal:** Connect live Gecko file prompts to Android's document picker without delivering results to a different page.

**Spec:** `/Users/moonrailgun/Downloads/dao-android-file-upload-prd.md`

**Architecture:** A ViewModel-owned file prompt adapter observes BrowserStore actions. Each picker registration has a unique in-memory identity bound to the original tab, engine session, and prompt. MainActivity owns activity-result registration; Android supplies the picker UI and Mozilla supplies the file-selection callbacks.

**Constraints:** Keep the existing checkout, extension picker, and unrelated prompts. Reuse the already installed Mozilla support-ktx dependency. Use English and Simplified Chinese resources. Do not persist picker state, request broad storage permissions, or retain URI grants. Direct capture and directory selection are outside scope.

## Implementation

- [x] Add request ownership tests for cancellation, repetition, competing/background prompts, navigation, tab changes, session replacement, and late results.
- [x] Implement the file-only BrowserStore adapter and lifecycle-stable activity result registration. Only visible browsing pages may initiate selection; activity pause alone must not cancel selection.
- [x] Stage selected documents off the main thread, preserve single/multiple selection, isolate files and sessions, and remove abandoned staging safely.
- [x] Add focused contract, localization, and Gecko/picker instrumentation coverage, including controlled file bytes and extension-based accept filtering.
- [x] Update Android feature inventory, shared checklist, and matching Android Unreleased notes.
- [x] Run focused Android checks, review the full change and connected callers, and report any unavailable device coverage.

## Review focus

- Rapid switch-away-and-back must invalidate a request even when state-flow emissions coalesce.
- A late result from an old picker must never resolve a newer request.
- Activity recreation must reuse the original result-registration key without launching again.
- Documents with identical names must not overwrite one another or another tab's files.
- Failed reads must dismiss the whole multiple-file selection and leave retry available.

## Implementation review decisions

- Mozilla 153.0.2 couples a synchronous, error-swallowing URI copy with its main-thread confirmation callback. Pre-reading alone cannot guarantee complete files. Use strict asynchronous staging and an Android Gradle instrumentation patch that returns already staged `file://` URIs unchanged from `GeckoPromptDelegate.toFileUri`. Preserve the original callback and all other prompt behavior. Fail the build if the pinned version or method signature changes.
- An independent review found callback reentrancy, cache-cleanup startup failure, and invalidated reads blocking new requests. Reserve terminal outcomes before callback invocation, clean old process directories best-effort, and cancel/clear invalidated staging immediately.

## Verification results

- 86 focused unit tests passed: file uploads (38 runs on API 26 and 35), tab ownership (20), download confirmation (5), extension staging (5), localization (2), and default-browser settings (16). The settings test used a role-name constant absent from the public SDK; it now asserts the platform's actual intent extra key.
- All three real Gecko/system-picker tests passed on an API 36 emulator: single-file upload with cancellation/retry, a styled `.pdf` input across Activity recreation, and multiple-file upload in a private tab. A local HTTP endpoint verified selected filenames and contents.
- Documentation validation and `git diff --check` passed.
- `:app:lintDebug` completed analysis but failed on two existing `MissingTranslation` errors for `set_as_default_browser` and `default_browser_settings_unavailable`. The new upload resources follow the existing English-fallback convention while generated translations await the manual workflow.
- Physical-device providers, the original reported website, API 26 device behavior, process death, image/audio/video filtering, keyboard/TalkBack return, and interactive extension/download regressions still need the release-checklist checks. No physical device or original website details were available.
- Mozilla upgrades must review the version- and signature-guarded staged-file compatibility patch.
