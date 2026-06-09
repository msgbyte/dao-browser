// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {ElementContextCapture} from '../dao_element_context.js';

const pickerMocks = vi.hoisted(() => ({
  startElementPicker: vi.fn(),
  cancelElementPicker: vi.fn(),
  callNative: vi.fn(),
}));

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('../../sidebar/__tests__/lit_test_shim.js');
});

vi.mock('../agent_bridge.js', () => ({
  BASE_SYSTEM_PROMPT: 'base prompt',
  currentSoulContent: 'soul',
  callNative: (...args: unknown[]) => pickerMocks.callNative(...args),
  recordApiCall: vi.fn(),
  refreshSoulContent: vi.fn(),
  soulChannel: {
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  },
}));

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
    pickerMocks.callNative.mockResolvedValue({success: true});
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
});
