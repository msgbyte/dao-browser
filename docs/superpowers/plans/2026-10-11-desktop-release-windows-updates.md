# Unified Desktop Releases and Windows Updates Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` for native execution,
> or `superpowers:subagent-driven-development` if the user selects delegation.
> Follow this plan task by task and read the approved spec first.

**Goal:** Add `release:mac`, make the default desktop release build and upload
both installers, and deliver signed Windows updates through the existing Dao UI.

**Architecture:** Extend the existing release transaction and updater facade.
Publish both installers to R2 before making a signed Windows feed current;
reuse Chromium networking and cryptography plus the existing Windows installer
for checking, downloading, and a cancellable install/restart handoff.

**Tech Stack:** TypeScript, Commander, Vitest, Chromium C++, Lit WebUI, Win32,
NSIS, Sparkle signing tools, and existing GitHub Actions.

**Spec:** [Approved design](../specs/2026-10-11-desktop-release-windows-updates-design.md)

**Status:** Implemented. Local compilation, packaging, and focused tests passed
on 2026-10-11; Windows runtime acceptance remains open below.
Native execution is recommended because the publication, client, and installer
share one security-sensitive protocol and all Chromium imports are serialized.

The release and client tasks share a signed manifest and installation protocol;
they remain in one plan to prevent incompatible producer/consumer changes.

## Global Constraints

- Work in the primary checkout on `main`; do not create branches or worktrees.
- Keep browser edits in `src/dao/` and `src/patches/`; never hand-edit `engine/`.
- Use only `npm run rebuild` for compilation verification and ordinary import.
- Do not run a production release, upload, mutate Git state, export a private key,
  invoke paid translation, or force-import as part of implementation.
- Build macOS and Windows serially; imported Chromium sources are shared.
- Use the existing R2 origin `https://dao-release.msgbyte.com` and feed path
  `/windows-update.json`; no additional service or runtime dependency.
- Limit update manifests to 64 KiB and installers to 1 GiB; check every 24 hours.
- Require explicit user application, preserve profiles, respect cancelled
  shutdown, and never force-kill a browser to install an update.
- Keep code/docs/tests in English; localize UI through existing systems and
  hand-author English and Simplified Chinese. Preserve macOS Sparkle behavior.
- Run focused tests, batch browser edits before builds, and distinguish Windows
  compilation from real Windows install/upgrade/relaunch verification.

## Review Focus

1. An interrupted latest-feed upload may already be public: preserve candidate
   identity and report uncertain publication, never silently reuse its version.
2. A valid manifest replayed with an old or different-platform executable must
   fail version, URL, length, or hash checks before any executable is launched.
3. A user cancels a page unload prompt and quits later: the abandoned update
   attempt must remain cancelled, including across process-ID reuse.
4. A second Dao process or a changed registry entry appears during handoff:
   installation must fail safely without relocating or replacing active files.
5. Dao advances while Chromium stays fixed: the same-version repair must replace
   browser bytes and preserve profile data and session restoration.

## Task 1: Define and test the signed Windows update artifact

**Files:** Create `scripts/windows-update.ts`,
`scripts/commands/__tests__/windows-update.test.ts`,
`src/dao/browser/updater/dao_update_manifest.h`,
`src/dao/browser/updater/dao_update_manifest.cc`, and
`src/dao/browser/updater/dao_update_manifest_unittest.cc`.
Modify `src/dao/browser/updater/BUILD.gn`; use the existing public key from
`src/patches/chrome/app/app-Info.plist.patch` and assert agreement in tests.

**Interfaces:** `WindowsUpdatePayload` has `schema: 1`, `platform: 'windows'`,
`arch: 'x64'`, `version`, `chromiumVersion`, `url`, `size`, and `sha256`.
The envelope is `{payload: string, signature: string}`; verify UTF-8 payload
bytes exactly, without JSON reserialization. `createWindowsUpdatePayload(
artifact: string, version: string, chromiumVersion: string): Promise<string>`
hashes the artifact and returns payload bytes. `signWindowsUpdatePayload(
payload: string, signal?: AbortSignal): Promise<string>` returns the envelope.
Versioned metadata is `dao-browser-<version>-windows-x64.update.json`.

C++ exposes `ParseSignedWindowsUpdate(std::string_view envelope,
const base::Version& current_dao, const base::Version& current_chromium)` returning
`base::expected<WindowsUpdateManifest, WindowsUpdateManifestError>`. The result
includes `kNoUpdate` separately from errors. `WindowsUpdateManifest` carries
validated versions, a `GURL`, byte length, and decoded SHA-256. Signature
verification occurs before interpreting the payload. An internal test helper
accepts a test public key; production callers cannot override the embedded key.

- [ ] Write tests for a correctly signed payload, modified payload/signature,
  wrong key, invalid base64, duplicate/invalid fields, wrong schema/platform/arch,
  oversized envelope, zero/oversized artifact, credentials/ports/fragments in URL,
  another origin, mismatched version filename, non-increasing Dao version, and
  decreasing Chromium version. Add one shared deterministic fixture signed with
  a test-only Ed25519 key for TypeScript/C++ agreement.
- [ ] Run the focused Vitest test and compile/run a small
  `dao_updater_unittests` target through `npm run rebuild`; observe the expected
  failures before implementing the parser. Keep this target free of browser UI
  dependencies so native macOS can exercise signature/version validation.
- [x] Implement hashing with streaming reads, signing through existing
  `third_party/sparkle/bin/sign_update`, and signature verification with Node
  crypto/Chromium crypto. Use temporary payload files and clean them on failure;
  never log or export private keys. Check the signing result against the embedded
  public key before accepting it. Treat versions as numeric components.
- [ ] Rerun the tests; verify both languages accept the same fixture and reject
  each mutation. Preserve SHA-256 sidecars used by existing Windows publishing.

## Task 2: Build both platforms in one release transaction

**Files:** Modify `package.json`, `scripts/commands/release.ts`,
`scripts/commands/release-transaction.ts` only where recovery requires it,
`scripts/commands/__tests__/release.test.ts`,
`scripts/commands/__tests__/release-transaction.test.ts`,
`scripts/commands/__tests__/windows-release.test.ts`, and
`scripts/commands/__tests__/package_scripts.test.ts`.

**Interfaces:** Extend `ReleaseOptions.platform` to `'mac' | 'windows' | 'all'`,
default `'all'`. Add `ReleaseStep = {phase: ReleasePhase, platform?: 'mac' |
'windows'}` and `plannedReleaseSteps(options): ReleaseStep[]` so errors and
dry-run output identify the platform without creating two transactions.
Retain `runRelease(options, dependencies): Promise<ReleaseResult>`.
Add Windows artifact/manifest paths and publication state to the existing
release context/recovery data. Publication state distinguishes immutable files
uploaded, latest-feed upload started, and latest-feed upload confirmed.

- [x] Add failing tests asserting `release:mac` selects macOS, default/all and
  minor/major select both, Windows-only retains its version, and a combined run
  bumps/archives/tags once. Make existing macOS tests select `platform: 'mac'`
  explicitly instead of accidentally testing the changed default.
- [x] Add phase tests: preflight both toolchains/signing tools before mutation;
  import/build/package/notarize macOS; import/build/package Windows; sign update
  metadata; prepare appcast; validate artifacts; upload; mutate final metadata;
  finish the existing local-tag transaction. No upload may occur before both
  packages are ready. Test normal import, cancellation, and Windows failure
  after a successful Mac build restoring local metadata.
- [x] Run `npx vitest run scripts/commands/__tests__/release.test.ts
  scripts/commands/__tests__/windows-release.test.ts
  scripts/commands/__tests__/release-transaction.test.ts
  scripts/commands/__tests__/package_scripts.test.ts`; confirm new cases fail.
- [x] Implement the steps using existing build/package helpers and platform
  caches. Default force import to false and preserve `--no-force-import`
  compatibility. Reject combined `--resume-from-staple` with a `release:mac`
  recovery command. Validate both files/signatures for `--skip-build`; dry runs
  must not access keychain secrets or create artifact files.
- [x] Test and implement publication recovery: before latest-feed upload, local
  rollback is allowed; from the instant that upload starts, retain candidate
  version/recovery information on failure and require verification/retry of that
  same candidate. Do not delete public artifacts or claim remote rollback.
- [x] Rerun these tests and typecheck. Inspect help/dry-run output using mocked
  release dependencies, without running a real release or creating Git tags.

## Task 3: Upload immutable artifacts, publish the feed last, archive on GitHub

**Files:** Modify `scripts/windows-update.ts`, `scripts/commands/release.ts`,
`scripts/commands/github-release.ts`, `.github/workflows/publish-github-release.yml`,
`scripts/commands/__tests__/windows-update.test.ts`,
`scripts/commands/__tests__/github-release.test.ts`, and
`scripts/commands/__tests__/github-windows-release.test.ts`.

**Interfaces:** The combined uploader uses existing `upload` for immutable DMG,
EXE, checksum, versioned update envelope, and current-candidate Mac deltas.
`publishWindowsUpdateFeed(manifestPath: string, options, signal?)` uploads the
same verified envelope under `windows-update.json` after artifact verification.
Extend the GitHub archival command with an explicit desktop/all selection; keep
macOS-only historical backfills and native Windows local attachments compatible.

- [x] Add failing tests for immutable-asset existence checks, identical retry,
  different-byte conflict, current feed newer than candidate, missing checksum,
  signing failure, feed-last ordering, HTTP 404 versus timeout/403/5xx, and an
  upload that takes effect remotely but reports an error locally.
- [x] Implement publication checks with fixed HTTPS origin and bounded metadata
  responses. Reject a prefix that would violate the signed URL contract in a
  combined release rather than silently publishing an unusable feed. Confirm
  downloaded/public metadata matches the candidate; prevent intentional older
  feed publication. Document that simultaneous release publishers are unsupported
  and reject a changed latest-feed snapshot detected before promotion.
- [x] Add GitHub tests proving a tagged dual release archives DMG, EXE, checksum,
  and versioned signed metadata without replacing existing assets. A real 404 for
  Windows metadata permits macOS-only archival; all other failures fail the job.
- [x] Update the tag workflow to select both platforms and retain existing
  per-tag concurrency and permissions. Preserve standalone Windows attachment's
  source/tag checks; it must not promote the automatic-update feed.
- [x] Run only the affected GitHub/update/release Vitest suites. Verify website
  metadata contains both R2 URLs for combined releases and preserves the last
  Windows URL for `release:mac`.

## Task 4: Add the Windows updater backend and download lifecycle

**Files:** Create `src/dao/browser/updater/dao_windows_updater.h`,
`dao_windows_updater.cc`, and focused tests in that directory.
Modify `dao_updater_service.{h,cc}`, `BUILD.gn`, and the shared service tests in
`src/dao/browser/ui/views/dao_browser_browsertest.cc`.
Create `src/patches/chrome/browser/chrome_browser_main_win.cc.patch`; adjust
`src/patches/chrome/browser/BUILD.gn.patch` for the Windows service dependency.

**Interfaces:** `DaoWindowsUpdater` exposes
`void Start(StatusCallback, scoped_refptr<network::SharedURLLoaderFactory>)`,
`void CheckForUpdates(bool user_initiated)`, `bool ApplyReadyUpdate()`, and
`void Shutdown()`. `StatusCallback` is
`base::RepeatingCallback<void(const DaoUpdateStatus&)>`; extend the enum with checking,
downloading, up-to-date, and error states plus an error enum, keeping ready/applying
semantics and Mac behavior. The backend owns timers, loaders, validated manifest,
cache files, and installation handoff; callers never supply a download URL.

- [ ] Test eligibility against registered per-user installation, a mismatched
  running executable, HKLM/system installation, alternate user data, component
  builds, guest/incognito-only startup, and multiple normal profiles.
- [ ] Test mock-time scheduling: first due check 30 seconds after startup,
  subsequent checks at 24 hours, saved last-success time, future/corrupt saved
  timestamps, no concurrent check/download, and manual check bypassing schedule.
- [ ] Test network lifecycle with Chromium's test URL loader: response limits,
  rejected redirects, offline/error status, cancellation/shutdown, progress
  exceeding signed size, truncated download, hash mismatch, and a mutated cached
  installer before apply. Background errors stay quiet; manual errors remain
  distinguishable from no update.
- [x] Implement the backend using `SimpleURLLoader`, browser-process networking,
  no cookies/credentials, a 30-second manifest timeout and a 30-minute download
  timeout, worker-thread hashing/file operations, and weak callbacks. Use a
  per-user `Dao/Updates` cache with user-only permissions and reject reparse
  points; stream into a unique temporary file and rename only after validation.
  Save success time and the signed ready envelope atomically; revalidate cached
  payload and installer on startup. Never follow persisted arbitrary paths.
- [x] Initialize only from a normal initial Windows profile and shut down
  explicitly before browser-process networking is torn down. Add a build define
  from `dao_display_version` for Dao comparison; keep Chromium's internal version
  separate and normalize its four-part Dao build value consistently.
- [ ] Add service tests for ready metadata, observers, reentrancy, applying once,
  failure recovery, and shutdown cancellation; enable shared supported-platform
  cases on Windows while preserving unsupported-platform assertions elsewhere.
  Run portable validator/state cases locally; compile Windows-only tests and
  explicitly record any runtime cases requiring a Windows host.

## Task 5: Implement cancellable installation and session-restoring restart

**Files:** Modify `scripts/windows-installer/native/host.cc`,
`scripts/windows-installer/installer.nsi`, installer locale sources,
`scripts/commands/build-windows-installer.ts`,
`scripts/commands/__tests__/build-windows-installer.test.ts`,
`scripts/commands/__tests__/windows-installer.test.ts`, and
`scripts/tests/test_windows_installer.ps1`; add an isolated Windows handoff test
under `scripts/tests/`. Integrate `dao_windows_updater.{h,cc}`.

**Interfaces:** NSIS accepts one narrow `/DAOUPDATE=<token>` mode before normal
wizard handling, extracts its existing host, and delegates without falling back
to interactive installation on handshake failure. The token names current-user
restricted coordination objects; it carries no shell command or path. The
browser is the server of a current-user-only named pipe. The host authenticates
the server's PID, creation time, executable and registered installation; the
browser authenticates the extracted signed-artifact host process before arming.
Messages are fixed `ready`, `commit`, and `cancel` records with a bounded size.
The host uses a fixed per-installation mutex, a held parent process handle, and a
five-minute deadline; missing commit, cancellation, disconnection before commit,
or timeout aborts. Installation requires commit plus confirmed browser exit.

- [ ] Add fixture-based handoff tests for valid parent shutdown, cancelled page
  unload, cancellation followed by later unrelated exit, PID reuse/wrong creation
  time, unauthorized pipe peer, duplicate updater, missing ready/commit, timeout,
  registry change, a second running Dao process, and non-ASCII custom paths.
  Fixtures must never install into or unregister the real Dao installation.
- [x] Implement `/DAOUPDATE` dispatch before COM/WebView initialization so updates
  work without WebView2. Validate all registration/executable paths by opened-file
  identity and enforce an appropriate current-user security descriptor on IPC.
  The client holds/rechecks the verified artifact against replacement until launch.
- [x] After authenticated ready, request ordinary browser shutdown with existing
  unload/download confirmation behavior. Send cancel if closure is cancelled;
  restore ready state. Send commit only at the irreversible graceful-exit hook,
  never merely because closing started. Unexpected browser termination before
  commit cannot authorize installation. Mark normal profiles for clean session
  restoration without bypassing unload prompts or preserving private sessions.
- [x] After commit and parent exit, validate no process still uses that installed
  browser path and recheck registration. Launch the same NSIS artifact with fixed
  `/S` arguments and its registered directory; recursion cannot re-enter update
  mode. Reuse installer rollback. Relaunch only the registered executable with
  `--restore-last-session` after success, under the original user context.
  Persist localized failure details/log path for the next start, with no retry loop.
- [ ] Test same-Chromium-version repair replaces Dao files, native upgrade preserves
  browsing data, failure never launches an unverified/new path, and success launches
  once. Update host input hashing/build commands for every new native source.
  Run TypeScript fixture checks locally; execute the Win32 fixture suite on Windows
  when available and retain its unrun status otherwise.

## Task 6: Expose Windows manual checks and reuse the sidebar update control

**Files:** Modify `src/patches/chrome/browser/ui/webui/settings/about_handler.{h,cc}.patch`,
the three `src/patches/chrome/browser/resources/settings/about_page/` patches,
`src/patches/chrome/test/data/webui/settings/test_about_page_browser_proxy.ts.patch`,
`src/dao/browser/ui/webui/dao_sidebar_ui.cc`,
`src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`,
`src/dao/browser/ui/webui/resources/sidebar/__tests__/update_button.test.ts`,
`src/dao/browser/strings/dao_strings.grd`,
`src/dao/browser/strings/translations/dao_strings_zh-CN.xtb`, and existing updater
localization tests.

**Interfaces:** Extend `daoCheckForUpdates` routing to Windows and add a bounded
`daoUpdateStatusChanged` Settings event sourced from `DaoUpdaterService`.
Settings subscribes/unsubscribes with page lifetime and requests current state.
The sidebar continues showing its button only for ready/applying; new transient
states do not create an empty toolbar item or change the download button.

- [ ] Add failing tests for checking/downloading/current/failed/ready feedback,
  unsupported installation explanation, button debouncing, late subscription,
  multiple-window apply, and cancellation restoring ready. Verify Windows no
  longer auto-fires Chromium's unsupported updater; Mac still uses Sparkle.
- [x] Implement localized status text and manual actions through the existing
  Settings bridge and observer lifecycle. Keep backend enums independent of UI
  strings. Reuse existing icons; no new framework, toast type, or hardcoded copy.
- [ ] Run focused WebUI/localization tests and `npm run lint:lit`; verify keyboard
  access, status announcement, and sidebar visibility for every state.

## Task 7: Documentation, integration review, and final build verification

**Files:** Update `docs/features.md`, `docs/feature-checklist.md`,
`docs/development.md`, `docs/release-signing.md`, `README.md` where commands change,
and matching `docs/changelog/en.md` / `zh-CN.md` desktop Unreleased entries.
Review every release caller/test and the final diff; preserve unrelated work.

- [x] Document `release`, `release:mac`, and `release:windows`, R2-first publication,
  manual Git steps, signed-feed/macOS signing requirements, retry semantics, and
  the distinction between package signing and Windows Authenticode signing.
  Replace the planning-only changelog entry with implemented outcomes; do not
  claim Windows runtime verification without evidence.
- [x] Add regression checklist entries for both-platform failure/cancellation,
  signed metadata tampering, network/download failure, uninstall/portable exclusion,
  other running processes, cancelled exit, same-engine upgrades, and profile/session
  preservation. Update obsolete claims that Windows has no automatic updater.
- [x] Run the focused release/update/GitHub/installer suites, `npm run typecheck`,
  relevant WebUI suites, `npm run lint:lit`, `npm run docs:check`, and
  `git diff --check`. Fix relevant failures before compiling; record unrelated ones.
- [x] Confirm native shared changes with `npm run rebuild -- -- --target chrome
  dao_updater_unittests -j 4`; run `engine/src/out/dao-debug/dao_updater_unittests`.
- [x] Confirm Windows serially with `DAO_BUILD_PLATFORM=windows npm run rebuild
  -- -- --release --target mini_installer dao_updater_unittests -j 4`, then
  `DAO_BUILD_PLATFORM=windows npm run rebuild -- -- --release --target dao_installer_ui`.
  Run `npm run package -- --platform windows` and verify the EXE checksum and PE
  architecture. These commands are local verification, not a release.
- [ ] On an available disposable Windows environment, run the handoff fixtures
  and signed test-feed install/upgrade/relaunch flow. Include a Dao-only version
  increase with unchanged Chromium. If no Windows environment is available,
  explicitly leave this acceptance item unverified rather than emulating success.
- [x] Complete an independent final review according to the selected execution
  method, address findings, and report implemented behavior, actual test/build
  results, artifact paths, and remaining Windows runtime limitations.

## Execution handoff

Native execution in this session is recommended. Subagent-driven execution is
also possible after the user selects it; Chromium imports/builds remain strictly
serial. No commit or push is included in either execution method.


## Execution notes

- Implementation stays on `main` in the existing checkout; repository rules
  override the general skill recommendation to create a worktree. No Git
  mutations or production publication are part of this verification.
- The CLI defaults to `all`. The internal `runRelease` API keeps its existing
  undefined-platform Mac behavior for compatibility with current direct callers;
  combined callers pass `platform: 'all'` explicitly.
- Native changes were batched before compile verification to preserve the warm
  Chromium caches. Compilation only uses `npm run rebuild`.
- Independent review identified partial-upload retry, session preservation before
  cancellation, stale cache files, and native test coverage. Recovery now retains
  artifact bytes and checks public candidates before rebuild; session preservation
  starts after all close prompts; cache cleanup is restricted to owned filenames;
  focused Windows scheduling, URL-loader and commit/exit tests were added.
  Windows starts with an unsupported status until an eligible normal profile
  initializes its updater, so private-only startup has no inactive check action.
- The original checklist retains unchecked mixed acceptance items where the full
  Windows runtime matrix has not been exercised. The compiled unit cases cover
  portable eligibility, request credentials/redirects/limits, manual and timed
  checks, shutdown, corrupted downloads, and commit versus process exit. They do
  not establish complete installed-browser behavior on this macOS host.
- Protocol fixtures reuse the compiled Win32 C++ helpers rather than adding a
  second installer driver. Registry replacement, simultaneous browser processes,
  non-ASCII installation, same-engine repair and session restoration remain
  explicitly unverified until an isolated Windows end-to-end run.


## Verification results (2026-10-11)

| Check | Result |
| --- | --- |
| Focused Vitest release/update/GitHub/installer/Settings/sidebar suites | 17 suites, 237 tests passed |
| `npm run typecheck` | Passed |
| `npm run lint:lit` | Passed; 225 files, no violations |
| `npm run docs:check` and `git diff --check` | Passed |
| macOS `npm run rebuild -- -- --target chrome dao_updater_unittests -j 4` | Passed; final build completed 49 steps |
| macOS `dao_updater_unittests --gtest_filter=DaoUpdateManifestTest.*` | 3 tests passed |
| Windows `DAO_BUILD_PLATFORM=windows npm run rebuild -- -- --release --target mini_installer dao_updater_unittests -j 4` | Passed; final build completed 234 steps; Windows test executable compiled, not run |
| Windows installer host through `npm run rebuild` | Passed |
| `npm run package -- --platform windows` | Passed; generated `dist/dao-browser-1.0.109-windows-x64.exe` (122,621,782 bytes) and checksum |
| `shasum -a 256 -c dao-browser-1.0.109-windows-x64.exe.sha256` | Passed |
| PE architecture inspection | Browser, helper, and test executable are x64; NSIS uses its normal x86 bootstrap wrapper |

Build/test logs are retained locally in `.dao/logs/desktop-updates/`. No version
bump, Git mutation, production signing, R2/GitHub publication, or website deployment
was performed. The Windows installer has no Authenticode signature. No new Mac
release DMG was signed or notarized during this implementation.

Windows-only unit execution, real registered install/upgrade, cancellation and
later quit, another active browser, registry changes, same-Chromium-version repair,
non-ASCII custom paths, and restored sessions remain unverified on a real Windows
host. Use a disposable installation for these acceptance checks before shipping.
