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

import {
  runWeeklyDream,
  validateWeeklyDreamResult,
} from '../dao_weekly_dream_runner.js';

interface Usage {
  prompt_tokens: number;
  completion_tokens: number;
  total_tokens: number;
}

function respondWith(content: string, usage?: Usage) {
  callLLMStreaming.mockImplementationOnce(
      (_messages: unknown, _tools: unknown,
       callbacks: {
         onDone: (content: string, tools: unknown[], usage?: Usage) => void;
       }) => {
        callbacks.onDone(content, [], usage);
        return Promise.resolve();
      });
}

function validOutput(): Record<string, unknown> {
  return {
    schema_version: 1,
    headline: '继续推进迁移主线',
    primary_thread: {
      title: '迁移方案',
      status_summary: '已经比较了两种可行方案。',
      next_step: '先验证风险较低的方案。',
      confidence: 0.8,
      source_refs: ['page_1', 'conversation_1'],
    },
    secondary_threads: [{
      title: '测试覆盖',
      status_summary: '关键路径已有基础测试。',
      next_step: '补充失败路径。',
      confidence: 0.6,
      source_refs: ['conversation_1'],
    }],
    retained_outcomes: [{
      text: '决定先保留兼容层。',
      confidence: 0.75,
      source_refs: ['conversation_1'],
    }],
    footprint_summary: {
      themes: ['迁移', '测试'],
      time_pattern: '主要工作集中在下午。',
    },
  };
}

const MATERIAL = {
  history: [{
    domain: 'docs.example',
    pages: [{ref_id: 'page_1', title: 'Migration guide'}],
  }],
  conversations: [{
    ref_id: 'conversation_1',
    summary: 'Compared two migration approaches.',
  }],
};

describe('validateWeeklyDreamResult', () => {
  it('strictly reconstructs a valid result and discards unknown data', () => {
    const parsed = validOutput();
    parsed['ignored'] = 'discard me';
    const primary = parsed['primary_thread'] as Record<string, unknown>;
    primary['ignored'] = 'discard me too';
    primary['source_refs'] = ['page_1', 'fabricated', 'page_1'];

    const result = validateWeeklyDreamResult(
        parsed, ['page_1', 'conversation_1']);

    expect(result).toEqual({
      schema_version: 1,
      headline: '继续推进迁移主线',
      primary_thread: {
        title: '迁移方案',
        status_summary: '已经比较了两种可行方案。',
        next_step: '先验证风险较低的方案。',
        confidence: 0.8,
        source_refs: ['page_1'],
      },
      secondary_threads: [{
        title: '测试覆盖',
        status_summary: '关键路径已有基础测试。',
        next_step: '补充失败路径。',
        confidence: 0.6,
        source_refs: ['conversation_1'],
      }],
      retained_outcomes: [{
        text: '决定先保留兼容层。',
        confidence: 0.75,
        source_refs: ['conversation_1'],
      }],
      footprint_summary: {
        themes: ['迁移', '测试'],
        time_pattern: '主要工作集中在下午。',
      },
    });
    expect(result).not.toHaveProperty('ignored');
    expect(result!.primary_thread).not.toHaveProperty('ignored');
  });

  it('clamps confidence and caps every bounded collection', () => {
    const parsed = validOutput();
    const primary = parsed['primary_thread'] as Record<string, unknown>;
    primary['confidence'] = 8;
    parsed['secondary_threads'] = Array.from({length: 4}, (_, index) => ({
      title: `Thread ${index}`,
      status_summary: 'Status',
      next_step: 'Next',
      confidence: index === 0 ? -2 : 0.5,
      source_refs: ['page_1'],
    }));
    parsed['retained_outcomes'] = Array.from({length: 5}, (_, index) => ({
      text: `Outcome ${index}`,
      confidence: index === 0 ? 3 : 0.5,
      source_refs: ['conversation_1'],
    }));
    parsed['footprint_summary'] = {
      themes: Array.from({length: 7}, (_, index) => `Theme ${index}`),
      time_pattern: 'Afternoons',
    };

    const result = validateWeeklyDreamResult(
        parsed, ['page_1', 'conversation_1']);

    expect(result!.primary_thread.confidence).toBe(1);
    expect(result!.secondary_threads).toHaveLength(2);
    expect(result!.secondary_threads[0]!.confidence).toBe(0);
    expect(result!.retained_outcomes).toHaveLength(3);
    expect(result!.retained_outcomes[0]!.confidence).toBe(1);
    expect(result!.footprint_summary.themes).toEqual([
      'Theme 0', 'Theme 1', 'Theme 2', 'Theme 3', 'Theme 4',
    ]);
  });

  it('accepts text at every exact character limit', () => {
    const parsed = validOutput();
    parsed['headline'] = 'h'.repeat(120);
    const primary = parsed['primary_thread'] as Record<string, unknown>;
    primary['title'] = 't'.repeat(120);
    primary['status_summary'] = 's'.repeat(320);
    primary['next_step'] = 'n'.repeat(240);
    parsed['retained_outcomes'] = [{
      text: 'o'.repeat(240),
      confidence: 0.5,
      source_refs: ['conversation_1'],
    }];
    parsed['footprint_summary'] = {
      themes: ['x'.repeat(80)],
      time_pattern: 'p'.repeat(320),
    };

    expect(validateWeeklyDreamResult(
        parsed, ['page_1', 'conversation_1'])).not.toBeNull();
  });

  it.each([
    ['headline', (value: Record<string, unknown>) => {
      value['headline'] = 'h'.repeat(121);
    }],
    ['thread title', (value: Record<string, unknown>) => {
      (value['primary_thread'] as Record<string, unknown>)['title'] =
          't'.repeat(121);
    }],
    ['thread status summary', (value: Record<string, unknown>) => {
      (value['primary_thread'] as Record<string, unknown>)['status_summary'] =
          's'.repeat(321);
    }],
    ['thread next step', (value: Record<string, unknown>) => {
      (value['primary_thread'] as Record<string, unknown>)['next_step'] =
          'n'.repeat(241);
    }],
    ['outcome', (value: Record<string, unknown>) => {
      (value['retained_outcomes'] as Array<Record<string, unknown>>)[0]![
          'text'] = 'o'.repeat(241);
    }],
    ['theme', (value: Record<string, unknown>) => {
      const footprint = value['footprint_summary'] as Record<string, unknown>;
      footprint['themes'] = ['x'.repeat(81)];
    }],
    ['time pattern', (value: Record<string, unknown>) => {
      const footprint = value['footprint_summary'] as Record<string, unknown>;
      footprint['time_pattern'] = 'p'.repeat(321);
    }],
  ])('rejects an overlong %s', (_name, mutate) => {
    const parsed = validOutput();
    mutate(parsed);
    expect(validateWeeklyDreamResult(
        parsed, ['page_1', 'conversation_1'])).toBeNull();
  });

  it.each([
    ['schema version', (value: Record<string, unknown>) => {
      value['schema_version'] = 2;
    }],
    ['headline', (value: Record<string, unknown>) => {
      value['headline'] = 1;
    }],
    ['primary thread', (value: Record<string, unknown>) => {
      value['primary_thread'] = 'not an object';
    }],
    ['primary title', (value: Record<string, unknown>) => {
      (value['primary_thread'] as Record<string, unknown>)['title'] = null;
    }],
    ['primary confidence', (value: Record<string, unknown>) => {
      (value['primary_thread'] as Record<string, unknown>)['confidence'] =
          'high';
    }],
    ['primary refs', (value: Record<string, unknown>) => {
      (value['primary_thread'] as Record<string, unknown>)['source_refs'] =
          'page_1';
    }],
    ['secondary threads', (value: Record<string, unknown>) => {
      value['secondary_threads'] = {};
    }],
    ['retained outcomes', (value: Record<string, unknown>) => {
      value['retained_outcomes'] = null;
    }],
    ['footprint', (value: Record<string, unknown>) => {
      value['footprint_summary'] = [];
    }],
    ['themes', (value: Record<string, unknown>) => {
      (value['footprint_summary'] as Record<string, unknown>)['themes'] =
          'migration';
    }],
    ['time pattern', (value: Record<string, unknown>) => {
      (value['footprint_summary'] as Record<string, unknown>)['time_pattern'] =
          false;
    }],
  ])('rejects a malformed required %s', (_name, mutate) => {
    const parsed = validOutput();
    mutate(parsed);
    expect(validateWeeklyDreamResult(
        parsed, ['page_1', 'conversation_1'])).toBeNull();
  });

  it.each([
    ['URL', 'See https://example.com/private'],
    ['www address', 'See www.example.com/private'],
    ['HTML tag', 'Continue with <strong>this</strong>'],
    ['HTML comment', 'Continue <!-- hidden --> now'],
  ])('rejects required text containing a %s', (_name, unsafeText) => {
    const parsed = validOutput();
    (parsed['primary_thread'] as Record<string, unknown>)['next_step'] =
        unsafeText;
    expect(validateWeeklyDreamResult(
        parsed, ['page_1', 'conversation_1'])).toBeNull();
  });

  it('treats an otherwise valid result without a trusted primary ref as sparse',
     () => {
       const parsed = validOutput();
       (parsed['primary_thread'] as Record<string, unknown>)['source_refs'] =
           ['fabricated'];

       expect(validateWeeklyDreamResult(
           parsed, ['page_1', 'conversation_1'])).toBeNull();
     });
});

describe('runWeeklyDream', () => {
  beforeEach(() => {
    callLLMStreaming.mockReset();
    recordApiCall.mockReset();
  });

  it('uses locale and the locked safety instructions with an empty tool list',
     async () => {
       respondWith(JSON.stringify(validOutput()));

       await runWeeklyDream(
           {start: '2026-07-06', end: '2026-07-13'}, MATERIAL);

       expect(callLLMStreaming).toHaveBeenCalledTimes(1);
       expect(callLLMStreaming.mock.calls[0]![1]).toEqual([]);
       const messages = callLLMStreaming.mock.calls[0]![0] as Array<{
         role: string;
         content: string;
       }>;
       const systemPrompt = messages[0]!.content.replace(/\s+/g, ' ');
       expect(systemPrompt).toContain('untrusted evidence');
       expect(systemPrompt).toContain(
           'Browsing or search activity alone cannot prove completion');
       expect(systemPrompt).toContain(
           'Fallback questions cannot prove outcomes');
       expect(systemPrompt).toContain('Only supplied source refs');
       expect(systemPrompt).toContain('no URLs');
       expect(systemPrompt).toContain('no HTML');
       expect(systemPrompt).toContain('no tool calls');
       expect(systemPrompt).toContain(
           'no prebuilt Agent instruction');
       expect(messages[1]!.content).toContain('Locale: zh-CN');
       expect(messages[1]!.content).toContain(
           'Period: 2026-07-06 to 2026-07-13');
     });

  it('records usage separately for the initial and repair calls', async () => {
    respondWith('not json', {
      prompt_tokens: 11,
      completion_tokens: 3,
      total_tokens: 14,
    });
    respondWith(JSON.stringify(validOutput()), {
      prompt_tokens: 17,
      completion_tokens: 7,
      total_tokens: 24,
    });

    await runWeeklyDream(
        {start: '2026-07-06', end: '2026-07-13'}, MATERIAL);

    expect(callLLMStreaming).toHaveBeenCalledTimes(2);
    expect(callLLMStreaming.mock.calls[0]![1]).toEqual([]);
    expect(callLLMStreaming.mock.calls[1]![1]).toEqual([]);
    expect(recordApiCall.mock.calls).toEqual([
      [11, 3, 2, 6],
      [17, 7, 2, 6],
    ]);
    const repairMessages = callLLMStreaming.mock.calls[1]![0] as Array<{
      role: string;
      content: string;
    }>;
    expect(repairMessages.at(-1)!.content).toContain('previous output');
    expect(repairMessages.at(-1)!.content).toContain('ONLY the JSON object');
  });

  it('does not log material, credentials, or provider endpoint URLs in debug',
     async () => {
       const info = vi.spyOn(console, 'info').mockImplementation(() => {});
       respondWith(JSON.stringify(validOutput()));

       await runWeeklyDream(
           {start: '2026-07-06', end: '2026-07-13'}, MATERIAL, {debug: true});

       expect(info).toHaveBeenCalledTimes(1);
       const logged = info.mock.calls[0]![1] as Record<string, unknown>;
       expect(logged).not.toHaveProperty('material');
       expect(logged).not.toHaveProperty('messages');
       expect(logged).not.toHaveProperty('baseUrl');
       expect(JSON.stringify(info.mock.calls)).not.toContain('sk-test');
       info.mockRestore();
     });

  it('repairs a structurally malformed result once', async () => {
    const malformed = validOutput();
    malformed['headline'] = 42;
    respondWith(JSON.stringify(malformed));
    respondWith(JSON.stringify(validOutput()));

    const result = await runWeeklyDream(
        {start: '2026-07-06', end: '2026-07-13'}, MATERIAL);

    expect(result!.headline).toBe('继续推进迁移主线');
    expect(callLLMStreaming).toHaveBeenCalledTimes(2);
  });

  it('repairs an empty required field even when primary refs are unsupported',
     async () => {
       const invalidSparse = validOutput();
       invalidSparse['headline'] = '   ';
       (invalidSparse['primary_thread'] as Record<string, unknown>)[
           'source_refs'] = ['fabricated'];
       respondWith(JSON.stringify(invalidSparse));
       respondWith(JSON.stringify(validOutput()));

       const result = await runWeeklyDream(
           {start: '2026-07-06', end: '2026-07-13'}, MATERIAL);

       expect(result!.headline).toBe('继续推进迁移主线');
       expect(callLLMStreaming).toHaveBeenCalledTimes(2);
     });

  it('rejects unsafe required text instead of returning sparse', async () => {
    const invalidSparse = validOutput();
    (invalidSparse['primary_thread'] as Record<string, unknown>)[
        'source_refs'] = ['fabricated'];
    (invalidSparse['footprint_summary'] as Record<string, unknown>)[
        'time_pattern'] = '<strong>unsafe</strong>';
    respondWith(JSON.stringify(invalidSparse));
    respondWith(JSON.stringify(invalidSparse));

    await expect(runWeeklyDream(
        {start: '2026-07-06', end: '2026-07-13'}, MATERIAL))
        .rejects.toThrow(/invalid weekly output after retry/);
    expect(callLLMStreaming).toHaveBeenCalledTimes(2);
  });

  it('fails as invalid output after at most one repair', async () => {
    respondWith('not json');
    respondWith('still not json');

    await expect(runWeeklyDream(
        {start: '2026-07-06', end: '2026-07-13'}, MATERIAL))
        .rejects.toThrow(/invalid weekly output after retry/);
    expect(callLLMStreaming).toHaveBeenCalledTimes(2);
  });

  it('returns sparse without repairing an otherwise valid unsupported thread',
     async () => {
       const sparse = validOutput();
       (sparse['primary_thread'] as Record<string, unknown>)['source_refs'] =
           ['fabricated'];
       respondWith(JSON.stringify(sparse));

       await expect(runWeeklyDream(
           {start: '2026-07-06', end: '2026-07-13'}, MATERIAL))
           .resolves.toBeNull();
       expect(callLLMStreaming).toHaveBeenCalledTimes(1);
     });
});
