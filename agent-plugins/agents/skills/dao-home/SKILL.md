---
name: dao-home
description: Create or maintain the user's Dao Home project, or handle an explicit dao-home request. An explicit dao-browser or dao-lite choice takes precedence; ordinary page browsing and background research use those skills.
---

# Dao Home

Run `dao-browser home guide` and follow the installed build's contract. Read it
again after Dao updates. This workflow uses Dao's independent Home connection;
it does not need MCP or the built-in Agent.

Pull a CLI-created temporary checkout and use only its returned absolute paths.
Never put Home source, state, assets, or artifacts in the current directory.
Read existing files before editing; preserve unrelated work. Use normal file
tools inside `project/` to create directly runnable HTML, CSS, and JavaScript.

Review `home diff`, then run `home preview` in the real Profile's dedicated Dao
window. Inspect both render and source results; request a screenshot when visual
verification helps. Do not navigate an ordinary Home tab for preview. Publish
within the user's requested task only after a valid preview, and report the
confirmed revision and any source limitations.

On stale revisions, pull a fresh temporary checkout with `--from` and reapply
the intended edits. On denial or revocation, stop. Resolve an unknown publication
through status; never clear its pending record or retry the original request.
If it remains unknown, preserve the old checkout and use `pull --from` to inspect
current source, then manually transfer only still-needed changes as described in
the guide. Keep failed or unpublished work and report its temporary path.
Ordinary `clean` removes only one checkout; use it for obsolete source checkouts
without ending their shared session. After confirmed task completion, clean the
final checkout with `--close-session` to end that session for all its checkouts.
System temporary cleanup can remove workspaces, so they are not backups.
