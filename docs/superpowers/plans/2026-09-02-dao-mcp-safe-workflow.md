# Dao MCP Safe Workflow P0 Implementation Plan

> **For Codex:** Execute this plan in the primary checkout. Repository policy forbids worktrees and does not authorize git commits.

**Goal:** Add a compact, safe workflow for scoped element lookup, guarded ref clicks, and terminal network-response waits.

**Architecture:** Reuse the existing page `Runtime.evaluate` and DevTools network event pipeline. Page queries create document-bound snapshots; clicks validate the snapshot and element preconditions in the same renderer evaluation that resolves click bounds. Network capture assigns a document-local monotonic cursor; waits filter completed responses, parse JSON bodies, evaluate one simple path condition, and return only selected paths.

**Tech Stack:** Chromium C++, CDP Runtime/Network domains, Lit/TypeScript tool catalog, browser tests, Vitest.

---

### Task 1: Lock the public tool contract with a failing catalog test

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/browser_tool_catalog.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json`

- [ ] Expect `query_elements` and `wait_for_network_response` in the MCP catalog.
- [ ] Assert the guarded click, query scope, cursor, condition, and projection schemas.
- [ ] Run the focused Vitest file and observe the expected failure before implementation.

### Task 2: Implement document-bound scoped queries and guarded clicks

**Files:**
- Modify: `src/dao/browser/automation/dao_page_tools.h`
- Modify: `src/dao/browser/automation/dao_page_tools.cc`
- Modify: `src/dao/browser/automation/dao_page_tools_browsertest.cc`

- [ ] Add `query_elements` using the existing semantic DOM helpers.
- [ ] Return `document_id`, `snapshot_id`, compact matches, count, and truncation state.
- [ ] Record only the latest snapshot per target/document.
- [ ] Require `click_by_ref` to match the document/snapshot and validate visible/enabled/text/role/ancestor preconditions atomically with bounds resolution.
- [ ] Add focused browser tests for scoped selection, successful click, stale snapshot rejection, and failed preconditions.

### Task 3: Implement cursor-based terminal network waits

**Files:**
- Modify: `src/dao/browser/automation/dao_browser_automation_session.h`
- Modify: `src/dao/browser/automation/dao_devtools_tools.h`
- Modify: `src/dao/browser/automation/dao_devtools_tools.cc`
- Modify: `src/dao/browser/automation/dao_devtools_tools_browsertest.cc`

- [ ] Assign a monotonic cursor to captured network events and return it from tracking/list calls.
- [ ] Capture completion events and add `wait_for_network_response` with URL/method/status filters.
- [ ] Evaluate a simple JSON path condition after completion and return selected JSON paths only.
- [ ] Respect timeout/cancellation and existing target/document validation.
- [ ] Add focused browser tests for cursor handoff, terminal match/projection, and timeout or non-match behavior.

### Task 4: Wire both Dao Agent and MCP surfaces

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/tool_catalog.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/pi_tool_adapter.ts`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.h`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc`
- Modify: `src/patches/chrome/browser/resources/settings/dao_page/dao_agent_settings_browser_proxy.ts.patch`

- [ ] Forward the new tools and guarded click arguments through the existing WebUI message handlers.
- [ ] Add the tools to the settings proxy and Agent tool groups.
- [ ] Keep MCP and Dao Agent backed by the same catalog and native executors.

### Task 5: Update feature inventory and regression checklist

**Files:**
- Modify: `docs/features.md`
- Modify: `docs/feature-checklist.md`
- Modify: `docs/agent-console-api.md`

- [ ] Document the 31-tool MCP / 32-tool native catalog and recommended composed workflow.
- [ ] Add scoped-query, stale-snapshot, guarded-click, cursor, wait, and projection checks.

### Task 6: Verify the smallest relevant surfaces

- [ ] Run the focused catalog Vitest file.
- [ ] Run `npm run test:webui` and `npm run lint:lit` for WebUI coverage.
- [ ] Run `npm run rebuild` as the only compile-confirmation command.
- [ ] Run only the new page/network browser-test filters.
- [ ] Review `git diff --check`, `git diff --stat`, and `git status` without staging or committing.
