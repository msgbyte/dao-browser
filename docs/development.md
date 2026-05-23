# Development Guide

This document describes how to build Dao Browser from source.

## Prerequisites

- macOS (initial target platform)
- Xcode and Command Line Tools
- [depot_tools](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up) installed and in `PATH`
- Node.js >= 18
- ~100 GB free disk space for Chromium source + build

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
npm run build     # build Dao Browser
```

## Commands

| Command | Description |
|---------|-------------|
| `npm run download` | Fetch Chromium source at the version specified in `dao.json` |
| `npm run import` | Apply patches and copy Dao code into the Chromium tree |
| `npm run export` | Generate patch files from modifications in the Chromium tree |
| `npm run build` | Build Dao Browser (gn gen + autoninja) |
| `npm run package` | Package into a `.dmg` for distribution |
| `npm run package:zip` | Package into a `.zip` for distribution |
| `npm run setup` | download + import (first-time setup) |
| `npm run rebuild` | import + build (iterative development) |

## Development Workflow

1. Run `npm run setup` to fetch Chromium and apply all patches
2. Make changes directly in `engine/` for rapid iteration
3. Run `npm run export -- <filepath>` to capture changes as patch files
4. Run `npm run rebuild` to verify patches apply cleanly and build succeeds

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
