# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Dao Browser is a Chromium-based browser with a vertical tab sidebar (inspired by Arc), currently targeting macOS arm64. It builds on top of Chromium source using a patch-based architecture — only patch files and Dao's own C++ code are version-controlled; the full Chromium tree lives in `engine/` (gitignored).

## Build Commands

```bash
npm run setup          # First-time: download Chromium + apply patches
npm run rebuild        # Iterative dev: import patches + build
npm run build          # Build only (gn gen + autoninja)
npm run build:debug    # Debug build (is_debug=true, component build)
npm run import         # Apply patches + copy src/dao/ into engine/
npm run export         # Export unstaged engine/src changes as patch files
npm run export -- <file>  # Export patch for a specific file
npm run start          # Launch the built browser
npm run start:debug    # Launch with stderr logging
```

All scripts go through a single CLI entrypoint: `scripts/cli.ts` (run via tsx).

## Critical Rule: Never Edit engine/ Directly

The `engine/` directory contains the full Chromium checkout and is gitignored. **Never write to files under `engine/` as a deliverable.** All changes must go through:

1. **`src/patches/`** — Unified diff patches against Chromium files. Patch paths mirror the Chromium tree (e.g., `src/patches/chrome/browser/ui/BUILD.gn.patch` patches `engine/src/chrome/browser/ui/BUILD.gn`).
2. **`src/dao/`** — Dao's own C++ code, copied into `engine/src/dao/` during import.

Workflow: edit in `src/dao/` or `src/patches/`, run `npm run import` to apply, iterate in `engine/` for testing, then `npm run export` to capture changes back.

### Source of Truth

**`src/patches/` and `src/dao/` are the source of truth.** The code in `engine/` is unstable and may be in any state (partially applied patches, manual test edits, etc.). When reading Chromium integration code, always refer to `src/patches/*.patch` files for the canonical version. Only read `engine/` files when you need to see the original unpatched Chromium code for context, or when debugging a build failure.

## Architecture

### Patch System
- Patches are applied via `git apply` inside `engine/src/`
- The import command auto-detects already-applied patches (reverse-check)
- Export generates per-file diffs from `git diff` in the Chromium tree

### Dao UI Layer (C++ / Chromium Views)
All Dao UI code lives under `src/dao/browser/ui/views/` in the `dao::` namespace. Key components:

- **DaoSidebarView** — Main vertical sidebar (240px default, collapsible to 4px with animation). Contains address bar, favorites, tab list, and space bar sections.
- **DaoTabListView / DaoTabItemView** — Vertical tab list replacing the top tab strip
- **DaoAddressBarView** — URL bar embedded in the sidebar
- **DaoFavoritesView** — Pinned sites section
- **DaoSpaceBarView** — Workspace/space switcher
- **DaoNewTabButton** — New tab button in the sidebar
- **DaoCornerOverlayView** — Overlay painted on top of web contents
- **DaoSidebarSectionView** — Reusable collapsible section container

### Integration with Chromium
Patches inject Dao components into the Chromium frame:
- `browser_view.cc.patch` — Adds DaoSidebarView and DaoCornerOverlayView to BrowserView, hides the top tab strip
- `browser_view.h.patch` — Declares sidebar/overlay member pointers
- `browser_view_layout.cc.patch` — Adjusts layout to accommodate the sidebar
- `browser_frame_mac.mm.patch` — macOS-specific frame modifications
- `BUILD.gn.patch` files — Add Dao source files to the build graph

### Build Configuration
- `configs/common.gn` — Shared GN args (component build, no NaCl, proprietary codecs, no Google API keys)
- `configs/macos.gn` — macOS-specific args (use_lld)
- `dao.json` — Master config: Chromium version, target platform, branding info
- Build outputs to `engine/src/out/dao/`
- Post-build: auto-fixes lld duplicate dylib issue on macOS component builds

## Design Language

- **Color**: Dark purple-gray sidebar `(40,32,48)`. All UI elements use white at varying opacity for hierarchy (text 100%/59%/39%, backgrounds 14%, separators 12%). Accent color is purple `(140,100,220)` for active states only. Content area dynamically adopts the web page's background color, switching between light/dark adaptive text and separators based on luminance.
- **Shape**: Content area has 10px rounded corners + 6-step progressive soft shadow, with 6–8px margin from the sidebar. Corner radius hierarchy: command bar 16px > URL pill 14px > tabs/buttons 12px > content 10px > favorite icons 8px.
- **Interaction**: Uniform white 6% InkDrop ripple, FocusRing disabled globally. Spotlight-style command bar with translucent scrim + centered floating panel + ghost text completion. Hover reveals close buttons and background highlights; keyboard-first (arrow keys to select, Tab to complete, Esc to dismiss).
- **Typography**: `system-ui` / `sans-serif`, 12–16px range. Titles use SemiBold, everything else Normal.
- **Icons**: All icons use [Lucide](https://lucide.dev/) — inline SVG with `stroke="currentColor"`, `fill="none"`, `stroke-width="2"`, and `stroke-linecap/linejoin="round"`. Never use emoji or custom icon paths; always pick from the Lucide set.
- **Philosophy**: Dark minimalism + Arc-style vertical tabs, maximizing content immersion with a purple brand identity.

## Code Conventions

- All source code, comments, and commit messages must be in **English**
- Communicate with the user in **Chinese**
- Use the `dao::` C++ namespace for all Dao-owned code
- Chromium coding style: `raw_ptr<>`, `METADATA_HEADER`, include guards with `#ifndef`
- **Batch all changes** — Chromium builds are expensive. Deliver all related changes (headers, implementations, BUILD.gn entries, patches) in a single pass. Verify includes, forward declarations, and symbol references are consistent before finishing.
- **Always verify with build** — After a complete task's code changes are all done, run `npm run rebuild` (import + build) to verify compilation. Do not consider the task finished until the build passes.
- **NEVER use `npm run build`** — Always use `npm run build:debug` (debug build) instead. The release build is extremely slow and not needed during development. This applies to all build-related commands: use `npm run rebuild` (which uses debug) for iterative dev, and `npm run build:debug` for build-only.
- **NEVER run `autoninja -C out/dao-debug chrome` directly** — Always go through the project scripts (`npm run rebuild` or `npm run build:debug`) so the build directory, GN args, and import/export workflow stay consistent. If the output directory reports a Ninja/Siso mismatch, run `gn clean out/dao-debug` before rebuilding instead of invoking `autoninja` manually.
- **NEVER commit to git automatically** — Do not run `git add`, `git commit`, or `git push` unless the user explicitly asks. Leave all git operations to the user.

## gstack

Use the `/browse` skill from gstack for all web browsing. Never use `mcp__claude-in-chrome__*` tools.

Available skills: `/office-hours`, `/plan-ceo-review`, `/plan-eng-review`, `/plan-design-review`, `/design-consultation`, `/review`, `/ship`, `/land-and-deploy`, `/canary`, `/benchmark`, `/browse`, `/qa`, `/qa-only`, `/design-review`, `/setup-browser-cookies`, `/setup-deploy`, `/retro`, `/investigate`, `/document-release`, `/codex`, `/cso`, `/autoplan`, `/careful`, `/freeze`, `/guard`, `/unfreeze`, `/gstack-upgrade`.

## Prerequisites

- macOS with depot_tools in PATH (`gclient`, `gn`, `autoninja`)
- Node.js >= 18
- ~100 GB disk space for Chromium source + build
