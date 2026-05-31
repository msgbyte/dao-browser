// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const piMocks = vi.hoisted(() => ({
  stream: vi.fn(),
  getModel: vi.fn(),
}));

vi.mock('../vendor/pi_runtime_bundle.js', () => ({
  stream: (...args: unknown[]) => piMocks.stream(...args),
  getModel: (...args: unknown[]) => piMocks.getModel(...args),
}));

import {
  callLLMStreamingWithPi,
  type LLMProviderConfig,
} from '../pi_llm_stream.js';
import type {ChatMessage, ToolDefinition} from '../agent_bridge.js';

function toolDef(name: string): ToolDefinition {
  return {
    type: 'function',
    function: {
      name,
      description: `${name} description`,
      parameters: {type: 'object', properties: {}, required: []},
    },
  };
}

async function* events(items: Array<Record<string, unknown>>) {
  for (const item of items) {
    yield item;
  }
}

function callbacks() {
  return {
    onToken: vi.fn(),
    onToolCall: vi.fn(),
    onDone: vi.fn(),
    onError: vi.fn(),
  };
}

describe('callLLMStreamingWithPi', () => {
  beforeEach(() => {
    piMocks.stream.mockReset();
    piMocks.getModel.mockReset();
    localStorage.clear();
  });

  it('converts streamed text, tool calls, and usage into the legacy callbacks',
     async () => {
    piMocks.stream.mockReturnValue(events([
      {type: 'text_delta', delta: 'hel'},
      {type: 'text_delta', delta: 'lo'},
      {
        type: 'toolcall_end',
        toolCall: {
          id: 'tool-1',
          name: 'get_page_info',
          arguments: {includeTitle: true},
        },
      },
      {type: 'done', message: {usage: {input: 7, output: 5}}},
    ]));
    const cb = callbacks();

    await callLLMStreamingWithPi(
        [
          {role: 'system', content: 'system prompt'},
          {role: 'user', content: 'hello'},
        ],
        [toolDef('get_page_info')],
        cb,
        {
          provider: 'openai-compatible',
          apiKey: 'key',
          baseUrl: 'https://proxy.example/',
          model: 'local-model',
        });

    expect(cb.onToken).toHaveBeenNthCalledWith(1, 'hel');
    expect(cb.onToken).toHaveBeenNthCalledWith(2, 'lo');
    expect(cb.onToolCall).toHaveBeenCalledWith({
      id: 'tool-1',
      type: 'function',
      function: {
        name: 'get_page_info',
        arguments: JSON.stringify({includeTitle: true}),
      },
    });
    expect(cb.onDone).toHaveBeenCalledWith(
        'hello',
        [{
          id: 'tool-1',
          type: 'function',
          function: {
            name: 'get_page_info',
            arguments: JSON.stringify({includeTitle: true}),
          },
        }],
        {prompt_tokens: 7, completion_tokens: 5, total_tokens: 12});
    expect(cb.onError).not.toHaveBeenCalled();

    const [model, context, options] = piMocks.stream.mock.calls[0];
    expect(model).toMatchObject({
      id: 'local-model',
      api: 'openai-completions',
      baseUrl: 'https://proxy.example/v1',
    });
    expect(context).toMatchObject({
      systemPrompt: 'system prompt',
      messages: [{role: 'user', content: 'hello'}],
      tools: [{
        name: 'get_page_info',
        description: 'get_page_info description',
      }],
    });
    expect(options).toMatchObject({apiKey: 'key'});
  });

  it('injects native provider web search and strips the local web_search tool',
     async () => {
    piMocks.getModel.mockReturnValue({
      id: 'gpt-4o',
      name: 'gpt-4o',
      api: 'openai-responses',
      provider: 'openai',
      baseUrl: '',
      reasoning: true,
      input: ['text'],
      cost: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
      contextWindow: 128000,
      maxTokens: 4096,
    });
    piMocks.stream.mockReturnValue(events([
      {type: 'done', message: {usage: {input: 1, output: 1}}},
    ]));
    const cb = callbacks();

    await callLLMStreamingWithPi(
        [{role: 'user', content: 'search'}],
        [toolDef('web_search'), toolDef('get_page_info')],
        cb,
        {provider: 'openai', apiKey: 'key', model: 'gpt-4o'});

    const [, context, options] = piMocks.stream.mock.calls[0];
    expect(context.tools).toEqual([
      expect.objectContaining({name: 'get_page_info'}),
      {type: 'web_search_preview'},
    ]);
    expect(options).toMatchObject({
      apiKey: 'key',
      reasoningEffort: 'medium',
    });
  });

  it('maps stream errors to onError without calling onDone', async () => {
    piMocks.stream.mockReturnValue(events([
      {type: 'error', error: {errorMessage: 'rate limited'}},
    ]));
    const cb = callbacks();
    const config: LLMProviderConfig = {
      provider: 'openai-compatible',
      apiKey: 'key',
      model: 'm',
    };

    await callLLMStreamingWithPi(
        [{role: 'user', content: 'hello'}], [], cb, config);

    expect(cb.onError).toHaveBeenCalledWith('API Error', 'rate limited');
    expect(cb.onDone).not.toHaveBeenCalled();
  });
});
