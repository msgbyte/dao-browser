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
});
