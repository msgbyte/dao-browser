# Dao Agent Message Edit and Message Actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add current-conversation user-message editing, per-message edit history, and per-assistant-message Copy/Image/Regenerate actions to Dao Agent.

**Architecture:** Keep the feature inside the Agent WebUI session state. Add Dao-owned message metadata to `agent.state.messages`, make action decorators target messages by stable `dao.id`, and reuse existing session persistence, clipboard, share-image, and `agent.continue()` flows.

**Tech Stack:** Lit WebUI TypeScript, pi-web-ui chat components, IndexedDB-backed pi `SessionsStore`, Vitest WebUI tests, Chromium browser tests.

---

## Repo Constraints

- Do not edit `engine/`.
- Do not edit generated vendor files under `src/dao/browser/ui/webui/resources/agent/vendor/`.
- Do not run `i18n.sh`.
- Do not run Chromium build tools directly.
- For compile confirmation, only use `npm run rebuild`.
- Do not run state-changing git commands unless the latest user message explicitly authorizes them. Commit steps below are checkpoint instructions for a future authorized execution, not permission to run them.

## File Structure

- Modify `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
  - Add Dao message metadata types.
  - Add helper methods for message IDs, role checks, prompt pairing, truncation, edit state, and targeted copy/image/regenerate.
  - Replace last-assistant-only action injection with message-aware user and assistant action rows.
  - Add test hooks for focused unit tests.
- Modify `src/dao/browser/ui/webui/resources/agent/agent.css`
  - Generalize assistant action-row styling.
  - Add user action row, inline edit editor, and edit-history popover styles.
  - Add dark-mode variants.
- Modify `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
  - Add English strings for edit, save, cancel, edit history, and empty edit.
- Modify `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`
  - Add hand-authored Chinese strings for the same keys.
- Modify `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`
  - Add unit tests for metadata, editing, truncation, targeted regenerate, targeted copy, and targeted image sharing.
- Modify `src/dao/browser/ui/views/dao_browser_browsertest.cc`
  - Update `DaoAgentAssistantActionsTest` so multiple assistant rows and user edit rows are expected.

---

### Task 1: Add Failing Tests for Message Metadata Helpers

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`

- [ ] **Step 1: Add helper tests near the existing `dao-chat-view element picker` suite**

Add this `describe` block before the current `describe('dao-chat-view element picker', ...)`:

```ts
describe('dao-chat-view message metadata helpers', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
  });

  function viewWithMessages(messages: any[]) {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      agent_: {state: {messages: any[]; isStreaming: boolean}};
      _daoTestEnsureMessageIds: () => void;
      _daoTestFindMessageIndexByDaoId: (id: string) => number;
      _daoTestFindPromptForAssistant:
          (assistantId: string) => {question: string; answer: string;
                                    source?: {title: string; domain: string}} |
              null;
    };
    view.agent_ = {state: {messages, isStreaming: false}};
    return view;
  }

  it('adds dao ids without replacing existing ids', () => {
    const messages = [
      {role: 'user', content: 'first'},
      {role: 'assistant', content: 'answer', dao: {id: 'existing'}},
      {role: 'toolResult', content: 'tool'},
    ];
    const view = viewWithMessages(messages);

    view._daoTestEnsureMessageIds();

    expect(messages[0].dao.id).toMatch(/^dao-msg-/);
    expect(messages[1].dao.id).toBe('existing');
    expect(messages[2].dao.id).toMatch(/^dao-msg-/);
  });

  it('finds a message index by dao id', () => {
    const view = viewWithMessages([
      {role: 'user', content: 'first', dao: {id: 'u1'}},
      {role: 'assistant', content: 'answer', dao: {id: 'a1'}},
    ]);

    expect(view._daoTestFindMessageIndexByDaoId('a1')).toBe(1);
    expect(view._daoTestFindMessageIndexByDaoId('missing')).toBe(-1);
  });

  it('builds a selected assistant share pair from its nearest user message', () => {
    const view = viewWithMessages([
      {role: 'user', content: 'older', dao: {id: 'u0'}},
      {role: 'assistant', content: 'older answer', dao: {id: 'a0'}},
      {
        role: 'user-with-attachments',
        content: 'summarize this',
        dao: {id: 'u1'},
        attachments: [{
          daoPageUrl: 'https://example.com/docs',
          daoPageTitle: 'Example Docs',
          fileName: 'Example Docs.md',
        }],
      },
      {role: 'assistant', content: 'selected answer', dao: {id: 'a1'}},
      {role: 'user', content: 'newer', dao: {id: 'u2'}},
      {role: 'assistant', content: 'newer answer', dao: {id: 'a2'}},
    ]);

    expect(view._daoTestFindPromptForAssistant('a1')).toEqual({
      question: 'summarize this',
      answer: 'selected answer',
      source: {title: 'Example Docs', domain: 'example.com'},
    });
  });
});
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts
```

Expected: FAIL because `_daoTestEnsureMessageIds`, `_daoTestFindMessageIndexByDaoId`, and `_daoTestFindPromptForAssistant` are not defined.

- [ ] **Step 3: Checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
git commit -m "test(agent): cover message metadata helpers"
```

---

### Task 2: Implement Message Metadata Helpers

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`

- [ ] **Step 1: Add metadata interfaces near the existing local interfaces**

Add after the `PiAttachment` / local type declarations:

```ts
interface DaoUserMessageVersion {
  content: unknown;
  attachments?: unknown[];
  editedAt: string;
}

interface DaoMessageMetadata {
  id: string;
  editedAt?: string;
  editHistory?: DaoUserMessageVersion[];
}

type DaoChatMessage = {
  role?: string;
  content?: unknown;
  attachments?: unknown[];
  timestamp?: unknown;
  dao?: DaoMessageMetadata;
};
```

- [ ] **Step 2: Add helper methods before `refreshAssistantActions_()`**

Insert this code in `DaoChatView` before `refreshAssistantActions_()`:

```ts
  private newDaoMessageId_(): string {
    try {
      const id = globalThis.crypto?.randomUUID?.();
      if (id) return `dao-msg-${id}`;
    } catch (_) {
      // Fall through to deterministic-enough local fallback.
    }
    return `dao-msg-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  }

  private currentMessages_(): DaoChatMessage[] {
    return (this.agent_?.state.messages ?? []) as DaoChatMessage[];
  }

  private ensureMessageIds_(): void {
    const msgs = this.currentMessages_();
    let changed = false;
    for (const msg of msgs) {
      if (!msg || typeof msg !== 'object') continue;
      if (!msg.dao) {
        msg.dao = {id: this.newDaoMessageId_()};
        changed = true;
      } else if (!msg.dao.id) {
        msg.dao.id = this.newDaoMessageId_();
        changed = true;
      }
    }
    if (changed && this.agent_) {
      this.agent_.state.messages = msgs.slice();
    }
  }

  private findMessageIndexByDaoId_(id: string): number {
    if (!id) return -1;
    return this.currentMessages_().findIndex(msg => msg?.dao?.id === id);
  }

  private isUserMessage_(msg: DaoChatMessage | undefined): boolean {
    return msg?.role === 'user' || msg?.role === 'user-with-attachments';
  }

  private isAssistantMessage_(msg: DaoChatMessage | undefined): boolean {
    return msg?.role === 'assistant' && !!this.extractVisibleText_(msg);
  }

  private extractVisibleText_(msg: DaoChatMessage | undefined): string {
    return this.extractAssistantText_(msg);
  }

  private pageSourceForUserMessage_(msg: DaoChatMessage):
      {title: string; domain: string}|undefined {
    if (msg.role !== 'user-with-attachments' ||
        !Array.isArray(msg.attachments)) {
      return undefined;
    }
    for (const att of msg.attachments) {
      const rawUrl = (att as {daoPageUrl?: unknown})?.daoPageUrl;
      if (typeof rawUrl !== 'string' || !rawUrl) continue;
      try {
        const host = new URL(rawUrl).hostname.replace(/^www\./, '');
        const rawTitle = (att as {daoPageTitle?: unknown; fileName?: unknown})
            .daoPageTitle;
        const rawFile = (att as {fileName?: unknown}).fileName;
        return {
          title: typeof rawTitle === 'string' && rawTitle ?
              rawTitle :
              (typeof rawFile === 'string' && rawFile ? rawFile : host),
          domain: host,
        };
      } catch (_) {
        // Bad attachment URL; keep looking.
      }
    }
    return undefined;
  }

  private findPromptForAssistantIndex_(assistantIdx: number):
      {question: string; source?: {title: string; domain: string};
       answer: string}|null {
    const msgs = this.currentMessages_();
    const assistant = msgs[assistantIdx];
    if (!this.isAssistantMessage_(assistant)) return null;
    const answer = this.extractVisibleText_(assistant);
    for (let i = assistantIdx - 1; i >= 0; i--) {
      const candidate = msgs[i];
      if (!this.isUserMessage_(candidate)) continue;
      return {
        question: this.extractVisibleText_(candidate),
        source: this.pageSourceForUserMessage_(candidate),
        answer,
      };
    }
    return {question: '', answer};
  }

  private findPromptForAssistantId_(assistantId: string):
      {question: string; source?: {title: string; domain: string};
       answer: string}|null {
    const idx = this.findMessageIndexByDaoId_(assistantId);
    return idx >= 0 ? this.findPromptForAssistantIndex_(idx) : null;
  }
```

- [ ] **Step 3: Replace `getLastQaPair_()` internals with helper delegation**

Keep the method name for existing callers, but replace its body:

```ts
  private getLastQaPair_():
      {question: string; source?: {title: string; domain: string};
       answer: string}|null {
    const msgs = this.currentMessages_();
    for (let i = msgs.length - 1; i >= 0; i--) {
      if (this.isAssistantMessage_(msgs[i])) {
        return this.findPromptForAssistantIndex_(i);
      }
    }
    return null;
  }
```

- [ ] **Step 4: Add test hooks near existing Dao test hooks**

Add before `_daoTestRefreshAssistantActions()`:

```ts
  _daoTestEnsureMessageIds(): void {
    this.ensureMessageIds_();
  }

  _daoTestFindMessageIndexByDaoId(id: string): number {
    return this.findMessageIndexByDaoId_(id);
  }

  _daoTestFindPromptForAssistant(assistantId: string):
      {question: string; source?: {title: string; domain: string};
       answer: string}|null {
    return this.findPromptForAssistantId_(assistantId);
  }
```

- [ ] **Step 5: Run the focused test and verify it passes**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts
```

Expected: PASS for the new helper tests and existing tests.

- [ ] **Step 6: Checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
git commit -m "feat(agent): add message targeting helpers"
```

---

### Task 3: Add Failing Tests for Targeted Assistant Actions

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`

- [ ] **Step 1: Import the mocked share image renderer**

Add after the existing imports near `import '../dao_chat_view.js';`:

```ts
import {renderShareImage} from '../dao_share_image.js';
```

- [ ] **Step 2: Add targeted assistant action tests**

Add inside `describe('dao-chat-view message metadata helpers', ...)` after the helper tests:

```ts
  it('regenerates from the user paired with the selected assistant', async () => {
    const continueSpy = vi.fn(async () => undefined);
    const requestUpdate = vi.fn();
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      agent_: {state: {messages: any[]; isStreaming: boolean};
               continue: ReturnType<typeof vi.fn>};
      panel_: HTMLElement;
      _daoTestRegenerateAssistantById: (id: string) => Promise<void>;
    };
    const panel = document.createElement('div');
    const iface = document.createElement('agent-interface') as MockAgentInterface;
    iface.sendMessage = vi.fn();
    iface.requestUpdate = requestUpdate;
    panel.appendChild(iface);
    view.panel_ = panel;
    view.agent_ = {
      state: {
        isStreaming: false,
        messages: [
          {role: 'user', content: 'old prompt', dao: {id: 'u0'}},
          {role: 'assistant', content: 'old answer', dao: {id: 'a0'}},
          {role: 'user', content: 'target prompt', dao: {id: 'u1'}},
          {role: 'assistant', content: 'target answer', dao: {id: 'a1'}},
          {role: 'user', content: 'newer prompt', dao: {id: 'u2'}},
          {role: 'assistant', content: 'newer answer', dao: {id: 'a2'}},
        ],
      },
      continue: continueSpy,
    };

    await view._daoTestRegenerateAssistantById('a1');

    expect(view.agent_.state.messages.map(m => m.dao.id)).toEqual([
      'u0',
      'a0',
      'u1',
    ]);
    expect(requestUpdate).toHaveBeenCalled();
    expect(continueSpy).toHaveBeenCalled();
  });

  it('copies the selected assistant text instead of the latest assistant text', async () => {
    const write = vi.fn(async () => undefined);
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {write, writeText: vi.fn(async () => undefined)},
    });
    Object.defineProperty(window, 'ClipboardItem', {
      configurable: true,
      value: class {
        constructor(readonly items: Record<string, Blob>) {}
      },
    });
    const btn = document.createElement('button');
    const view = viewWithMessages([
      {role: 'user', content: 'prompt', dao: {id: 'u1'}},
      {role: 'assistant', content: 'selected **answer**', dao: {id: 'a1'}},
      {role: 'user', content: 'newer', dao: {id: 'u2'}},
      {role: 'assistant', content: 'newer answer', dao: {id: 'a2'}},
    ]) as HTMLElement & {
      _daoTestCopyAssistantById: (id: string, btn: HTMLButtonElement) =>
          Promise<void>;
    };

    await view._daoTestCopyAssistantById('a1', btn);

    expect(write).toHaveBeenCalledTimes(1);
    const clipboardItem = write.mock.calls[0][0][0];
    expect(Object.keys(clipboardItem.items)).toEqual(['text/html', 'text/plain']);
    await expect(clipboardItem.items['text/plain'].text())
        .resolves.toBe('selected **answer**');
  });

  it('renders a share image from the selected assistant pair', async () => {
    const blob = new Blob(['png'], {type: 'image/png'});
    vi.mocked(renderShareImage).mockResolvedValue(blob);
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {write: vi.fn(async () => undefined)},
    });
    Object.defineProperty(window, 'ClipboardItem', {
      configurable: true,
      value: class {
        constructor(readonly items: Record<string, Blob>) {}
      },
    });
    const btn = document.createElement('button');
    const view = viewWithMessages([
      {
        role: 'user-with-attachments',
        content: 'selected prompt',
        dao: {id: 'u1'},
        attachments: [{
          daoPageUrl: 'https://docs.example.com/page',
          daoPageTitle: 'Docs Page',
        }],
      },
      {role: 'assistant', content: 'selected answer', dao: {id: 'a1'}},
      {role: 'user', content: 'newer prompt', dao: {id: 'u2'}},
      {role: 'assistant', content: 'newer answer', dao: {id: 'a2'}},
    ]) as HTMLElement & {
      _daoTestShareAssistantAsImageById:
          (id: string, btn: HTMLButtonElement) => Promise<void>;
    };

    await view._daoTestShareAssistantAsImageById('a1', btn);

    expect(renderShareImage).toHaveBeenCalledWith({
      question: 'selected prompt',
      source: {title: 'Docs Page', domain: 'docs.example.com'},
      answer: 'selected answer',
    });
  });
```

- [ ] **Step 3: Run the focused test and verify it fails**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts
```

Expected: FAIL because targeted action test hooks do not exist.

- [ ] **Step 4: Checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
git commit -m "test(agent): cover targeted assistant actions"
```

---

### Task 4: Implement Targeted Assistant Actions

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`

- [ ] **Step 1: Add targeted action helpers after `flashButtonLabel_()`**

Add these methods after `flashButtonLabel_()`:

```ts
  private requestAgentRerender_(): void {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    iface?.requestUpdate?.();
    this.syncMeta_();
    this.scheduleSaveSession_();
    this.scheduleDecorate_();
    setTimeout(() => this.refreshMessageActions_(), 80);
  }

  private truncateAfterUserIndex_(userIdx: number): boolean {
    const agent = this.agent_;
    if (!agent || userIdx < 0) return false;
    agent.state.messages = this.currentMessages_().slice(0, userIdx + 1);
    return true;
  }

  private async regenerateAssistantById_(assistantId: string): Promise<void> {
    const agent = this.agent_;
    if (!agent || agent.state.isStreaming) return;
    const assistantIdx = this.findMessageIndexByDaoId_(assistantId);
    if (assistantIdx < 0) return;
    let userIdx = -1;
    const msgs = this.currentMessages_();
    for (let i = assistantIdx - 1; i >= 0; i--) {
      if (this.isUserMessage_(msgs[i])) {
        userIdx = i;
        break;
      }
    }
    if (!this.truncateAfterUserIndex_(userIdx)) return;
    this.requestAgentRerender_();
    try {
      await agent.continue();
    } catch (e) {
      console.warn('[dao] regenerate failed', e);
    }
  }

  private async copyAssistantTextById_(
      assistantId: string, btn: HTMLButtonElement): Promise<void> {
    const pair = this.findPromptForAssistantId_(assistantId);
    if (!pair || !pair.answer) {
      this.flashButtonLabel_(btn, t('chat.message_actions.empty'), false);
      return;
    }
    try {
      const html = renderAssistantMarkdown(pair.answer);
      const ClipboardItemCtor =
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          (window as any).ClipboardItem as (new(items: Record<string, Blob>) =>
                                                ClipboardItem) |
          undefined;
      if (ClipboardItemCtor && navigator.clipboard?.write) {
        const item = new ClipboardItemCtor({
          'text/html': new Blob([html], {type: 'text/html'}),
          'text/plain': new Blob([pair.answer], {type: 'text/plain'}),
        });
        await navigator.clipboard.write([item]);
      } else {
        await navigator.clipboard.writeText(pair.answer);
      }
      this.flashButtonLabel_(btn, t('chat.message_actions.copied'), true);
    } catch (e) {
      console.warn('[dao] copy text failed', e);
      try {
        await navigator.clipboard.writeText(pair.answer);
        this.flashButtonLabel_(btn, t('chat.message_actions.copied'), true);
      } catch (e2) {
        console.warn('[dao] copy text fallback failed', e2);
        this.flashButtonLabel_(btn, t('chat.message_actions.failed'), false);
      }
    }
  }

  private async shareAssistantAsImageById_(
      assistantId: string, btn: HTMLButtonElement): Promise<void> {
    const pair = this.findPromptForAssistantId_(assistantId);
    if (!pair || !pair.answer) {
      this.flashButtonLabel_(btn, t('chat.message_actions.empty'), false);
      return;
    }
    try {
      const blob = await renderShareImage({
        question: pair.question,
        source: pair.source,
        answer: pair.answer,
      });
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const ClipboardItemCtor = (window as any).ClipboardItem;
      if (!ClipboardItemCtor || !navigator.clipboard?.write) {
        throw new Error('ClipboardItem API unavailable');
      }
      await navigator.clipboard.write(
          [new ClipboardItemCtor({'image/png': blob})]);
      this.flashButtonLabel_(btn, t('chat.message_actions.shared'), true);
    } catch (e) {
      console.warn('[dao] share image failed', e);
      this.flashButtonLabel_(btn, t('chat.message_actions.failed'), false);
    }
  }
```

- [ ] **Step 2: Route existing last-message helpers through targeted helpers**

Replace the body of `copyAssistantText_()`:

```ts
  private async copyAssistantText_(btn: HTMLButtonElement): Promise<void> {
    const msgs = this.currentMessages_();
    for (let i = msgs.length - 1; i >= 0; i--) {
      if (this.isAssistantMessage_(msgs[i]) && msgs[i].dao?.id) {
        await this.copyAssistantTextById_(msgs[i].dao.id, btn);
        return;
      }
    }
    this.flashButtonLabel_(btn, t('chat.message_actions.empty'), false);
  }
```

Replace the body of `shareAssistantAsImage_()`:

```ts
  private async shareAssistantAsImage_(btn: HTMLButtonElement):
      Promise<void> {
    const msgs = this.currentMessages_();
    for (let i = msgs.length - 1; i >= 0; i--) {
      if (this.isAssistantMessage_(msgs[i]) && msgs[i].dao?.id) {
        await this.shareAssistantAsImageById_(msgs[i].dao.id, btn);
        return;
      }
    }
    this.flashButtonLabel_(btn, t('chat.message_actions.empty'), false);
  }
```

Replace the body of `retryLastAssistant_()`:

```ts
  private async retryLastAssistant_(): Promise<void> {
    const msgs = this.currentMessages_();
    for (let i = msgs.length - 1; i >= 0; i--) {
      if (this.isAssistantMessage_(msgs[i]) && msgs[i].dao?.id) {
        await this.regenerateAssistantById_(msgs[i].dao.id);
        return;
      }
    }
  }
```

- [ ] **Step 3: Add targeted action test hooks**

Add near the existing test hooks:

```ts
  _daoTestRegenerateAssistantById(id: string): Promise<void> {
    return this.regenerateAssistantById_(id);
  }

  _daoTestCopyAssistantById(
      id: string, btn: HTMLButtonElement): Promise<void> {
    return this.copyAssistantTextById_(id, btn);
  }

  _daoTestShareAssistantAsImageById(
      id: string, btn: HTMLButtonElement): Promise<void> {
    return this.shareAssistantAsImageById_(id, btn);
  }
```

- [ ] **Step 4: Run the focused test and verify it passes**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts
```

Expected: PASS for targeted assistant action tests.

- [ ] **Step 5: Checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
git commit -m "feat(agent): target assistant message actions"
```

---

### Task 5: Add Failing Tests for Editing User Messages

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`

- [ ] **Step 1: Add edit tests to the metadata helper suite**

Add inside `describe('dao-chat-view message metadata helpers', ...)`:

```ts
  it('edits a user message, records history, truncates later messages, and regenerates', async () => {
    const continueSpy = vi.fn(async () => undefined);
    const requestUpdate = vi.fn();
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      agent_: {state: {messages: any[]; isStreaming: boolean};
               continue: ReturnType<typeof vi.fn>; abort: ReturnType<typeof vi.fn>};
      panel_: HTMLElement;
      _daoTestApplyUserMessageEdit: (id: string, text: string) => Promise<void>;
    };
    const panel = document.createElement('div');
    const iface = document.createElement('agent-interface') as MockAgentInterface;
    iface.sendMessage = vi.fn();
    iface.requestUpdate = requestUpdate;
    panel.appendChild(iface);
    view.panel_ = panel;
    view.agent_ = {
      state: {
        isStreaming: false,
        messages: [
          {
            role: 'user-with-attachments',
            content: 'old prompt',
            attachments: [{id: 'dao-page-1', extractedText: 'page'}],
            dao: {id: 'u1'},
          },
          {role: 'assistant', content: 'old answer', dao: {id: 'a1'}},
          {role: 'user', content: 'later prompt', dao: {id: 'u2'}},
        ],
      },
      continue: continueSpy,
      abort: vi.fn(),
    };

    await view._daoTestApplyUserMessageEdit('u1', 'new prompt');

    expect(view.agent_.state.messages).toHaveLength(1);
    expect(view.agent_.state.messages[0].content).toBe('new prompt');
    expect(view.agent_.state.messages[0].attachments).toEqual([
      {id: 'dao-page-1', extractedText: 'page'},
    ]);
    expect(view.agent_.state.messages[0].dao.editedAt).toMatch(/T/);
    expect(view.agent_.state.messages[0].dao.editHistory).toEqual([{
      content: 'old prompt',
      attachments: [{id: 'dao-page-1', extractedText: 'page'}],
      editedAt: expect.stringMatching(/T/),
    }]);
    expect(requestUpdate).toHaveBeenCalled();
    expect(continueSpy).toHaveBeenCalled();
  });

  it('rejects an empty user edit without truncating messages', async () => {
    const view = viewWithMessages([
      {role: 'user', content: 'old prompt', dao: {id: 'u1'}},
      {role: 'assistant', content: 'old answer', dao: {id: 'a1'}},
    ]) as HTMLElement & {
      _daoTestApplyUserMessageEdit: (id: string, text: string) => Promise<void>;
    };

    await view._daoTestApplyUserMessageEdit('u1', '   ');

    expect(view.agent_.state.messages.map(m => m.content)).toEqual([
      'old prompt',
      'old answer',
    ]);
  });

  it('aborts an active stream before saving an already-open edit', async () => {
    const abortSpy = vi.fn();
    const view = viewWithMessages([
      {role: 'user', content: 'old prompt', dao: {id: 'u1'}},
      {role: 'assistant', content: 'old answer', dao: {id: 'a1'}},
    ]) as HTMLElement & {
      agent_: {state: {messages: any[]; isStreaming: boolean};
               continue: ReturnType<typeof vi.fn>; abort: ReturnType<typeof vi.fn>};
      _daoTestApplyUserMessageEdit: (id: string, text: string) => Promise<void>;
    };
    view.agent_.state.isStreaming = true;
    view.agent_.continue = vi.fn(async () => undefined);
    view.agent_.abort = abortSpy;

    await view._daoTestApplyUserMessageEdit('u1', 'new prompt');

    expect(abortSpy).toHaveBeenCalled();
    expect(view.agent_.state.messages.map(m => m.content)).toEqual([
      'new prompt',
    ]);
  });
```

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts
```

Expected: FAIL because `_daoTestApplyUserMessageEdit` is not defined.

- [ ] **Step 3: Checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
git commit -m "test(agent): cover user message editing"
```

---

### Task 6: Implement User Message Editing State and Mutation

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`

- [ ] **Step 1: Add reactive edit state properties**

Add these properties in `static get properties()`:

```ts
      editingMessageId_: {state: true},
      editingDraft_: {state: true},
      editingError_: {state: true},
      historyPopoverMessageId_: {state: true},
```

Add declarations near other private state declarations:

```ts
  declare private editingMessageId_: string;
  declare private editingDraft_: string;
  declare private editingError_: string;
  declare private historyPopoverMessageId_: string;
```

Initialize them in the constructor:

```ts
    this.editingMessageId_ = '';
    this.editingDraft_ = '';
    this.editingError_ = '';
    this.historyPopoverMessageId_ = '';
```

- [ ] **Step 2: Add edit mutation helpers after assistant action helpers**

Add:

```ts
  private async applyUserMessageEdit_(
      userId: string, nextText: string): Promise<void> {
    const agent = this.agent_;
    const trimmed = nextText.trim();
    if (!agent) return;
    if (!trimmed) {
      this.editingError_ = t('chat.message_actions.empty_edit');
      this.refreshMessageActions_();
      return;
    }
    if (agent.state.isStreaming) {
      try {
        agent.abort();
      } catch (_) {
        // Ignore abort failures and still apply the explicit edit.
      }
      agent.state.isStreaming = false;
      this.isStreaming_ = false;
    }
    const idx = this.findMessageIndexByDaoId_(userId);
    const msg = this.currentMessages_()[idx];
    if (!this.isUserMessage_(msg)) return;
    const now = new Date().toISOString();
    const history = Array.isArray(msg.dao?.editHistory) ?
        msg.dao!.editHistory!.slice() :
        [];
    history.push({
      content: msg.content,
      attachments: Array.isArray(msg.attachments) ?
          msg.attachments.slice() :
          msg.attachments,
      editedAt: now,
    });
    msg.content = trimmed;
    msg.dao = {
      ...(msg.dao || {id: userId}),
      id: userId,
      editedAt: now,
      editHistory: history,
    };
    this.truncateAfterUserIndex_(idx);
    this.editingMessageId_ = '';
    this.editingDraft_ = '';
    this.editingError_ = '';
    this.historyPopoverMessageId_ = '';
    this.requestAgentRerender_();
    try {
      await agent.continue();
    } catch (e) {
      console.warn('[dao] continue after edit failed', e);
    }
  }

  private beginEditUserMessage_(userId: string): void {
    const idx = this.findMessageIndexByDaoId_(userId);
    const msg = this.currentMessages_()[idx];
    if (!this.isUserMessage_(msg)) return;
    this.editingMessageId_ = userId;
    this.editingDraft_ = this.extractVisibleText_(msg);
    this.editingError_ = '';
    this.historyPopoverMessageId_ = '';
  }

  private cancelEditUserMessage_(): void {
    this.editingMessageId_ = '';
    this.editingDraft_ = '';
    this.editingError_ = '';
  }

  private onEditDraftInput_(e: Event): void {
    this.editingDraft_ = (e.target as HTMLTextAreaElement).value;
    this.editingError_ = '';
  }

  private onEditDraftKeydown_(e: KeyboardEvent): void {
    if (e.key === 'Escape') {
      e.preventDefault();
      this.cancelEditUserMessage_();
    } else if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      void this.applyUserMessageEdit_(
          this.editingMessageId_, this.editingDraft_);
    }
  }

  private toggleEditHistory_(userId: string): void {
    this.historyPopoverMessageId_ =
        this.historyPopoverMessageId_ === userId ? '' : userId;
  }
```

- [ ] **Step 3: Add the edit test hook**

Add near other test hooks:

```ts
  _daoTestApplyUserMessageEdit(id: string, text: string): Promise<void> {
    return this.applyUserMessageEdit_(id, text);
  }
```

- [ ] **Step 4: Run the focused test and verify it passes**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts
```

Expected: PASS for edit mutation tests.

- [ ] **Step 5: Checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
git commit -m "feat(agent): edit user messages"
```

---

### Task 7: Render Message-Aware Action Rows

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/agent.css`

- [ ] **Step 1: Replace assistant-only decorator with message-aware decorator**

Rename `refreshAssistantActions_()` to `refreshMessageActions_()` and replace its body with:

```ts
  private refreshMessageActions_(): void {
    const panel = this.panel_;
    if (!panel) return;
    panel.querySelectorAll(
        '.dao-message-actions, .dao-edit-history-popover, .dao-user-edit-wrap')
        .forEach(el => el.remove());
    this.ensureMessageIds_();
    const streaming = !!this.agent_?.state.isStreaming;
    const msgs = this.currentMessages_();
    const userEls = Array.from(panel.querySelectorAll('user-message'));
    const assistantEls = Array.from(panel.querySelectorAll('assistant-message'));
    let userCursor = 0;
    let assistantCursor = 0;
    for (const msg of msgs) {
      if (this.isUserMessage_(msg)) {
        const el = userEls[userCursor++] as HTMLElement | undefined;
        if (el && msg.dao?.id) {
          const row = this.buildUserActionRow_(msg, streaming);
          el.insertAdjacentElement('afterend', row);
          this.insertUserAuxiliaryAfterRow_(msg, row);
        }
      } else if (this.isAssistantMessage_(msg)) {
        const el = assistantEls[assistantCursor++] as HTMLElement | undefined;
        if (el && msg.dao?.id) {
          el.insertAdjacentElement(
              'afterend', this.buildAssistantActionRow_(msg, streaming));
        }
      }
    }
    this.decorateCodeBlocks_();
  }
```

Keep this compatibility wrapper so existing call sites and tests continue to work during migration:

```ts
  private refreshAssistantActions_(): void {
    this.refreshMessageActions_();
  }
```

- [ ] **Step 2: Add action-row builders**

Add before `refreshMessageActions_()`:

```ts
  private buildActionButton_(
      className: string, titleKey: string, svg: string,
      onClick: (btn: HTMLButtonElement) => void): HTMLButtonElement {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = className;
    btn.title = t(titleKey);
    btn.setAttribute('aria-label', t(titleKey));
    btn.innerHTML = svg;
    btn.addEventListener('click', () => onClick(btn));
    return btn;
  }

  private buildAssistantActionRow_(
      msg: DaoChatMessage, disabled: boolean): HTMLElement {
    const row = document.createElement('div');
    row.className = 'dao-message-actions dao-assistant-actions';
    row.dataset.daoMessageId = msg.dao?.id || '';
    const copySvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>' +
        '<path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1">' +
        '</path></svg>';
    const imageSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect>' +
        '<circle cx="9" cy="9" r="2"></circle>' +
        '<path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"></path>' +
        '</svg>';
    const regenSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<path d="M3 12a9 9 0 1 0 3-6.7"></path>' +
        '<path d="M3 4v5h5"></path>' +
        '</svg>';
    const id = msg.dao?.id || '';
    const copy = this.buildActionButton_(
        'dao-copy-btn', 'chat.message_actions.copy_tooltip', copySvg,
        btn => void this.copyAssistantTextById_(id, btn));
    const image = this.buildActionButton_(
        'dao-share-btn', 'chat.message_actions.share_tooltip', imageSvg,
        btn => void this.shareAssistantAsImageById_(id, btn));
    const regen = this.buildActionButton_(
        'dao-retry-btn', 'chat.message_actions.regenerate_tooltip', regenSvg,
        () => void this.regenerateAssistantById_(id));
    copy.disabled = disabled;
    image.disabled = disabled;
    regen.disabled = disabled;
    row.append(copy, image, regen);
    return row;
  }

  private buildUserActionRow_(
      msg: DaoChatMessage, disabled: boolean): HTMLElement {
    const row = document.createElement('div');
    row.className = 'dao-message-actions dao-user-actions';
    row.dataset.daoMessageId = msg.dao?.id || '';
    const id = msg.dao?.id || '';
    const edit = document.createElement('button');
    edit.type = 'button';
    edit.className = 'dao-user-action-btn';
    edit.textContent = t('chat.message_actions.edit');
    edit.title = t('chat.message_actions.edit_tooltip');
    edit.setAttribute('aria-label', t('chat.message_actions.edit_tooltip'));
    edit.disabled = disabled;
    edit.addEventListener('click', () => this.beginEditUserMessage_(id));
    row.appendChild(edit);
    const count = msg.dao?.editHistory?.length ?? 0;
    if (count > 0) {
      const history = document.createElement('button');
      history.type = 'button';
      history.className = 'dao-user-action-btn dao-edit-history-btn';
      history.textContent = t(
          'chat.message_actions.edited_version', {version: count + 1});
      history.title = t('chat.message_actions.edit_history_tooltip');
      history.setAttribute(
          'aria-label', t('chat.message_actions.edit_history_tooltip'));
      history.addEventListener('click', () => this.toggleEditHistory_(id));
      row.appendChild(history);
    }
    return row;
  }
```

- [ ] **Step 3: Add inline editor, popover, and row-adjacent insertion builders**

Add after `buildUserActionRow_()`:

```ts
  private insertUserAuxiliaryAfterRow_(
      msg: DaoChatMessage, row: HTMLElement): void {
    const id = msg.dao?.id || '';
    if (this.editingMessageId_ === id) {
      row.insertAdjacentElement('afterend', this.buildInlineEditor_(id));
    } else if (this.historyPopoverMessageId_ === id) {
      row.insertAdjacentElement('afterend', this.buildEditHistoryPopover_(msg));
    }
  }

  private buildInlineEditor_(id: string): HTMLElement {
    const wrap = document.createElement('div');
    wrap.className = 'dao-user-edit-wrap';
    const textarea = document.createElement('textarea');
    textarea.className = 'dao-user-edit-input';
    textarea.value = this.editingDraft_;
    textarea.rows = Math.min(8, Math.max(2, this.editingDraft_.split('\n').length));
    textarea.addEventListener('input', e => this.onEditDraftInput_(e));
    textarea.addEventListener('keydown', e => this.onEditDraftKeydown_(e));
    const actions = document.createElement('div');
    actions.className = 'dao-user-edit-actions';
    const cancel = document.createElement('button');
    cancel.type = 'button';
    cancel.textContent = t('chat.message_actions.cancel_edit');
    cancel.addEventListener('click', () => this.cancelEditUserMessage_());
    const save = document.createElement('button');
    save.type = 'button';
    save.className = 'primary';
    save.textContent = t('chat.message_actions.save_edit');
    save.addEventListener(
        'click', () => void this.applyUserMessageEdit_(id, textarea.value));
    actions.append(cancel, save);
    wrap.append(textarea);
    if (this.editingError_) {
      const error = document.createElement('div');
      error.className = 'dao-user-edit-error';
      error.textContent = this.editingError_;
      wrap.appendChild(error);
    }
    wrap.append(actions);
    setTimeout(() => textarea.focus(), 0);
    return wrap;
  }

  private buildEditHistoryPopover_(msg: DaoChatMessage): HTMLElement {
    const pop = document.createElement('div');
    pop.className = 'dao-edit-history-popover';
    const versions = msg.dao?.editHistory ?? [];
    const title = document.createElement('div');
    title.className = 'dao-edit-history-title';
    title.textContent = t('chat.message_actions.edit_history_title');
    pop.appendChild(title);
    for (let i = versions.length - 1; i >= 0; i--) {
      const v = versions[i];
      const item = document.createElement('div');
      item.className = 'dao-edit-history-item';
      const meta = document.createElement('div');
      meta.className = 'dao-edit-history-meta';
      meta.textContent = new Date(v.editedAt).toLocaleString();
      const body = document.createElement('div');
      body.className = 'dao-edit-history-body';
      body.textContent = this.extractVisibleText_({role: 'user', content: v.content});
      item.append(meta, body);
      pop.appendChild(item);
    }
    return pop;
  }
```

- [ ] **Step 4: Update decorator call sites**

Replace calls to `refreshAssistantActions_()` after agent events and session load with `refreshMessageActions_()`. Keep `_daoTestRefreshAssistantActions()` calling the wrapper:

```ts
  _daoTestRefreshAssistantActions(): void {
    this.refreshAssistantActions_();
  }
```

- [ ] **Step 5: Add CSS**

Append near the existing `.dao-assistant-actions` styles in `agent.css`:

```css
.dao-message-actions {
  display: flex;
  gap: 6px;
  padding: 4px 16px 0;
  margin-top: 2px;
}
.dao-user-actions {
  justify-content: flex-end;
}
.dao-user-action-btn {
  border: none;
  background: transparent;
  color: rgba(70, 120, 190, 0.92);
  font: inherit;
  font-size: 11px;
  padding: 2px 4px;
  border-radius: 6px;
  cursor: pointer;
}
.dao-user-action-btn:hover:not(:disabled) {
  background: rgba(70, 120, 190, 0.12);
}
.dao-user-action-btn:disabled,
.dao-retry-btn:disabled,
.dao-copy-btn:disabled,
.dao-share-btn:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
.dao-user-edit-wrap {
  margin: 6px 16px 10px auto;
  max-width: min(82%, 520px);
}
.dao-user-edit-input {
  width: 100%;
  resize: vertical;
  min-height: 70px;
  padding: 10px 12px;
  border: 1px solid rgba(70, 120, 190, 0.32);
  border-radius: 12px;
  background: rgba(255, 255, 255, 0.78);
  color: var(--foreground);
  font: inherit;
  font-size: 13px;
  outline: none;
}
.dao-user-edit-input:focus {
  border-color: rgba(70, 120, 190, 0.70);
  box-shadow: 0 0 0 3px rgba(70, 120, 190, 0.18);
}
.dao-user-edit-actions {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
  margin-top: 6px;
}
.dao-user-edit-error {
  margin-top: 5px;
  color: var(--error);
  font-size: 11px;
}
.dao-user-edit-actions button {
  border: 1px solid rgba(70, 120, 190, 0.24);
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.42);
  color: var(--foreground);
  font: inherit;
  font-size: 11px;
  padding: 4px 9px;
  cursor: pointer;
}
.dao-user-edit-actions button.primary {
  background: rgba(70, 120, 190, 0.92);
  border-color: rgba(70, 120, 190, 0.92);
  color: white;
}
.dao-edit-history-popover {
  margin: 6px 16px 10px auto;
  max-width: min(82%, 520px);
  padding: 10px;
  border: 1px solid rgba(70, 120, 190, 0.24);
  border-radius: 12px;
  background: rgba(255, 255, 255, 0.92);
  color: var(--foreground);
  box-shadow: 0 8px 24px rgba(30, 20, 40, 0.14);
}
.dao-edit-history-title {
  font-size: 12px;
  font-weight: 600;
  margin-bottom: 8px;
}
.dao-edit-history-item + .dao-edit-history-item {
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px solid rgba(30, 20, 40, 0.08);
}
.dao-edit-history-meta {
  font-size: 10px;
  color: var(--text-tertiary);
  margin-bottom: 3px;
}
.dao-edit-history-body {
  font-size: 12px;
  white-space: pre-wrap;
}
```

Append inside the existing dark-mode block:

```css
  .dao-user-action-btn {
    color: rgba(170, 200, 240, 0.95);
  }
  .dao-user-action-btn:hover:not(:disabled) {
    background: rgba(70, 120, 190, 0.24);
  }
  .dao-user-edit-input {
    background: rgba(255, 255, 255, 0.08);
    border-color: rgba(170, 200, 240, 0.26);
    color: rgba(245, 245, 245, 0.94);
  }
  .dao-user-edit-actions button {
    background: rgba(255, 255, 255, 0.06);
    color: rgba(245, 245, 245, 0.92);
  }
  .dao-user-edit-actions button.primary {
    background: rgba(70, 120, 190, 0.92);
    color: white;
  }
  .dao-edit-history-popover {
    background: rgba(40, 44, 50, 0.96);
    border-color: rgba(170, 200, 240, 0.20);
    box-shadow: 0 8px 24px rgba(0, 0, 0, 0.28);
  }
  .dao-edit-history-item + .dao-edit-history-item {
    border-top-color: rgba(255, 255, 255, 0.10);
  }
```

- [ ] **Step 6: Run WebUI tests**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts
```

Expected: PASS.

- [ ] **Step 7: Checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts src/dao/browser/ui/webui/resources/agent/agent.css
git commit -m "feat(agent): render message action rows"
```

---

### Task 8: Add i18n Strings

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts`

- [ ] **Step 1: Add English strings after existing message action keys**

In `en.ts`, add:

```ts
  'chat.message_actions.edit': 'Edit',
  'chat.message_actions.edit_tooltip': 'Edit message',
  'chat.message_actions.edit_history_tooltip': 'Show edit history',
  'chat.message_actions.save_edit': 'Save',
  'chat.message_actions.cancel_edit': 'Cancel',
  'chat.message_actions.edited_version': 'Edited - v{version}',
  'chat.message_actions.edit_history_title': 'Edit history',
  'chat.message_actions.empty_edit': 'Message cannot be empty',
  'chat.message_actions.regenerating': 'Regenerating',
```

- [ ] **Step 2: Add Chinese strings after existing message action keys**

In `zh-CN.ts`, add:

```ts
  'chat.message_actions.edit': '编辑',
  'chat.message_actions.edit_tooltip': '编辑消息',
  'chat.message_actions.edit_history_tooltip': '查看编辑历史',
  'chat.message_actions.save_edit': '保存',
  'chat.message_actions.cancel_edit': '取消',
  'chat.message_actions.edited_version': '已编辑 · v{version}',
  'chat.message_actions.edit_history_title': '编辑历史',
  'chat.message_actions.empty_edit': '消息不能为空',
  'chat.message_actions.regenerating': '重新生成中',
```

- [ ] **Step 3: Run i18n test**

Run:

```bash
npm run test:webui -- i18n.test.ts
```

Expected: PASS.

- [ ] **Step 4: Checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts
git commit -m "feat(agent): add message action strings"
```

---

### Task 9: Update Browser Test for Rows Below Every Bubble

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Update the staged DOM in `DaoAgentAssistantActionsTest`**

Replace the staged fake assistant-only section in `AttachesActionRowToLastAssistantMessage` with:

```cpp
      view.agent_ = {
        state: {
          isStreaming: false,
          messages: [
            {role: 'user', content: 'first prompt', dao: {id: 'u1'}},
            {role: 'assistant', content: 'first answer', dao: {id: 'a1'}},
            {role: 'user', content: 'second prompt', dao: {id: 'u2'}},
            {role: 'assistant', content: 'second answer', dao: {id: 'a2'}},
          ],
        },
      };
      const u1 = document.createElement('user-message');
      u1.setAttribute('data-test', 'user-first');
      const a1 = document.createElement('assistant-message');
      a1.setAttribute('data-test', 'assistant-first');
      const u2 = document.createElement('user-message');
      u2.setAttribute('data-test', 'user-second');
      const a2 = document.createElement('assistant-message');
      a2.setAttribute('data-test', 'assistant-second');
      panel.appendChild(u1);
      panel.appendChild(a1);
      panel.appendChild(u2);
      panel.appendChild(a2);
```

- [ ] **Step 2: Update browser-test assertions**

Replace assertions after `view._daoTestRefreshAssistantActions();` with:

```cpp
      const assistantRows = panel.querySelectorAll('.dao-assistant-actions');
      if (assistantRows.length !== 2) {
        return 'wrong-assistant-row-count:' + assistantRows.length;
      }
      if (assistantRows[0].previousElementSibling !== a1 ||
          assistantRows[1].previousElementSibling !== a2) {
        return 'assistant-row-placement';
      }
      const userRows = panel.querySelectorAll('.dao-user-actions');
      if (userRows.length !== 2) {
        return 'wrong-user-row-count:' + userRows.length;
      }
      if (userRows[0].previousElementSibling !== u1 ||
          userRows[1].previousElementSibling !== u2) {
        return 'user-row-placement';
      }
      for (const row of assistantRows) {
        const haveCopy = !!row.querySelector('.dao-copy-btn');
        const haveShare = !!row.querySelector('.dao-share-btn');
        const haveRetry = !!row.querySelector('.dao-retry-btn');
        if (!haveCopy || !haveShare || !haveRetry) {
          return 'missing-assistant-btn';
        }
      }
      for (const row of userRows) {
        if (!row.querySelector('.dao-user-action-btn')) {
          return 'missing-user-edit';
        }
      }
      view._daoTestRefreshAssistantActions();
      const assistantRows2 = panel.querySelectorAll('.dao-assistant-actions');
      const userRows2 = panel.querySelectorAll('.dao-user-actions');
      if (assistantRows2.length !== 2 || userRows2.length !== 2) {
        return 'duplicated:' + assistantRows2.length + ':' + userRows2.length;
      }
      return 'ok';
```

- [ ] **Step 3: Run the focused browser test**

Run the smallest available browser-test command for this repository once implementation is ready. Use the existing Dao browser test filter:

```bash
npm run test -- --gtest_filter=DaoAgentAssistantActionsTest.*
```

Expected: PASS for `DaoAgentAssistantActionsTest.*`.

- [ ] **Step 4: Checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "test(agent): verify message action rows"
```

---

### Task 10: Final Verification Sweep

**Files:**
- No code changes unless verification finds defects.

- [ ] **Step 1: Run focused WebUI tests**

Run:

```bash
npm run test:webui -- dao_chat_view.test.ts dao_share_image.test.ts i18n.test.ts
```

Expected: PASS.

- [ ] **Step 2: Run Lit lint**

Run:

```bash
npm run lint:lit
```

Expected: PASS.

- [ ] **Step 3: Run focused browser test**

Run:

```bash
npm run test -- --gtest_filter=DaoAgentAssistantActionsTest.*
```

Expected: PASS.

- [ ] **Step 4: Run compile confirmation when C++ or Chromium integration changed**

If any C++ or Chromium integration code changed, run:

```bash
npm run rebuild
```

Expected: PASS. Do not substitute another compile command.

- [ ] **Step 5: Manual QA**

Use Dao Agent manually:

```text
1. Send a user message.
2. Send a follow-up user message.
3. Copy the first assistant answer and verify it copies the first answer.
4. Copy image for the first assistant answer and verify the image uses the first Q/A pair.
5. Regenerate the first assistant answer and verify later turns disappear.
6. Edit the first user message and verify later turns disappear and a new reply streams.
7. Open Edited - v2 and verify the old prompt is visible.
8. Confirm actions sit below bubbles, outside the bubble body.
```

- [ ] **Step 6: Final checkpoint**

Do not commit unless the user explicitly authorizes state-changing git commands. When authorized, use:

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts src/dao/browser/ui/webui/resources/agent/agent.css src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts src/dao/browser/ui/webui/resources/agent/i18n/locales/zh-CN.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "feat(agent): edit messages and target action rows"
```
