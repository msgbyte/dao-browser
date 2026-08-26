# Dao MCP Multi-Client Tab Leases Design

**Date:** 2026-08-26

**Status:** Proposed

**Scope:** Multiple local MCP clients controlling disjoint tabs in one Dao
browser process

## Summary

Dao Browser will accept multiple local MCP connections at the same time. Each
connection keeps independent protocol, approval, browser authorization, tool
calls, tab contexts, and cleanup state. Connections may execute against
different tabs concurrently, but only one agent client may lease a given tab at
a time.

This extends the current background-tab work instead of replacing it. The
existing per-tab MCP target contexts remain the unit of Page and DevTools
execution. The transport and service stop treating all external clients as one
process-global connection, and the existing Agent lease manager changes from
one Profile-wide owner to owners keyed by Chromium `tabs::TabHandle`.

The implementation remains local-only, default-off, nonce-authenticated, and
explicitly approved per connection.

## Goals

- Allow multiple Codex or other local MCP terminal sessions to connect to the
  same Dao browser process.
- Let approved clients operate disjoint eligible tabs concurrently.
- Reject conflicting control of the same tab before page, DevTools, or tab
  mutation begins.
- Let Dao Agent use a different unleased tab while MCP is active.
- Keep approval, cancellation, byte limits, DevTools state, and cleanup isolated
  per connection and per tab.
- Preserve the current 29-tool catalog and optional MCP `tab_id` routing.
- Preserve current user-visible activation behavior for MCP `switch_tab` and
  `open_tab`.

## Non-Goals

- Multiple browser processes sharing one runtime endpoint.
- Remote, LAN, HTTP, or Streamable HTTP transport.
- Automatic assignment of tabs to clients.
- A new scheduler, queue, priority system, or background worker pool.
- Silent approval reuse across connections or reconnects.
- Allowing two agents to control the same tab concurrently.
- Adding a new `claim_tab` tool. Existing clients acquire an existing tab with
  `switch_tab` and acquire a new tab with `open_tab`.

## Alternatives Considered

### Keep one external connection

This preserves the current service shape but does not meet the requested use
case. One Codex helper continues to disconnect every later helper.

### Run one Dao browser process per Codex session

This avoids shared state, but splits cookies, tabs, windows, and user attention
across browser instances. It also works around rather than fixes the browser's
process-global admission gate.

### Use one browser-wide lease with a multi-client transport

This lets several helpers initialize, but only one can execute tools. It moves
the failure from connection time to tool time without enabling parallel work.

### Recommended: per-connection contexts and per-tab leases

This matches the existing tab-pinned automation sessions and isolates the
smallest resource that actually conflicts. It requires no new tool and no new
execution backend.

## Product Behavior

### Connection and approval

- The local listener accepts up to 32 authenticated MCP connections. The
  fixed cap bounds aggregate memory and file-descriptor use; it is not exposed
  as a setting.
- `initialize` and `tools/list` remain available before approval.
- Each connection's first `tools/call` independently snapshots the last-active
  eligible normal Browser and requests approval for that Browser and Profile.
- Approval binds the connection to one Browser and Profile, not exclusively to
  its initially active tab.
- Native approval dialogs are presented one at a time. Later approval requests
  wait in arrival order so dialogs cannot overlap or replace each other.
- Denial, timeout, or dialog destruction closes only that connection.
- An authenticated connection above the cap receives a terminal, retryable
  `LEASE_BUSY` result and closes. No new protocol error code is added.

### Tab ownership

The lease key is Chromium's stable `tabs::TabHandle`, which already survives
`WebContents` replacement in `DaoBrowserAutomationSession`. Externally visible
sidebar tab IDs continue to be used for tool arguments and results, but they are
not the internal ownership key.

The shared Profile lease manager stores one owner per live tab handle:

- An MCP owner is the service-generated connection ID, not the reported client
  name or PID.
- A Dao Agent owner is its active turn ID.
- Reacquiring a tab already owned by the same owner succeeds without creating a
  conflicting owner.
- A different owner receives retryable `LEASE_BUSY` before MCP execution.
- Dao Agent receives retryable `AGENT_CONTROL_BUSY` only when its requested tab
  is owned by MCP.
- Destroying the tab, connection, or Agent turn releases its affected leases.

The manager stays attached to the Profile and runs on the browser UI sequence.
Lease checks and tab mutations therefore have one serialized authority; no
additional mutex or worker scheduler is needed.

### Tool rules

| Operation | Lease behavior |
|---|---|
| `list_tabs` | Read-only. Requires connection approval but does not acquire a tab. |
| First Page or DevTools call | Acquires the selected initial tab if unowned; otherwise returns retryable `LEASE_BUSY`. |
| Page or DevTools call with `tab_id` | The ID must already belong to that connection's controlled target contexts. |
| Page or DevTools call without `tab_id` | Uses that connection's current target and requires that connection's lease. |
| `switch_tab` | Preflights the destination lease before activation. Success retains earlier MCP targets and makes the destination current. |
| `open_tab` | Creates and activates the new tab, leases it to the caller, retains earlier MCP targets, and makes it current. |
| `close_tab` | Rejects a tab leased by another owner. A caller-owned or unowned tab may be closed; any caller context for it is then removed. |
| Dao Agent turn start | Acquires only the turn's active tab. Another unleased tab remains usable while MCP controls a different tab. |
| Dao Agent `switch_tab` or `open_tab` | Reserves the destination before retargeting, then swaps the turn lease from the old tab to the new tab. |

Tab-tool conflict checks belong in the shared tab execution path so Dao Agent
and every MCP connection follow the same rule. Failed or cancelled tab
mutations do not retain a destination lease. An asynchronous `close_tab` holds
its reservation until the close completes or is cancelled.

MCP window mutations still run on Chromium's browser UI sequence. Concurrent
`switch_tab` or `open_tab` calls are applied in accepted order; the last
successful activation is visible to the user. Page and DevTools operations
against already controlled background tabs do not require activation and may
overlap across disjoint tabs.

## Browser-Side Architecture

### Transport

`DaoMcpTransport` replaces its single accepted `connection_` with a bounded map
of `DaoMcpConnection` instances. Every transport callback includes an opaque
connection token so reads, writes, close deadlines, and disconnect notification
address exactly one socket.

Each `DaoMcpConnection` keeps its existing ingress and write-backpressure
budgets. With a maximum of 32 connections, the aggregate bound is 32
times the existing per-connection bound. Disabling the service closes all
connections and removes the one process-owned runtime endpoint as before.

### Service

`DaoMcpService` keeps listener and Local State ownership process-global, but
moves current singleton connection fields into a `ConnectionContext` keyed by
the transport token. A context owns:

- hello generation, verified peer data, client metadata, and connection ID;
- approval state, timer, and authorized Browser/Profile;
- default target ID and the existing per-tab target contexts;
- pending and active calls plus their byte accounting;
- temporary tab-tool sessions;
- per-target executors, DevTools clients, lifecycle monitors, and leases.

Approval presentation is the only process-global queue. Tool dispatch, target
invalidation, cancellation, and rejection always take a connection token and
operate on that one context.

The existing per-tab `TargetContext` remains the execution boundary. A
connection may own several contexts, and each context owns exactly one tab
lease. A target loss removes only that target context and clears the default
target when necessary. While the authorized Browser/Profile remains, the
connection stays approved: Page and DevTools calls without a controlled target
return `TARGET_GONE`, while `list_tabs`, `switch_tab`, and `open_tab` can
establish another context. The connection closes only when its authorized
Browser/Profile is gone, the user stops it, or a terminal protocol failure
occurs.

### Shared lease manager

`DaoAgentLeaseManager` remains the single arbitration point shared by Dao Agent
and MCP. Its one `active_lease_id_`/`owner_` pair becomes a collection keyed by
`tabs::TabHandle`. The existing move-only RAII `DaoAgentLease` records the
target handle and releases only its own entry.

This is a change to the existing manager, not a parallel MCP-only registry. It
keeps peer-agent conflict policy in one place and prevents the two adapters from
drifting.

### Shared executor and tab tools

The shared executor must validate that the caller owns the resolved tab before
dispatching Page or DevTools work. The shared tab-tool path resolves and checks
the destination before `switch_tab` or `close_tab` mutates the browser.

The caller retains successful leases in its existing lifetime container:

- MCP retains them in per-tab `TargetContext` objects.
- Dao Agent retains one in the active turn and replaces it when the turn
  changes target.

No second CDP implementation, target registry, or MCP-specific tab tool is
introduced.

## Status and User Control

The process-level service status becomes an aggregate plus a list of connection
summaries:

- **Disabled** when the master switch is off.
- **Ready** when listening with no pending or approved clients.
- **Approval requested** when at least one client is pending and none is
  approved.
- **Connected** when one or more clients are approved, with a connected count.

Settings lists each approved client with sanitized reported name, verified PID
when available, controlled-tab count, and a per-client **Stop** action. Pending
clients remain represented by their native approval dialogs. Quick setup is
unchanged.

The address-bar robot indicator is visible only when the active tab is leased
by an MCP connection. Its details popup shows that tab's owning client and that
client's controlled-tab count. **Stop** closes only that owning connection and
does not disturb other MCP clients. Turning off the master switch remains the
explicit stop-all action.

All new or changed user-facing text is localized through Dao strings.

## Lifecycle and Failure Handling

- A helper disconnect cancels only its calls, clears only its overlays/input
  locks/DevTools state, releases only its tab leases, and removes only its
  connection context.
- A tab close or forbidden navigation invalidates only contexts pinned to that
  tab. Other tabs and connections continue.
- Browser or Profile destruction closes only connections authorized to that
  Browser/Profile.
- Stop from an active-tab indicator closes only that tab owner's connection.
- Stop from a Settings client row closes only the selected connection.
- Disabling MCP or browser shutdown closes every connection before removing
  runtime metadata.
- Connection IDs and tab leases are never transferred across reconnects.
- Closing a connection does not close tabs it opened; it only releases control.

Cleanup remains fail-closed. A context is erased only after its active calls
have been cancelled and its executors, lifecycle monitors, and leases have been
released. Re-entrant callbacks must re-find the connection token and target
context before continuing.

## Security and Resource Bounds

The existing owner-only Unix socket, peer UID check, runtime nonce, Local State
toggle, target eligibility policy, and per-connection approval remain
mandatory. Reported client metadata remains untrusted display data.

The connection cap, existing per-connection ingress limits, per-call deadlines,
and per-context DevTools/page budgets bound the new aggregate concurrency. A
client cannot route a Page or DevTools call to another connection's target
context even if it knows the public tab ID.

## Compatibility and Documentation

- The helper command, MCP protocol versions, 29-tool catalog, and schemas remain
  compatible.
- Existing single-client setups behave as before after approval.
- Existing optional non-tab `tab_id` behavior remains connection-scoped.
- `LEASE_BUSY` and `AGENT_CONTROL_BUSY` retain their protocol names but become
  tab-scoped.
- `docs/dao-mcp-server-design.md`, `docs/mcp-server.md`, `docs/features.md`, and
  `docs/feature-checklist.md` must be updated with the implementation. The new
  design supersedes their one-external-client and Profile-wide exclusive-lease
  statements.

## Verification Strategy

Implementation follows test-first, focused verification:

1. Extend lease unit tests to prove disjoint tab acquisition, same-tab
   rejection, same-owner reuse, move/reset cleanup, and Dao Agent/MCP error
   selection.
2. Extend transport/service browser tests to prove two helpers can initialize
   and list tools, approval is isolated and queued, and a ninth connection is
   rejected without affecting the first 32.
3. Add service browser tests for two approved clients executing against
   different tabs, same-tab `LEASE_BUSY`, per-client cancellation, target loss,
   Stop, and master-switch cleanup.
4. Add focused tab-tool tests for conflict preflight on switch/close, lease
   commit/rollback, and Dao Agent retargeting.
5. Extend Settings and address-bar tests for aggregate status, correct active-tab
   owner, and per-client Stop.
6. Run `npm run import`, the smallest relevant browser-test filters, and then
   `npm run rebuild` as the only compile confirmation command.

No full test sweep is required unless focused failures reveal cross-cutting
risk.
