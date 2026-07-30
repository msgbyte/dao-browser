# Dao MCP Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a disabled-by-default local MCP server that lets one explicitly authorized external agent use Dao Browser's 29 browser automation tools against the user's current normal Dao window.

**Architecture:** A packaged native `dao-mcp` helper speaks MCP JSON-RPC over stdio and forwards versioned requests over a private Unix domain socket. Dao Browser owns authorization, the peer-agent lease, target window/tab state, the canonical browser-tool catalog, and one shared automation executor used by both Dao Agent and external MCP clients.

**Tech Stack:** Chromium C++/Views, `content::DevToolsAgentHost`, `net::UnixDomainServerSocket`, GRIT/WebUI resources, Polymer TypeScript settings UI, MCP stdio JSON-RPC, Vitest, Chromium browser tests.

## Global Constraints

- Communicate with the user in Chinese; keep source, tests, comments, docs, commit messages, and user-visible source strings in English.
- All visible copy must use Dao/Chromium internationalization resources.
- Work in the current checkout. Do not create a branch or worktree.
- Do not edit `engine/` directly. Change `src/dao/` and `src/patches/`, then use plain `npm run import`.
- Never use `npm run import -- --force` without explicit user confirmation.
- Never use direct `gn`, `ninja`, `autoninja`, or `siso`.
- `npm run rebuild` is the only compile-confirmation command.
- Use the smallest relevant test filter after a test binary exists. `npm run test` is permitted once when the new browser-test target must be built.
- Do not edit generated Agent vendor files or run `i18n.sh`.
- Do not execute `git add`, `git commit`, or another state-changing Git command unless the latest user message explicitly authorizes that exact action.
- If commits are authorized, use Conventional Commits without a body.
- MCP is macOS arm64-only in this release, local stdio only, and disabled by default.
- The first release rejects Incognito, Guest, DevTools, extension, internal, Agent WebUI, and `file://` targets.
- The MCP helper must never emit non-MCP output on stdout; diagnostics go to stderr.

## Planned File Structure

### Shared browser automation

- `src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json` — canonical definitions, schemas, exposure, annotations, and deadlines.
- `src/dao/browser/automation/dao_browser_tool_types.{h,cc}` — normalized calls, results, errors, media, and targets.
- `src/dao/browser/automation/dao_browser_tool_catalog.{h,cc}` — parses and serves the embedded catalog.
- `src/dao/browser/automation/dao_tool_schema_validator.{h,cc}` — validates `base::DictValue` arguments against the catalog subset.
- `src/dao/browser/automation/dao_agent_lease_manager.{h,cc}` — arbitrates Dao Agent and external MCP ownership.
- `src/dao/browser/automation/dao_browser_automation_session.{h,cc}` — authorized Browser/Profile/target and tracking state.
- `src/dao/browser/automation/dao_devtools_client.{h,cc}` — reusable `DevToolsAgentHostClient`.
- `src/dao/browser/automation/dao_browser_tool_executor.{h,cc}` — common validation, dispatch, deadline, and cancellation entry point.
- `src/dao/browser/automation/dao_browser_target_policy.{h,cc}` — validates exact external automation targets without making the shared executor depend on the MCP service.
- `src/dao/browser/automation/dao_page_tools.{h,cc}` — Page/Input/Runtime implementations.
- `src/dao/browser/automation/dao_tab_tools.{h,cc}` — window-scoped tab implementations.
- `src/dao/browser/automation/dao_devtools_tools.{h,cc}` — Network/Console/resource implementations.

### MCP browser service and native UI

- `src/dao/browser/mcp/dao_mcp_protocol.{h,cc}` — versioned newline-delimited browser IPC envelope.
- `src/dao/browser/mcp/dao_mcp_runtime_files.{h,mm}` — socket path, metadata, nonce, modes, and cleanup.
- `src/dao/browser/mcp/dao_mcp_connection.{h,cc}` — one accepted IPC stream and in-flight calls.
- `src/dao/browser/mcp/dao_mcp_service.{h,cc}` — global enablement, listener, approval, lease, and status.
- `src/dao/browser/mcp/dao_mcp_settings_handler.{h,cc}` — Settings WebUI messages and status events.
- `src/dao/browser/ui/views/dao_mcp_approval_dialog.{h,cc}` — one-connection approval dialog.
- `src/dao/browser/ui/views/dao_mcp_control_banner_view.{h,cc}` — active external-control status and Stop action.

### Packaged stdio helper

- `src/dao/browser/mcp/helper/main.cc` — process entry point and exit codes.
- `src/dao/browser/mcp/helper/dao_mcp_stdio_server.{h,cc}` — MCP initialize/list/call/cancel framing.
- `src/dao/browser/mcp/helper/dao_mcp_browser_client.{h,cc}` — runtime discovery and browser IPC client.
- `src/dao/browser/mcp/BUILD.gn` — browser library, helper executable, tests, and macOS bundle data.

### WebUI and integration

- `src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.ts` — cached loader for the canonical JSON resource.
- Existing Agent bridge/adapter files — keep Agent-only tools and delegate browser tools to the shared native executor.
- Existing Settings Dao page patches — toggle, status, copy config, and Stop control.
- Existing BrowserView/layout patches — mount and lay out the control banner.
- Existing pref, BUILD.gn, strings, feature docs, and checklist files — register and ship the feature.

---

### Task 1: Canonical browser-tool catalog

**Files:**
- Create: `src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json`
- Create: `src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.ts`
- Create: `src/dao/browser/ui/webui/resources/agent/__tests__/browser_tool_catalog.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/BUILD.gn`
- Modify: `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/pi_tool_adapter.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/pi_tool_adapter.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/tool_catalog.test.ts`

**Interfaces:**
- Produces: `initializeBrowserToolCatalog(): Promise<void>`
- Produces: `getBrowserToolDefinitions(client: 'dao_agent'|'mcp'): ToolDefinition[]`
- Produces: catalog entries with `name`, `description`, `inputSchema`, optional `outputSchema`, `group`, `clients`, `sideEffect`, and `timeoutMs`.
- Consumes: no new native code.

- [ ] **Step 1: Add failing catalog tests**

Test the exact MCP set, Agent-only resolver, duplicate rejection, and required
metadata:

```ts
it('exposes exactly 29 browser tools to MCP', async () => {
  await initializeBrowserToolCatalog();
  const names = getBrowserToolDefinitions('mcp').map(
      tool => tool.function.name);
  assertEquals(29, names.length);
  assertFalse(names.includes('resolve_element_context'));
  assertDeepEquals(
      ['devtools', 'page', 'tabs'],
      [...new Set(getCatalogEntries('mcp').map(entry => entry.group))].sort());
});

it('keeps resolve_element_context available to Dao Agent only', async () => {
  await initializeBrowserToolCatalog();
  assertTrue(getBrowserToolDefinitions('dao_agent').some(
      tool => tool.function.name === 'resolve_element_context'));
  assertFalse(getBrowserToolDefinitions('mcp').some(
      tool => tool.function.name === 'resolve_element_context'));
});
```

- [ ] **Step 2: Run the focused tests and verify failure**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/browser_tool_catalog.test.ts
```

Expected: FAIL because `browser_tool_catalog.ts` and the JSON resource do not
exist.

- [ ] **Step 3: Add the canonical JSON resource and cached loader**

Start the catalog with this exact entry shape and repeat it for the approved 30
browser tools:

```json
{
  "version": 1,
  "tools": [
    {
      "name": "get_page_info",
      "description": "Get the current target page URL, title, and meta description",
      "group": "page",
      "clients": ["dao_agent", "mcp"],
      "sideEffect": "read",
      "timeoutMs": 5000,
      "inputSchema": {
        "type": "object",
        "properties": {},
        "required": []
      },
      "outputSchema": {
        "type": "object",
        "properties": {
          "url": {"type": "string"},
          "title": {"type": "string"},
          "description": {"type": "string"},
          "tab_id": {"type": "string"}
        },
        "required": ["url", "title", "tab_id"]
      }
    }
  ]
}
```

Include these exact names: `get_page_info`, `get_page_html`,
`get_accessibility_tree`, `resolve_element_context`, `capture_screenshot`,
`click_element`, `agent_click`, `click_by_ref`, `move_cursor`,
`highlight_element`, `scroll_down`, `scroll_up`, `scroll_to_element`,
`press_key_chord`, `type_text`, `execute_script`, `list_tabs`, `switch_tab`,
`open_tab`, `close_tab`, `enable_network_tracking`, `get_network_requests`,
`clear_network_requests`, `get_network_body`, `enable_console_tracking`,
`get_console_messages`, `clear_console_messages`, `list_page_resources`,
`get_resource_content`, and `search_in_resources`.

Mark `resolve_element_context` with `"clients": ["dao_agent"]`. Move each
existing JSON Schema from `agent_bridge.ts` without changing its tool name or
argument meaning.
Add tool annotations through `sideEffect`: `read`, `interaction`, or
`destructive`; only `close_tab` is destructive.

Implement a fetch-once loader:

```ts
let catalogPromise: Promise<BrowserToolCatalog>|undefined;

export function initializeBrowserToolCatalog(): Promise<void> {
  catalogPromise ??= fetch('browser_tool_catalog.json')
      .then(response => {
        if (!response.ok) {
          throw new Error(`browser tool catalog load failed: ${response.status}`);
        }
        return response.json() as Promise<BrowserToolCatalog>;
      })
      .then(validateCatalog);
  return catalogPromise.then(() => undefined);
}
```

Add the JSON file to `static_files` and `browser_tool_catalog.ts` to `ts_files`.
Await initialization before constructing the Agent in `dao_chat_view.ts`.

- [ ] **Step 4: Remove browser schema duplication from `agent_bridge.ts`**

Keep Agent-only, memory, skill, web, and workspace definitions in TypeScript.
Build the exported Agent tool list as:

```ts
export function getAgentToolDefinitions(): ToolDefinition[] {
  return [
    ...getBrowserToolDefinitions('dao_agent'),
    ...agentOnlyTools,
  ];
}
```

Update `pi_tool_adapter.ts` and tests to call
`getAgentToolDefinitions()` after catalog initialization.

- [ ] **Step 5: Run focused WebUI verification**

Run:

```bash
npm run test:webui -- \
  src/dao/browser/ui/webui/resources/agent/__tests__/browser_tool_catalog.test.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/pi_tool_adapter.test.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/tool_catalog.test.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
npm run lint:lit
```

Expected: all selected Vitest files pass; Lit lint reports no errors.

- [ ] **Step 6: Commit only if explicitly authorized**

```bash
git add src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json \
  src/dao/browser/ui/webui/resources/agent
git commit -m "refactor(agent): centralize browser tool catalog"
```

Otherwise leave the changes unstaged and report that the commit step was
intentionally skipped.

### Task 2: Native result, catalog, schema, session, and lease foundations

**Files:**
- Create: `src/dao/browser/mcp/BUILD.gn`
- Create: `src/dao/browser/automation/dao_browser_tool_types.{h,cc}`
- Create: `src/dao/browser/automation/dao_browser_tool_catalog.{h,cc}`
- Create: `src/dao/browser/automation/dao_tool_schema_validator.{h,cc}`
- Create: `src/dao/browser/automation/dao_agent_lease_manager.{h,cc}`
- Create: `src/dao/browser/automation/dao_browser_automation_session.{h,cc}`
- Create: `src/dao/browser/mcp/dao_mcp_foundation_unittest.cc`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`
- Modify: `src/dao/browser/ui/webui/resources/agent/BUILD.gn`

**Interfaces:**
- Produces: `DaoBrowserToolCall`, `DaoBrowserToolResult`, `DaoToolError`,
  `DaoToolTarget`, and `DaoToolMedia`.
- Produces: `DaoBrowserToolCatalog::Get()->Find(name, client)`.
- Produces: `ValidateToolArguments(const DaoBrowserToolDefinition&, const base::DictValue&)`.
- Produces: `DaoAgentLeaseManager::TryAcquire(DaoAgentClientId)`.
- Produces: `DaoBrowserAutomationSession(Browser*, content::WebContents*)`.
- Consumes: `browser_tool_catalog.json` embedded as a GRIT resource.

- [ ] **Step 1: Add failing foundation tests**

Cover exact error codes, required schema fields, one-holder lease semantics, and
no target fallback:

```cpp
TEST(DaoMcpCatalogTest, ExposesExactlyTwentyNineToolsToMcp) {
  const auto tools =
      DaoBrowserToolCatalog::Get()->List(DaoToolClient::kMcp);
  EXPECT_EQ(29u, tools.size());
  EXPECT_EQ(nullptr, DaoBrowserToolCatalog::Get()->Find(
                         "resolve_element_context", DaoToolClient::kMcp));
}

TEST(DaoMcpLeaseTest, RejectsSecondAgentUntilRelease) {
  DaoAgentLeaseManager leases;
  auto first = leases.TryAcquire(
      {DaoToolClient::kMcp, "external-1", "Codex"});
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(DaoToolErrorCode::kLeaseBusy,
            leases.TryAcquire(
                {DaoToolClient::kDaoAgent, "dao-agent-turn", "Dao Agent"})
                .error()
                .code);
  first->Reset();
  EXPECT_TRUE(
      leases.TryAcquire(
          {DaoToolClient::kDaoAgent, "dao-agent-turn", "Dao Agent"})
          .has_value());
}
```

- [ ] **Step 2: Add normalized types**

Use explicit types rather than transport-shaped dictionaries:

```cpp
enum class DaoToolClient { kDaoAgent, kMcp };
struct DaoAgentClientId {
  DaoToolClient type;
  std::string connection_id;
  std::string display_name;
};
enum class DaoToolErrorCode {
  kMcpDisabled,
  kAuthorizationDenied,
  kAuthorizationTimeout,
  kAgentControlBusy,
  kLeaseBusy,
  kTargetGone,
  kTargetForbidden,
  kInvalidArgument,
  kUnknownTool,
  kIpcVersionUnsupported,
  kDevToolsAttachFailed,
  kToolTimeout,
  kToolCancelled,
  kInternalError,
};

struct DaoBrowserToolCall {
  std::string request_id;
  std::string name;
  base::DictValue arguments;
  base::TimeDelta timeout;
};

struct DaoBrowserToolResult {
  bool ok = false;
  base::Value data;
  std::optional<DaoToolMedia> media;
  std::optional<DaoToolError> error;
  std::optional<DaoToolTarget> target;
};
```

`TryAcquire` returns
`base::expected<DaoAgentLease, DaoToolError>`. `DaoAgentLease` is move-only and
releases its owner in `Reset()` and its destructor.

- [ ] **Step 3: Parse the embedded catalog and validate its schema subset**

Load `IDR_DAO_AGENT_BROWSER_TOOL_CATALOG_JSON` through `ui::ResourceBundle`.
Fail closed if the version, tool list, name, client list, group, timeout, or
input schema is invalid. Support the schema constructs used by the catalog:
object, string, number, integer, boolean, array, required, enum, minimum,
maximum, and `additionalProperties: false`.

- [ ] **Step 4: Implement RAII leases and weak target sessions**

`DaoAgentLease` releases ownership in its destructor. Session construction
stores weak pointers to the authorized `Browser`, original `Profile`, and
target `WebContents`. `ResolveTarget()` returns `TARGET_GONE` instead of looking
up a last-active Browser.

- [ ] **Step 5: Wire sources and tests into Dao targets**

Create `source_set("browser_mcp")`, depend on `//base`, `//content/public/browser`,
`//net`, `//ui/base`, and the Agent resource target, and add it to
`dao_browser_ui_deps`. Add `dao_mcp_foundation_unittest.cc` to
`dao_browser_ui_test_sources`.

- [ ] **Step 6: Import, compile, and run the first native tests**

Run:

```bash
npm run import
npm run rebuild
npm run test
engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoMcpCatalogTest.*:DaoMcpLeaseTest.*:DaoMcpSessionTest.*"
```

Expected: import applies cleanly; rebuild succeeds; the broad first build of
`browser_tests` passes Dao tests; the focused rerun passes.

- [ ] **Step 7: Commit only if explicitly authorized**

```bash
git add src/dao/browser/mcp src/dao/browser/ui/dao_ui_sources.gni \
  src/dao/browser/ui/webui/resources/agent/BUILD.gn
git commit -m "feat(mcp): add browser tool foundations"
```

### Task 3: Shared CDP client and Page tool executor

**Files:**
- Create: `src/dao/browser/automation/dao_devtools_client.{h,cc}`
- Create: `src/dao/browser/automation/dao_browser_tool_executor.{h,cc}`
- Create: `src/dao/browser/automation/dao_page_tools.{h,cc}`
- Create: `src/dao/browser/automation/dao_page_tools_browsertest.cc`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.{h,cc}`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

**Interfaces:**
- Consumes: foundation types, catalog, validator, lease, and session.
- Produces: `DaoDevToolsClient::AttachTo(WebContents*)`,
  `SendCommand(method, params, callback)`, `CancelAll(error)`.
- Produces: `DaoBrowserToolExecutor::Execute(session, client, call, callback)`.
- Produces: Page handlers registered by exact catalog name.

- [ ] **Step 1: Add failing Page executor browser tests**

Test `get_page_info`, `execute_script`, screenshot media, cancellation cleanup,
target-forbidden behavior, and an external lease blocking Dao Agent:

```cpp
IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ExecuteScriptUsesPinnedTarget) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  AddTabAtIndex(1, second_url(), ui::PAGE_TRANSITION_TYPED);
  browser()->tab_strip_model()->ActivateTabAt(1);

  DaoBrowserToolResult result =
      Execute(session.get(), "execute_script",
              base::DictValue().Set("code", "document.title"));
  ASSERT_TRUE(result.ok);
  EXPECT_EQ("First Page", result.data.GetDict().FindString("result").value());
}
```

- [ ] **Step 2: Move `DaoAgentDevToolsClient` into the MCP module**

Preserve command IDs, pending callbacks, event delivery, trusted-client
behavior, and host-close cleanup. Add cancellation that resolves every pending
callback exactly once with `TOOL_CANCELLED`.

- [ ] **Step 3: Implement the common executor pipeline**

Implement this order:

```cpp
void DaoBrowserToolExecutor::Execute(
    DaoBrowserAutomationSession* session,
    DaoToolClient client,
    DaoBrowserToolCall call,
    ResultCallback callback) {
  const auto* definition = catalog_->Find(call.name, client);
  if (!definition) {
    return ReplyError(std::move(callback), DaoToolErrorCode::kUnknownTool);
  }
  if (auto error = ValidateToolArguments(*definition, call.arguments)) {
    return ReplyError(std::move(callback), std::move(*error));
  }
  auto target = session->ResolveEligibleTarget();
  if (!target.has_value()) {
    return ReplyError(std::move(callback), target.error());
  }
  deadlines_.Start(call.request_id, call.timeout,
                   base::BindOnce(&DaoBrowserToolExecutor::Cancel,
                                  weak_factory_.GetWeakPtr(),
                                  call.request_id));
  Dispatch(*definition, *target, std::move(call), std::move(callback));
}
```

Every completion removes its deadline and releases temporary lock/highlight
state.

- [ ] **Step 4: Move the 15 shared Page tool implementations**

Move Page/Input/Runtime logic from `DaoAgentUIHandler` into `DaoPageTools`.
Keep `resolve_element_context` in the Agent adapter. Move all JavaScript
constants used by the shared tools into `dao_page_tools.cc`.

- [ ] **Step 5: Make `DaoAgentUIHandler` a thin adapter**

Each existing WebUI message converts arguments into `DaoBrowserToolCall`, calls
the shared executor with `DaoToolClient::kDaoAgent`, and resolves the JavaScript
callback from the normalized result. Delete the duplicate CDP client and Page
dispatch code after every existing handler is routed.

`HandleBeginAgentTurn` acquires a Dao Agent lease and constructs a session
pinned to the active tab. `HandleEndAgentTurn`, WebUI destruction, and an
aborted turn release that lease and session exactly once.

- [ ] **Step 6: Compile and run focused regression**

Run:

```bash
npm run import
npm run rebuild
engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoMcpPageToolsBrowserTest.*:DaoAgentSidebarViewBrowserTest.AgentTurnKeepsOriginalTabWhenActiveTabChanges"
```

Expected: rebuild passes; MCP Page tests and existing Agent tool tests pass.

- [ ] **Step 7: Commit only if explicitly authorized**

```bash
git add src/dao/browser/mcp src/dao/browser/ui/webui/dao_agent_ui.cc \
  src/dao/browser/ui/webui/dao_agent_ui.h src/dao/browser/ui/dao_ui_sources.gni
git commit -m "refactor(agent): share page automation executor"
```

### Task 4: Window-scoped Tabs tools with stable identities

**Files:**
- Create: `src/dao/browser/automation/dao_tab_tools.{h,cc}`
- Create: `src/dao/browser/automation/dao_tab_tools_browsertest.cc`
- Modify: `src/dao/browser/ui/views/dao_tab_identity.{h,cc}`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.{h,cc}`
- Modify: `src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

**Interfaces:**
- Consumes: `DaoBrowserAutomationSession`.
- Produces: `GetOrCreateSidebarTabId(WebContents*)`.
- Produces: list results containing `tab_id`, `index`, `url`, `title`, and
  `active`.
- Produces: switch/close arguments accepting `tab_id` or `index`, with
  `tab_id` precedence.

- [ ] **Step 1: Add failing tab identity tests**

Cover reorder safety, authorized-window scope, target updates, target close,
last-tab rejection, and no cross-window fallback.

- [ ] **Step 2: Extend the catalog schemas**

For `switch_tab` and `close_tab`, add:

```json
"tab_id": {
  "type": "string",
  "description": "Stable tab id from list_tabs; takes precedence over index"
}
```

Keep `index` for compatibility. `switch_tab` requires at least one selector,
enforced in the executor because the catalog schema subset does not need
`oneOf`.

- [ ] **Step 3: Implement window-scoped tab resolution**

Always use `session->browser()` and its `TabStripModel`. Resolve `tab_id` by
scanning that model only. Never call `FindLastActiveBrowserForMigration()`.

- [ ] **Step 4: Rewire the Agent tab handlers**

Delegate list/switch/open/close to `DaoTabTools`. Preserve current visible tab
activation and update the shared session target after switch/open/target close.

- [ ] **Step 5: Compile and test**

Run:

```bash
npm run import
npm run rebuild
engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoMcpTabToolsBrowserTest.*:DaoSidebarTabIdentity*"
```

Expected: all selected tests pass.

- [ ] **Step 6: Commit only if explicitly authorized**

```bash
git add src/dao/browser/mcp src/dao/browser/ui/views/dao_tab_identity.cc \
  src/dao/browser/ui/views/dao_tab_identity.h \
  src/dao/browser/ui/webui/dao_agent_ui.cc \
  src/dao/browser/ui/webui/dao_agent_ui.h \
  src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json \
  src/dao/browser/ui/dao_ui_sources.gni
git commit -m "feat(mcp): add stable tab targeting"
```

### Task 5: Shared DevTools tracking and resource tools

**Files:**
- Create: `src/dao/browser/automation/dao_devtools_tools.{h,cc}`
- Create: `src/dao/browser/automation/dao_devtools_tools_browsertest.cc`
- Modify: `src/dao/browser/automation/dao_browser_automation_session.{h,cc}`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.{h,cc}`
- Modify: `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

**Interfaces:**
- Consumes: shared CDP client and session.
- Produces: session-owned network/console buffers and enabled flags.
- Produces: target-switch hook that clears buffers and re-enables active CDP
  domains.

- [ ] **Step 1: Add failing DevTools tests**

Test network capture, console capture, body retrieval, resource listing/content,
regex search limits, binary exclusion, 512 KiB text truncation, and state reset
after target switching.

- [ ] **Step 2: Move the 10 DevTools tools into `DaoDevToolsTools`**

Move `OnCDPEvent`, resource-tree helpers, body retrieval, and enable/clear/get
handlers. Keep all current caps and base64 behavior.

- [ ] **Step 3: Move `search_in_resources` out of TypeScript**

Implement serial fetch, binary-type exclusion, regex compilation, line/excerpt
formatting, and `max_matches` early exit in C++. Remove the TypeScript composite
loop and delegate it like every other browser tool.

- [ ] **Step 4: Rebind tracking after target changes**

When `DaoBrowserAutomationSession::SetTarget()` changes WebContents:

```cpp
network_requests_.clear();
console_messages_.clear();
devtools_client_->Detach();
if (network_tracking_enabled_) {
  devtools_client_->SendCommand("Network.enable", {}, base::DoNothing());
}
if (console_tracking_enabled_) {
  devtools_client_->SendCommand("Runtime.enable", {}, base::DoNothing());
}
```

Attach to the new target before enabling either domain.

- [ ] **Step 5: Compile and test the complete shared executor**

Run:

```bash
npm run import
npm run rebuild
engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoMcpDevToolsBrowserTest.*:DaoMcpPageToolsBrowserTest.*:DaoMcpTabToolsBrowserTest.*"
npm run test:webui -- \
  src/dao/browser/ui/webui/resources/agent/__tests__/pi_tool_adapter.test.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: all selected native and WebUI tests pass.

- [ ] **Step 6: Commit only if explicitly authorized**

```bash
git add src/dao/browser/mcp src/dao/browser/ui/webui/dao_agent_ui.cc \
  src/dao/browser/ui/webui/dao_agent_ui.h \
  src/dao/browser/ui/webui/resources/agent/agent_bridge.ts \
  src/dao/browser/ui/dao_ui_sources.gni
git commit -m "refactor(agent): share devtools automation tools"
```

### Task 6: Global toggle, secure Unix socket, and browser service

**Files:**
- Create: `src/dao/browser/mcp/dao_mcp_protocol.{h,cc}`
- Create: `src/dao/browser/mcp/dao_mcp_runtime_files.{h,mm}`
- Create: `src/dao/browser/mcp/dao_mcp_connection.{h,cc}`
- Create: `src/dao/browser/mcp/dao_mcp_service.{h,cc}`
- Create: `src/dao/browser/mcp/dao_mcp_service_browsertest.cc`
- Modify: `src/dao/browser/dao_pref_names.{h,cc}`
- Modify: `src/patches/chrome/browser/prefs/browser_prefs.cc.patch`
- Modify: `src/patches/chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc.patch`
- Modify: `src/dao/browser/mcp/BUILD.gn`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

**Interfaces:**
- Produces: global local-state pref `dao.mcp_server_enabled`, default `false`.
- Produces: `DaoMcpService::Get()`, `SetEnabled(bool)`, `GetStatus()`,
  `StopControl()`, and observer callbacks.
- Produces: injectable `DaoMcpApprovalDelegate::RequestApproval(
  const DaoMcpClientInfo&, Browser*, base::OnceCallback<void(bool)>)`.
- Produces: IPC methods `hello`, `tools/list`, `tools/call`, and `tools/cancel`.
- Consumes: shared catalog, executor, session, and lease manager.

- [ ] **Step 1: Add failing lifecycle and IPC tests**

Cover default-off behavior, mode `0600`, nonce rotation, same-UID acceptance,
bad nonce rejection, toggle-off cleanup, single external connection, protocol
version rejection, and disconnect lease release.

- [ ] **Step 2: Register a global local-state pref**

Add:

```cpp
void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
inline constexpr char kDaoMcpServerEnabled[] = "dao.mcp_server_enabled";
```

Register it as `false` from Chromium's `RegisterLocalState()`. Do not put the
master switch in a Profile pref.

- [ ] **Step 3: Implement runtime files**

Use a per-user runtime directory under Dao's user-data directory. On enable:

- generate a 256-bit random nonce;
- bind a non-abstract Unix socket;
- chmod the directory, socket, and JSON metadata to owner-only access;
- atomically write metadata containing `version`, `socket_path`,
  `browser_pid`, and `nonce`.

On disable or shutdown, close the listener and delete socket and metadata.

- [ ] **Step 4: Implement the versioned browser IPC envelope**

Each message is one UTF-8 JSON object followed by `\n`:

```json
{
  "version": 1,
  "id": "request-id",
  "method": "tools/call",
  "params": {
    "name": "get_page_info",
    "arguments": {}
  }
}
```

Enforce an 8 MiB maximum line size. Match responses by `id`; notifications omit
`id`. Reject malformed JSON and unsupported versions before tool dispatch.

- [ ] **Step 5: Implement `DaoMcpService` state transitions**

Observe the local-state pref and implement:

```cpp
enum class DaoMcpStatus {
  kDisabled,
  kListening,
  kPendingApproval,
  kLeaseActive,
};
```

Start the listener only in enabled states. Accept only the current UID using
`net::UnixDomainServerSocket::Credentials`. Capture verified PID when the
platform provides it. `tools/list` may run before approval; `tools/call` waits
for bounded approval and then uses the external lease.

When `hello` completes, snapshot `BrowserList::GetInstance()->GetLastActive()`,
verify it is a normal eligible Browser, and pass that exact pointer to the
approval delegate. Store only weak Browser/Profile/WebContents references after
approval. Task 6 uses a fake delegate in tests; Task 8 installs the Views-backed
delegate. A missing delegate denies the request instead of auto-approving it.

- [ ] **Step 6: Initialize and shut down with the browser process**

Initialize the singleton from
`ChromeBrowserMainExtraPartsProfiles::PreProfileInit()` after local state is
available. Register shutdown cleanup so no stale endpoint remains after a clean
exit.

- [ ] **Step 7: Compile and test**

Run:

```bash
npm run import
npm run rebuild
engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoMcpServiceBrowserTest.*:DaoMcpProtocolTest.*:DaoMcpRuntimeFilesTest.*"
```

Expected: service and transport tests pass; no endpoint exists after tests that
disable the service.

- [ ] **Step 8: Commit only if explicitly authorized**

```bash
git add src/dao/browser/mcp src/dao/browser/dao_pref_names.cc \
  src/dao/browser/dao_pref_names.h src/dao/browser/ui/dao_ui_sources.gni \
  src/patches/chrome/browser/prefs/browser_prefs.cc.patch \
  src/patches/chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc.patch
git commit -m "feat(mcp): add local browser service"
```

### Task 7: Native `dao-mcp` stdio helper and app bundling

**Files:**
- Create: `src/dao/browser/mcp/helper/main.cc`
- Create: `src/dao/browser/mcp/helper/dao_mcp_stdio_server.{h,cc}`
- Create: `src/dao/browser/mcp/helper/dao_mcp_browser_client.{h,cc}`
- Create: `src/dao/browser/mcp/dao_mcp_helper_browsertest.cc`
- Modify: `src/dao/browser/mcp/BUILD.gn`
- Modify: `src/patches/chrome/BUILD.gn.patch`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

**Interfaces:**
- Consumes: runtime metadata and browser IPC version 1.
- Produces: MCP server identity `{name: "dao-browser", version: <Dao version>}`.
- Produces: MCP methods `initialize`, `notifications/initialized`,
  `tools/list`, `tools/call`, and `notifications/cancelled`.
- Produces: bundled path `Dao.app/Contents/Helpers/dao-mcp`.

- [ ] **Step 1: Add failing helper protocol tests**

Spawn the helper with piped stdin/stdout/stderr. Verify:

- initialize returns protocol version, server info, and tools capability;
- `tools/list` returns 29 tools;
- screenshot maps to MCP `image` content;
- ordinary structured results include serialized text and
  `structuredContent`;
- tool errors set `isError: true`;
- cancelled MCP request sends browser IPC cancellation;
- disabled Dao produces a deterministic stderr diagnostic and MCP tool error;
- every stdout line parses as one JSON-RPC object.

- [ ] **Step 2: Implement newline-delimited MCP stdio framing**

Read one JSON-RPC object per line from stdin. Reject embedded or over-limit
messages. Write one compact JSON object plus `\n` per response. Never use stdout
for logs.

Support the stable protocol version selected during initialize and respond to
unsupported versions with JSON-RPC `-32602`.

- [ ] **Step 3: Map MCP calls to browser IPC**

After initialize, read runtime metadata, connect to Dao, send `hello` with the
nonce and client info, and proxy list/call/cancel requests.

Map normalized browser results as:

```json
{
  "content": [
    {"type": "text", "text": "{\"url\":\"https://example.com\"}"}
  ],
  "structuredContent": {"url": "https://example.com"}
}
```

For screenshots, emit:

```json
{
  "content": [
    {"type": "image", "data": "<base64>", "mimeType": "image/png"}
  ]
}
```

- [ ] **Step 4: Add the helper executable and bundle data**

Create `executable("dao_mcp_helper")` with `output_name = "dao-mcp"`. Create
`bundle_data("dao_mcp_helper_bundle_data")` that copies the output to
`{{bundle_contents_dir}}/Helpers/dao-mcp`. Add that bundle-data target to
`chrome_app` dependencies through `src/patches/chrome/BUILD.gn.patch`. Add
`//dao/browser/mcp:dao_mcp_helper` to `dao_browser_ui_test_deps` so the helper
exists before `DaoMcpHelperBrowserTest` runs.

- [ ] **Step 5: Compile and verify the packaged helper**

Run:

```bash
npm run import
npm run rebuild
test -x "engine/src/out/dao-debug/Dao Debug.app/Contents/Helpers/dao-mcp"
engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoMcpHelperBrowserTest.*"
```

Expected: the helper exists and is executable; all protocol tests pass; stdout
purity assertion passes.

- [ ] **Step 6: Commit only if explicitly authorized**

```bash
git add src/dao/browser/mcp src/dao/browser/ui/dao_ui_sources.gni \
  src/patches/chrome/BUILD.gn.patch
git commit -m "feat(mcp): package stdio helper"
```

### Task 8: Approval dialog, active-control banner, and peer-agent UX

**Files:**
- Create: `src/dao/browser/ui/views/dao_mcp_approval_dialog.{h,cc}`
- Create: `src/dao/browser/ui/views/dao_mcp_control_banner_view.{h,cc}`
- Modify: `src/dao/browser/mcp/dao_mcp_service.{h,cc}`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`
- Modify: `src/patches/chrome/browser/ui/views/frame/browser_view.{h,cc}.patch`
- Modify: `src/patches/chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc.patch`
- Modify: `src/dao/browser/strings/dao_strings.grd`
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

**Interfaces:**
- Consumes: MCP service pending request and active status.
- Produces: approval callback `base::OnceCallback<void(bool)>`.
- Produces: `DaoMcpControlBannerView::SetConnection(client_name, tab_title)`
  and `ClearConnection()`.
- Produces: Stop callback to `DaoMcpService::StopControl()`.

- [ ] **Step 1: Add failing native UI tests**

Test localized labels, client/PID rendering, allow/deny callbacks exactly once,
banner visibility only for the authorized Browser, Stop revocation, and no
banner when the service is merely listening.

- [ ] **Step 2: Add localized approval and status strings**

Add string IDs for:

- approval title, explanation, current-login warning, client label, process
  label, window label, Allow, and Deny;
- connected status title, target label, and Stop;
- busy and revoked feedback.

Do not embed English text directly in the Views classes.

- [ ] **Step 3: Implement the approval dialog**

Use `ConfigureDaoSystemDialog`, show it in the Browser selected when the
connection entered `PENDING_APPROVAL`, and reject non-normal, Incognito, Guest,
DevTools, and popup Browser types before display. Closing the dialog equals
denial.

- [ ] **Step 4: Implement and mount the control banner**

Add the view only to normal BrowserViews. Expose it through `browser_view.h`,
lay it out at the top of Dao content without covering the address bar, and
update it through service observers. Stop releases the external lease, cancels
in-flight external calls, closes the connection, and returns to listening.

- [ ] **Step 5: Show peer-agent contention explicitly**

When external control is active, Dao Agent browser-tool adapter returns
`AGENT_CONTROL_BUSY` rather than invoking CDP. Do not disable chat or imply that
Dao Agent owns the MCP service.

- [ ] **Step 6: Compile and test**

Run:

```bash
npm run import
npm run rebuild
engine/src/out/dao-debug/browser_tests \
  --gtest_filter="DaoMcpApprovalDialogTest.*:DaoMcpControlBannerTest.*:DaoMcpPeerLeaseTest.*"
```

Expected: all UX and contention tests pass.

- [ ] **Step 7: Commit only if explicitly authorized**

```bash
git add src/dao/browser/mcp src/dao/browser/ui/views \
  src/dao/browser/ui/dao_ui_sources.gni \
  src/dao/browser/strings/dao_strings.grd \
  src/patches/chrome/browser/ui/views/frame
git commit -m "feat(mcp): add connection approval ux"
```

### Task 9: Settings master switch and live status

**Files:**
- Create: `src/dao/browser/mcp/dao_mcp_settings_handler.{h,cc}`
- Modify: `src/dao/browser/mcp/dao_mcp_service.{h,cc}`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`
- Modify: `src/patches/chrome/browser/ui/webui/settings/settings_ui.cc.patch`
- Modify: `src/patches/chrome/browser/resources/settings/dao_page/dao_page.{html,ts}.patch`
- Modify: `src/patches/chrome/test/data/webui/settings/dao_page_test.ts.patch`
- Modify: `src/patches/chrome/app/settings_strings.grdp.patch`
- Modify: `src/patches/chrome/browser/ui/webui/settings/settings_localized_strings_provider.cc.patch`
- Modify: localized Dao settings XTB patches only for existing hand-authored
  locales required by the repository's current settings workflow.

**Interfaces:**
- Consumes: `DaoMcpService::GetStatus()`, `SetEnabled()`,
  `GetMcpConfiguration()`, and `StopControl()`.
- Produces WebUI messages: `getDaoMcpStatus`, `setDaoMcpEnabled`,
  `copyDaoMcpConfig`, and `stopDaoMcpControl`.
- Produces event: `dao-mcp-status-changed`.

- [ ] **Step 1: Add failing Settings WebUI tests**

Extend `dao_page_test.ts` to assert:

- master switch defaults off;
- toggling sends the requested state;
- Disabled, Ready, Approval requested, and Connected statuses render;
- client name and PID appear only while connected;
- Copy config and Stop actions call their handlers;
- Stop is hidden unless a lease is active.

- [ ] **Step 2: Implement the Settings handler**

Register it from `SettingsUI`. Return a dictionary shaped as:

```json
{
  "enabled": true,
  "state": "connected",
  "clientName": "Codex",
  "clientPid": 12345,
  "canStop": true
}
```

Copy this configuration using `ui::ScopedClipboardWriter`:

```json
{
  "mcpServers": {
    "dao": {
      "command": "/Applications/Dao.app/Contents/Helpers/dao-mcp"
    }
  }
}
```

Resolve the actual current application bundle path instead of hardcoding
`/Applications` in the returned value.

- [ ] **Step 3: Implement the Settings UI**

Use localized `settings-toggle-button`, status rows, and Cr buttons. Subscribe
on connect and unsubscribe on disconnect. The setting is global local state, so
do not bind it under `prefs.dao`.

- [ ] **Step 4: Add localized strings**

Add every visible label and state to settings GRDP and the Dao localized-string
provider. Do not invoke `i18n.sh`.

- [ ] **Step 5: Import, compile, and test Settings**

Run:

```bash
npm run import
npm run rebuild
npm run test
engine/src/out/dao-debug/browser_tests \
  --gtest_filter="*SettingsDaoPage*:*DaoMcpSettingsHandler*"
```

Expected: Settings tests and handler tests pass.

- [ ] **Step 6: Commit only if explicitly authorized**

```bash
git add src/dao/browser/mcp src/dao/browser/ui/dao_ui_sources.gni \
  src/patches/chrome/browser/resources/settings/dao_page \
  src/patches/chrome/browser/ui/webui/settings \
  src/patches/chrome/app/settings_strings.grdp.patch \
  src/patches/chrome/app/resources/generated_resources_zh-CN.xtb.patch \
  src/patches/chrome/test/data/webui/settings/dao_page_test.ts.patch
git commit -m "feat(settings): add mcp server controls"
```

### Task 10: Eligibility, cancellation, end-to-end regression, and docs

**Files:**
- Create: `src/dao/browser/mcp/dao_mcp_end_to_end_browsertest.cc`
- Modify: `src/dao/browser/mcp/dao_mcp_service.{h,cc}`
- Modify: `src/dao/browser/mcp/dao_mcp_connection.{h,cc}`
- Modify: `src/dao/browser/automation/dao_browser_automation_session.{h,cc}`
- Modify: `src/dao/browser/automation/dao_browser_tool_executor.{h,cc}`
- Modify: `src/dao/browser/automation/dao_page_tools.{h,cc}`
- Modify: `src/dao/browser/mcp/helper/dao_mcp_stdio_server.{h,cc}`
- Modify: `src/dao/browser/mcp/helper/dao_mcp_browser_client.{h,cc}`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`
- Modify: `docs/features.md`
- Modify: `docs/feature-checklist.md`
- Create: `docs/mcp-server.md`
- Modify: `docs/agent-console-api.md`

**Interfaces:**
- Consumes: the complete browser service and helper.
- Produces: documented install/config/use/revoke workflow.
- Produces: final eligibility and cleanup guarantees.

- [ ] **Step 1: Add end-to-end failure-path tests**

Cover:

- helper started while toggle is off;
- approval allow, deny, close, and timeout;
- full initialize/list/call/disconnect;
- cancellation during a CDP command;
- toggle off during an active call;
- authorized window close;
- target tab close;
- focus switching to another Browser;
- second external connection;
- Dao Agent contention;
- HTTP, HTTPS, blank, PDF, Incognito, Guest, internal, extension, DevTools,
  Agent WebUI, and `file://` eligibility;
- no stale runtime file after disable and clean shutdown.

- [ ] **Step 2: Enforce eligible target policy centrally**

Implement one policy function:

```cpp
base::expected<void, DaoToolError> ValidateExternalTarget(
    Browser* browser,
    Profile* profile,
    content::WebContents* contents);
```

Return `TARGET_FORBIDDEN` for every rejected type. Do not search another tab.
Dao Agent keeps its existing target policy.

- [ ] **Step 3: Complete cancellation and cleanup**

On MCP cancellation, helper exit, connection close, user Stop, toggle off,
Browser close, or Profile destruction:

- stop deadlines;
- resolve callbacks exactly once;
- unlock temporary tab input;
- clear highlight/cursor state;
- detach the CDP client;
- release the external lease;
- close the accepted socket;
- notify Settings and the control banner.

- [ ] **Step 4: Document the shipped feature**

Update `docs/features.md` with architecture, Settings location, 29-tool scope,
approval, eligible targets, and peer-agent lease behavior.

Update `docs/feature-checklist.md` with upgrade/regression checks for:

- browser startup and local-state registration;
- Unix socket and helper packaging;
- MCP protocol framing;
- DevTools attachment;
- BrowserView approval/banner layout;
- Settings WebUI;
- tab identity and target rebinding.

Create `docs/mcp-server.md` with enablement, a copyable client config, status
states, security scope, Stop/revoke, errors, and troubleshooting.

Update `docs/agent-console-api.md` so the 30 shared Dao Agent browser tools and
the MCP-only exposure distinction are accurate.

- [ ] **Step 5: Run final verification**

Run:

```bash
npm run test:webui
npm run lint:lit
npm run import
npm run rebuild
npm run test
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoMcp*"
git diff --check
```

Expected:

- all WebUI tests pass;
- Lit lint passes;
- import applies cleanly without force;
- rebuild succeeds;
- broad Dao browser tests pass;
- focused `DaoMcp*` tests pass;
- diff check reports no whitespace errors.

- [ ] **Step 6: Inspect the packaged artifact manually**

With Dao Debug running:

1. Confirm MCP is disabled and no runtime metadata exists.
2. Enable MCP under Settings → Me & Dao.
3. Copy the config and start an MCP client.
4. Confirm the approval card identifies the client and PID.
5. Approve and call `get_page_info`, `list_tabs`, `capture_screenshot`,
   `execute_script`, and one network tool.
6. Confirm focus changes do not move the target.
7. Confirm Dao Agent receives `AGENT_CONTROL_BUSY`.
8. Press Stop and confirm the external client loses access.
9. Disable MCP and confirm the helper returns `MCP_DISABLED`.

- [ ] **Step 7: Commit only if explicitly authorized**

```bash
git add docs/features.md docs/feature-checklist.md docs/mcp-server.md \
  docs/agent-console-api.md
git diff --cached --name-only
git diff --cached --check
git commit -m "docs(mcp): document browser tool server"
```

Before committing, verify the staged list contains only MCP feature files and
the approved documentation. Do not include unrelated worktree changes.

## Implementation References

- Design: `docs/dao-mcp-server-design.md`
- Existing schemas and adapter: `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
- Existing native CDP bridge: `src/dao/browser/ui/webui/dao_agent_ui.{h,cc}`
- Existing tool groups: `src/dao/browser/ui/webui/resources/agent/tool_catalog.ts`
- Existing stable tab identity: `src/dao/browser/ui/views/dao_tab_identity.{h,cc}`
- Existing native dialog styling: `src/dao/browser/ui/views/dao_system_dialog.{h,cc}`
- Existing Settings page patches:
  `src/patches/chrome/browser/resources/settings/dao_page/`
- MCP stdio transport:
  `https://modelcontextprotocol.io/specification/2025-11-25/basic/transports`
- MCP tools:
  `https://modelcontextprotocol.io/specification/2025-11-25/server/tools`
