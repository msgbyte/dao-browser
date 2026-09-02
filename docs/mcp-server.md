# Dao Local MCP Server

Dao Browser includes a local MCP server for controlling one explicitly
approved browser target. It is disabled by default and available on macOS.

## Enable and configure

1. Open **Settings → You and Dao**.
2. Turn on **MCP server**.
3. In **Quick setup**, choose **Codex CLI**, **Claude Code CLI**, or
   **Generic MCP**.
4. Review the complete command or JSON configuration, then use the copy button
   for the selected option.

The Settings card has a header, connection status, and enabled-only quick
setup section. The selector and copy action share an aligned control row and
stack at narrow widths. Quick setup is shown only while the MCP server switch
is on. Codex CLI is the default selection. Dao derives the helper path from the
currently running app bundle and binds the helper to the current browser's
user-data directory. Both arguments are shell-quoted before generating either
command:

```sh
codex mcp add dao -- '/Applications/Dao.app/Contents/Helpers/dao-mcp' '--user-data-dir=/Users/example/Library/Application Support/Dao'
claude mcp add --scope user dao -- '/Applications/Dao.app/Contents/Helpers/dao-mcp' '--user-data-dir=/Users/example/Library/Application Support/Dao'
```

The Claude Code command installs Dao in user scope. The preview and clipboard
content come from the same native command builder, so paths containing spaces
or single quotes remain safe to paste into a POSIX shell.

For a client that does not support either CLI command, select **Generic MCP**.
The preview uses Chromium-native three-space pretty JSON and the button changes
to **Copy MCP configuration**. The clipboard content exactly matches the
visible preview. Paste it into that client's MCP configuration, then restart or
reload the client. If Dao's service configuration cannot be parsed as JSON,
Generic MCP fails closed: the preview is empty and Copy does not change the
clipboard.

The copied configuration points to the same helper. A typical installation
produces:

```json
{
   "mcpServers": {
      "dao": {
         "args": [
            "--user-data-dir=/Users/example/Library/Application Support/Dao"
         ],
         "command": "/Applications/Dao.app/Contents/Helpers/dao-mcp"
      }
   }
}
```

Use Dao's generated command or **Generic MCP** configuration instead of
assuming `/Applications` or a default user-data directory when Dao is running
from another folder, a Debug bundle, or a custom profile root.

## Connection flow and status

The Settings row reports one of four states:

- **Disabled** — the global Local State switch is off and no endpoint is
  published.
- **Ready** — the endpoint is listening and no client controls a browser.
- **Approval requested** — a client's first tool call is waiting for the native
  Dao approval dialog.
- **Connected** — approval succeeded and the client owns the external browser
  automation lease.

Dao exposes the tool catalog before browser authorization. If the client starts
while Dao Settings or another ineligible page is active, discovery still
returns all tools. Initialization and `tools/list` do not display an approval
dialog or start the approval timeout, so an idle discovery connection remains
available. The first `tools/call` snapshots the exact last-active eligible
normal browser window and active tab and requests approval. Changing focus
after target selection does not move the approved target. Approval is required
before every connection can execute tools, and Allow is not the default dialog
action.

## Tool scope

MCP exposes 31 native browser tools from the same versioned catalog used by Dao
Agent:

- page information, HTML, accessibility, scoped semantic queries, screenshots,
  guarded clicks, script execution, input, scrolling, highlighting, and cursor
  interaction;
- window-scoped tab listing, switching, opening, and closing;
- network and console capture, including cursor-based response waits;
- page-resource listing, reading, and search.

`resolve_element_context` is an additional native Dao Agent browser tool and is
not exposed to MCP. Agent memory, skills, workspace, and web-provider tools are
also outside the MCP browser catalog.

The shared native catalog, executor, session, Page, Tab, and DevTools
implementations live under `src/dao/browser/automation/`. MCP-specific service,
transport, protocol, authorization lifecycle, and stdio helper code remains
under `src/dao/browser/mcp/`.

The authoritative names and schemas are in
`src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json`.

## Eligible targets

An MCP target must remain:

- a tab in the exact approved normal Browser;
- in a regular, non-Incognito, non-Guest Profile;
- committed to `http://`, `https://`, or the literal `about:blank`.

Web-hosted PDFs are eligible because their top-level committed URL remains
HTTP(S). Popups, internal Dao/Chromium pages, extension pages, DevTools, the
Agent WebUI, `file://`, data URLs, custom schemes, Incognito, and Guest windows
are rejected. Dao never searches for or falls back to another eligible tab.
A forbidden `switch_tab` candidate is rejected before the active tab or
session target changes.

Closing the approved window, unexpectedly losing the target, destroying its
Profile, or navigating the target to a forbidden URL terminates that
connection. A successful `close_tab` may instead retarget the eligible active
replacement in the same approved window; no replacement or a forbidden
replacement fails closed. Terminal cleanup cancels work, removes temporary
input locks and overlays, detaches DevTools, releases the lease, and returns to
Ready only after the accepted socket actually closes.

## Stop, revoke, and disable

- Select **Stop** in Settings or in the exact-window control banner to cancel
  active work and revoke the current connection. The enabled server returns to
  Ready after disconnect.
- Turn off **MCP server** to stop listening, revoke any connection, and remove
  the socket and runtime metadata.
- Denying or closing the approval dialog fails closed.

Only one external connection is admitted. While MCP owns the browser lease,
Dao Agent chat and non-browser work continue, but Agent browser tools return
the retryable `AGENT_CONTROL_BUSY` error.

## Security model

The browser publishes a Unix domain socket and fresh 256-bit nonce under the
current user-data directory with owner-only permissions. The browser verifies
the peer UID and nonce, bounds protocol lines and pending I/O, and keeps
listener I/O on Chromium's browser IO thread. The helper uses NDJSON over
stdin/stdout, negotiates MCP `2025-11-25` or `2025-06-18`, and writes
diagnostics only to stderr.

MCP clients can inspect and interact with the approved page, including running
JavaScript. Review the client identity and target shown in the approval dialog
before allowing access.

## Common errors

| Error | Meaning |
|---|---|
| `MCP_DISABLED` | Dao is disabled, unavailable, or the browser connection was lost. |
| `AUTHORIZATION_DENIED` | Approval, nonce, or client authorization failed. |
| `AUTHORIZATION_TIMEOUT` | Hello or user approval did not finish in time. |
| `LEASE_BUSY` | The admitted external client could not acquire the shared browser-control lease. A second external socket is rejected by the one-client admission gate. |
| `AGENT_CONTROL_BUSY` | Dao Agent attempted a browser tool while MCP held the lease. |
| `TARGET_FORBIDDEN` | The exact window, Profile, URL, or switch candidate is ineligible. |
| `TARGET_GONE` | The exact approved window, tab, Profile, or owner no longer exists. |
| `TOOL_CANCELLED` | The call was cancelled by the client or terminal cleanup. |

## Troubleshooting

- **Client says the helper is missing:** copy **Generic MCP** again from the
  running Dao build and verify `Contents/Helpers/dao-mcp` is executable.
- **Status stays Disabled:** confirm the Settings switch is on. Disable and
  re-enable it to republish fresh runtime metadata.
- **No approval appears:** tool discovery does not require approval. Bring the
  intended normal, regular-Profile Dao window and an eligible tab to the front,
  then make the first tool call.
- **Target forbidden:** move to an HTTP(S) page or `about:blank`; internal,
  extension, DevTools, local-file, popup, Incognito, and Guest targets are not
  eligible.
- **Busy error:** stop the current MCP connection or wait for Dao Agent to
  release its browser lease, then retry.
- **Stale client after Stop:** wait for the client process to observe the
  disconnect, then reconnect. Dao does not admit a replacement until the old
  accepted socket is closed.
