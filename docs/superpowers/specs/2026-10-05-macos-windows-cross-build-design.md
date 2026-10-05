# Windows cross compilation on macOS

## Goal

Build the Windows x64 browser, WebView2 installer host, and unsigned NSIS
installer from source on the Mac packaging machine. Preserve the default macOS
arm64 release and append Windows assets to its existing desktop version/tag.

## Design

Separate the build target from the host. `--platform mac|windows` overrides
`DAO_BUILD_PLATFORM`; absent both, retain native host defaults. A Mac targeting
Windows uses `out/dao-win-x64` or `out/dao-win-x64-debug`, preserving native
`out/dao` and `out/dao-debug` caches. Source import remains shared and builds
must run sequentially in a checkout.

Use Chromium's official hermetic Windows toolchain archive and its pinned
Clang, lld-link, and resource compiler. Store only machine-local archive
configuration in ignored `.dao/`. Support configuring an existing archive and
exporting one from a compatible Windows SDK/Visual Studio installation. SDK
provisioning is separate from compiling application binaries. No Wine or remote
Windows builder is involved.

The download command retains existing gclient configuration, adds the Windows
checkout target, syncs without hooks, then runs hooks with the selected SDK
environment. Both browser and standalone installer compilation use that
environment. NSIS runs natively on macOS and bundles the cross-compiled files.

## Acceptance

- Explicit and inherited targets agree across import, build, package, release.
- Invalid hosts, SDK configuration, and missing tools fail with actionable errors.
- Existing Windows native builds and macOS native builds keep their output paths.
- Windows releases on Mac reuse the desktop version/tag and unsigned EXE format.
- Focused tests cover command construction, cache isolation, SDK setup, and
  packaging on both hosts. This Windows workspace cannot prove a real Mac build;
  the development guide must state the exact remaining Mac verification steps.

## Constraints

No direct edits under `engine/`, no worktrees or new branches, no git mutations.
Compilation verification uses only `npm run rebuild`. Keep unrelated changes.
Do not download or install a new Visual Studio automatically, sign artifacts,
publish releases, or change existing release assets during implementation.
