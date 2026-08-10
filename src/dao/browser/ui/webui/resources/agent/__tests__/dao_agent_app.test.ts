// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

const mocks = vi.hoisted(() => {
  let resolveI18nPromise: () => void = () => {};
  const i18nPromise = new Promise<void>(resolve => {
    resolveI18nPromise = resolve;
  });
  return {
    callNative: vi.fn(),
    chatRequestUpdate: vi.fn(),
    prefillExternalPrompt: vi.fn(),
    openExternalSession: vi.fn(),
    refreshSkillRegistryIfStale: vi.fn(async () => false),
    initI18n: vi.fn(() => i18nPromise),
    resolveI18n: () => resolveI18nPromise(),
  };
});

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('../../sidebar/__tests__/lit_test_shim.js');
});

vi.mock('../agent_bridge.js', () => ({
  callNative: (...args: unknown[]) => mocks.callNative(...args),
}));

vi.mock('../dao_chat_view.js', () => {
  if (!customElements.get('dao-chat-view')) {
    customElements.define('dao-chat-view', class extends HTMLElement {
      requestUpdate = mocks.chatRequestUpdate;
      focusInput() {}
      startNewSession() {}
      openHistory() {}
      prefillExternalPrompt = mocks.prefillExternalPrompt;
      openExternalSession = mocks.openExternalSession;
    });
  }
  return {};
});

vi.mock('../dao_dream_dispatcher.js', () => ({}));

vi.mock('../i18n/i18n.js', () => ({
  initI18n: mocks.initI18n,
  t: (key: string) => key,
}));

vi.mock('../skill_registry.js', () => ({
  refreshSkillRegistryIfStale: () => mocks.refreshSkillRegistryIfStale(),
}));

async function loadApp() {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  await import('../dao_agent_app.js');
  const el = document.createElement('dao-agent-app') as HTMLElement & {
    updateComplete: Promise<boolean>;
  };
  document.body.appendChild(el);
  await el.updateComplete;
  mocks.chatRequestUpdate.mockClear();
  return {el, send};
}

describe('dao-agent-app i18n refresh', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    mocks.callNative.mockReset();
    mocks.chatRequestUpdate.mockReset();
    mocks.prefillExternalPrompt.mockReset();
    mocks.openExternalSession.mockReset();
    mocks.refreshSkillRegistryIfStale.mockReset();
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
    delete (window as unknown as {__daoExternalPrefill?: unknown})
        .__daoExternalPrefill;
    delete (window as unknown as {__daoExternalOpenSession?: unknown})
        .__daoExternalOpenSession;
  });

  it('refreshes mounted child views when the async locale finishes loading', async () => {
    const {el} = await loadApp();

    mocks.resolveI18n();
    await Promise.resolve();
    await el.updateComplete;

    expect(mocks.chatRequestUpdate).toHaveBeenCalled();
  });

  it('closes the sidebar after unified settings opens', async () => {
    mocks.callNative.mockResolvedValueOnce({success: true});
    const {el, send} = await loadApp();
    const settingsButton = el.shadowRoot!.querySelector<HTMLButtonElement>(
        '.dao-app-tab-bar .dao-app-tab:not(.active)');
    expect(settingsButton).not.toBeNull();
    settingsButton!.click();

    await vi.waitFor(() => {
      expect(mocks.callNative).toHaveBeenCalledWith('openAgentSettings');
    });
    expect(mocks.callNative).not.toHaveBeenCalledWith(
        'openTab', expect.anything());
    expect(send).toHaveBeenCalledWith('closeSidebar');
  });

  it('keeps the sidebar open when unified settings cannot open', async () => {
    mocks.callNative.mockRejectedValueOnce(new Error('navigation failed'));
    const {el, send} = await loadApp();
    const settingsButton = el.shadowRoot!.querySelector<HTMLButtonElement>(
        '.dao-app-tab-bar .dao-app-tab:not(.active)');
    expect(settingsButton).not.toBeNull();
    settingsButton!.click();

    await vi.waitFor(() => {
      expect(mocks.callNative).toHaveBeenCalledWith('openAgentSettings');
    });
    expect(send).not.toHaveBeenCalledWith('closeSidebar');
  });

  it('routes external prefill requests to the chat view', async () => {
    const {el} = await loadApp();
    expect(el.shadowRoot?.querySelector('dao-chat-view')).not.toBeNull();

    const prefill =
        (window as unknown as {__daoExternalPrefill: (text: string) => void})
            .__daoExternalPrefill;
    prefill('Review this report');
    await vi.waitFor(() => {
      expect(mocks.prefillExternalPrompt).toHaveBeenCalledWith(
          'Review this report');
    });
  });

  it('routes external session requests to the chat view', async () => {
    const {el} = await loadApp();
    expect(el.shadowRoot?.querySelector('dao-chat-view')).not.toBeNull();

    const openSession = (window as unknown as {
      __daoExternalOpenSession: (sessionId: string) => void,
    }).__daoExternalOpenSession;
    openSession('session-42');
    await vi.waitFor(() => {
      expect(mocks.openExternalSession).toHaveBeenCalledWith('session-42');
    });
  });
});
