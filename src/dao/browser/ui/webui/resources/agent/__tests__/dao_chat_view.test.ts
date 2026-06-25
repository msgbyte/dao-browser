// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {readFileSync} from 'node:fs';

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {ElementContextCapture} from '../dao_element_context.js';

const pickerMocks = vi.hoisted(() => ({
  startElementPicker: vi.fn(),
  cancelElementPicker: vi.fn(),
  callNative: vi.fn(),
  callNativeArgs: vi.fn(),
  webUiListeners: {} as
      Record<string, Array<(...args: unknown[]) => void>>,
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
  return await import('../../sidebar/__tests__/lit_test_shim.js');
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
    getAllMetadata: vi.fn(async () => []),
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
    };
    view.agent_ = {
      state: {messages, isStreaming: false},
      abort: vi.fn(),
      continue: vi.fn(async () => undefined),
    };
    view.panel_ = null;
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
    const menu = openRow?.querySelector('.dao-user-action-menu');
    expect(menu).toBeTruthy();
    expect(menu?.querySelector('.dao-edit-menu-item')?.textContent).toContain(
        'chat.message_actions.edit');
    expect(menu?.querySelector('.dao-history-menu-item')).toBeNull();
  });

  it('bottom-aligns user message actions with the visible bubble', () => {
    const cssText = readFileSync(
        'src/dao/browser/ui/webui/resources/agent/agent.css', 'utf8');
    expect(cssText).toMatch(
        /\.dao-user-message-line\s*>\s*\.dao-user-actions\s*{[^}]*align-self:\s*flex-end;/s);
    expect(cssText).not.toMatch(
        /\.dao-user-message-line\s*>\s*\.dao-user-actions\s*{[^}]*align-self:\s*center;/s);
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

  it('stores a proactive suggestion without running native page capture', () => {
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

    expect(view.proactiveSuggestion_?.scenarioId).toBe('seed_github_pr');
    expect(view.proactiveSuggestion_?.text).toBe('Review this PR');
    expect(pickerMocks.callNative).not.toHaveBeenCalled();
    expect(pickerMocks.callNativeArgs).not.toHaveBeenCalled();
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
        runProactiveSuggestion_: () => Promise<void>;
      }).onProactiveSuggestion_({
        text: 'Review this PR',
        scenarioId: 'seed_github_pr',
        scenarioName: 'Review this PR',
        actionLabel: 'review_code',
        actionPrompt: 'Review this page:\n{page_content}',
        requiresPageContent: true,
        tabId: 42,
        confidence: 1,
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
      expect(originalSend.mock.calls[0][0]).toBe(
          'chat.proactive.user_prompt:Review this PR');
      const attachments = originalSend.mock.calls[0][1] || [];
      expect(attachments).toHaveLength(1);
      const extracted = attachments[0].extractedText as string;
      expect(extracted).toContain('Review this page:');
      expect(extracted).toContain(
          'Page content truncated to 12000 characters');
      expect((extracted.match(/x/g) || []).length).toBe(12000);
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

  it('dismisses a proactive scenario without sending a message', async () => {
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
          }));
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
