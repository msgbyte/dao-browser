# Windows Desktop Development Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` for native execution or `subagent-driven-development` if the user selects delegated execution. Steps use checkboxes for tracking. Repository rules override skill suggestions to create worktrees or commit.

**Goal:** Build and launch Dao's Chromium desktop browser on Windows x64 through the existing npm development commands.

**Architecture:** Adapt the existing TypeScript CLI, GN configurations, and Dao-owned native integration. Keep platform selection shared between build and launch, and isolate macOS implementations in the existing GN graph. Resolve additional native failures from actual compiler output rather than predicting undocumented Chromium APIs.

**Tech Stack:** Node.js 22.23.3 via nvm, TypeScript, Vitest, Chromium 149.0.7827.201, GN, Chromium C++/Views/Aura, VS 2022/MSVC, Windows SDK 26100.

**Spec:** [Approved design](../specs/2026-09-25-windows-development-design.md).

## Global constraints

- Work in `D:\develop\dao-browser`, on `main`; do not create branches or worktrees.
- Reuse `npm run setup`, `npm run rebuild`, and `npm run start:debug`.
- Confirm compilation exclusively through `npm run rebuild`.
- Never edit `engine/` directly; use tracked Dao sources and patches plus the importer.
- Do not commit or run state-changing Git operations without the authorization required by `AGENTS.md`.
- No new runtime dependencies, paid translation, generated vendor edits, or automatic force imports.
- Preserve macOS arm64 behavior; report that macOS runtime verification requires a Mac.
- Windows launch profiles must be separate from installed Chrome and from each other for debug/release.
- Windows installers, signing, release publishing, and an automatic-update backend are outside this milestone.
- Unsupported platform-specific actions must not appear to succeed.
- User-facing browser text uses the existing localization systems.

## Review focus

1. Paths containing spaces or non-ASCII characters, including the installed Visual Studio path: argument boundaries must survive (Tasks 1 and 3).
2. Windows batch wrappers and shell metacharacters: legitimate paths work, and arguments cannot execute an extra command (Task 1).
3. A partial download, repeated import, or failed patch repair: no unrelated file or global Git setting changes, and retries preserve useful state (Task 2).
4. Debug/release and macOS/Windows selection: no profile collision, accidental macOS build settings, or change to macOS launch behavior (Tasks 1 and 3).
5. Incomplete native platform support: no invisible active overlay, swallowed clicks after cancellation, or enabled action backed by a successful no-op (Tasks 4 and 5).

## File responsibilities

| Files | Responsibility |
| --- | --- |
| `scripts/utils.ts` | Host target resolution, executable lookup, child-process argument handling |
| `scripts/commands/build.ts`, `configs/common.gn`, `configs/macos.gn`, new `configs/win.gn` | Platform-specific build arguments |
| `scripts/commands/download.ts`, `scripts/commands/import.ts`, `scripts/fix-import-patches.sh` | Repeatable download/import and narrow patch repair |
| New `scripts/commands/start.ts`, `scripts/cli.ts`, `package.json` | Shared platform-aware launch entry point |
| `scripts/commands/__tests__/windows_development.test.ts` | Focused Windows CLI behavior checks |
| `scripts/commands/__tests__/import.test.ts`, `package_scripts.test.ts`, `release.test.ts` | Existing integration and process-lifecycle regression checks |
| `src/dao/browser/ui/dao_ui_sources.gni` and native callers listed in Task 4 | Native source selection and actual Windows UI behavior |
| `src/dao/browser/mcp/` and capability consumers listed in Task 4 | Isolate the existing macOS/POSIX MCP implementation |
| `src/patches/chrome/browser/ui/views/frame/` | Chromium integration changes justified by build or runtime evidence |
| `docs/development.md`, `docs/features.md`, `docs/feature-checklist.md` | Developer instructions, supported behavior, and regression criteria |

## Task 1: Resolve the native target and run Windows tools correctly

**Modify:** `scripts/utils.ts`, `scripts/commands/build.ts`, `configs/common.gn`, `configs/macos.gn`.
**Create:** `configs/win.gn`, `scripts/commands/__tests__/windows_development.test.ts`.
**Regression tests:** relevant process-lifecycle cases in `scripts/commands/__tests__/release.test.ts`.

**Interfaces:** Keep `loadConfig()` as the raw project configuration reader. Export the following from `utils.ts` for build/download/launch consumers:

```ts
export interface BuildTarget {
  os: 'mac' | 'win';
  cpu: 'arm64' | 'x64';
}

export function resolveBuildTarget(
  config: DaoConfig,
  platform: NodeJS.Platform = process.platform,
  arch: string = process.arch,
): BuildTarget;
```

- [ ] Add focused target-selection tests before changing the implementation. Load the existing config and assert:

```ts
expect(resolveBuildTarget(config, 'win32', 'x64'))
    .toEqual({os: 'win', cpu: 'x64'});
expect(resolveBuildTarget(config, 'darwin', 'arm64'))
    .toEqual({os: 'mac', cpu: 'arm64'});
expect(resolveBuildTarget(config, 'darwin', 'x64'))
    .toEqual({os: 'mac', cpu: 'arm64'});
expect(() => resolveBuildTarget(config, 'win32', 'arm64')).toThrow();
expect(() => resolveBuildTarget(config, 'linux', 'x64')).toThrow();
expect(config.build).toEqual({target_os: 'mac', target_cpu: 'arm64'});
```

- [ ] Run `npm.cmd run test:webui -- scripts/commands/__tests__/windows_development.test.ts` and confirm the missing target behavior fails.
- [ ] Implement the resolver: Windows x64 selects win/x64; the supported Darwin host retains the configured mac/arm64 target; unsupported host/config pairs fail explicitly. Do not mutate `dao.json` or globally change `loadConfig()` for macOS release/package consumers.
- [ ] Replace shell-string executable lookup with argument-based invocation of `where.exe` on Windows and `which` on POSIX. Return the first valid match; preserve `null` for a missing tool.
- [ ] Add process tests using a temporary directory with spaces and a non-ASCII component. An executable fixture prints `JSON.stringify(process.argv.slice(2))`; verify exact arguments and nonzero exit propagation. Add a Windows `.cmd` fixture and a sentinel-file injection test. Cover spaces, `&`, parentheses, `%`, quotes, and empty arguments: preserve them or reject unsupported batch input before execution; never interpolate them unchecked.
- [ ] Use `shell: false` for executable programs. Handle known `.cmd`/`.bat` wrappers through an explicit, tested Windows invocation path. Preserve the existing POSIX abort/process-group lifecycle behavior and environment forwarding. Do not add a generic shell abstraction or an npm dependency.
- [ ] Extract the build argument assembly into an exported pure helper in `build.ts`:

```ts
export function createBuildArgs(
  config: DaoConfig,
  target: BuildTarget,
  debug: boolean,
  commonArgs: string,
  platformArgs: string,
): string;
```

- [ ] Remove the unconditional architecture from `common.gn`. Keep target selection authoritative in the resolver and write `target_os`/`target_cpu` consistently. Start `configs/win.gn` with the Windows linker setting:

```gn
use_lld = true
```

- [ ] Keep `is_component_build = true` and `is_official_build = false` for the existing development flavor. Restrict its `use_lld = false` override, bundle branding, and dylib fix to macOS. Preserve the shared `dao_display_version` argument. Fail if a required platform config is absent rather than silently omitting it.
- [ ] Assert generated Windows arguments select win/x64 and retain lld; assert macOS development arguments retain their existing behavior. Verify unchanged argument content does not rewrite `args.gn`.
- [ ] Run the focused tests, existing process-group configuration tests, and `npm.cmd run typecheck`. Do not invoke a build in this task.

## Task 2: Make setup and import repeatable on Windows

**Modify:** `scripts/commands/download.ts`, `scripts/commands/import.ts`, `scripts/fix-import-patches.sh` only if the existing repair script requires a demonstrated compatibility change.
**Tests:** `scripts/commands/__tests__/windows_development.test.ts`, `scripts/commands/__tests__/import.test.ts`.
**Consumes:** `resolveBuildTarget`, corrected `which` and `runStreaming` from Task 1.
**Produces:** the existing `download` and `import` commands with unchanged public command names and patch-state semantics.

- [ ] Add a command test that reports a missing depot_tools executable before creating `engine/`; add a Windows batch-tool fixture for `gclient` invocation. Assert download keeps the version from `dao.json` and retains the existing shallow/retry behavior.
- [ ] Run these new cases and record the failing behavior.
- [ ] Remove the unconditional `git config --global http.postBuffer` side effect. Do not substitute a different global Git setting. If checkout line endings or long paths require overrides, pass only demonstrated settings to the download process and preserve the caller's existing Git configuration entries.
- [ ] Guard the entire Sparkle framework-copy block, including its missing-framework warning, with the resolved macOS target. Windows must never execute `ditto`, `codesign`, or `rm -rf` through that block.
- [ ] Normalize glob patterns and stored patch paths independently of filesystem paths. Keep Git diff headers POSIX-style, and pass actual filesystem paths as argument-array elements.
- [ ] Harden patch target validation for Windows drive paths, UNC paths, and backslash traversal without weakening existing checks:

```ts
// Add these rejected header paths to the existing parser tests.
const unsafeWindowsTargets = [
  'C:/outside.txt',
  'C:\\outside.txt',
  '\\\\server\\share\\outside.txt',
  '..\\outside.txt',
];
```

- [ ] Exercise the existing import fixtures with CRLF patch content, a workspace path containing spaces, an already-applied patch, and one failing patch among unrelated files. Assert the second import changes nothing and a failed repair preserves unrelated content.
- [ ] Resolve Git for Windows' shell when repair actually needs it. Pass repository-relative forward-slash patch paths into the existing shell repair script, rather than handing it `D:\...` paths that its POSIX parser treats as relative. Keep the transaction/rollback behavior and narrow target validation.
- [ ] Run `npm.cmd run test:webui -- scripts/commands/__tests__/import.test.ts scripts/commands/__tests__/windows_development.test.ts`. Any fixture Git activity must remain in its disposable fixture repository, never the main checkout.
- [ ] After the focused script checks pass, run `npm.cmd run setup` to obtain the pinned Chromium source and perform the first import. Native source selection does not prevent this import. Preserve a partial checkout on network failure and resume through the same command. This supplies the exact headers needed by Task 4; do not compile yet.

## Task 3: Add a platform-aware launch command

**Create:** `scripts/commands/start.ts`.
**Modify:** `scripts/cli.ts`, `package.json`.
**Tests:** `scripts/commands/__tests__/windows_development.test.ts`, focused package-script checks.
**Consumes:** `resolveBuildTarget` and the safe executable process path from Task 1.
**Produces:** `startCommand` and a pure launch-description helper:

```ts
export interface LaunchOptions {
  debug?: boolean;
  view?: boolean;
  little?: boolean;
  urls: string[];
}

export function createLaunchSpec(
  config: DaoConfig,
  target: BuildTarget,
  options: LaunchOptions,
  engineDir: string,
  localAppData?: string,
): {command: string; args: string[]};
```

- [ ] Add launch-description tests for Windows and macOS, debug and release, missing output, Unicode paths, and a URL containing shell metacharacters. Assert debug/release Windows profiles differ and neither is Chrome's profile.
- [ ] Run the new tests and confirm they fail before implementing the command.
- [ ] For Windows, launch the actual `chrome.exe` under the selected `out/dao` or `out/dao-debug` directory. Set a Dao-specific `--user-data-dir` under `LOCALAPPDATA`; report a clear error if that variable is absent. Use argument arrays and `shell: false`.
- [ ] For macOS, preserve release `open` behavior, debug execution inside the correctly named `.app`, mock-keychain behavior, and existing debug logging/UI-devtools flags. Preserve `getAppName` from `build.ts` as the bundle-name source.
- [ ] Wire existing npm launch aliases through the CLI:

```json
{
  "start": "tsx scripts/cli.ts start",
  "start:debug": "tsx scripts/cli.ts start --debug",
  "start:debug:view": "tsx scripts/cli.ts start --debug --view",
  "start:little": "tsx scripts/cli.ts start --debug --little https://bing.com"
}
```

- [ ] Preserve the macOS Little Dao alias's `open -a` route to the debug app. On Windows, do not silently map a Little Dao request to a regular window: route it only if the native implementation exists, otherwise report that the entry point is unavailable. Keep the macOS-only clean-profile helper documented as such; do not introduce profile deletion into the standard launch path. Register the command in `scripts/cli.ts`.
- [ ] Verify CLI help, missing-executable diagnostics, argument forwarding to a fixture executable, and the affected package-script tests. No browser launch is claimed before a real executable exists.

## Task 4: Isolate native platform dependencies without hiding failures

**Modify:** `src/dao/browser/ui/dao_ui_sources.gni`; native callers in `dao_sidebar_ui.cc`, `dao_sidebar_view.cc`, `dao_command_bar_view.cc`, `dao_control_center_popup.cc`, `split/dao_split_view.cc`, and Little Dao views.
**Native helper files:** `src/dao/browser/ui/views/dao_native_util_mac.{h,mm}`, `dao_native_share_mac.{h,mm}`, `sidebar/dao_file_icon_util_mac.{h,mm}`; add Windows or common `.cc` files only for concrete required behavior.
**MCP files:** `src/dao/browser/mcp/BUILD.gn`, `dao_mcp_service.{h,cc}`, `dao_mcp_transport.{h,cc}`, `dao_mcp_runtime_files.{h,mm}`, and service consumers in UI/settings.
**Tests:** targeted additions to `src/dao/browser/ui/views/dao_browser_browsertest.cc` and applicable MCP/browser contract tests.

- [ ] Move the three macOS UI `.mm` sources out of the common source list:

```gn
if (is_mac) {
  dao_browser_ui_sources += [
    "//dao/browser/ui/views/dao_native_share_mac.mm",
    "//dao/browser/ui/views/dao_native_util_mac.mm",
    "//dao/browser/ui/views/sidebar/dao_file_icon_util_mac.mm",
  ]
}
```

- [ ] Trace each helper caller before changing its header or source selection. Restrict traffic-light positioning and AppKit share pickers to macOS. Keep unsupported share controls hidden or explicitly unavailable; do not leave an enabled button whose handler does nothing.
- [ ] Implement required Windows event handling through Chromium Views/Aura. Use the pinned Chromium headers obtained during Task 2 to confirm exact API signatures. Preserve overlay input interception and restore the previous event state when the overlay closes, a drag is cancelled, or its source disappears. Do not install inert implementations of the drag watchdog and claim the drag behavior works.
- [ ] Provide download icons through Chromium's existing icon facilities or a small Windows implementation with explicit icon-handle ownership. Preserve the existing thumbnail-to-icon fallback for unsupported decoders; document any remaining Windows thumbnail limitation.
- [ ] Isolate the MCP runtime as a platform capability: the existing runtime uses `uid_t`, `ScopedFD`, Unix sockets, and POSIX file permissions. Keep those implementation sources and Unix-only tests out of Windows targets. Keep shared protocol/tool types available where required.
- [ ] Make the Windows capability report unavailable at the service/UI boundary. Hide or disable the MCP enable action and related copy/connect actions using existing localization, or add localized text if needed. Do not create an unauthenticated substitute listener or report that a nonexistent transport is running. A Windows MCP transport is a later feature, not an implicit part of this first launch milestone.
- [ ] Review `base::FilePath::value()` uses in the modified native files for Windows wide-string assumptions. Convert explicitly at text/protocol boundaries using existing Chromium helpers.
- [ ] Add focused browser checks for command-bar open/close restoring click input, ordinary tab create/navigate/close, drag cancellation restoring interaction, and unsupported MCP state. Keep macOS-specific tests under macOS conditions.
- [ ] Verify no required core behavior has been replaced by a success-returning stub. Record platform limitations for Task 6. Batch these tracked changes before Task 5's real rebuild.

## Task 5: Fetch the pinned engine, compile, and resolve concrete failures

**Files:** Changes discovered here remain in the specific Dao source or matching `src/patches/` file named by the compiler/runtime evidence. The known initial frame integration owners are `browser_view.cc.patch`, `browser_view.h.patch`, and `layout/browser_view_tabbed_layout_impl.cc.patch`.
**Consumes:** Tasks 1-4; no alternative build pipeline.
**Produces:** a real Windows browser executable or an accurately documented external blocker, never an assumed successful build.

- [ ] Run the focused script suite and TypeScript checking once after related edits are batched. Fix a new failure in its owning task before downloading/building.
- [ ] Confirm sufficient free space, required tools, and proxy availability. Do not rewrite user-wide Git configuration or manually modify the depot_tools checkout.
- [ ] Verify Task 2's checkout version and successful import. If setup was interrupted, resume and observe:

```powershell
npm.cmd run setup
```

- [ ] Preserve `engine/` state if network access fails; resume through the existing download/setup path. Do not use force import, erase a partial checkout, or run an alternate checkout recipe to mask failure.
- [ ] Read the fetched pinned Chromium headers when resolving the Windows helper APIs from Task 4, update only tracked Dao files, and synchronize through normal import.
- [ ] Run the only allowed compile-confirmation entry point:

```powershell
npm.cmd run rebuild
```

- [ ] Classify the first build failure as tool execution, GN graph, compile, or link. Read the failing command and owning source; fix the smallest tracked owner and any genuinely related errors together. Repeat `npm.cmd run rebuild` only after new changes justify it. Keep a concise list of the actual failures and fixes in the execution notes.
- [ ] Launch `npm.cmd run start:debug`. Confirm the running executable and the isolated Dao development profile, then manually check the visible sidebar, address navigation, tab creation/closing, and minimize/maximize/close controls. Check that overlays do not swallow page input after dismissal.
- [ ] If existing relevant browser-test binaries are available, run only the necessary test filters. Do not compile tests through a prohibited alternate command. If no test binary is available, report browser-test coverage as unrun and retain direct runtime evidence.
- [ ] Do not mark the milestone complete if the executable has not successfully launched. Distinguish a source change, a passed script test, a completed build, and a runtime observation.

## Task 6: Document the verified workflow and close the review

**Modify:** `docs/development.md`, `docs/features.md`, `docs/feature-checklist.md`.
**Validation:** `npm.cmd run docs:check`, `git diff --check`, focused checks affected by final edits.

- [ ] Document Windows x64 prerequisites, nvm usage, depot_tools/Python ordering, local Visual Studio selection, SDK/debugger requirements, and the existing setup/rebuild/start commands. Use `npm.cmd` in PowerShell examples where the execution policy blocks `npm.ps1`.
- [ ] Document the actual Windows profile and output paths, and clearly identify macOS-only helpers. Explain any manual proxy requirement without embedding this machine's private environment in portable project code.
- [ ] Update the desktop feature inventory and checklist with the behavior actually implemented. Include unsupported MCP transport and native-share/thumbnail gaps only where still applicable; do not imply feature parity that was not verified.
- [ ] Add regression rows for native target selection, repeat imports, Windows launch/profile isolation, core window controls, overlay dismissal, and unavailable platform actions.
- [ ] Run the documentation checker and inspect the complete tracked diff. Verify no generated engine changes, generated vendor edits, new dependency, unauthorized Git changes, branch, or worktree was introduced.
- [ ] Perform a final code review using the execution method selected by the user. Report actual test/build/runtime outcomes, remaining platform limitations, and the unverified macOS runtime status.

## Execution handoff

Recommended: native execution in the current session. The CLI, import, GN, and native changes form one dependent Windows build path, and a single implementer can carry compiler findings across tasks without handoff overhead. Delegated execution is available if the user prefers separate task implementers and reviewers.

The user approved native execution in the existing primary checkout. Progress
and validation results are recorded in the task ledger while setup and build
verification run.
