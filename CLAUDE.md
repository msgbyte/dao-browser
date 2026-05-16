# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Dao Browser is a Chromium-based browser with a vertical tab sidebar (inspired by Arc), currently targeting macOS arm64. It builds on top of Chromium source using a patch-based architecture — only patch files and Dao's own C++ code are version-controlled; the full Chromium tree lives in `engine/` (gitignored).

For a full inventory of features Dao Browser adds on top of Chromium (sidebar, AI Agent, Picture-in-Picture, Split View, Control Center, Little Dao, branding patches, etc.), see [`docs/features.md`](docs/features.md). Read this first when asked what the project does, when locating the component that owns a given behavior, or before adding a feature that might overlap with an existing one.

## Build Commands

```bash
npm run setup          # First-time: download Chromium + apply patches
npm run rebuild        # Iterative dev: import patches + build
npm run build          # Build only (gn gen + autoninja)
npm run build:debug    # Debug build (is_debug=true, component build)
npm run import         # Apply patches + copy src/dao/ into engine/
npm run export         # Export unstaged engine/src changes as patch files
npm run export -- <file>  # Export patch for a specific file
npm run test           # Build + run all Dao browser tests
npm run test:build     # Build browser_tests only (no run)
npm run start          # Launch the built browser
npm run start:debug    # Launch with stderr logging
```

All scripts go through a single CLI entrypoint: `scripts/cli.ts` (run via tsx).

> **⚠️ NEVER run `autoninja`, `ninja`, or `siso` directly. ALWAYS use the npm scripts above. Direct build tool invocation corrupts build state and causes full rebuilds. ⚠️**

## Critical Rule: Never Edit engine/ Directly

The `engine/` directory contains the full Chromium checkout and is gitignored. **Never write to files under `engine/` as a deliverable.** All changes must go through:

1. **`src/patches/`** — Unified diff patches against Chromium files. Patch paths mirror the Chromium tree (e.g., `src/patches/chrome/browser/ui/BUILD.gn.patch` patches `engine/src/chrome/browser/ui/BUILD.gn`).
2. **`src/dao/`** — Dao's own C++ code, copied into `engine/src/dao/` during import.

Workflow: edit in `src/dao/` or `src/patches/`, run `npm run import` to apply, iterate in `engine/` for testing, then `npm run export` to capture changes back.

### Source of Truth

**`src/patches/` and `src/dao/` are the source of truth.** The code in `engine/` is unstable and may be in any state (partially applied patches, manual test edits, etc.). When reading Chromium integration code, always refer to `src/patches/*.patch` files for the canonical version. Only read `engine/` files when you need to see the original unpatched Chromium code for context, or when debugging a build failure.

## Git Worktrees

`engine/` is gitignored and holds 100GB+ of Chromium source + build output (`out/dao-debug/`). A git worktree only checks out version-controlled files, so a fresh worktree has **no `engine/`** — any Chromium build inside it starts from scratch. This is hours-to-days of lost incremental compile state.

Rules:
- **Web / docs / scripts only** (`website/`, `docs/`, `scripts/`, `src/patches/` text edits with no rebuild) — worktree is fine. For `website/`, symlink `node_modules` from the main checkout to skip `npm install`.
- **Anything that triggers a Chromium build** (`src/dao/**`, `src/patches/**` changes that need verification, `npm run rebuild`, `npm run test`) — **do NOT use a worktree**. Work on a branch in the main checkout so `engine/` and `out/dao-debug/` stay warm.
- Do NOT symlink `engine/` into a worktree to share the build dir — two checkouts racing the same `out/` corrupts Siso/Ninja state (see the build rules below).

## Architecture

### Patch System
- Patches are applied via `git apply` inside `engine/src/`
- The import command auto-detects already-applied patches (reverse-check)
- Export generates per-file diffs from `git diff` in the Chromium tree
- **Reverting a patch**: When reverting a patch file via `git checkout -- src/patches/foo.patch`, you must also revert the corresponding engine file (`cd engine/src && git checkout -- <path>`) then re-apply the patch (`git apply <patch>`). Otherwise engine/ and patches/ will be out of sync.

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

- **Color**: Light mode uses a pale blue-gray sidebar `(231,238,245)` with dark text `(30,20,40)` at 100% / 60% / 40% opacity for hierarchy, and black at 6–8% opacity for backgrounds and separators. Dark mode (follows system setting, not user-toggleable) uses a deep blue-gray sidebar `(54,59,64)` with white text at the same 100% / 60% / 40% hierarchy, and white at 6–8% opacity for backgrounds and separators. Accent color is blue `(70,120,190)` for active states only, shared across both modes. Content area dynamically adopts the web page's background color, switching between light/dark adaptive text and separators based on luminance.
- **Shape**: Content area has 10px rounded corners + 6-step progressive soft shadow, with 6–8px margin from the sidebar. Corner radius hierarchy: command bar 16px > URL pill 14px > tabs/buttons 12px > content 10px > favorite icons 8px.
- **Interaction**: Uniform InkDrop ripple (black 4% in light mode, white 6% in dark mode — dark surfaces need slightly stronger feedback), FocusRing disabled globally. Spotlight-style command bar with translucent scrim + centered floating panel + ghost text completion. Hover reveals close buttons and background highlights; keyboard-first (arrow keys to select, Tab to complete, Esc to dismiss).
- **Typography**: `system-ui` / `sans-serif`, 12–16px range. Titles use SemiBold, everything else Normal.
- **Icons**: All icons use [Lucide](https://lucide.dev/) — inline SVG with `stroke="currentColor"`, `fill="none"`, `stroke-width="2"`, and `stroke-linecap/linejoin="round"`. Never use emoji or custom icon paths; always pick from the Lucide set. **Never hand-write or recall SVG path data from memory** — Lucide is updated frequently and older cached versions use outdated polygon/line shapes (e.g., the current `play`/`skip-back`/`volume-2` use bezier paths, not polygons). Always fetch the authoritative SVG from the upstream repo before adding or modifying an icon: `curl -s https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/<name>.svg`, then copy the `<path>`/`<rect>`/`<line>` children verbatim (keep original coordinates, don't "simplify").
- **Philosophy**: Calm minimalism + Arc-style vertical tabs, maximizing content immersion with a blue brand identity. The chrome recedes so the web page is the focal point, whether on a light or dark system.

## Code Conventions

- All source code, comments, and commit messages must be in **English**
- Communicate with the user in **Chinese**
- Use the `dao::` C++ namespace for all Dao-owned code
- Chromium coding style: `raw_ptr<>`, `METADATA_HEADER`, include guards with `#ifndef`
- **Batch all changes** — Chromium builds are expensive. Deliver all related changes (headers, implementations, BUILD.gn entries, patches) in a single pass. Verify includes, forward declarations, and symbol references are consistent before finishing.
- **Always verify with build** — After a complete task's code changes are all done, run `npm run rebuild` (import + build) to verify compilation. Do not consider the task finished until the build passes.
- **NEVER use `npm run build`** — Always use `npm run build:debug` (debug build) instead. The release build is extremely slow and not needed during development. This applies to all build-related commands: use `npm run rebuild` (which uses debug) for iterative dev, and `npm run build:debug` for build-only.
- **⚠️ NEVER run `autoninja`, `ninja`, `siso`, or any build tool directly ⚠️** — This is the single most important build rule. NEVER run `autoninja -C ...`, `ninja -C ...`, or any direct build command. ALWAYS use the project npm scripts (`npm run rebuild`, `npm run build:debug`). Direct invocation causes Siso/Ninja state mismatches that corrupt the build directory and trigger expensive full rebuilds. If you see a build state mismatch error, run `gn clean out/dao-debug` and then `npm run build:debug` — do NOT attempt to fix it by running ninja/autoninja directly, that will make it worse.
- **NEVER commit to git automatically** — Do not run `git add`, `git commit`, or `git push` unless the user explicitly asks. Leave all git operations to the user.
- **Agent WebUI: never use Tailwind utility classes in Dao-owned code** — The Tailwind CSS under `src/dao/browser/ui/webui/resources/agent/vendor/pi_web_ui.css` is **precompiled** by the vendor pipeline from pi-web-ui's source. Any utility class we write in Dao-owned files (`agent.html`, `dao_agent_app.ts`, `dao_chat_view.ts`, `dao_settings_view.ts`, etc.) is NOT in that compiled output and will silently render as unstyled. Always use inline `style=""` attributes or scoped rules in `agent.css` / lit `<style>` blocks for Dao-owned UI. Utility classes like `bg-card`, `rounded-xl`, `p-4` are fine ONLY when they appear inside pi-web-ui components that we render through — because those classes were scanned at pi-web-ui build time.
- **Agent WebUI vendor directory is generated — NEVER hand-edit** — Everything under `src/dao/browser/ui/webui/resources/agent/vendor/` (currently `pi_runtime_bundle.ts` and `pi_web_ui.css`) is produced by `npm run vendor` from `vendor.config.ts` + `vendor/entries/*`. Treat these files as read-only build artifacts. To change anything that ends up there: edit the entry source (e.g., `vendor/entries/pi-runtime.entry.ts` to re-export more pi-mono APIs; `vendor/entries/pi-web-ui-css.build.mjs` to tweak the CSS copy step) and re-run `npm run vendor` (or `npm run vendor -- --entry=<name>` to rebuild one entry). Never patch the generated files directly — the next `npm run vendor` will overwrite your edits and the `manifest.json` sha256 check will flag the drift.

## Internationalization (i18n)

**Never hardcode user-facing English in Dao-owned code.** Two pipelines:

- **C++ Views**: add `<message name="IDS_DAO_<MODULE>_<DESC>" desc="...">` to `src/dao/browser/strings/dao_strings.grd`, then call `l10n_util::GetStringUTF16(IDS_DAO_...)` (or `GetStringFUTF16` for `$1`-style placeholders). Empty `u""` and runtime data (`base::UTF8ToUTF16(url)`) stay as-is — they are data, not copy.
- **Agent WebUI**: add a `<view>.<area>.<purpose>` key to `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`, then `import {t} from './i18n/i18n.js'; t('key', { var: 'x' })` (placeholder syntax: `{name}`). Don't compare against literal English as a state sentinel (`if (label === 'Copied')`) — translations break the comparison.

`zh-CN` is hand-authored and treated as the tone reference. Other locales are filled by **manually** running `OPENAI_API_KEY=sk-... sh ./i18n.sh` (gpt-4o; flags: `--langs`, `--force`, `--only grd|webui`, `--dry-run`). Do not invoke the script automatically — translation costs tokens and overwrites files; the user runs it when ready.

Two browser tests (`DaoI18nBrowserTest.*`) smoke-test the pipeline; keep them green. When Chromium's locale set changes, rerun `tsx scripts/i18n-bootstrap.ts` to refresh xtb / `<lang>.ts` skeletons + the `i18n_locale_files.gni` GN fragment.

## Testing

Uses Chromium's `browser_tests` framework. Test file: `src/dao/browser/ui/views/dao_browser_browsertest.cc`.

```bash
npm run test           # Build browser_tests + run all Dao* tests
npm run test:build     # Build browser_tests only (via --target flag)
```

To run specific tests directly:
```bash
./engine/src/out/dao-debug/browser_tests --gtest_filter="DaoSidebar*"          # One test suite
./engine/src/out/dao-debug/browser_tests --gtest_filter="DaoTabBrowserTest.*"  # Another suite
./engine/src/out/dao-debug/browser_tests --gtest_filter="Dao*" --gtest_list_tests  # List all
```

### Adding New Tests

1. Add test cases to `src/dao/browser/ui/views/dao_browser_browsertest.cc` using `IN_PROC_BROWSER_TEST_F`
2. Access Dao views via `BrowserView::GetBrowserViewForBrowser(browser())->dao_sidebar()` (and other accessors)
3. Build target is `dao_browser_tests` source_set in `chrome/browser/ui/BUILD.gn.patch`, wired into the main `browser_tests` via `chrome/test/BUILD.gn.patch`
4. Run `npm run test` to verify

### Current Test Coverage

- **Sidebar**: exists, default width, collapse/expand toggle
- **Sidebar Resize**: drag changes width, clamp to min/max (150–400px), ignored when collapsed, width preserved after collapse/expand cycle
- **AddressBar**: exists, correct height
- **CommandBar**: initially hidden, show/hide
- **Tabs**: create, switch, close
- **SplitView**: exists but inactive, split creates two panes
- **CornerOverlay**: exists and visible
- **Folder Persistence**: file path in profile, write/read round-trip, not exists by default on fresh profile

## gstack

Use the `/browse` skill from gstack for all web browsing. Never use `mcp__claude-in-chrome__*` tools.

Available skills: `/office-hours`, `/plan-ceo-review`, `/plan-eng-review`, `/plan-design-review`, `/design-consultation`, `/review`, `/ship`, `/land-and-deploy`, `/canary`, `/benchmark`, `/browse`, `/qa`, `/qa-only`, `/design-review`, `/setup-browser-cookies`, `/setup-deploy`, `/retro`, `/investigate`, `/document-release`, `/codex`, `/cso`, `/autoplan`, `/careful`, `/freeze`, `/guard`, `/unfreeze`, `/gstack-upgrade`.

## ABSOLUTE BUILD RULES (READ BEFORE ANY BUILD ACTION)

1. **NEVER run `autoninja`, `ninja`, or `siso` directly** — not even for a single object file, not even "just to check the error". There are NO exceptions.
2. **ALWAYS use `npm run rebuild` or `npm run build:debug`** — these are the ONLY approved ways to build.
3. **NEVER run `gn gen` directly** — the npm scripts handle this automatically.
4. If build state is corrupted (Siso/Ninja mismatch), run `gn clean out/dao-debug` then `npm run build:debug`.

## Prerequisites

- macOS with depot_tools in PATH (`gclient`, `gn`, `autoninja`)
- Node.js >= 18
- ~100 GB disk space for Chromium source + build
