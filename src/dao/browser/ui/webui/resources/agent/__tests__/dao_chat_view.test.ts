// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {readFileSync} from 'node:fs';

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {ElementContextCapture} from '../dao_element_context.js';

const pickerMocks = vi.hoisted(() => ({
  startElementPicker: vi.fn(),
  cancelElementPicker: vi.fn(),
  captureElementScreenshotFromPage: vi.fn(),
  callNative: vi.fn(),
  callNativeArgs: vi.fn(),
  webUiListeners: {} as
      Record<string, Array<(...args: unknown[]) => void>>,
}));

const storageMocks = vi.hoisted(() => ({
  getAllMetadata: vi.fn(async () => []),
  getMetadata: vi.fn(async () => ({title: 'Existing session'})),
  loadSession: vi.fn(async () => ({messages: []})),
  saveSession: vi.fn(async () => undefined),
}));

const skillMocks = vi.hoisted(() => ({
  skills: [] as Array<{
    id: string;
    name: string;
    description: string;
    source: string;
    hosts: string[];
    requiresPageContent: boolean;
    disabled: boolean;
  }>,
  initSkillRegistry: vi.fn(),
  loadSkillInstructions: vi.fn(async () => null),
  refreshSkillRegistry: vi.fn(async () => undefined),
  refreshSkillRegistryIfStale: vi.fn(async () => false),
}));

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  const lit = await import('../../sidebar/__tests__/lit_test_shim.js');
  Object.defineProperty(lit.CrLitElement.prototype, 'disconnectedCallback', {
    configurable: true,
    value() {},
  });
  return lit;
});

vi.mock('../agent_bridge.js', async () => {
  const actual =
      await vi.importActual<typeof import('../agent_bridge.js')>(
          '../agent_bridge.js');
  return {
    ...actual,
    currentSoulContent: 'soul',
    callNative: (...args: unknown[]) => pickerMocks.callNative(...args),
    callNativeArgs: (...args: unknown[]) =>
        pickerMocks.callNativeArgs(...args),
    addWebUIListener: (
        event: string, callback: (...args: unknown[]) => void) => {
      pickerMocks.webUiListeners[event] =
          pickerMocks.webUiListeners[event] || [];
      pickerMocks.webUiListeners[event].push(callback);
    },
    removeWebUIListener: (
        event: string, callback: (...args: unknown[]) => void) => {
      const listeners = pickerMocks.webUiListeners[event] || [];
      pickerMocks.webUiListeners[event] =
          listeners.filter(listener => listener !== callback);
    },
    recordApiCall: vi.fn(),
    refreshSoulContent: vi.fn(),
    soulChannel: {
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    },
  };
});

vi.mock('../dao_compact.js', () => ({
  compactAgentMessages: vi.fn(),
  estimateMessagesTokens: vi.fn(() => 0),
}));

vi.mock('../llm_config.js', () => ({
  getActiveLLMConfig: () => ({
    provider: 'openai',
    model: 'gpt-4.1-mini',
    apiKey: 'test-key',
    baseUrl: '',
  }),
}));

vi.mock('../model_capabilities.js', () => ({
  lookupModelCapabilities: () => ({contextWindow: 128000, maxTokens: 4096}),
}));

vi.mock('../dao_page_capture.js', async () => {
  const actual = await vi.importActual<typeof import('../dao_page_capture.js')>(
      '../dao_page_capture.js');
  return {
    ...actual,
    cancelElementPicker: (...args: unknown[]) =>
        pickerMocks.cancelElementPicker(...args),
    captureElementScreenshotFromPage: (...args: unknown[]) =>
        pickerMocks.captureElementScreenshotFromPage(...args),
    startElementPicker: (...args: unknown[]) =>
        pickerMocks.startElementPicker(...args),
  };
});

vi.mock('../dao_share_image.js', () => ({
  copyPngBlobToClipboard: vi.fn(),
  renderShareImage: vi.fn(),
}));

vi.mock('../dao_telemetry.js', () => ({
  reportTelemetryEvent: vi.fn(),
}));

vi.mock('../pi_tool_adapter.js', () => ({
  buildAgentTools: () => [],
}));

vi.mock('../dao_chat_history_panel.js', () => ({}));

vi.mock('../pi_app_storage.js', () => ({
  ensurePiAppStorage: vi.fn(async () => ({
    sessions: {
      getAllMetadata: storageMocks.getAllMetadata,
      getMetadata: storageMocks.getMetadata,
      loadSession: storageMocks.loadSession,
      saveSession: storageMocks.saveSession,
    },
  })),
  syncActiveKeyToPiStorage: vi.fn(),
}));

vi.mock('../skill_registry.js', async () => {
  const actual =
      await vi.importActual<typeof import('../skill_registry.js')>(
          '../skill_registry.js');
  return {
    ...actual,
    getAllSkills: () => skillMocks.skills,
    initSkillRegistry: (...args: unknown[]) =>
        skillMocks.initSkillRegistry(...args),
    loadSkillInstructions: (...args: unknown[]) =>
        skillMocks.loadSkillInstructions(...args),
    refreshSkillRegistry: (...args: unknown[]) =>
        skillMocks.refreshSkillRegistry(...args),
    refreshSkillRegistryIfStale: (...args: unknown[]) =>
        skillMocks.refreshSkillRegistryIfStale(...args),
  };
});

vi.mock('../tool_catalog.js', () => ({
  toolConfigChannel: {
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  },
}));

vi.mock('../i18n/i18n.js', () => ({
  t: (key: string, vars?: Record<string, string | number>) => {
    const first = Object.values(vars || {})[0];
    return first === undefined ? key : `${key}:${first}`;
  },
}));

vi.mock('../vendor/pi_runtime_bundle.js', () => ({
  Agent: class {
    convertToLlm: (msgs: any[]) => any[];
    state = {
      systemPrompt: '',
      model: {},
      thinkingLevel: 'off',
      tools: [],
      messages: [],
      isStreaming: false,
    };
    constructor(opts: any = {}) {
      this.state = {
        ...this.state,
        ...(opts.initialState || {}),
      };
      this.convertToLlm = opts.convertToLlm || ((msgs: any[]) => msgs);
    }
    subscribe() {
      return () => {};
    }
    abort() {}
    reset() {}
    waitForIdle() {
      return Promise.resolve();
    }
    continue() {
      return Promise.resolve();
    }
  },
  getProviders: () => [],
  getModels: () => [],
  getModel: () => ({reasoning: false}),
  marked: {parse: (markdown: string) => markdown},
}));

import {clearReusableElementContext} from '../dao_element_context.js';
import {
  compactAgentMessages,
  estimateMessagesTokens,
} from '../dao_compact.js';
import {
  copyPngBlobToClipboard,
  renderShareImage,
} from '../dao_share_image.js';
import '../dao_chat_view.js';

function sampleContext(): ElementContextCapture {
  return {
    url: 'https://example.com/login',
    title: 'Login',
    label: 'Sign in button',
    text: 'Sign in',
    locator: {
      role: 'button',
      name: 'Sign in',
      tag: 'button',
      text: 'Sign in',
      attributes: {'data-testid': 'login-submit'},
      css: '[data-testid="login-submit"]',
      fallbackPath: 'body > form > button:nth-of-type(1)',
      nearText: ['Email', 'Password'],
      bounds: {x: 12, y: 34, width: 80, height: 32},
    },
  };
}

type MockAgentInterface = HTMLElement & {
  sendMessage: ReturnType<typeof vi.fn>;
  requestUpdate: ReturnType<typeof vi.fn>;
};

async function mountChatViewWithSend(originalSend: ReturnType<typeof vi.fn>) {
  const view = document.createElement('dao-chat-view') as HTMLElement;
  const panel = document.createElement('div') as HTMLElement & {
    setAgent: ReturnType<typeof vi.fn>;
  };
  const iface = document.createElement('agent-interface') as MockAgentInterface;
  iface.sendMessage = originalSend;
  iface.requestUpdate = vi.fn();
  panel.setAgent = vi.fn(async () => undefined);
  panel.appendChild(iface);

  Object.assign(view, {
    panel_: panel,
    refreshChips_: vi.fn(async () => undefined),
    maybeResumeLastSession_: vi.fn(async () => undefined),
    suppressChipAttachOnce_: true,
  });

  await (view as unknown as {mount_: () => Promise<void>}).mount_();
  pickerMocks.callNative.mockClear();

  return {view, iface};
}

function clearTabWatchTimer(view: HTMLElement) {
  const timer = (view as unknown as {tabWatchTimer_?: number}).tabWatchTimer_;
  if (timer) {
    window.clearInterval(timer);
  }
}

function templateText(value: unknown): string {
  if (value === null || value === undefined || value === false) {
    return '';
  }
  if (Array.isArray(value)) {
    return value.map(templateText).join('');
  }
  if (typeof value === 'object' && 'strings' in value && 'values' in value) {
    const result = value as {
      strings: ArrayLike<string>;
      values: unknown[];
    };
    let out = '';
    for (let i = 0; i < result.values.length; i++) {
      out += result.strings[i] ?? '';
      out += templateText(result.values[i]);
    }
    out += result.strings[result.strings.length - 1] ?? '';
    return out;
  }
  return String(value);
}

function restorePropertyDescriptor(
    target: object, key: PropertyKey,
    descriptor: PropertyDescriptor | undefined) {
  if (descriptor) {
    Object.defineProperty(target, key, descriptor);
    return;
  }
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  delete (target as any)[key];
}

describe('dao-chat-view message metadata helpers', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    vi.mocked(compactAgentMessages).mockReset();
    vi.mocked(estimateMessagesTokens).mockReset();
    vi.mocked(estimateMessagesTokens).mockReturnValue(0);
    storageMocks.getAllMetadata.mockClear();
    storageMocks.getMetadata.mockClear();
    storageMocks.loadSession.mockClear();
    storageMocks.saveSession.mockClear();
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
  });

  function viewWithMessages(messages: any[]) {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      agent_: {
        state: {messages: any[]; isStreaming: boolean};
        abort: ReturnType<typeof vi.fn>;
        continue: ReturnType<typeof vi.fn>;
      };
      currentSessionId_: string;
      panel_: HTMLElement | null;
      _daoTestEnsureMessageIds: () => void;
      _daoTestFindMessageIndexByDaoId: (id: string) => number;
      _daoTestFindPromptForAssistant:
          (assistantId: string) => {question: string; answer: string;
                                    source?: {title: string; domain: string}} |
              null;
      _daoTestRegenerateAssistantById: (assistantId: string) => Promise<void>;
      _daoTestCopyAssistantById: (assistantId: string) => Promise<void>;
      _daoTestShareAssistantAsImageById:
          (assistantId: string) => Promise<void>;
      _daoTestApplyUserMessageEdit:
          (messageId: string, content: string) => Promise<void>;
      _daoTestRefreshAssistantActions: () => void;
      _daoTestRewindAssistantById: (assistantId: string) => Promise<void>;
      _daoTestMaybeAutoCompactAfterTurn: () => Promise<void>;
    };
    view.agent_ = {
      state: {
        messages,
        isStreaming: false,
        model: {contextWindow: 128000},
      },
      abort: vi.fn(),
      continue: vi.fn(async () => undefined),
    };
    view.panel_ = null;
    view.currentSessionId_ = 'sess-existing';
    return view;
  }

  function selectedAssistantHistory() {
    return [
      {role: 'user', content: 'old user prompt'},
      {role: 'assistant', content: 'old assistant answer'},
      {role: 'user', content: 'latest user prompt'},
      {role: 'assistant', content: 'latest assistant answer'},
    ];
  }

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

  it('auto-compacts after a turn when context crosses the hot threshold',
     async () => {
    vi.mocked(estimateMessagesTokens).mockReturnValue(810);
    vi.mocked(compactAgentMessages).mockImplementation(async (agent: any) => {
      agent.state.messages = [{role: 'user', content: 'summary'}];
      return {summary: 'summary', collapsedCount: 2, keptCount: 0};
    });
    const view = viewWithMessages([
      {role: 'user', content: 'prompt'},
      {role: 'assistant', content: 'answer'},
    ]);
    view.agent_.state.model = {contextWindow: 1000};
    const {iface} = attachMessageHosts(view);

    await view._daoTestMaybeAutoCompactAfterTurn();

    expect(compactAgentMessages).toHaveBeenCalledWith(
        view.agent_,
        expect.objectContaining({keepTailUserTurns: 1}));
    expect(view.agent_.state.messages).toHaveLength(2);
    expect(view.agent_.state.messages[1]).toMatchObject({
      role: 'assistant',
      dao: {autoCompactNotice: true},
      // Carries the assistant metadata the vendor renderer dereferences so a
      // synthesized bubble never crashes on undefined usage/provider.
      usage: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
      stopReason: 'stop',
    });
    expect(view.agent_.state.messages[1].content[0].text).toContain(
        'chat.compact.auto_notice');
    expect(iface.requestUpdate).toHaveBeenCalled();
  });

  it('stays silent when background auto-compaction fails', async () => {
    vi.mocked(estimateMessagesTokens).mockReturnValue(810);
    vi.mocked(compactAgentMessages).mockRejectedValue(
        new Error('History is already compacted'));
    const view = viewWithMessages([
      {role: 'user', content: 'prompt'},
      {role: 'assistant', content: 'answer'},
    ]);
    view.agent_.state.model = {contextWindow: 1000};
    const toast = vi.fn();
    view.addEventListener('show-toast', toast);

    await view._daoTestMaybeAutoCompactAfterTurn();

    expect(compactAgentMessages).toHaveBeenCalled();
    expect(toast).not.toHaveBeenCalled();
    // No notice appended on failure; history is left untouched.
    expect(view.agent_.state.messages).toHaveLength(2);
  });

  it('does not auto-compact below the hot threshold', async () => {
    vi.mocked(estimateMessagesTokens).mockReturnValue(790);
    const view = viewWithMessages([
      {role: 'user', content: 'prompt'},
      {role: 'assistant', content: 'answer'},
    ]);
    view.agent_.state.model = {contextWindow: 1000};

    await view._daoTestMaybeAutoCompactAfterTurn();

    expect(compactAgentMessages).not.toHaveBeenCalled();
    expect(view.agent_.state.messages).toHaveLength(2);
  });

  it('renders user actions in a more menu to the left of the bubble', () => {
    const view = viewWithMessages([
      {
        role: 'user',
        content: 'editable prompt',
        dao: {
          id: 'u1',
          editHistory: [{content: 'old prompt', editedAt: '2026-01-01'}],
        },
      },
    ]);
    const panel = document.createElement('div');
    const host = document.createElement('user-message');
    const flex = document.createElement('div');
    flex.className = 'flex justify-start mx-4';
    const bubble = document.createElement('div');
    bubble.className = 'user-message-container';
    const markdown = document.createElement('markdown-block');
    markdown.getBoundingClientRect = () => ({
      x: 520,
      y: 100,
      top: 100,
      right: 640,
      bottom: 140,
      left: 520,
      width: 120,
      height: 40,
      toJSON: () => ({}),
    }) as DOMRect;
    bubble.appendChild(markdown);
    flex.appendChild(bubble);
    host.appendChild(flex);
    panel.appendChild(host);
    view.panel_ = panel;

    view._daoTestRefreshAssistantActions();

    const row = panel.querySelector('.dao-user-actions') as HTMLElement|null;
    const line =
        panel.querySelector('.dao-user-message-line') as HTMLElement|null;
    expect(row?.parentElement).toBe(line);
    expect(row?.nextElementSibling).toBe(markdown);
    expect(row?.style.marginLeft).not.toBe('auto');
    expect(panel.querySelector('.dao-edit-btn')).toBeNull();

    const more =
        row?.querySelector('.dao-user-more-btn') as HTMLButtonElement|null;
    expect(more).toBeTruthy();
    expect(more?.title).toBe('chat.message_actions.more_tooltip');
    const circles = Array.from(more?.querySelectorAll('svg circle') ?? [])
                        .map(circle => [
                          circle.getAttribute('cx'),
                          circle.getAttribute('cy'),
                          circle.getAttribute('r'),
                        ]);
    expect(circles).toEqual([
      ['12', '12', '1'],
      ['19', '12', '1'],
      ['5', '12', '1'],
    ]);

    more?.click();

    const openRow =
        panel.querySelector('.dao-user-actions') as HTMLElement|null;
    const openMore =
        openRow?.querySelector('.dao-user-more-btn') as HTMLButtonElement|null;
    const menu = openRow?.querySelector('.dao-user-action-menu');
    expect(openRow?.classList.contains('dao-user-actions-open')).toBe(true);
    expect(openMore?.classList.contains('is-selected')).toBe(true);
    expect(openMore?.getAttribute('aria-expanded')).toBe('true');
    expect(menu).toBeTruthy();
    expect(menu?.querySelector('.dao-edit-menu-item')?.textContent).toContain(
        'chat.message_actions.edit');
    expect(menu?.querySelector('.dao-history-menu-item')).toBeNull();
  });

  it('shows user message actions only on row hover focus or menu open',
     () => {
       const cssText = readFileSync(
           'src/dao/browser/ui/webui/resources/agent/agent.css', 'utf8');
       expect(cssText).toMatch(
           /\.dao-user-message-line\s*>\s*\.dao-user-actions\s*{[^}]*opacity:\s*0;/s);
       expect(cssText).toMatch(
           /\.dao-user-message-line\s*>\s*\.dao-user-actions\s*{[^}]*pointer-events:\s*none;/s);
       expect(cssText).toMatch(
           /\.dao-user-message-line:hover\s*>\s*\.dao-user-actions,\s*\.dao-user-message-line:focus-within\s*>\s*\.dao-user-actions,\s*\.dao-user-message-line\s*>\s*\.dao-user-actions\.dao-user-actions-open\s*{[^}]*opacity:\s*1;/s);
       expect(cssText).toMatch(
           /\.dao-user-message-line:hover\s*>\s*\.dao-user-actions,\s*\.dao-user-message-line:focus-within\s*>\s*\.dao-user-actions,\s*\.dao-user-message-line\s*>\s*\.dao-user-actions\.dao-user-actions-open\s*{[^}]*pointer-events:\s*auto;/s);
       expect(cssText).not.toMatch(
           /\.dao-user-message-line\s*>\s*\.dao-user-actions\s*{[^}]*display:\s*none;/s);
     });

  it('bottom-aligns user message actions with the visible bubble', () => {
    const cssText = readFileSync(
        'src/dao/browser/ui/webui/resources/agent/agent.css', 'utf8');
    expect(cssText).toMatch(
        /\.dao-user-message-line\s*>\s*\.dao-user-actions\s*{[^}]*align-self:\s*flex-end;/s);
    expect(cssText).not.toMatch(
        /\.dao-user-message-line\s*>\s*\.dao-user-actions\s*{[^}]*align-self:\s*center;/s);
  });

  it('keeps the composer send icon in the standard Lucide send orientation',
     () => {
       const cssText = readFileSync(
           'src/dao/browser/ui/webui/resources/agent/agent.css', 'utf8');
       expect(cssText).toMatch(
           /dao-agent-app\s+message-editor\s+button\s*>\s*div\[style\*="rotate\(-45deg\)"\]\s*{[^}]*transform:\s*none\s*!important;/s);
     });

  it('keeps the user action menu anchored to the button and flips upward near the bottom',
     () => {
       const innerHeightDesc =
           Object.getOwnPropertyDescriptor(window, 'innerHeight');
       const innerWidthDesc =
           Object.getOwnPropertyDescriptor(window, 'innerWidth');
       Object.defineProperty(window, 'innerHeight', {
         configurable: true,
         value: 220,
       });
       Object.defineProperty(window, 'innerWidth', {
         configurable: true,
         value: 260,
       });
       try {
         const view = viewWithMessages([
           {role: 'user', content: 'bottom prompt', dao: {id: 'u1'}},
         ]);
         const panel = document.createElement('div');
         const host = document.createElement('user-message');
         const flex = document.createElement('div');
         flex.className = 'flex justify-start mx-4';
         const bubble = document.createElement('div');
         bubble.className = 'user-message-container';
         const markdown = document.createElement('markdown-block');
         bubble.appendChild(markdown);
         flex.appendChild(bubble);
         host.appendChild(flex);
         panel.appendChild(host);
         view.panel_ = panel;

         view._daoTestRefreshAssistantActions();
         const more =
             panel.querySelector('.dao-user-more-btn') as HTMLButtonElement;
         more.getBoundingClientRect = () => ({
           x: 196,
           y: 188,
           top: 188,
           right: 220,
           bottom: 212,
           left: 196,
           width: 24,
           height: 24,
           toJSON: () => ({}),
         }) as DOMRect;

         more.click();

         const menu = panel.querySelector(
             '.dao-user-action-menu') as HTMLElement|null;
         expect(menu).toBeTruthy();
         expect(menu?.classList.contains('dao-user-action-menu-above'))
             .toBe(true);
         expect(menu?.style.position).toBe('');
         expect(menu?.style.bottom).toBe('');
       } finally {
         restorePropertyDescriptor(window, 'innerHeight', innerHeightDesc);
         restorePropertyDescriptor(window, 'innerWidth', innerWidthDesc);
       }
     });

  it('opens the user action menu to the right when the button is near the left edge',
     () => {
       const innerWidthDesc =
           Object.getOwnPropertyDescriptor(window, 'innerWidth');
       Object.defineProperty(window, 'innerWidth', {
         configurable: true,
         value: 180,
       });
       try {
         const view = viewWithMessages([
           {role: 'user', content: 'left edge prompt', dao: {id: 'u1'}},
         ]);
         const panel = document.createElement('div');
         const host = document.createElement('user-message');
         const flex = document.createElement('div');
         flex.className = 'flex justify-start mx-4';
         const bubble = document.createElement('div');
         bubble.className = 'user-message-container';
         const markdown = document.createElement('markdown-block');
         bubble.appendChild(markdown);
         flex.appendChild(bubble);
         host.appendChild(flex);
         panel.appendChild(host);
         view.panel_ = panel;

         view._daoTestRefreshAssistantActions();
         const more =
             panel.querySelector('.dao-user-more-btn') as HTMLButtonElement;
         more.getBoundingClientRect = () => ({
           x: 20,
           y: 40,
           top: 40,
           right: 44,
           bottom: 64,
           left: 20,
           width: 24,
           height: 24,
           toJSON: () => ({}),
         }) as DOMRect;

         more.click();

         const menu = panel.querySelector(
             '.dao-user-action-menu') as HTMLElement|null;
         expect(menu).toBeTruthy();
         expect(menu?.classList.contains('dao-user-action-menu-align-start'))
             .toBe(true);
       } finally {
         restorePropertyDescriptor(window, 'innerWidth', innerWidthDesc);
       }
     });

  it('closes the user action menu when clicking outside it', () => {
    const view = viewWithMessages([
      {role: 'user', content: 'close menu', dao: {id: 'u1'}},
    ]);
    const panel = document.createElement('div');
    const host = document.createElement('user-message');
    const flex = document.createElement('div');
    flex.className = 'flex justify-start mx-4';
    const bubble = document.createElement('div');
    bubble.className = 'user-message-container';
    const markdown = document.createElement('markdown-block');
    bubble.appendChild(markdown);
    flex.appendChild(bubble);
    host.appendChild(flex);
    panel.appendChild(host);
    view.panel_ = panel;

    view._daoTestRefreshAssistantActions();
    const more =
        panel.querySelector('.dao-user-more-btn') as HTMLButtonElement|null;
    more?.click();
    expect(panel.querySelector('.dao-user-action-menu')).toBeTruthy();

    document.body.dispatchEvent(new Event('pointerdown', {bubbles: true}));

    expect(panel.querySelector('.dao-user-action-menu')).toBeNull();
  });

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
    const paths = Array.from(rewind?.querySelectorAll('svg path') ?? [])
                      .map(path => path.getAttribute('d'));
    expect(paths).toEqual([
      'm15 14 5-5-5-5',
      'M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13',
    ]);
  });

  it('shows full user context from the debug-only message menu', () => {
    const fullPageContext =
        '<current-webpage>\nFull page context body\n</current-webpage>';
    localStorage.setItem('dao_agent_debug_mode', 'true');
    const view = viewWithMessages([
      {
        role: 'user-with-attachments',
        content: 'summarize this page',
        timestamp: '2026-06-23T08:00:00.000Z',
        dao: {id: 'u1'},
        attachments: [{
          id: 'dao-page-1',
          daoPageUrl: 'https://example.com/docs',
          daoPageTitle: 'Example Docs',
          fileName: 'Example Docs.md',
          extractedText: fullPageContext,
        }],
      },
    ]);
    const panel = document.createElement('div');
    const host = document.createElement('user-message');
    const flex = document.createElement('div');
    flex.className = 'flex justify-start mx-4';
    const bubble = document.createElement('div');
    bubble.className = 'user-message-container';
    const markdown = document.createElement('markdown-block');
    markdown.getBoundingClientRect = () => ({
      top: 100,
      right: 640,
      bottom: 140,
      left: 520,
      width: 120,
      height: 40,
      x: 520,
      y: 100,
      toJSON: () => ({}),
    }) as DOMRect;
    bubble.appendChild(markdown);
    flex.appendChild(bubble);
    host.appendChild(flex);
    panel.appendChild(host);
    view.panel_ = panel;

    view._daoTestRefreshAssistantActions();
    const more =
        panel.querySelector('.dao-user-more-btn') as HTMLButtonElement|null;
    more?.click();

    const debugItem = panel.querySelector(
        '.dao-debug-context-menu-item') as HTMLButtonElement|null;
    expect(debugItem).toBeTruthy();
    expect(debugItem?.textContent).toContain(
        'chat.message_actions.view_context');

    const originalInnerWidth =
        Object.getOwnPropertyDescriptor(window, 'innerWidth');
    Object.defineProperty(window, 'innerWidth', {
      configurable: true,
      value: 700,
    });

    let rendered = '';
    try {
      debugItem?.click();
      const inlinePanel = panel.querySelector('.dao-user-context-wrap');
      expect(inlinePanel).toBeNull();
      rendered = templateText(
          (view as unknown as {render: () => unknown}).render());
    } finally {
      restorePropertyDescriptor(window, 'innerWidth', originalInnerWidth);
    }
    expect(rendered).toContain('class="dao-user-context-scrim"');
    expect(rendered).toContain('class="dao-user-context-modal"');
    expect(rendered).not.toContain('--dao-user-context-top');
    expect(rendered).not.toContain('--dao-user-context-right');
    const cssText = readFileSync(
        'src/dao/browser/ui/webui/resources/agent/agent.css', 'utf8');
    expect(cssText).toMatch(
        /\.dao-user-context-modal\s*{[^}]*top:\s*50%;/s);
    expect(cssText).toMatch(
        /\.dao-user-context-modal\s*{[^}]*left:\s*50%;/s);
    expect(cssText).toMatch(
        /\.dao-user-context-modal\s*{[^}]*transform:\s*translate\(-50%,\s*-50%\);/s);
    expect(rendered).toContain('role="dialog"');
    expect(rendered).toContain('aria-modal="true"');
    expect(rendered).toContain('chat.message_actions.context_title');
    expect(rendered).toContain('Full page context body');
    expect(rendered).toContain('llmMessagesUpToMessage');
    expect(rendered).toContain('summarize this page');
  });

  it('hides the context action until General debug mode is enabled', () => {
    localStorage.removeItem('dao_agent_debug_mode');
    const view = viewWithMessages([
      {role: 'user', content: 'plain prompt', dao: {id: 'u1'}},
    ]);
    const panel = document.createElement('div');
    const host = document.createElement('user-message');
    const flex = document.createElement('div');
    flex.className = 'flex justify-start mx-4';
    const bubble = document.createElement('div');
    bubble.className = 'user-message-container';
    flex.appendChild(bubble);
    host.appendChild(flex);
    panel.appendChild(host);
    view.panel_ = panel;

    view._daoTestRefreshAssistantActions();
    const more =
        panel.querySelector('.dao-user-more-btn') as HTMLButtonElement|null;
    more?.click();

    expect(panel.querySelector('.dao-debug-context-menu-item')).toBeNull();
  });

  it('regenerates from the user paired with the selected assistant', async () => {
    const messages = selectedAssistantHistory();
    const view = viewWithMessages(messages);

    view._daoTestEnsureMessageIds();
    const oldAssistantId = messages[1].dao.id;

    await view._daoTestRegenerateAssistantById(oldAssistantId);

    expect(view.agent_.continue).toHaveBeenCalledTimes(1);
    expect(view.agent_.state.messages.map(({role, content}) => ({
      role,
      content,
    }))).toEqual([
      {role: 'user', content: 'old user prompt'},
    ]);
    expect(view.agent_.state.messages).not.toContainEqual(
        expect.objectContaining({content: 'latest user prompt'}));
  });

  it('clears a proactive suggestion when regenerating an assistant answer',
     async () => {
       const messages = selectedAssistantHistory();
       const view = viewWithMessages(messages) as ReturnType<
           typeof viewWithMessages> & {
             onProactiveSuggestion_: (raw: unknown) => void;
             proactiveSuggestion_: {scenarioId: string; text: string} | null;
             renderProactiveCard_: () => unknown;
             clearProactiveIgnoredTimer_: () => void;
           };
       view._daoTestEnsureMessageIds();
       const oldAssistantId = messages[1].dao.id;
       pickerMocks.callNativeArgs.mockClear();

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           reason: 'You are on an open pull request',
           expectedOutcome: 'Catch risky changes before merge',
           contextDisclosure:
               'Dao will attach the current PR page after you run it.',
           actionPrompt: 'Review:\n{page_content}',
           requiresPageContent: true,
           tabId: 42,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 0.93,
         });
         await Promise.resolve();
         expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
         pickerMocks.callNativeArgs.mockClear();

         await view._daoTestRegenerateAssistantById(oldAssistantId);

         expect(view.proactiveSuggestion_).toBeNull();
         expect(templateText(view.renderProactiveCard_()))
             .not.toContain('chat.proactive.title.review_code');
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
         expect(view.agent_.continue).toHaveBeenCalledTimes(1);
       } finally {
         localStorage.removeItem('dao_proactive_not_now_snoozes');
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('clears a proactive suggestion when starting a new session', async () => {
    const view = viewWithMessages([
      {role: 'user', content: 'existing prompt', dao: {id: 'u1'}},
      {role: 'assistant', content: 'existing answer', dao: {id: 'a1'}},
    ]) as ReturnType<typeof viewWithMessages> & {
      onProactiveSuggestion_: (raw: unknown) => void;
      proactiveSuggestion_: {scenarioId: string; text: string} | null;
      renderProactiveCard_: () => unknown;
      clearProactiveIgnoredTimer_: () => void;
      startNewSession: () => void;
    };
    pickerMocks.callNativeArgs.mockClear();

    try {
      view.onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        reason: 'You are on an open pull request',
        expectedOutcome: 'Catch risky changes before merge',
        contextDisclosure:
            'Dao will attach the current PR page after you run it.',
        actionPrompt: 'Review:\n{page_content}',
        requiresPageContent: true,
        tabId: 42,
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
        confidence: 0.93,
      });
      await Promise.resolve();
      expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
      pickerMocks.callNativeArgs.mockClear();

      view.startNewSession();
      await Promise.resolve();

      expect(view.proactiveSuggestion_).toBeNull();
      expect(templateText(view.renderProactiveCard_()))
          .not.toContain('chat.proactive.title.review_code');
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'ignored',
          }));
    } finally {
      view.clearProactiveIgnoredTimer_();
    }
  });

  it('clears a proactive suggestion when loading a history session',
     async () => {
       const view = viewWithMessages([
         {role: 'user', content: 'current prompt', dao: {id: 'u1'}},
         {role: 'assistant', content: 'current answer', dao: {id: 'a1'}},
       ]) as ReturnType<typeof viewWithMessages> & {
         onHistorySelect_: (e: CustomEvent<{id: string}>) => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string; text: string} | null;
         renderProactiveCard_: () => unknown;
         clearProactiveIgnoredTimer_: () => void;
       };
       storageMocks.loadSession.mockResolvedValueOnce({
         messages: [
           {role: 'user', content: 'restored prompt', dao: {id: 'u2'}},
           {role: 'assistant', content: 'restored answer', dao: {id: 'a2'}},
         ],
       });
       pickerMocks.callNativeArgs.mockClear();

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           reason: 'You are on an open pull request',
           expectedOutcome: 'Catch risky changes before merge',
           contextDisclosure:
               'Dao will attach the current PR page after you run it.',
           actionPrompt: 'Review:\n{page_content}',
           requiresPageContent: true,
           tabId: 42,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 0.93,
         });
         await Promise.resolve();
         expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
         pickerMocks.callNativeArgs.mockClear();

         await view.onHistorySelect_(
             new CustomEvent('history-select', {detail: {id: 'sess-2'}}));

         expect(view.currentSessionId_).toBe('sess-2');
         expect(view.agent_.state.messages.map(({content}) => content))
             .toEqual(['restored prompt', 'restored answer']);
         expect(view.proactiveSuggestion_).toBeNull();
         expect(templateText(view.renderProactiveCard_()))
             .not.toContain('chat.proactive.title.review_code');
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
       } finally {
         localStorage.removeItem('dao_proactive_not_now_snoozes');
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('edits a user message without edit history, truncates later messages, and regenerates',
     async () => {
       const isoLikeTimestamp =
           /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/;
       const attachments = [{id: 'dao-page-1', extractedText: 'page'}];
       const messages = [
         {
           role: 'user-with-attachments',
           content: 'old prompt',
           dao: {
             id: 'u1',
             editHistory: [
               {content: 'very old prompt', editedAt: '2026-01-01'},
             ],
           },
           attachments,
         },
         {role: 'assistant', content: 'old answer', dao: {id: 'a1'}},
         {role: 'user', content: 'later prompt', dao: {id: 'u2'}},
       ];
       const originalMessage = messages[0];
       const view = viewWithMessages(messages);
       const panel = document.createElement('div');
       const iface = document.createElement('agent-interface') as HTMLElement & {
         requestUpdate: ReturnType<typeof vi.fn>;
       };
       iface.requestUpdate = vi.fn();
       panel.appendChild(iface);
       view.panel_ = panel;

       await view._daoTestApplyUserMessageEdit('u1', 'new prompt');

       expect(view.agent_.state.messages).toHaveLength(1);
       const editedMessage = view.agent_.state.messages[0];
       expect(editedMessage).not.toBe(originalMessage);
       expect(originalMessage.content).toBe('old prompt');
       expect(editedMessage.content).toBe('new prompt');
       expect(editedMessage.attachments).toEqual(attachments);
       expect(editedMessage.dao.editedAt).toMatch(isoLikeTimestamp);
       expect(editedMessage.dao.editHistory).toBeUndefined();
       expect(iface.requestUpdate).toHaveBeenCalled();
       expect(view.agent_.continue).toHaveBeenCalled();
     });

  it('clears a deferred proactive suggestion when applying a user edit',
     async () => {
       const messages = [
         {role: 'user', content: 'old prompt', dao: {id: 'u1'}},
         {role: 'assistant', content: 'old answer', dao: {id: 'a1'}},
       ];
       const view = viewWithMessages(messages) as ReturnType<
           typeof viewWithMessages> & {
             editingMessageId_: string;
             onProactiveSuggestion_: (raw: unknown) => void;
             proactiveIgnoredTimer_: number | null;
             proactiveSuggestion_: {scenarioId: string; text: string} | null;
             renderProactiveCard_: () => unknown;
             clearProactiveIgnoredTimer_: () => void;
           };
       const panel = document.createElement('div');
       const iface = document.createElement('agent-interface') as HTMLElement & {
         requestUpdate: ReturnType<typeof vi.fn>;
       };
       iface.requestUpdate = vi.fn();
       panel.appendChild(iface);
       view.panel_ = panel;
       view.editingMessageId_ = 'u1';
       pickerMocks.callNativeArgs.mockClear();

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           reason: 'You are on an open pull request',
           expectedOutcome: 'Catch risky changes before merge',
           contextDisclosure:
               'Dao will attach the current PR page after you run it.',
           actionPrompt: 'Review:\n{page_content}',
           requiresPageContent: true,
           tabId: 42,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 0.93,
         });
         await Promise.resolve();
         expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
         expect(view.proactiveIgnoredTimer_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         await view._daoTestApplyUserMessageEdit('u1', 'new prompt');

         expect(view.proactiveSuggestion_).toBeNull();
         expect(templateText(view.renderProactiveCard_()))
             .not.toContain('chat.proactive.title.review_code');
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback', expect.anything());
         expect(view.agent_.continue).toHaveBeenCalled();
       } finally {
         localStorage.removeItem('dao_proactive_not_now_snoozes');
         view.clearProactiveIgnoredTimer_();
       }
     });

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

  it('clears a proactive suggestion when rewinding to an earlier answer',
     async () => {
       const view = viewWithMessages([
         {role: 'user', content: 'first prompt', dao: {id: 'u1'}},
         {role: 'assistant', content: 'first answer', dao: {id: 'a1'}},
         {role: 'user', content: 'second prompt', dao: {id: 'u2'}},
         {role: 'assistant', content: 'second answer', dao: {id: 'a2'}},
       ]) as ReturnType<typeof viewWithMessages> & {
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string; text: string} | null;
         renderProactiveCard_: () => unknown;
         clearProactiveIgnoredTimer_: () => void;
       };
       attachMessageHosts(view);
       pickerMocks.callNativeArgs.mockClear();

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           reason: 'You are on an open pull request',
           expectedOutcome: 'Catch risky changes before merge',
           contextDisclosure:
               'Dao will attach the current PR page after you run it.',
           actionPrompt: 'Review:\n{page_content}',
           requiresPageContent: true,
           tabId: 42,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 0.93,
         });
         await Promise.resolve();
         expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
         pickerMocks.callNativeArgs.mockClear();

         await view._daoTestRewindAssistantById('a1');

         expect(view.agent_.state.messages.map(({content}) => content))
             .toEqual(['first prompt', 'first answer']);
         expect(view.proactiveSuggestion_).toBeNull();
         expect(templateText(view.renderProactiveCard_()))
             .not.toContain('chat.proactive.title.review_code');
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

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

  it('rejects an empty user edit without truncating messages', async () => {
    const messages = [
      {role: 'user', content: 'old prompt', dao: {id: 'u1'}},
      {role: 'assistant', content: 'old answer', dao: {id: 'a1'}},
    ];
    const view = viewWithMessages(messages);

    await view._daoTestApplyUserMessageEdit('u1', '   ');

    expect(view.agent_.state.messages.map(({content}) => content)).toEqual([
      'old prompt',
      'old answer',
    ]);
  });

  it('aborts an active stream before saving an already-open edit', async () => {
    const messages = [
      {role: 'user', content: 'old prompt', dao: {id: 'u1'}},
      {role: 'assistant', content: 'old answer', dao: {id: 'a1'}},
    ];
    const view = viewWithMessages(messages);
    view.agent_.state.isStreaming = true;
    view.agent_.abort = vi.fn(() => {
      expect(view.agent_.state.messages.map(({content}) => content)).toEqual([
        'old prompt',
        'old answer',
      ]);
    });
    view.agent_.continue = vi.fn(async () => undefined);

    await view._daoTestApplyUserMessageEdit('u1', 'new prompt');

    expect(view.agent_.abort).toHaveBeenCalled();
    expect(view.agent_.state.messages.map(({content}) => content)).toEqual([
      'new prompt',
    ]);
  });

  it('copies the selected assistant text instead of the latest assistant text',
     async () => {
       const messages = selectedAssistantHistory();
       const view = viewWithMessages(messages);
       const writeText = vi.fn(async () => undefined);
       const originalClipboardDescriptor =
           Object.getOwnPropertyDescriptor(navigator, 'clipboard');
       const originalClipboardItemDescriptor =
           Object.getOwnPropertyDescriptor(window, 'ClipboardItem');
       Object.defineProperty(navigator, 'clipboard', {
         configurable: true,
         value: {writeText},
       });
       Object.defineProperty(window, 'ClipboardItem', {
         configurable: true,
         value: undefined,
       });

       try {
         view._daoTestEnsureMessageIds();
         const oldAssistantId = messages[1].dao.id;

         await view._daoTestCopyAssistantById(oldAssistantId);

         expect(writeText).toHaveBeenCalledWith('old assistant answer');
         expect(writeText).not.toHaveBeenCalledWith('latest assistant answer');
       } finally {
         restorePropertyDescriptor(
             navigator, 'clipboard', originalClipboardDescriptor);
         restorePropertyDescriptor(
             window, 'ClipboardItem', originalClipboardItemDescriptor);
       }
     });

  it('renders a share image from the selected assistant pair', async () => {
    const messages = selectedAssistantHistory();
    const view = viewWithMessages(messages);
    const renderShareImageMock = vi.mocked(renderShareImage);
    const copyPngBlobToClipboardMock = vi.mocked(copyPngBlobToClipboard);
    renderShareImageMock.mockResolvedValue(new Blob(['png'], {
      type: 'image/png',
    }));
    copyPngBlobToClipboardMock.mockResolvedValue(undefined);

    view._daoTestEnsureMessageIds();
    const oldAssistantId = messages[1].dao.id;

    await view._daoTestShareAssistantAsImageById(oldAssistantId);

    expect(renderShareImageMock).toHaveBeenCalledWith(expect.objectContaining({
      question: 'old user prompt',
      answer: 'old assistant answer',
    }));
    expect(renderShareImageMock).not.toHaveBeenCalledWith(
        expect.objectContaining({
          question: 'latest user prompt',
          answer: 'latest assistant answer',
        }));
    const blob = await renderShareImageMock.mock.results[0]!.value;
    expect(copyPngBlobToClipboardMock).toHaveBeenCalledWith(blob);
  });
});

describe('dao-chat-view element picker', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    clearReusableElementContext();
    pickerMocks.startElementPicker.mockReset();
    pickerMocks.cancelElementPicker.mockReset();
    pickerMocks.captureElementScreenshotFromPage.mockReset();
    pickerMocks.callNative.mockReset();
    pickerMocks.callNativeArgs.mockReset();
    pickerMocks.callNative.mockResolvedValue({success: true});
    pickerMocks.callNativeArgs.mockResolvedValue({success: true});
    pickerMocks.webUiListeners = {};
    skillMocks.skills = [];
    skillMocks.initSkillRegistry.mockReset();
    skillMocks.loadSkillInstructions.mockReset();
    skillMocks.loadSkillInstructions.mockResolvedValue(null);
    skillMocks.refreshSkillRegistry.mockReset();
    skillMocks.refreshSkillRegistry.mockResolvedValue(undefined);
    skillMocks.refreshSkillRegistryIfStale.mockReset();
    skillMocks.refreshSkillRegistryIfStale.mockResolvedValue(false);
    localStorage.clear();
  });

  afterEach(() => {
    document.body.innerHTML = '';
    clearReusableElementContext();
    vi.restoreAllMocks();
  });

  it('focuses the composer immediately after an element is selected', async () => {
    pickerMocks.startElementPicker.mockResolvedValue(sampleContext());
    const view = document.createElement('dao-chat-view') as HTMLElement;
    const panel = document.createElement('div');
    const editor = document.createElement('message-editor');
    const textarea = document.createElement('textarea');
    const focusSpy = vi.spyOn(textarea, 'focus');
    editor.appendChild(textarea);
    panel.appendChild(editor);
    (view as unknown as {panel_: HTMLElement}).panel_ = panel;

    await (view as unknown as {onElementPickClick_: () => Promise<void>})
        .onElementPickClick_();
    await Promise.resolve();

    expect(pickerMocks.callNative).toHaveBeenCalledWith('focusAgentSidebar');
    expect(focusSpy).toHaveBeenCalled();
  });

  it('adds an element screenshot as an image attachment in the composer',
     async () => {
    const imageAttachment = {
      id: 'dao-element-shot-1',
      type: 'image',
      fileName: 'Sign in button.jpg',
      mimeType: 'image/jpeg',
      size: 12,
      content: 'base64-jpeg',
      preview: 'base64-jpeg',
    };
    pickerMocks.captureElementScreenshotFromPage.mockResolvedValue(
        imageAttachment);

    const view = document.createElement('dao-chat-view') as HTMLElement;
    const panel = document.createElement('div');
    const editor = document.createElement('message-editor') as HTMLElement & {
      attachments?: unknown[];
      requestUpdate: ReturnType<typeof vi.fn>;
    };
    editor.attachments = [];
    editor.requestUpdate = vi.fn();
    const textarea = document.createElement('textarea');
    editor.appendChild(textarea);
    panel.appendChild(editor);
    (view as unknown as {panel_: HTMLElement}).panel_ = panel;

    await (view as unknown as {onElementScreenshotClick_: () => Promise<void>})
        .onElementScreenshotClick_();

    expect(editor.attachments).toEqual([
      imageAttachment,
    ]);
    expect(editor.requestUpdate).toHaveBeenCalled();
    expect((view as any).pendingElementContexts_ || []).toEqual([]);
  });

  it('places the element screenshot button before the composer send button',
     () => {
       const view = document.createElement('dao-chat-view') as HTMLElement;
       const panel = document.createElement('div');
       const editor = document.createElement('message-editor');
       const row = document.createElement('div');
       const modelButton = document.createElement('button');
       const sendButton = document.createElement('button');
       row.append(modelButton, sendButton);
       editor.appendChild(row);
       panel.appendChild(editor);

       (view as unknown as {
         attachElementScreenshotButton_: (panel: HTMLElement) => void;
       }).attachElementScreenshotButton_(panel);

       const screenshotButton =
           row.querySelector('.dao-element-screenshot-button');
       expect(screenshotButton).toBeTruthy();
       expect(screenshotButton?.nextElementSibling).toBe(sendButton);
     });

  it('sends only the element screenshot image attachment', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);
    const imageAttachment = {
      id: 'dao-element-shot-1',
      type: 'image',
      fileName: 'Sign in button.jpg',
      mimeType: 'image/jpeg',
      size: 12,
      content: 'base64-jpeg',
      preview: 'base64-jpeg',
    };
    Object.assign(view, {
      suppressChipAttachOnce_: false,
      pendingPageAttachment_: null,
      pendingSelection_: null,
      pendingElementContexts_: [],
    });

    try {
      await iface.sendMessage('what is this?', [imageAttachment]);

      const attachments = originalSend.mock.calls[0][1];
      expect(attachments).toHaveLength(1);
      expect(attachments[0].type).toBe('image');
      expect(JSON.stringify(attachments)).not.toContain('<element-context');
      expect(JSON.stringify(attachments)).not.toContain('.element.json');
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('sends element screenshots to the LLM as image_url content parts',
     async () => {
       let viewRef: HTMLElement | null = null;
       let llmMessages: any[] = [];
       const originalSend = vi.fn(async (text: string, attachments: any[]) => {
         const agent = (viewRef as unknown as {agent_: {
           convertToLlm: (msgs: any[]) => any[];
         }}).agent_;
         llmMessages = agent.convertToLlm([{
           role: 'user-with-attachments',
           content: text,
           attachments,
           timestamp: Date.now(),
         }]);
         return 'sent';
       });
       const {view, iface} = await mountChatViewWithSend(originalSend);
       viewRef = view;
       const imageAttachment = {
         id: 'dao-element-shot-1',
         type: 'image',
         fileName: 'Sign in button.jpg',
         mimeType: 'image/jpeg',
         size: 12,
         content: 'base64-jpeg',
         preview: 'base64-jpeg',
       };
       Object.assign(view, {
         suppressChipAttachOnce_: false,
         pendingPageAttachment_: null,
         pendingSelection_: null,
         pendingElementContexts_: [],
       });

       try {
         await iface.sendMessage('what is this?', [imageAttachment]);

         expect(llmMessages).toHaveLength(1);
         expect(llmMessages[0].role).toBe('user');
         expect(llmMessages[0].content).toEqual([
           {type: 'text', text: 'what is this?'},
           {
             type: 'image_url',
             image_url: {
               url: 'data:image/jpeg;base64,base64-jpeg',
               detail: 'auto',
             },
           },
         ]);
       } finally {
         clearTabWatchTimer(view);
       }
     });

  it('brackets a user send with an agent turn tab target', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);

    try {
      const result = await iface.sendMessage('hello', []);

      expect(result).toBe('sent');
      expect(originalSend).toHaveBeenCalledWith('hello', []);
      expect(pickerMocks.callNative.mock.calls.map(call => call[0])
                 .filter(method => method === 'beginAgentTurn' ||
                     method === 'endAgentTurn'))
          .toEqual([
            'beginAgentTurn',
            'endAgentTurn',
          ]);
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('clears a visible proactive suggestion when the user sends a manual message',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view, iface} = await mountChatViewWithSend(originalSend);
       Object.assign(view, {suppressChipAttachOnce_: true});
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveIgnoredTimer_: number|null;
         proactiveSuggestion_: unknown;
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_).not.toBeNull();
         expect(typedView.proactiveIgnoredTimer_).not.toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         await expect(iface.sendMessage('I have a different question', []))
             .resolves.toBe('sent');

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(typedView.proactiveIgnoredTimer_).toBeNull();
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
         expect(originalSend).toHaveBeenCalledWith(
             'I have a different question', []);
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('snoozes a visible proactive suggestion after manual send clears it',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view, iface} = await mountChatViewWithSend(originalSend);
       Object.assign(view, {suppressChipAttachOnce_: true});
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: unknown;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'You are on a pull request page.',
         expectedOutcome: 'Catch risky changes before merge.',
         contextDisclosure: 'Dao will attach the page after you run this.',
         confidence: 0.97,
         url: 'https://github.com/acme/repo/pull/123',
         domain: 'github.com',
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_).not.toBeNull();

         await expect(iface.sendMessage('I have a different question', []))
             .resolves.toBe('sent');
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           confidence: 1,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('silently clears an unshown proactive suggestion when the user sends manually',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view, iface} = await mountChatViewWithSend(originalSend);
       Object.assign(view, {suppressChipAttachOnce_: true});
       const textarea = document.createElement('textarea');
       textarea.value = 'I already started typing';
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         composerTextarea_: HTMLTextAreaElement|null;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveIgnoredTimer_: number|null;
         proactiveSuggestion_: unknown;
       };
       typedView.composerTextarea_ = textarea;
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_).not.toBeNull();
         expect(typedView.proactiveIgnoredTimer_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         await expect(iface.sendMessage('I already started typing', []))
             .resolves.toBe('sent');

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
         expect(originalSend).toHaveBeenCalledWith(
             'I already started typing', []);
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('snoozes an unshown proactive suggestion after manual send clears it',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view, iface} = await mountChatViewWithSend(originalSend);
       Object.assign(view, {suppressChipAttachOnce_: true});
       const textarea = document.createElement('textarea');
       textarea.value = 'I already started typing';
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         composerTextarea_: HTMLTextAreaElement|null;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: unknown;
       };
       typedView.composerTextarea_ = textarea;
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'You are on a pull request page.',
         expectedOutcome: 'Catch risky changes before merge.',
         contextDisclosure: 'Dao will attach the page after you run this.',
         confidence: 0.97,
         url: 'https://github.com/acme/repo/pull/123',
         domain: 'github.com',
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_).not.toBeNull();

         await expect(iface.sendMessage('I already started typing', []))
             .resolves.toBe('sent');
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         textarea.value = '';
         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           confidence: 1,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('ends the agent turn when the original send rejects', async () => {
    const originalSend = vi.fn(async () => {
      throw new Error('send failed');
    });
    const {view, iface} = await mountChatViewWithSend(originalSend);

    try {
      await expect(iface.sendMessage('hello', [])).rejects.toThrow(
          'send failed');
      expect(pickerMocks.callNative.mock.calls.map(call => call[0])
                 .filter(method => method === 'beginAgentTurn' ||
                     method === 'endAgentTurn'))
          .toEqual([
            'beginAgentTurn',
            'endAgentTurn',
          ]);
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('keeps proactive suggestion feedback untouched when manual send fails',
     async () => {
       const originalSend = vi.fn(async () => {
         throw new Error('send failed');
       });
       const {view, iface} = await mountChatViewWithSend(originalSend);
       Object.assign(view, {suppressChipAttachOnce_: true});
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveIgnoredTimer_: number|null;
         proactiveSuggestion_: unknown;
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_).not.toBeNull();
         expect(typedView.proactiveIgnoredTimer_).not.toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         await expect(iface.sendMessage('I have a different question', []))
             .rejects.toThrow('send failed');

         expect(typedView.proactiveSuggestion_).not.toBeNull();
         expect(typedView.proactiveIgnoredTimer_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('does not end an agent turn that failed to start', async () => {
    const warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);
    pickerMocks.callNative.mockImplementation(async method => {
      if (method === 'beginAgentTurn') {
        throw new Error('no tab');
      }
      return {success: true};
    });

    try {
      await expect(iface.sendMessage('hello', [])).resolves.toBe('sent');
      expect(originalSend).toHaveBeenCalledWith('hello', []);
      expect(pickerMocks.callNative.mock.calls.map(call => call[0])
                 .filter(method => method === 'beginAgentTurn' ||
                     method === 'endAgentTurn'))
          .toEqual([
            'beginAgentTurn',
          ]);
    } finally {
      warnSpy.mockRestore();
      clearTabWatchTimer(view);
    }
  });

  it('includes the memory context contract in the system prompt', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);

    try {
      const prompt =
          (view as unknown as {buildSystemPrompt_: () => string})
              .buildSystemPrompt_();

      expect(prompt).toContain('<memory-context>');
      expect(prompt).toContain('historical, potentially stale personal context');
      expect(prompt).toContain('Current user instructions');
      expect(prompt).toContain('<soul>\nsoul\n</soul>');
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('declares the skill catalog prompt as Lit state', () => {
    const properties = (customElements.get('dao-chat-view') as unknown as {
      properties: Record<string, {state?: boolean}>;
    }).properties;

    expect(properties.skillCatalogPrompt_).toEqual({state: true});
  });

  it('injects enabled available skills into the next system prompt', async () => {
    const summarySkill = {
      id: 'summary',
      name: 'summary',
      description: 'Summarize the current page',
      source: 'builtin',
      hosts: ['*'],
      requiresPageContent: true,
      disabled: false,
    };
    skillMocks.refreshSkillRegistry.mockImplementation(async () => {
      skillMocks.skills = [summarySkill];
    });
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'getPageInfo') {
        return {url: 'https://example.com/article', title: 'Article'};
      }
      return {success: true};
    });
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);

    try {
      await iface.sendMessage('summarize this', []);
      const systemPrompt = (view as unknown as {
        agent_: {state: {systemPrompt: string}};
      }).agent_.state.systemPrompt;

      expect(systemPrompt).toContain('<available_skills>');
      expect(systemPrompt).toContain('id="summary"');
      expect(systemPrompt).toContain('Summarize the current page');
      expect(skillMocks.refreshSkillRegistry).toHaveBeenCalledTimes(1);
      expect(skillMocks.refreshSkillRegistryIfStale).not.toHaveBeenCalled();
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('instructs the agent how to activate matching available skills',
     async () => {
       skillMocks.skills = [{
         id: 'summary',
         name: 'summary',
         description: 'Summarize the current page',
         source: 'builtin',
         hosts: ['*'],
         requiresPageContent: true,
         disabled: false,
       }];
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {url: 'https://example.com/article', title: 'Article'};
         }
         return {success: true};
       });
       const originalSend = vi.fn(async () => 'sent');
       const {view, iface} = await mountChatViewWithSend(originalSend);

       try {
         await iface.sendMessage('summarize this', []);
         const systemPrompt = (view as unknown as {
           agent_: {state: {systemPrompt: string}};
         }).agent_.state.systemPrompt;

         expect(systemPrompt).toContain('<available_skills>');
         expect(systemPrompt).toContain('activate_skill');
         expect(systemPrompt).toContain('Do not invent skill ids');
         expect(systemPrompt).toContain('Skill instructions guide the task');
       } finally {
         clearTabWatchTimer(view);
       }
     });

  it('filters disabled and host-unavailable skills from the system prompt',
     async () => {
       skillMocks.skills = [
         {
           id: 'global-skill',
           name: 'global-skill',
           description: 'Works anywhere',
           source: 'user',
           hosts: ['*'],
           requiresPageContent: false,
           disabled: false,
         },
         {
           id: 'disabled-skill',
           name: 'disabled-skill',
           description: 'Disabled',
           source: 'user',
           hosts: ['*'],
           requiresPageContent: false,
           disabled: true,
         },
         {
           id: 'github-skill',
           name: 'github-skill',
           description: 'GitHub only',
           source: 'user',
           hosts: ['github.com'],
           requiresPageContent: false,
           disabled: false,
         },
       ];
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {url: 'https://example.com/app', title: 'Example'};
         }
         return {success: true};
       });
       const originalSend = vi.fn(async () => 'sent');
       const {view, iface} = await mountChatViewWithSend(originalSend);

       try {
         await iface.sendMessage('help me', []);
         const systemPrompt = (view as unknown as {
           agent_: {state: {systemPrompt: string}};
         }).agent_.state.systemPrompt;

         expect(systemPrompt).toContain('id="global-skill"');
         expect(systemPrompt).not.toContain('id="disabled-skill"');
         expect(systemPrompt).not.toContain('id="github-skill"');
       } finally {
         clearTabWatchTimer(view);
       }
     });

  it('keeps memory context hidden from visible attachments while sending it to LLM', async () => {
    let viewRef: HTMLElement | null = null;
    let llmMessages: any[] = [];
    const originalSend = vi.fn(async (text: string, attachments: any[]) => {
      const agent = (viewRef as unknown as {agent_: {
        convertToLlm: (msgs: any[]) => any[];
      }}).agent_;
      const msg = attachments.length > 0 ? {
        role: 'user-with-attachments',
        content: text,
        attachments,
        timestamp: Date.now(),
      } : {
        role: 'user',
        content: text,
        timestamp: Date.now(),
      };
      llmMessages = agent.convertToLlm([msg]);
      return 'sent';
    });
    const {view, iface} = await mountChatViewWithSend(originalSend);
    viewRef = view;
    Object.assign(view, {
      suppressChipAttachOnce_: false,
      pendingPageAttachment_: null,
      pendingSelection_: null,
      pendingElementContexts_: [],
    });
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {url: 'https://example.com/app', title: 'Example App'};
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockImplementation(async (method: string) => {
      if (method === 'getMemoryContext') {
        return {
          preferences: [{
            key: 'response.style',
            value: 'Prefers concise implementation notes',
            confidence: 0.92,
          }],
          episodes: [{
            title: 'Example App',
            intent: 'Summarize dashboard data',
            outcome: 'Returned grouped findings',
            confidence: 0.78,
          }],
          recentMessages: [{
            role: 'user',
            content: 'Duplicated recent message should stay out.',
          }],
          relevantSummary: {
            summary: 'User often asks for implementation-focused help here.',
            primaryDomain: 'example.com',
          },
        };
      }
      return {success: true};
    });

    try {
      await expect(iface.sendMessage('what next?', [])).resolves.toBe('sent');
      const firstSendCall = originalSend.mock.calls[0] || [];
      const sentAttachments = firstSendCall[1] || [];
      expect(sentAttachments).toHaveLength(0);
      expect(llmMessages).toHaveLength(1);
      expect(llmMessages[0].content).toContain('<memory-context');
      expect(llmMessages[0].content).toContain(
          'Prefers concise implementation notes');
      expect(llmMessages[0].content).toContain(
          'User often asks for implementation-focused help here.');
      expect(llmMessages[0].content).not.toContain(
          'Duplicated recent message should stay out.');
      expect(llmMessages[0].content).toContain('what next?');
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'getMemoryContext',
          'https://example.com/app',
          'example.com',
          '');
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('continues sending when memory context retrieval fails', async () => {
    const warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);
    Object.assign(view, {
      suppressChipAttachOnce_: false,
      pendingPageAttachment_: null,
      pendingSelection_: null,
      pendingElementContexts_: [],
    });
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {url: 'https://example.com/app', title: 'Example App'};
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockImplementation(async (method: string) => {
      if (method === 'getMemoryContext') {
        throw new Error('memory unavailable');
      }
      return {success: true};
    });

    try {
      await expect(iface.sendMessage('hello', [])).resolves.toBe('sent');
      expect(originalSend).toHaveBeenCalledWith('hello', []);
    } finally {
      warnSpy.mockRestore();
      clearTabWatchTimer(view);
    }
  });

  it('skips memory context when suppressChipAttachOnce_ is true', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);
    Object.assign(view, {
      suppressChipAttachOnce_: true,
      pendingPageAttachment_: null,
      pendingSelection_: null,
      pendingElementContexts_: [],
    });
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {url: 'https://example.com/app', title: 'Example App'};
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockImplementation(async (method: string) => {
      if (method === 'getMemoryContext') {
        return {
          preferences: [{
            key: 'response.style',
            value: 'Should not be attached',
            confidence: 0.92,
          }],
        };
      }
      return {success: true};
    });

    try {
      await expect(iface.sendMessage('hello', [])).resolves.toBe('sent');
      expect(originalSend).toHaveBeenCalledWith('hello', []);
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalled();
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('skips memory context for non-capturable internal URLs', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);
    Object.assign(view, {
      suppressChipAttachOnce_: false,
      pendingPageAttachment_: null,
      pendingSelection_: null,
      pendingElementContexts_: [],
    });
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {url: 'chrome://settings', title: 'Settings'};
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockImplementation(async (method: string) => {
      if (method === 'getMemoryContext') {
        return {
          preferences: [{
            key: 'response.style',
            value: 'Should not be attached',
            confidence: 0.92,
          }],
        };
      }
      return {success: true};
    });

    try {
      await expect(iface.sendMessage('hello', [])).resolves.toBe('sent');
      expect(originalSend).toHaveBeenCalledWith('hello', []);
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalled();
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('does not attach memory context for empty native payload', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);
    Object.assign(view, {
      suppressChipAttachOnce_: false,
      pendingPageAttachment_: null,
      pendingSelection_: null,
      pendingElementContexts_: [],
    });
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {url: 'https://example.com/app', title: 'Example App'};
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockImplementation(async (method: string) => {
      if (method === 'getMemoryContext') {
        return {};
      }
      return {success: true};
    });

    try {
      await expect(iface.sendMessage('hello', [])).resolves.toBe('sent');
      expect(originalSend).toHaveBeenCalledWith('hello', []);
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'getMemoryContext',
          'https://example.com/app',
          'example.com',
          '');
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('stores a proactive suggestion without running native page capture', async () => {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      proactiveSuggestion_: {
        scenarioId: string;
        text: string;
        reason: string;
        expectedOutcome: string;
        contextDisclosure: string;
      } | null;
      onProactiveSuggestion_: (raw: unknown) => void;
    };

    view.onProactiveSuggestion_({
      text: 'Review this PR',
      scenarioId: 'seed_github_pr',
      reason: 'You are on an open pull request',
      expectedOutcome: 'Catch risky changes before merge',
      contextDisclosure: 'Dao will attach the current PR page after you run it.',
      actionPrompt: 'Review:\n{page_content}',
      requiresPageContent: true,
      tabId: 42,
      url: 'https://github.com/acme/repo/pull/123',
      domain: 'github.com',
      confidence: 0.93,
    });
    await Promise.resolve();

    expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
    expect(view.proactiveSuggestion_?.text).toBe('Review this PR');
    expect(view.proactiveSuggestion_?.reason)
        .toBe('You are on an open pull request');
    expect(view.proactiveSuggestion_?.expectedOutcome)
        .toBe('Catch risky changes before merge');
    expect(view.proactiveSuggestion_?.contextDisclosure)
        .toBe('Dao will attach the current PR page after you run it.');
    expect(pickerMocks.callNative).not.toHaveBeenCalled();
    expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
        'getPageContentForScenario', 42);
    expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
        'recordActionFeedback',
        expect.objectContaining({
          scenarioId: 'seed_github_pr',
          domain: 'github.com',
          url: 'https://github.com/acme/repo/pull/123',
          confidence: 0.93,
          outcome: 'shown',
        }));
  });

  it('ignores a suppressed proactive scenario from native', async () => {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      proactiveSuggestion_: unknown;
      onProactiveSuggestion_: (raw: unknown) => void;
    };

    view.onProactiveSuggestion_({
      text: 'Review this PR',
      scenarioId: 'seed_github_pr',
      actionLabel: 'review_code',
      reason: 'missing_action_evidence',
      expectedOutcome: 'review_code',
      contextDisclosure: 'captures_after_run',
      suppressionReason: 'missing_action_evidence',
      url: 'https://github.com/acme/repo/pull/123',
      domain: 'github.com',
      confidence: 0.96,
    });
    await Promise.resolve();

    expect(view.proactiveSuggestion_).toBeNull();
    expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
        'recordActionFeedback',
        expect.objectContaining({
          scenarioId: 'seed_github_pr',
          outcome: 'shown',
        }));
  });

  it('ignores a low-confidence proactive scenario from native', async () => {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      proactiveSuggestion_: unknown;
      onProactiveSuggestion_: (raw: unknown) => void;
    };

    view.onProactiveSuggestion_({
      text: 'Summarize this page',
      scenarioId: 'seed_docs_summary',
      actionLabel: 'summarize_doc',
      reason: 'matched_structure',
      expectedOutcome: 'summarize_doc',
      contextDisclosure: 'captures_after_run',
      url: 'https://docs.example.com/guide',
      domain: 'docs.example.com',
      confidence: 0.2,
    });
    await Promise.resolve();

    expect(view.proactiveSuggestion_).toBeNull();
    expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
        'recordActionFeedback',
        expect.objectContaining({
          scenarioId: 'seed_docs_summary',
          outcome: 'shown',
        }));
  });

  it('keeps a stronger proactive suggestion when a weaker one arrives',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: {scenarioId: string; text: string} | null;
         onProactiveSuggestion_: (raw: unknown) => void;
       };

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'structured_content',
           expectedOutcome: 'review_code',
           contextDisclosure: 'captures_after_run',
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 0.92,
         });
         await Promise.resolve();
         expect(view.proactiveSuggestion_?.scenarioId)
             .toBe('seed_github_pr');
         pickerMocks.callNativeArgs.mockClear();

         view.onProactiveSuggestion_({
           text: 'Summarize this page',
           scenarioId: 'seed_docs_summary',
           actionLabel: 'summarize_doc',
           reason: 'matched_structure',
           expectedOutcome: 'summarize_doc',
           contextDisclosure: 'captures_after_run',
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 0.41,
         });
         await Promise.resolve();

         expect(view.proactiveSuggestion_?.scenarioId)
             .toBe('seed_github_pr');
         expect(view.proactiveSuggestion_?.text).toBe('Review this PR');
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_docs_summary',
               outcome: 'shown',
             }));
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('keeps the current proactive suggestion when a same-page candidate is only slightly weaker',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: {scenarioId: string; text: string} | null;
         onProactiveSuggestion_: (raw: unknown) => void;
       };

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'structured_content',
           expectedOutcome: 'review_code',
           contextDisclosure: 'captures_after_run',
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 0.92,
         });
         await Promise.resolve();
         expect(view.proactiveSuggestion_?.scenarioId)
             .toBe('seed_github_pr');
         pickerMocks.callNativeArgs.mockClear();

         view.onProactiveSuggestion_({
           text: 'Summarize this page',
           scenarioId: 'seed_docs_summary',
           actionLabel: 'summarize_doc',
           reason: 'matched_structure',
           expectedOutcome: 'summarize_doc',
           contextDisclosure: 'captures_after_run',
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 0.90,
         });
         await Promise.resolve();

         expect(view.proactiveSuggestion_?.scenarioId)
             .toBe('seed_github_pr');
         expect(view.proactiveSuggestion_?.text).toBe('Review this PR');
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_docs_summary',
               outcome: 'shown',
             }));
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('ignores duplicate proactive suggestion events after showing once',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: unknown;
         onProactiveSuggestion_: (raw: unknown) => void;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'structured_content',
         expectedOutcome: 'review_code',
         contextDisclosure: 'captures_after_run',
         actionPrompt: 'Review:\n{page_content}',
         requiresPageContent: true,
         tabId: 42,
         url: 'https://github.com/acme/repo/pull/123?utm_source=email#files',
         domain: 'github.com',
         confidence: 0.93,
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         view.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         const firstSuggestion = view.proactiveSuggestion_;
         expect(firstSuggestion).not.toBeNull();
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
         pickerMocks.callNativeArgs.mockClear();

         view.onProactiveSuggestion_({...rawSuggestion});
         await Promise.resolve();

         expect(view.proactiveSuggestion_).toBe(firstSuggestion);
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('defers shown feedback while the user is composing a message', async () => {
    const textarea = document.createElement('textarea');
    textarea.value = 'I am already writing a question';
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      composerTextarea_: HTMLTextAreaElement | null;
      proactiveIgnoredTimer_: number | null;
      proactiveSuggestion_: {scenarioId: string; text: string} | null;
      onProactiveSuggestion_: (raw: unknown) => void;
      renderProactiveCard_: () => unknown;
      clearProactiveIgnoredTimer_: () => void;
    };
    view.composerTextarea_ = textarea;
    pickerMocks.callNativeArgs.mockClear();

    try {
      view.onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        reason: 'You are on an open pull request',
        expectedOutcome: 'Catch risky changes before merge',
        contextDisclosure:
            'Dao will attach the current PR page after you run it.',
        actionPrompt: 'Review:\n{page_content}',
        requiresPageContent: true,
        tabId: 42,
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
        confidence: 0.93,
      });
      await Promise.resolve();

      expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
      expect(templateText(view.renderProactiveCard_()))
          .not.toContain('Review this PR');
      expect(view.proactiveIgnoredTimer_).toBeNull();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback', expect.anything());
    } finally {
      view.clearProactiveIgnoredTimer_();
    }
  });

  it('defers shown feedback while the user edits an earlier message',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         editingMessageId_: string;
         proactiveIgnoredTimer_: number | null;
         proactiveSuggestion_: {scenarioId: string; text: string} | null;
         onProactiveSuggestion_: (raw: unknown) => void;
         renderProactiveCard_: () => unknown;
         clearProactiveIgnoredTimer_: () => void;
       };
       view.editingMessageId_ = 'message-1';
       pickerMocks.callNativeArgs.mockClear();

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           reason: 'You are on an open pull request',
           expectedOutcome: 'Catch risky changes before merge',
           contextDisclosure:
               'Dao will attach the current PR page after you run it.',
           actionPrompt: 'Review:\n{page_content}',
           requiresPageContent: true,
           tabId: 42,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 0.93,
         });
         await Promise.resolve();

         expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
         expect(templateText(view.renderProactiveCard_()))
             .not.toContain('Review this PR');
         expect(view.proactiveIgnoredTimer_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback', expect.anything());
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('defers shown feedback while the history panel is open', async () => {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      openHistory: () => void;
      onHistoryClose_: () => void;
      proactiveIgnoredTimer_: number | null;
      proactiveSuggestion_: {scenarioId: string; text: string} | null;
      onProactiveSuggestion_: (raw: unknown) => void;
      renderProactiveCard_: () => unknown;
      clearProactiveIgnoredTimer_: () => void;
    };
    view.openHistory();
    pickerMocks.callNativeArgs.mockClear();

    try {
      view.onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        reason: 'You are on an open pull request',
        expectedOutcome: 'Catch risky changes before merge',
        contextDisclosure:
            'Dao will attach the current PR page after you run it.',
        actionPrompt: 'Review:\n{page_content}',
        requiresPageContent: true,
        tabId: 42,
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
        confidence: 0.93,
      });
      await Promise.resolve();

      expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
      expect(templateText(view.renderProactiveCard_()))
          .not.toContain('chat.proactive.title.review_code');
      expect(view.proactiveIgnoredTimer_).toBeNull();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback', expect.anything());

      view.onHistoryClose_();
      await Promise.resolve();

      expect(templateText(view.renderProactiveCard_()))
          .toContain('chat.proactive.title.review_code');
      expect(view.proactiveIgnoredTimer_).not.toBeNull();
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'shown',
          }));
    } finally {
      view.clearProactiveIgnoredTimer_();
    }
  });

  it('defers shown feedback while the agent is streaming', async () => {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      isStreaming_: boolean;
      proactiveIgnoredTimer_: number | null;
      proactiveSuggestion_: {scenarioId: string; text: string} | null;
      onProactiveSuggestion_: (raw: unknown) => void;
      renderProactiveCard_: () => unknown;
      activateProactiveSuggestionIfVisible_: () => void;
      clearProactiveIgnoredTimer_: () => void;
    };
    view.isStreaming_ = true;
    pickerMocks.callNativeArgs.mockClear();

    try {
      view.onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        reason: 'You are on an open pull request',
        expectedOutcome: 'Catch risky changes before merge',
        contextDisclosure:
            'Dao will attach the current PR page after you run it.',
        actionPrompt: 'Review:\n{page_content}',
        requiresPageContent: true,
        tabId: 42,
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
        confidence: 0.93,
      });
      await Promise.resolve();

      expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
      expect(templateText(view.renderProactiveCard_()))
          .not.toContain('Review this PR');
      expect(view.proactiveIgnoredTimer_).toBeNull();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback', expect.anything());

      view.isStreaming_ = false;
      view.activateProactiveSuggestionIfVisible_();
      await Promise.resolve();

      expect(templateText(view.renderProactiveCard_()))
          .toContain('chat.proactive.title.review_code');
      expect(view.proactiveIgnoredTimer_).not.toBeNull();
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'shown',
          }));
    } finally {
      view.clearProactiveIgnoredTimer_();
    }
  });

  it('renders proactive reason outcome context and three actions', () => {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      onProactiveSuggestion_: (raw: unknown) => void;
      renderProactiveCard_: () => unknown;
    };

    view.onProactiveSuggestion_({
      text: 'Review this PR',
      scenarioId: 'seed_github_pr',
      reason: 'structured_content',
      expectedOutcome: 'review_code',
      contextDisclosure: 'captures_after_run',
      actionPrompt: 'Review:\n{page_content}',
      requiresPageContent: true,
      tabId: 42,
      url: 'https://github.com/acme/repo/pull/123',
      domain: 'github.com',
      confidence: 0.93,
    });

    const cardText = templateText(view.renderProactiveCard_());
    expect(cardText).toContain('chat.proactive.title.review_code');
    expect(cardText).toContain('chat.proactive.reason_label');
    expect(cardText)
        .toContain('chat.proactive.reason.structured_content');
    expect(cardText).toContain('chat.proactive.expected_outcome_label');
    expect(cardText)
        .toContain('chat.proactive.expected.review_code');
    expect(cardText).toContain('chat.proactive.context_label');
    expect(cardText)
        .toContain('chat.proactive.context.captures_after_run');
    expect(cardText).toContain('chat.proactive.cost_hint');
    expect(cardText).toContain('chat.proactive.run');
    expect(cardText).toContain('chat.proactive.not_now');
    expect(cardText).toContain('chat.proactive.never_here');
  });

  it('localizes seed scenario titles instead of trusting native names', () => {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      onProactiveSuggestion_: (raw: unknown) => void;
      renderProactiveCard_: () => unknown;
    };

    view.onProactiveSuggestion_({
      text: 'Review this PR',
      scenarioId: 'seed_github_pr',
      scenarioName: 'Review this PR',
      actionLabel: 'review_code',
      reason: 'structured_content',
      expectedOutcome: 'review_code',
      contextDisclosure: 'captures_after_run',
      confidence: 0.93,
    });

    const cardText = templateText(view.renderProactiveCard_());
    expect(cardText).toContain('chat.proactive.title.review_code');
    expect(cardText).not.toContain('Review this PR');
  });

  it('renders a localized repeat-action title when native text is omitted',
     () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         renderProactiveCard_: () => unknown;
       };

       view.onProactiveSuggestion_({
         episodeId: 7,
         type: 'repeat_action',
         confidence: 0.8,
       });

       expect(templateText(view.renderProactiveCard_()))
           .toContain('chat.proactive.repeat_action_title');
     });

  it('does not send the repeat-action display title as the model prompt',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
           return {success: true};
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         (view as unknown as {
           onProactiveSuggestion_: (raw: unknown) => void;
           runProactiveSuggestion_: () => Promise<void>;
         }).onProactiveSuggestion_({
           episodeId: 7,
           type: 'repeat_action',
           text: 'You usually interact with this page. Want me to help again?',
           confidence: 0.8,
         });

         await (view as unknown as {
           runProactiveSuggestion_: () => Promise<void>;
         }).runProactiveSuggestion_();

         expect(originalSend).toHaveBeenCalledTimes(1);
         expect(originalSend.mock.calls[0][0]).toBe(
             'chat.proactive.default_user_prompt');
         expect(originalSend.mock.calls[0][0]).not.toBe(
             'You usually interact with this page. Want me to help again?');
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'acceptSuggestion', 7);
       } finally {
         clearTabWatchTimer(view);
       }
     });

  it('uses the transparent visible prompt when a scenario omits actionPrompt',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
           return {success: true};
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         (view as unknown as {
           onProactiveSuggestion_: (raw: unknown) => void;
           runProactiveSuggestion_: () => Promise<void>;
           clearProactiveIgnoredTimer_: () => void;
         }).onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           scenarioName: 'Review this PR',
           reason: 'structured_content',
           expectedOutcome: 'review_code',
           contextDisclosure: 'captures_after_run',
           requiresPageContent: false,
           tabId: 42,
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });

         await (view as unknown as {
           runProactiveSuggestion_: () => Promise<void>;
         }).runProactiveSuggestion_();

         expect(originalSend).toHaveBeenCalledTimes(1);
         expect(originalSend.mock.calls[0][0]).toContain(
             'chat.proactive.title.review_code.');
         expect(originalSend.mock.calls[0][0]).toContain(
             'chat.proactive.visible_prompt_reason_header');
         expect(originalSend.mock.calls[0][0]).toContain(
             'chat.proactive.reason.structured_content');
         expect(originalSend.mock.calls[0][0]).toContain(
             'chat.proactive.visible_prompt_expected_header');
         expect(originalSend.mock.calls[0][0]).toContain(
             'chat.proactive.expected.review_code');
         expect(originalSend.mock.calls[0][1]).toEqual([]);
       } finally {
         (view as unknown as {
           clearProactiveIgnoredTimer_: () => void;
         }).clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('wraps a continue-conversation intent in a localized model prompt',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
           return {success: true};
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         (view as unknown as {
           onProactiveSuggestion_: (raw: unknown) => void;
           runProactiveSuggestion_: () => Promise<void>;
         }).onProactiveSuggestion_({
           episodeId: 8,
           type: 'continue_conversation',
           text: 'comparing ticket prices',
           confidence: 0.8,
         });

         await (view as unknown as {
           runProactiveSuggestion_: () => Promise<void>;
         }).runProactiveSuggestion_();

         expect(originalSend).toHaveBeenCalledTimes(1);
         expect(originalSend.mock.calls[0][0]).toBe(
             'chat.proactive.continue_conversation_prompt:comparing ticket prices');
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'acceptSuggestion', 8);
       } finally {
         clearTabWatchTimer(view);
       }
     });

  it('does not record action feedback for legacy proactive episodes',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
           return {success: true};
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         (view as unknown as {
           onProactiveSuggestion_: (raw: unknown) => void;
           runProactiveSuggestion_: () => Promise<void>;
         }).onProactiveSuggestion_({
           episodeId: 7,
           type: 'repeat_action',
           text: 'You usually interact with this page. Want me to help again?',
           confidence: 0.8,
         });
         pickerMocks.callNativeArgs.mockClear();

         await (view as unknown as {
           runProactiveSuggestion_: () => Promise<void>;
         }).runProactiveSuggestion_();

         expect(originalSend).toHaveBeenCalledTimes(1);
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'acceptSuggestion', 7);
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback', expect.anything());
       } finally {
         clearTabWatchTimer(view);
       }
     });

  it('keeps the proactive ignored timer when Run is blocked by streaming',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         isStreaming_: boolean;
         proactiveIgnoredTimer_: number|null;
         onProactiveSuggestion_: (raw: unknown) => void;
         runProactiveSuggestion_: () => Promise<void>;
         clearProactiveIgnoredTimer_: () => void;
       };

       try {
         typedView.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           scenarioName: 'Review this PR',
           reason: 'structured_content',
           expectedOutcome: 'review_code',
           contextDisclosure: 'captures_after_run',
           requiresPageContent: false,
           tabId: 42,
           confidence: 0.97,
         });
         const timerBefore = typedView.proactiveIgnoredTimer_;
         typedView.isStreaming_ = true;

         await typedView.runProactiveSuggestion_();

         expect(originalSend).not.toHaveBeenCalled();
         expect(typedView.proactiveIgnoredTimer_).toBe(timerBefore);
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('uses dark-mode-aware tokens for proactive suggestion styling',
     () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         render: () => unknown;
       };

       const styleText = templateText(view.render());
       expect(styleText).toContain('--dao-proactive-card-bg');
       expect(styleText).toContain('@media (prefers-color-scheme: dark)');
       expect(styleText).toContain(
           'background: var(--dao-proactive-card-bg');
     });

  it('ignores proactive suggestions when the local setting is disabled', () => {
    localStorage.setItem('dao_proactive_enabled', 'false');
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      proactiveSuggestion_: {scenarioId: string; text: string} | null;
      onProactiveSuggestion_: (raw: unknown) => void;
    };

    view.onProactiveSuggestion_({
      text: 'Review this PR',
      scenarioId: 'seed_github_pr',
      actionPrompt: 'Review:\n{page_content}',
      requiresPageContent: true,
      tabId: 42,
    });

    expect(view.proactiveSuggestion_).toBeNull();
    expect(pickerMocks.callNative).not.toHaveBeenCalled();
    expect(pickerMocks.callNativeArgs).not.toHaveBeenCalled();
  });

  it('clears the active proactive suggestion when the setting is disabled',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         clearProactiveIgnoredTimer_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveIgnoredTimer_: number|null;
         proactiveSuggestion_: unknown;
       };
       document.body.appendChild(view);
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();
         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(view.proactiveIgnoredTimer_).not.toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         window.dispatchEvent(new CustomEvent(
             'dao-proactive-enabled-changed',
             {detail: {enabled: false}}));
         await Promise.resolve();

         expect(view.proactiveSuggestion_).toBeNull();
         expect(view.proactiveIgnoredTimer_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback', expect.anything());
       } finally {
         view.clearProactiveIgnoredTimer_();
         view.remove();
       }
     });

  it('not now records timing feedback without sending a message', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    pickerMocks.callNativeArgs.mockResolvedValue(true);

    try {
      (view as unknown as {
        onProactiveSuggestion_: (raw: unknown) => void;
        notNowProactiveSuggestion_: () => Promise<void>;
        proactiveSuggestion_: unknown;
      }).onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        actionLabel: 'review_code',
        reason: 'You are on a pull request page',
        expectedOutcome: 'Catch issues before merge',
        contextDisclosure: 'Dao will attach the page only after you run this.',
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
        confidence: 0.91,
      });
      pickerMocks.callNativeArgs.mockClear();

      await (view as unknown as {
        notNowProactiveSuggestion_: () => Promise<void>;
      }).notNowProactiveSuggestion_();

      expect(originalSend).not.toHaveBeenCalled();
      expect((view as unknown as {
        proactiveSuggestion_: unknown;
      }).proactiveSuggestion_).toBeNull();
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            actionLabel: 'review_code',
            domain: 'github.com',
            url: 'https://github.com/acme/repo/pull/123',
            outcome: 'not_now',
          }));
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'dismissSuggestion', expect.anything());
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('snoozes the same proactive scenario after not-now', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    const typedView = view as unknown as {
      clearProactiveIgnoredTimer_: () => void;
      notNowProactiveSuggestion_: () => Promise<void>;
      onProactiveSuggestion_: (raw: unknown) => void;
      proactiveSuggestion_: {scenarioId: string} | null;
    };
    const rawSuggestion = {
      text: 'Review this PR',
      scenarioId: 'seed_github_pr',
      actionLabel: 'review_code',
      reason: 'structured_content',
      expectedOutcome: 'review_code',
      contextDisclosure: 'captures_after_run',
      url: 'https://github.com/acme/repo/pull/123?utm_source=email#files',
      domain: 'github.com',
      confidence: 0.91,
    };
    pickerMocks.callNativeArgs.mockResolvedValue(true);

    try {
      typedView.onProactiveSuggestion_(rawSuggestion);
      await Promise.resolve();
      expect(typedView.proactiveSuggestion_?.scenarioId)
          .toBe('seed_github_pr');

      await typedView.notNowProactiveSuggestion_();
      expect(typedView.proactiveSuggestion_).toBeNull();
      pickerMocks.callNativeArgs.mockClear();

      typedView.onProactiveSuggestion_({
        ...rawSuggestion,
        url: 'https://github.com/acme/repo/pull/123#discussion',
        confidence: 1,
      });
      await Promise.resolve();

      expect(typedView.proactiveSuggestion_).toBeNull();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'shown',
          }));
    } finally {
      typedView.clearProactiveIgnoredTimer_();
      clearTabWatchTimer(view);
    }
  });

  it('keeps a not-now proactive scenario suppressed across trailing slashes',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         notNowProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string} | null;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'structured_content',
         expectedOutcome: 'review_code',
         contextDisclosure: 'captures_after_run',
         url: 'https://github.com/acme/repo/pull/123',
         domain: 'github.com',
         confidence: 0.91,
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_?.scenarioId)
             .toBe('seed_github_pr');

         await typedView.notNowProactiveSuggestion_();
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://github.com/acme/repo/pull/123/',
           confidence: 1,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('keeps a not-now proactive scenario suppressed across www and apex urls',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         notNowProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {domain: string} | null;
       };
       const rawSuggestion = {
         text: 'Analyze project progress',
         scenarioId: 'seed_linear_project',
         actionLabel: 'analyze_progress',
         reason: 'matched_structure',
         expectedOutcome: 'analyze_progress',
         contextDisclosure: 'captures_after_run',
         url: 'https://www.linear.app/acme/project/dao',
         domain: 'www.linear.app',
         confidence: 0.91,
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_?.domain)
             .toBe('www.linear.app');

         await typedView.notNowProactiveSuggestion_();
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://linear.app/acme/project/dao',
           domain: 'linear.app',
           confidence: 1,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_linear_project',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('snoozes the same proactive episode after not-now', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    const typedView = view as unknown as {
      clearProactiveIgnoredTimer_: () => void;
      notNowProactiveSuggestion_: () => Promise<void>;
      onProactiveSuggestion_: (raw: unknown) => void;
      proactiveSuggestion_: {episodeId: number} | null;
    };
    const rawSuggestion = {
      episodeId: 19,
      type: 'continue_conversation',
      text: 'pricing comparison',
      url: 'https://example.com/pricing?utm_source=email#section',
      domain: 'example.com',
      confidence: 0.91,
    };
    pickerMocks.callNativeArgs.mockResolvedValue(true);

    try {
      typedView.onProactiveSuggestion_(rawSuggestion);
      await Promise.resolve();
      expect(typedView.proactiveSuggestion_?.episodeId).toBe(19);

      await typedView.notNowProactiveSuggestion_();
      expect(typedView.proactiveSuggestion_).toBeNull();
      pickerMocks.callNativeArgs.mockClear();

      typedView.onProactiveSuggestion_({
        ...rawSuggestion,
        url: 'https://example.com/pricing#details',
        confidence: 1,
      });
      await Promise.resolve();

      expect(typedView.proactiveSuggestion_).toBeNull();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'acceptSuggestion', 19);
    } finally {
      typedView.clearProactiveIgnoredTimer_();
      clearTabWatchTimer(view);
    }
  });

  it('ignores a proactive episode with non-finite confidence', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    const typedView = view as unknown as {
      clearProactiveIgnoredTimer_: () => void;
      onProactiveSuggestion_: (raw: unknown) => void;
      proactiveSuggestion_: {episodeId: number} | null;
    };
    pickerMocks.callNativeArgs.mockResolvedValue(true);

    try {
      typedView.onProactiveSuggestion_({
        episodeId: 19,
        type: 'continue_conversation',
        text: 'pricing comparison',
        url: 'https://example.com/pricing',
        domain: 'example.com',
        confidence: Number.NaN,
      });
      await Promise.resolve();

      expect(typedView.proactiveSuggestion_).toBeNull();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({outcome: 'shown'}));
    } finally {
      typedView.clearProactiveIgnoredTimer_();
      clearTabWatchTimer(view);
    }
  });

  it('ignores a legacy proactive suggestion without an episode or text',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {type: string} | null;
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_({
           type: 'continue_conversation',
           url: 'https://example.com/pricing',
           domain: 'example.com',
           confidence: 0.91,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({outcome: 'shown'}));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('ignores a continue-conversation episode without an intent',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {episodeId: number} | null;
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_({
           episodeId: 19,
           type: 'continue_conversation',
           url: 'https://example.com/pricing',
           domain: 'example.com',
           confidence: 0.91,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({outcome: 'shown'}));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('keeps proactive episode not-now snoozed after the chat view reloads',
       async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view: firstView} = await mountChatViewWithSend(originalSend);
       let secondView: HTMLElement | null = null;
       const first = firstView as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         notNowProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {episodeId: number} | null;
       };
       const rawSuggestion = {
         episodeId: 19,
         type: 'continue_conversation',
         text: 'pricing comparison',
         url: 'https://example.com/pricing?utm_source=email#section',
         domain: 'example.com',
         confidence: 0.91,
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         first.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(first.proactiveSuggestion_?.episodeId).toBe(19);

         await first.notNowProactiveSuggestion_();
         expect(first.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         const mounted = await mountChatViewWithSend(originalSend);
         secondView = mounted.view;
         const second = secondView as unknown as {
           clearProactiveIgnoredTimer_: () => void;
           onProactiveSuggestion_: (raw: unknown) => void;
           proactiveSuggestion_: {episodeId: number} | null;
         };
         second.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://example.com/pricing#details',
           confidence: 1,
         });
         await Promise.resolve();

         expect(second.proactiveSuggestion_).toBeNull();
       } finally {
         first.clearProactiveIgnoredTimer_();
         (secondView as unknown as {
           clearProactiveIgnoredTimer_?: () => void;
         } | null)?.clearProactiveIgnoredTimer_?.();
         clearTabWatchTimer(firstView);
         if (secondView) {
           clearTabWatchTimer(secondView);
         }
       }
     });

  it('keeps legacy www not-now storage suppressed after restore',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       localStorage.setItem(
           'dao_proactive_not_now_snoozes',
           JSON.stringify({
             [
               'scenario:seed_linear_project\n' +
               'https://www.linear.app/acme/project/dao'
             ]: Date.now() + 600000,
           }));
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string} | null;
       };

       try {
         typedView.onProactiveSuggestion_({
           text: 'Analyze project progress',
           scenarioId: 'seed_linear_project',
           actionLabel: 'analyze_progress',
           reason: 'matched_structure',
           expectedOutcome: 'analyze_progress',
           contextDisclosure: 'captures_after_run',
           confidence: 0.99,
           url: 'https://linear.app/acme/project/dao',
           domain: 'linear.app',
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_linear_project',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('snoozes not-now suggestions using current page when native omits url',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         notNowProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string} | null;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'structured_content',
         expectedOutcome: 'review_code',
         contextDisclosure: 'captures_after_run',
         domain: 'github.com',
         confidence: 0.91,
       };
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://github.com/acme/repo/pull/123?utm_source=email#files',
             title: 'Pull request',
           };
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_?.scenarioId)
             .toBe('seed_github_pr');

         await typedView.notNowProactiveSuggestion_();
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://github.com/acme/repo/pull/123#discussion',
           confidence: 1,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('snoozes repeated not-now suggestions when native keeps omitting url',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         notNowProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string} | null;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'structured_content',
         expectedOutcome: 'review_code',
         contextDisclosure: 'captures_after_run',
         domain: 'github.com',
         confidence: 0.91,
       };
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://github.com/acme/repo/pull/123?utm_source=email#files',
             title: 'Pull request',
           };
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_?.scenarioId)
             .toBe('seed_github_pr');

         await typedView.notNowProactiveSuggestion_();
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           confidence: 1,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('snoozes not-now suggestions by identity when page context is unavailable',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         notNowProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string} | null;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'structured_content',
         expectedOutcome: 'review_code',
         contextDisclosure: 'captures_after_run',
         confidence: 0.91,
       };
       pickerMocks.callNative.mockRejectedValue(new Error('page unavailable'));
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_?.scenarioId)
             .toBe('seed_github_pr');

         await typedView.notNowProactiveSuggestion_();
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           confidence: 1,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
           confidence: 1,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('does not snooze a different page on the same domain when not-now had a url',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         notNowProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string; url: string} | null;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'structured_content',
         expectedOutcome: 'review_code',
         contextDisclosure: 'captures_after_run',
         url: 'https://github.com/acme/repo/pull/123',
         domain: 'github.com',
         confidence: 0.91,
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_?.url)
             .toBe('https://github.com/acme/repo/pull/123');

         await typedView.notNowProactiveSuggestion_();
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://github.com/acme/repo/pull/456',
           confidence: 0.99,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_?.url)
             .toBe('https://github.com/acme/repo/pull/456');
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('does not suppress a different page after never-here had a url',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         dismissProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string; url: string} | null;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'structured_content',
         expectedOutcome: 'review_code',
         contextDisclosure: 'captures_after_run',
         url: 'https://github.com/acme/repo/pull/123',
         domain: 'github.com',
         confidence: 0.91,
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_?.url)
             .toBe('https://github.com/acme/repo/pull/123');

         await typedView.dismissProactiveSuggestion_();
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://github.com/acme/repo/pull/456',
           confidence: 0.99,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_?.url)
             .toBe('https://github.com/acme/repo/pull/456');
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('runs a proactive scenario only after click and truncates page text', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {url: 'https://github.com/acme/repo/pull/123', title: 'PR'};
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockImplementation(async (method: string) => {
      if (method === 'getPageContentForScenario') {
        return {text: 'x'.repeat(13000)};
      }
      return true;
    });

    try {
      (view as unknown as {
        onProactiveSuggestion_: (raw: unknown) => void;
        proactiveSuggestion_: {scenarioId: string} | null;
        runProactiveSuggestion_: () => Promise<void>;
      }).onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        scenarioName: 'Review this PR',
        actionLabel: 'review_code',
        reason: 'You are looking at an open pull request.',
        expectedOutcome: 'Catch risky changes before merge.',
        contextDisclosure: 'Dao will attach the current page after you run this.',
        actionPrompt: 'Review this page:\n{page_content}',
        requiresPageContent: true,
        tabId: 42,
        confidence: 1,
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
      });

      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'getPageContentForScenario', 42);

      await (view as unknown as {
        runProactiveSuggestion_: () => Promise<void>;
      }).runProactiveSuggestion_();

      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'getPageContentForScenario', 42);
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'acceptSuggestion',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            actionLabel: 'review_code',
            domain: 'github.com',
            url: 'https://github.com/acme/repo/pull/123',
          }));
      expect(originalSend).toHaveBeenCalledTimes(1);
      expect(originalSend.mock.calls[0][0])
          .toContain('chat.proactive.title.review_code.');
      expect(originalSend.mock.calls[0][0]).toContain(
          'chat.proactive.visible_prompt_reason_header');
      expect(originalSend.mock.calls[0][0]).toContain(
          'You are looking at an open pull request.');
      expect(originalSend.mock.calls[0][0]).toContain(
          'chat.proactive.visible_prompt_expected_header');
      expect(originalSend.mock.calls[0][0]).toContain(
          'Catch risky changes before merge.');
      const attachments = originalSend.mock.calls[0][1] || [];
      expect(attachments).toHaveLength(1);
      const extracted = attachments[0].extractedText as string;
      expect(extracted).toContain('Review this page:');
      expect(extracted).toContain(
          'Page content truncated to 12000 characters');
      expect((extracted.match(/x/g) || []).length).toBe(12000);
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'completed',
          }));
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'getMemoryContext',
          expect.anything(),
          expect.anything(),
          expect.anything());
      expect(iface.sendMessage).toBeDefined();
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('snoozes an accepted proactive scenario after run', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockResolvedValue(true);

    const typedView = view as unknown as {
      clearProactiveIgnoredTimer_: () => void;
      onProactiveSuggestion_: (raw: unknown) => void;
      proactiveSuggestion_: {scenarioId: string} | null;
      runProactiveSuggestion_: () => Promise<void>;
    };
    const rawSuggestion = {
      text: 'Review this PR',
      scenarioId: 'seed_github_pr',
      scenarioName: 'Review this PR',
      actionLabel: 'review_code',
      reason: 'structured_content',
      expectedOutcome: 'review_code',
      contextDisclosure: 'captures_after_run',
      actionPrompt: 'Review without page capture',
      requiresPageContent: false,
      tabId: 42,
      confidence: 0.97,
      url: 'https://github.com/acme/repo/pull/123',
      domain: 'github.com',
    };

    try {
      typedView.onProactiveSuggestion_(rawSuggestion);
      await Promise.resolve();
      expect(typedView.proactiveSuggestion_?.scenarioId)
          .toBe('seed_github_pr');

      await typedView.runProactiveSuggestion_();
      expect(originalSend).toHaveBeenCalledTimes(1);
      expect(typedView.proactiveSuggestion_).toBeNull();
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'acceptSuggestion',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            actionLabel: 'review_code',
          }));

      pickerMocks.callNativeArgs.mockClear();
      typedView.onProactiveSuggestion_({
        ...rawSuggestion,
        confidence: 1,
      });
      await Promise.resolve();

      expect(typedView.proactiveSuggestion_).toBeNull();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'shown',
          }));
    } finally {
      typedView.clearProactiveIgnoredTimer_();
      clearTabWatchTimer(view);
    }
  });

  it('dismisses a proactive scenario as strong suppression', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'getPageInfo') {
        return {
          url: 'https://linear.app/acme/project/dao',
          title: 'Dao project',
        };
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockResolvedValue(true);

    try {
      (view as unknown as {
        onProactiveSuggestion_: (raw: unknown) => void;
        dismissProactiveSuggestion_: () => Promise<void>;
      }).onProactiveSuggestion_({
        text: 'Analyze project progress',
        scenarioId: 'seed_linear_project',
        actionLabel: 'analyze_progress',
        reason: 'You are viewing a project overview.',
        expectedOutcome: 'Summarize project health and blockers.',
        contextDisclosure: 'Dao will use the current page only after you run it.',
        confidence: 0.9,
      });
      await (view as unknown as {
        dismissProactiveSuggestion_: () => Promise<void>;
      }).dismissProactiveSuggestion_();

      expect(originalSend).not.toHaveBeenCalled();
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'dismissSuggestion',
          expect.objectContaining({
            scenarioId: 'seed_linear_project',
            actionLabel: 'analyze_progress',
            domain: 'linear.app',
            url: 'https://linear.app/acme/project/dao',
            outcome: 'never_here',
          }));
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('does not show the same proactive scenario again after never-here',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         dismissProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string} | null;
       };
       const rawSuggestion = {
         text: 'Analyze project progress',
         scenarioId: 'seed_linear_project',
         actionLabel: 'analyze_progress',
         reason: 'matched_structure',
         expectedOutcome: 'analyze_progress',
         contextDisclosure: 'captures_after_run',
         confidence: 0.9,
         url: 'https://linear.app/acme/project/dao?utm_source=mail#updates',
         domain: 'linear.app',
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_?.scenarioId)
             .toBe('seed_linear_project');

         await typedView.dismissProactiveSuggestion_();
         expect(typedView.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://linear.app/acme/project/dao#overview',
           confidence: 0.99,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_linear_project',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('keeps never-here suppression across www and apex domains', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    const typedView = view as unknown as {
      clearProactiveIgnoredTimer_: () => void;
      dismissProactiveSuggestion_: () => Promise<void>;
      onProactiveSuggestion_: (raw: unknown) => void;
      proactiveSuggestion_: {domain: string} | null;
    };
    const rawSuggestion = {
      text: 'Analyze project progress',
      scenarioId: 'seed_linear_project',
      actionLabel: 'analyze_progress',
      reason: 'matched_structure',
      expectedOutcome: 'analyze_progress',
      contextDisclosure: 'captures_after_run',
      confidence: 0.9,
      url: 'https://www.linear.app/acme/project/dao',
      domain: 'www.linear.app',
    };
    pickerMocks.callNativeArgs.mockResolvedValue(true);

    try {
      typedView.onProactiveSuggestion_(rawSuggestion);
      await Promise.resolve();
      expect(typedView.proactiveSuggestion_?.domain)
          .toBe('www.linear.app');

      await typedView.dismissProactiveSuggestion_();
      expect(typedView.proactiveSuggestion_).toBeNull();
      pickerMocks.callNativeArgs.mockClear();

      typedView.onProactiveSuggestion_({
        ...rawSuggestion,
        url: 'https://linear.app/acme/project/dao',
        domain: 'linear.app',
        confidence: 0.99,
      });
      await Promise.resolve();

      expect(typedView.proactiveSuggestion_).toBeNull();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_linear_project',
            outcome: 'shown',
          }));
    } finally {
      typedView.clearProactiveIgnoredTimer_();
      clearTabWatchTimer(view);
    }
  });

  it('keeps legacy www never-here storage suppressed after restore',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       localStorage.setItem(
           'dao_proactive_never_here_keys',
           JSON.stringify([
             'scenario:seed_linear_project\ndomain:www.linear.app',
           ]));
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {scenarioId: string} | null;
       };

       try {
         typedView.onProactiveSuggestion_({
           text: 'Analyze project progress',
           scenarioId: 'seed_linear_project',
           actionLabel: 'analyze_progress',
           reason: 'matched_structure',
           expectedOutcome: 'analyze_progress',
           contextDisclosure: 'captures_after_run',
           confidence: 0.99,
           url: 'https://linear.app/acme/project/dao',
           domain: 'linear.app',
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_linear_project',
               outcome: 'shown',
             }));
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('does not show the same proactive episode again after never-here',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view} = await mountChatViewWithSend(originalSend);
       const typedView = view as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         dismissProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {episodeId: number} | null;
       };
       const rawSuggestion = {
         episodeId: 19,
         type: 'continue_conversation',
         text: 'pricing comparison',
         confidence: 0.91,
         url: 'https://example.com/pricing?utm_source=email#section',
         domain: 'example.com',
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         typedView.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(typedView.proactiveSuggestion_?.episodeId).toBe(19);

         await typedView.dismissProactiveSuggestion_();
         expect(typedView.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'dismissSuggestion', 19);
         pickerMocks.callNativeArgs.mockClear();

         typedView.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://example.com/pricing#details',
           confidence: 1,
         });
         await Promise.resolve();

         expect(typedView.proactiveSuggestion_).toBeNull();
       } finally {
         typedView.clearProactiveIgnoredTimer_();
         clearTabWatchTimer(view);
       }
     });

  it('keeps proactive episode never-here suppressed after the chat view reloads',
     async () => {
       const originalSend = vi.fn(async () => 'sent');
       const {view: firstView} = await mountChatViewWithSend(originalSend);
       let secondView: HTMLElement | null = null;
       const first = firstView as unknown as {
         clearProactiveIgnoredTimer_: () => void;
         dismissProactiveSuggestion_: () => Promise<void>;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveSuggestion_: {episodeId: number} | null;
       };
       const rawSuggestion = {
         episodeId: 19,
         type: 'continue_conversation',
         text: 'pricing comparison',
         confidence: 0.91,
         url: 'https://example.com/pricing?utm_source=email#section',
         domain: 'example.com',
       };
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         first.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(first.proactiveSuggestion_?.episodeId).toBe(19);

         await first.dismissProactiveSuggestion_();
         expect(first.proactiveSuggestion_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         const mounted = await mountChatViewWithSend(originalSend);
         secondView = mounted.view;
         const second = secondView as unknown as {
           clearProactiveIgnoredTimer_: () => void;
           onProactiveSuggestion_: (raw: unknown) => void;
           proactiveSuggestion_: {episodeId: number} | null;
         };
         second.onProactiveSuggestion_({
           ...rawSuggestion,
           url: 'https://example.com/pricing#details',
           confidence: 1,
         });
         await Promise.resolve();

         expect(second.proactiveSuggestion_).toBeNull();
       } finally {
         first.clearProactiveIgnoredTimer_();
         (secondView as unknown as {
           clearProactiveIgnoredTimer_?: () => void;
         } | null)?.clearProactiveIgnoredTimer_?.();
         clearTabWatchTimer(firstView);
         if (secondView) {
           clearTabWatchTimer(secondView);
         }
       }
     });

  it('records failed when page capture fails', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    let captureFails = true;
    pickerMocks.callNativeArgs.mockImplementation(async (method: string) => {
      if (method === 'getPageContentForScenario') {
        if (!captureFails) {
          return {text: 'retry page content'};
        }
        return {error: 'capture failed'};
      }
      return true;
    });

    try {
      (view as unknown as {
        onProactiveSuggestion_: (raw: unknown) => void;
        runProactiveSuggestion_: () => Promise<void>;
      }).onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        scenarioName: 'Review this PR',
        actionLabel: 'review_code',
        reason: 'You are on a pull request page.',
        expectedOutcome: 'Catch risky changes before merge.',
        contextDisclosure: 'Dao will attach the page after you run this.',
        actionPrompt: 'Review:\n{page_content}',
        requiresPageContent: true,
        tabId: 42,
        confidence: 0.97,
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
      });
      pickerMocks.callNativeArgs.mockClear();

      await (view as unknown as {
        runProactiveSuggestion_: () => Promise<void>;
      }).runProactiveSuggestion_();

      expect((view as unknown as {
        proactiveSuggestion_: unknown;
      }).proactiveSuggestion_).not.toBeNull();
      expect(originalSend).not.toHaveBeenCalled();
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'getPageContentForScenario', 42);
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'failed',
          }));
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'acceptSuggestion', expect.anything());
      pickerMocks.callNativeArgs.mockClear();
      captureFails = false;

      await (view as unknown as {
        runProactiveSuggestion_: () => Promise<void>;
      }).runProactiveSuggestion_();

      expect(originalSend).toHaveBeenCalledTimes(1);
      expect(originalSend.mock.calls[0][1]).toHaveLength(1);
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'acceptSuggestion',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            actionLabel: 'review_code',
          }));
      expect((view as unknown as {
        proactiveSuggestion_: unknown;
      }).proactiveSuggestion_).toBeNull();
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('does not accept a proactive scenario when sending fails', async () => {
    const originalSend =
        vi.fn()
            .mockRejectedValueOnce(new Error('send failed'))
            .mockResolvedValue('sent');
    const {view} = await mountChatViewWithSend(originalSend);
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockResolvedValue(true);

    try {
      const typedView = view as unknown as {
        onProactiveSuggestion_: (raw: unknown) => void;
        proactiveSuggestion_: unknown;
        runProactiveSuggestion_: () => Promise<void>;
        suppressChipAttachOnce_: boolean;
      };
      typedView.suppressChipAttachOnce_ = false;
      typedView.onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        scenarioName: 'Review this PR',
        actionLabel: 'review_code',
        reason: 'You are on a pull request page.',
        expectedOutcome: 'Catch risky changes before merge.',
        contextDisclosure: 'Dao will attach the page after you run this.',
        actionPrompt: 'Review without page capture',
        requiresPageContent: false,
        tabId: 42,
        confidence: 0.97,
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
      });
      pickerMocks.callNativeArgs.mockClear();

      await typedView.runProactiveSuggestion_();

      expect(originalSend).toHaveBeenCalledTimes(1);
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'failed',
          }));
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'acceptSuggestion', expect.anything());
      expect(typedView.suppressChipAttachOnce_).toBe(false);
      expect(typedView.proactiveSuggestion_).not.toBeNull();

      pickerMocks.callNativeArgs.mockClear();
      await typedView.runProactiveSuggestion_();

      expect(originalSend).toHaveBeenCalledTimes(2);
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'acceptSuggestion',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            actionLabel: 'review_code',
          }));
      expect(typedView.proactiveSuggestion_).toBeNull();
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('records ignored when the card expires', async () => {
    vi.useFakeTimers();
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      onProactiveSuggestion_: (raw: unknown) => void;
      proactiveSuggestion_: unknown;
    };
    try {
      pickerMocks.callNativeArgs.mockResolvedValue(true);
      view.onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        actionLabel: 'review_code',
        reason: 'You are on a pull request page.',
        expectedOutcome: 'Catch risky changes before merge.',
        contextDisclosure: 'Dao will attach the page after you run this.',
        confidence: 0.97,
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
      });
      pickerMocks.callNativeArgs.mockClear();

      await vi.advanceTimersByTimeAsync(120000);

      expect(view.proactiveSuggestion_).toBeNull();
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'ignored',
          }));
    } finally {
      vi.useRealTimers();
    }
  });

  it('snoozes an expired proactive card before it can reappear', async () => {
    vi.useFakeTimers();
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      onProactiveSuggestion_: (raw: unknown) => void;
      proactiveSuggestion_: unknown;
    };
    const rawSuggestion = {
      text: 'Review this PR',
      scenarioId: 'seed_github_pr',
      actionLabel: 'review_code',
      reason: 'You are on a pull request page.',
      expectedOutcome: 'Catch risky changes before merge.',
      contextDisclosure: 'Dao will attach the page after you run this.',
      confidence: 0.97,
      url: 'https://github.com/acme/repo/pull/123',
      domain: 'github.com',
    };

    try {
      pickerMocks.callNativeArgs.mockResolvedValue(true);
      view.onProactiveSuggestion_(rawSuggestion);
      await Promise.resolve();
      expect(view.proactiveSuggestion_).not.toBeNull();
      pickerMocks.callNativeArgs.mockClear();

      await vi.advanceTimersByTimeAsync(120000);
      expect(view.proactiveSuggestion_).toBeNull();

      view.onProactiveSuggestion_({
        ...rawSuggestion,
        confidence: 1,
      });
      await Promise.resolve();

      expect(view.proactiveSuggestion_).toBeNull();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'shown',
          }));
    } finally {
      vi.useRealTimers();
    }
  });

  it('does not record proactive shown or ignored while the panel is hidden',
     async () => {
       vi.useFakeTimers();
       const visibilityDesc =
           Object.getOwnPropertyDescriptor(document, 'visibilityState');
       Object.defineProperty(document, 'visibilityState', {
         configurable: true,
         value: 'hidden',
       });
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveIgnoredTimer_: number|null;
         proactiveSuggestion_: unknown;
       };
       try {
         pickerMocks.callNativeArgs.mockResolvedValue(true);
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();

         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(view.proactiveIgnoredTimer_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({outcome: 'shown'}));

         await vi.advanceTimersByTimeAsync(120000);

         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({outcome: 'ignored'}));
       } finally {
         restorePropertyDescriptor(document, 'visibilityState', visibilityDesc);
         vi.useRealTimers();
       }
     });

  it('does not record ignored after a visible card is hidden by user typing',
     async () => {
       vi.useFakeTimers();
       const textarea = document.createElement('textarea');
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         activateProactiveSuggestionIfVisible_: () => void;
         composerTextarea_: HTMLTextAreaElement|null;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveIgnoredTimer_: number|null;
         proactiveSuggestion_: unknown;
         renderProactiveCard_: () => unknown;
       };
       view.composerTextarea_ = textarea;

       try {
         pickerMocks.callNativeArgs.mockResolvedValue(true);
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();
         expect(view.proactiveIgnoredTimer_).not.toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         textarea.value = 'I am already asking something else';
         view.activateProactiveSuggestionIfVisible_();
         await Promise.resolve();

         expect(templateText(view.renderProactiveCard_()))
             .not.toContain('Review this PR');
         expect(view.proactiveIgnoredTimer_).toBeNull();

         await vi.advanceTimersByTimeAsync(120000);

         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
       } finally {
         vi.useRealTimers();
       }
     });

  it('does not record ignored after a visible card is hidden by page typing',
     async () => {
       vi.useFakeTimers();
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         activateProactiveSuggestionIfVisible_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         panel_: HTMLElement|null;
         proactiveIgnoredTimer_: number|null;
         proactiveSuggestion_: unknown;
         refreshChips_: () => Promise<void>;
         renderProactiveCard_: () => unknown;
       };
       view.panel_ = document.createElement('div');
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://github.com/acme/repo/pull/123',
             title: 'Pull Request',
           };
         }
         if (method === 'executeScript') {
           return {
             result: JSON.stringify({
               url: 'https://github.com/acme/repo/pull/123',
               title: 'Pull Request',
               text: '',
               hasFocusedInput: true,
             }),
           };
         }
         return {success: true};
       });

       try {
         pickerMocks.callNativeArgs.mockResolvedValue(true);
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();
         expect(view.proactiveIgnoredTimer_).not.toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         await view.refreshChips_();
         await Promise.resolve();

         expect(templateText(view.renderProactiveCard_()))
             .not.toContain('Review this PR');
         expect(view.proactiveIgnoredTimer_).toBeNull();

         await vi.advanceTimersByTimeAsync(120000);

         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
       } finally {
         vi.useRealTimers();
       }
     });

  it('does not record ignored after a visible card is hidden by panel visibility',
     async () => {
       vi.useFakeTimers();
       const visibilityDesc =
           Object.getOwnPropertyDescriptor(document, 'visibilityState');
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         activateProactiveSuggestionIfVisible_: () => void;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveIgnoredTimer_: number|null;
         proactiveSuggestion_: unknown;
       };

       try {
         pickerMocks.callNativeArgs.mockResolvedValue(true);
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();
         expect(view.proactiveIgnoredTimer_).not.toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         Object.defineProperty(document, 'visibilityState', {
           configurable: true,
           value: 'hidden',
         });
         view.activateProactiveSuggestionIfVisible_();
         await Promise.resolve();

         expect(view.proactiveIgnoredTimer_).toBeNull();

         await vi.advanceTimersByTimeAsync(120000);

         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
       } finally {
         restorePropertyDescriptor(document, 'visibilityState', visibilityDesc);
         vi.useRealTimers();
       }
     });

  it('drops an old visible card that was paused while the user typed',
     async () => {
       vi.useFakeTimers();
       const textarea = document.createElement('textarea');
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         activateProactiveSuggestionIfVisible_: () => void;
         composerTextarea_: HTMLTextAreaElement|null;
         onProactiveSuggestion_: (raw: unknown) => void;
         proactiveIgnoredTimer_: number|null;
         proactiveSuggestion_: unknown;
       };
       view.composerTextarea_ = textarea;

       try {
         pickerMocks.callNativeArgs.mockResolvedValue(true);
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();
         expect(view.proactiveIgnoredTimer_).not.toBeNull();

         textarea.value = 'I am already asking something else';
         view.activateProactiveSuggestionIfVisible_();
         await Promise.resolve();
         expect(view.proactiveIgnoredTimer_).toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         await vi.advanceTimersByTimeAsync(300001);
         textarea.value = '';
         view.activateProactiveSuggestionIfVisible_();
         await Promise.resolve();

         expect(view.proactiveSuggestion_).toBeNull();
         expect(view.proactiveIgnoredTimer_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         vi.useRealTimers();
       }
     });

  it('drops a deferred proactive suggestion after it gets stale',
     async () => {
       vi.useFakeTimers();
       const visibilityDesc =
           Object.getOwnPropertyDescriptor(document, 'visibilityState');
       Object.defineProperty(document, 'visibilityState', {
         configurable: true,
         value: 'hidden',
       });
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         activateProactiveSuggestionIfVisible_: () => void;
         proactiveSuggestion_: unknown;
       };
       try {
         pickerMocks.callNativeArgs.mockResolvedValue(true);
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         await Promise.resolve();
         expect(view.proactiveSuggestion_).not.toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         await vi.advanceTimersByTimeAsync(300001);
         Object.defineProperty(document, 'visibilityState', {
           configurable: true,
           value: 'visible',
         });
         view.activateProactiveSuggestionIfVisible_();
         await Promise.resolve();

         expect(view.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback', expect.anything());
       } finally {
         restorePropertyDescriptor(document, 'visibilityState', visibilityDesc);
         vi.useRealTimers();
       }
     });

  it('starts proactive shown feedback when a hidden card becomes visible',
     async () => {
       const visibilityDesc =
           Object.getOwnPropertyDescriptor(document, 'visibilityState');
       Object.defineProperty(document, 'visibilityState', {
         configurable: true,
         value: 'hidden',
       });
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         activateProactiveSuggestionIfVisible_: () => void;
         proactiveIgnoredTimer_: number|null;
       };
       try {
         pickerMocks.callNativeArgs.mockResolvedValue(true);
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         pickerMocks.callNativeArgs.mockClear();

         Object.defineProperty(document, 'visibilityState', {
           configurable: true,
           value: 'visible',
         });
         view.activateProactiveSuggestionIfVisible_();
         await Promise.resolve();

         expect(view.proactiveIgnoredTimer_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         (view as unknown as {
           clearProactiveIgnoredTimer_: () => void;
         }).clearProactiveIgnoredTimer_();
         restorePropertyDescriptor(document, 'visibilityState', visibilityDesc);
       }
     });

  it('clears stale proactive scenarios when the active page changes',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         refreshChips_: () => Promise<void>;
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: unknown;
       };
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://github.com/acme/repo/issues/9',
             title: 'Issue',
           };
         }
         if (method === 'executeScript') {
           return {
             result: JSON.stringify({
               url: 'https://github.com/acme/repo/issues/9',
               title: 'Issue',
               text: '',
               hasFocusedInput: false,
             }),
           };
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         pickerMocks.callNativeArgs.mockClear();

         await view.refreshChips_();

         expect(view.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
               url: 'https://github.com/acme/repo/pull/123',
             }));
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('does not snooze a stale proactive scenario after the user leaves its page',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         refreshChips_: () => Promise<void>;
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: unknown;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'You are on a pull request page.',
         expectedOutcome: 'Catch risky changes before merge.',
         contextDisclosure: 'Dao will attach the page after you run this.',
         confidence: 0.97,
         url: 'https://github.com/acme/repo/pull/123',
         domain: 'github.com',
       };
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://github.com/acme/repo/issues/9',
             title: 'Issue',
           };
         }
         if (method === 'executeScript') {
           return {
             result: JSON.stringify({
               url: 'https://github.com/acme/repo/issues/9',
               title: 'Issue',
               text: '',
               hasFocusedInput: false,
             }),
           };
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         view.onProactiveSuggestion_(rawSuggestion);
         await Promise.resolve();
         expect(view.proactiveSuggestion_).not.toBeNull();
         pickerMocks.callNativeArgs.mockClear();

         await view.refreshChips_();
         expect(view.proactiveSuggestion_).toBeNull();

         view.onProactiveSuggestion_({
           ...rawSuggestion,
           confidence: 1,
         });
         await Promise.resolve();

         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('clears stale hidden proactive scenarios without recording ignored',
     async () => {
       const visibilityDesc =
           Object.getOwnPropertyDescriptor(document, 'visibilityState');
       Object.defineProperty(document, 'visibilityState', {
         configurable: true,
         value: 'hidden',
       });
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         refreshChips_: () => Promise<void>;
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: unknown;
       };
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://github.com/acme/repo/issues/9',
             title: 'Issue',
           };
         }
         if (method === 'executeScript') {
           return {
             result: JSON.stringify({
               url: 'https://github.com/acme/repo/issues/9',
               title: 'Issue',
               text: '',
               hasFocusedInput: false,
             }),
           };
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123',
           domain: 'github.com',
         });
         pickerMocks.callNativeArgs.mockClear();

         await view.refreshChips_();

         expect(view.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({outcome: 'ignored'}));
       } finally {
         view.clearProactiveIgnoredTimer_();
         restorePropertyDescriptor(document, 'visibilityState', visibilityDesc);
       }
     });

  it('clears stale proactive episodes when the active page changes',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         refreshChips_: () => Promise<void>;
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: unknown;
       };
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://example.com/new-page',
             title: 'New page',
           };
         }
         if (method === 'executeScript') {
           return {
             result: JSON.stringify({
               url: 'https://example.com/new-page',
               title: 'New page',
               text: '',
               hasFocusedInput: false,
             }),
           };
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         view.onProactiveSuggestion_({
           episodeId: 19,
           type: 'continue_conversation',
           text: 'pricing comparison',
           confidence: 0.91,
           url: 'https://example.com/old-page',
           domain: 'example.com',
         });
         pickerMocks.callNativeArgs.mockClear();

         await view.refreshChips_();

         expect(view.proactiveSuggestion_).toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback', expect.anything());
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('keeps proactive scenarios across fragment and tracking param changes',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         refreshChips_: () => Promise<void>;
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: unknown;
       };
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://github.com/acme/repo/pull/123?utm_medium=slack#files',
             title: 'Pull request',
           };
         }
         if (method === 'executeScript') {
           return {
             result: JSON.stringify({
               url: 'https://github.com/acme/repo/pull/123?utm_medium=slack#files',
               title: 'Pull request',
               text: '',
               hasFocusedInput: false,
             }),
           };
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         view.onProactiveSuggestion_({
           text: 'Review this PR',
           scenarioId: 'seed_github_pr',
           actionLabel: 'review_code',
           reason: 'You are on a pull request page.',
           expectedOutcome: 'Catch risky changes before merge.',
           contextDisclosure: 'Dao will attach the page after you run this.',
           confidence: 0.97,
           url: 'https://github.com/acme/repo/pull/123?utm_source=email#discussion',
           domain: 'github.com',
         });
         pickerMocks.callNativeArgs.mockClear();

         await view.refreshChips_();

         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
             }));
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('does not run a stale proactive scenario after navigation', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {
          url: 'https://github.com/acme/repo/issues/9',
          title: 'Issue',
        };
      }
      return {success: true};
    });
    pickerMocks.callNativeArgs.mockImplementation(async (method: string) => {
      if (method === 'getPageContentForScenario') {
        return {text: 'issue content from the new page'};
      }
      return true;
    });

    try {
      (view as unknown as {
        onProactiveSuggestion_: (raw: unknown) => void;
        runProactiveSuggestion_: () => Promise<void>;
      }).onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        actionLabel: 'review_code',
        reason: 'You are on a pull request page.',
        expectedOutcome: 'Catch risky changes before merge.',
        contextDisclosure: 'Dao will attach the page after you run this.',
        actionPrompt: 'Review:\n{page_content}',
        requiresPageContent: true,
        tabId: 42,
        confidence: 0.97,
        url: 'https://github.com/acme/repo/pull/123',
        domain: 'github.com',
      });
      pickerMocks.callNativeArgs.mockClear();

      await (view as unknown as {
        runProactiveSuggestion_: () => Promise<void>;
      }).runProactiveSuggestion_();

      expect(originalSend).not.toHaveBeenCalled();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'getPageContentForScenario', 42);
      expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
          'recordActionFeedback',
          expect.objectContaining({
            scenarioId: 'seed_github_pr',
            outcome: 'ignored',
            url: 'https://github.com/acme/repo/pull/123',
          }));
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('does not record not-now feedback for a stale proactive scenario',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         notNowProactiveSuggestion_: () => Promise<void>;
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: {scenarioId: string} | null;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'You are on a pull request page.',
         expectedOutcome: 'Catch risky changes before merge.',
         contextDisclosure: 'Dao will attach the page after you run this.',
         confidence: 0.97,
         url: 'https://github.com/acme/repo/pull/123',
         domain: 'github.com',
       };
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://github.com/acme/repo/issues/9',
             title: 'Issue',
           };
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         view.onProactiveSuggestion_(rawSuggestion);
         pickerMocks.callNativeArgs.mockClear();

         await view.notNowProactiveSuggestion_();
         expect(view.proactiveSuggestion_).toBeNull();

         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'not_now',
             }));
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
               url: 'https://github.com/acme/repo/pull/123',
             }));
         pickerMocks.callNativeArgs.mockClear();

         view.onProactiveSuggestion_({
           ...rawSuggestion,
           confidence: 1,
         });
         await Promise.resolve();

         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('does not record never-here feedback for a stale proactive scenario',
     async () => {
       const view = document.createElement('dao-chat-view') as HTMLElement & {
         onProactiveSuggestion_: (raw: unknown) => void;
         dismissProactiveSuggestion_: () => Promise<void>;
         clearProactiveIgnoredTimer_: () => void;
         proactiveSuggestion_: {scenarioId: string} | null;
       };
       const rawSuggestion = {
         text: 'Review this PR',
         scenarioId: 'seed_github_pr',
         actionLabel: 'review_code',
         reason: 'You are on a pull request page.',
         expectedOutcome: 'Catch risky changes before merge.',
         contextDisclosure: 'Dao will attach the page after you run this.',
         confidence: 0.97,
         url: 'https://github.com/acme/repo/pull/123',
         domain: 'github.com',
       };
       pickerMocks.callNative.mockImplementation(async (method: string) => {
         if (method === 'getPageInfo') {
           return {
             url: 'https://github.com/acme/repo/issues/9',
             title: 'Issue',
           };
         }
         return {success: true};
       });
       pickerMocks.callNativeArgs.mockResolvedValue(true);

       try {
         view.onProactiveSuggestion_(rawSuggestion);
         pickerMocks.callNativeArgs.mockClear();

         await view.dismissProactiveSuggestion_();
         expect(view.proactiveSuggestion_).toBeNull();

         expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
             'dismissSuggestion',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'never_here',
             }));
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'ignored',
               url: 'https://github.com/acme/repo/pull/123',
             }));
         pickerMocks.callNativeArgs.mockClear();

         view.onProactiveSuggestion_({
           ...rawSuggestion,
           confidence: 1,
         });
         await Promise.resolve();

         expect(view.proactiveSuggestion_).not.toBeNull();
         expect(pickerMocks.callNativeArgs).toHaveBeenCalledWith(
             'recordActionFeedback',
             expect.objectContaining({
               scenarioId: 'seed_github_pr',
               outcome: 'shown',
             }));
       } finally {
         view.clearProactiveIgnoredTimer_();
       }
     });

  it('does not run a stale proactive episode after navigation', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {
          url: 'https://example.com/new-page',
          title: 'New page',
        };
      }
      return {success: true};
    });

    try {
      (view as unknown as {
        onProactiveSuggestion_: (raw: unknown) => void;
        runProactiveSuggestion_: () => Promise<void>;
      }).onProactiveSuggestion_({
        episodeId: 19,
        type: 'continue_conversation',
        text: 'pricing comparison',
        confidence: 0.91,
        url: 'https://example.com/old-page',
        domain: 'example.com',
      });

      await (view as unknown as {
        runProactiveSuggestion_: () => Promise<void>;
      }).runProactiveSuggestion_();

      expect(originalSend).not.toHaveBeenCalled();
      expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
          'acceptSuggestion', 19);
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('opens the standalone dream page instead of expanding the report', async () => {
    pickerMocks.callNative.mockResolvedValue({success: true});
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      dreamReport_: {
        id: number;
        dreamDate: string;
        reportMarkdown: string;
        habits: unknown[];
        debugMaterialJson: string;
      };
      dreamExpanded_: boolean;
      toggleDreamExpanded_: () => void;
    };
    view.dreamReport_ = {
      id: 7,
      dreamDate: '2026-06-11',
      reportMarkdown: 'Report body should stay outside the agent panel',
      habits: [],
      debugMaterialJson: '',
    };

    view.toggleDreamExpanded_();
    await Promise.resolve();

    expect(pickerMocks.callNative).toHaveBeenCalledWith(
        'openTab', {url: 'dao://dream/'});
    expect(view.dreamExpanded_).toBe(false);
    expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
        'markDreamReportViewed', 7);
  });

  it('dismisses the dream card locally without marking the report viewed', async () => {
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      dreamReport_: {
        id: number;
        dreamDate: string;
        reportMarkdown: string;
        habits: unknown[];
        debugMaterialJson: string;
      } | null;
      dismissDreamReport_: () => void;
    };
    view.dreamReport_ = {
      id: 7,
      dreamDate: '2026-06-11',
      reportMarkdown: 'Report body should stay in history',
      habits: [],
      debugMaterialJson: '',
    };

    view.dismissDreamReport_();
    await Promise.resolve();

    expect(view.dreamReport_).toBeNull();
    expect(JSON.parse(
        localStorage.getItem('dao_dismissed_dream_report_ids') || '[]'))
        .toContain(7);
    expect(pickerMocks.callNativeArgs).not.toHaveBeenCalledWith(
        'markDreamReportViewed', 7);
  });

  it('does not show a locally dismissed unviewed dream report again', async () => {
    pickerMocks.callNative.mockResolvedValue({
      id: 7,
      dreamDate: '2026-06-11',
      reportMarkdown: 'Report body should stay in history',
      habitCandidates: '[]',
      debugMaterialJson: '',
    });
    localStorage.setItem('dao_dismissed_dream_report_ids', '[7]');
    const view = document.createElement('dao-chat-view') as HTMLElement & {
      dreamReport_: unknown;
      loadDreamReport_: () => Promise<void>;
    };

    await view.loadDreamReport_();

    expect(view.dreamReport_).toBeNull();
  });

  it('does not show unviewed dream reports older than yesterday', async () => {
    vi.useFakeTimers();
    try {
      vi.setSystemTime(new Date('2026-06-12T12:00:00'));
      pickerMocks.callNative.mockResolvedValue({
        id: 7,
        dreamDate: '2026-06-10',
        reportMarkdown: 'Older report should stay in history',
        habitCandidates: '[]',
        debugMaterialJson: '',
      });
      const view = document.createElement('dao-chat-view') as HTMLElement & {
        dreamReport_: unknown;
        loadDreamReport_: () => Promise<void>;
      };

      await view.loadDreamReport_();

      expect(view.dreamReport_).toBeNull();
    } finally {
      vi.useRealTimers();
    }
  });
});
