# macOS Startup Diagnostics Implementation Plan

> **For agentic workers:** Use systematic debugging and executing-plans in the primary checkout. Independent ANGLE and Crashpad dependency investigations may run in parallel; the parent owns import, compilation, and runtime checks.

**Goal:** Correct the reported duplicate Objective-C registrations and false quarantine-removal error, and establish whether the VideoToolbox error affects playback.

**Architecture:** Give each Objective-C implementation one shared-library owner instead of suppressing runtime diagnostics. Preserve quarantine removal and report genuine failures. Reproduce video behavior before changing codec policy.

**Tech Stack:** Chromium 154, GN, C++/Objective-C++, existing browser_tests, disposable CDP runtime checks.

**Spec:** The user's post-migration startup log and repair request.

## Global Constraints

- Work on `main`; no state-changing Git commands or worktrees.
- Modify canonical Dao sources and patches only; import through the existing CLI.
- Compile only with `npm run rebuild`, retaining the warm component-build cache.
- Preserve GPU acceleration, sandboxing, crash reporting, installed apps, and user profiles.
- Keep scratch and backups under `/tmp/dao-m154-startup-fix-idltvijt`; remove them after successful verification.

## Review Focus

- Ordinary WebGL and WebGLOnWebGPU must retain the same ANGLE entry points.
- Crashpad handler and iOS dependencies must retain their implementation symbols.
- Missing quarantine on a protected directory is success; an existing protected attribute is still an error.
- Missing paths must remain errors, and normal quarantine removal must still work.
- Video diagnostics must distinguish successful fallback from a playback failure.

## Tasks

- [x] Reproduce the duplicate-class and quarantine startup diagnostics and collect WebGL pixel checks plus H.264, VP8, VP9, AV1, and HEVC playback/decoder results with a disposable profile.
- [x] Remove Blink's duplicate static ANGLE linkage by obtaining its proc resolver from the existing GL init component. Patch the GL init header/implementation and Blink WebGL source, GN dependencies, and include allowlist.
- [x] Replace the new Crashpad app-to-handler implementation dependency with the existing header-only interface, while preserving handler implementation and iOS linkage.
- [x] Add Mac quarantine regressions to the existing Dao browser test source. Verify the missing-attribute case on an immutable temporary directory fails before patching `base/mac/mac_util.mm`; cover a protected present attribute, normal removal, and a missing path.
- [x] Patch the common quarantine helper to accept a failed removal only when a follow-up attribute read proves the attribute is absent.
- [x] Run the normal rebuild for `chrome` and `browser_tests`, execute the focused native cases, and rerun startup/WebGL/media checks.
- [x] Update relevant documentation, record evidence and limits, review the final diff, and remove task-owned temporary files.

## Initial Evidence

- The system `X/` directory has `com.apple.rootless` but no quarantine attribute. `getxattr` returns `ENOATTR` (93), while removing the already-absent attribute returns `EPERM` (1). An immutable task-owned directory reproduces the same result without modifying system protections.
- ANGLE's same `WindowSurfaceCGL.o` is linked into both Blink modules and GL init.
- Chromium 154's crash app target adds the handler common dependency on all platforms; that pulls the Objective-C HTTP transport into both UI views and the main browser library.
- The original failed-video URL and visible symptom are requested from the user; local fixture playback is independently testable.

## Verification Results

- Both builds completed with exit 0 using `npm run rebuild -- -- -j 4 --target chrome browser_tests`. The final plain `npm run import` reported 296 already-applied patches, zero failures, and no Dao source changes.
- Before the fixes, the disposable-profile probe recorded 58 duplicate-class warnings and two quarantine-removal errors. The final default-path probe and both independent experimental-path probes recorded zero of either warning.
- `nm` confirms `ANGLESwapCGLLayer` is defined only in `libgl_init.dylib`, not `libblink_modules.dylib`. `CrashpadHTTPBodyStreamTransport` is absent from both browser libraries and remains defined in `chrome_crashpad_handler`.
- Before the quarantine helper patch, the protected-absent-attribute case failed at the expected `EXPECT_TRUE`, while the ordinary removal case passed. After the patch, both `DaoMacQuarantineTest.*` tests passed with retries disabled. The tests also check that a protected existing attribute remains present and reports failure.
- The rebuilt app reports Chromium `154.0.8037.100`. Default WebGL 1 and WebGL 2 both produced `[255, 0, 0, 255]` pixels with no GL error or lost context on the Apple M4 Metal renderer.
- All six local video fixtures decoded frames and advanced beyond 0.5 seconds without media errors. The following decoder names were reported by CDP; platform decoder selection alone is not a claim that every codec used hardware decoding.

| Fixture | Decoder |
| --- | --- |
| `bear.mp4` (H.264) | `FFmpegVideoDecoder` |
| `bear-320x240.webm` (VP8) | `VpxVideoDecoder` |
| `bear-1280x720-hevc.mp4` | `VideoToolboxVideoDecoder` |
| `bear-1280x720-hevc-10bit.mp4` | `VideoToolboxVideoDecoder` |
| `bear-av1.mp4` | `Dav1dVideoDecoder` |
| `bear-320x240-vp9_profile2.webm` | `VpxVideoDecoder` |

- Each WebGLOnWebGPU context type passed the same pixel/error/context assertions in its own process with `--enable-blink-features=WebGLOnWebGPU`. This exercises the new shared ANGLE resolver.
- `npm run docs:check` and the focused source whitespace check passed. A separate read-only review found no blocking issue in the eight new patches or quarantine tests.
- After recording these results and confirming no task-owned browser or build process remained, `/tmp/dao-m154-startup-fix-idltvijt` was removed, including backups, raw logs, probe scripts, and disposable profile data. The original upgrade's two temporary roots also remain absent.

## Remaining Limits

- The original VideoToolbox `-12911` error was not reproduced. No video decoder behavior was changed, and the original page and visible symptom remain unknown.
- Creating two WebGLOnWebGPU contexts in one renderer triggers Dawn's `forClient == mClient` assertion. Source inspection traces this to the existing ANGLE display cache key, which omits the per-context Dawn instance: the second context reuses the first device with a different wire client. Both initialization functions are unchanged by this patch, and both contexts previously shared Blink's static ANGLE instance. This supports a pre-existing experimental-path defect, but no pre-patch binary A/B test was run. The feature is disabled by default; this separate defect remains unfixed.
- Validation covers the local macOS arm64 component debug build. Other platforms, release/non-component builds, protected attribute reads, and read-only volumes were not exercised.
- This change repairs development-build linkage and startup diagnostics without adding or materially changing a browser feature. The feature inventory, shared feature checklist, and user changelog need no additional entries for this follow-up.
