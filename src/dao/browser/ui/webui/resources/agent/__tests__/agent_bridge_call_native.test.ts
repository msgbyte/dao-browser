// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, describe, expect, it, vi} from 'vitest';

import {callNative, cr, executeTool} from '../agent_bridge.js';

describe('callNative', () => {
  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
  });

  it('honors custom timeouts for long-running native calls', async () => {
    vi.useFakeTimers();
    const send = vi.fn();
    vi.stubGlobal('chrome', {send});

    const promise = callNative('startManualDream', undefined, {
      timeoutMs: 360000,
    });
    let settled = false;
    promise.then(
        () => {
          settled = true;
        },
        () => {
          settled = true;
        });

    await vi.advanceTimersByTimeAsync(15000);

    expect(settled).toBe(false);
    expect(send).toHaveBeenCalledTimes(1);
    const [, args] = send.mock.calls[0] as [string, unknown[]];
    const [callbackId] = args as [string, Record<string, unknown>];
    cr.webUIResponse(callbackId, true, true);

    await expect(promise).resolves.toBe(true);
  });

  it('cancels an in-flight native tool once and ignores a late response',
     async () => {
       const send = vi.fn();
       vi.stubGlobal('chrome', {send});
       const controller = new AbortController();

       const promise = callNative('agentClick', {selector: '#save'}, {
         signal: controller.signal,
         cancelMethod: 'cancelBrowserTool',
       });
       const callbackId =
           (send.mock.calls[0]![1] as unknown[])[0] as string;
       controller.abort();
       controller.abort();

       await expect(promise).rejects.toMatchObject({name: 'AbortError'});
       expect(send).toHaveBeenCalledWith(
           'cancelBrowserTool', [callbackId]);
       expect(send.mock.calls.filter(call => call[0] === 'cancelBrowserTool'))
           .toHaveLength(1);
       expect(() => cr.webUIResponse(callbackId, true, {success: true}))
           .not.toThrow();
     });

  it('marks only explicit legacy UI one-shot Page calls', async () => {
    const send = vi.fn();
    vi.stubGlobal('chrome', {send});

    const promise = executeTool(
        'get_page_info', {},
        {context: 'legacy_ui_one_shot'} as never);
    const [, args] = send.mock.calls[0] as [string, unknown[]];
    const [callbackId, params] =
        args as [string, Record<string, unknown>];

    expect(params).toEqual({
      __daoAgentExecutionContext: 'legacy_ui_one_shot',
    });
    cr.webUIResponse(callbackId, true, {url: 'https://example.com'});
    await expect(promise).resolves.toEqual({url: 'https://example.com'});

    await expect(executeTool(
                     'click_element', {selector: '#save'},
                     {context: 'legacy_ui_one_shot'} as never))
        .rejects.toThrow(
            'not authorized for legacy UI one-shot execution');
    expect(send).toHaveBeenCalledTimes(1);
  });

  it('does not mark default model Page calls', async () => {
    const send = vi.fn();
    vi.stubGlobal('chrome', {send});

    const promise = executeTool('get_page_info', {});
    const [, args] = send.mock.calls[0] as [string, unknown[]];
    const [callbackId, params] =
        args as [string, Record<string, unknown>];

    expect(params).toEqual({});
    cr.webUIResponse(callbackId, true, {code: 'LEASE_BUSY'});
    await expect(promise).resolves.toEqual({code: 'LEASE_BUSY'});
  });

  it('forwards stable and legacy tab selectors without a one-shot marker',
     async () => {
       const send = vi.fn();
       vi.stubGlobal('chrome', {send});

       const switchPromise = executeTool(
           'switch_tab', {tab_id: 'stable-switch', index: 7});
       const [, switchArgs] = send.mock.calls[0] as [string, unknown[]];
       const [switchCallbackId, switchParams] =
           switchArgs as [string, Record<string, unknown>];
       expect(switchParams).toEqual({
         tab_id: 'stable-switch',
         index: 7,
       });
       expect(switchParams).not.toHaveProperty('__daoAgentExecutionContext');
       cr.webUIResponse(switchCallbackId, true, {success: true});
       await expect(switchPromise).resolves.toEqual({success: true});

       const closePromise = executeTool('close_tab', {index: 3});
       const [, closeArgs] = send.mock.calls[1] as [string, unknown[]];
       const [closeCallbackId, closeParams] =
           closeArgs as [string, Record<string, unknown>];
       expect(closeParams).toEqual({
         index: 3,
       });
       expect(closeParams).not.toHaveProperty('tab_id');
       expect(closeParams).not.toHaveProperty('__daoAgentExecutionContext');
       cr.webUIResponse(closeCallbackId, true, {success: true});
       await expect(closePromise).resolves.toEqual({success: true});
     });

  it('delegates resource search to one native executor call', async () => {
    const send = vi.fn();
    vi.stubGlobal('chrome', {send});

    const promise = executeTool('search_in_resources', {
      pattern: 'api\\\\/v1',
      flags: 'i',
      types: 'Script,Document',
      max_matches: 7,
    });

    expect(send).toHaveBeenCalledTimes(1);
    const [method, args] = send.mock.calls[0] as [string, unknown[]];
    const [callbackId, params] =
        args as [string, Record<string, unknown>];
    expect(method).toBe('searchInResources');
    expect(params).toEqual({
      pattern: 'api\\\\/v1',
      flags: 'i',
      types: 'Script,Document',
      max_matches: 7,
    });
    cr.webUIResponse(callbackId, true, {
      searched: 3,
      matches: [],
      truncated: false,
    });

    await expect(promise).resolves.toEqual({
      searched: 3,
      matches: [],
      truncated: false,
    });
  });
});
