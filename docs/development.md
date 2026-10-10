# Development Guide

This document describes how to build Dao Browser from source.

## Prerequisites

- macOS arm64 with Xcode and Command Line Tools, or Windows x64 with the
  Visual Studio C++ toolchain described below
- [depot_tools](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up) installed and in `PATH`
- Node.js >= 22.19.0
- At least 200 GB free disk space for Chromium source + a component build;
  additional build configurations require more space

### Windows x64 development

Use a native Windows checkout on a local disk. Node.js is managed by
[nvm-windows](https://github.com/coreybutler/nvm-windows); this environment uses
Node.js 22.23.3. Use `npm.cmd` in PowerShell if its execution policy blocks
`npm.ps1`. The CLI selects `win/x64` on Windows and keeps `dao.json`'s macOS
release metadata unchanged. Windows arm64 targets are not supported. macOS can
cross-compile Windows x64 as described below. On macOS, the configured arm64
target is retained even when Node runs as x64 under Rosetta.

The prepared machine retains these installation paths:

| Component | Path / version |
| --- | --- |
| nvm-windows | `D:\software\nvm` |
| Active Node.js link | `D:\software\nodejs` |
| depot_tools | `D:\software\depot_tools` |
| Visual Studio 2022 Community | `D:\Microsoft Visual Studio\2022\Community`, 17.14 |
| MSVC | v143, 14.44; x64/x86 C++ tools and ATL/MFC |
| Windows SDK and Debugging Tools | `C:\Program Files (x86)\Windows Kits\10`, 10.0.26100.7705 |

Set `DEPOT_TOOLS_WIN_TOOLCHAIN=0`, set `vs2022_install` to the Visual Studio
installation, and place depot_tools on `PATH`. Restart the terminal after
changing environment variables. If Google downloads require a proxy, set
`HTTP_PROXY` and `HTTPS_PROXY` to the local proxy that is actually running;
proxy addresses are machine-specific and are not part of the project config.
Check the [pinned Chromium Windows instructions](https://chromium.googlesource.com/chromium/src/+/refs/tags/149.0.7827.201/docs/windows_build_instructions.md)
and `build/vs_toolchain.py` when upgrading the Chromium pin.

```powershell
nvm use 22.23.3
node --version
npm.cmd ci
npm.cmd run setup
npm.cmd run rebuild
npm.cmd run start:debug
```

`setup` resumes an existing checkout with `gclient sync`; keep partial downloads
when retrying. Source and build output remain in `engine/`. Compilation
confirmation must use `npm run rebuild`, which imports tracked changes before
building `engine/src/out/dao-debug/chrome.exe`.

If LLVM reports `out of memory`, reduce parallelism while retaining the same
build configuration and incremental cache:

```powershell
npm.cmd run rebuild -- -- -j 6
```

Both `--` separators are intentional: the second forwards `-j 6` through the
nested `build:debug` npm script. Reduce the job count further when other
memory-heavy applications are running.

If Siso itself consumes excessive memory while loading the build graph, set a
[Go runtime soft memory limit](https://pkg.go.dev/runtime#hdr-Environment_Variables)
in the current PowerShell session before resuming:

```powershell
$env:GOMEMLIMIT = "4GiB"
npm.cmd run rebuild -- -- -j 2
```

This setting encourages Go to reclaim memory earlier. It is not a hard process
limit and does not limit compiler memory, so retain a suitable job count and
monitor available memory. It does not change GN arguments or invalidate the
build cache. The environment setting lasts for the current shell and its child
processes; remove it with `Remove-Item Env:GOMEMLIMIT` when no longer needed.

On Windows, also check **Committed** memory in Task Manager. Allocations can
fail even with available physical RAM when the system commit limit is reached.
That limit depends on RAM and paging files; automatic page-file growth requires
free space on the page-file drive. If other memory-heavy applications must stay
open, lower build concurrency and check the paging configuration before
retrying. Paging changes require administrator rights and may take effect only
after a restart. See Microsoft's [page-file guidance](https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/introduction-to-the-page-file).

The launch command uses `%LOCALAPPDATA%\Dao Debug\User Data` for development
and `%LOCALAPPDATA%\Dao\User Data` for release, separate from installed Chrome.
`start:debug:view` also enables Chromium UI devtools. `start:little`,
`start:debug:clean` and Apple signing remain macOS workflows. Windows packaging
and publishing use the unsigned Chromium installer described below.
Windows build and release commands handle Ctrl+C by stopping the owned process
tree through `taskkill /PID ... /T /F`, then waiting for cleanup. Repeated Ctrl+C
does not bypass cleanup. Other terminals' build jobs remain independent.
If cleanup fails or times out, the command reports uncertain termination.
The build command exits with status 130 for SIGINT or 143 for SIGTERM after
successful cancellation; cleanup failures exit with status 1.
The Windows port does not yet provide MCP transport, macOS sharing,
AppKit tab tear-off detection, ImageIO thumbnails, or Sparkle updates.

### Cross-compiling the complete Windows installer on macOS

The CLI can build the Windows x64 browser, native WebView2 host, and unsigned
NSIS installer on an Apple Silicon or Intel Mac. It uses Chromium's bundled
Clang/lld/resource compiler and its hermetic MSVC/Windows SDK archive. Compilation
and packaging run on the Mac; no Windows VM, Wine, or remote builder is used.
See the [upstream cross-compilation instructions](https://chromium.googlesource.com/chromium/src/+/refs/tags/149.0.7827.201/docs/win_cross.md).

**Prepare the SDK on the Mac.** With the Chromium checkout already present,
install the native extraction/packaging tools and run (for a fresh checkout,
run `npm run download` first):

```bash
brew install msitools sevenzip makensis
npx tsx scripts/cli.ts windows-toolchain setup --accept-license
npm run download -- --platform windows
```

The first setup accepts the Microsoft Visual Studio Build Tools and Windows SDK
licenses, downloads their public packages, and creates a local Chromium-compatible
SDK archive. A Windows machine, Wine, and a VM are not required. The downloader
from [msvc-wine](https://github.com/mstorsjo/msvc-wine) is pinned by commit and
SHA-256; only its package download/extraction code runs. Microsoft's Visual Studio
2026 (18.10.3) manifest is also pinned by SHA-256; the manifest and verified payloads
are cached under `.dao/windows-sdk/`.
The standalone SDK 10.0.26100.7705 bundle supplies the debugging tools omitted by
Visual Studio's SDK component. Setup verifies its SHA-256 and its payload hashes,
then packages the x86/x64/ARM64 headers, libraries, ATL, DIA, runtimes, and environment
files using Chromium's content-hash format. Chromium's own installer verifies and
installs that archive. SDK setup does not require the cross resource compiler;
the following download command fetches those Chromium host tools. The output
target remains Windows x64; Chromium also loads the ARM64 toolchain environment
when generating its build graph on Apple Silicon.

Existing archive configurations remain supported and are reused without public
downloads. The Google-hosted SDK archive is not public. To use a compatible private
archive instead, optionally export one from a Windows installation using:

```powershell
npx.cmd tsx scripts/cli.ts windows-toolchain export --output C:\dao-sdk-export
```

The exporter delegates to depot_tools' `package_from_installed.py` and requires
an empty destination. For Chromium 149.0.7827.201, its hermetic toolchain expects
Visual Studio 2026 and Windows SDK 10.0.26100.0. Install the C++ desktop workload,
ATL/MFC, ARM64 tools and SDK Debugging Tools required by that upstream packager.
The existing native VS2022 development environment is not a compatible archive
for this hermetic configuration. No application binaries are compiled during
SDK export. Copy the resulting `<hash>.zip` to a private directory on the Mac;
the hash is the ten-character archive filename without `.zip`.

Use a case-insensitive macOS volume for the checkout, depot_tools and its SDK cache. Keep the
normal macOS build prerequisites and allow extra space for a separate Windows
build. Install the native [NSIS compiler](https://formulae.brew.sh/formula/makensis),
configure the archive, and sync the Windows dependencies:

```bash
brew install makensis
npx tsx scripts/cli.ts windows-toolchain configure \
  --base-url "$HOME/dao-sdk" --hash <hash>
npm run download -- --platform windows
npx tsx scripts/cli.ts windows-toolchain setup
```

`--base-url` also accepts a private HTTP(S) directory URL containing the archive.
Configuration is stored in ignored `.dao/windows-toolchain.json`; SDK contents
are managed by Chromium/depot_tools. Standard
`DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL` and `GYP_MSVS_HASH_<upstream-hash>` overrides
are also supported. The CLI selects `DEPOT_TOOLS_WIN_TOOLCHAIN=1` for cross
builds even if the native Windows environment used `0`. Upgrading Chromium may
require exporting a new compatible SDK archive.

Download preserves existing `.gclient` settings and imported Dao patches when
the shallow checkout already matches the locally available requested version tag,
adds the Windows target, then
runs hooks with the configured SDK. This fetches Windows runtime libraries,
resource tools and the native archive tools required by `mini_installer`.
Do not skip this sync just because a Mac build already exists.

For local compile verification and packaging:

```bash
DAO_BUILD_PLATFORM=windows npm run rebuild -- -- --release --target mini_installer -j 4
DAO_BUILD_PLATFORM=windows npm run rebuild -- -- --release --target dao_installer_ui
npm run package -- --platform windows
```

Both `--` separators forward arguments through the nested npm script. The
environment variable also selects the target during source import; individual
CLI commands accept `--platform mac|windows`, which takes precedence. The cross
release cache is `engine/src/out/dao-win-x64`; debug uses `dao-win-x64-debug`.
Native `dao` and `dao-debug` caches retain their existing names. Run Mac and
Windows builds sequentially because the imported Chromium sources are shared.
Unset `DAO_BUILD_PLATFORM` to return to native Mac development; `start` cannot
execute Windows binaries on macOS.

The output is `dist/dao-browser-<version>-windows-x64.exe` plus its SHA-256 file.
After the normal Mac release has created the shared desktop tag, use:

```bash
npm run release:windows -- --dry-run
npm run release:windows
```

The second command builds and publishes Windows assets under the existing tag.
Use `--skip-upload` for build/package only, or `--skip-build` to publish a
previously verified EXE/checksum. The Windows release does not bump the desktop
version and keeps macOS assets. Signing remains disabled. Script tests cover
cross-platform routing and tool invocation; a real Mac compilation and a
Windows install/launch/upgrade/uninstall test are still required before treating
the cross-produced artifact as release-verified.

### Windows installer and shared desktop releases

Windows uses Chromium's native `mini_installer`, including its installer,
uninstaller, shortcuts, default-browser registration, and upgrade logic.
The distributed EXE uses NSIS to package a Win32/WebView2 host and the native
backend. `scripts/windows-installer/native/host.cc` loads the fully inlined local
HTML from `scripts/windows-installer/web/`; its English and Simplified Chinese
strings live in `web/locales/`. The frontend sends fixed commands for folder
selection (`browse:<current path>`), installation, minimizing, closing and launch.
The host invokes the same outer
EXE with `/S /D=<root>`, reusing NSIS validation and native installation logic.
Session configuration uses a UTF-16 INI file to preserve Unicode paths.

The compact UI follows the supplied `dao-installer.html` design: a fixed 560 x 420
DIP window, custom caption buttons, centered 96px transparent logo, halo, ring,
inline path editing, and system light/dark themes. The reference logo lives in
`web/assets/dao-logo.png`; the EXE/taskbar icon still uses the Windows branding ICO.
WebView2's [non-client region support](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2settings9)
handles the title-bar drag region; runtimes without that interface use NSIS.
The folder picker opens at the current input's nearest existing parent and appends
`Dao` unless the selected folder already has that name. Existing installations
keep their registered root. The completion button launches Dao; closing exits
without launching. Version metadata is real; the prototype's sample size and
simulated percentage are not shown. The backend has no cancellation/progress API,
so installation uses an indeterminate ring and cannot be interrupted from the UI.
The per-user wrapper does not elevate or support installation in Program Files.

Missing/unusable WebView2 falls back to the original native NSIS pages without
downloading a runtime. `/NATIVE` forces this path; `/S` skips both UIs.
`scripts/windows-installer/theme.nsh` and `locales/` own the native fallback's
appearance and strings. Once the backend starts, the host waits for completion
and never opens a second installer on a renderer failure. Diagnostic host events
are appended to `%TEMP%\dao_installer_ui.log`; native install details remain in
`%TEMP%\dao_installer.log`.
The default per-user install is `%LOCALAPPDATA%\Dao\Application`; choosing
`D:\Apps\Dao` installs binaries into `D:\Apps\Dao\Application`. The wizard
supports writable local fixed drives, with a product-root limit of 180 characters.
It rejects Windows/Program Files, network and profile paths, and occupied
`Application` or `Temp` subdirectories. Do not store unrelated files in these
two installer-owned subdirectories. Upgrades/repairs lock the directory to the
registered installation. To move an existing installation, uninstall it while
keeping browsing data and rerun the wizard. Existing all-users installations
must be uninstalled before using this per-user wizard.

The internal native `mini_installer.exe --system-level` remains available for
administrative installs under `%ProgramFiles%\Dao\Application` with elevation;
the distribution wizard does not forward arbitrary native installer switches.
The default profile is `%LOCALAPPDATA%\Dao\User Data`, including
when launching the installed executable without CLI flags. Browser/HTML/PDF
registration uses Dao identities. Existing Chromium profiles are not migrated.

Packaging requires [NSIS 3](https://nsis.sourceforge.io/Download) on the build host.
For a Mac host, use `brew install makensis` as described above. On Windows,
Install it normally, put `makensis.exe` on PATH, or set `DAO_NSIS_DIR` to an
extracted portable distribution. A portable NSIS 3.13 extracted into
`.dao/tools/nsis-3.13/` is also detected. The compiler is only a packaging tool;
users do not need NSIS installed. Packaging rejects old `setup.exe` builds
without the `--dao-install-dir` capability and payloads older than `setup.exe`.
Compiler failures preserve the previous distribution EXE and checksum.

The WebView2 host requires Visual Studio C++ x64 tools and the Windows 10/11 SDK.
Its standalone target downloads a checksum-pinned Microsoft WebView2 SDK into
`.dao/tools/` and writes the host plus SDK notices into `.dao/installer/`:

```powershell
npm.cmd run rebuild -- -- --release --target dao_installer_ui
```

This target compiles only the small installer host; it does not run GN or touch
the Chromium build cache. Packaging requires a matching source/output fingerprint
and fails with that rebuild command if the host is missing or stale. HTML/CSS/JS
changes require only repackaging; native source and locale changes require the
host rebuild. SDK runtime binaries are not bundled.

Run the wizard checks without installing Dao or modifying its registration:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/tests/test_windows_installer.ps1
npx.cmd tsx scripts/tests/test-webview-installer.ts
```

This compiles the actual wizard with a harmless backend and a temporary registry
namespace. It checks editable fresh-install and locked repair directory pages,
Unicode/space-containing paths, failures, and preservation of unrelated files.
It also edits the directory in the actual first-page control and exercises the
progress/finish flow with the isolated backend. Opening the wizard must not
write to the selected installation directory. Use `-Language 1033` or
`-Language 2052` to verify English or Simplified Chinese, and optionally
`-CaptureDir .dao/installer-preview` to save screenshots of the hidden test
windows for visual inspection. It never presses the finish-page launch action.
The default NSIS language follows Windows; new locales belong in separate NSIS
language resource files. The WebView2 check runs the compiled host with a harmless
backend and a process-local debugging endpoint. It checks absent-runtime fallback,
real bridge messages, localized layout, inline path editing, retry, repair locking
and duplicate-submit protection. Add `--capture` to open the isolated test windows
visibly and save light/dark, path-edit, progress, error/details and completion
WebView2 screenshots into `.dao/installer-preview/` (hidden windows do not produce
compositor frames for screenshot capture).
These checks do not replace a real native installation and uninstall test in a
disposable account or VM.

Dao's COM GUID substitutions move the MIDL input into `gen/`. The MIDL patch
aligns only the original IDL path in copied compiler-settings comments; it still
validates generated interface code and binary type libraries against Chromium's
baselines. After importing, run this lightweight regression check without
invoking MIDL or compiling Chromium:

```powershell
python3 -m unittest scripts.tests.test_windows_midl
```

For incremental compile verification and an installer for local testing:

```powershell
npm.cmd run rebuild -- -- --target mini_installer -j 2
npm.cmd run rebuild -- -- --target dao_installer_ui
npm.cmd run package -- --debug
```

This produces `dist/dao-browser-<version>-windows-x64-debug.exe` and a matching
`.sha256` file. The debug installer uses the same Dao installation identity;
test it in a disposable Windows user or VM. `start:debug` still explicitly
selects the separate `Dao Debug` profile. Debug packages cannot be published
by the release command, which expects the filename without `-debug`.

`build:release` builds the browser target in `out/dao`; it does not create the
installer, bump the version, tag, or upload. If that build is already complete,
produce a local release installer:

```powershell
npm.cmd run release:windows -- --skip-upload
```

This imports normally, builds `mini_installer` incrementally in the same release
output directory and the separate `dao_installer_ui` host, then packages both.
It performs no Git or publication changes. Once `out/dao/mini_installer.exe`
is current, `npm.cmd run package` compiles the wizard into `dist/` and hashes
the resulting EXE. To verify native installer changes against an existing
release cache without switching build flavors:

```powershell
npm.cmd run rebuild -- -- --release --target mini_installer -j 2
npm.cmd run rebuild -- -- --release --target dao_installer_ui
npm.cmd run package
```

`--release` overrides the debug flag supplied by the `rebuild` npm script;
without it, `rebuild` still targets `out/dao-debug`. Native directory regression
tests are in `installer_util_unittests` with filter
`*GetChromeInstallPathWithPrefsTest.*` and can be built with the same rebuild
command by selecting that target. Test install/uninstall behavior in a disposable
Windows account or VM, not against a development machine's active Dao profile.

Desktop releases share one `dao.json.version.display` and one GitHub Release
named `v<version>`. Publish macOS first with the existing `npm run release`
workflow, then commit/push its metadata and tag as directed. On Windows, sync
that release's sources and tags before running:

```powershell
# Preview without building, uploading, changing versions, or creating tags.
npm.cmd run release:windows -- --dry-run
# Build an unsigned release installer and append it to the desktop release.
npm.cmd run release:windows
# Build/package locally without publishing or changing website metadata.
npm.cmd run release:windows -- --skip-upload
```

`release:windows` is `cli release --platform windows`; plain `cli release`
defaults to `--platform mac`. Windows reuses the current version without a
bump or new tag, imports normally without `--force`, builds `mini_installer`
in `out/dao` on Windows or `out/dao-win-x64` on Mac, and packages
`dao-browser-<version>-windows-x64.exe`. It requires native Windows x64 tools or
the configured Mac cross toolchain, and authenticated GitHub CLI (`gh auth login`),
but no signing credentials, R2 credentials, or Sparkle tools. Unsigned packages
can display Windows security prompts. Automatic Windows updates are not included.

The GitHub uploader preserves existing macOS assets and rejects a different
Windows installer for an already published version. A retry can complete a
missing checksum upload. `--skip-build` reuses the existing release EXE and
checksum in `dist/`. The tag's sources must match HEAD; release-only changes to
the display version, appcast, and website metadata are permitted. Code changes
require a new desktop release. Before building and again before uploading, the
publisher checks for uncommitted source changes and verifies the tag against
`origin`; staged and untracked sources also prevent publication.
The internal Chromium engine version and native
versioned installation directories remain Chromium-versioned.

Successful Windows publication adds only `platforms.win` to
`website/public/info.json`; commit/deploy this metadata when ready. Subsequent
macOS releases preserve that Windows URL until the corresponding Windows
installer is published. Neither platform's release command commits or pushes
Git changes automatically. Android retains its independent release command.
An existing draft desktop release must be published before Windows assets are
attached. Windows metadata records its own version so download text remains
accurate when macOS advances first. Standalone packaging and `--skip-build`
trust the selected build/artifact; filenames and checksums do not prove which
source commit produced a binary. Use the normal release build for fresh releases.

### Setting up depot_tools

[depot_tools](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up) is a collection of tools built by the Chromium team for managing the Chromium source code. It provides `gclient` (dependency management), `gn` (build file generation), `autoninja` (parallel build), and other utilities required to fetch and build Chromium-based projects.

```bash
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git

# Add to your shell profile (~/.zshrc or ~/.bash_profile)
export PATH="$PATH:/path/to/depot_tools"
```

After adding to PATH, restart your terminal and verify:

```bash
gclient --version
```

> **Note:** On first run, depot_tools will automatically bootstrap the required Python environment. Ensure you have a working internet connection.

## Quick Start

```bash
npm install
npm run setup     # download chromium + apply patches
npm run rebuild   # re-import tracked sources and build Dao Browser
```

## Commands

| Command | Description |
|---------|-------------|
| `npm run download` | Fetch Chromium source at the version specified in `dao.json` |
| `npm run import` | Apply patches and copy Dao code into the Chromium tree |
| `npm run export -- <file>` | Export one explicitly scoped Chromium file into its patch |
| `npm run build` | Build Dao Browser (gn gen + autoninja) |
| `npm run package` | Package into a `.dmg` for distribution |
| `npm run package:zip` | Package into a `.zip` for distribution |
| `npm run setup` | download + import (first-time setup) |
| `npm run rebuild` | import + build (iterative development) |
| `npm run docs:check` | Validate maintained documentation commands, links, and Dao URLs |
| `npm run engine:cache:refresh` | Refresh the local `.dao/engine` warm cache for Git worktree workers |
| `npm run setup:worktree` | Initialize an externally-created Git worktree for agent work |
| `npm run archive:worktree` | Archive current worktree engine or dry-run stale primary copies |
| `npm run worktree:create -- <name>` | Create a Git worktree with a private copy-on-write cloned `engine/` |

## Development Workflow

1. Run `npm run setup` to fetch Chromium and apply all patches.
2. Make Dao-owned changes in `src/dao/` and Chromium integration changes in
   the matching file under `src/patches/`.
3. Run `npm run import` to synchronize the tracked sources into `engine/src`.
4. Run `npm run rebuild` after the related edits are complete.

`src/dao/` and `src/patches/` are the source of truth. Do not edit `engine/`
directly during routine development. When an explicitly scoped Chromium patch
debugging task requires engine-first iteration, export only the intentional
file with `npm run export -- <file>`; never run bare `npm run export`.

## Project Structure

```
dao-browser/
├── dao.json          # Core config (Chromium version, branding)
├── scripts/          # TypeScript build toolchain (CLI)
├── src/
│   ├── patches/      # Patch files against Chromium (mirrors Chromium dir structure)
│   └── dao/          # Dao's own code (copied into engine/ on import)
├── configs/          # GN build arguments
└── branding/         # Brand assets (icons, logos)
```

## Packaging

After building, create a distributable package:

```bash
npm run package       # creates dist/dao-browser-<version>-mac-arm64.dmg
npm run package:zip   # creates dist/dao-browser-<version>-mac-arm64.zip
```

Options:
- `--zip` — produce a `.zip` instead of `.dmg`
- `--sign` — apply ad-hoc code signature (off by default)

## Architecture

Chromium source lives in `engine/` (gitignored). Only patch files and Dao's own
code are version-controlled. This follows the Zen Browser approach of maintaining
a clean separation between upstream and custom code.

For a full inventory of features Dao Browser adds on top of Chromium, see [features.md](./features.md).

## Installation Notes (Unsigned Builds)

Dao Browser is currently distributed without Apple notarization. macOS Gatekeeper will block the first launch. Use one of the following methods to open it:

**Method 1 — Right-click to open (simplest)**

1. Right-click (or Control-click) on `Dao Browser.app`
2. Select **Open** from the context menu
3. In the dialog that appears, click **Open**

You only need to do this once.

**Method 2 — System Settings**

1. Double-click the app (it will be blocked)
2. Open **System Settings → Privacy & Security**
3. Scroll down and click **Open Anyway** next to the Dao Browser message

**Method 3 — Terminal (recommended)**

```bash
xattr -cr /Applications/Dao\ Browser.app
```

This removes the quarantine attribute entirely. Run it once after each update.

> **Note:** On macOS Sequoia (15+), Method 1 may not work. Use Method 2 or 3 instead.
