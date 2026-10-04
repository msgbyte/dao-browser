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
release metadata unchanged. Windows arm64 targets and cross-platform builds are
not supported by this development entry point. On macOS, the configured arm64
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
`start:debug:clean`, packaging, signing, and release publishing remain macOS
workflows. The Windows port does not yet provide MCP transport, macOS sharing,
AppKit tab tear-off detection, ImageIO thumbnails, or Sparkle updates.

### Windows installation identity

Windows uses Chromium's native `mini_installer`, including its installer,
uninstaller, shortcuts, default-browser registration, and upgrade logic. A
per-user install lives in `%LOCALAPPDATA%\Dao\Application`; an explicit
`--system-level` install uses `%ProgramFiles%\Dao\Application` and requires
elevation. The default profile is `%LOCALAPPDATA%\Dao\User Data`, including
when launching the installed executable without CLI flags. Browser/HTML/PDF
registration uses Dao identities. Existing Chromium profiles are not migrated.

Dao's COM GUID substitutions move the MIDL input into `gen/`. The MIDL patch
aligns only the original IDL path in copied compiler-settings comments; it still
validates generated interface code and binary type libraries against Chromium's
baselines. After importing, run this lightweight regression check without
invoking MIDL or compiling Chromium:

```powershell
python3 -m unittest scripts.tests.test_windows_midl
```

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
