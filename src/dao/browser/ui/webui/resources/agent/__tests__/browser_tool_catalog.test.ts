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
    expect(getBrowserToolDefinitions('mcp')).toHaveLength(34);
  });

  it('exposes exactly 34 browser tools to MCP', async () => {
    await initializeBrowserToolCatalog();
    const names = getBrowserToolDefinitions('mcp').map(
        tool => tool.function.name);

    expect(names).toHaveLength(34);
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
      'fill_by_ref',
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
      'query_elements',
      'run_browser_task',
      'scroll_down',
      'scroll_to_element',
      'scroll_up',
      'search_in_resources',
      'switch_tab',
      'type_text',
      'wait_for_element',
      'wait_for_network_response',
    ]);
    expect([...new Set(getCatalogEntries('mcp').map(entry => entry.group))]
               .sort())
        .toEqual(['devtools', 'page', 'tabs']);
  });

  it('shares the bounded Jev task with Agent and MCP', () => {
    const task = validateBrowserToolCatalog(loadCatalogResource()).tools.find(
        entry => entry.name === 'run_browser_task');
    expect(task?.clients).toEqual(['dao_agent', 'mcp']);
    expect(task?.timeoutMs).toBe(65000);
    expect(task?.inputSchema.required).toEqual(['goal', 'completion']);
    expect(task?.inputSchema.additionalProperties).toBe(false);
  });

  it('exposes the safe query-click-wait workflow', () => {
    const entries = validateBrowserToolCatalog(loadCatalogResource()).tools;
    const query = entries.find(entry => entry.name === 'query_elements');
    const click = entries.find(entry => entry.name === 'click_by_ref');
    const fill = entries.find(entry => entry.name === 'fill_by_ref');
    const elementWait = entries.find(entry => entry.name === 'wait_for_element');
    const enableNetwork =
        entries.find(entry => entry.name === 'enable_network_tracking');
    const wait =
        entries.find(entry => entry.name === 'wait_for_network_response');

    expect(query?.inputSchema.properties).toHaveProperty('scope');
    expect(query?.inputSchema.properties).toHaveProperty('require_count');
    expect(query?.inputSchema.properties.scope.properties).toEqual(
        expect.objectContaining({
          ref_id: expect.any(Object),
          document_id: expect.any(Object),
          snapshot_id: expect.any(Object),
        }));
    expect(query?.inputSchema.properties.scope.required).toEqual([]);
    expect(click?.inputSchema.required).toEqual(
        expect.arrayContaining(['ref_id', 'document_id', 'snapshot_id']));
    expect(click?.inputSchema.properties).toHaveProperty('preconditions');
    expect(click?.inputSchema.properties.preconditions.required).toEqual([]);
    for (const tool of [click, fill]) {
      expect(tool?.inputSchema.properties.preconditions.properties).toMatchObject({
        name: {type: 'string'}, value: {type: 'string'}, href: {type: 'string'},
        checked: {type: 'boolean'}, in_viewport: {type: 'boolean'},
        sensitive: {type: 'boolean'},
      });
    }
    expect(fill?.inputSchema.required).toEqual(
        expect.arrayContaining(['ref_id', 'document_id', 'snapshot_id', 'text']));
    expect(elementWait?.inputSchema.properties).toHaveProperty('enabled');
    expect(elementWait?.inputSchema.properties.timeout_ms)
        .toMatchObject({maximum: 30000});
    expect(enableNetwork?.description).toContain('cursor');
    expect(wait?.inputSchema.properties).toHaveProperty('cursor');
    expect(wait?.inputSchema.properties).toHaveProperty('json_path');
    expect(wait?.inputSchema.properties).toHaveProperty('any_of');
    expect(wait?.inputSchema.properties).toHaveProperty('select');
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

  it('keeps page interaction distinct from browser tab switching', () => {
    const entries = validateBrowserToolCatalog(loadCatalogResource()).tools;
    const accessibilityTree =
        entries.find(entry => entry.name === 'get_accessibility_tree');
    const switchTab = entries.find(entry => entry.name === 'switch_tab');

    expect(accessibilityTree?.description)
        .toContain('Use this before interacting with page elements');
    expect(switchTab?.description)
        .toContain('not for page-local tabs, links, buttons, or menu items');
  });
});
