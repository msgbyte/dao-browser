# Unified Desktop Releases and Windows Automatic Updates

Status: approved by the user on 2026-10-11; implementation and verification are
tracked in the [implementation plan](../plans/2026-10-11-desktop-release-windows-updates.md).

## Intent and scope

The requested outcome is one command that builds and uploads the macOS and
Windows installers for one desktop version, a separate macOS release command,
and automatic updates for installed Windows browsers.

The approved Windows interaction is background checking and downloading, followed
by an explicit click on the existing sidebar update button to install and
restart.

Work stays in the primary checkout on `main`. Canonical browser changes belong in
`src/dao/` and `src/patches/`. Compilation verification uses only `npm run rebuild`.
Implementing this design does not authorize running a production release,
uploading artifacts, changing Git state, exporting private keys, or force-importing
Chromium sources.

## Behavior before this change

- `release` currently defaults to macOS, bumps the desktop version, signs and
  notarizes a DMG, generates the Sparkle appcast, uploads to R2, and creates a
  local tag. Commit, push, and website deployment remain manual steps.
- `release:windows` currently reuses the desktop version and attaches an unsigned
  EXE and checksum to a GitHub Release. Publication requires matching published
  source tags, so chaining it after the current macOS command cannot implement
  one uninterrupted dual-platform release.
- Windows cross-compilation already works on this Mac and uses a separate cache.
  The imported Chromium source tree is shared, so platform builds must be serial.
- `DaoUpdaterService` already exposes shared update state and an apply action.
  Its only backend is Sparkle on macOS; Windows returns unsupported.
- The Windows installer reuses Chromium's `mini_installer`, remembers the
  registered installation directory, and stores browsing data separately.
- Native installation directories use the Chromium version. Dao display
  releases can therefore exercise Chromium's same-version repair path.

## Approaches considered

1. **Reuse Dao's update service, Chromium networking/cryptography, and the existing
   installer.** Recommended: preserves the sidebar interaction and cross-build
   pipeline without another runtime dependency. Dao must own the small update
   manifest and safe install/restart protocol.
2. **Add WinSparkle.** Provides an established signed-update implementation, but
   introduces a Windows DLL, its packaging and update policy, and integration
   work for Dao's existing sidebar and shutdown behavior.
3. **Deploy Chromium Updater.** Offers a broader updater infrastructure but adds
   service registration and update-server requirements beyond this request.

## Release commands and publication

- `npm run release`: selects both platforms, increments the desktop version once,
  archives desktop release notes once, and runs macOS then Windows serially.
- `npm run release:mac`: retains the macOS-only release workflow.
- `npm run release:windows`: retains the current desktop version and produces the
  Windows artifact. Existing GitHub attachment behavior remains available for
  native Windows maintainers; the combined macOS-hosted workflow is the initial
  signing and publication path for automatic Windows updates.
- Explicit CLI selection supports `--platform mac`, `windows`, or `all`; the
  default is `all`. `release:minor` and `release:major` inherit the combined default.
- Normal import is the default. The release pipeline must not implicitly reset
  the imported source tree or native build caches.

The combined command uses one release transaction. It builds both packages and
completes macOS notarization before uploading either. Both installers, the
Windows checksum, and immutable signed Windows update metadata are uploaded to
the existing R2 release origin. Only after all required objects are available
does it publish the Windows latest-update manifest. Website metadata then records
both download URLs and their common version.

This preserves the existing macOS meaning of upload: R2 receives the artifacts
during `release`. Git commits and pushes remain manual. The existing tag-triggered
GitHub workflow is extended to archive both available platform artifacts from R2
under one release. A macOS-only release remains valid when no Windows artifact
exists, while network failures must not be mistaken for absent Windows assets.

`--dry-run` prints both platform phases without mutations. `--skip-upload`
produces artifacts without changing remote update feeds; existing macOS version,
local-tag, and metadata semantics remain explicit. `--skip-build` requires both
artifacts for a combined release. A notarization-only recovery remains macOS-only
and tells the maintainer to use `release:mac` rather than silently omitting Windows.

Local failures retain existing transaction conflict detection. Uploaded immutable
objects cannot be rolled back like local files. Once an update feed has been
published, a later error must report a partially published version and preserve
recovery information; it must not imply that public availability was undone.
Retries must reject replacing a published version with different artifact bytes.

## Windows update contract

The client fetches `https://dao-release.msgbyte.com/windows-update.json` over HTTPS.
The bounded JSON envelope contains an exact signed payload string and its
base64 Ed25519 signature. The payload includes a schema version, platform and
architecture, Dao display version, Chromium version, installer URL, byte length,
and SHA-256 digest. The signature binds these fields together.

The release process signs the payload with the existing Sparkle `sign_update`
tool and the existing keychain key. It does not export the private key. The
Windows client embeds the matching public key and uses Chromium's existing
cryptography library to verify the original payload bytes before parsing their
meaning. Publication verifies that the signature matches the embedded key.
The Windows publisher initially runs on the macOS signing host; local native
Windows build/package and the existing GitHub-only attachment remain supported.
Unsigned GitHub attachments do not advance the automatic-update feed.

Validation requires a supported schema, Windows x64, a strictly newer valid Dao
version, a non-decreasing Chromium version, an expected HTTPS artifact URL under
the fixed release origin, a positive bounded byte length, and a valid hash.
Neither the feed nor redirects may supply arbitrary commands, install arguments,
filesystem paths, or alternative origins. There is no automatic downgrade.

Downloads use Chromium's browser-process network service without cookies or
profile credentials. The manifest is limited to 64 KiB and the installer to
1 GiB, with request timeouts and a single in-flight check/download. The manifest
request bypasses stale caches. Downloads go into a private per-user update cache,
are streamed to a temporary file, checked for size and SHA-256, and only then
renamed to a ready artifact. Installation revalidates the cached artifact.

## Scheduling and browser integration

- Start once after the initial regular Windows profile is available.
- Check shortly after startup and every 24 hours while running; persist the last
  successful check so restarting does not cause repeated large downloads.
- Ignore Incognito-only, Guest-only, development, portable, and unregistered
  installations. Only enable applying updates when the running executable belongs
  to the registered per-user Dao installation.
- Keep background failures quiet and retry on the normal schedule; do not report
  failed requests as an up-to-date result.
- Add a manual check in Settings > About, routed through the Dao service instead
  of Chromium's unsupported update backend on Windows. Provide localized checking,
  current-version, download, ready, and failure feedback.
- Reuse the existing sidebar ready/applying states and button. Multiple windows
  share one process-wide operation, and duplicate apply clicks cannot launch
  multiple installers.

## Safe application and restart

An explicit update click starts a normal browser shutdown that respects pending
downloads and page unload prompts. Cancelling shutdown cancels the pending
installation and restores the ready state. No force-kill is used.

The existing Windows installer gains a narrow update mode. It coordinates with
the browser using a validated handoff, waits for the originating browser to exit,
and enforces one updater for the registered installation. Installation must not
begin while another Dao process still uses that installation. A cancelled or
timed-out handoff must not become an installation on a later unrelated exit.

The installer uses the registered directory and fixed silent-install arguments;
it cannot relocate an existing installation. It delegates replacement and
rollback to `mini_installer`, preserves user profiles, and relaunches the registered
browser only after successful installation. Restart restores the normal browsing
session without replaying arbitrary command-line arguments or private windows.
Installation failure preserves an actionable localized error and log path and
does not enter an automatic restart/install loop.

## Change scope

Release work touches `package.json`, `scripts/commands/release.ts`, its transaction
and regression tests, Windows packaging/signing metadata, GitHub archival logic,
and `.github/workflows/publish-github-release.yml`.

Browser work adds the Windows backend under `src/dao/browser/updater/`, extends
the existing service and its build target, and integrates startup/shutdown and
Settings through tracked Chromium patches. It reuses the sidebar bridge and
update button. The existing Windows installer owns installation coordination.
User-visible strings go through `dao_strings.grd` and existing installer locale
files, with hand-authored English and Simplified Chinese only.

The implementation updates `docs/features.md`, `docs/feature-checklist.md`,
`docs/development.md`, `docs/release-signing.md`, relevant command examples, and
matching desktop Unreleased entries in both hand-maintained changelogs. No paid
translation runs automatically.

## Verification and acceptance

- Focused release tests cover default/all/mac/windows routing, one version bump,
  serial builds, both artifacts before publication, feed-last ordering, missing
  artifacts, changed-artifact retries, cancellation, and publication recovery.
- Manifest tests cover valid signatures, tampering, malformed and oversized
  input, wrong platform, hostile URLs/redirects, size/hash mismatch, version
  comparison, and rejected downgrades. Use a deterministic test key only.
- Updater tests cover scheduling, one active download, persisted cache validation,
  registered-install eligibility, manual error feedback, multiple windows, and
  duplicate apply requests.
- Installer tests cover cancelled shutdown, timeout, another browser process,
  existing custom directories, same-Chromium-version Dao upgrades, failed install,
  and successful session-restoring restart.
- Run focused TypeScript/WebUI tests, relevant Lit lint, and documentation checks.
- Confirm native and Windows compilation serially using only `npm run rebuild`
  with explicit platform/flavor/targets. Package the Windows EXE locally.
- A real Windows install/upgrade/relaunch test is required to validate runtime
  behavior; cross-compilation and fixture tests alone must not be reported as
  end-to-end Windows verification. Production publishing is not part of this
  implementation task.

## References

- [Existing release implementation](../../../scripts/commands/release.ts)
- [Existing updater service](../../../src/dao/browser/updater/dao_updater_service.cc)
- [Existing installer](../../../scripts/windows-installer/installer.nsi)
- [Sparkle signing documentation](https://sparkle-project.org/documentation/)
- [WinSparkle integration and shutdown callbacks](https://github.com/vslavik/winsparkle/blob/master/include/winsparkle.h)
