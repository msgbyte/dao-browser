# Dao Browser

A Chromium-based browser with a left sidebar for vertical tabs, inspired by Arc.

## Prerequisites

- macOS (initial target platform)
- [depot_tools](https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up) installed and in `PATH`
- Node.js >= 18
- ~100 GB free disk space for Chromium source + build

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

## Architecture

Chromium source lives in `engine/` (gitignored). Only patch files and Dao's own
code are version-controlled. This follows the Zen Browser approach of maintaining
a clean separation between upstream and custom code.

## License

MPL-2.0
