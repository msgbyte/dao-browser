// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Hardcoded catalog of agent tools, grouped by purpose. The source of truth
// for tool definitions themselves still lives in agent_bridge.ts — this
// file only adds category metadata and a persisted enable/disable set so
// the settings UI can present them in groups and users can opt out of
// individual tools or entire groups.
//
// Semantics: we store ONLY the disabled tool names in localStorage. Tools
// absent from the set are enabled. That means newly added tools in future
// releases are enabled by default without a migration.

export interface ToolGroup {
  id: string;
  label: string;
  toolNames: string[];
}

export const TOOL_GROUPS: ToolGroup[] = [
  {
    id: 'page-info',
    label: 'Page Info',
    toolNames: [
      'get_page_info',
      'get_page_html',
      'get_accessibility_tree',
      'capture_screenshot',
    ],
  },
  {
    id: 'page-interact',
    label: 'Page Interaction',
    toolNames: [
      'click_element',
      'agent_click',
      'click_by_ref',
      'move_cursor',
      'highlight_element',
      'scroll_down',
      'scroll_up',
      'scroll_to_element',
      'press_key_chord',
      'type_text',
    ],
  },
  {
    id: 'scripting',
    label: 'Scripting',
    toolNames: ['execute_script'],
  },
  {
    id: 'tabs',
    label: 'Tab Management',
    toolNames: ['list_tabs', 'switch_tab', 'open_tab', 'close_tab'],
  },
  {
    id: 'network',
    label: 'Network',
    toolNames: [
      'enable_network_tracking',
      'get_network_requests',
      'clear_network_requests',
      'get_network_body',
    ],
  },
  {
    id: 'console',
    label: 'Console',
    toolNames: [
      'enable_console_tracking',
      'get_console_messages',
      'clear_console_messages',
    ],
  },
  {
    id: 'resources',
    label: 'Resource Inspection',
    toolNames: [
      'list_page_resources',
      'get_resource_content',
      'search_in_resources',
    ],
  },
  {
    id: 'memory-skills',
    label: 'Memory & Skills',
    toolNames: ['update_soul', 'save_memory', 'save_skill'],
  },
];

const STORAGE_KEY = 'dao_disabled_tools';
const CHANNEL_NAME = 'dao-tool-config';

// Cross-view notification channel. Settings view posts after a change,
// chat view listens to push the new tool list into the live agent.
export const toolConfigChannel = new BroadcastChannel(CHANNEL_NAME);

function readDisabled(): Set<string> {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return new Set();
    const parsed = JSON.parse(raw);
    if (Array.isArray(parsed)) {
      return new Set(parsed.filter((v): v is string => typeof v === 'string'));
    }
  } catch (_) { /* fall through */ }
  return new Set();
}

function writeDisabled(set: Set<string>) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify([...set]));
  } catch (_) { /* storage full / disabled — ignore */ }
  // Notify other views in the same document AND other agent tabs.
  toolConfigChannel.postMessage({type: 'changed'});
  // BroadcastChannel does not echo to the poster, so also dispatch a
  // same-window event for views inside the same document to observe.
  window.dispatchEvent(new Event('dao-tool-config-changed'));
}

export function isToolEnabled(name: string): boolean {
  return !readDisabled().has(name);
}

export function getDisabledTools(): Set<string> {
  return readDisabled();
}

export function setToolEnabled(name: string, enabled: boolean): void {
  const set = readDisabled();
  if (enabled) {
    set.delete(name);
  } else {
    set.add(name);
  }
  writeDisabled(set);
}

export function setGroupEnabled(groupId: string, enabled: boolean): void {
  const group = TOOL_GROUPS.find(g => g.id === groupId);
  if (!group) return;
  const set = readDisabled();
  for (const name of group.toolNames) {
    if (enabled) set.delete(name);
    else set.add(name);
  }
  writeDisabled(set);
}

export type GroupState = 'all' | 'some' | 'none';

export function getGroupState(groupId: string): GroupState {
  const group = TOOL_GROUPS.find(g => g.id === groupId);
  if (!group || group.toolNames.length === 0) return 'none';
  const disabled = readDisabled();
  let enabledCount = 0;
  for (const name of group.toolNames) {
    if (!disabled.has(name)) enabledCount++;
  }
  if (enabledCount === group.toolNames.length) return 'all';
  if (enabledCount === 0) return 'none';
  return 'some';
}

export function countEnabled(groupId: string): {enabled: number; total: number} {
  const group = TOOL_GROUPS.find(g => g.id === groupId);
  if (!group) return {enabled: 0, total: 0};
  const disabled = readDisabled();
  let enabledCount = 0;
  for (const name of group.toolNames) {
    if (!disabled.has(name)) enabledCount++;
  }
  return {enabled: enabledCount, total: group.toolNames.length};
}
