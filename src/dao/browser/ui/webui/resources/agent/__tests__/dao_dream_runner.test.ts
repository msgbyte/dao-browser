// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const callLLMStreaming = vi.fn();
const recordApiCall = vi.fn();

vi.mock('../agent_bridge.js', () => ({
  addWebUIListener: vi.fn(),
  recordApiCall: (...args: unknown[]) => recordApiCall(...args),
}));
vi.mock('../llm_cost.js', () => ({
  getCostRatesForConfig: () => ({
    input: 2,
    output: 6,
    cacheRead: 0,
    cacheWrite: 0,
  }),
}));
vi.mock('../pi_llm_stream.js', () => ({
  callLLMStreamingWithPi:
      (...args: unknown[]) => callLLMStreaming(...args),
}));
vi.mock('../llm_config.js', () => ({
  getActiveLLMConfig: () => ({
    provider: 'openai',
    apiKey: 'sk-test',
    baseUrl: '',
    model: 'gpt-test',
  }),
}));
vi.mock('../i18n/i18n.js', () => ({
  currentLocale: () => 'zh-CN',
}));

import {extractJson, runDream} from '../dao_dream_runner.js';

function respondWith(content: string, usage?: {
  prompt_tokens: number;
  completion_tokens: number;
  total_tokens: number;
}) {
  callLLMStreaming.mockImplementationOnce(
      (_msgs: unknown, _tools: unknown,
       callbacks: {onDone: (c: string, tools: unknown[], usage?: unknown) => void}) => {
        callbacks.onDone(content, [], usage);
        return Promise.resolve();
      });
}

const VALID = JSON.stringify({
  report_markdown: '昨晚的报告',
  habits: [{
    key: 'interest.rust',
    value: '你最近在关注 Rust 异步编程。',
    confidence: 0.95,  // should be capped to 0.8
    evidence: '你多次浏览 docs.rs 相关页面。',
    relation: 'new',
  }],
  scenario_adjustments: [{scenario_id: 's1', suggestion: 'lower_confidence'}],
});

describe('extractJson', () => {
  it('passes plain JSON through', () => {
    expect(extractJson('{"a":1}')).toBe('{"a":1}');
  });
  it('strips ```json fences', () => {
    expect(extractJson('```json\n{"a":1}\n```')).toBe('{"a":1}');
  });
});

describe('runDream', () => {
  beforeEach(() => {
    callLLMStreaming.mockReset();
    recordApiCall.mockReset();
  });

  it('parses a valid response and caps confidence at 0.8', async () => {
    respondWith(VALID);
    const result = await runDream('2026-06-11', {history: []});
    expect(result.report_markdown).toBe('昨晚的报告');
    expect(result.habits).toHaveLength(1);
    expect(result.habits[0]!.confidence).toBe(0.8);
    expect(result.scenario_adjustments).toHaveLength(1);
  });

  it('sends an empty tool list (no browser tool catalog)', async () => {
    respondWith(VALID);
    await runDream('2026-06-11', {});
    expect(callLLMStreaming).toHaveBeenCalledTimes(1);
    const toolsArg = callLLMStreaming.mock.calls[0]![1];
    expect(toolsArg).toEqual([]);
  });

  it('records token usage from completed dream LLM calls', async () => {
    respondWith(VALID, {
      prompt_tokens: 11,
      completion_tokens: 7,
      total_tokens: 18,
    });

    await runDream('2026-06-11', {});

    expect(recordApiCall).toHaveBeenCalledWith(11, 7, 2, 6);
  });

  it('asks the model to keep user-facing text in the current locale', async () => {
    respondWith(VALID);
    await runDream('2026-06-11', {});

    const messages = callLLMStreaming.mock.calls[0]![0] as Array<{
      role: string;
      content: string;
    }>;
    const systemPrompt = messages[0]!.content;

    expect(systemPrompt).toContain(
        'All user-facing report text, habit values, evidence, and questions');
    expect(systemPrompt).toContain('For zh-CN, use Simplified Chinese');
    expect(systemPrompt).not.toContain('Habit keys and values in English');
  });

  it('asks the model to summarize long history without exposing raw details',
     async () => {
       respondWith(VALID);
       await runDream('2026-06-11', {});

       const messages = callLLMStreaming.mock.calls[0]![0] as Array<{
         role: string;
         content: string;
       }>;
       const systemPrompt = messages[0]!.content;

       expect(systemPrompt).toContain(
           'summarize by topic, intent, and time pattern');
       expect(systemPrompt).toContain('Use foreground_seconds');
       expect(systemPrompt).toContain('deep');
       expect(systemPrompt).toContain('light');
       expect(systemPrompt).toContain('Never expose');
       expect(systemPrompt).toContain('raw URLs');
       expect(systemPrompt).toContain('not like an audit log');
     });

  it('asks the model to treat material preferences as existing memory',
     async () => {
       respondWith(VALID);
       await runDream('2026-06-11', {});

       const messages = callLLMStreaming.mock.calls[0]![0] as Array<{
         role: string;
         content: string;
       }>;
       const systemPrompt = messages[0]!.content;

       expect(systemPrompt).toContain('material.preferences');
       expect(systemPrompt).toContain('existing memory');
       expect(systemPrompt).toContain('reinforce');
       expect(systemPrompt).toContain('contradict');
     });

  it('logs sanitized request info when debug mode is enabled', async () => {
    const info = vi.spyOn(console, 'info').mockImplementation(() => {});
    respondWith(VALID);

    await runDream('2026-06-11', {history: [{domain: 'example.com'}]}, {
      debug: true,
    });

    expect(info).toHaveBeenCalledWith(
        'Dao Dream request',
        expect.objectContaining({
          dreamDate: '2026-06-11',
          provider: 'openai',
          model: 'gpt-test',
          baseUrl: '',
          apiKeyConfigured: true,
          tools: [],
        }));
    expect(JSON.stringify(info.mock.calls)).not.toContain('sk-test');
  });

  it('retries once on invalid JSON then succeeds', async () => {
    respondWith('sorry, here is the JSON: not-json');
    respondWith(VALID);
    const result = await runDream('2026-06-11', {});
    expect(result.habits).toHaveLength(1);
    expect(callLLMStreaming).toHaveBeenCalledTimes(2);
  });

  it('fails after two invalid responses', async () => {
    respondWith('garbage');
    respondWith('still garbage');
    await expect(runDream('2026-06-11', {}))
        .rejects.toThrow(/invalid JSON after retry/);
  });

  it('drops malformed habit entries instead of failing', async () => {
    respondWith(JSON.stringify({
      report_markdown: 'r',
      habits: [{key: 'k'}, {key: 'k2', value: 'v2'}],
    }));
    const result = await runDream('2026-06-11', {});
    expect(result.habits).toHaveLength(1);
    expect(result.habits[0]!.key).toBe('k2');
  });
});
