// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const mocks = vi.hoisted(() => ({
  executeTool: vi.fn(),
  recordToolCall: vi.fn(),
  registerDaoToolRenderers: vi.fn(),
  disabled: new Set<string>(),
}));

function tool(name: string) {
  return {
    type: 'function',
    function: {
      name,
      description: `${name} description`,
      parameters: {type: 'object', properties: {}, required: []},
    },
  };
}

vi.mock('../agent_bridge.js', () => ({
  executeTool: (...args: unknown[]) => mocks.executeTool(...args),
  recordToolCall: (...args: unknown[]) => mocks.recordToolCall(...args),
  tools: [tool('web_search'), tool('close_tab'), tool('activate_skill')],
}));

vi.mock('../dao_tool_renderer.js', () => ({
  registerDaoToolRenderers:
      (...args: unknown[]) => mocks.registerDaoToolRenderers(...args),
}));

vi.mock('../tool_catalog.js', () => ({
  isToolEnabled: (name: string) => !mocks.disabled.has(name),
}));

import {buildAgentTools} from '../pi_tool_adapter.js';

describe('pi_tool_adapter', () => {
  beforeEach(() => {
    mocks.executeTool.mockReset();
    mocks.recordToolCall.mockReset();
    mocks.registerDaoToolRenderers.mockReset();
    mocks.disabled.clear();
  });

  it('adapts enabled Dao tools and registers renderers for every known tool', () => {
    mocks.disabled.add('close_tab');

    const adapted = buildAgentTools();

    expect(adapted.map(t => t.name)).toEqual(['web_search', 'activate_skill']);
    expect(mocks.registerDaoToolRenderers).toHaveBeenCalledWith(
        ['web_search', 'close_tab', 'activate_skill']);
  });

  it('executes the Dao tool and preserves raw details', async () => {
    mocks.executeTool.mockResolvedValue({ok: true, answer: 42});
    const [adapted] = buildAgentTools();

    const result = await adapted.execute('call-1', {query: 'dao'});

    expect(mocks.executeTool).toHaveBeenCalledWith(
        'web_search', {query: 'dao'});
    expect(mocks.recordToolCall).toHaveBeenCalledWith('web_search');
    expect(result).toEqual({
      content: [{
        type: 'text',
        text: JSON.stringify({ok: true, answer: 42}, null, 2),
      }],
      details: {ok: true, answer: 42},
    });
  });

  it('adapts activate_skill like other enabled Dao tools', async () => {
    mocks.executeTool.mockResolvedValue({
      success: true,
      skill_id: 'summary',
      instructions: '<activated_skill id="summary">body</activated_skill>',
    });
    const adapted = buildAgentTools().find(t => t.name === 'activate_skill');

    expect(adapted).toBeTruthy();
    const result = await adapted.execute(
        'call-1', {skill_id: 'summary', reason: 'Summarize the current page'});

    expect(mocks.executeTool).toHaveBeenCalledWith('activate_skill', {
      skill_id: 'summary',
      reason: 'Summarize the current page',
    });
    expect(mocks.recordToolCall).toHaveBeenCalledWith('activate_skill');
    expect(result.content[0].text).toContain('activated_skill');
  });

  it('records failed tool calls and wraps the error with the tool name', async () => {
    mocks.executeTool.mockRejectedValue(new Error('network down'));
    const [adapted] = buildAgentTools();

    await expect(adapted.execute('call-1', {}))
        .rejects.toThrow('web_search failed: network down');
    expect(mocks.recordToolCall).toHaveBeenCalledWith('web_search');
  });

  it('turns an aborted signal into AbortError before side effects run', async () => {
    const [adapted] = buildAgentTools();
    const controller = new AbortController();
    controller.abort();

    await expect(adapted.execute('call-1', {}, controller.signal))
        .rejects.toMatchObject({name: 'AbortError'});
    expect(mocks.executeTool).not.toHaveBeenCalled();
    expect(mocks.recordToolCall).not.toHaveBeenCalled();
  });
});
