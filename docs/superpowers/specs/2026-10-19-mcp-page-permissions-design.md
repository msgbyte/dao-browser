# MCP Page Permissions

## Intent

Window approval lets an MCP connection create and control its own new tabs in
that window. It must not grant access to existing tabs. Existing tabs require
an additional native approval. Users can revoke individual tabs without closing
them or stopping unrelated work.

This records the scope approved in conversation on 2026-10-19. Implementation
is authorized; Git mutations, commits, pushing, and Computer Use are not.

## Permission model

- A connection has one approved normal browser window and regular profile.
- Window approval grants tab creation, independently of any current page.
  Initial authorization works while the active tab is an internal page.
- Tabs successfully created by this connection are granted to this connection.
- Existing tabs, including another connection's tabs and previously revoked
  tabs, need an explicit page-access request and native user approval.
- An initial explicit `tab_id` can retain the existing narrow page approval
  entry point. It does not implicitly grant window creation permission.
- Every tab grant is explicit and connection-scoped. A tab's origin or ownership
  history never bypasses a revoked grant.
- Existing external-target eligibility and exclusive per-tab automation leases
  remain enforced. Grants do not override another controller's lease.
- Listing tabs exposes only granted, eligible tabs. Page and tab operations
  consistently check the same grants before reading or mutating anything.
- Explicit tab requests stay pinned to the selected tab and approved window.
  Missing, moved, destroyed, forbidden, or changed pending targets fail closed;
  no active-tab or other-tab fallback can grant access.

## Request and approval flow

Provide an explicit MCP page-access request. It accepts a reason and optionally
a tab ID supplied by the user. A request without an ID lets the user select an
eligible page in native browser UI without exposing unapproved page metadata to
the client. The resulting grant identifies exactly the approved tab.

Window and page approval prompts explain their distinct scopes. Requests remain
serialized, bounded, cancelable, and fail closed on timeout or owner loss. A page
request denial must not revoke existing grants or the window creation grant.
Requests queued before approval cannot adopt a different page after approval.

## Revocation and lifecycle

- Revoke a tab by explicit connection and tab identity. Cancel its pending and
  active operations, stop capture/monitoring, detach DevTools, and release the
  lease and page UI state immediately. Keep the browser tab open.
  Cancellation does not roll back actions already dispatched to the renderer,
  including a close whose confirmation has not appeared yet.
- Revoking or losing the default target clears default routing; callers must
  choose another granted tab or create a new one. Do not silently reroute work.
- Revoking the final tab preserves an existing window creation grant. A page-only
  connection can request another page but cannot create a tab without window
  permission.
- Stopping a connection cancels its requests and revokes every grant. Closing its
  window or destroying its profile ends that connection, including when it has
  no page grants. Disconnect and disabling MCP retain existing cleanup behavior.

## User interface and helper behavior

Show each approved connection, its window scope, and granted pages with per-page
revoke and connection stop actions. Actions use explicit identities, never a
fresh global focus lookup. Counts and client details refer to the same connection.
Permission management remains reachable when no page is currently controlled.
Reuse existing native controls, dialogs, styling, and localization infrastructure.

New MCP tabs open in the background and become that connection's default target
without changing the user's active tab. An explicit `switch_tab` may continue to
activate an already granted page. The helper tells research clients to create a
tab, and to request existing-page access only when needed.

## Scope limits

Grants continue following a tab through eligible navigation after approval.
New tabs share the normal profile's login state. Read-only permissions,
cross-origin reapproval, idle expiry, separate profiles, and automatic tab closing
are outside this change.

## Delivery and verification

Keep implementation in tracked Dao-owned sources. Do not edit `engine/` directly,
create branches/worktrees, run Git mutations, force import, or paid translation.
Use only `npm run rebuild` for compilation. Add focused permission regression
tests, run available relevant tests, and distinguish unbuilt tests from executed
tests. Update `docs/mcp-server.md`, desktop feature inventory, shared feature
checklist, and matching English/Simplified Chinese desktop Unreleased notes.

Review authorization races, queued cancellations, target replacement, zero-page
connections, multiple clients/windows, and shared Agent tool compatibility.
