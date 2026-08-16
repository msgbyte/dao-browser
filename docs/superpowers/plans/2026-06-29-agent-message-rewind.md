# Agent Message Rewind Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a rewind button under older Dao Agent assistant replies that keeps the selected reply and removes all following messages.

**Architecture:** Extend Dao's existing message-action decoration in `DaoChatView` rather than changing pi-web-ui vendor components. The rewind action is a WebUI-only state edit over `agent.state.messages`, followed by the existing session-save path so history persistence matches the visible chat.

**Tech Stack:** Lit WebUI, TypeScript, pi-web-ui light-DOM components, Vitest, Chromium browser tests, Dao Agent i18n dictionaries.

## Global Constraints

- Communicate with the user in Chinese; write source code, tests, commit messages, PR titles, and documentation in English.
- Do not hardcode user-facing copy in Dao-owned UI; use the Agent WebUI i18n system.
- Do not run `i18n.sh`.
- Do not edit files under `engine/`.
- Do not edit generated files under `src/dao/browser/ui/webui/resources/agent/vendor/`.
- Do not run `autoninja`, `ninja`, `siso`, direct Chromium build tools, or `gn gen`.
- This is WebUI-only; use `npm run test:webui` and `npm run lint:lit` for verification.
- If the implementation unexpectedly touches C++ or Chromium integration patches, compile confirmation must be `npm run rebuild`.
- Do not run state-changing git commands such as `git add`, `git commit`, `git push`, `git reset`, `git checkout`, or `git stash` unless the latest user message explicitly authorizes the exact action.

---

## File Structure

- Modify `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
  - Add latest-assistant detection.
  - Add the rewind action button to older assistant action rows.
  - Add the rewind handler that truncates `agent.state.messages`.
  - Add a test hook for focused Vitest coverage.
- Modify `src/dao/browser/ui/webui/resources/agent/agent.css`
  - Include `.dao-rewind-btn` in the existing assistant action button style groups.
- Modify `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
  - Add the English tooltip string.
- Modify `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`
  - Add the hand-authored Chinese tooltip string.
- Modify `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`
  - Add unit coverage for rendering availability, truncation behavior, persistence, and no automatic regeneration.
- Modify `src/dao/browser/ui/views/dao_browser_browsertest.cc`
  - Extend the existing assistant action browser test to assert older replies show rewind and the latest reply hides it.

---

### Task 1: Add Failing WebUI Unit Tests

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`

**Interfaces:**
- Consumes: Existing `viewWithMessages(messages)` helper, `_daoTestRefreshAssistantActions()`, and `agent_.state.messages`.
- Produces: Expected test hook `_daoTestRewindAssistantById(assistantId: string): Promise<void>` for Task 2.

- [ ] **Step 1: Add storage mocks with real `sessions.*` shape**

Add this hoisted mock near the existing `pickerMocks` and `skillMocks` declarations:

```ts
const storageMocks = vi.hoisted(() => ({
  getAllMetadata: vi.fn(async () => []),
  getMetadata: vi.fn(async () => ({title: 'Existing session'})),
  saveSession: vi.fn(async () => undefined),
}));
```

Replace the existing `vi.mock('../pi_app_storage.js')` block with:

```ts
vi.mock('../pi_app_storage.js', () => ({
  ensurePiAppStorage: vi.fn(async () => ({
    sessions: {
      getAllMetadata: storageMocks.getAllMetadata,
      getMetadata: storageMocks.getMetadata,
      saveSession: storageMocks.saveSession,
    },
  })),
  syncActiveKeyToPiStorage: vi.fn(),
}));
```

Update `beforeEach` to clear these mocks without removing their implementations:

```ts
beforeEach(() => {
  document.body.innerHTML = '';
  vi.restoreAllMocks();
  storageMocks.getAllMetadata.mockClear();
  storageMocks.getMetadata.mockClear();
  storageMocks.saveSession.mockClear();
});
```

Keep the existing `afterEach` block unchanged.

- [ ] **Step 2: Extend the `viewWithMessages` test type**

In the `viewWithMessages` helper return type, add the current session id and rewind test hook:

```ts
currentSessionId_: string;
_daoTestRewindAssistantById: (assistantId: string) => Promise<void>;
```

After `view.panel_ = null;`, initialize the current session id so save assertions use an existing session:

```ts
view.currentSessionId_ = 'sess-existing';
```

- [ ] **Step 3: Add a helper for staged message DOM**

Add this helper below `selectedAssistantHistory()`:

```ts
function attachMessageHosts(view: ReturnType<typeof viewWithMessages>) {
  const panel = document.createElement('div');
  const iface = document.createElement('agent-interface') as HTMLElement & {
    requestUpdate: ReturnType<typeof vi.fn>;
  };
  iface.requestUpdate = vi.fn();
  panel.appendChild(iface);

  for (const msg of view.agent_.state.messages) {
    if (msg.role === 'user' || msg.role === 'user-with-attachments') {
      const user = document.createElement('user-message');
      const flex = document.createElement('div');
      flex.className = 'flex justify-start mx-4';
      const bubble = document.createElement('div');
      bubble.className = 'user-message-container';
      const markdown = document.createElement('markdown-block');
      bubble.appendChild(markdown);
      flex.appendChild(bubble);
      user.appendChild(flex);
      panel.appendChild(user);
      continue;
    }
    if (msg.role === 'assistant') {
      panel.appendChild(document.createElement('assistant-message'));
    }
  }

  view.panel_ = panel;
  return {panel, iface};
}
```

- [ ] **Step 4: Add render availability test**

Add this test near the existing assistant action tests:

```ts
it('shows rewind only on non-latest assistant messages', () => {
  const view = viewWithMessages([
    {role: 'user', content: 'first prompt', dao: {id: 'u1'}},
    {role: 'assistant', content: 'first answer', dao: {id: 'a1'}},
    {role: 'user', content: 'second prompt', dao: {id: 'u2'}},
    {role: 'assistant', content: 'second answer', dao: {id: 'a2'}},
  ]);
  const {panel} = attachMessageHosts(view);

  view._daoTestRefreshAssistantActions();

  const rows = panel.querySelectorAll('.dao-assistant-actions');
  expect(rows).toHaveLength(2);
  expect(rows[0]?.querySelector('.dao-rewind-btn')).toBeTruthy();
  expect(rows[1]?.querySelector('.dao-rewind-btn')).toBeNull();
  const rewind = rows[0]?.querySelector(
      '.dao-rewind-btn') as HTMLButtonElement|null;
  expect(rewind?.title).toBe('chat.message_actions.rewind_tooltip');
});
```

- [ ] **Step 5: Add truncation and persistence test**

Add this test next to the regenerate/edit tests:

```ts
it('rewinds to the selected assistant without regenerating', async () => {
  const view = viewWithMessages([
    {role: 'user', content: 'first prompt', dao: {id: 'u1'}},
    {role: 'assistant', content: 'first answer', dao: {id: 'a1'}},
    {role: 'toolResult', content: 'tool after first answer', dao: {id: 't1'}},
    {role: 'user', content: 'second prompt', dao: {id: 'u2'}},
    {role: 'assistant', content: 'second answer', dao: {id: 'a2'}},
  ]);
  const {iface} = attachMessageHosts(view);

  await view._daoTestRewindAssistantById('a1');

  expect(view.agent_.state.messages.map(({role, content}) => ({
    role,
    content,
  }))).toEqual([
    {role: 'user', content: 'first prompt'},
    {role: 'assistant', content: 'first answer'},
  ]);
  expect(view.agent_.continue).not.toHaveBeenCalled();
  expect(iface.requestUpdate).toHaveBeenCalled();
  expect(storageMocks.saveSession).toHaveBeenCalledTimes(1);
  expect(storageMocks.saveSession.mock.calls[0]?.[0]).toBe('sess-existing');
  expect(storageMocks.saveSession.mock.calls[0]?.[1].messages).toHaveLength(2);
});
```

- [ ] **Step 6: Add latest-assistant no-op test**

Add this test after the truncation test:

```ts
it('does not rewind the latest assistant message', async () => {
  const view = viewWithMessages([
    {role: 'user', content: 'first prompt', dao: {id: 'u1'}},
    {role: 'assistant', content: 'first answer', dao: {id: 'a1'}},
  ]);
  attachMessageHosts(view);

  await view._daoTestRewindAssistantById('a1');

  expect(view.agent_.state.messages.map(({content}) => content)).toEqual([
    'first prompt',
    'first answer',
  ]);
  expect(view.agent_.continue).not.toHaveBeenCalled();
  expect(storageMocks.saveSession).not.toHaveBeenCalled();
});
```

- [ ] **Step 7: Run focused tests and confirm failure**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts
```

Expected result before Task 2:

```text
FAIL src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected failures mention missing `.dao-rewind-btn` and missing `_daoTestRewindAssistantById`.

- [ ] **Step 8: Inspect diff without staging**

Run:

```bash
git diff -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: diff shows only test and mock changes. Do not stage or commit.

---

### Task 2: Implement Rewind UI, State Truncation, Styles, And I18n

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/agent.css`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`

**Interfaces:**
- Consumes: Test hook expected by Task 1.
- Produces:
  - `_daoTestRewindAssistantById(assistantId: string): Promise<void>`
  - Private `rewindToAssistantById_(assistantId: string): Promise<void>`
  - Private `findLatestAssistantIndex_(): number`

- [ ] **Step 1: Add latest-assistant helper**

In `dao_chat_view.ts`, add this method near `findMessageIndexByDaoId_`:

```ts
private findLatestAssistantIndex_(): number {
  const msgs = this.currentMessages_();
  for (let i = msgs.length - 1; i >= 0; i--) {
    if (this.isAssistantMessage_(msgs[i])) return i;
  }
  return -1;
}
```

- [ ] **Step 2: Update the assistant action row signature**

Change the `buildAssistantActionRow_` signature from:

```ts
private buildAssistantActionRow_(
    msg: DaoChatMessage, disabled: boolean): HTMLElement {
```

to:

```ts
private buildAssistantActionRow_(
    msg: DaoChatMessage, disabled: boolean, canRewind: boolean): HTMLElement {
```

- [ ] **Step 3: Add the rewind button in the assistant action row**

Inside `buildAssistantActionRow_`, after the `regenSvg` constant, add a Lucide rewind icon. Use the current upstream Lucide child nodes when implementing; the structure should match:

```ts
const rewindSvg =
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
    ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
    ' aria-hidden="true">' +
    '<path d="m11 17-5-5 5-5"></path>' +
    '<path d="m18 17-5-5 5-5"></path>' +
    '</svg>';
```

After creating `regen`, add:

```ts
const rewind = this.buildActionButton_(
    'dao-rewind-btn', 'chat.message_actions.rewind_tooltip', rewindSvg,
    () => void this.rewindToAssistantById_(id));
rewind.disabled = disabled;
```

Replace:

```ts
row.append(copy, image, regen);
```

with:

```ts
row.append(copy, image, regen);
if (canRewind) {
  row.appendChild(rewind);
}
```

- [ ] **Step 4: Pass the availability flag from `refreshMessageActions_`**

In `refreshMessageActions_`, after `const msgs = this.currentMessages_();`, add:

```ts
const latestAssistantIdx = this.findLatestAssistantIndex_();
```

Change the assistant branch from:

```ts
} else if (role === 'assistant') {
  const el = assistantEls[assistantCursor++] as HTMLElement | undefined;
  if (el && msg.dao?.id && this.isAssistantMessage_(msg)) {
    el.insertAdjacentElement(
        'afterend', this.buildAssistantActionRow_(msg, disabled));
  }
}
```

to:

```ts
} else if (role === 'assistant') {
  const el = assistantEls[assistantCursor++] as HTMLElement | undefined;
  if (el && msg.dao?.id && this.isAssistantMessage_(msg)) {
    const idx = msgs.indexOf(msg);
    const canRewind = idx >= 0 && idx !== latestAssistantIdx;
    el.insertAdjacentElement(
        'afterend',
        this.buildAssistantActionRow_(msg, disabled, canRewind));
  }
}
```

- [ ] **Step 5: Add the rewind handler**

Add this method near `regenerateAssistantById_`:

```ts
private async rewindToAssistantById_(assistantId: string): Promise<void> {
  const agent = this.agent_;
  if (!agent || agent.state.isStreaming || this.isStreaming_) return;

  const assistantIdx = this.findMessageIndexByDaoId_(assistantId);
  const msg = agent.state.messages[assistantIdx] as
      DaoChatMessage | undefined;
  if (assistantIdx < 0 || !this.isAssistantMessage_(msg)) return;
  if (assistantIdx === this.findLatestAssistantIndex_()) return;

  agent.state.messages = agent.state.messages.slice(0, assistantIdx + 1);
  this.editingMessageId_ = '';
  this.editingDraft_ = '';
  this.editingError_ = '';
  this.debugContextMessageId_ = '';
  this.closeUserActionMenu_(false);

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const iface = this.panel_?.querySelector('agent-interface') as any;
  iface?.requestUpdate?.();
  this.syncMeta_();
  this.refreshMessageActions_();
  await this.saveCurrentSession_();
  this.focusInput();
}
```

- [ ] **Step 6: Add the test hook**

Near the existing `_daoTestRegenerateAssistantById` hook, add:

```ts
_daoTestRewindAssistantById(assistantId: string): Promise<void> {
  return this.rewindToAssistantById_(assistantId);
}
```

- [ ] **Step 7: Add i18n strings**

In `en.ts`, add the string near the existing message action tooltips:

```ts
'chat.message_actions.rewind_tooltip': 'Rewind to this response',
```

In `zh-CN.ts`, add:

```ts
'chat.message_actions.rewind_tooltip': '回到这条回答',
```

- [ ] **Step 8: Include `.dao-rewind-btn` in button styles**

In `agent.css`, update every assistant action button selector group that contains `.dao-retry-btn`, `.dao-copy-btn`, and `.dao-share-btn` to also include `.dao-rewind-btn`.

The light-mode base group should become:

```css
.dao-retry-btn,
.dao-copy-btn,
.dao-share-btn,
.dao-rewind-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  width: 24px;
  height: 24px;
  border: 1px solid rgba(255, 255, 255, 0.16);
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.06);
  color: rgba(30, 20, 40, 0.65);
  line-height: 1;
  cursor: pointer;
  transition: background-color 0.12s, border-color 0.12s, color 0.12s;
}
```

The light-mode hover group should become:

```css
.dao-retry-btn:hover,
.dao-copy-btn:hover,
.dao-share-btn:hover,
.dao-rewind-btn:hover {
  background: rgba(70, 120, 190, 0.16);
  border-color: rgba(70, 120, 190, 0.32);
  color: rgba(30, 20, 40, 0.92);
}
```

The light-mode active group should become:

```css
.dao-retry-btn:active,
.dao-copy-btn:active,
.dao-share-btn:active,
.dao-rewind-btn:active {
  background: rgba(70, 120, 190, 0.24);
}
```

The icon-size group should become:

```css
.dao-retry-btn svg,
.dao-copy-btn svg,
.dao-share-btn svg,
.dao-rewind-btn svg {
  width: 12px;
  height: 12px;
}
```

The disabled group should become:

```css
.dao-retry-btn:disabled,
.dao-copy-btn:disabled,
.dao-share-btn:disabled,
.dao-rewind-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
```

Inside the dark-mode section, change the assistant action comment from:

```css
/* Assistant action row (retry / copy / share). */
```

to:

```css
/* Assistant action row (retry / copy / share / rewind). */
```

The dark-mode base group should become:

```css
.dao-retry-btn,
.dao-copy-btn,
.dao-share-btn,
.dao-rewind-btn {
  border-color: rgba(255, 255, 255, 0.12);
  background: rgba(255, 255, 255, 0.04);
  color: rgba(255, 255, 255, 0.65);
}
```

The dark-mode hover group should become:

```css
.dao-retry-btn:hover,
.dao-copy-btn:hover,
.dao-share-btn:hover,
.dao-rewind-btn:hover {
  background: rgba(70, 120, 190, 0.24);
  border-color: rgba(70, 120, 190, 0.45);
  color: rgba(245, 245, 245, 0.92);
}
```

The dark-mode active group should become:

```css
.dao-retry-btn:active,
.dao-copy-btn:active,
.dao-share-btn:active,
.dao-rewind-btn:active {
  background: rgba(70, 120, 190, 0.36);
}
```

Do not add `.dao-rewind-btn.is-flashing` because the rewind button disappears after a successful rewind.

- [ ] **Step 9: Run focused WebUI tests**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts
```

Expected:

```text
PASS src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

- [ ] **Step 10: Inspect diff without staging**

Run:

```bash
git diff -- src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts src/dao/browser/ui/webui/resources/agent/agent.css src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: diff contains only WebUI implementation, i18n, CSS, and tests. Do not stage or commit.

---

### Task 3: Extend Browser-Level Assistant Action Coverage

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

**Interfaces:**
- Consumes: `.dao-rewind-btn` DOM class produced by Task 2.
- Produces: Browser coverage that the action-row injector shows rewind only on older assistant replies.

- [ ] **Step 1: Update the assistant action browser test assertions**

In `DaoAgentAssistantActionsTest.AttachesAssistantRowsAndUserMoreMenus`, replace the assistant button loop:

```js
for (const row of assistantRows) {
  const haveCopy = !!row.querySelector('.dao-copy-btn');
  const haveShare = !!row.querySelector('.dao-share-btn');
  const haveRetry = !!row.querySelector('.dao-retry-btn');
  if (!haveCopy || !haveShare || !haveRetry) {
    return 'missing-assistant-btn';
  }
}
```

with:

```js
for (let i = 0; i < assistantRows.length; i++) {
  const row = assistantRows[i];
  const haveCopy = !!row.querySelector('.dao-copy-btn');
  const haveShare = !!row.querySelector('.dao-share-btn');
  const haveRetry = !!row.querySelector('.dao-retry-btn');
  const haveRewind = !!row.querySelector('.dao-rewind-btn');
  if (!haveCopy || !haveShare || !haveRetry) {
    return 'missing-assistant-btn';
  }
  if (i === 0 && !haveRewind) {
    return 'missing-rewind-on-older-assistant';
  }
  if (i === assistantRows.length - 1 && haveRewind) {
    return 'unexpected-rewind-on-latest-assistant';
  }
}
```

- [ ] **Step 2: Do not run browser tests as the default verification**

Because the implementation is WebUI-only and the project guidance prefers the smallest relevant verification, leave the browser test as coverage for CI or for a later explicit browser-test run. Do not run `npm run test:build`, direct browser test binaries, `autoninja`, or `ninja`.

- [ ] **Step 3: Inspect diff without staging**

Run:

```bash
git diff -- src/dao/browser/ui/views/dao_browser_browsertest.cc
```

Expected: diff only extends `DaoAgentAssistantActionsTest`. Do not stage or commit.

---

### Task 4: Final Verification And Review

**Files:**
- Read: `docs/superpowers/specs/2026-06-29-agent-rewind-design.md`
- Read: changed files from Tasks 1-3

**Interfaces:**
- Consumes: Completed implementation and tests from Tasks 1-3.
- Produces: Verified WebUI feature ready for user review.

- [ ] **Step 1: Run WebUI test suite**

Run:

```bash
npm run test:webui
```

Expected:

```text
PASS
```

- [ ] **Step 2: Run Lit lint**

Run:

```bash
npm run lint:lit
```

Expected:

```text
PASS
```

- [ ] **Step 3: Confirm no forbidden generated or engine files changed**

Run:

```bash
git diff --name-only
```

Expected changed files are limited to:

```text
src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts
src/dao/browser/ui/webui/resources/agent/agent.css
src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts
src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts
src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
src/dao/browser/ui/views/dao_browser_browsertest.cc
```

Other pre-existing user changes may appear in `git diff --name-only`; do not revert them. If unrelated user changes are present, mention them in the final summary as pre-existing and untouched.

- [ ] **Step 4: Review behavior against the design**

Confirm these statements are true from code and tests:

```text
Older assistant replies show rewind.
The latest assistant reply hides rewind.
Rewind keeps the selected assistant reply.
Rewind removes every following user, assistant, and toolResult message.
Rewind saves the current session.
Rewind does not call agent.continue().
Rewind is disabled while streaming.
User-visible text goes through i18n.
```

- [ ] **Step 5: Report result without staging or committing**

Final response should include:

```text
Implemented the WebUI rewind action for older assistant replies.
Verified with npm run test:webui and npm run lint:lit.
No git staging or commit was performed.
```

If either command fails, report the exact failing command and the highest-signal error lines.
