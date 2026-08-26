# Dao MCP Multi-Client Tab Leases Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let up to 32 local MCP clients stay connected concurrently while serializing browser mutation per stable tab rather than per Profile.

**Architecture:** Keep the existing authenticated Unix-socket listener and per-tab automation sessions. Replace the transport's single connection fields with a bounded map keyed by connection generation, move the service's mutable protocol/session state into one context per connection, and key the shared Agent/MCP lease manager by `tabs::TabHandle`. Approval remains one explicit decision per connection and the native dialog is queued because its controller is single-dialog.

**Tech Stack:** Chromium C++, `base::expected`, `base::flat_map`/`std::map`, `tabs::TabHandle`, browser tests, gtest.

**Spec:** `docs/superpowers/specs/2026-08-26-dao-mcp-multi-client-tab-leases-design.md`

**Repository constraints:** Work only in tracked Dao sources and patches, preserve pre-existing dirty changes, do not run state-changing git commands, do not edit `engine/`, and use only `npm run rebuild` for compile confirmation.

---

## Task 1: Scope the shared automation lease to a stable tab

**Files:**
- Modify: `src/dao/browser/automation/dao_agent_lease_manager.h`
- Modify: `src/dao/browser/automation/dao_agent_lease_manager.cc`
- Modify: `src/dao/browser/automation/dao_browser_automation_session.h`
- Modify: `src/dao/browser/automation/dao_browser_automation_session.cc`
- Test: `src/dao/browser/mcp/dao_mcp_foundation_unittest.cc`
- Modify callers found by: `rg -n "TryAcquire\\(" src/dao --glob '!**/vendor/**'`

- [ ] Add failing unit coverage proving same-tab conflict, different-tab coexistence, and independent RAII release:

```cpp
TEST(DaoMcpLeaseTest, AllowsDifferentTabsButRejectsSameTab) {
  DaoAgentLeaseManager leases;
  auto first = leases.TryAcquire(tabs::TabHandle(1),
                                 {DaoToolClient::kMcp, "one", "Codex"});
  ASSERT_TRUE(first.has_value());
  EXPECT_TRUE(leases.TryAcquire(tabs::TabHandle(2),
                                {DaoToolClient::kMcp, "two", "Codex"}));
  EXPECT_EQ(DaoToolErrorCode::kLeaseBusy,
            leases.TryAcquire(tabs::TabHandle(1),
                              {DaoToolClient::kMcp, "three", "Codex"})
                .error().code);
}
```

- [ ] Run the focused test and confirm it fails before implementation:

```bash
npm run test -- --gtest_filter='DaoMcpLeaseTest.*'
```

- [ ] Change the manager API and RAII token to carry the target handle:

```cpp
base::expected<DaoAgentLease, DaoToolError> TryAcquire(
    tabs::TabHandle target_handle,
    DaoAgentClientId client);

struct LeaseState {
  uint64_t lease_id;
  DaoAgentClientId owner;
};
std::map<tabs::TabHandle, LeaseState> leases_;
```

- [ ] Expose `tabs::TabHandle target_handle() const` on `DaoBrowserAutomationSession` and update every production/test caller to pass the intended stable target.

- [ ] Re-run `DaoMcpLeaseTest.*` and the smallest Agent lease browser filters touched by caller updates.

## Task 2: Admit up to 32 authenticated transport connections

**Files:**
- Modify: `src/dao/browser/mcp/dao_mcp_transport.h`
- Modify: `src/dao/browser/mcp/dao_mcp_transport.cc`
- Test: `src/dao/browser/mcp/dao_mcp_service_browsertest.cc`

- [ ] Replace `RejectsConcurrentExternalConnection` with a failing test where two clients both complete `hello`, plus a boundary test where the 33rd socket is rejected without disturbing the first 32.

- [ ] Replace the singleton transport fields with exactly one small state object per generation:

```cpp
static constexpr size_t kMaxConnections = 32;
struct ConnectionState {
  std::unique_ptr<DaoMcpConnection> connection;
  size_t pending_request_count = 0;
  size_t pending_request_bytes = 0;
  bool closing = false;
  base::OneShotTimer drain_timer;
};
std::map<uint64_t, std::unique_ptr<ConnectionState>> connections_;
```

- [ ] Route send, acknowledgement, close, pending-ingress limits, and disconnect cleanup through the matching generation; keep each connection's existing 64-request/8 MiB and drain limits unchanged.

- [ ] Run the two focused multi-connection admission tests.

## Task 3: Isolate protocol, approval, target, and call state per client

**Files:**
- Modify: `src/dao/browser/mcp/dao_mcp_service.h`
- Modify: `src/dao/browser/mcp/dao_mcp_service.cc`
- Test: `src/dao/browser/mcp/dao_mcp_service_browsertest.cc`

- [ ] Add failing browser tests proving: two clients can approve against different tabs and execute concurrently; a second client receives `LEASE_BUSY` on an already leased tab; disconnecting either client leaves the other usable; approval requests are presented one at a time.

- [ ] Move current singleton connection fields into `ConnectionContext` keyed by transport generation. Keep `TargetContext` as the existing per-tab execution unit and store its `DaoAgentLease` there:

```cpp
struct ConnectionContext {
  ApprovalState approval_state = ApprovalState::kNotRequested;
  std::optional<DaoMcpClientInfo> client_info;
  base::WeakPtr<BrowserWindowInterface> approved_window;
  std::map<std::string, std::unique_ptr<TargetContext>, std::less<>> targets;
  std::string default_target_id;
  // Existing pending/active calls, timers, tab executor, and tab sessions.
};
std::map<uint64_t, std::unique_ptr<ConnectionContext>> connections_;
```

- [ ] Bind every asynchronous callback to its generation and look up the context again before mutation; request IDs remain unique only inside that context.

- [ ] Queue connection generations before calling the existing single-dialog `DaoMcpApprovalDelegate`; start the next approval after allow, deny, timeout, cancellation, or disconnect.

- [ ] Acquire a target lease before Page/DevTools dispatch. Let `list_tabs` run after approval without a target lease. Before `switch_tab`, `open_tab`, or `close_tab`, validate/acquire the destination or reject with the existing busy error; never activate or close another owner's tab.

- [ ] Preserve the approved window after target loss, release only the lost target's lease/state, and allow `list_tabs` followed by `switch_tab`/`open_tab` to recover the same connection.

- [ ] Run the new multi-client, contention, approval-queue, and target-recovery filters.

## Task 4: Report aggregate and per-target control without breaking existing UI

**Files:**
- Modify: `src/dao/browser/mcp/dao_mcp_service.h`
- Modify: `src/dao/browser/mcp/dao_mcp_service.cc`
- Modify if required by the focused tests: `src/dao/browser/ui/views/dao_mcp_control_banner_view.h`
- Modify if required by the focused tests: `src/dao/browser/ui/views/dao_mcp_control_banner_view.cc`
- Test: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] Make aggregate status `kLeaseActive` when any connection owns a target and `kPendingApproval` when none is active but at least one is pending. Resolve the active tab's owning client for the address-bar indicator.

- [ ] Keep the current popup compact: show the active tab's owner and that client's controlled-tab count, and make Stop close only that owning connection. Do not add new protocol or settings-page UI unless a test proves it is required.

- [ ] Run the focused MCP indicator/popup/Stop browser tests.

## Task 5: Synchronize documentation and verify the canonical tree

**Files:**
- Modify: `docs/features.md`
- Modify: `docs/feature-checklist.md`

- [ ] Update the Local MCP inventory from one-client/Profile-wide lease wording to 32 authenticated clients, queued per-connection approval, stable per-tab leases, same-tab contention, and target-loss recovery.

- [ ] Run canonical import:

```bash
npm run import
```

- [ ] Run the smallest relevant unit/browser filters recorded above, then compile once:

```bash
npm run rebuild
```

- [ ] Review `git diff --check`, `git status --short`, and the final scoped diff. Do not commit or push.
