# Dao Memory SQL Browser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `dao://memory`, a standalone read-only SQL browser for the Agent's local SQLite memory database.

**Architecture:** Add a safe read-only query path to `DaoAgentMemoryStore` and expose it through `DaoAgentMemoryService`. Add a `DaoMemoryUI` WebUI controller and a small `DaoMemoryBrowserHandler` that serves table metadata and query results to a Lit-based page under the existing agent WebUI resource bundle.

**Tech Stack:** Chromium C++ WebUI, `sql::Database`, `base::Value`, Lit/TypeScript, existing Dao agent WebUI build pipeline.

---

### Task 1: Read-Only Query Core

**Files:**
- Modify: `src/dao/browser/agent/dao_agent_memory_types.h`
- Modify: `src/dao/browser/agent/dao_agent_memory_store.h`
- Modify: `src/dao/browser/agent/dao_agent_memory_store.cc`
- Modify: `src/dao/browser/agent/dao_agent_memory_service.h`
- Modify: `src/dao/browser/agent/dao_agent_memory_service.cc`
- Test: `src/dao/browser/agent/dao_agent_memory_store_unittest.cc`
- Modify: `src/dao/browser/agent/BUILD.gn`

- [ ] **Step 1: Write failing store tests**

Add tests that create an in-memory-temp `DaoAgentMemoryStore`, save a preference, run `SELECT key, value FROM preferences`, and assert the returned columns and rows. Add separate tests that reject `DELETE FROM preferences`, `SELECT 1; SELECT 2`, and `ATTACH DATABASE '/tmp/x' AS x`.

- [ ] **Step 2: Run the focused test and verify red**

Run: `npm run test -- --gtest_filter=DaoAgentMemoryStoreTest.ReadOnlySql*`

Expected: FAIL because `dao_agent_memory_store_unittest.cc` is new and the `ExecuteReadOnlySqlForDebug` API does not exist yet.

- [x] **Step 3: Add query result types**

Add `MemorySqlCell` and `MemorySqlQueryResult` to `dao_agent_memory_types.h`. Cells carry a string `value` and a `type` string so the WebUI can display NULL distinctly.

- [ ] **Step 4: Implement SQL guard and execution**

Add `DaoAgentMemoryStore::ExecuteReadOnlySqlForDebug(std::string sql, int max_rows)`. Trim whitespace, reject empty SQL, reject multiple statements, allow only `SELECT`, `WITH`, and safe `PRAGMA table_info|table_list|index_list|index_info|database_list`, and reject tokens such as `ATTACH`, `DETACH`, `INSERT`, `UPDATE`, `DELETE`, `DROP`, `ALTER`, `CREATE`, `REPLACE`, `VACUUM`, `REINDEX`, and `PRAGMA writable_schema`. Execute with `GetReadonlyStatement`, collect column names and up to `max_rows` rows, and report `truncated` when the limit is hit.

- [ ] **Step 5: Expose service async wrapper**

Add `DaoAgentMemoryService::ExecuteReadOnlySqlForDebug(sql, max_rows, callback)` and post to the existing background task runner.

- [ ] **Step 6: Run focused test and verify green**

Run: `npm run test -- --gtest_filter=DaoAgentMemoryStoreTest.ReadOnlySql*`

Expected: PASS.

### Task 2: WebUI Controller And Handler

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.h`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc`
- Modify: `src/patches/chrome/browser/ui/webui/chrome_web_ui_configs.cc.patch`

- [ ] **Step 1: Add `DaoMemoryUIConfig` and `DaoMemoryUI`**

Register a new WebUI config for host `memory`, create a data source named `memory`, reuse `kDaoAgentResources`, and set the default resource to `IDR_DAO_AGENT_MEMORY_HTML`.

- [ ] **Step 2: Add `DaoMemoryBrowserHandler`**

Register `memoryGetTables` and `memoryExecuteSql` chrome.send callbacks. `memoryGetTables` should run a fixed safe query against `sqlite_schema` and return table/view names. `memoryExecuteSql` should accept `{sql, maxRows}`, clamp rows to 1-500, call the service API, and serialize the result to `{ok, columns, rows, truncated, error}`.

- [ ] **Step 3: Wire Chromium config patch**

Add `map.AddWebUIConfig(std::make_unique<dao::DaoMemoryUIConfig>());` beside the existing Dao WebUI registrations.

### Task 3: Frontend Page

**Files:**
- Create: `src/dao/browser/ui/webui/resources/agent/memory.html`
- Create: `src/dao/browser/ui/webui/resources/agent/memory.ts`
- Create: `src/dao/browser/ui/webui/resources/agent/dao_memory_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/BUILD.gn`

- [ ] **Step 1: Add static HTML and entrypoint**

Create `memory.html` that loads `memory.js`, and `memory.ts` that installs the Trusted Types policy and imports `dao_memory_app.js`.

- [ ] **Step 2: Build the Lit app**

Create `<dao-memory-app>` with a dense debugger layout: sidebar table list, SQL editor textarea, run button, status strip, and horizontally scrollable result table. Default SQL should be `SELECT name, type FROM sqlite_schema WHERE type IN ('table', 'view') ORDER BY type, name`.

- [ ] **Step 3: Add native bridge**

Use the existing chrome.send callback pattern locally in `dao_memory_app.ts`. On load, fetch tables and run the default query. Clicking a table should populate `SELECT * FROM <table> LIMIT 100` with quoted identifiers.

- [ ] **Step 4: Register resources**

Add `memory.html`, `memory.ts`, and `dao_memory_app.ts` to `resources/agent/BUILD.gn` so grit creates `IDR_DAO_AGENT_MEMORY_HTML` and compiled JS.

### Task 4: Verification

**Files:**
- All touched files above.

- [ ] **Step 1: Run WebUI checks**

Run: `npm run test:webui`

Expected: PASS.

- [ ] **Step 2: Run Lit lint**

Run: `npm run lint:lit`

Expected: PASS.

- [ ] **Step 3: Compile confirmation**

Run: `npm run rebuild`

Expected: PASS. This is the only allowed compile-confirmation command for this repository.

- [ ] **Step 4: Inspect diff**

Run: `git diff --stat` and `git diff -- src/dao/browser/agent src/dao/browser/ui/webui src/dao/browser/ui/webui/resources/agent src/patches/chrome/browser/ui/webui/chrome_web_ui_configs.cc.patch docs/superpowers/plans/2026-06-13-dao-memory.md`

Expected: Only files related to `dao://memory` changed.
