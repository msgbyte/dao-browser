// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, describe, expect, it, vi} from 'vitest';

import {callNative, cr} from '../agent_bridge.js';

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
});
