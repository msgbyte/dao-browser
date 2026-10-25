# AGENTS.md

## Communication

- Communicate with the user in Chinese.
- Write source code, comments, tests, commit messages, PR titles, and documentation in English unless the user explicitly asks otherwise.
- All user-visible text must use the internationalization system described below.

## Project Overview

Dao Browser is a Chromium-based browser with an Arc-style vertical sidebar, currently targeting macOS arm64. This repository tracks Dao-owned code and patches; the large Chromium checkout and build output live in gitignored `engine/`.

Read `docs/features.md` first for desktop Chromium behavior and
`docs/features-android.md` first for Android GeckoView behavior when asked what
Dao Browser does, when locating the owner of a behavior, or before adding a
feature that may overlap with existing work.

## Feature Documentation

- When adding or materially changing a desktop or Android feature, update its inventory (`docs/features.md` for desktop, `docs/features-android.md` for Android) and `docs/feature-checklist.md` in the same change.
- If a feature change does not require updates to its platform feature inventory or the shared checklist, explicitly mention why in the final response.

## Changelog

- Write for ordinary browser users: describe what they can do or what is now fixed, in plain language. Record user-facing changes in the same change, including performance, security, compatibility, installation, and updates.
- Skip development-only work with no user impact: tests, internal refactors, CI, local build tooling, developer docs, and agent instructions. Even alongside a user-facing fix, omit regression tests, coverage, and verification details; describe only the user outcome. No entry is needed to explain an omission.
- Maintain `docs/changelog/en.md` and `docs/changelog/zh-CN.md` by hand, with matching entries in the same order. English is the source. Separate unrelated changes; do not pad entries, invent details, or list commits. Check historical backfills against same-platform tag diffs.
- Add pending entries to the affected platform's `Unreleased` section. Desktop, Android, and iOS versions are independent; shared changes may need entries on multiple platforms. Website and repository-wide work defaults to desktop.
- Keep one line per bullet and preserve bracketed platform/version identifiers. On version bumps, archive notes under the released version and date, retaining an empty `Unreleased`. Never assign pending work to an old version.
- Desktop/Android release commands archive notes automatically; manual bumps, including iOS public releases, must do so in the same change. Packaging, Windows attachments, and TestFlight builds do not archive notes.
- Never hand-edit generated locales or invoke paid translation automatically. See Internationalization below and `docs/changelog.md` for translation and release details.

## Source Of Truth

- `src/dao/` contains Dao-owned C++ / WebUI / assets copied into Chromium during import.
- `src/patches/` contains unified diff patches against Chromium files. Patch paths mirror `engine/src/`.
- `engine/` is generated / checked-out Chromium state, available to read for context or build errors.
- `branding/` contains brand assets used by the import pipeline.
- `configs/` and `dao.json` define Chromium version, platform, GN args, and branding metadata.
- `scripts/cli.ts` is the single CLI entrypoint behind the npm scripts.
- `website/` is the standalone marketing/product website.

Treat `src/dao/` and `src/patches/` as canonical. If `engine/` and tracked files disagree, trust the tracked files unless you are investigating a build failure.

## Critical Rules

- Never edit files under `engine/` directly unless the latest user message explicitly asks for an `engine/` edit. Keep deliverable changes in `src/dao/`, `src/patches/`, or other tracked Dao-owned sources.
- Never invoke `autoninja`, `ninja`, `siso`, `gn gen`, or other Chromium build tools directly.
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

## Patch Workflow

- Add Dao-owned files under `src/dao/` and Chromium integration changes under `src/patches/**/*.patch`.
- Update tracked sources first, then run plain `npm run import` to sync `engine/src/`. Force import is only for resetting and reapplying patch state, with explicit user approval as required above.
- For explicitly authorized `engine/src/` debugging edits, export only the intentionally changed file with `npm run export -- <file>`.
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
- Use Lucide SVGs, never emoji, for icons. Fetch the current upstream SVG and copy its child nodes verbatim; do not recreate path data from memory.

## WebUI Rules

- Use Lit / TypeScript with inline styles, bundled scoped CSS, or Lit `<style>` blocks. Tailwind is allowed only inside generated/vendor pi-web-ui components.
- Files under `src/dao/browser/ui/webui/resources/agent/vendor/` are generated from `vendor.config.ts` and `vendor/entries/*`; change the entry source and rerun `npm run vendor` instead of editing generated output.

## Internationalization

Never hardcode user-facing copy in Dao-owned UI. All user-visible text must have internationalization support, even when the source text is English.

- C++ Views strings go in `src/dao/browser/strings/dao_strings.grd` and are read with `l10n_util::GetStringUTF16` or `GetStringFUTF16`.
- Sidebar WebUI strings go in `src/dao/browser/strings/dao_strings.grd`, are registered on the sidebar `WebUIDataSource` with `AddLocalizedString` / `UseStringsJs`, and are read from TypeScript through `loadTimeData.getString(...)`.
- Agent WebUI strings go in `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts` and are read with `t('key', { var: 'x' })`.
- Android strings use `android/app/src/main/res/values/strings.xml` as the English source and qualified `values-*/strings.xml` files for translations. Android follows the system locale and falls back to English.
- iOS strings use `ios/DaoBrowser/Resources/en.lproj/*.strings` as the English source, are read in Swift with `L("key")`, and have translations in `<lang>.lproj/` (`zh-CN` → `zh-Hans`). iOS follows the system locale and falls back to English. New `.lproj` folders are picked up by xcodegen automatically.
- `zh-CN` is hand-authored and treated as the tone reference.
- Other desktop, Android, iOS, and changelog locales are generated manually by the user via `OPENAI_API_KEY=... sh ./i18n.sh`; do not run it automatically. The root command dispatches independent translators, and `--only desktop`, `--only android`, `--only ios`, or `--only changelog` limits the run. Changelog English and Simplified Chinese remain hand-authored even with `--force`.

## Native Toast Feedback

- All native browser toasts, including upstream Chromium actions such as Copy Link, must render through the shared `dao::DaoToastView`. Do not introduce a parallel toast surface or restore Chromium's toast appearance.
- Dao-owned feedback uses `DaoToastView::ShowToast`. Existing Chromium callers may keep `ToastController`: its toast view hosts `DaoToastView` and anchors inside the active content area. Keep this shared integration when upgrading Chromium instead of adding per-command replacements.
- Preserve localized messages, action buttons, menus, accessibility, dismissal callbacks, queueing, and tab/navigation scope when adapting Chromium toasts.
- Keep toast messages action-specific and localized.

## Testing And Verification

- For C++ / Chromium changes, batch related source, patch, resource, and test edits, then confirm compilation with `npm run rebuild`.
- Run the smallest relevant tests, lint checks, or manual verification. Use full coverage (`npm run test`) only for broad cross-cutting risk or an explicit user request, never as compile confirmation.
- For browser tests, run the relevant binary with a narrow `--gtest_filter`.
- For WebUI-only changes, run `npm run test:webui` and `npm run lint:lit` when relevant.
- Browser tests live mainly in `src/dao/browser/ui/views/dao_browser_browsertest.cc`; typical filters are `Dao*`, `DaoSidebar*`, or `DaoTabBrowserTest.*`.

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

Use Conventional Commits for commit messages and PR titles:

```text
<type>(<scope>): <summary>
```

For example: `fix(chat): adjust TTS popup spacing`.

- Use a lowercase type such as `fix`, `feat`, `chore`, `refactor`, `test`, `docs`, `build`, `ci`, `perf`, or `revert`.
- Use a concise lowercase scope when it is clear.
- Keep the summary short, imperative, and without a trailing period.
- Do not add a commit body or PR body unless explicitly requested.
