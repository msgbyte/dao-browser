# Dao Home Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the security, lifecycle, publication, connector, settings, and version-portability gaps found in the Dao Home implementation review.

**Architecture:** Keep generated code in the existing untrusted frames, but move every durable or externally visible effect behind trusted state machines. Source permission becomes a draft-scoped approval that must be exercised successfully before the same draft can atomically publish and persist grants; generated sessions are destroyed on visibility loss; completed connector executors are retained only for the bounded Home session so media handles remain usable.

**Tech Stack:** Chromium C++, Profile keyed services, `content::WebContents`, Lit/TypeScript, Vitest, Chromium browser tests, JSON revision packages.

## Global Constraints

- Edit only canonical tracked sources and patches; never edit `engine/` directly.
- Do not run `i18n.sh`, direct Chromium build tools, or any state-changing Git command.
- User-visible trusted-host copy must use `dao_strings.grd` and hand-authored `zh-CN` translations.
- Every behavior change starts with a failing focused test and ends with its focused test passing.
- Compile confirmation uses only `npm run rebuild` after all C++ edits are batched.

---

### Task 1: Trusted navigation and active-session teardown

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/home/dao_home_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/generated_runtime.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/__tests__/dao_home_app.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/__tests__/generated_runtime.test.ts`
- Modify: `src/dao/browser/strings/dao_strings.grd`
- Modify: `src/dao/browser/strings/translations/dao_strings_zh-CN.xtb`

**Interfaces:**
- `navigation.open` creates a trusted confirmation request and resolves only after the user confirms or cancels.
- `visibilitychange` removes both untrusted frames, disconnects the connector host, cancels native session work, and recreates a fresh session only when visible again.

- [ ] Add failing WebUI tests proving automatic navigation does not call native navigation before confirmation and tab visibility loss removes the generated frame/session.
- [ ] Run the focused Home WebUI tests and confirm the new assertions fail for the missing trusted state.
- [ ] Implement localized navigation confirmation and visibility-owned frame lifecycle.
- [ ] Re-run the focused tests and confirm they pass.

### Task 2: Budget-aware grants and draft-scoped source approval

**Files:**
- Modify: `src/dao/browser/home/dao_home_types.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_manifest.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_project_store.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_project_service.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_agent_tools.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_project_store_unittest.cc`

**Interfaces:**
- Grant fingerprints include connector permissions plus `HomeLimits`.
- `HomeDraft::permission_expanded_connector_ids` identifies every connector requiring approval.
- Trusted approval records an in-memory approval for one exact draft; successful draft collection marks it tested; only that exact tested draft can publish with all grants atomically.

- [ ] Add failing store tests for budget increases, budget decreases, and multiple expanded connectors.
- [ ] Run the focused store tests and confirm RED.
- [ ] Implement budget-aware classification/fingerprints and atomic multi-grant publication.
- [ ] Re-run the store tests and confirm GREEN.

### Task 3: Execute unpublished connectors before publication

**Files:**
- Modify: `src/dao/browser/home/dao_home_project_store.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_project_service.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_agent_tools.{h,cc}`
- Modify: `src/dao/browser/ui/webui/dao_home_ui.{h,cc}`
- Modify: `src/dao/browser/ui/webui/resources/home/home_bridge.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/connector_host.ts`
- Modify: `src/dao/browser/ui/webui/resources/home/dao_home_app.ts`
- Modify: focused native and WebUI tests.

**Interfaces:**
- `GetDraftConnectorBundle(draft_id, connector_id)` reads module/schema from the validated draft.
- `startHomeDraftConnector` starts only when the exact draft scope has trusted approval.
- `home_test_connector` returns a schema-validated sample and marks the exact draft tested; `home_publish` consumes that approval for atomic publish+grant.

- [ ] Add failing connector-host and store/service tests for unpublished bundle execution and publish-before-test rejection.
- [ ] Confirm the tests fail against the current publish-first flow.
- [ ] Implement the draft connector bridge and approval/test/publish state machine.
- [ ] Confirm focused tests pass.

### Task 4: Session connector retention and real `waitFor`

**Files:**
- Modify: `src/dao/browser/home/dao_home_connector_executor.{h,cc}`
- Modify: `src/dao/browser/ui/webui/dao_home_ui.{h,cc}`
- Modify: `src/dao/browser/ui/webui/resources/home/connector_host.ts`
- Modify: `src/dao/browser/home/dao_home_browsertest.cc`
- Modify: `src/dao/browser/ui/webui/resources/home/__tests__/connector_host.test.ts`

**Interfaces:**
- The trusted handler owns a bounded map of execution IDs to executors until Home session cancellation, preserving media handles from completed collections.
- Successful duplicate connector/input collections remain cached for the active session.
- `page.waitFor(selector, timeout?)` observes DOM mutations for at most five seconds and resolves `true` only when the selector appears.

- [ ] Add failing tests for two connector results retaining independent handles, successful session coalescing, and delayed DOM appearance.
- [ ] Confirm RED with focused WebUI/native tests.
- [ ] Implement bounded executor retention, success caching, and MutationObserver-backed waiting.
- [ ] Confirm GREEN with focused tests.

### Task 5: Complete Home tool controls

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/tool_catalog.ts`
- Modify: `src/patches/chrome/browser/resources/settings/dao_page/dao_agent_settings_browser_proxy.ts.patch`
- Modify: `src/patches/chrome/browser/resources/settings/dao_page/dao_agent_page.{ts,html}.patch`
- Modify: Agent and Settings WebUI tests.

**Interfaces:**
- Settings exposes every Home tool, including `home_add_asset`, through the persisted `dao_disabled_tools` set.
- A Home group toggle updates all Home tool names, while individual toggles remain available.

- [ ] Add failing catalog/settings tests that enumerate the complete Home tool set.
- [ ] Confirm RED.
- [ ] Add the missing tool and Settings Home group/toggle with localized Chromium Settings copy.
- [ ] Confirm focused WebUI and settings contract tests pass.

### Task 6: Portable version history

**Files:**
- Modify: `src/dao/browser/home/dao_home_project_store.{h,cc}`
- Modify: `src/dao/browser/home/dao_home_project_store_unittest.cc`

**Interfaces:**
- Export includes bounded encoded files for every exported revision, plus validated metadata.
- Import validates the full revision graph and contents in temporary directories, remaps imported revision IDs, preserves local history, installs imported historical revisions, and creates a new local import head without grants.

- [ ] Add a failing round-trip test proving two imported historical revisions remain readable and rollback-capable.
- [ ] Confirm RED.
- [ ] Implement bounded revision-content export and transactional history import.
- [ ] Confirm GREEN.

### Task 7: Documentation and full verification

**Files:**
- Modify: `docs/features.md`
- Modify: `docs/feature-checklist.md`

- [ ] Update feature inventory and regression checklist with trusted navigation confirmation, visibility teardown, draft test-before-publish, budget grants, retained media handles, and version round-trip.
- [ ] Run focused WebUI tests and Home browser/unit filters.
- [ ] Run `npm run test:webui`, `npm run lint:lit`, `npm run typecheck`, `npm run docs:check`, `npm run vendor:check`, and `git diff --check`.
- [ ] Run `npm run import`, then `npm run rebuild`, then the focused `DaoHome*` browser tests.
