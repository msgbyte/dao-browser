# Dao Home external editing

This guide ships with the installed CLI. Use `dao-browser home guide --topic
runtime` for the runtime contract and `--topic sources` for connector details.
Home uses an independent authenticated local endpoint, even with MCP disabled.
No browser Agent or model call is involved.

## Workflow

1. `dao-browser home pull --reason "Describe the user's Home change"` asks for
   native approval for one fixed hour. Optional `--profile ID` selects a regular
   Profile; omit it to let Dao resolve the target. Client names are labels, not
   authenticated application identities.
2. Read the returned `project_dir`, edit only there, and retain `workspace_dir`.
   Both are absolute paths under the system temporary directory. Never write
   source, package files, state, screenshots, or other artifacts into CWD.
3. `dao-browser home diff --workspace /absolute/returned/workspace` works offline.
   It compares bytes, including binary assets and removals, against the base.
4. `dao-browser home preview --workspace /absolute/returned/workspace` stages all
   files together and runs the real Home app and authorized connectors in the
   exact Profile. Dao opens its own visible preview window. Keep its Home tab
   selected and the window unminimized. Focusing another app or ordinary window
   is fine. Inspect `render`, every `sources` result, and `publishable` separately.
   `--screenshot` explicitly captures that preview into private artifacts.
   Its `screenshot` result is independent: a failed capture does not erase a
   successful preview or its publication readiness.
5. `dao-browser home publish --workspace /absolute/returned/workspace --summary
   "Describe the user-visible change"` commits only the unchanged successfully
   previewed draft. Summaries must be nonempty UTF-8 text of at most 500 bytes.
   Report success only after Dao confirms `revision`.
6. At task completion, `dao-browser home clean --workspace
   /absolute/returned/workspace --close-session` ends the session and removes
   that checkout. It refuses unpublished changes; use
   `--discard` only when the user intends to discard them. Keep failed work and
   report its temporary path. The OS may clean temporary directories.

The first pull of an empty Home supplies an unpublished minimal HTML/CSS/JS
scaffold without connectors. Read existing files before editing. Keep the
manifest and entry file valid together. No npm/build pipeline is needed or
accepted. A project contains at most 500 files and 20 MiB of decoded bytes.
Development/editor artifacts such as `node_modules`, package manifests, lock
files, dotfiles, and editor temporary files are rejected before creating a
checkout and when scanning edits. Directly runnable assets in `dist/` or `build/`
are supported. Links, special files, and unsafe paths are rejected.

## Other commands

- `home status` checks availability without launching Dao. With `--workspace`,
  it reports the bound Profile, current/base revision, session and preview state,
  and resolves a pending publication when its durable outcome is known.
- `home diagnostics --workspace W` gives bounded preview diagnostics.
- `home sources --workspace W` stages the current local candidate when needed
  and lists its connectors and permission/test state without collecting content. `home sources test ID --workspace W`
  explicitly retests one connector in the staged candidate.
  Retesting invalidates the preview receipt; preview again before publishing.
- `home screenshot --workspace W` captures only the live authorized preview.
  Screenshots can contain signed-in content and remain under private artifacts.
- `home versions --workspace W` lists published versions and summaries.
- `home rollback REVISION --workspace W --summary TEXT` restores a known version
  as a new version. It never restores revoked grants or overwrites local files;
  pull fresh source before further publication.
- `home resume --workspace W --reason TEXT` reuses a valid session and its
  preview. After expiry/restart it explicitly requests a new session for the
  same Profile, preserving edits and discarding old preview receipts. In either
  case it resolves a pending publication when possible.
- `home pull --from W` reuses a valid approved target/session but always creates
  a fresh temporary checkout. Add `--reason TEXT` to explicitly request renewed
  approval after expiry. It is mutually exclusive with `--profile`. The original
  directory and any pending operation remain intact.
- `home clean --workspace W` removes only that local checkout and works offline.
  Use this when removing an older checkout after `pull --from`; it preserves
  the shared session. Add `--close-session` only when the task is finished: it
  ends the session for every checkout sharing it, requires a reachable browser,
  and preserves the directory if closing the session fails. If project cleanup
  fails, recovery metadata remains available so the same checkout can be retried.

All workspace paths must be the explicit absolute path from pull; there is no
CWD default, custom destination, arbitrary directory adoption, or force bypass.
`--client NAME`, `--user-data-dir PATH` and `--no-launch` work on Home commands.
User data root and Profile ID are distinct; existing checkouts cannot change
targets or the approved client label. Omit `--client` on later commands to reuse
the stored label. Human help is localized; stdout is exactly one JSON result for commands
other than guide/help. Exit codes: 0 success, 1 failure, 2 denied/expired/revoked,
3 unavailable transport/protocol, 64 usage. Machine error `code` is stable.

## Recovery and conflict handling

`FILES_CHANGED` requires a new preview. `STALE_REVISION` preserves local work:
pull with `--from`, read the current source, and reapply the intended changes;
never silently merge or overwrite the published head. Source denial/revocation
requires stopping, not trying another route. New/changed connectors must pass
permission, schema, and runtime checks. An unchanged unavailable source can be
a warning only when Dao explicitly returns `publishable: true`.

`OUTCOME_UNKNOWN` means publication may have committed. Keep the checkout, run
status (or resume after browser restart), and inspect the recovered operation.
`not_found` is not proof of failure: the commit may still be running or its
provenance may no longer be available. Never clear the pending record or retry
the original publication to guess its outcome. If status cannot resolve it,
`home pull --from W` can safely read the current published files into a new
checkout while preserving W. Inspect the current source and the old diff, then
manually transfer only still-needed changes for a new preview and publication;
do not blindly republish the old candidate. Keep the original directory and
report the unresolved operation. `source_operation_unresolved: true` on the new
pull means its source checkout still has an unknown result; it does not mean
the old publication failed. Stop on denied or revoked consent; `resume` and
`pull --from` do not reacquire a revoked session.

A successful result with
`local_recovery_needed: true` is already committed; recover local metadata via
status. Publication advances the base without overwriting files edited while it
was in flight. Rollback requires a fresh pull and does not overwrite local work.
Approval waits last up to 60 seconds. Operation waits are bounded to 330 seconds;
a connection loss cancels unfinished runtime work but cannot undo a commit.
