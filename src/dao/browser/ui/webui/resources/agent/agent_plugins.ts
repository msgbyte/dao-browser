// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export interface AgentPlugin {
  id: string; name: string; tool: string; revision?: string;
  enabledKey: string; urlKey: string; tokenKey: string; permissionKey: string;
}
let plugins: AgentPlugin[] = [];
let values: Record<string, string> = {};

export function syncAgentPlugins(next: AgentPlugin[], settings: Record<string, string>) {
  plugins = next;
  values = settings;
}

export function getAgentPlugins(): AgentPlugin[] { return plugins; }
export function isPluginTool(name: string): boolean {
  // Keep removed plugins fail-closed in tool lists captured by old sessions.
  return name === 'run_browser_task';
}
// Mirrors IsDaoAgentPluginAuthorized() in dao_agent_plugins.h.
export function isPluginEnabled(name: string): boolean {
  const plugin = plugins.find(p => p.tool === name);
  if (!plugin || values[plugin.enabledKey] !== 'true' ||
      values[plugin.permissionKey] !== 'true') return false;
  const token = values[plugin.tokenKey] ?? '';
  if (!token.trim() || /[\0\r\n]/.test(token)) return false;
  try {
    const url = new URL(values[plugin.urlKey] ?? '');
    // href keeps an empty fragment's '#', which url.hash drops.
    return ['http:', 'https:'].includes(url.protocol) && !url.username && !url.password &&
        !url.href.includes('#');
  } catch { return false; }
}
