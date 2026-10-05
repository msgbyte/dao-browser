# macOS Windows Cross Build Implementation Plan

> **For agentic workers:** Use subagent-driven-development for independent
> toolchain, installer, and release tasks. The primary agent owns integration.

**Goal:** Compile and package Windows x64 entirely on the Mac packaging host.

**Architecture:** Reuse Chromium's hermetic SDK and cross compilation support.
Separate requested platform from host, isolate output directories, and route the
existing release pipeline through the selected target.

**Tech Stack:** TypeScript, Commander, Vitest, Chromium Clang/lld/rc, NSIS.

**Spec:** `docs/superpowers/specs/2026-10-05-macos-windows-cross-build-design.md`

## Global Constraints

- No direct edits under `engine/`; import/download own generated files.
- Compilation verification uses only `npm run rebuild`.
- Keep native caches and shared desktop version/tag behavior.
- No git mutations, signing, real release execution, or unrelated edits.

## Review Focus

- An existing Mac checkout must fetch Windows dependencies without losing its configuration.
- A Windows environment override must not change an explicitly selected Mac release.
- Missing SDK metadata must fail before GN writes to a native build cache.
- Paths with spaces and non-ASCII characters must remain literal arguments.
- Mac-produced installers require Windows runtime validation before publication.

## Task 1: Target selection and cache isolation

**Files:** `scripts/utils.ts`, `scripts/commands/{build,download,import,package,start}.ts`,
`scripts/commands/__tests__/windows_development.test.ts`, focused cross-build tests.

**Interfaces:** Extend `resolveBuildTarget(config, host, arch, requestedPlatform)`;
export `getBuildOutputName(target, debug, host)`.

- [x] Add tests for explicit Windows on Mac, invalid targets, inherited defaults,
  and `dao-win-x64[-debug]` cache selection.
- [x] Run the focused tests, implement target routing, then rerun them.
- [x] Add idempotent gclient Windows target configuration and hook sequencing.
- [x] Check that `start` rejects execution of a Windows target on macOS.

## Task 2: Hermetic Windows SDK

**Files:** `scripts/windows-toolchain.ts`, `scripts/commands/windows-toolchain.ts`,
focused toolchain tests; registration in `scripts/cli.ts`.

**Interfaces:** `getWindowsToolchainEnvironment()` supplies gclient environment;
`prepareWindowsCrossToolchain(signal?)` returns tool paths, SDK include/library
directories, and environment for GN and standalone installer builds.

- [x] Test configuration validation, pinned hash mapping, and SDK metadata parsing.
- [x] Reuse official Chromium update/export scripts without invoking compilers.
- [x] Add configure/setup/export commands with actionable missing-tool errors.
- [x] Run focused tests and inspect platform-specific path resolution.

## Task 3: Installer cross compilation

**Files:** `scripts/commands/{build-windows-installer,package-windows}.ts` and tests.

- [x] Test Mac clang-cl, lld-link, and rc.py command arguments using fake tools.
- [x] Reuse the SDK helper, add portable NuGet extraction and NSIS arguments.
- [x] Preserve source/output freshness checks and atomic artifact replacement.
- [x] Run existing host/frontend/NSIS tests alongside Mac command tests.

## Task 4: Release routing and documentation

**Files:** `scripts/commands/release.ts`, release tests, `docs/development.md`,
`docs/features.md`, `docs/feature-checklist.md`.

- [x] Forward `--platform` through every release build/package/import phase.
- [x] Test Windows release on Mac and reject unsupported host architectures.
- [x] Document SDK provisioning, Mac dependencies, rebuild/package/release commands,
  cache locations, and actual verification limits.
- [x] Run focused regression tests, TypeScript checks, documentation checks, and
  diff checks. Review the combined change before reporting completion.

## External validation still required

- [ ] Run the documented source build on the Mac packaging machine with a compatible SDK archive.
- [ ] Validate the cross-produced installer in a disposable Windows account or VM.

The gclient integration was additionally checked against the actual depot_tools
restricted parser after review caught an unsupported dynamic configuration.
