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
available. Without a `tab_id`, the first `tools/call` snapshots the exact
last-active eligible normal browser window and requests permission to create
new tabs there. The active page can be an internal page: window approval grants
no access to existing pages. New MCP tabs open in the background and become the
connection's default target without changing your active tab. Approval is
required before execution, and Allow is not the default dialog action. A first
page tool, `switch_tab`, or `close_tab` call without `tab_id` returns
`AUTHORIZATION_DENIED` without prompting, because window approval would grant
it no page.

The first tool call must include `reason` in its arguments, for example:

```json
{"name":"open_tab","arguments":{"url":"https://example.com/","reason":"Research the documentation in a new background tab."}}
```

Use non-blank text within 1024 UTF-8 bytes. Missing, blank, or invalid reasons
return `INVALID_ARGUMENT` without opening a dialog; retry with a valid reason.
The tool schemas and server instructions advertise this requirement. Calls on
that connection may omit `reason` once approval has been requested. The reason
is permission metadata and is removed before executing the browser tool.

The dialog shows the reported client name/version,
the browser-recorded request date/time with time zone, and the
reason labeled as client-provided, alongside the selected window and Profile.
Queued dialogs retain each request's original time and reason. Long reasons
scroll within the dialog so the approval controls remain accessible.

### Request access only to a specific tab

Copy the target's ID from its sidebar context menu, then include `tab_id` and
`reason` in the first tool call on a new connection:

```json
{"name":"get_page_info","arguments":{"tab_id":"<copied-tab-id>","reason":"Read the specific tab selected by the user."}}
```

`list_tabs` also accepts `tab_id` for this initial request. A first `switch_tab`
or `close_tab` call with an ID uses the same single-tab authorization. Dao finds
that exact tab across windows, including background tabs, and displays its
recorded title and URL with an explicit single-tab scope. Missing, ambiguous,
or ineligible targets fail before prompting; Dao never falls back to another
tab. The normal Profile and URL restrictions still apply.

After approval, page tools can omit `tab_id` or keep passing the authorized ID.
`list_tabs` returns only granted, eligible tabs. Tab indices in MCP requests and
results count granted tabs only, so they never reveal unapproved tabs; a
non-integer or out-of-range index returns `INVALID_ARGUMENT`. Switching to or
closing another tab by ID is denied. An initial explicit page grant does not
permit `open_tab`. Grants follow the tab across eligible navigation, not just
the URL displayed in the dialog. New tabs share the normal profile's login
state; page grants do not isolate cookies or same-origin web capabilities.

### Request an existing page after connecting

Use `request_tab_access` with a fresh non-blank `reason` whenever an existing
page is needed. An optional `tab_id` pins approval to that exact page in the
approved window. Without an ID, Dao opens a native page picker: candidate titles,
URLs, and IDs are never returned to the MCP client. Approval returns the chosen
page's `tab_id`, title, and URL and makes it the connection's default target.
Ambiguous explicit tab IDs are rejected; the picker repairs duplicated IDs
before listing pages. IDs changed or duplicated while approval is
pending cannot receive a grant. Denied, cancelled, expired, or stale approvals
grant nothing and preserve all
previous grants. A page leased by another controller remains unavailable.

```json
{"name":"request_tab_access","arguments":{"reason":"Read the page you want me to summarize."}}
```

### Manage grants

The native MCP controls show each connection, its window creation permission,
and its granted pages. Revoke a page to cancel its pending work and release its
lease without initiating a page close. Revocation or client cancellation rejects
an already-visible close confirmation, but cannot roll back actions already
dispatched to the renderer, including a close that has not yet shown its
confirmation. A `close_tab` timeout leaves the confirmation for you to answer.
Revoking one page does not cancel a pending request for a different page. Stop a
connection to revoke all its grants.
Revoking or closing the default target clears default routing; choose another
granted ID or create a new tab. Losing the final page preserves window creation
permission, and a page-only connection may request another page. A revoked page
always needs fresh approval, even if this connection originally created it.
Disconnecting, disabling MCP, closing the approved window, or destroying its
profile ends the connection and releases all grants.

## Tool scope

MCP exposes 43 native browser tools from the same versioned catalog used by Dao
Agent:

- page information, HTML, accessibility, scoped semantic queries, screenshots,
  guarded clicks, script execution, input, scrolling, highlighting, and cursor
  interaction;
- granted-tab listing, switching, opening, closing, and native existing-page approval;
- network and console capture, including cursor-based response waits;
- page-resource listing, reading, and search;
- bounded Jev browser subtasks through `run_browser_task` (requires explicit
  Jev configuration and tool permission).
- background Agent Spaces (`create_space`, `list_spaces`, `set_space_status`,
  `request_user_help`, `handle_dialog`, `finish_space`, `run_browser_script`),
  which run in hidden task windows under their own approval, plus MCP-only
  `get_site_skills`. Passing `space_id` to other browser tools runs them in that
  space instead of a granted tab.

`resolve_element_context` is an additional native Dao Agent browser tool and is
not exposed to MCP. Agent memory, skills, workspace, and web-provider tools are
also outside the MCP browser catalog.

The shared native catalog, executor, session, Page, Tab, and DevTools
implementations live under `src/dao/browser/automation/`. MCP-specific service,
transport, protocol, authorization lifecycle, and stdio helper code remains
under `src/dao/browser/mcp/`.

The authoritative names and schemas are in
`src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json`.

## Response selection and script execution

`wait_for_network_response` matches responses using URL, method, status, and
optional `json_path`/`any_of` conditions. Its `select` list only projects output
into the `selected` object: missing paths are omitted and explicit JSON nulls
are preserved. A response can therefore match when it has `$.result` but no
`$.error`, or vice versa. Invalid path syntax is still rejected.

`capture_screenshot` returns the encoded image once in MCP `content`, with its
actual MIME type. `structuredContent` retains metadata such as `format`, without
a second copy of the image. MCP consumers should read image bytes from the image
content block; the internal Agent screenshot result is unchanged.

`execute_script` evaluates code as supplied and returns a synchronous value.
Top-level `let` and `const` bindings persist between calls, so use an immediately
invoked function expression (IIFE) for local declarations and `return`:

```javascript
(() => {
  const title = document.title;
  return {title};
})()
```

Return a small JSON-serializable value. Top-level `await` is unsupported, and
returned promises are not awaited. Use `wait_for_element` or
`wait_for_network_response` for supported waits. Correct script syntax before
retrying. Script exceptions retain their original details and existing
non-retryable `INTERNAL_ERROR` classification. A `SyntaxError` can also come from
JSON parsing or page code at runtime, so its class alone does not indicate invalid
script syntax.

## Use Jev from an MCP client

1. In **Settings → Agent**, enable the experimental **Jev** connection, enter
   the full compatible Decisions API URL and Bearer token, and enable the
   separate **run_browser_task** tool permission.
2. Connect your usual MCP client using **Generic MCP** above. If already
   connected, refresh its tool list after upgrading Dao or changing Jev settings.
   `run_browser_task` is listed only with an enabled, valid Jev connection and
   tool permission. Discovery does not call Jev or request browser control.
3. Invoke `run_browser_task` on the intended HTTP(S) tab and approve the usual
   MCP connection dialog. For example, a `tools/call` request can contain:

```json
{
  "name": "run_browser_task",
  "arguments": {
    "goal": "Fill the name and submit the form",
    "known_inputs": [{"field": "Name", "value": "Alice"}],
    "completion": [{"kind": "text", "value": "Submitted successfully"}],
    "reason": "Complete the form requested by the user"
  }
}
```

Use labels and completion text that match the actual page. Completion conditions
are checked locally and must all match: `text` requires visible text, `url`
requires an exact URL, and `field` requires an exact accessible field name and
value. Supply only non-sensitive known inputs. `max_steps` defaults to 20 (1–20)
and `timeout_ms` defaults to 60000 (1000–60000).

The client keeps its existing main model. Dao calls the saved Jev endpoint with
`jev-latest`; credentials are never passed as MCP tool arguments. Execution is
native and works while the Agent panel is closed. Ordinary `click_by_ref`,
`fill_by_ref`, and other browser tools continue to execute directly; choose
`run_browser_task` explicitly when you want Jev to carry out a subtask.

Results include `status`, `progress` (actions and locally verified condition
indexes), and timing/count `metrics`. An incomplete task has `isError: true`
while preserving its structured partial result. Review that progress before
retrying, since some actions may already have completed. Client cancellation,
connection revocation, target loss, or changing Jev settings stops pending
requests and prevents late responses from taking actions. The saved configuration
and permission are shared with Dao Agent.

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

Closing the approved window or destroying its Profile terminates the connection.
Closing a granted page or navigating it to a forbidden URL revokes only that page.
The connection remains available with any creation capability and other page grants
intact, even when no pages remain. Closing or revoking the default page clears the
default target; Dao never grants or selects a replacement automatically. Revocation
cancels work on that page and removes its input locks, overlays, DevTools session,
and lease. Terminal connection cleanup returns to Ready after its socket closes.

## Stop, revoke, and disable

- Select **Stop** in Settings or in the exact-window control banner to cancel
  active work and revoke the current connection. The enabled server returns to
  Ready after disconnect.
- Turn off **MCP server** to stop listening, revoke any connection, and remove
  the socket and runtime metadata.
- Denying or closing the approval dialog fails closed.

Up to 32 external connections are admitted, with exclusive control per tab.
Different connections can control different tabs. Dao Agent chat and
non-browser work continue; Agent browser tools targeting an MCP-controlled tab
return the retryable `AGENT_CONTROL_BUSY` error.

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
| `TOO_MANY_CLIENTS` | Dao already has 32 MCP clients admitted and every one of them is busy with an approval or a tool call, so nothing idle could be evicted. Retry later or close unused MCP sessions. |
| `LEASE_BUSY` | The admitted external client could not acquire the shared browser-control lease for its target tab. |
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
