// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {readFileSync} from 'node:fs';

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

const injectedCatalog = vi.hoisted(() => ({
  json: '',
  getString: vi.fn<(key: string) => string>(),
}));

vi.mock('//resources/js/load_time_data.js', () => ({
  loadTimeData: {
    getString: (key: string) => injectedCatalog.getString(key),
  },
}));

import {
  getBrowserToolDefinitions,
  getCatalogEntries,
  initializeBrowserToolCatalog,
  validateBrowserToolCatalog,
} from '../browser_tool_catalog.js';

function loadCatalogResource(): unknown {
  return JSON.parse(readFileSync(
      'src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json',
      'utf8'));
}

describe('browser_tool_catalog', () => {
  beforeEach(() => {
    injectedCatalog.json = JSON.stringify(loadCatalogResource());
    injectedCatalog.getString.mockImplementation((key: string) => {
      expect(key).toBe('browser_tool_catalog_json');
      return injectedCatalog.json;
    });
  });

  afterEach(() => {
    injectedCatalog.getString.mockReset();
  });

  it('retries after invalid injected catalog data is corrected', async () => {
    injectedCatalog.json = '{';

    await expect(initializeBrowserToolCatalog()).rejects.toThrow(
        'browser tool catalog load failed: invalid injected JSON');
    injectedCatalog.json = JSON.stringify(loadCatalogResource());
    await initializeBrowserToolCatalog();

    expect(injectedCatalog.getString).toHaveBeenCalledTimes(2);
    expect(getBrowserToolDefinitions('mcp')).toHaveLength(29);
  });

  it('exposes exactly 29 browser tools to MCP', async () => {
    await initializeBrowserToolCatalog();
    const names = getBrowserToolDefinitions('mcp').map(
        tool => tool.function.name);

    expect(names).toHaveLength(29);
    expect(names).not.toContain('resolve_element_context');
    expect([...names].sort()).toEqual([
      'agent_click',
      'capture_screenshot',
      'clear_console_messages',
      'clear_network_requests',
      'click_by_ref',
      'click_element',
      'close_tab',
      'enable_console_tracking',
      'enable_network_tracking',
      'execute_script',
      'get_accessibility_tree',
      'get_console_messages',
      'get_network_body',
      'get_network_requests',
      'get_page_html',
      'get_page_info',
      'get_resource_content',
      'highlight_element',
      'list_page_resources',
      'list_tabs',
      'move_cursor',
      'open_tab',
      'press_key_chord',
      'scroll_down',
      'scroll_to_element',
      'scroll_up',
      'search_in_resources',
      'switch_tab',
      'type_text',
    ]);
    expect([...new Set(getCatalogEntries('mcp').map(entry => entry.group))]
               .sort())
        .toEqual(['devtools', 'page', 'tabs']);
  });

  it('keeps resolve_element_context available to Dao Agent only', async () => {
    await initializeBrowserToolCatalog();

    expect(getBrowserToolDefinitions('dao_agent').some(
        tool => tool.function.name === 'resolve_element_context')).toBe(true);
    expect(getBrowserToolDefinitions('mcp').some(
        tool => tool.function.name === 'resolve_element_context')).toBe(false);
  });

  it('rejects duplicate browser tool names', () => {
    const catalog = loadCatalogResource() as {tools: unknown[]};
    expect(() => validateBrowserToolCatalog({
      version: 1,
      tools: [...catalog.tools, catalog.tools[0]],
    })).toThrow('duplicate browser tool name');
  });

  it('requires every browser tool to declare execution metadata', () => {
    const entries = validateBrowserToolCatalog(loadCatalogResource()).tools;
    for (const entry of entries) {
      expect(entry.name).toBeTruthy();
      expect(entry.description).toBeTruthy();
      expect(entry.inputSchema.type).toBe('object');
      expect(entry.group).toMatch(/^(devtools|page|tabs)$/);
      expect(entry.clients.length).toBeGreaterThan(0);
      expect(entry.sideEffect).toMatch(/^(read|interaction|destructive)$/);
      expect(entry.timeoutMs).toBeGreaterThan(0);
    }
  });

  it('documents exact frame matching for resource content', () => {
    const entries = validateBrowserToolCatalog(loadCatalogResource()).tools;
    const resource = entries.find(entry => entry.name === 'get_resource_content');
    const frameId =
        resource?.inputSchema.properties?.['frame_id'] as
        {description?: string}|undefined;

    expect(frameId?.description).toContain('omitted');
    expect(frameId?.description).toContain('exact');
    expect(frameId?.description).not.toContain('Defaults to the main frame');
  });
});
