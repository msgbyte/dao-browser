// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {AgentStats} from './agent_bridge.js';

export interface AgentSettingsSnapshot {
  migrationVersion: number;
  values: Record<string, string>;
  usageStats: AgentStats;
}

export interface AgentSettingsBridge {
  getSnapshot(): Promise<AgentSettingsSnapshot>;
  migrateLegacy(
      values: Record<string, string>): Promise<AgentSettingsSnapshot>;
}

export const AGENT_SETTINGS_MIGRATION_VERSION = 2;

const LEGACY_AGENT_STATISTICS_KEY = 'dao_agent_stats';

// These keys are durable user choices. Runtime state such as sessions,
// dismissed cards, and circuit-breaker timestamps deliberately stays owned by
// dao://agent and is never copied into browser settings.
export const MANAGED_AGENT_SETTING_KEYS = [
  'dao_agent_providers',
  'dao_agent_active_provider',
  'dao_agent_api_key',
  'dao_agent_base_url',
  'dao_agent_model',
  'dao_agent_soul',
  'dao_disabled_tools',
  'dao_tool_call_show_details',
  'dao_agent_debug_mode',
  'dao_resume_last_session',
  'dao_resume_stale_hours',
  'dao_proactive_enabled',
  'dao_page_context_enabled',
  'dao_conversation_enabled',
  'dao_proactive_threshold',
  'dao_search_source',
  'dao_jina_api_key',
] as const;

export function collectLegacyAgentSettings(
    storage: Storage): Record<string, string> {
  const values: Record<string, string> = {};
  for (const key of MANAGED_AGENT_SETTING_KEYS) {
    const value = storage.getItem(key);
    if (value !== null) {
      values[key] = value;
    }
  }
  const stats = storage.getItem(LEGACY_AGENT_STATISTICS_KEY);
  if (stats !== null) {
    values[LEGACY_AGENT_STATISTICS_KEY] = stats;
  }
  return values;
}

export function applyAgentSettingsSnapshot(
    snapshot: AgentSettingsSnapshot, storage: Storage): void {
  for (const key of MANAGED_AGENT_SETTING_KEYS) {
    const value = snapshot.values[key];
    if (typeof value === 'string') {
      storage.setItem(key, value);
    } else {
      storage.removeItem(key);
    }
  }

  // Existing consumers already listen for these same-document events. Keep
  // them as the compatibility boundary while native Profile prefs become the
  // source of truth.
  window.dispatchEvent(new Event('llm-config-changed'));
  window.dispatchEvent(new Event('dao-tool-config-changed'));
  window.dispatchEvent(new CustomEvent('dao-agent-debug-mode-changed', {
    detail: {enabled: storage.getItem('dao_agent_debug_mode') === 'true'},
  }));
  window.dispatchEvent(new CustomEvent('dao-proactive-enabled-changed', {
    detail: {enabled: storage.getItem('dao_proactive_enabled') !== 'false'},
  }));
}

export async function initializeAgentSettingsSync(
    bridge: AgentSettingsBridge,
    storage: Storage): Promise<AgentSettingsSnapshot> {
  let snapshot = await bridge.getSnapshot();
  if (snapshot.migrationVersion < AGENT_SETTINGS_MIGRATION_VERSION) {
    snapshot = await bridge.migrateLegacy(
        collectLegacyAgentSettings(storage));
  }
  applyAgentSettingsSnapshot(snapshot, storage);
  return snapshot;
}
