// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

type DreamRunListener = (payload: unknown) => void;

const mocks = vi.hoisted(() => ({
  listeners: new Map<string, DreamRunListener>(),
  addWebUIListener: vi.fn(),
  runDream: vi.fn(),
  runWeeklyDream: vi.fn(),
}));

vi.mock('../agent_bridge.js', () => ({
  addWebUIListener: (event: string, listener: DreamRunListener) => {
    mocks.addWebUIListener(event, listener);
    mocks.listeners.set(event, listener);
  },
}));
vi.mock('../dao_dream_runner.js', () => ({
  runDream: (...args: unknown[]) => mocks.runDream(...args),
}));
vi.mock('../dao_weekly_dream_runner.js', () => ({
  runWeeklyDream: (...args: unknown[]) => mocks.runWeeklyDream(...args),
}));

import '../dao_dream_dispatcher.js';

const DAILY_RESULT = {
  report_markdown: 'Daily report',
  habits: [],
  scenario_adjustments: [],
};
const WEEKLY_RESULT = {
  schema_version: 1,
  headline: 'Continue the migration',
  primary_thread: {
    title: 'Migration',
    status_summary: 'Two approaches were compared.',
    next_step: 'Test the lower-risk approach.',
    confidence: 0.8,
    source_refs: ['page_1'],
  },
  secondary_threads: [],
  retained_outcomes: [],
  footprint_summary: {
    themes: ['Migration'],
    time_pattern: 'Afternoons',
  },
};

function emit(payload: unknown) {
  const listener = mocks.listeners.get('dream-run');
  expect(listener).toBeTypeOf('function');
  listener!(payload);
}

function dailyPayload(requestId = 'daily-request') {
  return {
    requestId,
    reportKind: 'daily',
    periodStart: '2026-07-16',
    periodEnd: '2026-07-17',
    material: {history: []},
    debug: true,
  } as const;
}

function weeklyPayload(requestId = 'weekly-request') {
  return {
    requestId,
    reportKind: 'weekly',
    periodStart: '2026-07-06',
    periodEnd: '2026-07-13',
    material: {history: []},
    debug: false,
  } as const;
}

describe('dream dispatcher', () => {
  beforeEach(() => {
    mocks.runDream.mockReset();
    mocks.runWeeklyDream.mockReset();
    const send = vi.fn();
    (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  });

  it('is the single dream-run listener', () => {
    expect(mocks.addWebUIListener).toHaveBeenCalledTimes(1);
    expect(mocks.addWebUIListener).toHaveBeenCalledWith(
        'dream-run', expect.any(Function));
  });

  it('dispatches daily and echoes the request ID in the locked envelope',
     async () => {
       mocks.runDream.mockResolvedValueOnce(DAILY_RESULT);

       emit(dailyPayload('daily-123'));

       await vi.waitFor(() => expect(chrome.send).toHaveBeenCalledWith(
           'dreamComplete', ['daily-123', {
             status: 'completed',
             result: DAILY_RESULT,
           }]));
       expect(mocks.runDream).toHaveBeenCalledWith(
           '2026-07-16', {history: []}, {debug: true});
       expect(mocks.runWeeklyDream).not.toHaveBeenCalled();
     });

  it('dispatches weekly and echoes the request ID in the locked envelope',
     async () => {
       mocks.runWeeklyDream.mockResolvedValueOnce(WEEKLY_RESULT);

       emit(weeklyPayload('weekly-456'));

       await vi.waitFor(() => expect(chrome.send).toHaveBeenCalledWith(
           'dreamComplete', ['weekly-456', {
             status: 'completed',
             result: WEEKLY_RESULT,
           }]));
       expect(mocks.runWeeklyDream).toHaveBeenCalledWith(
           {start: '2026-07-06', end: '2026-07-13'},
           {history: []}, {debug: false});
       expect(mocks.runDream).not.toHaveBeenCalled();
     });

  it('maps a sparse weekly result to skipped without a result field',
     async () => {
       mocks.runWeeklyDream.mockResolvedValueOnce(null);

       emit(weeklyPayload('sparse-789'));

       await vi.waitFor(() => expect(chrome.send).toHaveBeenCalledWith(
           'dreamComplete', ['sparse-789', {status: 'skipped'}]));
     });

  it.each([
    ['configuration', new Error('no LLM api key configured')],
    ['invalid_output', new Error(
      'invalid weekly output after retry: headline must be a string')],
    ['provider', new Error('network connection failed')],
  ])('classifies %s failures into the locked envelope',
     async (code, error) => {
       mocks.runWeeklyDream.mockRejectedValueOnce(error);

       emit(weeklyPayload(`failed-${code}`));

       await vi.waitFor(() => expect(chrome.send).toHaveBeenCalledWith(
           'dreamFailed', [`failed-${code}`, {
             code,
             message: error.message,
           }]));
     });

  it('ignores a second event while a request is in flight', async () => {
    let finishDaily: ((value: typeof DAILY_RESULT) => void)|undefined;
    mocks.runDream.mockReturnValueOnce(new Promise(resolve => {
      finishDaily = resolve;
    }));

    emit(dailyPayload('first'));
    emit(weeklyPayload('second'));

    expect(mocks.runDream).toHaveBeenCalledTimes(1);
    expect(mocks.runWeeklyDream).not.toHaveBeenCalled();
    expect(chrome.send).not.toHaveBeenCalled();

    finishDaily!(DAILY_RESULT);
    await vi.waitFor(() => expect(chrome.send).toHaveBeenCalledWith(
        'dreamComplete', ['first', {
          status: 'completed',
          result: DAILY_RESULT,
        }]));
    expect(chrome.send).toHaveBeenCalledTimes(1);
  });

  it.each([
    ['missing', () => {
      const payload = {...dailyPayload()} as Record<string, unknown>;
      delete payload['material'];
      return payload;
    }],
    ['inherited', () => {
      const payload = {...dailyPayload()} as Record<string, unknown>;
      delete payload['material'];
      return Object.assign(
          Object.create({material: {history: ['inherited']}}), payload);
    }],
  ])('ignores a payload with a %s material field', async (_name, makePayload) => {
    mocks.runDream.mockResolvedValueOnce(DAILY_RESULT);

    emit(makePayload());
    await Promise.resolve();
    await Promise.resolve();

    expect(mocks.runDream).not.toHaveBeenCalled();
    expect(mocks.runWeeklyDream).not.toHaveBeenCalled();
    expect(chrome.send).not.toHaveBeenCalled();
  });

  it('releases the guard when completion delivery throws', async () => {
    const send = chrome.send as ReturnType<typeof vi.fn>;
    send.mockImplementationOnce(() => {
      throw new Error('bridge send failed');
    });
    mocks.runDream.mockResolvedValue(DAILY_RESULT);

    emit(dailyPayload('delivery-fails'));

    await vi.waitFor(() => expect(send).toHaveBeenCalledTimes(2));
    expect(send).toHaveBeenNthCalledWith(2, 'dreamFailed', [
      'delivery-fails',
      {code: 'provider', message: 'bridge send failed'},
    ]);

    emit(dailyPayload('after-delivery-failure'));

    await vi.waitFor(() => expect(send).toHaveBeenCalledTimes(3));
    expect(send).toHaveBeenNthCalledWith(3, 'dreamComplete', [
      'after-delivery-failure',
      {status: 'completed', result: DAILY_RESULT},
    ]);
    expect(mocks.runDream).toHaveBeenCalledTimes(2);
  });

  it('ignores malformed or unsupported payloads', async () => {
    emit(null);
    emit({...dailyPayload(), requestId: ''});
    emit({...dailyPayload(), reportKind: 'monthly'});
    emit({...dailyPayload(), periodEnd: 7});
    const missingDebug = {...dailyPayload()} as Record<string, unknown>;
    delete missingDebug['debug'];
    emit(missingDebug);

    await Promise.resolve();
    expect(mocks.runDream).not.toHaveBeenCalled();
    expect(mocks.runWeeklyDream).not.toHaveBeenCalled();
    expect(chrome.send).not.toHaveBeenCalled();
  });
});
