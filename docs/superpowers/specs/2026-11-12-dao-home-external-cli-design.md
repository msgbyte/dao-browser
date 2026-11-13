# Dao Home External Agent Skill and CLI — Design

Date: 2026-11-12
Status: Proposed for design review; no product implementation in this change.

## Intent and confirmed requirements

Let an external coding agent create and maintain the user's Dao Home through a
`dao-home` skill and the existing `dao-browser` executable's `home` subcommand.
The external agent supplies the reasoning and edits. Dao owns the project,
runtime, permissions, immutable revisions, and publication.

The user chose the recommended local-file workflow and added these requirements:

1. Home source editing must happen in a CLI-created system temporary directory.
   Never place the checkout, metadata, assets, or generated artifacts in the
   agent's current working directory.
2. `preview` must run in the actual Dao Home runtime of the target browser
   Profile, including authorized collection from websites using that Profile's
   existing signed-in state. A localhost server or ordinary browser preview
   cannot substitute for this validation.
3. Preview opens a dedicated, visible Dao window using that same real Profile.
   It must not reuse or navigate the user's ordinary Home tab or browsing window.

MCP is excluded from this path: no MCP configuration, enabled-server requirement,
stdio helper, tool discovery, or `tools/call`. The existing Dao Agent is not
started and no browser-side model call is needed.

## Approaches

| Approach | Benefits | Costs and disposition |
| --- | --- | --- |
| Temporary source checkout plus native preview/publication | Uses the external agent's existing file tools; related changes form one draft; editing can continue offline | Requires checkout identity and conflict detection; recommended and accepted |
| Individual read/patch commands | Small edits need no full checkout | Makes the CLI an additional editing tool vocabulary; not the primary workflow |
| Edit Profile storage directly | Superficially little transport work | Bypasses the store's transactions, permissions, and runtime validation; rejected |

Use file editing for source and typed CLI commands for browser operations.
Read-only status, version listing, and diagnostics do not need a fresh checkout.

## Scope and design decisions

The first implementation covers creation, editing, real runtime preview,
connector permissions/testing, publication, diagnostics, screenshots on request,
version listing, and rollback. It targets desktop macOS and Windows, matching the
packaged CLI. Android and iOS are outside this design.

The following are design recommendations, not additional user requirements:

- Short-lived CLI processes reconnect to a browser-owned, time-limited Home
  editing session; no agent-managed daemon or persistent shell is necessary.
- One external editing session per Profile owns preview/publication at a time.
  It does not lock ordinary browsing or hold an editing lock while local files
  are being changed.
- Runtime operations use the Home document in the dedicated preview window.
  The user can switch tabs/windows in ordinary browsing windows or focus the
  external agent without interrupting preview. Offline editing needs no window.
- No automatic publication, file watching, merge algorithm, package manager,
  frontend build pipeline, or unrestricted browser scripting is introduced.
- History-driven first-time generation and CLI reset/import/export are deferred.
  Existing Home projects created from history remain editable. This does not
  expose browsing history through the external session.

## User and agent workflow

All commands below are proposed interfaces, not currently shipped commands.

```sh
dao-browser home guide
dao-browser home pull --reason "Update my Dao Home navigation"
```

After browser-owned approval, `pull` returns JSON including absolute
`workspace_dir`, `project_dir`, `base_revision`, and the session expiry. For
example, the directories may be:

```text
<system-temp>/dao-home-<random>/
  project/
    manifest.json
    index.html
    src/
    connectors/
    schemas/
    assets/
  .dao-home/
    state.json
    base/
    artifacts/
```

The agent edits only the returned `project_dir` with its existing file tools.
An empty Home produces an unpublished minimal valid project and an empty base
revision. The scaffold contains local HTML, CSS, and JavaScript and no connector.
It is a starting point, not a new published Home.

```sh
dao-browser home diff --workspace <workspace_dir>
dao-browser home preview --workspace <workspace_dir>
dao-browser home publish --workspace <workspace_dir> --summary "Update navigation"
```

`preview` creates and tests an immutable draft in a dedicated window using the
real Profile. It returns the preview window identity along with the draft result.
`publish` publishes only that exact successful preview and reports its resulting
revision. Files changed since preview require another preview. The skill reports
completion only after publication is confirmed.

### Command surface

| Command | Contract |
| --- | --- |
| `home guide [--topic runtime\|sources]` | Print the versioned agent guide or a focused runtime/connector reference; works without a running browser |
| `home pull [--profile ID] [--from WORKSPACE] --reason TEXT` | Create a new private temporary checkout; `--from` reuses that checkout's authorized target/session when valid and always creates a new directory |
| `home status [--workspace WORKSPACE]` | Report reachability; with a workspace, report exact Profile, preview window, base/current revision, session, draft, preview, and last publication outcome |
| `home diff --workspace WORKSPACE` | Compare project bytes with the immutable local base; list additions, modifications, and removals; works offline |
| `home preview --workspace WORKSPACE [--screenshot]` | Stage the complete candidate atomically, open/reuse this session's dedicated preview window in the real Profile, obtain source permission, test connectors, and validate the real Home runtime |
| `home publish --workspace WORKSPACE --summary TEXT` | Publish the unchanged previewed candidate against its exact base revision |
| `home diagnostics --workspace WORKSPACE` | Return bounded, content-minimized diagnostics for the session's exact draft/revision |
| `home sources --workspace WORKSPACE` | List the candidate's declared connectors, permission state, and latest test states without collecting content |
| `home sources test ID --workspace WORKSPACE` | Retest one connector in the already staged candidate using the same native runner and permission rules |
| `home screenshot --workspace WORKSPACE` | Capture the current authorized Home preview into the workspace artifact directory; requires a live preview display |
| `home versions --workspace WORKSPACE` | List published revisions and summaries in the bound Profile |
| `home rollback REVISION --workspace WORKSPACE --summary TEXT` | Restore a known revision as a new revision under current authorization; preserve existing rollback grant semantics |
| `home resume --workspace WORKSPACE --reason TEXT` | Explicitly request a new session for the same Profile after expiry/restart; retain local edits and invalidate old preview receipts |
| `home clean --workspace WORKSPACE [--discard] [--close-session]` | Remove only this verified CLI-created temporary workspace; refuse unpublished local changes unless `--discard` is present. Only `--close-session` ends the session shared by its checkouts |

The existing executable's `--user-data-dir`, `--client`, and `--no-launch`
options also apply. `--user-data-dir` selects the browser installation's user
data root; `--profile` selects a regular Profile within it. They are distinct.
Commands on an existing workspace cannot override its target. Selecting another
Profile requires a fresh pull. `--profile` and `--from` are mutually exclusive.
`pull --from` may omit `--reason` only while reusing a valid approved session.

There is no checkout destination argument, `--output-dir`, current-directory
default, or arbitrary-directory adoption. Each workspace command requires the
explicit absolute path returned by `pull`; `preview .` is not supported.
There is no `--force` path around stale revisions, permissions, or preview.

## Temporary workspace contract

- The CLI creates an unpredictable child of the OS temporary directory with
  owner-only access: mode 0700 on macOS and a current-user ACL on Windows.
  Metadata and credentials have equivalent private access. Failure to obtain a
  safe system temporary location is an error, never a fallback to the CWD.
- `project/` is the complete candidate. `.dao-home/` contains CLI-owned state,
  a byte-exact base copy, and artifacts. Metadata, credentials, screenshots,
  diagnostics, editor files, and build caches are never uploaded as project
  content. The scan rejects unexpected development artifacts in `project/`
  rather than silently dropping source files.
- State records the canonical workspace identity, target installation/Profile,
  base revision, base file hashes, current candidate digest, draft/receipt IDs,
  and last operation ID. Session credentials are private and never printed in
  JSON, examples, logs, screenshots, or project files.
- Resolve and validate every path. Reject path traversal, symlinked files or
  directories, Windows junctions/reparse points, duplicate normalized paths,
  special files, and workspace identity mismatches. Never recursively delete a
  caller-supplied directory just because it has a `dao-home-` name.
- Read bytes into a bounded snapshot before sending them; validate that files
  did not change during capture. The browser independently checks paths, sizes,
  and manifest references. Reuse the current 500-file/20-MiB decoded project
  limits. Binary assets retain their bytes.
- Two commands cannot mutate one workspace's CLI state concurrently. A short
  local lock covers capture and metadata transitions; the external editor is
  not held under a long filesystem lock. Atomic metadata writes and digest
  checks detect intervening edits.
- Successful publication advances the local base to the published bytes, but
  never overwrites `project/`. Edits made while publication was in flight remain
  local changes against that new base. A local state-update failure after
  publication is reported as committed-with-local-recovery-needed, not as a
  failed browser publication.
- Failed preview, agent exit, and browser restart preserve local source files.
  Explicit cleanup removes only the owned directory; `--close-session` also
  ends its shared editing session, including checkouts created with `pull --from`.
  OS temporary-file cleanup may also remove it, so this is not a durable backup. The skill does not promise
  recovery after the OS has deleted the directory. There is no silent cleanup
  of another session's files or unpublished edits.

## Native connection and session

### Independent Home endpoint

Add a narrow Home IPC service in the browser process, separate from
`DaoMcpService`. Use private local transport: a Unix-domain socket on macOS and
a loopback endpoint with same-user/server-process verification on Windows,
following the security mechanisms already used by Dao's native helpers.

The endpoint has its own private runtime metadata, per-launch nonce, protocol
version, bounded messages, cancellation, and lifecycle. It is available while
the normal browser is running, regardless of the MCP preference. Before Home
approval it exposes only handshake, availability, and session-request methods.
Discovery does not return projects, Profile contents, or tabs.

Reuse low-level local-transport/identity primitives through a small neutral
helper where useful. Do not route Home calls through MCP or undertake a broad
automation refactor. Existing MCP commands retain their behavior. The Home
wire protocol uses explicitly enumerated operations such as `session.open`,
`project.snapshot`, `draft.stage`, `draft.preview`, and `draft.publish`; it has
no generic tool dispatch or arbitrary JavaScript evaluation.

Use a bounded JSON envelope with base64 file bytes; a 32-MiB frame ceiling
accommodates the existing 20-MiB decoded project limit. Bound aggregate ingress
and concurrent requests as well as each frame. Version mismatch fails before
project access. The browser only receives project bytes, never a local path to
open on the agent's behalf. Output files are written by the CLI.

### Home editing consent

The first `pull` requests native approval in a normal Dao window. The prompt
identifies the self-reported client, task reason, chosen Profile, and the scope:
read Home source, preview changes with that Profile's authorized sources, and
publish/restore Home versions. Client names are labels, not authenticated app
identities. No cookies, credentials, general browsing history, or unrelated tabs
are exposed by this consent.

If the target is ambiguous, use a browser-owned Profile picker. Anchor approval
in an appropriate normal window, creating one for approval if that Profile has
none. That window is only the consent surface; it is never the preview target.
Return only the selected identity after approval. No last-active-Profile fallback
is allowed once the session is bound.

Approval creates an opaque bearer capability bound to the Profile, valid for
one fixed hour across short CLI connections. There is no permanent approval in
v1. Merely reconnecting or activity does not renew it. The native Home menu and
the dedicated preview window expose the active external editor and a Stop action
even when the MCP server is disabled. Stop, Profile loss, browser shutdown, and
expiry invalidate the capability, pending requests, and preview receipts.
Closing the original consent window does not revoke a session while its Profile
remains available. Closing the preview window invalidates that preview, not the
editing capability or temporary checkout.

Source access still requires the existing distinct Home source permission.
Ordinary authorized publication does not add a redundant confirmation prompt.
An expired or revoked session is never silently recreated; `resume` is an
explicit new request, and the skill stops on denial/revocation.

### Dedicated preview window and document ownership

Reading source and editing temporary files do not require an open Home page.
The first `preview` creates and shows a dedicated normal Dao browser window
containing a Home tab, backed by the exact approved regular Profile. Use the
normal window type so existing Home ownership checks still apply. The window
has a localized preview title/status and is omitted from session restore; it
must not trigger restoration of other browser windows. Do not create a separate
Profile, incognito context, hidden Agent Space, or Dao Agent turn.

There is at most one preview window per external editing session. Subsequent
`preview` calls replace its draft in place while it remains an intact owned
window. Never find an arbitrary existing Home tab by URL and adopt it. Show the
window on creation; ordinary status calls and completed background work do not
repeatedly activate it. Source approval dialogs are displayed in this window.

Runtime operations use `DaoHomeMutationLease` derived from the exact preview
document and external editing session. The Home tab must remain selected in its
own window, but its window need not have OS focus. Switching tabs in another
window, focusing another application, or macOS occlusion leaves it valid.
Navigating or closing the preview document, selecting a different tab in the
preview window, or truly hiding/minimizing it cancels runtime work and invalidates
the receipt under the existing visibility rules. Restoring a cancelled window
does not revive the receipt; a fresh explicit preview is required.

If the user closes the preview window, the next explicit `preview` can create a
replacement using the same still-authorized Profile. If the user repurposes that
window by navigating its Home tab or opening unrelated tabs, cease ownership
and leave those pages alone; a fresh preview gets a new dedicated window.
`publish` requires the original successful preview's still-valid document and
receipt. It cannot recreate a window or adopt another Home to rescue a receipt.

Before publication, ordinary Home windows continue displaying the published
revision. Draft selection, preview rendering, permission prompts, diagnostic
events, and screenshot targeting are bound to the dedicated window/session;
Profile-level project observers must not switch other Home documents to the
draft or broadcast its permission prompt there. The ordinary Home's runtime
state, connector executions, and media caches remain independent. The preview
shares the Profile's site storage and native connector capability, not another
Home document's transient application state.

After publication, ordinary Home windows observe the new committed revision
through the existing project service. Keep the dedicated window open showing
the published result with an appropriate trusted status; the consumed preview
receipt is cleared. A later preview can reuse this still-owned window. Stop,
expiry, or `clean` closes only the dedicated window while it still contains only
the session-owned Home page, cancelling its work first. Never close ordinary
windows or pages the user added. These events do not undo a committed version.

Short CLI disconnection after a completed preview does not end the browser's
editing session or its document authorization. Disconnect during an unfinished
read/preview/test cancels that operation. Publication may already have committed
when a response is lost; the recovery contract below handles this case.

The preview/connector runner is exclusive per Home owner. Serialize shared
project mutation/permission operations with a concurrent Dao Agent turn, another
external preview, reset, import, or rollback; return a stable busy or conflict
result instead of replacing callbacks or cancelling unrelated work. Ordinary
browsing, published Home collection in other windows, and local editing remain
possible. Base-revision checks at staging and commit provide the final
concurrency boundary.

## Real Dao preview and website access

`preview` must exercise this path:

```text
External agent's temporary files
  -> dao-browser home preview
  -> independent Home IPC / exact Profile session
  -> DaoHomeProjectService: immutable draft
  -> dedicated visible Dao window in that same real Profile
  -> trusted dao://home host owned by this preview session
  -> chrome-untrusted://dao-home-app: actual generated runtime
  -> declared connector sandbox + DaoHomeConnectorExecutor
  -> detached source WebContents in the same regular Profile
```

The connector's source page uses the real Profile's cookies and signed-in state.
The generated app remains sandboxed and uses `dao.sources.collect`; it does not
receive cookie values, browser APIs, unrestricted network access, or trusted
WebUI bindings. Existing origin/path/capability checks, schema and result limits,
sensitive-control rejection, login detection, and cancellation remain enforced.
There is no clean temporary Profile, local HTTP server, or mocked connector
substitution in successful preview validation.

The preview sequence is:

1. Capture the complete local candidate, compute its digest, and stage it
   against the exact base revision. The store validates all related text and
   binary changes atomically, including additions and removals.
2. Create/reuse the session's dedicated preview window, bind its Home document,
   inspect connector/grant differences, and show native source permission UI
   there for new or expanded access. CLI arguments cannot approve it. Permission
   must bind to the exact staged draft and budgets.
3. Test new or changed connectors through the real executor. A test returns
   bounded schema/shape, counts, and status, following existing content
   minimization; it does not save collected source values into the checkout.
4. Load the same draft in the actual generated Home runtime. Authorized
   `dao.sources.collect` requests from its code execute against real signed-in
   sites. Validate the exact frame, load, runtime errors, and applicable Home
   semantics using the existing native preview path.
5. Return separate render and source results. A genuine empty result is a
   successful empty state; missing permission, required sign-in, schema failure,
   timeout, and execution error are distinct. Do not label all sources working
   just because HTML loaded.
6. Issue a browser-owned receipt tied to session, preview window/document, draft,
   content digest, base revision, and connector test/permission fingerprints.
   Keep the preview display available for visual inspection until replaced,
   published, cancelled, or expired. Source collection is user-operation driven;
   do not introduce polling or background refresh.

New or changed connectors must pass required permission and schema/runtime
checks before publication. An unchanged source that has become unavailable can
remain a reported warning if the page handles it and existing publication rules
allow it; it must not be reported as successfully tested. The response explicitly
includes `publishable` and reasons. There is no bypass flag: correct the draft,
restore sign-in, or remove the failing new/changed connector and preview again.

Preview needs a persistent browser-owned display lifetime distinct from the
bounded initial load-validation callback. The current callback clears its
temporary preview on completion, so this is an explicit adaptation of
`DaoHomeUIHandler`, not an assumption about its current behavior. Runtime and
connector errors arriving while the preview remains displayed update diagnostics
and invalidate a receipt when they fail its publication requirements.

Screenshots are explicit (`--screenshot` or `home screenshot`), scoped to the
authorized preview, and written under `.dao-home/artifacts/`. They can include
visible signed-in source content; the initial consent explains this visual
access. Raw connector payloads, cookies, and source-page screenshots are not
exported or persisted. A displayed preview is distinguishable from the published
Home through trusted, localized UI in its dedicated window with a way to close
the preview. Screenshots never capture another window's published Home or any
unrelated browser content.

## Drafts, conflicts, and publication

Add a service/store operation that stages a complete bounded project snapshot
as one draft. Reuse `ValidateProject`, immutable revision storage, grant
comparison, and commit guards. `ReplaceFiles` only covers existing text files;
calling it repeatedly, or using project import, cannot implement this contract.
Import carries different history and grant-reset semantics and is not reused.

The project digest is SHA-256 over sorted normalized relative paths with
unambiguous length-prefixed path and byte data. CLI and browser compute it
independently. File removals are explicit in the resulting change summary.
State files and artifact directories are outside this digest.

`publish` re-captures local files and rejects a mismatch with the preview digest.
It publishes the browser's immutable draft, not bytes reread from disk during
commit. The store checks the base revision and live authorization while holding
its final commit guard. New permissions and the published revision are committed
together through the existing approved-draft path. Rollback restores source as
a new revision, never a revoked grant; it invalidates outstanding draft receipts.

When the published base has changed, return `STALE_REVISION` with expected and
current revision IDs. Preserve all local work. The skill uses
`pull --from <old-workspace>` to obtain another temporary checkout and reapplies
the intended changes against current source; it never overwrites the published
head or silently merges. The old directory remains available until explicit
cleanup.

Publication and rollback use a request ID recorded before dispatch. Record its
successful result atomically with the published version as browser-local
operation provenance, excluding capabilities. Repeating the same operation ID
and same request returns that result; a changed payload is rejected. The status
path can resolve the operation after CLI failure, browser restart, or a lost
response following renewed authorization to the same Profile. Provenance is not
imported from project packages. If no definitive result can be established,
report an unknown outcome and never automatically submit a new publication.
A `not_found` status is not proof that retry is safe. Preserve the pending
workspace; use `pull --from` to compare a fresh checkout with its edited files
and reapply only still-needed changes after inspecting the current revision.

`publish --summary` records a final user-readable version summary of at most
500 UTF-8 bytes without
changing the previewed project bytes. After confirmed success the CLI updates
the base copy and clears the pending draft. Rollback does not overwrite local
edits: return the new browser revision and require a fresh temporary pull before
further publication from an older base.

## Results and errors

Except for `guide` and help, stdout contains exactly one JSON result. Progress
and localized human notices go to stderr. Both success and failure have stable
machine-readable fields, with details sufficient for deterministic recovery:

```json
{
  "ok": true,
  "command": "home.preview",
  "workspace_dir": "<absolute-system-temp-path>",
  "base_revision": "<revision>",
  "draft_id": "<draft>",
  "preview_window_id": "<window>",
  "content_digest": "<sha256>",
  "render": {"status": "passed"},
  "sources": [{"id": "news", "status": "passed", "empty": false}],
  "publishable": true,
  "artifacts": []
}
```

Relevant error codes include `WORKSPACE_INVALID`, `WORKSPACE_BUSY`,
`FILES_CHANGED`, `INVALID_MANIFEST`, `QUOTA_EXCEEDED`, `STALE_REVISION`,
`PREVIEW_REQUIRED`, `PREVIEW_FAILED`, `HOME_NOT_ACTIVE`, `HOME_BUSY`,
`SOURCE_PERMISSION_REQUIRED`, `SOURCE_AUTH_REQUIRED`, `SESSION_EXPIRED`,
`SESSION_REVOKED`, `AUTHORIZATION_DENIED`, `BROWSER_UNAVAILABLE`,
`PROTOCOL_MISMATCH`, and `OUTCOME_UNKNOWN`. Use stable codes rather than parsing
localized messages. Reuse existing CLI exit meanings: 0 success, 1 operation
failure, 2 stopped/denied/revoked control, 3 unavailable transport, and 64 usage.
Detailed distinctions belong in JSON.

Bound source collection with existing executor limits. Approval waits have a
visible, cancellable native prompt and a 60-second timeout. Long preview work
uses bounded operation deadlines and cancellation; no busy-poll shell loop is
required. Expired, denied, or cancelled operations cannot produce valid receipts.
The implementation may select tighter compatible budgets, documented by `guide`.

## Skill, guide, and distribution

The installed `dao-home` entry skill is short and version-independent. Its
description selects it for explicit `dao-home` requests and for creating or
maintaining Dao Home. It instructs the external agent to:

1. Run `dao-browser home guide` and use the installed build's contract.
2. Pull into a CLI-created system temporary directory; use returned absolute
   paths and never write project material into the agent's current directory.
3. Read existing source before editing, preserve unrelated work, and use normal
   file tools only inside `project/`.
4. Generate directly runnable HTML/CSS/JavaScript with the real Home APIs and
   sandbox rules. Keep source collectors declared and permission-scoped.
5. Review the local diff, run real Dao preview in its dedicated window, inspect
   render/source results, and use explicit screenshots when visual verification
   is needed. Do not navigate an ordinary Home tab for preview.
6. Publish only within the user's requested task after a valid preview. Report
   the confirmed revision and any remaining source limitations.
7. On conflict, reapply to a fresh temporary checkout; on denial/revocation,
   stop. Recover unknown outcomes through status before retrying publication.
8. Clean only the owned temporary workspace after the task is complete; retain
   failed/unpublished work for recovery and tell the user its temporary path.

`home guide` is bundled with the executable, like the existing browser and Lite
guides, and does not contact a model or require an MCP endpoint. Its runtime
contract must remain aligned with the built-in Home contract: opaque origin,
no inline scripts, no Web Storage or direct network APIs, `dao.session`,
`dao.sources`, navigation/media APIs, manifest/schema rules, and design quality.
Share the runtime/design reference source with the internal Agent where
practical; keep transport-specific workflow instructions separate.

Extend existing Settings installation/update/removal and plugin distributions
to include the third entry skill, preserving custom user files and Windows
quoted executable fallbacks. Keep canonical entry content under
`src/dao/browser/cli/` and synchronized copies under both existing agent plugin
packages, with the existing distribution consistency test. No skill is installed
on the user's machine by this design-document change.

Explicit selection among `dao-home`, `dao-browser`, and `dao-lite` wins. Otherwise
Home project authoring routes to `dao-home`; unrelated page browsing and
background research retain their current skills. Update the other entry guides
so they do not redirect a Home task back into MCP.

New browser UI, native prompts, toasts, CLI help, and human error text require
localization. Reuse Dao string resources with English and hand-authored zh-CN;
CLI-side localized help must be available from packaged resources even offline.
Protocol identifiers, JSON keys, and developer/agent reference documentation
remain stable English. Do not run paid translation automatically.

## Implementation boundaries

Likely source owners:

- `src/dao/browser/cli/`: `home` dispatch, temporary checkout and atomic state,
  guide/entry embedding, structured output, and platform helper integration.
- `src/dao/browser/home/`: dedicated external session/IPC handler, complete
  snapshot staging, dedicated preview window ownership/lifecycle, durable
  publication recovery, and existing store/service integration. Do not make Home
  depend on the Agent WebUI's turn lifecycle.
- `src/dao/browser/ui/webui/dao_home_ui.*` and `resources/home/`: reusable
  preview/connector runner, retained preview display, owner-scoped source
  permissions/events, external-session stop control, and document leases.
- `src/dao/browser/mcp/dao_local_agent_integration.*`, plugin manifests/entry
  copies, and Settings patches: installation of the third skill. This file's
  existing location does not imply runtime dependence on MCP.
- Native bootstrap/build/resource patches and packaged strings: start/stop the
  independent service, include resources, and support both desktop platforms.

Leave exact class names, small shared-transport extraction details, and internal
test helpers to the implementer. Do not create a general remote-control
framework, additional executable, or new browser-wide scripting API.

## Acceptance and verification

The implementation is complete when an external agent can start with MCP
disabled and the Dao Agent sidebar closed, pull to a fresh system temporary
directory, edit a project, preview real signed-in source behavior in a dedicated
visible Dao window, and publish one confirmed revision without writing into its
CWD or taking over an ordinary browsing window.

Required checks include:

- CLI dispatch, JSON/exit codes, no CWD writes (including read-only CWD), correct
  temporary ownership/ACLs, workspace/path substitution rejection, binary
  round-trips, quota boundaries, and dirty-workspace cleanup protection.
- Complete atomic snapshot staging, file additions/removals, base conflicts,
  file changes during/after preview, no import grant reset, and durable
  publication/rollback recovery after a lost response.
- Browser tests with MCP disabled: independent endpoint, session approval and
  reconnection, exact Profile and dedicated-window binding, Stop/expiry,
  concurrent Agent/Home work, and cancellation between preview and final commit.
- A fixture website signed in through the target regular Profile: collection
  succeeds only with the expected session cookie, while cookie bytes never
  appear in CLI outputs; another Profile cannot supply its session. Test
  missing sign-in, source denial, scope expansion, revoked grants, schema
  failures, empty results, CSP failures, and full real-runtime rendering.
- Dedicated window creation/reuse and exclusion from session restore; ordinary
  Home pages keep their published revision and independent source state until
  publication. Permission prompts and screenshots stay in the preview window.
- Live-preview retention, late runtime errors, switching tabs in another window,
  closing the original approval window, focusing the external agent, and macOS
  occlusion must not invalidate a healthy preview. Closing/navigating/minimizing
  the preview itself must invalidate it; a new explicit preview can recover.
- Successful publication updates ordinary Home pages while preserving the
  dedicated result window. Stop, expiry, or `clean --close-session` closes only
  intact owned preview
  windows and preserves unrelated tabs or repurposed windows.
- Skill distribution consistency, routing precedence, version-matched guides,
  offline localized help, and installation independent of MCP enablement.

For implementation, batch source/resource/patch edits, synchronize using plain
`npm run import`, and confirm compilation only with `npm run rebuild`. Run
narrow native filters and relevant WebUI tests/lint; never invoke another build
path as compile confirmation. Do not create branches/worktrees or run
state-changing Git commands without explicit user authorization.

When implementing, update `docs/features.md`, `docs/feature-checklist.md`, and
matching desktop Unreleased entries in `docs/changelog/en.md` and `zh-CN.md`.
This design-only change does not mark any capability as implemented and does
not change the feature inventory or user changelog.
