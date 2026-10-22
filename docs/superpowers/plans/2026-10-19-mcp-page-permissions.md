# MCP Page Permissions Implementation Plan

**Spec:** `../specs/2026-10-19-mcp-page-permissions-design.md`

**Execution:** Core implementation with delegated UI, regression tests, and
documentation work, followed by an independent final review. No Git mutations,
new worktree, or branch changes.

## 1. Separate window capability from page grants

- Add connection-owned window/profile lifetime independent of target contexts.
- Keep the initial explicit-tab approval path; default approval grants creation
  only, including when the current page is internal.
- Filter tab enumeration and guard switch/close/page routing with a live
  connection-scoped grant check. Open new tabs in the background.
- Add focused browser regression coverage for empty grants, existing-page
  denial, and zero-page creation.

## 2. Add explicit page approval and revocation

- Add the MCP-only `request_tab_access` tool with a reason and optional tab ID.
- Present a native eligible-page picker when no ID is supplied; snapshot and
  revalidate the selected target before granting it.
- Bound and cancel secondary approvals independently of existing grants.
- Revoke one page by explicit connection/tab IDs, cancel work and release all
  page state, preserve the tab and other grants, and clear default routing.
- Cover denial, cancellation, stale approval, close/revoke, and reacquisition.

## 3. Connect native management and guidance

- Expose connection/page snapshots and explicit stop/revoke APIs.
- Show all connections and their granted pages in the existing MCP popup;
  retain a reachable management entry with zero pages.
- Update helper instructions, native English/Simplified Chinese strings,
  feature inventory/checklist, MCP documentation, and both changelogs.

## 4. Verify and review

- Run the smallest available related tests and `git diff --check`.
- Run `npm run rebuild` as the only compile-confirmation path.
- Independently review grant checks, callback races, zero-page lifetimes,
  multi-client identities, Agent compatibility, and changed test assumptions.
- Record actual verification and remaining environment limits. Do not claim
  browser tests passed without an executable and observed passing results.

## Verification record

- Implemented all four work areas and completed independent static review.
- WebUI excluding `website/**`: 95 test files, 1,215 tests passed; 7 skipped.
- Lit validation: passed for 229 files. Documentation checks passed.
- Full WebUI execution is blocked in two website suites by missing
  `next/server` and `react-dom/client` dependencies.
- `npm run rebuild` was attempted and stopped because `engine/src` is absent.
  Native compilation and Chromium browser regression tests remain unverified.
- Revocation rejects an already-visible close confirmation, but cannot roll back
  actions already dispatched to the renderer. Strict close rollback would need
  a Chromium close-token integration beyond the existing public dialog API.
