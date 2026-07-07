// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const piMocks = vi.hoisted(() => ({
  stream: vi.fn(),
}));

vi.mock('../vendor/pi_runtime_bundle.js', () => ({
  stream: (...args: unknown[]) => piMocks.stream(...args),
}));

import {
  compactAgentMessages,
  estimateMessagesTokens,
} from '../dao_compact.js';

async function* summaryEvents() {
  yield {
    type: 'text_delta',
    delta: '<summary>Keep the latest verified state.</summary>',
  };
}

describe('compactAgentMessages', () => {
  beforeEach(() => {
    piMocks.stream.mockReset();
    piMocks.stream.mockReturnValue(summaryEvents());
  });

  it('keeps the summarization request inside the model context window',
     async () => {
    const oldBlob = 'old evidence '.repeat(1600);
    const recentBlob = 'recent verified state '.repeat(400);
    const messages = [
      {role: 'user', content: oldBlob, timestamp: 1},
      {
        role: 'assistant',
        content: [{type: 'text', text: oldBlob}],
        api: 'openai-responses',
        provider: 'openai',
        model: 'gpt-4.1',
        usage: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
        stopReason: 'stop',
        timestamp: 2,
      },
      {
        role: 'toolResult',
        toolCallId: 'call-1',
        toolName: 'read_url',
        content: [{type: 'text', text: oldBlob}],
        isError: false,
        timestamp: 3,
      },
      {role: 'user', content: recentBlob, timestamp: 4},
      {
        role: 'assistant',
        content: [{type: 'text', text: recentBlob}],
        api: 'openai-responses',
        provider: 'openai',
        model: 'gpt-4.1',
        usage: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
        stopReason: 'stop',
        timestamp: 5,
      },
      {role: 'user', content: 'keep this tail', timestamp: 6},
    ];
    const agent = {
      state: {
        systemPrompt: 'system',
        model: {
          id: 'gpt-4.1',
          provider: 'openai',
          contextWindow: 4000,
          maxTokens: 1000,
        },
        messages: messages.slice(),
        isStreaming: false,
      },
      getApiKey: () => 'key',
    };

    await compactAgentMessages(agent, {keepTailUserTurns: 1});

    const [, context] = piMocks.stream.mock.calls[0];
    expect(estimateMessagesTokens(context.messages))
        .toBeLessThan(agent.state.model.contextWindow);
    expect(context.messages).toHaveLength(1);
    expect(context.messages[0].role).toBe('user');
    expect(context.messages[0].content).toContain('[content truncated');
    expect(agent.state.messages).toHaveLength(2);
    expect(agent.state.messages[0].content)
        .toContain('[Dao compacted summary]');
    expect(agent.state.messages[1].content).toBe('keep this tail');
  });

  it('estimates attachment text on user-with-attachments messages', () => {
    const messages = [{
      role: 'user-with-attachments',
      content: 'prompt',
      attachments: [{
        fileName: 'page.md',
        extractedText: 'captured page text '.repeat(40),
      }],
      timestamp: 1,
    }];

    expect(estimateMessagesTokens(
        messages as unknown as Parameters<typeof estimateMessagesTokens>[0]))
        .toBeGreaterThan(100);
  });

  it('compacts the latest turn when keeping it would stay over budget',
     async () => {
    const oversizedToolResult = 'tool output '.repeat(4000);
    const messages = [
      {role: 'user', content: 'research this project', timestamp: 1},
      {
        role: 'toolResult',
        toolCallId: 'call-1',
        toolName: 'read_url',
        content: [{type: 'text', text: oversizedToolResult}],
        isError: false,
        timestamp: 2,
      },
    ];
    const agent = {
      state: {
        systemPrompt: 'system',
        model: {
          id: 'gpt-4.1',
          provider: 'openai',
          contextWindow: 4000,
          maxTokens: 1000,
        },
        messages: messages.slice(),
        isStreaming: false,
      },
      getApiKey: () => 'key',
    };

    const result = await compactAgentMessages(agent, {keepTailUserTurns: 1});

    expect(result.collapsedCount).toBe(2);
    expect(result.keptCount).toBe(0);
    expect(agent.state.messages).toHaveLength(1);
    expect(agent.state.messages[0].content)
        .toContain('[Dao compacted summary]');
  });

  it('refuses to overwrite history changed during summarization', async () => {
    const messages = [
      {role: 'user', content: 'a first prompt '.repeat(200), timestamp: 1},
      {
        role: 'assistant',
        content: [{type: 'text', text: 'an answer '.repeat(200)}],
        api: 'openai-responses',
        provider: 'openai',
        model: 'gpt-4.1',
        usage: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
        stopReason: 'stop',
        timestamp: 2,
      },
      {role: 'user', content: 'second prompt', timestamp: 3},
    ];
    const agent = {
      state: {
        systemPrompt: 'system',
        model: {id: 'gpt-4.1', provider: 'openai', contextWindow: 4000},
        messages: messages.slice(),
        isStreaming: false,
      },
      getApiKey: () => 'key',
    };
    // Simulate a new turn arriving mid-summarization: the stream mock swaps in
    // a fresh messages array (pi-agent-core reassigns by reference on send).
    const concurrent = [...agent.state.messages, {role: 'user', content: 'x'}];
    piMocks.stream.mockImplementation(() => {
      agent.state.messages = concurrent;
      return summaryEvents();
    });

    await expect(compactAgentMessages(agent, {keepTailUserTurns: 1}))
        .rejects.toThrow(/changed during compaction/);
    // The concurrent turn's messages are preserved, not clobbered.
    expect(agent.state.messages).toBe(concurrent);
  });

  it('keeps tool-call ids in the flattened transcript', async () => {
    let captured = '';
    piMocks.stream.mockImplementation((_model: unknown, ctx: any) => {
      captured = ctx.messages[0].content;
      return summaryEvents();
    });
    const messages = [
      {role: 'user', content: 'do it '.repeat(400), timestamp: 1},
      {
        role: 'assistant',
        content: [
          {type: 'text', text: 'calling'},
          {
            type: 'toolCall',
            id: 'call-xyz',
            name: 'read_url',
            arguments: {url: 'a.ts'},
          },
        ],
        api: 'openai-responses',
        provider: 'openai',
        model: 'gpt-4.1',
        usage: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
        stopReason: 'tool_use',
        timestamp: 2,
      },
      {
        role: 'toolResult',
        toolCallId: 'call-xyz',
        toolName: 'read_url',
        content: [{type: 'text', text: 'contents'}],
        isError: false,
        timestamp: 3,
      },
      {role: 'user', content: 'keep', timestamp: 4},
    ];
    const agent = {
      state: {
        systemPrompt: 'system',
        model: {id: 'gpt-4.1', provider: 'openai', contextWindow: 8000},
        messages: messages.slice(),
        isStreaming: false,
      },
      getApiKey: () => 'key',
    };

    await compactAgentMessages(agent, {keepTailUserTurns: 1});

    expect(captured).toContain('id=call-xyz');
  });

  it('ignores local auto-compact notices when estimating tokens', () => {
    const messages = [{
      role: 'assistant',
      content: [{type: 'text', text: 'local notice '.repeat(1000)}],
      api: 'openai-responses',
      provider: 'openai',
      model: 'gpt-4.1',
      usage: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
      stopReason: 'stop',
      timestamp: 1,
      dao: {autoCompactNotice: true},
    }];

    expect(estimateMessagesTokens(
        messages as unknown as Parameters<typeof estimateMessagesTokens>[0]))
        .toBe(0);
  });
});
