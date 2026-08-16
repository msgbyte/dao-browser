# Element Context Picker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a WebContents element picker that stores one selected element as reusable Agent context.

**Architecture:** Implement the picker in `dao_page_capture.ts` using the existing `executeScript` bridge. Store the selected context in `DaoChatView`, render it as a chip above the composer, and append an `<element-context>` attachment on every normal send until dismissed.

**Tech Stack:** Lit TypeScript WebUI, Vitest, existing `callNative('executeScript')` bridge, existing pi-web-ui attachment shape.

---

### Task 1: Element Context Capture Helpers

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_page_capture.ts`
- Test: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_page_capture.test.ts`

- [ ] **Step 1: Write failing tests**

Add tests that import `buildElementContextAttachment`, `startElementPicker`, and `cancelElementPicker`. Verify that element attachments produce an `<element-context>` block, and that `startElementPicker()` starts the injected script, polls until a selected payload appears, and returns the captured element.

- [ ] **Step 2: Run focused tests**

Run: `npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_page_capture.test.ts`

Expected: FAIL because the new exports do not exist yet.

- [ ] **Step 3: Implement minimal helpers**

Add `ElementLocator`, `ElementContextCapture`, `buildElementContextAttachment`, `startElementPicker`, and `cancelElementPicker` to `dao_page_capture.ts`.

- [ ] **Step 4: Run focused tests**

Run: `npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_page_capture.test.ts`

Expected: PASS.

### Task 2: Agent UI Chip And Picker Button

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/agent.css`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`

- [ ] **Step 1: Add state and send attachment flow**

Add `pendingElementContext_` and `elementPicking_` state. Add `maybeAttachElementContext_()` after page and selection attachments. Do not clear the element context after send.

- [ ] **Step 2: Add picker button and chip**

Render an icon-only picker button in the chip row. When clicked, call `startElementPicker()`. If a context is returned, store it and show a chip. If clicked while active, call `cancelElementPicker()`.

- [ ] **Step 3: Add localized strings**

Add English and zh-CN strings for picker tooltip, active tooltip, chip label, dismiss title, selected toast, failed toast, and canceled toast.

### Task 3: Verification

**Files:**
- Test: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_page_capture.test.ts`

- [ ] **Step 1: Run WebUI tests**

Run: `npm run test:webui`

Expected: PASS.

- [ ] **Step 2: Run Lit lint**

Run: `npm run lint:lit`

Expected: PASS.

## Commit Note

This repository's AGENTS.md forbids state-changing git commands unless the latest user message explicitly authorizes the exact action. This plan intentionally omits automatic `git add` / `git commit` steps.

