// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {ElementContextCapture} from '../dao_element_context.js';

const pickerMocks = vi.hoisted(() => ({
  startElementPicker: vi.fn(),
  cancelElementPicker: vi.fn(),
  callNative: vi.fn(),
  callNativeArgs: vi.fn(),
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

vi.mock('../skill_registry.js', () => ({
  getAllSkills: () => [],
  initSkillRegistry: vi.fn(),
  loadSkillInstructions: vi.fn(async () => null),
  refreshSkillRegistry: vi.fn(async () => undefined),
  refreshSkillRegistryIfStale: vi.fn(async () => false),
}));

vi.mock('../tool_catalog.js', () => ({
  toolConfigChannel: {
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  },
}));

vi.mock('../i18n/i18n.js', () => ({
  t: (key: string, vars?: Record<string, string | number>) =>
      vars?.['label'] ? `${key}:${vars['label']}` : key,
}));

vi.mock('../vendor/pi_runtime_bundle.js', () => ({
  Agent: class {
    state = {
      systemPrompt: '',
      model: {},
      thinkingLevel: 'off',
      tools: [],
      messages: [],
      isStreaming: false,
    };
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
      expect(pickerMocks.callNative.mock.calls.map(call => call[0])).toEqual([
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
      expect(pickerMocks.callNative.mock.calls.map(call => call[0])).toEqual([
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
      expect(pickerMocks.callNative.mock.calls.map(call => call[0])).toEqual([
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

  it('attaches memory context on send', async () => {
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
          recentMessages: [],
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
      expect(sentAttachments).toHaveLength(1);
      expect(sentAttachments[0].extractedText).toContain('<memory-context');
      expect(sentAttachments[0].extractedText).toContain(
          'Prefers concise implementation notes');
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
});
