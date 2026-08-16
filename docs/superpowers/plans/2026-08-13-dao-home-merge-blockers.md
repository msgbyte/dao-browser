# Dao Home Merge Blockers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make draft publication depend on a real isolated preview, show resource-budget expansion in the trusted permission dialog, and keep successful connector caching within the native executor retention bound.

**Architecture:** The active trusted Home host owns a temporary sandboxed preview frame and reports a draft-scoped load receipt to the Profile service; Agent publication consumes that receipt. Permission requests carry current and requested limits so trusted UI can render the security-relevant diff. ConnectorHost separates in-flight coalescing from a bounded completed-result cache whose size matches native completed-executor retention.

**Tech Stack:** Chromium C++, Profile-keyed services, `chrome-untrusted` WebUI, Lit/TypeScript, Vitest, Chromium browser tests.

## Global Constraints

- Work in the existing primary checkout on `main`; do not create a branch or worktree.
- Edit canonical files only; never edit `engine/` directly.
- Do not run state-changing Git commands or `i18n.sh`.
- Add English and hand-authored `zh-CN` strings for every new trusted-host label.
- Write and observe a focused failing test before each production change.
- Use only `npm run rebuild` for compile confirmation.

---

### Task 1: Bounded connector result cache

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/home/connector_host.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/__tests__/connector_host.test.ts`

**Interfaces:**
- Produces: at most 16 settled successful collection values retained per active Home session.
- Preserves: concurrent identical requests share one in-flight Promise; failed requests are never cached.

- [x] Add a failing test that completes 17 unique collections, requests the first key again, and expects a new native start.
- [x] Run `npx vitest run src/dao/browser/ui/webui/resources/home/__tests__/connector_host.test.ts` and confirm the test fails because the first settled result is still cached.
- [x] Split `collections_` into in-flight and completed maps and evict the oldest completed value after the sixteenth entry:

```ts
const MAX_COMPLETED_COLLECTIONS = 16;
private readonly inFlight_ = new Map<string, Promise<unknown>>();
private readonly completed_ = new Map<string, unknown>();
```

- [x] Re-run the focused test and confirm it passes.

### Task 2: Visible budget expansion

**Files:**
- Modify: `src/dao/browser/home/dao_home_types.h`
- Modify: `src/dao/browser/home/dao_home_project_store.cc`
- Modify: `src/dao/browser/home/dao_home_project_service.cc`
- Modify: `src/dao/browser/ui/webui/dao_home_ui.cc`
- Modify: `src/dao/browser/ui/webui/resources/home/home_bridge.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/dao_home_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts`
- Modify: `src/dao/browser/strings/dao_strings.grd`
- Modify: `src/dao/browser/strings/translations/dao_strings_zh-CN.xtb`

**Interfaces:**
- `HomeDraft::previous_limits` is empty for a new project and otherwise stores the published limits used for diff classification.
- `HomePermissionRequest::{previous_limits,requested_limits}` binds the visible budget scope to the exact draft approval.

- [x] Add a failing trusted-host test whose permission request changes only `max_result_bytes` and assert the dialog displays both the old and new values.
- [x] Run the focused Home app test and confirm the budget text is absent.
- [x] Propagate the previous/requested limits through draft registration, permission request serialization, and `HomePermissionRequest` TypeScript.
- [x] Render only increased dimensions with localized labels; render requested maxima for a connector with no previous project.
- [x] Re-run the focused test and the existing budget-aware store test.

### Task 3: Isolated draft preview receipt

**Files:**
- Modify: `src/dao/browser/home/dao_home_project_store.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_project_service.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_agent_tools.{h,cc}`
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc`
- Modify: `src/dao/browser/ui/webui/dao_home_ui.{h,cc}`
- Modify: `src/dao/browser/ui/webui/resources/home/home_bridge.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/generated_runtime.ts`
- Add: `src/dao/browser/ui/webui/resources/home/preview_bootstrap.js`
- Modify: `src/dao/browser/ui/webui/resources/home/dao_home_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts`
- Modify: `src/dao/browser/home/dao_home_browsertest.cc`

**Interfaces:**
- `ReadDraftFile(draft_id, relative_path)` serves validated draft resources only through `/preview/<draft-id>/<path>`.
- `DaoHomeUI::PreviewDraftForAgent(draft_id, callback)` owns one hidden sandboxed frame, a timeout, and exact-frame message validation.
- `runtime.previewReady` is emitted after window load; `runtime.report` fails preview before publication can receive a draft-scoped receipt.
- `DaoHomeProjectService::MarkDraftPreviewed` records an in-memory receipt for one immutable draft; Agent publication rejects drafts without it.

- [x] Add failing WebUI tests proving a preview request creates a draft URL, runtime failure returns an error, and runtime ready returns success.
- [x] Add a failing native test proving an unpreviewed Agent publication path is rejected while the exact previewed draft is accepted.
- [x] Run focused tests and confirm RED.
- [x] Add draft resource reads and preview URL routing without exposing WebUI bindings.
- [x] Add the trusted preview request/response lifecycle and Agent preview runner.
- [x] Require the exact draft preview receipt in both regular and permission-expanding Agent publication paths.
- [x] Re-run focused WebUI tests, rebuild, and run the focused `*DaoHome*` browser tests.

### Task 4: Documentation and verification

**Files:**
- Modify: `docs/features.md`
- Modify: `docs/feature-checklist.md`

- [x] Document isolated preview receipts, visible budget diffs, and the 16-entry completed collection cache.
- [x] Run `npm run test:webui`, `npm run lint:lit`, `npm run typecheck`, `npm run docs:check`, `npm run vendor:check`, and `git diff --check`.
- [x] Run `npm run import`, `npm run rebuild`, and `engine/src/out/dao-debug/browser_tests --gtest_filter='*DaoHome*'`.

No commit step is included because the user did not authorize state-changing Git operations.
