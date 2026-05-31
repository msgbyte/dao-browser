// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, describe, expect, it, vi} from 'vitest';

async function loadBridge() {
  vi.resetModules();
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  const bridge = await import('../welcome_bridge.js');
  return {bridge, send};
}

describe('welcome_bridge', () => {
  afterEach(() => {
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
    delete (window as unknown as {cr?: unknown}).cr;
  });

  it('sends markWelcomeShown without extra arguments', async () => {
    const {bridge, send} = await loadBridge();

    bridge.markWelcomeShown();

    expect(send).toHaveBeenCalledWith('markWelcomeShown', []);
  });

  it('exposes WebUI listener callback on window.cr', async () => {
    const {bridge} = await loadBridge();
    const callback = vi.fn();
    bridge.addListener('welcomeEvent', callback);

    (window as unknown as {
      cr: {webUIListenerCallback: (event: string, ...args: unknown[]) => void};
    }).cr.webUIListenerCallback('welcomeEvent', 1, 'two');

    expect(callback).toHaveBeenCalledWith(1, 'two');
  });
});
