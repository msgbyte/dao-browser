# Sidebar Context Menu Shortcuts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Display the existing Duplicate Tab, Copy Link, and Close Tab shortcuts in the native sidebar tab context menu.

**Architecture:** Extend the existing `ui::SimpleMenuModel::Delegate` implementation to map sidebar commands to canonical browser command IDs. Resolve accelerators through the browser's provider so menu display cannot drift from registered platform shortcuts.

**Tech Stack:** Chromium C++, Views `SimpleMenuModel`, Vitest source-contract tests.

## Global Constraints

- Keep canonical changes under `src/dao/`; do not edit `engine/` directly.
- Do not hardcode shortcut glyphs into localized labels.
- Use only `npm run rebuild` for compile confirmation.
- Do not run state-changing git commands.

---

### Task 1: Native sidebar context-menu accelerators

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.h`
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.cc`
- Test: `src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts`
- Modify: `docs/features.md`
- Modify: `docs/feature-checklist.md`

**Interfaces:**
- Consumes: `AcceleratorProviderForBrowser(Browser*)` and existing `IDC_DAO_DUPLICATE_TAB`, `IDC_DAO_COPY_URL`, and `IDC_CLOSE_TAB` registrations.
- Produces: `DaoSidebarUIHandler::GetAcceleratorForCommandId(int, ui::Accelerator*) const`.

- [ ] Add a failing source-contract test requiring all three sidebar-to-browser command mappings.
- [ ] Run the focused Vitest test and confirm it fails because the override is absent.
- [ ] Implement the delegate override using the browser accelerator provider.
- [ ] Update the feature catalog and sidebar regression checklist.
- [ ] Run the focused test, import canonical sources, run `npm run rebuild`, and inspect the final diff.
