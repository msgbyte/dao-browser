# Dao MCP Server Design

**Date:** 2026-07-23

**Status:** Approved design

**Scope:** Local MCP access to Dao Browser's existing browser automation tools

## Summary

Dao Browser will provide an MCP server that lets a local external agent control
the user's current Dao window. The service belongs to Dao Browser, not Dao
Agent. Dao Agent and external MCP agents are peer clients of one shared browser
automation layer.

The MCP server is disabled by default. The user must enable it through
**Settings → You and Dao → MCP Server**. Enabling the setting starts a private
local IPC listener. Each external connection still requires explicit, one-time
approval inside Dao. Approval ends when the connection closes, the user revokes
it, or the setting is disabled.

The first release exposes 29 Page, Tabs, and DevTools tools. It does not expose
memory, skills, soul, web search, or workspace tools. It also does not expose raw
CDP.

## Goals

- Let stdio-based local MCP clients use Dao's browser automation capabilities.
- Control the current normal Dao window with the user's existing browser state.
- Keep Dao Agent and external agents at the same architectural level.
- Use one tool catalog and one execution core for both clients.
- Require explicit user enablement and per-connection approval.
- Give one agent at a time an exclusive browser-control lease.
- Keep target window and tab selection deterministic throughout a connection.
- Preserve MCP-native text, structured, image, error, timeout, and cancellation
  behavior.

## Non-Goals

- Remote or LAN access.
- HTTP or Streamable HTTP transport.
- A raw CDP proxy.
- Headless or isolated browser profiles.
- Incognito, Guest, extension, DevTools, internal, or `file://` page control.
- Exposing Dao memory, skills, soul, web search, or workspace capabilities.
- Making Dao Agent a prerequisite for MCP.
- Persistent or "always allow" client authorization.

## Product Model

### Settings

Add an **MCP Server** row under **Settings → You and Dao**.

The row contains:

- A master toggle, off by default.
- A status value: Disabled, Ready, Approval requested, or Connected.
- The connected client's reported name and verified local PID when applicable.
- A **Copy MCP config** action.
- A **Stop control** action while a connection owns the lease.

Generated CLI commands and generic configuration bind the packaged helper to
the current browser process's user-data directory. This keeps Debug, release,
and custom-profile runtime endpoints isolated and prevents a client from
silently connecting to a different Dao installation.

All user-visible strings use Dao's internationalization system.

When the toggle is off:

- No local IPC listener exists.
- No runtime endpoint metadata exists.
- No MCP status is shown in browser windows.
- Any external connection and lease are revoked immediately.
- Dao Agent remains available.

When the toggle is on:

- Dao starts a private local IPC listener.
- Dao publishes process-owned endpoint metadata for the packaged helper.
- An idle "Ready" state is visible in Settings.
- A new external client may initialize, but it cannot call browser tools until
  the user approves that connection.

### Connection Approval

MCP initialization and catalog discovery do not display a connection approval
card or start the approval timeout. On the first tool call, Dao selects the then
current eligible target and displays the approval card. The card includes:

- The client-reported name and version, clearly marked as reported data.
- The verified helper PID when the platform credential API provides one.
- The Browser window and Profile that will be controlled.
- A warning that the agent can read and interact with websites using the
  current login state, including running JavaScript.
- **Allow this connection** and **Deny** actions.

There is no persistent approval option. Approval is valid only for the current
connection.

While an external connection is active, the authorized window displays a
lightweight status indicator containing the client name, current target tab, and
a **Stop** action. The user retains normal browser control. The exclusive lease
is between agents, not between an agent and the user. Existing per-operation tab
locking may still apply briefly when a tool requires it.

## Architecture

### Process Boundary

The MCP client launches a packaged `dao-mcp` helper over stdio.

The helper is intentionally thin. It owns:

- MCP initialization and capability negotiation.
- `tools/list` and `tools/call` request handling.
- JSON-RPC request IDs.
- Cancellation propagation.
- MCP content and error result formatting.
- stderr-only diagnostic logging.

The helper does not own:

- Browser authorization policy.
- A CDP connection.
- Browser, Profile, window, or tab selection.
- Tool implementation.
- Agent lease state.

The helper connects to the running Dao browser process through a private Unix
domain socket. The Dao browser process owns all browser capabilities.

### Browser-Side Components

Shared browser automation code lives under
`src/dao/browser/automation/`. MCP-specific service, authorization, lifecycle,
transport, protocol, and helper code lives under `src/dao/browser/mcp/`. This
physical boundary keeps Dao Agent and MCP as peer adapters over the same native
implementation without making the shared core MCP-owned.

#### MCP Connection Service

The browser-level connection service owns:

- Listener lifecycle tied to the Settings toggle.
- Client metadata and peer validation.
- The pending-approval state.
- The authorized Browser and Profile.
- The external connection lifecycle.
- Revocation and disconnect cleanup.

The service is independent of the Agent WebUI. It works when the Dao Agent
sidebar is closed, unloaded, or never created.

#### Agent Lease Manager

The lease manager arbitrates between peer agent clients:

- Dao Agent.
- One authorized external MCP connection.

Only one agent may hold the browser-control lease at a time. The lease does not
interrupt a tool call already executing.

When an external lease is active, Dao Agent may continue non-browser work and
chat, but browser tool calls fail with `AGENT_CONTROL_BUSY`.

When Dao Agent is completing a browser tool call, external authorization waits
for that call to finish before acquiring the lease. The transport admits only
one external socket; a second external connection is closed before MCP
initialization. `LEASE_BUSY` applies when the admitted external client cannot
acquire the shared automation lease.

#### Browser Tool Executor

The shared executor owns:

- Tool exposure checks.
- JSON Schema input validation.
- Lease validation.
- Authorized Browser and target tab resolution.
- Tool routing.
- Default deadlines.
- Cancellation and cleanup.
- Normalized success and error results.

Neither the executor nor the automation core knows whether the caller is the
Dao Agent adapter or the MCP adapter.

#### Browser Automation Core

The automation core contains the reusable browser operations currently spread
across `agent_bridge.ts` and `DaoAgentUIHandler`:

- `DevToolsAgentHost` attachment and CDP command dispatch.
- Page inspection and script execution.
- Accessibility-tree generation and ref-based interaction.
- Mouse, keyboard, typing, scrolling, cursor, and highlight behavior.
- Screenshot capture.
- Tab management.
- Network and console tracking.
- Page resource and response body inspection.
- Composite resource search.

The existing Agent WebUI handler becomes a thin adapter over this core. MCP must
not introduce a second CDP execution implementation.

## Tool Catalog

Create one declarative browser tool manifest containing:

- Tool name and description.
- Input JSON Schema.
- Optional output JSON Schema.
- Tool group.
- Side-effect classification.
- Default timeout.
- Client exposure.

The Dao Agent adapter and MCP adapter consume the same manifest. The helper gets
the exposed tool definitions from Dao rather than maintaining a duplicate list.

### MCP Tool Set

The first release exposes 29 tools.

Page tools:

1. `get_page_info`
2. `get_page_html`
3. `get_accessibility_tree`
4. `capture_screenshot`
5. `click_element`
6. `agent_click`
7. `click_by_ref`
8. `move_cursor`
9. `highlight_element`
10. `scroll_down`
11. `scroll_up`
12. `scroll_to_element`
13. `press_key_chord`
14. `type_text`
15. `execute_script`

Tab tools:

16. `list_tabs`
17. `switch_tab`
18. `open_tab`
19. `close_tab`

DevTools tools:

20. `enable_network_tracking`
21. `get_network_requests`
22. `clear_network_requests`
23. `get_network_body`
24. `enable_console_tracking`
25. `get_console_messages`
26. `clear_console_messages`
27. `list_page_resources`
28. `get_resource_content`
29. `search_in_resources`

`resolve_element_context` remains Dao Agent-only because it depends on element
context captured by the Agent composer. External MCP clients do not have that
context. All non-browser tool groups also remain unexposed.

### Composite Tools

Composite tools such as `search_in_resources` move from Agent TypeScript into
the shared executor. Both adapters must observe the same limits, ordering,
truncation, and error behavior.

## Session and Target Semantics

### Browser Binding

Approval binds the connection to the current normal Browser window and its
Profile. The binding does not follow later focus changes to another window.

The initial target is the active tab at the moment of approval. Page and
DevTools tools always operate on the session target.

`list_tabs` lists only tabs in the authorized Browser window. It never crosses a
window or Profile boundary.

### Tab Identity

Tab results retain the current `index` for compatibility and add Dao's stable
`tab_id`.

`switch_tab` and `close_tab` accept either value:

- `tab_id` takes precedence when both are provided.
- `index` is resolved only at the moment the call begins.

`switch_tab` and `open_tab` explicitly update the session target. If the target
is closed, the new active tab in the same authorized window becomes the target.

If the authorized window, Profile, or target disappears unexpectedly, the call
fails. It never falls back to the last active Browser or tab.

### DevTools Session State

Network and console tracking state belongs to the automation session.

When the target tab changes:

- The old CDP target is detached.
- Collected network and console buffers are cleared.
- Enabled tracking domains are re-enabled on the new target.
- Subsequent results identify the current `tab_id` and URL.

## Connection Lifecycle

The external service uses these states:

1. `DISABLED`
2. `LISTENING`
3. `PENDING_APPROVAL`
4. `LEASE_ACTIVE`

Transitions:

- Settings toggle on: `DISABLED` → `LISTENING`.
- Client initialization and catalog discovery keep the service in `LISTENING`
  without starting the approval timer.
- The first tool call selects an eligible target:
  `LISTENING` → `PENDING_APPROVAL`.
- User approval and available lease: `PENDING_APPROVAL` → `LEASE_ACTIVE`.
- Denial, timeout, disconnect, helper exit, or user revocation:
  `PENDING_APPROVAL`/`LEASE_ACTIVE` → `LISTENING`.
- Settings toggle off from any state: → `DISABLED`.

Tool discovery may complete and remain idle before approval. A tool call starts
and waits for the pending approval for a bounded period. Denial or approval
timeout returns a stable tool error. Initialization itself must not grant
browser access.

## IPC and Local Security

- Use a Unix domain socket accessible only to the current user.
- Set the socket and runtime metadata permissions to `0600`.
- Verify the peer UID after accepting a connection.
- Publish the socket path, browser PID, IPC protocol version, and connection
  nonce in the runtime metadata.
- Rotate the nonce whenever MCP is enabled and whenever Dao restarts.
- Delete runtime metadata immediately when MCP is disabled.
- Treat the client name and version as untrusted labels.
- Display the verified local PID, when available, alongside the reported client
  name.
- Keep the IPC protocol versioned independently from the MCP protocol version.
- Reject unknown tools and protocol versions before execution.
- Do not expose a generic CDP command tool.

The packaged helper's stdout is reserved exclusively for MCP JSON-RPC. All logs
and diagnostics go to stderr.

## Eligible Targets

The first release supports:

- Normal browser Profiles.
- Normal browser windows.
- `http://` and `https://` pages.
- A new blank tab created through `open_tab`.
- Web-hosted PDF content when the underlying target is otherwise eligible.

The first release rejects:

- Incognito and Guest Profiles.
- DevTools windows and pages.
- Extension pages.
- Dao and Chromium internal pages.
- `file://` pages.
- Agent WebUI pages.

An ineligible target produces `TARGET_FORBIDDEN`. The executor does not search
for a different eligible tab.

## Tool Execution and Results

Each tool call follows this sequence:

1. Validate connection and approval.
2. Validate the caller's lease.
3. Validate tool exposure and input schema.
4. Resolve the authorized Browser and session target.
5. Validate target eligibility.
6. Execute the native or composite operation.
7. Normalize the result.
8. Convert it in the caller's adapter.

The internal normalized result shape is conceptually:

```json
{
  "ok": true,
  "data": {},
  "media": null,
  "target": {
    "tab_id": "stable-id",
    "url": "https://example.com/"
  }
}
```

Failures contain:

```json
{
  "ok": false,
  "error": {
    "code": "STABLE_ERROR_CODE",
    "message": "Human-readable message",
    "retryable": false
  }
}
```

The MCP adapter maps:

- Ordinary values to text content and `structuredContent`.
- Screenshots to native MCP image content with a MIME type.
- Tool failures to `isError: true` with a stable error code.
- MCP request cancellation to IPC cancellation and executor cleanup.

All calls have a deadline. Cancellation, timeout, disconnect, and revocation
must release temporary tab locks, pending callbacks, cursor state, and
highlights. A CDP command that cannot be recalled may finish internally, but its
result is discarded and no later queued operation may start for the cancelled
request.

## Error Contract

Required stable errors include:

- `MCP_DISABLED`
- `AUTHORIZATION_DENIED`
- `AUTHORIZATION_TIMEOUT`
- `AGENT_CONTROL_BUSY`
- `LEASE_BUSY`
- `TARGET_GONE`
- `TARGET_FORBIDDEN`
- `INVALID_ARGUMENT`
- `UNKNOWN_TOOL`
- `IPC_VERSION_UNSUPPORTED`
- `DEVTOOLS_ATTACH_FAILED`
- `TOOL_TIMEOUT`
- `TOOL_CANCELLED`
- `INTERNAL_ERROR`

Every error includes `code`, `message`, and `retryable`. Tool errors use MCP
tool-result errors rather than falsely reporting success.

## Verification

### Catalog and Unit Tests

- Assert the 29 MCP tool names and schemas.
- Assert `resolve_element_context` is Dao Agent-only.
- Assert non-browser groups are not exposed.
- Validate input and output schema handling.
- Validate MCP text, image, structured, and error result mapping.
- Validate every stable error code.

### Lease and Session Tests

- Dao Agent and external Agent cannot hold the lease concurrently.
- An in-flight tool completes before a lease changes owner.
- A second external socket is rejected by the one-client transport admission
  gate; an admitted external client blocked on the shared lease receives
  `LEASE_BUSY`.
- Denial, approval timeout, revoke, disconnect, and toggle-off release state.
- Browser, Profile, and tab destruction never cause last-active fallback.
- User interaction remains available outside brief per-operation tab locks.

### IPC and Helper Tests

- Golden tests for MCP initialize, `tools/list`, `tools/call`, cancellation, and
  shutdown over stdio.
- stdout contains only valid MCP traffic.
- Diagnostics use stderr.
- Socket and metadata permissions are `0600`.
- Peer UID and nonce validation reject invalid connections.
- Disabling MCP removes runtime metadata and rejects helper connections.
- IPC protocol version mismatch fails closed.

### Browser Tests

- Approval binds the intended Browser, Profile, and initial target.
- Focus changes do not move the authorized Browser or target.
- Stable `tab_id` survives tab reordering.
- Switching and closing tabs update the target correctly.
- Tracking state rebinds and buffers reset after target changes.
- Incognito, Guest, internal, extension, DevTools, and `file://` targets fail.
- Screenshot, input, script, resource, network, and console paths use the shared
  executor.

### Dao Agent Regression Tests

- The existing 30 browser tools continue to work through the shared executor.
- Agent-specific `resolve_element_context` behavior remains unchanged.
- Existing cursor, highlight, tab-lock, truncation, and tool rendering behavior
  remains unchanged.

### Manual Verification

- Settings toggle and status states.
- Approval and denial UI.
- Active-control indicator and Stop action.
- Application restart and helper disconnect.
- Copyable MCP client configuration.
- Final compile confirmation uses only `npm run rebuild`.

## Delivery Sequence

1. Extract the declarative catalog, shared executor, session state, and
   automation core while preserving Dao Agent behavior.
2. Add Settings, the disabled-by-default listener lifecycle, authorization UI,
   agent lease manager, and active-control indicator.
3. Package the native `dao-mcp` helper and add MCP/IPC result mapping.
4. Add end-to-end protocol, security, concurrency, and browser tests.
5. Update `docs/features.md`, `docs/feature-checklist.md`, and user-facing MCP
   configuration documentation in the implementation change.

## Documentation Impact

This document is design-only and does not add or change a shipped feature.
Therefore, `docs/features.md` and `docs/feature-checklist.md` are not changed
yet. They are required in the implementation change because the MCP server will
materially add a Dao Browser feature and a Chromium upgrade regression surface.
