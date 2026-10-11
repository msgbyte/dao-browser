// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {StreamCallbacks} from '../agent_bridge.js';
import type {LLMProviderConfig} from '../pi_llm_stream.js';

const mocks = vi.hoisted(() => ({
  listeners: new Map<string, (payload: unknown) => Promise<void>|void>(),
  call: vi.fn(),
  config: vi.fn(),
  recordApiCall: vi.fn(),
  initI18n: vi.fn(),
  initialized: false,
}));

vi.mock('../agent_bridge.js', () => ({
  addWebUIListener: (name: string, listener: (payload: unknown) => void) =>
      mocks.listeners.set(name, listener),
  recordApiCall: (...args: unknown[]) => mocks.recordApiCall(...args),
}));
vi.mock('../pi_llm_stream.js', () => ({
  callLLMStreamingWithPi: (...args: unknown[]) => mocks.call(...args),
}));
vi.mock('../llm_config.js', () => ({
  getActiveLLMConfig: () => mocks.config(),
}));
vi.mock('../llm_cost.js', () => ({
  getCostRatesForConfig: () => ({input: 2, output: 6}),
}));
vi.mock('../i18n/i18n.js', () => ({
  initI18n: () => mocks.initI18n(),
  currentLocale: () => mocks.initialized ? 'zh-CN' : 'en',
}));

import '../dao_tab_classification.js';

const CONFIG = {
  provider: 'openai', apiKey: 'test-key', baseUrl: '', model: 'test-model',
};
const TABS = [
  {tabId: 'tab-1', title: 'TypeScript guide', url: 'https://example.com/docs'},
  {tabId: 'tab-2', title: 'Train tickets', url: 'https://example.org/travel'},
];
const FOLDERS = [{id: 'folder-1', name: 'Development'}];
const GROUPS = [
  {folderId: 'folder-1', name: 'Development', tabIds: ['tab-1']},
  {folderId: '', name: 'Travel', tabIds: ['tab-2']},
];
const VALID = JSON.stringify(GROUPS);

function request(requestId = 'request-1') {
  return {requestId, tabs: TABS, folders: FOLDERS};
}

function emit(payload: unknown = request()): Promise<void>|void {
  return mocks.listeners.get('dao-classify-tabs')!(payload);
}

function cancel(requestId: string) {
  mocks.listeners.get('dao-classify-tabs-cancel')!(requestId);
}

function respondWith(content: string) {
  mocks.call.mockImplementationOnce(async (
      _messages: unknown, _tools: unknown, callbacks: StreamCallbacks) => {
    callbacks.onDone(content, [], {
      prompt_tokens: 200, completion_tokens: 60, total_tokens: 260,
    });
  });
}

function holdResponse() {
  let callbacks: StreamCallbacks;
  let signal: AbortSignal;
  const started = new Promise<void>(resolve => {
    mocks.call.mockImplementationOnce((
        _messages: unknown, _tools: unknown, cb: StreamCallbacks,
        config: LLMProviderConfig) => {
      callbacks = cb;
      signal = config.signal!;
      resolve();
      return new Promise<void>(() => {});
    });
  });
  return {started, callbacks: () => callbacks, signal: () => signal};
}

function expectFailure(requestId = 'request-1') {
  expect(chrome.send).toHaveBeenCalledWith(
      'daoTabClassificationResult', [requestId, {ok: false}]);
}

describe('tab classification runner', () => {
  beforeEach(() => {
    vi.stubGlobal('chrome', {send: vi.fn()});
    mocks.call.mockReset();
    mocks.config.mockReset().mockReturnValue({...CONFIG});
    mocks.recordApiCall.mockReset();
    mocks.initialized = false;
    mocks.initI18n.mockReset().mockImplementation(async () => {
      mocks.initialized = true;
    });
  });

  afterEach(() => {
    window.dispatchEvent(new Event('pagehide'));
    vi.useRealTimers();
    vi.unstubAllGlobals();
  });

  it('classifies only the supplied metadata with no tools or chat history',
     async () => {
       respondWith(VALID);
       await emit({
         ...request(),
         history: 'private history must not be sent',
         tabs: TABS.map(tab => ({...tab, pageContent: 'private page text'})),
       });

       expect(chrome.send).toHaveBeenCalledExactlyOnceWith(
           'daoTabClassificationResult',
           ['request-1', {ok: true, groups: GROUPS}]);
       const [messages, tools, , config] = mocks.call.mock.calls[0]!;
       expect(tools).toEqual([]);
       expect(config).toEqual({...CONFIG, signal: expect.any(AbortSignal)});
       expect(messages).toHaveLength(2);
       expect(messages[0].role).toBe('system');
       expect(messages[0].content).toMatch(/existing folders/i);
       expect(messages[0].content).toMatch(/untrusted/i);
       expect(messages[0].content).toContain('zh-CN');
       expect(messages[1]).toEqual({
         role: 'user', content: JSON.stringify({tabs: TABS, folders: FOLDERS}),
       });
       expect(mocks.recordApiCall).toHaveBeenCalledWith(200, 60, 2, 6);
     });

  it('trims a new category and counts its Unicode codepoints', async () => {
    const name = '\u{1F680}'.repeat(100);
    respondWith(JSON.stringify([{folderId: '', name: ` ${name} `,
      tabIds: ['tab-1', 'tab-2']} ]));
    await emit();
    expect(chrome.send).toHaveBeenCalledWith('daoTabClassificationResult', [
      'request-1', {ok: true, groups: [{folderId: '', name,
        tabIds: ['tab-1', 'tab-2']}]},
    ]);
  });

  it.each([
    ['non-JSON', 'not JSON'],
    ['markdown fence', `\`\`\`json\n${VALID}\n\`\`\``],
    ['object instead of array', '{}'],
    ['empty result', '[]'],
    ['null group', '[null]'],
    ['unknown tab', JSON.stringify([
      {...GROUPS[0], tabIds: ['tab-1', 'unknown']}, GROUPS[1],
    ])],
    ['duplicate tab', JSON.stringify([
      {...GROUPS[0], tabIds: ['tab-1', 'tab-1']}, GROUPS[1],
    ])],
    ['duplicate across groups', JSON.stringify([
      GROUPS[0], {...GROUPS[1], tabIds: ['tab-1', 'tab-2']},
    ])],
    ['missing tab', JSON.stringify([GROUPS[0]])],
    ['unknown folder', JSON.stringify([
      {...GROUPS[0], folderId: 'unknown'}, GROUPS[1],
    ])],
    ['renamed existing folder', JSON.stringify([
      {...GROUPS[0], name: 'Renamed'}, GROUPS[1],
    ])],
    ['duplicate folder', JSON.stringify([
      GROUPS[0], {...GROUPS[0], tabIds: ['tab-2']},
    ])],
    ['duplicate normalized name', JSON.stringify([
      GROUPS[0], {...GROUPS[1], name: ' development '},
    ])],
    ['blank new name', JSON.stringify([
      GROUPS[0], {...GROUPS[1], name: '  '},
    ])],
    ['overlong new name', JSON.stringify([
      GROUPS[0], {...GROUPS[1], name: '\u{1F680}'.repeat(101)},
    ])],
    ['missing folderId', JSON.stringify([
      {name: 'All tabs', tabIds: ['tab-1', 'tab-2']},
    ])],
    ['numeric tab ID', JSON.stringify([
      {...GROUPS[0], tabIds: [1, 'tab-2']},
    ])],
    ['empty group', JSON.stringify([...GROUPS,
      {folderId: '', name: 'Empty', tabIds: []}])],
  ])('rejects %s output without returning partial groups', async (_name, raw) => {
    respondWith(raw);
    await emit();
    expectFailure();
    expect(chrome.send).toHaveBeenCalledTimes(1);
  });

  it.each(['provider', 'apiKey', 'model'])(
      'rejects a missing %s configuration', async field => {
        mocks.config.mockReturnValue({...CONFIG, [field]: ''});
        await emit();
        expectFailure();
        expect(mocks.call).not.toHaveBeenCalled();
      });

  it.each(['callback', 'rejection'])(
      'reports a provider %s as failure', async mode => {
        mocks.call.mockImplementationOnce(async (
            _messages: unknown, _tools: unknown, callbacks: StreamCallbacks) => {
          if (mode === 'rejection') throw new Error('Provider failed');
          callbacks.onError('Provider failed', 'Connection lost');
        });
        await emit();
        expectFailure();
      });

  it.each([
    ['too many tabs', {...request(), tabs: Array.from({length: 1001}, (_, i) =>
      ({tabId: String(i), title: '', url: ''}))}],
    ['too many folders', {...request(), folders: Array.from({length: 501},
      (_, i) => ({id: String(i), name: 'Folder'}))}],
    ['oversized UTF-8 input', {...request(), tabs: [
      {...TABS[0], title: '\u4E00'.repeat(180000)}, TABS[1],
    ]}],
    ['duplicate input tab', {...request(), tabs: [TABS[0], TABS[0]]}],
    ['duplicate input folder', {...request(), folders: [FOLDERS[0], FOLDERS[0]]}],
    ['invalid tab metadata', {...request(), tabs: [{...TABS[0], title: 4}]}],
  ])('rejects %s before calling the model', async (_name, payload) => {
    await emit(payload);
    expectFailure();
    expect(mocks.call).not.toHaveBeenCalled();
  });

  it('aborts oversized streaming output before accepting completion',
     async () => {
       const pending = holdResponse();
       const finished = emit();
       await pending.started;
       pending.callbacks().onToken('\u4E00'.repeat(180000));
       pending.callbacks().onDone(VALID, []);
       await finished;
       expect(pending.signal().aborted).toBe(true);
       expectFailure();
     });

  it('rejects oversized final output without token callbacks', async () => {
    respondWith(' '.repeat(512 * 1024) + VALID);
    await emit();
    expectFailure();
  });

  it('aborts after 60 seconds even if the provider never settles', async () => {
    vi.useFakeTimers();
    const pending = holdResponse();
    const finished = emit();
    await pending.started;
    await vi.advanceTimersByTimeAsync(60000);
    await finished;
    expect(pending.signal().aborted).toBe(true);
    expectFailure();
    pending.callbacks().onDone(VALID, []);
    await Promise.resolve();
    expect(chrome.send).toHaveBeenCalledTimes(1);
  });

  it('rejects overlap and releases the guard after cancellation', async () => {
    const pending = holdResponse();
    const first = emit();
    await pending.started;
    await emit(request('overlap'));
    expectFailure('overlap');
    expect(mocks.call).toHaveBeenCalledTimes(1);
    cancel('other-request');
    expect(pending.signal().aborted).toBe(false);
    cancel('request-1');
    await first;
    expectFailure();
    expect(pending.signal().aborted).toBe(true);
    pending.callbacks().onDone(VALID, []);
    respondWith(VALID);
    await emit(request('next'));
    expect(chrome.send).toHaveBeenCalledTimes(3);
    expect(chrome.send).toHaveBeenLastCalledWith(
        'daoTabClassificationResult', ['next', {ok: true, groups: GROUPS}]);
  });

  it('does not start a model call when cancelled while the locale loads',
     async () => {
       let ready: () => void;
       mocks.initI18n.mockReturnValueOnce(new Promise<void>(resolve => {
         ready = resolve;
       }));
       const finished = emit();
       cancel('request-1');
       await finished;
       ready!();
       await Promise.resolve();
       expectFailure();
       expect(mocks.call).not.toHaveBeenCalled();
     });

  it('aborts on pagehide and rejects a late completion', async () => {
    const pending = holdResponse();
    const finished = emit();
    await pending.started;
    window.dispatchEvent(new Event('pagehide'));
    pending.callbacks().onDone(VALID, []);
    await finished;
    expect(pending.signal().aborted).toBe(true);
    expectFailure();
    expect(chrome.send).toHaveBeenCalledTimes(1);
  });

  it('rejects a tool call instead of executing it', async () => {
    const pending = holdResponse();
    const finished = emit();
    await pending.started;
    pending.callbacks().onToolCall({id: 'tool-1', type: 'function',
      function: {name: 'navigate', arguments: '{}'}});
    await finished;
    expect(pending.signal().aborted).toBe(true);
    expectFailure();
  });
});
