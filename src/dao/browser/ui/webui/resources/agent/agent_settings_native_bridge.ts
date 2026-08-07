// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  addWebUIListener,
  applyAgentStatsSnapshot,
  callNativeArgs,
  CONFIDENCE_THRESHOLD_MAP,
} from './agent_bridge.js';
import {
  applyAgentSettingsSnapshot,
  initializeAgentSettingsSync,
  type AgentSettingsBridge,
  type AgentSettingsSnapshot,
} from './agent_settings_sync.js';

const nativeBridge: AgentSettingsBridge = {
  async getSnapshot() {
    return await callNativeArgs('getDaoAgentSettings') as AgentSettingsSnapshot;
  },
  async migrateLegacy(values: Record<string, string>) {
    return await callNativeArgs(
        'migrateDaoAgentSettings', values) as AgentSettingsSnapshot;
  },
};

function applyRuntimeSettings(
    snapshot: AgentSettingsSnapshot, applyUsageStats = true): void {
  applyAgentSettingsSnapshot(snapshot, localStorage);
  if (applyUsageStats) {
    applyUsageStatsSnapshot(snapshot.usageStats);
  }
  const values = snapshot.values;
  void callNativeArgs(
      'setMemoryEnabled', values['dao_agent_memory_enabled'] === 'true');
  void callNativeArgs(
      'setDreamEnabled', values['dao_dream_enabled'] === 'true');
  void callNativeArgs(
      'setDreamDebug', values['dao_dream_debug'] === 'true');
  void callNativeArgs(
      'setProactiveEnabled', values['dao_proactive_enabled'] !== 'false');
  const threshold = values['dao_proactive_threshold'] || 'balanced';
  void callNativeArgs(
      'setConfidenceThreshold',
      CONFIDENCE_THRESHOLD_MAP[threshold] ??
          CONFIDENCE_THRESHOLD_MAP['balanced']);
}

let listenersRegistered = false;
let usageStatsGeneration = 0;

function applyUsageStatsSnapshot(stats: AgentSettingsSnapshot['usageStats']):
    void {
  usageStatsGeneration++;
  applyAgentStatsSnapshot(stats);
}

function registerNativeListeners(): void {
  if (listenersRegistered) {
    return;
  }
  listenersRegistered = true;
  addWebUIListener('dao-agent-settings-changed', snapshot => {
    if (snapshot && typeof snapshot === 'object') {
      applyRuntimeSettings(snapshot as AgentSettingsSnapshot);
    }
  });
  addWebUIListener('dao-agent-usage-stats-changed', stats => {
    if (stats && typeof stats === 'object') {
      applyUsageStatsSnapshot(stats as AgentSettingsSnapshot['usageStats']);
    }
  });
}

export async function startAgentSettingsSync(): Promise<void> {
  registerNativeListeners();
  const initialUsageStatsGeneration = usageStatsGeneration;
  const snapshot = await initializeAgentSettingsSync(
      nativeBridge, localStorage);
  // initializeAgentSettingsSync already applied the cache; runtime handlers
  // still need the canonical values so lifecycle-sensitive settings such as
  // proactive suggestions and Dream are activated consistently.
  if (usageStatsGeneration === initialUsageStatsGeneration) {
    applyRuntimeSettings(snapshot);
  } else {
    applyRuntimeSettings(snapshot, false);
  }
}

export async function setCanonicalAgentSetting(
    key: string, value: string|null): Promise<boolean> {
  return await callNativeArgs('setDaoAgentSetting', key, value) as boolean;
}
