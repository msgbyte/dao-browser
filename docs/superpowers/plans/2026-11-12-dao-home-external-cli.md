# Dao Home External CLI Implementation Plan

> **For agentic workers:** Use the approved design and execute the tasks below in the primary checkout. Independent file scopes may be delegated with superpowers:dispatching-parallel-agents. Never commit or create worktrees.

**Goal:** Ship `dao-home` and `dao-browser home` with private temporary editing, independent local IPC, and real-Profile preview in a dedicated visible Dao window.

**Architecture:** Extract the existing authenticated native socket substrate into neutral local IPC classes while preserving MCP compatibility. A separate Home service owns one-hour Profile sessions and dedicated preview windows, and calls the existing Home project service. The CLI edits temporary snapshots and publishes only an unchanged, validated draft.

**Tech Stack:** Chromium C++, base/net/crypto, Views, Lit/TypeScript, embedded Markdown guides, existing native and Vitest tests.

**Spec:** `docs/superpowers/specs/2026-11-12-dao-home-external-cli-design.md`

## Global Constraints

- No MCP endpoint, stdio server, tool catalog, or MCP preference in the Home execution path.
- Home workspaces are owner-private CLI-created system temporary directories; no project files or artifacts in CWD.
- Preview uses the actual regular Profile and connectors in a dedicated visible window; ordinary windows remain independent.
- Fixed one-hour session; source permissions and immutable preview/publication checks remain enforced.
- Maximum 500 files and 20 MiB decoded project data; local IPC Home envelope maximum 32 MiB.
- Preserve existing unrelated changes in development/upload files.
- No state-changing Git, branches, worktrees, direct engine edits, or translation command.
- Batch C++ changes and use only `npm run rebuild` for compile confirmation. Native RED/GREEN compile cycles are consolidated to honor the repository's required build/cache workflow.

## Review Focus

- Switching ordinary tabs, focusing another app, or closing the original approval window must not cancel independent preview.
- CLI disconnect after browser commit must not create duplicate publication on recovery.
- Changed local bytes, stale bases, or cancelled preview authorization must never publish a different draft.
- A malicious workspace path/link must never cause reads/deletes outside the CLI-owned temporary directory.
- An MCP-disabled browser must still preview a signed-in fixture in the exact Profile without returning cookie bytes.

## Shared interfaces

Local IPC lives under `src/dao/browser/local_ipc/`. Generic class names are
`DaoLocalIpcClient`, `DaoLocalIpcTransport`, `DaoLocalIpcRuntimeFiles`, and
`DaoLocalIpcRequest`. `DaoLocalIpcConfig::Home()` selects directory `HomeCLI`,
socket `home.sock`, protocol version 1 and a 32-MiB message limit. Default config
retains existing MCP directory/socket/8-MiB behavior. Keep old MCP headers as
compatibility aliases/wrappers; the Home service includes only neutral headers.

The wire envelope is `{version,id,method,params}`; native transport responses use
`{version,id,result}`. Home results are objects with `ok`; errors have `ok:false`
and `error:{code,message}`. Request arguments contain `token` after session open.

| Method | Arguments beyond token | Successful result fields |
| --- | --- | --- |
| `hello` | transport nonce, client name/version | `ok`, protocol information |
| `session.open` | `reason`, optional `profile` | `token`, `profile_id`, `expires_at_ms` |
| `session.status` | optional `operation_id` | current revision, draft, preview state, operation outcome |
| `session.close` | none | `ok` |
| `project.snapshot` | none | `revision`, `files` (relative path to base64 bytes), `digest` |
| `draft.stage` | `base_revision`, `files`, `digest`, optional `summary` | `draft_id`, `base_revision`, `digest` |
| `draft.preview` | `draft_id` | `render`, `sources`, `publishable`, `preview_window_id` |
| `draft.publish` | `draft_id`, `base_revision`, `digest`, `operation_id`, `summary` | `revision`, `digest`, `operation_id` |
| `project.versions` | none | `versions` |
| `project.rollback` | `revision`, `base_revision`, `operation_id`, `summary` | `revision`, `operation_id` |
| `project.diagnostics` | none | `diagnostics` |
| `draft.sources` | optional `draft_id` | `sources` |
| `draft.test_source` | `draft_id`, `connector_id` | source status/shape |
| `preview.screenshot` | none | `mime_type`, base64 `data` |

Shared byte snapshots are `HomeFileSnapshot = std::map<std::string,std::string>`
in `home/dao_home_file_snapshot.h`. Helpers validate paths/size, encode/decode
base64 maps, and hash sorted length-prefixed path and byte values. Both CLI and
store use these helpers. Errors reuse `HomeError`; browser/CLI map stable errors.

Store/service additions are `GetFileSnapshot(revision)`,
`StageSnapshot(base_revision, files, summary)`, and durable operation provenance
for publication/rollback. The exact C++ result type for a snapshot may be the
map; the caller supplies its revision and computes its digest from returned bytes.
All store operations remain sequenced and service methods asynchronous.

## Task 1: Neutral authenticated local IPC

**Files:** `src/dao/browser/local_ipc/*`; compatibility substrate files and BUILD under `src/dao/browser/mcp/`.

**Produces:** Generic transport/client/runtime API above with default-compatible MCP aliases.

- [x] Add focused protocol/config tests for default and Home limits, private endpoint identity, and malformed envelopes.
- [x] Extract the existing implementation without removing ownership, nonce, process/UID, queue, shutdown, or cancellation checks.
- [x] Parameterize runtime path, socket name, maximum frame size, and preserve default MCP behavior.
- [x] Keep Home dependencies free of MCP service/helper dispatch; report necessary build dependencies for integration.
- [x] Verify through the consolidated build and narrow native tests in Task 5.

## Task 2: Atomic Home snapshots and publication provenance

**Files:** `src/dao/browser/home/dao_home_file_snapshot.*`, `dao_home_project_{store,service}.*`, their native tests, Home type serialization as needed.

**Consumes:** Existing immutable store, manifest validation and mutation leases.
**Produces:** Shared snapshot codec/digest and store/service methods above, preserving grants.

- [x] Add tests for new/binary/removed files in one draft, invalid paths/links/limits, deterministic digests, stale bases, and lost-response operation recovery.
- [x] Implement complete atomic snapshot staging using existing validation/register-draft behavior, without import semantics.
- [x] Persist request provenance with successful publish/rollback and expose lookup; repeated operation IDs cannot change payload or create a second revision.
- [x] Keep local provenance out of imports and exported capabilities.
- [x] Verify through Task 5's consolidated build/native tests.

## Task 3: CLI temporary workspace and skill distribution

**Files:** `src/dao/browser/cli/*`; `dao_local_agent_integration.*`; `agent-plugins/*`; related distribution and CLI tests. Do not modify Home runtime files or shared native BUILD patches.

**Consumes:** Generic IPC client and shared Home snapshot codec; wire contract above.
**Produces:** The complete `dao-browser home` command surface from the design and third installed skill.

- [x] Add focused tests for fresh private temporary roots, no CWD writes, link/path substitution, dirty cleanup, binary snapshots, local file changes, and publication recovery.
- [x] Implement explicit-workspace commands and atomic private state with bounded capture; preserve local edits on error.
- [x] Connect to Home endpoint, launch the packaged Dao when needed, preserve target/session across short invocations, and produce structured JSON.
- [x] Add versioned Home guide and concise entry skill using skill-creator guidance; synchronize plugin copies and existing routing/install/update/remove behavior.
- [x] Run the distribution/available pure CLI tests; pass integration dependencies to the root implementer.

## Task 4: Browser Home sessions and dedicated real-runtime preview

**Files:** New Home external service/window/session files; `dao_home_ui.*`; Home Lit app/bridge/tests; native strings; bootstrap and UI BUILD patches.

**Consumes:** Tasks 1 and 2; existing Home source permissions, connector executor, and preview checks.
**Produces:** Independent typed IPC service and full session/runtime lifecycle.

- [x] Add browser/WebUI regression tests for dedicated retained preview, real Profile source collection, source authorization, and cancelled/late callbacks.
- [x] Implement authenticated independent endpoint and bounded one-hour sessions with native localized approval and Stop.
- [x] Create/reuse a visible normal browser window in the exact real Profile, omitted from session restore; do not adopt existing Home tabs.
- [x] Retain draft display after validation, scope permissions/events to its owner, cancel on owner loss, and keep ordinary Home documents isolated.
- [x] Stage/test/preview/publish and screenshot via existing native primitives with pinned leases, base revisions, digests, and operation provenance.
- [x] Wire source permission UI, native connector runner, structured diagnostics and status, cleanup and recovery.

## Task 5: Integration, verification, and user documentation

**Files:** Desktop feature inventory/checklist, en/zh-CN changelog, native BUILD integration/tests, fixes within owned feature files.

- [x] Review all agent changes and reconcile exact shared interfaces before building.
- [x] Update desktop feature inventory/checklist and matching Unreleased notes; preserve unrelated user edits.
- [x] Run relevant Vitest tests, `npm run lint:lit`, skill validation, and source/patch checks.
- [x] Run the normal import/rebuild workflow; diagnose and fix every feature-related compilation error using that same permitted path.
- [x] Run available focused native tests, including an MCP-disabled real-Profile preview/connector fixture and recovery/cancellation checks.
- [x] Use one independent final code review; fix material findings and rerun affected checks.
- [x] Record actual results and remaining platform/runtime limits without claiming unrun checks.

## Execution record

- 2026-11-12: Implementation authorized after design and dedicated-window clarification. Proceed in the primary checkout; no commit or worktree steps apply.
- Ruling: Execute the approved implementation request without another plan-approval round. Use bounded independent file scopes for parallel work and root-owned integration.
- Ruling: Follow repository batched C++ compile verification over per-test Chromium rebuilds; write native tests before corresponding implementation, then compile/run the batch.

- Verification: `npm run test:webui` with Python 3.12 on PATH passed 1,381 tests (7 skipped); the default Python 3.9 failed an unrelated existing Android release test because `hashlib.file_digest` requires Python 3.11 or newer. No Android source change was made.
- Verification: `npm run lint:lit` passed (233 files). The Home app/connector suite passed 32 tests after lifecycle/cache fixes.
- Verification: `npm run rebuild -- -- --target dao_browser_cli <affected native objects>` compiled the CLI, changed browser sources, and new native tests successfully. English/Chinese offline help and missing-reason usage checks passed on the generated CLI.
- Verification: `dao_home_store_unittests --gtest_filter=DaoHome*` passed all 47 snapshot, store, and workspace tests. IPC socket fixtures and browser runtime verification are recorded below.
- Review: Fixed staging without a summary, queued publication lease invalidation, retained source media, visibility readiness, permanent window ownership, failed-session cleanup, fresh explicit source tests, and published-to-preview transitions.

- 2026-11-13: Final review added generation-checked iframe readiness, moved Home endpoint startup after initial Profile loading, required `IsRegularProfile()`, and completed the ordinary Home editor/Stop surface before the first preview. Editor metadata changes do not rebuild the ordinary Home runtime.
- Verification: The final focused Home/connector/plugin resource suite passed 40 tests; both packaged Home skills passed `quick_validate.py`. Startup patch hunk counts match. The completed native verification is recorded below.

- Final build: `npm run rebuild -- -- --target dao_home_browser_tests dao_local_ipc_unittests` completed the full browser and focused tests (31m14.92s), followed by a successful 45-second incremental build for the final product changes and a 16.53-second build for fixture/guide corrections. No direct Chromium build command or manual engine edit was used.
- Final native results: all 47 snapshot/store/workspace tests and all 11 independent IPC tests passed. The 38 selected browser/service/skill-installer/legacy-transport cases passed across the initial run (35 passed) and focused rerun (3 passed). The three fixture failures were corrected by deferring application scripts until the injected runtime exists and waiting for the ordinary iframe's committed navigation. Product logic was unchanged by those fixture fixes.
- Verified end to end: real CLI pull/edit/preview/screenshot/publish/status/clean with MCP disabled; exact Profile/HttpOnly-cookie source collection without returning credentials/content; independent ordinary Home runtime and Profile-scoped Stop before preview; retained source media; stale readiness rejection; hidden/moved/late-error cancellation; committed operation recovery.
- Final static/distribution checks: Lit scanned 233 files with zero violations, `git diff --check` passed, both skill packages validated, and the rebuilt CLI emitted the updated embedded runtime guide. The guide resource tests passed after clarifying deferred script loading.
- Platform limit: compilation and native runtime tests ran on macOS arm64. Windows-specific runtime/ACL behavior was not executed on this host. No Git commit, branch, worktree, release, or installation into the user's agent directories was performed.


## Claude review follow-up (2026-11-13)

- Native owner activity now drives renderer lifecycle, preserving occluded preview windows and in-flight source collection while still cancelling hidden or backgrounded owners. Same-document hash/history navigation keeps the receipt; document reload, frame removal, and runtime errors invalidate it.
- Cleanup is local by default. `--close-session` explicitly ends the authorization shared by all checkouts. Stop produces `SESSION_REVOKED`, and neither resume nor pull-from renews revoked consent. Busy target Profiles are rejected before prompting; the picker shows Profile display names.
- Snapshot source requirements compare module/schema bytes, permissions, limits, and current grants. An unchanged unavailable source reports a warning only after a successful fallback render; changed sources must pass tests before publication. Render and source results remain separate, and external preview windows cannot approve internal Agent drafts.
- Summary validation shares a 500 UTF-8 byte limit and rejects oversized CLI requests before recording a pending operation. Supported host-platform filenames and static `dist`/`build` assets round-trip; development artifacts fail during checkout creation rather than after a successful pull.
- Unknown operations remain pending: `not_found` is not evidence of failure. Recovery guidance preserves the original checkout and obtains a fresh checkout for comparison and explicit reapplication. Unreferenced snapshots are pruned, source retests clear local preview receipts, and screenshot failure does not erase a successful preview result.
- The new unavailable-source test exposed an existing crash from destroying a detached WebContents during its navigation observer callback. Detachment now precedes deferred destruction; failure cleanup precedes callback reentry. Local deterministic network-failure and invalid-schema fixtures cover both warning and blocking publication paths.
- Verification: `npm run rebuild -- -- --target chrome dao_home_browser_tests dao_home_store_unittests` passed in 43.13 seconds, then passed in 16.92 seconds after the collector fix. The final Home native run passed all 23 cases; the earlier run also passed 25 unchanged installer/legacy-protocol cases, with its single navigation-crash failure resolved by the final run.
- Verification: all 52 snapshot/store/workspace unit tests, 10 real-CLI/isolated-IPC regressions (`python3 scripts/tests/test-home-cli.py engine/src/out/dao-debug/dao-browser`), and 55 focused Home/resource/plugin WebUI tests passed. Lit scanned 233 files with zero violations; both packaged Home skills validated, and whitespace checks passed.
- Verification limits remain macOS arm64 with isolated browser-test Profiles and a test consent hook. No everyday signed-in user Profile or Windows runtime was exercised. Unrelated development/upload changes remained byte-for-byte unchanged; no Git state-changing command was used.


## Installation guidance and recovery follow-up (2026-11-13)

- Failed project cleanup now keeps metadata and the exclusive lock until the command exits. A separate process verifies that a competing CLI cannot open the workspace during failed cleanup; after releasing the lock, the original checkout can be reopened and cleaned successfully.
- Resume reuses a still-live authorization, expiry, and preview instead of opening a competing session. It also resolves pending publication through the existing operation record without republishing; expired and revoked-session behavior remains unchanged.
- Settings > Local agent collaboration > Skills and command > How to use now explains Dao Home installation and provides Codex and Claude Code examples, with hand-authored English and Simplified Chinese strings. The installer test covers both client destinations, managed updates, removal, and preservation of custom skills.
- Regression evidence: all three new CLI cases failed against the prior binary (metadata removed before project cleanup failure; live resume and pending recovery rejected as HOME_BUSY). After rebuilding, all 13 actual-CLI/isolated-IPC cases passed. The first same-process lock assertion was corrected to use a separate process because macOS record locks are process-owned; all 53 native snapshot/store/workspace tests then passed.
- Compilation: `npm run rebuild -- -- --target chrome dao_home_browser_tests dao_home_store_unittests` passed in 1m48.39s; the final unit-test-only rebuild passed in 8.40s. All 73 focused WebUI/settings/resource/plugin tests passed.
- Real-browser verification: `MAC_CHROMIUM_TMPDIR=/tmp engine/src/out/dao-debug/dao_home_browser_tests --gtest_filter='DaoHome*' --test-launcher-jobs=1 --test-launcher-retry-limit=0` passed all 24 cases in 88 seconds. The actual CLI flow covered pull, edit, retained independent preview, screenshot, live resume, publication, pending-result recovery without an extra version, failed cleanup, status, and successful cleanup retry. Signed-in source collection used a real regular test Profile and a deterministic local HTTP fixture with an HttpOnly cookie, with MCP disabled.
- The settings-renderer case passed in the full run and separately with `--lang=zh-CN`; all four new message IDs have hand-authored Simplified Chinese translations. Feature inventory, checklist, CLI guide, plugin installation documentation, and both changelogs were updated. `git diff --check` passed, and unrelated development/upload files remained byte-for-byte unchanged.
- Logs: `/tmp/dao-home-followup-build.log`, `/tmp/dao-home-followup-unit-build.log`, `/tmp/dao-home-followup-cli.log`, `/tmp/dao-home-followup-unit-final.log`, `/tmp/dao-home-followup-webui.log`, `/tmp/dao-home-followup-native.log`, and `/tmp/dao-home-followup-settings-zh.log`.
- Scope: macOS arm64, isolated browser-test Profiles, and an automatic editing-consent test hook. Settings guidance was rendered by the real browser; skill installation was exercised through the native installer into a temporary home, without touching the user's installed agents. No everyday user Profile or Windows runtime was exercised, and no Git state-changing commands were used.
