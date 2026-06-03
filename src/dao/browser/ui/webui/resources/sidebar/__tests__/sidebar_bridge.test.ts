// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

async function loadBridge() {
  vi.resetModules();
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  const bridge = await import('../sidebar_bridge.js');
  return {bridge, send};
}

describe('sidebar_bridge', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
    delete (window as unknown as {cr?: unknown}).cr;
  });

  it('parses valid tab drag data and rejects malformed payloads', async () => {
    const {bridge} = await loadBridge();

    expect(bridge.parseTabDragData('dao-tab-drag:12:3')).toEqual({
      sessionId: 12,
      tabIndex: 3,
    });
    expect(bridge.parseTabDragData('dao-tab-drag:abc:3')).toBeNull();
    expect(bridge.parseTabDragData('dao-tab-drag:12:x')).toBeNull();
    expect(bridge.parseTabDragData('other:12:3')).toBeNull();
  });

  it('only treats drag points outside the sidebar viewport as leaving', async () => {
    const {bridge} = await loadBridge();

    expect(bridge.isPointOutsideViewport(12, 24, 240, 800)).toBe(false);
    expect(bridge.isPointOutsideViewport(0, 0, 240, 800)).toBe(false);
    expect(bridge.isPointOutsideViewport(240, 800, 240, 800)).toBe(false);
    expect(bridge.isPointOutsideViewport(-1, 24, 240, 800)).toBe(true);
    expect(bridge.isPointOutsideViewport(12, -1, 240, 800)).toBe(true);
    expect(bridge.isPointOutsideViewport(241, 24, 240, 800)).toBe(true);
    expect(bridge.isPointOutsideViewport(12, 801, 240, 800)).toBe(true);
  });

  it('dispatches and removes WebUI listeners by event id', async () => {
    const {bridge} = await loadBridge();
    const first = vi.fn();
    const second = vi.fn();

    const firstHandle = bridge.addListener('sidebarStateChanged', first);
    bridge.addListener('sidebarStateChanged', second);

    (window as unknown as {
      cr: {webUIListenerCallback: (event: string, ...args: unknown[]) => void};
    }).cr.webUIListenerCallback('sidebarStateChanged', 'payload');
    expect(first).toHaveBeenCalledWith('payload');
    expect(second).toHaveBeenCalledWith('payload');

    expect(bridge.removeListener(firstHandle)).toBe(true);
    (window as unknown as {
      cr: {webUIListenerCallback: (event: string, ...args: unknown[]) => void};
    }).cr.webUIListenerCallback('sidebarStateChanged', 'next');
    expect(first).toHaveBeenCalledTimes(1);
    expect(second).toHaveBeenLastCalledWith('next');
  });

  it('resolves sendNativeAsync when native replies to the callback id', async () => {
    const {bridge, send} = await loadBridge();

    const promise = bridge.sendNativeAsync<string>('loadFolders', 'arg');
    expect(send).toHaveBeenCalledWith('loadFolders', ['loadFolders_0', 'arg']);

    (window as unknown as {
      cr: {webUIListenerCallback: (event: string, ...args: unknown[]) => void};
    }).cr.webUIListenerCallback('loadFolders_0', '{"version":1}');

    await expect(promise).resolves.toBe('{"version":1}');
  });

  it('debounces saveFolders so rapid updates only send the latest JSON', async () => {
    const {bridge, send} = await loadBridge();

    bridge.saveFolders('first');
    bridge.saveFolders('second');
    vi.advanceTimersByTime(299);
    expect(send).not.toHaveBeenCalled();

    vi.advanceTimersByTime(1);
    expect(send).toHaveBeenCalledTimes(1);
    expect(send).toHaveBeenCalledWith('saveFolders', ['second']);
  });
});
