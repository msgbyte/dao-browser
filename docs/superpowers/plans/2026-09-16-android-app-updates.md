# Android app updates

**Goal:** Add manual and quiet daily update checks to the Android About screen.

**Architecture:** Read stable Android releases from the existing GitHub repository, reuse DataStore and SystemDownloadRepository, and hand verified APKs to the system installer. No new dependency or backend.

**Tech stack:** Kotlin, Compose, coroutines, Android DownloadManager, FileProvider, JUnit/Robolectric.

**Spec:** Approved conversation design: About check/download/install states, default-enabled daily checks, a Settings update hint, numeric version comparison, ABI selection, persisted downloads, SHA-256/package/version verification, and unknown-source permission recovery.

**Global constraints:** Work on the current main checkout; preserve unrelated desktop changes; no Git mutations; localize English and Chinese UI; update Android inventory and regression checklist.

## Tasks

1. Add `AppUpdateRepositoryTest.kt` before `AppUpdateRepository.kt`. Cover mixed desktop/Android releases, numeric version ordering, pagination, ABI priority, unsupported devices, and invalid checksums. Run `./gradlew :app:testDebugUnitTest --tests '*AppUpdateRepositoryTest'` from `android/` and confirm red before implementation.
2. Extend `BrowserPreferences.kt` with the automatic-check toggle, attempted-check timestamp, and cached release. Add `AppUpdateManager.kt` to serialize checks and download requests. Test disabled/daily/manual checks, failure throttling, cached state, and download reuse.
3. Persist optional update metadata in `DownloadModels.kt` and `SystemDownloadRepository.kt`. Copy completed update APKs to private storage while hashing, validate package/version, and expose only the verified file through FileProvider. Add tests for corrupt and mismatched APKs and metadata restoration.
4. Add the About update card and Settings hint. Connect startup checks in `MainActivity.kt`. Route both update installation entry points through the same validation and permission-return flow; preserve ordinary download opening behavior.
5. Update English/Chinese string resources, `docs/features-android.md`, and `docs/feature-checklist.md`. Run focused Android unit tests and lint. Inspect available device support and report separately whether installer/UI behavior was exercised on a device.

## Acceptance checks

- Desktop tags and prereleases never become Android updates; 0.1.10 sorts after 0.1.9.
- Automatic checks run at most daily, including failures; manual checks bypass the interval but respect server rate limits.
- A failed request never reports that the app is current.
- Rotation/reopening does not create duplicate downloads; cancellation and retry remain available.
- Installation rejects missing/corrupted files, other package names, unexpected versions, and downgrades, from both About and Downloads.
- Permission denial leaves the downloaded file available; granting permission resumes verification and installation.
