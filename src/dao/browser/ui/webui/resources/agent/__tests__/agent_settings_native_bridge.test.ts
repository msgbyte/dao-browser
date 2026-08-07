// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const mocks = vi.hoisted(() => ({
  listeners: new Map<string, Array<(value: unknown) => void>>(),
  addWebUIListener: vi.fn(),
  applyAgentSettingsSnapshot: vi.fn(),
  applyAgentStatsSnapshot: vi.fn(),
  callNativeArgs: vi.fn(),
  initializeAgentSettingsSync: vi.fn(),
}));

vi.mock('../agent_bridge.js', () => ({
  addWebUIListener: (event: string, callback: (value: unknown) => void) => {
    mocks.addWebUIListener(event, callback);
    const callbacks = mocks.listeners.get(event) || [];
    callbacks.push(callback);
    mocks.listeners.set(event, callbacks);
  },
  applyAgentStatsSnapshot: (stats: unknown) =>
      mocks.applyAgentStatsSnapshot(stats),
  callNativeArgs: (...args: unknown[]) => mocks.callNativeArgs(...args),
  CONFIDENCE_THRESHOLD_MAP: {balanced: 0.5},
}));

vi.mock('../agent_settings_sync.js', () => ({
  applyAgentSettingsSnapshot: (...args: unknown[]) =>
      mocks.applyAgentSettingsSnapshot(...args),
  initializeAgentSettingsSync: (...args: unknown[]) =>
      mocks.initializeAgentSettingsSync(...args),
}));

const oldStats = {
  apiCalls: 5,
  toolCalls: {web_search: 2},
  promptTokens: 20,
  completionTokens: 10,
  totalTokens: 30,
  estimatedCost: 0.0001,
  lastReset: 100,
};

const resetStats = {
  apiCalls: 0,
  toolCalls: {},
  promptTokens: 0,
  completionTokens: 0,
  totalTokens: 0,
  estimatedCost: 0,
  lastReset: 200,
};

const oldSnapshot = {
  migrationVersion: 2,
  values: {},
  usageStats: oldStats,
};

describe('agent settings native bridge', () => {
  let startAgentSettingsSync: () => Promise<void>;

  beforeEach(async () => {
    vi.resetModules();
    mocks.listeners.clear();
    mocks.addWebUIListener.mockReset();
    mocks.applyAgentSettingsSnapshot.mockReset();
    mocks.applyAgentStatsSnapshot.mockReset();
    mocks.callNativeArgs.mockReset();
    mocks.initializeAgentSettingsSync.mockReset();
    mocks.callNativeArgs.mockImplementation((method: string) =>
        Promise.resolve(method === 'getDaoAgentSettings' ? oldSnapshot : true));
    mocks.initializeAgentSettingsSync.mockImplementation(
        async (bridge: {getSnapshot: () => Promise<typeof oldSnapshot>}) =>
            await bridge.getSnapshot());
    ({startAgentSettingsSync} = await import('../agent_settings_native_bridge.js'));
  });

  it('keeps a usage event that arrives before the initial snapshot', async () => {
    let resolveSnapshot: (snapshot: typeof oldSnapshot) => void;
    const pendingSnapshot = new Promise<typeof oldSnapshot>(resolve => {
      resolveSnapshot = resolve;
    });
    mocks.callNativeArgs.mockImplementation((method: string) =>
        method === 'getDaoAgentSettings' ? pendingSnapshot : Promise.resolve(true));

    const starting = startAgentSettingsSync();
    const usageListener = mocks.listeners.get('dao-agent-usage-stats-changed')![0]!;
    usageListener(resetStats);
    resolveSnapshot!(oldSnapshot);
    await starting;

    expect(mocks.applyAgentStatsSnapshot).toHaveBeenLastCalledWith(resetStats);
  });

  it('registers each native listener only once', async () => {
    await startAgentSettingsSync();
    await startAgentSettingsSync();

    expect(mocks.addWebUIListener).toHaveBeenCalledTimes(2);
    expect(mocks.listeners.get('dao-agent-settings-changed')).toHaveLength(1);
    expect(mocks.listeners.get('dao-agent-usage-stats-changed')).toHaveLength(1);
  });
});
