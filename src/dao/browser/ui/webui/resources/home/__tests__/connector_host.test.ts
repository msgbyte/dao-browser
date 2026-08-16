// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const bridge = vi.hoisted(() => ({
  startHomeConnector: vi.fn(),
  startHomeDraftConnector: vi.fn(),
  callHomeConnectorPage: vi.fn(),
  finishHomeConnector: vi.fn(),
}));

vi.mock('../home_bridge.js', () => bridge);

import {
  COMPLETED_COLLECTION_TTL_MS,
  ConnectorHost,
} from '../connector_host.js';

describe('ConnectorHost', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    bridge.startHomeConnector.mockResolvedValue({
      execution_id: 'execution-1',
      revision: 'revision-1',
      module: 'export default {}',
      input: {page: 1},
    });
  });

  it('coalesces identical connector requests for the active session',
     async () => {
       const contentWindow = {postMessage: vi.fn()} as unknown as Window;
       const frame = {contentWindow} as HTMLIFrameElement;
       const host = new ConnectorHost(frame, 'revision-1');

       const first = host.collect('feed', {page: 1});
       const second = host.collect('feed', {page: 1});
       await vi.waitFor(() => {
         expect(bridge.startHomeConnector).toHaveBeenCalledTimes(1);
       });
       expect(contentWindow.postMessage).toHaveBeenCalledWith(
           expect.objectContaining({executionId: 'execution-1'}), '*');
       bridge.finishHomeConnector.mockResolvedValue({
         result: [{title: 'Fresh'}],
       });

       const handled = await host.handleMessage({
         source: contentWindow,
         origin: 'null',
         data: {
           daoHomeConnector: 1,
           type: 'complete',
           executionId: 'execution-1',
           result: [{title: 'Fresh'}],
         },
       } as unknown as MessageEvent);
       expect(handled).toBe(true);
       await expect(Promise.all([first, second])).resolves.toEqual([
         [{title: 'Fresh'}], [{title: 'Fresh'}],
       ]);

       await expect(host.collect('feed', {page: 1})).resolves.toEqual(
           [{title: 'Fresh'}]);
       expect(bridge.startHomeConnector).toHaveBeenCalledTimes(1);
     });

  it('refreshes a completed connector result after ten minutes', async () => {
    vi.useFakeTimers();
    try {
      const contentWindow = {postMessage: vi.fn()} as unknown as Window;
      const frame = {contentWindow} as HTMLIFrameElement;
      const host = new ConnectorHost(frame, 'revision-1');
      bridge.finishHomeConnector.mockImplementation(
          (_executionId: string, result: unknown) =>
              Promise.resolve({result}));

      const first = host.collect('feed', {});
      await vi.advanceTimersByTimeAsync(0);
      await host.handleMessage({
        source: contentWindow,
        origin: 'null',
        data: {
          daoHomeConnector: 1,
          type: 'complete',
          executionId: 'execution-1',
          result: [{title: 'Fresh'}],
        },
      } as unknown as MessageEvent);
      await expect(first).resolves.toEqual([{title: 'Fresh'}]);

      await vi.advanceTimersByTimeAsync(COMPLETED_COLLECTION_TTL_MS - 1);
      await expect(host.collect('feed', {})).resolves.toEqual(
          [{title: 'Fresh'}]);
      expect(bridge.startHomeConnector).toHaveBeenCalledTimes(1);

      bridge.startHomeConnector.mockResolvedValue({
        execution_id: 'execution-2',
        revision: 'revision-1',
        module: 'export default {}',
        input: {},
      });
      await vi.advanceTimersByTimeAsync(1);
      const refreshed = host.collect('feed', {});
      await vi.advanceTimersByTimeAsync(0);
      expect(bridge.startHomeConnector).toHaveBeenCalledTimes(2);
      await host.handleMessage({
        source: contentWindow,
        origin: 'null',
        data: {
          daoHomeConnector: 1,
          type: 'complete',
          executionId: 'execution-2',
          result: [{title: 'New'}],
        },
      } as unknown as MessageEvent);
      await expect(refreshed).resolves.toEqual([{title: 'New'}]);
    } finally {
      vi.useRealTimers();
    }
  });

  it('evicts completed results when native media executors roll over',
     async () => {
       const contentWindow = {postMessage: vi.fn()} as unknown as Window;
       const frame = {contentWindow} as HTMLIFrameElement;
       const host = new ConnectorHost(frame, 'revision-1');
       bridge.startHomeConnector.mockImplementation(
           (_revision: string, _connectorId: string, input: {page: number}) =>
               Promise.resolve({
                 execution_id: `execution-${input.page}`,
                 revision: 'revision-1',
                 module: 'export default {}',
                 input,
               }));
       bridge.finishHomeConnector.mockImplementation(
           (_executionId: string, result: unknown) =>
               Promise.resolve({result}));

       for (let page = 0; page < 17; ++page) {
         const collection = host.collect('feed', {page});
         await vi.waitFor(() => {
           expect(bridge.startHomeConnector).toHaveBeenCalledTimes(page + 1);
         });
         await host.handleMessage({
           source: contentWindow,
           origin: 'null',
           data: {
             daoHomeConnector: 1,
             type: 'complete',
             executionId: `execution-${page}`,
             result: {page},
           },
         } as unknown as MessageEvent);
         await expect(collection).resolves.toEqual({page});
       }

       const replay = host.collect('feed', {page: 0});
       await vi.waitFor(() => {
         expect(bridge.startHomeConnector).toHaveBeenCalledTimes(18);
       });
       await host.handleMessage({
         source: contentWindow,
         origin: 'null',
         data: {
           daoHomeConnector: 1,
           type: 'complete',
           executionId: 'execution-0',
           result: {page: 0},
         },
       } as unknown as MessageEvent);
       await expect(replay).resolves.toEqual({page: 0});
     });

  it('starts an approved draft connector without publishing it', async () => {
    const contentWindow = {postMessage: vi.fn()} as unknown as Window;
    const frame = {contentWindow} as HTMLIFrameElement;
    const host = new ConnectorHost(frame, 'revision-1');
    bridge.startHomeDraftConnector.mockResolvedValue({
      execution_id: 'draft-execution-1',
      revision: 'revision-1',
      module: 'export default {}',
      input: {page: 2},
    });

    const collection = host.collectDraft('draft-1', 'feed', {page: 2});

    await vi.waitFor(() => {
      expect(bridge.startHomeDraftConnector).toHaveBeenCalledWith(
          'draft-1', 'feed', {page: 2});
    });
    expect(bridge.startHomeConnector).not.toHaveBeenCalled();
    bridge.finishHomeConnector.mockResolvedValue({result: {ok: true}});
    await host.handleMessage({
      source: contentWindow,
      origin: 'null',
      data: {
        daoHomeConnector: 1,
        type: 'complete',
        executionId: 'draft-execution-1',
        result: {ok: true},
      },
    } as unknown as MessageEvent);
    await expect(collection).resolves.toEqual({ok: true});
  });

  it('keeps the sandbox error when native cleanup also fails', async () => {
    const contentWindow = {postMessage: vi.fn()} as unknown as Window;
    const frame = {contentWindow} as HTMLIFrameElement;
    const host = new ConnectorHost(frame, 'revision-1');
    const collection = host.collect('feed', {page: 1});
    await vi.waitFor(() => {
      expect(bridge.startHomeConnector).toHaveBeenCalledTimes(1);
    });
    bridge.finishHomeConnector.mockRejectedValue(new Error('cleanup failed'));

    await expect(host.handleMessage({
      source: contentWindow,
      origin: 'null',
      data: {
        daoHomeConnector: 1,
        type: 'complete',
        executionId: 'execution-1',
        error: 'connector failed',
      },
    } as unknown as MessageEvent)).resolves.toBe(true);
    await expect(collection).rejects.toThrow('connector failed');
  });

  it('times out a sandbox that never completes', async () => {
    vi.useFakeTimers();
    try {
      const contentWindow = {postMessage: vi.fn()} as unknown as Window;
      const frame = {contentWindow} as HTMLIFrameElement;
      const host = new ConnectorHost(frame, 'revision-1');
      bridge.finishHomeConnector.mockResolvedValue({});
      const collection = host.collect('feed', {page: 1});
      const assertion = expect(collection).rejects.toMatchObject({
        code: 'timed_out',
      });
      await vi.advanceTimersByTimeAsync(21_000);

      await assertion;
      expect(bridge.finishHomeConnector).toHaveBeenCalledWith(
          'execution-1', null);
    } finally {
      vi.useRealTimers();
    }
  });
});
