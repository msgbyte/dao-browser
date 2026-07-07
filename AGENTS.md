# AGENTS.md

This file gives Codex project-specific context for working in Dao Browser.

## Communication

- Communicate with the user in Chinese.
- Write source code, comments, tests, commit messages, PR titles, and documentation in English unless the user explicitly asks otherwise.
- Do not embed non-English user-facing copy directly in code. User-visible text in any language must be provided through the appropriate internationalization system.

## Project Overview

Dao Browser is a Chromium-based browser with an Arc-style vertical sidebar, currently targeting macOS arm64. The full Chromium checkout and build output live under `engine/`, which is gitignored and very large. This repository tracks Dao-owned code and patch files, not the whole Chromium tree.

Read `docs/features.md` first when asked what Dao Browser does, when locating the owner of a behavior, or before adding a feature that may overlap with existing work.

## Feature Documentation

- When adding or materially changing a Dao Browser feature, update `docs/features.md` in the same change to keep the feature catalog current.
- When adding or materially changing a Dao Browser feature, update `docs/feature-checklist.md` in the same change so Chromium upgrade and regression checks cover the new behavior.
- If a feature change does not require updates to either document, explicitly mention why in the final response.

## Source Of Truth

- `src/dao/` contains Dao-owned C++ / WebUI / assets copied into Chromium during import.
- `src/patches/` contains unified diff patches against Chromium files. Patch paths mirror `engine/src/`.
- `engine/` is generated / checked-out Chromium state. Read it for context or build errors, but do not edit it directly unless the latest user message explicitly asks you to do so.
- `branding/` contains brand assets used by the import pipeline.
- `configs/` and `dao.json` define Chromium version, platform, GN args, and branding metadata.
- `scripts/cli.ts` is the single CLI entrypoint behind the npm scripts.
- `website/` is the standalone marketing/product website.

Treat `src/dao/` and `src/patches/` as canonical. If `engine/` and tracked files disagree, trust the tracked files unless you are investigating a build failure.

## Critical Rules

- Never edit files under `engine/` directly unless the latest user message explicitly asks for an `engine/` edit. Keep deliverable changes in `src/dao/`, `src/patches/`, or other tracked Dao-owned sources.
- Never run `autoninja`, `ninja`, `siso`, or direct Chromium build tools.
- Never run `gn gen` directly.
- For compile confirmation, use only `npm run rebuild`. Do not use `npm run build`, `npm run build:debug`, `npm run test:build`, direct Chromium build tools, or any other compile path as a substitute. Other compile paths can lose or corrupt the warm Chromium build cache.
- Do not use `npm run import -- --force` unless it is necessary. If force import is needed, ask the user for explicit confirmation first and explain why normal `npm run import` is insufficient.
- Never run bare `npm run export`; always scope it with `npm run export -- <file>`.
- Never hand-edit generated vendor files under `src/dao/browser/ui/webui/resources/agent/vendor/`.
- Never invoke `i18n.sh` automatically; it uses paid translation and overwrites files.
- Do not create git worktrees or feature branches for this repository. Work in the primary checkout on `main`.
- Do not run state-changing git commands unless the latest user message explicitly authorizes the exact action.

State-changing git commands include `git add`, `git commit`, `git push`, `git pull`, `git reset`, `git rebase`, `git checkout` when it discards changes, `git stash`, `git tag`, `git merge`, `git cherry-pick`, `git restore`, and `git apply` against the repo root. Read-only git commands such as `git status`, `git diff`, `git log`, `git show`, `git branch`, and `git blame` are allowed.

## Common Commands

```bash
npm run setup          # First-time setup: download Chromium and apply patches
npm run import         # Apply patches and copy src/dao into engine
npm run rebuild        # The only allowed compile-confirmation command
npm run test           # Full Dao test sweep; use only when broad coverage is needed
npm run test:build     # Build browser_tests only; never use for compile confirmation
npm run test:webui     # Run Vitest WebUI tests
npm run lint:lit       # Check Lit reactive fields
npm run vendor         # Regenerate WebUI vendor bundles
npm run vendor:check   # Verify generated vendor bundles
npm run start          # Launch release app
npm run start:debug    # Launch debug app with stderr logging
```

Chromium builds are expensive. Batch related C++, header, BUILD.gn patch, resource, and test edits before building. When you need to confirm compilation, run `npm run rebuild` and no other build command, because alternate compile paths can lose the incremental build cache.

## Patch Workflow

- Add Dao-owned files under `src/dao/`.
- Update Chromium integration through `src/patches/**/*.patch`.
- Run `npm run import` to apply patches into `engine/src/`.
- Prefer plain `npm run import`. Use `npm run import -- --force` only when the existing `engine/src` patch state must be reset and reapplied, and only after the user explicitly confirms that force import is allowed.
- Do not hand-edit `engine/src/` to sync patch changes. Update the canonical tracked file first, then run `npm run import`.
- If the user explicitly asks you to iterate inside `engine/src/` during debugging, export only the file you intentionally changed with `npm run export -- <file>`.
- After reverting a patch file, also ensure the corresponding `engine/src/` file is reverted and the intended patch state is reapplied.

Bare `npm run export` is dangerous because it can create false patches for branding-managed files and rewrite unrelated patches.

## Architecture Map

Dao UI code uses the `dao::` namespace and mostly lives under `src/dao/browser/ui/views/`.

- `DaoSidebarView` is the main vertical sidebar container.
- `DaoTabListView` / `DaoTabItemView` implement vertical tabs.
- `DaoAddressBarView` is the sidebar URL pill.
- `DaoCommandBarView` is the Spotlight-style command bar.
- `DaoCornerOverlayView` paints rounded content corners and shadow.
- `DaoColors` centralizes Dao color tokens.
- `DaoLucideIcons` centralizes Lucide icon usage.
- `src/dao/browser/ui/webui/resources/sidebar/` hosts the sidebar Lit WebUI.
- `src/dao/browser/ui/webui/resources/agent/` hosts the AI Agent Lit WebUI.
- `src/dao/browser/agent/` contains agent services, memory, scenarios, skills, and tab lock state.
- `src/dao/browser/pip/` contains Picture-in-Picture additions.
- `src/dao/browser/ui/views/split/` contains Split View.
- `src/dao/browser/ui/views/little_dao/` contains the Little Dao window.
- `src/dao/browser/qrcode/` and `third_party/zxing-cpp/` support QR generation.

Important Chromium integration patches include:

- `src/patches/chrome/browser/ui/views/frame/browser_view.cc.patch`
- `src/patches/chrome/browser/ui/views/frame/browser_view.h.patch`
- `src/patches/chrome/browser/ui/views/frame/browser_view_layout.cc.patch`
- `src/patches/chrome/browser/ui/views/frame/browser_frame_mac.mm.patch`
- `src/patches/chrome/browser/ui/BUILD.gn.patch`
- `src/patches/chrome/test/BUILD.gn.patch`

## Code Conventions

- Follow Chromium C++ style: `raw_ptr<>`, `METADATA_HEADER`, include guards, existing ownership patterns, and existing `base` / `views` conventions.
- Keep Dao-owned C++ in the `dao::` namespace.
- Prefer existing local helpers and design tokens before adding new abstractions.
- Do not hardcode user-facing copy in any language in Dao-owned code.
- Do not use emoji as icons. Use Lucide SVGs for iconography.
- When adding or changing Lucide icons, fetch the current upstream SVG and copy the child nodes verbatim; do not recreate path data from memory.

## WebUI Rules

- Dao WebUI uses Lit / TypeScript.
- Do not use Tailwind utility classes in Dao-owned WebUI files unless they are inside generated/vendor pi-web-ui components.
- For Dao-owned WebUI, use inline styles, scoped CSS files, or Lit `<style>` blocks that are actually included in the bundle.
- Files under `src/dao/browser/ui/webui/resources/agent/vendor/` are generated from `vendor.config.ts` and `vendor/entries/*`; change the entry source and rerun `npm run vendor` instead of editing generated output.

## Internationalization

Never hardcode user-facing copy in Dao-owned UI. All user-visible text must have internationalization support, even when the source text is English.

- C++ Views strings go in `src/dao/browser/strings/dao_strings.grd` and are read with `l10n_util::GetStringUTF16` or `GetStringFUTF16`.
- Sidebar WebUI strings go in `src/dao/browser/strings/dao_strings.grd`, are registered on the sidebar `WebUIDataSource` with `AddLocalizedString` / `UseStringsJs`, and are read from TypeScript through `loadTimeData.getString(...)`.
- Agent WebUI strings go in `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts` and are read with `t('key', { var: 'x' })`.
- `zh-CN` is hand-authored and treated as the tone reference.
- Other locales are generated manually by the user via `OPENAI_API_KEY=... sh ./i18n.sh`; do not run it automatically.

## Testing And Verification

- For C++ / Chromium changes, compile confirmation must be `npm run rebuild` after edits are complete. Do not substitute any other command for compilation confirmation.
- When running unit tests, run only the smallest necessary subset by default. Do not default to the full test suite; use full coverage only when the change has broad cross-cutting risk or the user explicitly asks for it.
- Choose the smallest relevant verification for the changed surface, such as focused WebUI tests, lint checks, targeted browser test filters, or a narrow manual/browser verification path.
- For browser tests, prefer running only the relevant test binary and `--gtest_filter` needed for the change. Use `npm run test` only when broad Dao coverage is intentionally needed, and treat it as test verification, not compile confirmation. If compilation must be confirmed, run `npm run rebuild`.
- For WebUI-only changes, run `npm run test:webui` and `npm run lint:lit` when relevant.
- Existing browser tests live mainly in `src/dao/browser/ui/views/dao_browser_browsertest.cc`.
- Dao browser test filters are typically `Dao*`, `DaoSidebar*`, or `DaoTabBrowserTest.*`.

Do not claim a build or test passed unless you actually ran it and saw it pass.

## Design Language

- Overall feel: calm minimalism, Arc-style vertical tabs, web content as the focal point, blue Dao identity.
- Light sidebar: pale blue-gray `rgb(231, 238, 245)` with dark text hierarchy.
- Dark sidebar: deep blue-gray `rgb(54, 59, 64)` with white text hierarchy.
- Accent: blue `rgb(70, 120, 190)` for active states.
- Shape hierarchy: command bar 16px, URL pill 14px, tabs/buttons 12px, content 10px, favorite icons 8px.
- Interaction: subtle InkDrop ripple, hover highlights, keyboard-first command bar, no global FocusRing.
- Typography: system UI / sans-serif, mostly 12-16px, semibold titles, normal body text.

## Commit And PR Naming

When creating commits or PRs, use an Angular / Conventional Commits style title/message:

```text
<type>(<scope>): <summary>
```

Examples:

```text
fix(chat): adjust TTS popup spacing
feat(image): add prompt presets
```

Rules:

- Use a lowercase type such as `fix`, `feat`, `chore`, `refactor`, `test`, `docs`, `build`, `ci`, `perf`, or `revert`.
- Use a concise lowercase scope when it is clear.
- Keep the summary short, imperative, and without a trailing period.
- Do not add a commit body or PR body unless explicitly requested.
