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
  getAgentToolDefinitions: () =>
      [tool('web_search'), tool('close_tab'), tool('activate_skill')],
}));

vi.mock('../dao_tool_renderer.js', () => ({
  registerDaoToolRenderers:
      (...args: unknown[]) => mocks.registerDaoToolRenderers(...args),
}));

vi.mock('../tool_catalog.js', () => ({
  isToolEnabled: (name: string) => !mocks.disabled.has(name),
}));

vi.mock('../browser_tool_catalog.js', () => ({
  getCatalogEntries: () => [{name: 'close_tab'}],
}));

import {buildAgentTools, createBrowserToolExecutionHooks} from '../pi_tool_adapter.js';

describe('pi_tool_adapter', () => {
  it('serializes browser calls without changing independent tool execution', () => {
    const adapted = buildAgentTools();
    expect(adapted.find(t => t.name === 'close_tab').executionMode)
        .toBe('sequential');
    expect(adapted.find(t => t.name === 'web_search').executionMode)
        .toBeUndefined();
  });

  it('skips browser actions after execution or validation errors in the same batch', () => {
    const hooks = createBrowserToolExecutionHooks();
    const first = {type: 'toolCall', name: 'web_search'};
    const second = {type: 'toolCall', name: 'close_tab'};
    const assistantMessage = {content: [first, second]};
    const context = {assistantMessage, toolCall: second};

    // Schema failures bypass both hooks in pi, so an unfinished call is unsafe.
    expect(hooks.beforeToolCall(context)?.block).toBe(true);
    hooks.afterToolCall({assistantMessage, toolCall: first, isError: true});
    expect(hooks.beforeToolCall(context)?.block).toBe(true);
    hooks.afterToolCall({assistantMessage, toolCall: first, isError: false});
    expect(hooks.beforeToolCall(context)).toBeUndefined();
    expect(hooks.beforeToolCall({
      assistantMessage: {content: [second]}, toolCall: second,
    })).toBeUndefined();
  });

  it.each([
    'success', 'execution error', 'bare error', 'coded error', 'invalid arguments',
  ])(
      'runs the bundled Agent with ordered browser calls: %s', async mode => {
        const {Agent, getModel} = await import('../vendor/pi_runtime_bundle.js');
        const executed: string[] = [];
        mocks.executeTool.mockImplementation(async (_name, args) => {
          executed.push(args.step + ':start');
          await Promise.resolve();
          if (mode === 'execution error') throw new Error('target changed');
          if (mode === 'bare error') {
            return {error: 'No reusable element context selected.'};
          }
          if (mode === 'coded error') {
            return {
              error: 'Another automation client is active.',
              code: 'AGENT_CONTROL_BUSY',
              retryable: true,
            };
          }
          executed.push(args.step + ':end');
          return {success: true};
        });
        let response = 0;
        const agent = new Agent({
          initialState: {
            model: getModel('openai', 'gpt-4.1-mini'),
            tools: buildAgentTools(),
          },
          ...createBrowserToolExecutionHooks(),
          streamFn: () => {
            const first = response++ === 0;
            const message = {
              role: 'assistant', api: 'openai-responses', provider: 'openai',
              model: 'gpt-4.1-mini', timestamp: 1,
              usage: {input: 0, output: 0, totalTokens: 0},
              stopReason: first ? 'toolUse' : 'stop',
              content: first ? [
                {type: 'toolCall', id: 'first', name: 'close_tab',
                 arguments: mode === 'invalid arguments' ? [] : {step: 'first'}},
                {type: 'toolCall', id: 'second', name: 'close_tab',
                 arguments: {step: 'second'}},
              ] : [{type: 'text', text: 'Done'}],
            };
            return {
              async *[Symbol.asyncIterator]() { yield {type: 'done'}; },
              result: async () => message,
            };
          },
        });
        await agent.prompt('Run the browser actions.', []);
        expect(executed).toEqual(mode === 'success' ?
            ['first:start', 'first:end', 'second:start', 'second:end'] :
            mode === 'invalid arguments' ? [] : ['first:start']);
        const results = agent.state.messages.filter((m: {role: string}) => m.role === 'toolResult');
        expect(results).toHaveLength(2);
        expect(results[0].isError).toBe(mode !== 'success');
        expect(results[1].isError).toBe(mode !== 'success');
        if (mode === 'bare error') {
          expect(results[0].content[0].text)
              .toContain('No reusable element context selected.');
        }
        if (mode !== 'success') {
          expect(results[1].content[0].text).toContain('Skipped browser action');
        }
      });

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

  it.each([
    {ok: true, answer: 42},
    {result: '{"error":"Page response data"}'},
  ])('executes the Dao tool and preserves raw details: %j', async raw => {
    mocks.executeTool.mockResolvedValue(raw);
    const [adapted] = buildAgentTools();

    const result = await adapted.execute('call-1', {query: 'dao'});

    expect(mocks.executeTool).toHaveBeenCalledWith(
        'web_search', {query: 'dao'});
    expect(mocks.recordToolCall).toHaveBeenCalledWith('web_search');
    expect(result).toEqual({
      content: [{
        type: 'text',
        text: JSON.stringify(raw, null, 2),
      }],
      details: raw,
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

  it('includes a native tool error code in the model-visible message', async () => {
    mocks.executeTool.mockResolvedValue({
      error: 'Another automation client is active.',
      code: 'AGENT_CONTROL_BUSY',
      retryable: true,
    });
    const [adapted] = buildAgentTools();

    await expect(adapted.execute('call-1', {})).rejects.toMatchObject({
      message:
          'Another automation client is active. [code: AGENT_CONTROL_BUSY]',
      code: 'AGENT_CONTROL_BUSY',
      retryable: true,
    });
    expect(mocks.recordToolCall).toHaveBeenCalledWith('web_search');
  });

  it('preserves screenshot media as an image content part without base64 text',
     async () => {
       mocks.executeTool.mockResolvedValue({
         screenshot_taken: true,
         message: 'Screenshot captured successfully.',
         data: 'jpeg-base64',
         format: 'jpeg',
         media: {mimeType: 'image/jpeg', data: 'jpeg-base64'},
       });
       const adapted =
           buildAgentTools().find(t => t.name === 'web_search');

       const result = await adapted.execute('call-1', {});

       expect(result.content).toEqual([
         {
           type: 'text',
           text: JSON.stringify({
           screenshot_taken: true,
           message: 'Screenshot captured successfully.',
           format: 'jpeg',
         }, null, 2),
         },
         {type: 'image', mimeType: 'image/jpeg', data: 'jpeg-base64'},
       ]);
       expect(result.details).toEqual({
         screenshot_taken: true,
         message: 'Screenshot captured successfully.',
         format: 'jpeg',
       });
       expect(JSON.stringify(result.details)).not.toContain('jpeg-base64');
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

  it('threads an active AbortSignal into execution and preserves AbortError',
     async () => {
       mocks.executeTool.mockRejectedValue(
           Object.assign(new Error('aborted'), {name: 'AbortError'}));
       const [adapted] = buildAgentTools();
       const controller = new AbortController();

       await expect(adapted.execute('call-1', {}, controller.signal))
           .rejects.toMatchObject({name: 'AbortError'});
       expect(mocks.executeTool).toHaveBeenCalledWith(
           'web_search', {}, {signal: controller.signal});
       expect(mocks.recordToolCall).toHaveBeenCalledWith('web_search');
     });
});
