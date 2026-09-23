// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

type Listener = (payload: unknown) => void;

const mocks = vi.hoisted(() => {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  return {
    listeners: new Map<string, Listener>(),
    callLLMStreamingWithPi: vi.fn(),
    send,
  };
});

vi.mock('../agent_bridge.js', () => ({
  addWebUIListener: (event: string, listener: Listener) => {
    mocks.listeners.set(event, listener);
  },
}));
vi.mock('../pi_llm_stream.js', () => ({
  callLLMStreamingWithPi: (...args: unknown[]) =>
      mocks.callLLMStreamingWithPi(...args),
}));

import {CONNECTION_TEST_TIMEOUT_MS} from '../llm_connection_test.js';

const importTimeCalls = [...mocks.send.mock.calls];

const CONFIG = {
  provider: 'anthropic-compatible',
  model: 'vendor/claude-custom',
  apiKey: 'key',
  baseUrl: 'https://gateway.example',
};

function emit(payload: unknown) {
  const listener = mocks.listeners.get('dao-agent-connection-test');
  expect(listener).toBeTypeOf('function');
  listener!(payload);
}

describe('LLM connection test runner', () => {
  const send = mocks.send;

  beforeEach(() => {
    mocks.callLLMStreamingWithPi.mockReset();
    send.mockClear();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('registers with the browser once its listener exists', () => {
    expect(mocks.listeners.has('dao-agent-connection-test')).toBe(true);
    expect(importTimeCalls).toEqual([['registerDaoAgentConnectionTester']]);
  });

  it('reports success through the same pi adapter chat uses', async () => {
    mocks.callLLMStreamingWithPi.mockImplementation(
        async (_msgs, tools, callbacks) => {
          expect(tools).toEqual([]);
          callbacks.onDone('OK', [], undefined);
        });

    emit({requestId: 'req-1', ...CONFIG});

    await vi.waitFor(() => expect(send).toHaveBeenCalledWith(
        'daoAgentConnectionTestResult',
        ['req-1', {ok: true, latencyMs: expect.any(Number)}]));
    expect(mocks.callLLMStreamingWithPi.mock.calls[0]![3]).toMatchObject(
        CONFIG);
  });

  it('forwards the provider error message', async () => {
    mocks.callLLMStreamingWithPi.mockImplementation(
        async (_msgs, _tools, callbacks) => {
          callbacks.onError('API Error', '401 invalid x-api-key');
        });

    emit({requestId: 'req-2', ...CONFIG});

    await vi.waitFor(() => expect(send).toHaveBeenCalledWith(
        'daoAgentConnectionTestResult', ['req-2', {
          ok: false,
          latencyMs: expect.any(Number),
          errorCode: 'provider',
          error: 'API Error: 401 invalid x-api-key',
        }]));
  });

  it('reports thrown model resolution errors', async () => {
    mocks.callLLMStreamingWithPi.mockRejectedValue(
        new Error('Unknown provider'));

    emit({requestId: 'req-3', ...CONFIG});

    await vi.waitFor(() => expect(send).toHaveBeenCalledWith(
        'daoAgentConnectionTestResult', ['req-3', {
          ok: false,
          latencyMs: expect.any(Number),
          errorCode: 'provider',
          error: 'Unknown provider',
        }]));
  });

  it('aborts and reports a timeout when the provider hangs', async () => {
    vi.useFakeTimers();
    mocks.callLLMStreamingWithPi.mockImplementation(
        (_msgs, _tools, _callbacks, config: {signal: AbortSignal}) =>
            new Promise((_resolve, reject) => {
              config.signal.addEventListener('abort', () => reject(
                  Object.assign(new Error('aborted'), {name: 'AbortError'})));
            }));

    emit({requestId: 'req-4', ...CONFIG});
    await vi.advanceTimersByTimeAsync(CONNECTION_TEST_TIMEOUT_MS);

    expect(send).toHaveBeenCalledWith('daoAgentConnectionTestResult', [
      'req-4',
      {ok: false, latencyMs: expect.any(Number), errorCode: 'timeout'},
    ]);
  });

  it('ignores malformed requests', () => {
    emit({requestId: '', ...CONFIG});
    emit({requestId: 'req-5', provider: 'anthropic'});

    expect(mocks.callLLMStreamingWithPi).not.toHaveBeenCalled();
  });
});
