// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

import {
  applyAgentSettingsSnapshot,
  collectLegacyAgentSettings,
  initializeAgentSettingsSync,
  type AgentSettingsBridge,
} from '../agent_settings_sync.js';

const usageStats = {
  apiCalls: 0,
  toolCalls: {},
  promptTokens: 0,
  completionTokens: 0,
  totalTokens: 0,
  estimatedCost: 0,
  lastReset: 1720000000000,
};

describe('agent settings sync', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  it('collects only durable settings owned by the unified settings page', () => {
    localStorage.setItem('dao_agent_active_provider', 'anthropic');
    localStorage.setItem('dao_agent_soul', 'Be concise.');
    localStorage.setItem('dao_disabled_tools', '["execute_script"]');
    localStorage.setItem('dao_agent_stats', '{"apiCalls":9}');
    localStorage.setItem('dao_agent_runtime_session', 'ignored');
    localStorage.setItem('dao_reusable_element_contexts', '[{"id":1}]');

    expect(collectLegacyAgentSettings(localStorage)).toEqual({
      dao_agent_active_provider: 'anthropic',
      dao_agent_soul: 'Be concise.',
      dao_disabled_tools: '["execute_script"]',
      dao_agent_stats: '{"apiCalls":9}',
    });
    expect(collectLegacyAgentSettings(localStorage))
        .not.toHaveProperty('dao_agent_runtime_session');
  });

  it('migrates legacy values once and applies the canonical snapshot', async () => {
    localStorage.setItem('dao_agent_active_provider', 'anthropic');
    localStorage.setItem('dao_agent_soul', 'Legacy soul');
    const bridge: AgentSettingsBridge = {
      getSnapshot: vi.fn().mockResolvedValue({
        migrationVersion: 1,
        values: {},
        usageStats,
      }),
      migrateLegacy: vi.fn().mockResolvedValue({
        migrationVersion: 2,
        values: {
          dao_agent_active_provider: 'anthropic',
          dao_agent_soul: 'Canonical soul',
        },
        usageStats,
      }),
    };

    await initializeAgentSettingsSync(bridge, localStorage);

    expect(bridge.migrateLegacy).toHaveBeenCalledWith({
      dao_agent_active_provider: 'anthropic',
      dao_agent_soul: 'Legacy soul',
    });
    expect(localStorage.getItem('dao_agent_soul')).toBe('Canonical soul');
  });

  it('does not re-migrate a version 2 snapshot', async () => {
    const bridge: AgentSettingsBridge = {
      getSnapshot: vi.fn().mockResolvedValue({
        migrationVersion: 2,
        values: {},
        usageStats,
      }),
      migrateLegacy: vi.fn(),
    };

    await initializeAgentSettingsSync(bridge, localStorage);

    expect(bridge.migrateLegacy).not.toHaveBeenCalled();
  });

  it('removes stale cached settings missing from the canonical snapshot', () => {
    localStorage.setItem('dao_agent_active_provider', 'openai');
    localStorage.setItem('dao_jina_api_key', 'stale-secret');
    localStorage.setItem('unrelated_runtime_state', 'keep');

    applyAgentSettingsSnapshot({
      migrationVersion: 2,
      values: {dao_agent_active_provider: 'google'},
      usageStats,
    }, localStorage);

    expect(localStorage.getItem('dao_agent_active_provider')).toBe('google');
    expect(localStorage.getItem('dao_jina_api_key')).toBeNull();
    expect(localStorage.getItem('unrelated_runtime_state')).toBe('keep');
  });
});
