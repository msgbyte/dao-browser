// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

const settingsMocks = vi.hoisted(() => ({
  callNative: vi.fn(),
  callNativeArgs: vi.fn(),
  chromeSend: vi.fn(),
  refreshSoulContent: vi.fn(),
  resetAgentStats: vi.fn(),
  saveSoul: vi.fn(),
  setActiveProvider: vi.fn(),
  setProviderConfig: vi.fn(),
  setSearchSourceOverride: vi.fn(),
  toolConfigListeners: {} as Record<string, Array<() => void>>,
}));

const catalogMocks = vi.hoisted(() => ({
  initializeBrowserToolCatalog: vi.fn(),
  definitions: [{
    type: 'function',
    function: {
      name: 'execute_script',
      description: 'Execute JavaScript code on the current page and return the result',
      parameters: {type: 'object', properties: {}, required: []},
    },
  }],
}));

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  const lit = await import('../../sidebar/__tests__/lit_test_shim.js');
  Object.defineProperty(lit.CrLitElement.prototype, 'disconnectedCallback', {
    configurable: true,
    value() {},
  });
  return lit;
});

vi.mock('../agent_bridge.js', () => ({
  callNative: (...args: unknown[]) => settingsMocks.callNative(...args),
  callNativeArgs: (...args: unknown[]) =>
      settingsMocks.callNativeArgs(...args),
  CONFIDENCE_THRESHOLD_MAP: {quiet: 0.85, balanced: 0.75, active: 0.6},
  currentSoulContent: 'soul',
  DEFAULT_SOUL: 'default soul',
  getAgentStats: () => ({
    apiCalls: 0,
    toolCalls: {},
    totalTokens: 0,
    promptTokens: 0,
    completionTokens: 0,
    estimatedCost: 0,
    lastReset: Date.now(),
  }),
  refreshSoulContent: settingsMocks.refreshSoulContent,
  resetAgentStats: settingsMocks.resetAgentStats,
  saveSoul: settingsMocks.saveSoul,
  soulChannel: {
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  },
  getAgentToolDefinitions: () => catalogMocks.definitions,
}));

vi.mock('../browser_tool_catalog.js', () => ({
  initializeBrowserToolCatalog: () =>
      catalogMocks.initializeBrowserToolCatalog(),
}));

vi.mock('../i18n/i18n.js', () => ({
  t: (key: string, vars?: Record<string, string | number>) => {
    if (!vars) return key;
    return Object.entries(vars).reduce(
        (text, [name, value]) => text.replaceAll(`{${name}}`, String(value)),
        key);
  },
}));

vi.mock('../llm_config.js', () => ({
  getActiveProvider: () => 'openai-compatible',
  getProviderConfig: () => ({
    apiKey: '',
    baseUrl: 'https://api.openai.com/v1',
    model: 'gpt-5',
  }),
  LLM_PROVIDERS: [{
    id: 'openai-compatible',
    label: 'OpenAI Compatible',
    defaultModel: 'gpt-5',
    apiKeyPlaceholder: 'key',
    needsBaseUrl: true,
    defaultBaseUrl: 'https://api.openai.com/v1',
  }],
  setActiveProvider: settingsMocks.setActiveProvider,
  setProviderConfig: settingsMocks.setProviderConfig,
}));

vi.mock('../tool_catalog.js', () => ({
  countEnabled: () => ({enabled: 1, total: 1}),
  getGroupState: () => 'all',
  isGroupExpanded: () => true,
  isToolEnabled: () => true,
  setGroupEnabled: vi.fn(),
  setGroupExpanded: vi.fn(),
  setToolEnabled: vi.fn(),
  TOOL_GROUPS: [{id: 'page', label: 'Page', toolNames: ['execute_script']}],
  toolConfigChannel: {
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  },
}));

vi.mock('../web_search/index.js', () => ({
  getJinaApiKey: () => '',
  getSearchSourceOverride: () => 'auto',
  setJinaApiKey: vi.fn(),
  setSearchSourceOverride: settingsMocks.setSearchSourceOverride,
}));

import '../dao_settings_view.js';

type TestSettingsView = HTMLElement & {
  updateComplete: Promise<boolean>;
  switchSubTab: (tab: string) => void;
  memoryEnabled_: boolean;
  dreamEnabled_: boolean;
};

async function mountMemorySettings(): Promise<TestSettingsView> {
  const view = document.createElement('dao-settings-view') as TestSettingsView;
  document.body.appendChild(view);
  await view.updateComplete;
  view.switchSubTab('memory');
  view.memoryEnabled_ = true;
  view.dreamEnabled_ = true;
  await view.updateComplete;
  return view;
}

describe('dao-settings-view dream controls', () => {
  beforeEach(() => {
    (globalThis as unknown as {chrome: {send: typeof settingsMocks.chromeSend}})
        .chrome = {send: settingsMocks.chromeSend};
    settingsMocks.chromeSend.mockReset();
    document.body.innerHTML = '';
    localStorage.clear();
    settingsMocks.callNative.mockReset();
    settingsMocks.callNativeArgs.mockReset();
    settingsMocks.callNative.mockResolvedValue(true);
    settingsMocks.callNativeArgs.mockImplementation(async (method: string) => {
      switch (method) {
        case 'getMemoryEnabled':
        case 'getDreamEnabled':
          return true;
        case 'getDreamDebug':
          return false;
        case 'getStorageStats':
          return {
            totalSize: 0,
            conversationCount: 0,
            episodeCount: 0,
            preferenceCount: 0,
          };
        default:
          return true;
      }
    });
    catalogMocks.initializeBrowserToolCatalog.mockReset();
    catalogMocks.initializeBrowserToolCatalog.mockResolvedValue(undefined);
  });

  afterEach(() => {
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
    document.body.innerHTML = '';
    vi.restoreAllMocks();
  });

  it('opens dream history from the Dream Analysis settings area', async () => {
    const view = await mountMemorySettings();
    const button = Array.from(view.shadowRoot!.querySelectorAll('button'))
        .find(el => el.textContent?.trim() ===
            'settings.dream.history_button') as HTMLButtonElement | undefined;

    expect(button).toBeTruthy();
    button!.click();
    await Promise.resolve();

    expect(settingsMocks.chromeSend).toHaveBeenCalledWith(
        'openDreamReport', []);
  });

  it('loads adds and removes dream excluded domains', async () => {
    settingsMocks.callNativeArgs.mockImplementation(async (method: string,
        value?: unknown) => {
      switch (method) {
        case 'getMemoryEnabled':
        case 'getDreamEnabled':
          return true;
        case 'getDreamDebug':
          return false;
        case 'getDreamExcludedDomains':
          return ['github.com'];
        case 'addDreamExcludedDomain':
          expect(value).toBe('https://Example.com/private');
          return {domain: 'example.com'};
        case 'removeDreamExcludedDomain':
          expect(value).toBe('github.com');
          return true;
        case 'getStorageStats':
          return {
            totalSize: 0,
            conversationCount: 0,
            episodeCount: 0,
            preferenceCount: 0,
          };
        default:
          return true;
      }
    });

    const view = await mountMemorySettings();
    await Promise.resolve();
    await view.updateComplete;
    expect(view.shadowRoot!.textContent).toContain('github.com');

    const input = view.shadowRoot!.querySelector<HTMLInputElement>(
        'input[data-testid="dream-excluded-domain-input"]');
    expect(input).toBeTruthy();
    input!.value = 'https://Example.com/private';
    input!.dispatchEvent(new Event('input', {bubbles: true}));
    input!.dispatchEvent(new KeyboardEvent('keydown', {
      key: 'Enter',
      bubbles: true,
    }));
    await Promise.resolve();
    await view.updateComplete;

    expect(settingsMocks.callNativeArgs).toHaveBeenCalledWith(
        'addDreamExcludedDomain', 'https://Example.com/private');
    expect(view.shadowRoot!.textContent).toContain('example.com');

    const removeButton = view.shadowRoot!.querySelector<HTMLButtonElement>(
        'button[data-domain="github.com"]');
    expect(removeButton).toBeTruthy();
    removeButton!.click();
    await Promise.resolve();
    await view.updateComplete;

    expect(settingsMocks.callNativeArgs).toHaveBeenCalledWith(
        'removeDreamExcludedDomain', 'github.com');
    expect(view.shadowRoot!.textContent).not.toContain('github.com');
  });

  it('persists the General debug mode toggle', async () => {
    const listener = vi.fn();
    window.addEventListener('dao-agent-debug-mode-changed', listener);
    const view = document.createElement('dao-settings-view') as TestSettingsView;
    document.body.appendChild(view);
    await view.updateComplete;

    const input = view.shadowRoot!.querySelector(
        'label[aria-label="settings.general.debug_mode_name"] input',
    ) as HTMLInputElement|null;
    expect(input).toBeTruthy();
    expect(input!.checked).toBe(false);

    (view as unknown as {setDebugMode_: (enabled: boolean) => void})
        .setDebugMode_(true);
    await view.updateComplete;

    expect(localStorage.getItem('dao_agent_debug_mode')).toBe('true');
    expect(listener).toHaveBeenCalledTimes(1);
    expect(listener.mock.calls[0][0]).toMatchObject({
      detail: {enabled: true},
    });
    window.removeEventListener('dao-agent-debug-mode-changed', listener);
  });

  it('broadcasts proactive setting changes to the current agent view',
     async () => {
       const listener = vi.fn();
       window.addEventListener('dao-proactive-enabled-changed', listener);
       const view = await mountMemorySettings();
       const input = view.shadowRoot!.querySelector(
           'label[aria-label="settings.memory.proactive_name"] input',
       ) as HTMLInputElement|null;
       expect(input).toBeTruthy();
       settingsMocks.callNativeArgs.mockClear();

       input!.checked = false;
       input!.dispatchEvent(new Event('change'));
       await view.updateComplete;

       expect(localStorage.getItem('dao_proactive_enabled')).toBe('false');
       expect(settingsMocks.callNativeArgs).toHaveBeenCalledWith(
           'setProactiveEnabled', false);
       expect(listener).toHaveBeenCalledTimes(1);
       expect(listener.mock.calls[0][0]).toMatchObject({
         detail: {enabled: false},
       });
       window.removeEventListener('dao-proactive-enabled-changed', listener);
     });
});

describe('dao-settings-view tool descriptions', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    catalogMocks.initializeBrowserToolCatalog.mockReset();
    catalogMocks.initializeBrowserToolCatalog.mockResolvedValue(undefined);
  });

  afterEach(() => {
    document.body.innerHTML = '';
  });

  it('renders browser-tool descriptions after catalog initialization',
     async () => {
       const view = document.createElement(
           'dao-settings-view') as TestSettingsView;
       document.body.appendChild(view);
       await view.updateComplete;

       view.switchSubTab('tools');
       await Promise.resolve();
       await view.updateComplete;

       expect(catalogMocks.initializeBrowserToolCatalog).toHaveBeenCalled();
       expect(view.shadowRoot!.textContent).toContain(
           'Execute JavaScript code on the current page and return the result');
     });

  it('retries a failed catalog load and restores browser-tool descriptions',
     async () => {
       catalogMocks.initializeBrowserToolCatalog.mockReset();
       catalogMocks.initializeBrowserToolCatalog
           .mockRejectedValueOnce(new Error('temporary catalog failure'))
           .mockResolvedValueOnce(undefined);
       const view = document.createElement(
           'dao-settings-view') as TestSettingsView;
       document.body.appendChild(view);
       await Promise.resolve();
       await view.updateComplete;

       view.switchSubTab('tools');
       await view.updateComplete;

       expect(view.shadowRoot!.textContent).toContain(
           'settings.tools.catalog_load_failed');
       const retry = view.shadowRoot!.querySelector<HTMLButtonElement>(
           'button[data-testid="retry-tool-catalog"]');
       expect(retry).toBeTruthy();

       retry!.click();
       await Promise.resolve();
       await view.updateComplete;

       expect(catalogMocks.initializeBrowserToolCatalog).toHaveBeenCalledTimes(2);
       expect(view.shadowRoot!.textContent).toContain(
           'Execute JavaScript code on the current page and return the result');
     });
});
