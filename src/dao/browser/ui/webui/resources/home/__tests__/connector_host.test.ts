// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

const bridge = vi.hoisted(() => ({
  startHomeConnector: vi.fn(),
  startHomeDraftConnector: vi.fn(),
  callHomeConnectorPage: vi.fn(),
  cancelHomeConnector: vi.fn(),
  finishHomeConnector: vi.fn(),
}));
vi.mock('../home_bridge.js', () => bridge);

import {ConnectorHost} from '../connector_host.js';

describe('ConnectorHost', () => {
  let host: ConnectorHost;
  let frames: HTMLIFrameElement[];

  beforeEach(() => {
    vi.resetAllMocks();
    const template = document.createElement('iframe');
    template.setAttribute('sandbox', 'allow-scripts');
    document.body.append(template);
    frames = [];
    const cloneNode = template.cloneNode.bind(template);
    vi.spyOn(template, 'cloneNode').mockImplementation(() => {
      const frame = cloneNode(false) as HTMLIFrameElement;
      frames.push(frame);
      return frame;
    });
    bridge.startHomeConnector.mockImplementation(async (_revision, id, input, mode) =>
      mode === 'cache-only' ? {cache_miss: true} : {execution_id: id, module: 'export default {}', input});
    bridge.finishHomeConnector.mockImplementation(async (_id, result) => ({result}));
    host = new ConnectorHost(template, 'revision');
  });

  afterEach(() => {
    host.disconnect();
    document.body.replaceChildren();
    vi.useRealTimers();
  });

  function message(frame: HTMLIFrameElement, executionId: string,
                   data: Record<string, unknown>) {
    return host.handleMessage(new MessageEvent('message', {
      source: frame.contentWindow, origin: 'null',
      data: {daoHomeConnector: 1, executionId, ...data},
    }));
  }

  it('starts eight sources, coalesces duplicates and frees each slot independently', async () => {
    const ids = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'];
    const requests = ids.map(id => host.collect(id, {}));
    const duplicate = host.collect('c', {});
    await vi.waitFor(() => expect(frames).toHaveLength(8));
    expect(bridge.startHomeConnector.mock.calls.filter(call => call[3] === true)).toHaveLength(8);
    const failure = expect(requests[1]).rejects.toMatchObject({code: 'auth_required'});
    bridge.callHomeConnectorPage.mockResolvedValueOnce({error: 'Sign in', code: 'auth_required'});
    await message(frames[1]!, 'b', {type: 'page_call', callId: 1, operation: 'exists', args: ['main']});
    await message(frames[1]!, 'b', {type: 'complete', error: 'Sign in', code: 'auth_required'});
    await failure;
    await vi.waitFor(() => expect(frames).toHaveLength(9));
    await message(frames[2]!, 'c', {type: 'complete', result: 2});
    await expect(duplicate).resolves.toBe(2);
    await vi.waitFor(() => expect(frames).toHaveLength(10));
    for (const i of [9, 8, 7, 6, 5, 4, 3, 0]) {
      await message(frames[i]!, ids[i]!, {type: 'complete', result: i});
      await expect(requests[i]).resolves.toBe(i);
    }
    expect(bridge.cancelHomeConnector).toHaveBeenCalledExactlyOnceWith('b');
  });

  it('routes interleaved replies by frame and execution even with identical call IDs', async () => {
    const a = host.collect('a', {});
    const b = host.collect('b', {});
    await vi.waitFor(() => expect(frames).toHaveLength(2));
    const [first, second] = frames as [HTMLIFrameElement, HTMLIFrameElement];
    const replyA = vi.spyOn(first.contentWindow!, 'postMessage');
    const replyB = vi.spyOn(second.contentWindow!, 'postMessage');
    const call = {type: 'page_call', callId: 1, operation: 'exists', args: ['main']};
    expect(await message(first, 'b', call)).toBe(false);
    bridge.callHomeConnectorPage.mockImplementation(async id => id);
    await Promise.all([message(second, 'b', call), message(first, 'a', call)]);
    expect(replyA).toHaveBeenCalledWith(expect.objectContaining({result: 'a', callId: 1}), '*');
    expect(replyB).toHaveBeenCalledWith(expect.objectContaining({result: 'b', callId: 1}), '*');
    await message(first, 'a', {type: 'complete', result: 'a'});
    await message(second, 'b', {type: 'complete', result: 'b'});
    await expect(Promise.all([a, b])).resolves.toEqual(['a', 'b']);
  });

  it('keeps connector-supplied strings out of external error codes', async () => {
    const result = expect(host.collect('a', {})).rejects.not.toHaveProperty('code');
    await vi.waitFor(() => expect(frames).toHaveLength(1));
    await message(frames[0]!, 'a', {type: 'complete', error: 'failed', code: 'private fixture'});
    await result;
  });

  it.each(['auth_required', 'session_changed'])('preserves the native %s when a connector wraps a page error', async code => {
    const stale = vi.fn();
    bridge.startHomeConnector.mockResolvedValueOnce({cached: 'old account', stale: true});
    const result = expect(host.collect('feed', {}, false, stale)).rejects.toMatchObject({code});
    await vi.waitFor(() => expect(frames).toHaveLength(1));
    expect(stale).toHaveBeenCalledWith('old account');
    bridge.callHomeConnectorPage.mockResolvedValueOnce({error: 'Native session ended', code});
    await message(frames[0]!, 'feed', {type: 'page_call', callId: 1, operation: 'exists', args: ['main']});
    await message(frames[0]!, 'feed', {type: 'complete', error: 'Could not read feed'});
    await result;
    expect(bridge.finishHomeConnector).not.toHaveBeenCalled();
  });

  it('returns native cache hits without a sandbox and forwards explicit refreshes', async () => {
    bridge.startHomeConnector.mockResolvedValueOnce({cached: null});
    bridge.startHomeDraftConnector.mockResolvedValueOnce({cached: []});
    await expect(host.collect('a', {})).resolves.toBeNull();
    await expect(host.collectDraft('draft', 'a', {})).resolves.toEqual([]);
    expect(frames).toHaveLength(0);
    const fresh = host.collect('a', {}, true);
    await vi.waitFor(() => expect(frames).toHaveLength(1));
    expect(bridge.startHomeConnector).toHaveBeenLastCalledWith('revision', 'a', {}, true);
    await message(frames[0]!, 'a', {type: 'complete', result: 'new'});
    await expect(fresh).resolves.toBe('new');
    bridge.startHomeDraftConnector.mockResolvedValueOnce({error: 'Denied', code: 'permission_required'});
    await expect(host.collectDraft('draft', 'a', {}, true)).rejects.toMatchObject({code: 'permission_required'});
    expect(bridge.startHomeDraftConnector).toHaveBeenLastCalledWith('draft', 'a', {}, true);
  });

  it('serves a later cache reader immediately while the same refresh is in flight', async () => {
    bridge.startHomeConnector.mockImplementation(async (_revision, id, input, mode) =>
      mode === true ? {execution_id: id, module: 'export default {}', input} :
                      {cached: [], stale: true});
    const first = vi.fn();
    const second = vi.fn();
    const read = host.collect('a', {}, false, first);
    await vi.waitFor(() => expect(frames).toHaveLength(1));
    expect(first).toHaveBeenCalledExactlyOnceWith([]);
    const later = host.collect('a', {}, false, second);
    await vi.waitFor(() => expect(second).toHaveBeenCalledExactlyOnceWith([]));
    expect(frames).toHaveLength(1);
    await message(frames[0]!, 'a', {type: 'complete', result: ['new']});
    await expect(Promise.all([read, later])).resolves.toEqual([['new'], ['new']]);
  });

  it.each([false, true])('retries a cold read once when its site data is cleared (cleared again: %s)', async changesAgain => {
    const changed = {error: 'Session changed', code: 'session_changed'};
    bridge.startHomeConnector.mockResolvedValueOnce({cache_miss: true})
        .mockResolvedValueOnce(changed)
        .mockResolvedValueOnce(changesAgain ? changed : {execution_id: 'a', module: 'export default {}'});
    const read = host.collect('a', {});
    if (changesAgain) {
      await expect(read).rejects.toMatchObject({code: 'session_changed'});
    } else {
      const result = expect(read).resolves.toBe('new session');
      await vi.waitFor(() => expect(frames).toHaveLength(1));
      await message(frames[0]!, 'a', {type: 'complete', result: 'new session'});
      await result;
    }
    expect(bridge.startHomeConnector).toHaveBeenCalledTimes(3);
  });

  it('reports an ended session after a stale result even when both sessions show the same count', async () => {
    bridge.startHomeConnector.mockResolvedValueOnce({cached: 0, stale: true});
    bridge.finishHomeConnector.mockResolvedValueOnce({
      error: 'Changed', code: 'session_changed',
    });
    const read = expect(host.collect('feed', {}, false, vi.fn())).rejects.toMatchObject({code: 'session_changed'});
    await vi.waitFor(() => expect(frames).toHaveLength(1));
    await message(frames[0]!, 'feed', {type: 'complete', result: 0});
    expect(bridge.startHomeConnector).toHaveBeenCalledTimes(2);
    await read;
  });

  it('does not reopen a manual read after its session ended', async () => {
    bridge.finishHomeConnector.mockResolvedValueOnce({
      error: 'Changed', code: 'session_changed',
    });
    const read = expect(host.collect('feed', {}, true)).rejects.toMatchObject({code: 'session_changed'});
    await vi.waitFor(() => expect(frames).toHaveLength(1));
    await message(frames[0]!, 'feed', {type: 'complete', result: 'before'});
    await read;
    expect(bridge.startHomeConnector).toHaveBeenCalledTimes(1);
  });

  it('starts a new read when the in-flight read belongs to a previous session', async () => {
    const probe = Promise.withResolvers<unknown>();
    bridge.startHomeConnector.mockImplementation(async (_revision, id, input, mode) =>
      mode === 'cache-only' ? probe.promise : {execution_id: id, module: 'export default {}', input});
    const later = host.collect('a', {});
    const original = host.collect('a', {}, true);
    await vi.waitFor(() => expect(frames).toHaveLength(1));
    probe.resolve({cache_miss: true, previous_session: true});
    bridge.finishHomeConnector.mockResolvedValueOnce({error: 'Session changed', code: 'session_changed'});
    const rejected = expect(original).rejects.toMatchObject({code: 'session_changed'});
    await message(frames[0]!, 'a', {type: 'complete', result: 'old'});
    await rejected;
    await vi.waitFor(() => expect(frames).toHaveLength(2));
    await message(frames[1]!, 'a', {type: 'complete', result: 'new'});
    await expect(later).resolves.toBe('new');
  });

  it('times out a stalled sandbox and removes its frame', async () => {
    vi.useFakeTimers();
    const rejected = expect(host.collect('a', {})).rejects.toMatchObject({code: 'timed_out'});
    await vi.advanceTimersByTimeAsync(21_000);
    await rejected;
    expect(bridge.cancelHomeConnector).toHaveBeenCalledExactlyOnceWith('a');
    expect(frames[0]!.isConnected).toBe(false);
  });

  it('shares a live read with a manual refresh of the same input', async () => {
    const first = host.collect('a', {});
    await vi.waitFor(() => expect(frames).toHaveLength(1));
    const fresh = host.collect('a', {}, true);
    await message(frames[0]!, 'a', {type: 'complete', result: 'updated'});
    await expect(Promise.all([first, fresh])).resolves.toEqual(['updated', 'updated']);
    expect(frames).toHaveLength(1);
  });

  it('rejects pending starts immediately and cancels a late native reply', async () => {
    const start = Promise.withResolvers<unknown>();
    bridge.startHomeConnector.mockReturnValue(start.promise);
    const result = expect(host.collect('a', {})).rejects.toThrow('disconnected');
    host.disconnect();
    await result;
    start.resolve({execution_id: 'late', module: 'export default {}'});
    await vi.waitFor(() => expect(bridge.cancelHomeConnector).toHaveBeenCalledWith('late'));
    expect(frames).toHaveLength(0);
  });

  it('cancels active work and rejects queued work on disconnect', async () => {
    const requests = Array.from({length: 10}, (_, i) => host.collect(String(i), {}));
    const results = Promise.allSettled(requests);
    await vi.waitFor(() => expect(frames).toHaveLength(8));
    host.disconnect();
    expect((await results).every(result => result.status === 'rejected')).toBe(true);
    expect(bridge.startHomeConnector.mock.calls.filter(call => call[3] === true)).toHaveLength(8);
    expect(frames.every(frame => !frame.isConnected)).toBe(true);
  });
});
