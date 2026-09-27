# Windows Desktop Development Design

Status: approved by the user on September 25, 2026.

## Goal and scope

Enable native Windows x64 development of Dao's Chromium desktop browser in the
existing primary checkout. Reuse `npm run setup`, `npm run rebuild`, and
`npm run start:debug`. Preserve the existing macOS arm64 development workflow.

The first milestone is a locally built browser that launches, displays Dao's
sidebar, and can open and navigate tabs. Windows installers, signing, release
publishing, and a Windows automatic-update backend are outside this milestone.
Platform-specific functionality must be documented accurately; unsupported
actions must not appear to succeed.

## Verified local prerequisites

Verified after the user's reboot on September 25, 2026:

- Checkout: `D:\develop\dao-browser`, on `main`.
- Node.js 22.23.3, managed by the existing nvm installation.
- depot_tools: `D:\software\depot_tools`; `gclient help` succeeds.
- depot_tools Python: 3.11.8.
- Visual Studio 2022: `D:\Microsoft Visual Studio\2022\Community`.
- MSVC 14.44.35207, including MFC/ATL headers and x64 libraries.
- Windows SDK and debugger files: 10.0.26100.7705.
- `DEPOT_TOOLS_WIN_TOOLCHAIN=0` and `vs2022_install` are configured.
- Command-line proxy settings reuse the machine's existing local proxy.
- Visual Studio reports no pending reboot.

Chromium has not been downloaded or compiled. Tool availability does not yet
establish that Dao's native code builds or runs on Windows.

## Current barriers

- `dao.json` specifies macOS arm64, and `configs/common.gn` hardcodes arm64.
- `scripts/utils.ts` finds executables using Unix `which`.
- npm launch scripts invoke `open` or an executable inside a macOS `.app`.
- `scripts/commands/build.ts` applies a macOS-oriented linker override to every
  development build.
- The importer handles Sparkle with `ditto` and Unix shell commands.
- Shared native source lists in `dao_ui_sources.gni` include Objective-C++
  implementations for sharing, native utilities, and file icons.
- The download command changes a global Git setting as a side effect.

## Approach

Adapt the existing CLI and Chromium integration. A separate Windows build system
would duplicate patch import and build-cache behavior. A remote macOS build would
not meet the requested native Windows development goal.

### Target and build configuration

Resolve the supported local target once for build and launch commands: Windows
x64 on a Windows x64 host, with the existing macOS configuration retained on
macOS. Keep the configured Mac target independent of Node's process architecture
so Rosetta invocations retain their existing behavior. Reject unsupported
host/target combinations with a specific diagnostic.
Do not rewrite `dao.json` just to switch development machines.

Move architecture-specific GN settings out of the common configuration and add
the Windows platform configuration. Keep macOS branding, bundle versioning,
linker settings, and post-build fixes inside macOS conditions. Retain the shared
`dao_display_version` argument used by non-bundle targets. Preserve existing
output locations under `engine/src/out/dao` and `engine/src/out/dao-debug`.

### Commands and import

Use platform-aware executable lookup and child-process invocation, including
Windows batch wrappers and paths containing spaces. Prefer existing Node APIs
and helpers; add no runtime dependency for these tasks.

Keep download and import behind the existing npm commands. Remove the download
command's global Git mutation; apply any demonstrated repository requirement
at the narrowest appropriate scope. Keep patch paths in their portable format
and filesystem paths native. Preserve patch target validation, repeatable imports,
and the existing repair behavior. Run Sparkle framework copying only on macOS.

Add a small CLI launch command shared by the npm launch scripts. On Windows,
launch the Chromium executable produced in the chosen output directory with
appropriate logging flags. Keep the existing macOS launch behavior. Windows
development launches must use a Dao-specific profile location, separate from
an installed Chrome profile.

### Native build compatibility

Audit the shared GN source lists, dependencies, and callers of macOS helpers.
Build Objective-C++ sources only on macOS. Use existing Chromium cross-platform
APIs for Windows behavior where available. Keep Windows-specific implementations
small and in Dao-owned tracked sources.

Resolve compilation and link failures through `src/dao/` and `src/patches/`,
then synchronize with the standard importer. Do not hide core browser behavior
behind successful no-op implementations. Record any platform-specific feature
limitations in the feature inventory and regression checklist.

The precise set of additional native fixes must be determined from a real
Windows `npm run rebuild`; source inspection alone cannot establish that set.

## Verification and acceptance

1. Run focused script tests for target selection, executable lookup, process
   arguments, Windows launch paths, and affected patch-import behavior.
2. Run TypeScript checking and the documentation checker for changed surfaces.
3. Initialize the pinned Chromium version through `npm run setup` after the
   Windows initialization path is ready. Use plain imports without force.
4. Confirm compilation exclusively through `npm run rebuild`. Do not run direct
   Chromium build tools, direct GN generation, or an alternate compile command.
5. Launch through `npm run start:debug` and verify a visible Dao sidebar,
   address navigation, tab creation/closing, and ordinary window controls.
6. Update `docs/development.md`, `docs/features.md`, and
   `docs/feature-checklist.md` to describe the implemented Windows behavior and
   any remaining gaps. Claim only the checks that actually ran successfully.

macOS execution cannot be verified on this Windows machine. Preserve its behavior
through focused script coverage and report the need for macOS runtime regression
verification separately.

## Repository constraints

Work in the primary checkout on `main`; do not create branches or worktrees.
Do not commit or run state-changing Git operations without the authorization
required by `AGENTS.md`. Never edit `engine/` directly. Keep translations in the
existing localization systems and do not invoke the paid translation script.
