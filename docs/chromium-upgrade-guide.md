# Chromium Kernel Upgrade Guide

> **Purpose.** The step-by-step manual for rebasing Dao Browser onto a newer Chromium
> version. Covers three phases: **Rebase** (get the new kernel building with all patches),
> **Redesign** (re-fit patches that upstream refactored out from under us), and
> **Self-check** (prove no feature was lost).
>
> Companion documents:
> - [`feature-checklist.md`](feature-checklist.md) — the per-feature verification checklist. **This is your definition of "done."**
> - [`features.md`](features.md) — prose tour of what Dao adds.
> - [`development.md`](development.md) — day-to-day build workflow.
>
> Prior precedent in git history: the 134 → 147 rebase (`fce90f6 feat: resolve all issue
> after rebase new chrome engine`, `ef9a1b2 chore: update version number`). Read those
> diffs before starting a new bump.

---

## Mental model: what a "kernel upgrade" actually changes

Dao is **not a Chromium fork in the git sense**. The full Chromium tree lives in `engine/`
(gitignored). What's version-controlled is a *thin overlay*:

1. **`src/patches/*.patch`** (~166 files) — unified diffs against Chromium source, applied
   with `git apply` inside `engine/src`.
2. **`src/dao/`** — Dao's own C++ / WebUI, copied verbatim into `engine/src/dao/`.
3. **`scripts/chromium-rewrites.ts`** — mechanical `chrome://`→`dao://` rewrites applied at
   import time to ~30 grd/html/json files that would otherwise be dozens of tiny patches.
4. **`branding/`** — logos + `BRANDING` manifest, copied into the theme dir at import time
   (NOT patched — see the CLAUDE.md warning about bare `npm run export`).

A kernel upgrade = point `engine/` at a new Chromium tag, then get all four overlay
mechanisms to re-apply cleanly and re-verify behavior. **Patches are the hard part**:
`git apply` is strict, and upstream churns the files we hook.

### The three modification vectors (and their failure modes)

| Vector | Applied by | Failure mode on rebase |
|--------|-----------|------------------------|
| Patches | `git apply` | Hunk fails to apply (loud) **or** applies fuzzily to the wrong place (silent) |
| `src/dao/` copy | `smartCopySync` | Compile error if a `dao/browser/...` include or a hooked Chromium API renamed |
| Generated rewrites | `applyChromiumRewrites()` | "Missing file" warning if a target path moved; new files not rewritten |
| Branding copy | `copyIfDifferent` | Rare; theme dir layout change |

---

## Phase 0 — Preparation

Do this **before** touching the version number.

1. **Establish a known-good baseline.** On the current version, run a full
   `npm run rebuild` and walk [`feature-checklist.md`](feature-checklist.md) end-to-end.
   Anything already broken is not the rebase's fault — but you must know that now, not
   later. Screenshot the working sidebar / PiP / agent / command bar for visual reference.

2. **Pick the target Chromium tag.** Prefer a stable channel tag (`git ls-remote` the
   Chromium repo, or check https://chromiumdash.appspot.com/releases). Note the milestone
   number (e.g. `M147`).

3. **Read the delta scope.** The bigger the version jump, the more upstream refactoring.
   Skim the Chromium release notes for anything touching: BrowserView/layout, the WebUI
   scheme/loader stack, PiP, extensions/MV2, the settings WebUI, and macOS windowing —
   these are Dao's deepest hooks (all 🔴 in the checklist).

4. **Back up the current engine state** (optional but cheap insurance):
   ```bash
   cd engine/src && git stash list && git status
   ```
   Confirm `engine/src` is on the old tag and clean-ish. The `out/dao-debug/` build dir is
   what you're protecting — a botched rebase shouldn't require re-downloading 100GB.

5. **Never use a worktree.** Per project policy, all rebase work happens on `main` in the
   primary checkout. A worktree has no `engine/` and would start Chromium from scratch.

---

## Phase 1 — Rebase (get it applying + building)

### 1.1 Bump the version and fetch the new kernel

Edit `dao.json`:
```jsonc
"version": {
  "product": "chromium",
  "version": "148.0.XXXX.YY",   // ← new Chromium tag
  "display": "1.0.63"           // ← Dao product version (2–4 numeric segments)
}
```

> `version.version` drives `gclient sync --revision src@refs/tags/<version>`.
> `version.display` is the user-facing Dao version, injected only at the Info.plist layer
> via `dao_display_version` — **do NOT** stamp it into `chrome/VERSION` (breaks policy /
> flag-expiry / metrics generators that assume the real Chromium MAJOR).

Fetch the new source (updates the existing checkout in place):
```bash
npm run download        # runs: gclient sync --revision src@refs/tags/<version>
```

Then reset `engine/src` tracked files to the pristine new tag so patches apply against
clean upstream:
```bash
cd engine/src && git status          # see what the old patches left behind
# The importer's --force does this for you (git checkout -- .), but confirm first.
```

### 1.2 First import attempt — triage the failures

```bash
npm run import -- --force
```

`--force` hard-resets `engine/src` tracked files to HEAD before applying (untracked
artifacts like `out/` and `engine/src/dao/` are preserved). The importer:

1. Reverse-checks each patch (parallel) to skip already-applied ones.
2. Batch-applies the rest; on batch failure, falls back to **per-file apply for precise
   error reporting**.
3. Applies generated rewrites (`applyChromiumRewrites`).
4. Injects the Dao version into `version_ui.cc`.
5. Copies `src/dao/` → `engine/src/dao/`, Sparkle framework, branding assets.

**The output you care about:** `Patches: N applied, M already applied, K failed`. Any
`K > 0` prints a `fix-import-patches.sh` command. That's your work list.

### 1.3 Resolve failed patches (the core loop)

For each failed patch, work in `engine/src` and regenerate the patch. **Prioritize by the
Rebase-risk rating in [`feature-checklist.md`](feature-checklist.md)** — 🔴 patches are
where hunks mis-apply.

**Per-patch workflow:**

```bash
# 1. See exactly what the patch tries to do
cat src/patches/<path>.patch

# 2. Look at the current upstream file it targets
#    (engine/src/<path> — the pristine new-version file)

# 3. Apply by hand: re-create the intended change in engine/src/<path>
#    using the patch as a guide. The FEATURE (from the checklist) is what
#    you're preserving, not the literal diff.

# 4. Re-export the single patch (NEVER bare `npm run export`)
npm run export -- <path>          # e.g. chrome/browser/ui/BUILD.gn

# 5. Verify it now applies from clean
cd engine/src && git checkout -- <path>
git apply ../../src/patches/<path>.patch && echo OK
```

**Rules for patch resolution:**

- **Never run bare `npm run export`.** It rewrites all ~166 patches (stripping
  semantically-significant trailing whitespace → "corrupt patch at line N") and falsely
  creates patches for branding-managed files. Always scope: `npm run export -- <file>`.
- **If `npm run export` corrupts a patch** (trailing-whitespace stripping), fall back to
  raw `git diff`:
  ```bash
  cd engine/src && git diff -- <path> > ../../src/patches/<path>.patch
  ```
- **Watch for silently-mis-applied hunks.** `git apply` can land a fuzzy hunk in the wrong
  function. After resolving, read the resulting `engine/src` file around the change.
- **Respect apply order for same-file patches.** Two patches edit `app_controller_mac.mm`
  (`.mm.patch` then `_little_dao_external.mm.patch`); two edit
  `startup_browser_creator_impl.cc` (`_impl` then `_little_dao`); `router.ts` is touched by
  both `router.ts.patch` and `router_dao.ts.patch`. Both hunks in each pair must land.
- **Suspicious diff metadata.** A few patches have placeholder-looking index hashes
  (`0000000..1111111`, `1234567..abcdef`) — they apply by *context*, not blob hash, so a
  clean apply doesn't prove correct placement. Verify manually:
  `account_consistency_mode_manager.cc`, `extension_features.cc`,
  `chrome_content_browser_client.cc`, `chrome_render_widget_host_view_mac_delegate.mm`.

### 1.4 Handle generated-rewrite failures

If import warns `Generated Chromium rewrites skipped N missing file(s)`, a rewrite target
moved or was removed upstream. Edit `scripts/chromium-rewrites.ts`:

- File **renamed/moved** → update its path in the relevant list
  (`SCHEME_TEXT_REWRITE_PATHS`, `WEBUI_BASE_HREF_PATHS`, or `EXTENSION_API_FEATURE_PATHS`).
- New upstream WebUI page with a `<base href="chrome://...">` → add to
  `WEBUI_BASE_HREF_PATHS`.
- New `chrome://` host in an `_api_features.json` → add to the
  `EXTENSION_API_FEATURE_URLS_BY_PATH` allowlist set.

These are code, not patches — no `git apply` fragility, but they must stay in sync with the
upstream file set.

### 1.5 Resource-ID collisions (grit)

`tools/gritsettings/resource_ids.spec.patch` reserves ID ranges (8400 agent / 8605 sidebar
/ 8625 welcome / 8645 dao_strings). Upstream continually adds entries and pushes the
next-available range up. If grit reports an **ID overlap**, relocate Dao's whole block to a
fresh gap above upstream's new high-water mark and re-export the patch.

### 1.6 Build

```bash
npm run rebuild     # import + build:debug
```

**Never** run `autoninja`/`ninja`/`siso`/`gn gen` directly (corrupts Siso state → full
rebuild). If you hit a Siso/Ninja state mismatch:
```bash
cd engine/src && gn clean out/dao-debug   # then:
npm run build:debug
```

Iterate compile errors. The most common categories after a jump:

- **Renamed Chromium API** a Dao patch/`src/dao` file calls → update the call site, re-export.
- **`base::ListValue` → `base::Value::List`** style migrations (watch `about_handler.cc`).
- **Removed feature flag** a default-flip patch targeted (`kBackToOpener`,
  `kExtensionManifestV2*`, media switches) → the flag may be gone; re-establish the default
  elsewhere or drop the patch if the feature was absorbed.
- **BUILD.gn `sources=[`/`deps=[` prepend** conflicts (`chrome/browser/ui/BUILD.gn`) — the
  `.gni` interface (`dao_ui_sources.gni`) must still be imported and consumed.

---

## Phase 2 — Redesign (when a patch can't just be re-applied)

Some upstream changes are structural: the function you hooked was split, renamed, deleted,
or its whole subsystem was rearchitected. Re-applying the old diff is impossible — you must
**re-implement the feature against the new code**. This is redesign, not rebase.

### When to redesign vs. patch

| Situation | Action |
|-----------|--------|
| Hunk fails but the target code is recognizably the same | Re-apply by hand (Phase 1.3) |
| The hooked function was renamed / moved | Find the new home, re-hook, re-export |
| The function was split into several | Decide which new function(s) carry the behavior; may become multiple hunks |
| The subsystem was rearchitected (e.g. layout system rewrite) | Redesign: re-implement the *feature* using new APIs |
| Upstream absorbed the behavior (feature now default, bug now fixed) | **Drop the patch** — don't re-add redundant code |

### Redesign workflow

1. **Start from the feature, not the diff.** Open [`feature-checklist.md`](feature-checklist.md)
   for the affected row — the "Feature" and "Verify" columns define what must still be true.
2. **Locate the new integration point.** Use the "Mechanism" description to find where the
   equivalent hook now belongs in the new tree.
3. **Re-implement minimally.** Match the *behavior*, using current upstream APIs and Dao
   accessors (`dao_sidebar()`, `dao_command_bar()`, etc.). Keep the hook small.
4. **Export the new patch.** `npm run export -- <path>`.
5. **Verify the feature**, not just compilation.

### Known redesign-prone areas (highest structural risk)

These are Dao's deepest hooks — expect them to need redesign, not just re-application, on
any large jump:

- **BrowserView + layout impls** (`browser_view.cc`, `browser_view_tabbed_layout_impl.cc`,
  `browser_view_popup_layout_impl.cc`) — Chromium's browser layout system is actively
  evolving; the whole content-inset / sidebar-carve-out geometry may need re-fitting.
- **WebUI scheme/loader stack** (`render_frame_host_impl.cc` `CommitNavigation`,
  `web_ui_url_loader_factory.cc`, `webui_util.cc` CSP) — hot content-layer code.
- **PiP frame view** (`picture_in_picture_browser_frame_view.cc`) — deep macOS widget /
  event-monitor / animation integration.
- **Extension install dialog + MV2** — upstream is *removing* MV2 infrastructure, so the
  flags/enums the patches target may disappear entirely. This is the feature most likely to
  require genuine redesign (or a different mechanism to keep MV2 alive).
- **Settings About page** (`about_page.ts`) — heavily interleaved `<if not is_macosx>`
  guards around frequently-added update-status methods.
- **macOS menus** (`main_menu_builder.mm`) — anchor-tag-based insertion breaks if menu
  structure changes.

### SDK-compat patches: prefer dropping over redesigning

`ui/display/mac/screen_utils_mac.mm.patch` and the `kCGImageByteOrder32Host` renames exist
only because the build SDK lags. When the Xcode / macOS SDK is bumped, upstream's version
becomes correct — **delete these patches** rather than re-applying. Check
[`feature-checklist.md`](feature-checklist.md) §16.

---

## Phase 3 — Self-check (prove no feature was lost)

**A clean build is necessary but not sufficient.** The dominant failure mode is a patch
that *applied cleanly* but landed fuzzily, or a silent default-flip that reverted. Self-check
catches these.

### 3.1 Import / patch integrity

```bash
npm run import         # expect: N applied, M already applied, 0 failed, 0 missing rewrites
```
- [ ] 0 failed patches.
- [ ] 0 missing generated-rewrite targets.
- [ ] No grit resource-ID overlap in the build log.

### 3.2 Diff every silent-loss pattern

These apply cleanly even when wrong. Explicitly diff each after rebase:

- [ ] **Feature-flag flips** — `kBackToOpener` (features.cc), `kExtensionManifestV2*`
      (extension_features.cc), `kLogSodaLoadFailures`/`kAutoDocPiPPermissionPromptAndroid`
      (media_switches.cc). Confirm the flag still exists AND still defaults as Dao intends.
- [ ] **`return` hardcodes** — `ShouldShowDownloadBubble()`=false, `IsSyncAllowedByFlag()`=false,
      `GetDefaultStartupType()`=LAST, `GetOverlayView()`=nullptr, `signinAllowed`=false.
- [ ] **`#if 0` / early-return blocks** — `infobar_utils.cc`,
      `browser_user_education_service.cc` (IPH/tutorials). Confirm the disabled range didn't
      accidentally swallow new upstream logic.
- [ ] **DCHECK removals** — web_request event router (2), css_default_style_sheets (`4u`
      bound), paint_controller (fatal→LOG). Confirm still absent/relaxed.
- [ ] **Default-value flips** — `render_active_ = false` (PiP frame view header),
      `MemorySaverModeState::kEnabled`, `kSigninAllowedOnNextStartup=false`.
- [ ] **String substitutions** — copyright "The Chromium Authors"→"MsgByte" (if upstream
      reworded, the substring won't match and it silently fails). Keychain "Dao Safe
      Storage" and profile dir "Dao" **must NOT change** (data-loss if they do).

### 3.3 Automated tests

```bash
npm run test           # build browser_tests + run all Dao* tests
```
- [ ] All `Dao*` browser tests green (except the known-`DISABLED_` set — 5 memory-store
      FTS5 tests, `DISABLED_DaoPage` webui-test-loader test).
- [ ] `./engine/src/out/dao-debug/browser_tests --gtest_filter="Dao*" --gtest_list_tests`
      shows the expected suite is present (a dropped test target = silently reduced coverage).

### 3.4 Manual smoke test (the checklist)

Walk [`feature-checklist.md`](feature-checklist.md) in a running build
(`npm run start:debug`). At minimum exercise the 🔴 rows and these seams:

- [ ] **Sidebar shell** — vertical tabs, no top strip, content inset + rounded corners,
      drag-resize, collapse/expand.
- [ ] **Command bar** — Cmd+T shows it (not a blank tab), Cmd+L pre-fills URL.
- [ ] **`dao://` scheme** — `dao://settings`, `dao://agent`, `dao://dao-welcome` all load;
      `chrome://settings` redirects/displays as `dao://`; a PDF renders (not blank).
- [ ] **Agent** — panel opens (Cmd+E), can send a prompt to the LLM.
- [ ] **PiP** — plain video auto-PiPs on tab switch; Document-PiP top bar hover-fades;
      resize persists.
- [ ] **MV2 extension** installs and runs.
- [ ] **macOS menus** — the 4 Dao items present and functional; Cmd+D duplicates tab.
- [ ] **Branding** — About shows "Dao" + product version; correct Dock icon; profile at
      `~/Library/Application Support/Dao`.

### 3.5 Cross-cutting greps

- [ ] `grep -rn "chrome://" ` in changed strings → no user-visible occurrences that should
      be `dao://`.
- [ ] Confirm `browser_about_handler` reverse-rewriter is still registered (else omnibox
      shows `chrome://`).
- [ ] Confirm the WebUI CSP host list (`webui_util.cc`) mirrors any newly-added upstream
      `chrome://` host into `dao://` (else that resource silently 404s).

### 3.6 Sign-off

Only after 3.1–3.5 pass, and every 🔴 patch hunk was re-read against new upstream:
- [ ] Update `docs/features.md` "Current Status" (version, patch count if changed).
- [ ] Update [`feature-checklist.md`](feature-checklist.md) if patches were added/dropped/redesigned.
- [ ] Commit — **only when the user explicitly authorizes it** (see CLAUDE.md ABSOLUTE GIT
      RULES). Leave changes unstaged otherwise.

---

## Quick reference — command cheat sheet

```bash
# Version bump + fetch
#   edit dao.json version.version
npm run download                    # gclient sync to new tag

# Apply overlay
npm run import -- --force           # reset engine/src, apply patches + rewrites + dao/ copy
npm run import                      # non-destructive re-apply (idempotent)

# Resolve a single failed patch
cat src/patches/<path>.patch                       # read intent
#   hand-edit engine/src/<path>
npm run export -- <path>                           # regenerate THAT patch only
cd engine/src && git checkout -- <path> \
  && git apply ../../src/patches/<path>.patch      # verify clean apply

# If export corrupts the patch (trailing whitespace):
cd engine/src && git diff -- <path> > ../../src/patches/<path>.patch

# Build (NEVER autoninja/ninja/siso directly)
npm run rebuild                     # import + build:debug
npm run build:debug                 # build only
gn clean out/dao-debug              # ONLY to fix Siso state mismatch, then build:debug

# Verify
npm run test                        # Dao browser_tests
npm run start:debug                 # launch with stderr logging
```

## Anti-patterns (things that will cost you hours)

- ❌ Bare `npm run export` → rewrites/corrupts all 166 patches + fabricates branding patches.
- ❌ Running `autoninja`/`ninja`/`siso`/`gn gen` directly → Siso state corruption, full rebuild.
- ❌ Editing `engine/` as a deliverable → lost on next import; always edit `src/patches` or `src/dao`.
- ❌ Trusting "patch applied" as "feature works" → fuzzy hunks + silent flips are the #1 failure.
- ❌ Stamping `version.display` into `chrome/VERSION` → breaks policy/flag/metrics generators.
- ❌ Changing the Keychain "Dao Safe Storage" string or profile dir name → user data loss.
- ❌ Rotating/altering `SUPublicEDKey` in Info.plist without the matching private key → bricks auto-update.
- ❌ Using a git worktree → no `engine/`, Chromium builds from scratch.
