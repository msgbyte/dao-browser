// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const callLLMStreaming = vi.fn();
const recordApiCall = vi.fn();
const addWebUIListener = vi.fn();
const i18nMocks = vi.hoisted(() => ({
  initialized: false,
  locale: 'zh-CN',
  initI18n: vi.fn(),
  currentLocale: vi.fn(),
}));

vi.mock('../agent_bridge.js', () => ({
  addWebUIListener: (...args: unknown[]) => addWebUIListener(...args),
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
  initI18n: () => i18nMocks.initI18n(),
  currentLocale: () => i18nMocks.currentLocale(),
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
  recap: {
    summary: '下午专注 Rust，晚上阅读浏览器架构。',
    time_buckets: {
      morning_minutes: 22,
      afternoon_minutes: 125,
      evening_minutes: 78,
      night_minutes: 9,
    },
    themes: [{
      name: 'Rust 异步编程',
      summary: '围绕 tokio 与 async runtime 深入阅读。',
      intensity: 'deep',
      time_label: '下午为主',
      attention_share: 100,
    }],
  },
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

describe('daily runner module', () => {
  it('does not register the shared dream listener', () => {
    expect(addWebUIListener).not.toHaveBeenCalled();
  });
});

describe('runDream', () => {
  beforeEach(() => {
    callLLMStreaming.mockReset();
    recordApiCall.mockReset();
    i18nMocks.initialized = false;
    i18nMocks.locale = 'zh-CN';
    i18nMocks.initI18n.mockReset();
    i18nMocks.initI18n.mockImplementation(async () => {
      i18nMocks.initialized = true;
    });
    i18nMocks.currentLocale.mockReset();
    i18nMocks.currentLocale.mockImplementation(
        () => i18nMocks.initialized ? i18nMocks.locale : 'en');
  });

  it('parses a valid response and caps confidence at 0.8', async () => {
    respondWith(VALID);
    const result = await runDream('2026-06-11', {history: []});
    expect(result.report_markdown).toBe('昨晚的报告');
    expect(result.recap.summary).toBe(
        '下午专注 Rust，晚上阅读浏览器架构。');
    expect(result.recap.time_buckets.afternoon_minutes).toBe(125);
    expect(result.recap.themes).toEqual([{
      name: 'Rust 异步编程',
      summary: '围绕 tokio 与 async runtime 深入阅读。',
      intensity: 'deep',
      time_label: '下午为主',
      attention_share: 100,
    }]);
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

  it('injects the resolved locale into the report prompts', async () => {
    i18nMocks.locale = 'fr';
    respondWith(VALID);
    await runDream('2026-06-11', {});

    const messages = callLLMStreaming.mock.calls[0]![0] as Array<{
      role: string;
      content: string;
    }>;
    const systemPrompt = messages[0]!.content;

    expect(messages[0]!.content).toContain('Required output locale: fr');
    expect(messages[0]!.content).not.toContain('zh-CN');
    expect(messages[1]!.content).toContain('Locale: fr');
    expect(systemPrompt).toContain(
        'All user-facing report text, habit values, evidence, and questions');
    expect(systemPrompt).not.toContain(
        'recent questions clearly use another language');
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
       expect(systemPrompt).toContain('stats.foreground_seconds_by_bucket');
       expect(systemPrompt).toContain('visit-count-only buckets');
       expect(systemPrompt).toContain('deep');
       expect(systemPrompt).toContain('light');
       expect(systemPrompt).toContain('Never expose');
       expect(systemPrompt).toContain('raw URLs');
       expect(systemPrompt).toContain('not like an audit log');
     });

  it('does not treat unavailable foreground coverage as measured zero',
     async () => {
       respondWith(VALID);
       await runDream('2026-06-11', {
         stats: {
           foreground_source: 'dao_active_tab_v1',
           foreground_coverage: 'unavailable',
           coverage_seconds: 0,
           foreground_seconds_by_bucket: {
             morning: 0,
             afternoon: 0,
             evening: 0,
             night: 0,
           },
         },
       });

       const messages = callLLMStreaming.mock.calls[0]![0] as Array<{
         role: string;
         content: string;
       }>;
       const systemPrompt = messages[0]!.content;

       expect(systemPrompt).toContain(
           'unavailable means foreground timing is unknown');
       expect(systemPrompt).toContain(
           'do not infer zero foreground time or time-of-day habits');
     });

  it('uses partial foreground values only with their coverage qualifier',
     async () => {
       respondWith(VALID);
       await runDream('2026-06-11', {
         stats: {
           foreground_source: 'dao_active_tab_v1',
           foreground_coverage: 'partial',
           coverage_seconds: 3600,
           foreground_seconds_by_bucket: {morning: 900},
         },
       });

       const messages = callLLMStreaming.mock.calls[0]![0] as Array<{
         role: string;
         content: string;
       }>;
       const systemPrompt = messages[0]!.content;

       expect(systemPrompt).toContain(
           'Native full and partial values are the only measured foreground evidence');
       expect(systemPrompt).toContain(
           'Keep the partial coverage qualifier on every time claim');
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

  it('drops malformed recap themes and clamps numeric recap values',
     async () => {
       respondWith(JSON.stringify({
         report_markdown: 'r',
         recap: {
           summary: 'A focused day.',
           time_buckets: {
             morning_minutes: -3,
             afternoon_minutes: 25.8,
             evening_minutes: 'invalid',
             night_minutes: 5,
           },
           themes: [
             {name: 'Missing summary'},
             {
               name: 'Architecture',
               summary: 'Read rendering documentation.',
               intensity: 'unknown',
               time_label: 'Evening',
               attention_share: 140,
             },
           ],
         },
         habits: [],
         scenario_adjustments: [],
       }));

       const result = await runDream('2026-06-11', {});

       expect(result.recap.time_buckets).toEqual({
         morning_minutes: 0,
         afternoon_minutes: 26,
         evening_minutes: 0,
         night_minutes: 5,
       });
       expect(result.recap.themes).toEqual([{
         name: 'Architecture',
         summary: 'Read rendering documentation.',
         intensity: 'medium',
         time_label: 'Evening',
         attention_share: 100,
       }]);
     });

  it('leaves a missing recap summary empty for the UI fallback', async () => {
    respondWith(JSON.stringify({
      report_markdown: '## Main thread\nBuilt the new Dream report.\n\n' +
          '## Note\nKept the old data path.',
      habits: [],
      scenario_adjustments: [],
    }));

    const result = await runDream('2026-06-11', {});

    expect(result.recap.summary).toBe('');
  });
});
