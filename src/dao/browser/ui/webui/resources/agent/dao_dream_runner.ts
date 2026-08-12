// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Daily Dream Analysis LLM runner. Executes a single-shot LLM summarization
// over the material pack using the user's active provider and validates the
// JSON output. Listener ownership lives in dao_dream_dispatcher.ts.

import {recordApiCall} from './agent_bridge.js';
import type {ChatMessage, UsageInfo} from './agent_bridge.js';
import {currentLocale, initI18n} from './i18n/i18n.js';
import {getCostRatesForConfig} from './llm_cost.js';
import {getActiveLLMConfig} from './llm_config.js';

interface DreamHabit {
  key: string;
  value: string;
  confidence: number;
  evidence: string;
  relation: 'new'|'reinforce'|'contradict';
}

interface DreamTheme {
  name: string;
  summary: string;
  intensity: 'light'|'medium'|'deep';
  time_label: string;
  attention_share: number;
}

interface DreamRecap {
  summary: string;
  time_buckets: {
    morning_minutes: number;
    afternoon_minutes: number;
    evening_minutes: number;
    night_minutes: number;
  };
  themes: DreamTheme[];
}

interface DreamResult {
  report_markdown: string;
  recap: DreamRecap;
  habits: DreamHabit[];
  scenario_adjustments: Array<{scenario_id: string; suggestion: string}>;
}

interface RunDreamOptions {
  debug?: boolean;
}

export interface DreamCallResult {
  content: string;
  usage?: UsageInfo;
}

const SYSTEM_PROMPT = `You are Dao Browser's dream analyst. You receive a
condensed JSON "material pack" describing one browsing day: top domains with
page titles, foreground_seconds, total_seconds, duration_level, and
time-of-day visit buckets plus foreground_seconds_by_bucket, search queries,
short excerpts of the user's questions to
the in-browser AI agent, high-confidence known preferences, feedback stats on
proactive suggestions, and aggregate counts. Turn it into a calm morning
reflection and a small set of durable memory candidates. The report should
feel like Dao quietly made sense of yesterday, not like an audit log.

Output STRICT JSON (no markdown fence, no commentary) with exactly this
shape:
{
  "report_markdown": "<morning report, 180-320 words, written in the
    user's current language and style. Use short markdown sections. Cover the
    main themes of the day, broad time pattern, notable observations, and
    1-3 gentle reflection questions or next-day hints. Do not list every
    domain, page title, or query. Friendly, calm tone; address the user
    directly in the target language.>",
  "recap": {
    "summary": "<one calm sentence that captures the day's main arc in the user's language>",
    "time_buckets": {
      "morning_minutes": <non-negative integer>,
      "afternoon_minutes": <non-negative integer>,
      "evening_minutes": <non-negative integer>,
      "night_minutes": <non-negative integer>
    },
    "themes": [{
      "name": "<short theme name in the user's language>",
      "summary": "<one concise evidence-grounded sentence>",
      "intensity": "light" | "medium" | "deep",
      "time_label": "<short localized time-of-day label>",
      "attention_share": <integer 0-100, relative to the strongest theme>
    }]
  },
  "habits": [{
    "key": "<dot.namespaced key, e.g. interest.rust_async>",
    "value": "<one-sentence habit/preference description in the user's language>",
    "confidence": <0.3-0.8>,
    "evidence": "<one-line justification from the material in the user's language>",
    "relation": "new" | "reinforce" | "contradict"
  }],
  "scenario_adjustments": [{
    "scenario_id": "<id from feedback>",
    "suggestion": "lower_confidence" | "raise_confidence"
  }]
}

Rules:
- Use the required output locale injected below for every user-facing string.
  It is authoritative; source material in another language must not change
  the output language.
- All user-facing report text, habit values, evidence, and questions must
  use the user's current locale and language style. Do not mix English into
  Chinese output unless the source material itself is an English proper noun,
  product name, domain, code term, or quoted user text.
- Treat the material pack as sampled, condensed evidence. If there is a lot of
  history, summarize by topic, intent, and time pattern instead of enumerating
  entries.
- Use foreground_seconds as the primary attention signal for history domains:
  duration_level "deep" deserves the most narrative weight, "medium" can be
  mentioned briefly, and "light" should be summarized in passing or omitted
  unless it reinforces searches, agent conversations, or another strong theme.
- Build recap.time_buckets from stats.foreground_seconds_by_bucket, converting
  measured seconds to rounded minutes. Keep zero values when a bucket has no
  foreground activity. Do not infer time from the visit-count-only buckets.
- Include 0-3 recap themes. Merge related domains, titles, searches, and agent
  questions into user-meaningful topics instead of using domain names as theme
  names. Set the strongest theme's attention_share to 100 and scale the rest
  relative to it.
- Treat material.preferences as existing memory, not as new evidence from the
  day. Use it only to decide whether a habit candidate is "reinforce",
  "contradict", or "new".
- Never invent domains or facts not present in the material.
- Mention domains only when they materially support an insight. Never expose
  raw URLs, query parameters, account identifiers, or unnecessary private
  snippets. Avoid making the report feel surveillant.
- Express weak signals with uncertainty. Do not diagnose the user's health,
  personality, or identity, and do not moralize about their browsing.
- 0-6 habits. Only durable patterns, not one-off visits. Prefer fewer,
  higher-confidence habits over noisy guesses.
- Habit values must be concise, user-facing preference statements in the
  target language. Keep only machine-readable habit keys in English
  dot.namespace format.
- Use "reinforce" or "contradict" only when the material clearly relates to
  known preferences included in the input. If no known preferences are
  included, use "new".
- Scenario adjustments must reference only scenario IDs present in feedback
  and should be empty when the signal is weak.
- Return valid JSON only. Use empty arrays when there are no good habits or
  scenario adjustments.`;

function validateResult(parsed: unknown): DreamResult|null {
  if (typeof parsed !== 'object' || parsed === null) return null;
  const obj = parsed as Record<string, unknown>;
  if (typeof obj['report_markdown'] !== 'string' ||
      obj['report_markdown'].length === 0) {
    return null;
  }
  const recap = normalizeRecap(obj['recap']);
  const habits: DreamHabit[] = [];
  if (Array.isArray(obj['habits'])) {
    for (const h of obj['habits']) {
      if (typeof h !== 'object' || h === null) continue;
      const hh = h as Record<string, unknown>;
      if (typeof hh['key'] !== 'string' || typeof hh['value'] !== 'string') {
        continue;
      }
      const relation = hh['relation'];
      habits.push({
        key: hh['key'],
        value: hh['value'],
        confidence: typeof hh['confidence'] === 'number' ?
            Math.min(Math.max(hh['confidence'], 0), 0.8) :
            0.5,
        evidence: typeof hh['evidence'] === 'string' ? hh['evidence'] : '',
        relation: relation === 'reinforce' || relation === 'contradict' ?
            relation :
            'new',
      });
    }
  }
  const adjustments: DreamResult['scenario_adjustments'] = [];
  if (Array.isArray(obj['scenario_adjustments'])) {
    for (const a of obj['scenario_adjustments']) {
      if (typeof a !== 'object' || a === null) continue;
      const aa = a as Record<string, unknown>;
      if (typeof aa['scenario_id'] === 'string' &&
          (aa['suggestion'] === 'lower_confidence' ||
           aa['suggestion'] === 'raise_confidence')) {
        adjustments.push({
          scenario_id: aa['scenario_id'],
          suggestion: aa['suggestion'],
        });
      }
    }
  }
  return {
    report_markdown: obj['report_markdown'],
    recap,
    habits,
    scenario_adjustments: adjustments,
  };
}

function boundedInteger(value: unknown, max: number): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    return 0;
  }
  return Math.min(Math.max(Math.round(value), 0), max);
}

function normalizeRecap(value: unknown): DreamRecap {
  const recap = typeof value === 'object' && value !== null ?
      value as Record<string, unknown> : {};
  const rawBuckets =
      typeof recap['time_buckets'] === 'object' &&
          recap['time_buckets'] !== null ?
      recap['time_buckets'] as Record<string, unknown> : {};
  const themes: DreamTheme[] = [];
  if (Array.isArray(recap['themes'])) {
    for (const theme of recap['themes']) {
      if (themes.length >= 3 || typeof theme !== 'object' || theme === null) {
        continue;
      }
      const candidate = theme as Record<string, unknown>;
      if (typeof candidate['name'] !== 'string' ||
          typeof candidate['summary'] !== 'string' ||
          !candidate['name'].trim() || !candidate['summary'].trim()) {
        continue;
      }
      const intensity = candidate['intensity'];
      themes.push({
        name: candidate['name'].trim(),
        summary: candidate['summary'].trim(),
        intensity: intensity === 'light' || intensity === 'deep' ?
            intensity : 'medium',
        time_label: typeof candidate['time_label'] === 'string' ?
            candidate['time_label'].trim() : '',
        attention_share: boundedInteger(candidate['attention_share'], 100),
      });
    }
  }
  return {
    summary: typeof recap['summary'] === 'string' ?
        recap['summary'].trim() : '',
    time_buckets: {
      morning_minutes: boundedInteger(rawBuckets['morning_minutes'], 1440),
      afternoon_minutes:
          boundedInteger(rawBuckets['afternoon_minutes'], 1440),
      evening_minutes: boundedInteger(rawBuckets['evening_minutes'], 1440),
      night_minutes: boundedInteger(rawBuckets['night_minutes'], 1440),
    },
    themes,
  };
}

// Strips ```json fences if the model wrapped its output anyway.
export function extractJson(raw: string): string {
  const trimmed = raw.trim();
  const fence = trimmed.match(/^```(?:json)?\s*([\s\S]*?)\s*```$/);
  return fence ? fence[1]! : trimmed;
}

export async function callDreamOnce(
    messages: ChatMessage[]): Promise<DreamCallResult> {
  // Call the pi adapter directly with an EMPTY tool list — dream
  // summarization is a single-shot text task. Going through
  // agent_bridge.callLLMStreaming would attach the full browser tool
  // catalog, which wastes tokens and can trip provider-side tool-schema
  // validation (e.g. "Missing required parameter: tools[N].function.name").
  const cfg = getActiveLLMConfig();
  const {callLLMStreamingWithPi} = await import('./pi_llm_stream.js');
  return new Promise<DreamCallResult>((resolve, reject) => {
    void callLLMStreamingWithPi(messages, [], {
      onToken: () => {},
      onToolCall: () => {},
      onDone: (fullContent, _toolCalls, usage) =>
        resolve({content: fullContent, usage}),
      onError: (shortMsg, fullError) =>
          reject(new Error(shortMsg + ': ' + fullError)),
    }, {
      provider: cfg.provider,
      apiKey: cfg.apiKey,
      baseUrl: cfg.baseUrl,
      model: cfg.model,
    });
  });
}

export function recordDreamUsage(
    usage: UsageInfo|undefined,
    cfg: ReturnType<typeof getActiveLLMConfig>) {
  if (!usage) {
    return;
  }
  const promptTokens = Number(usage.prompt_tokens) || 0;
  const completionTokens = Number(usage.completion_tokens) || 0;
  if (promptTokens <= 0 && completionTokens <= 0) {
    return;
  }
  const cost = getCostRatesForConfig(cfg);
  recordApiCall(promptTokens, completionTokens, cost.input, cost.output);
}

function logDreamRequest(
    dreamDate: string, material: unknown, messages: ChatMessage[],
    cfg: ReturnType<typeof getActiveLLMConfig>) {
  console.info('Dao Dream request', {
    dreamDate,
    provider: cfg.provider,
    baseUrl: cfg.baseUrl,
    model: cfg.model,
    apiKeyConfigured: Boolean(cfg.apiKey),
    tools: [],
    messages,
    material,
  });
}

export async function runDream(
    dreamDate: string, material: unknown,
    options: RunDreamOptions = {}): Promise<DreamResult> {
  const cfg = getActiveLLMConfig();
  if (!cfg.apiKey) {
    throw new Error('no LLM api key configured');
  }
  await initI18n();
  const locale = currentLocale();
  const systemPrompt =
      `${SYSTEM_PROMPT}\n\nRequired output locale: ${locale}`;
  const userPrompt = `Locale: ${locale}\n` +
      `Dream date: ${dreamDate}\n` +
      `Material pack:\n${JSON.stringify(material)}`;
  const messages: ChatMessage[] = [
    {role: 'system', content: systemPrompt},
    {role: 'user', content: userPrompt},
  ];
  if (options.debug) {
    logDreamRequest(dreamDate, material, messages, cfg);
  }

  let lastError = '';
  for (let attempt = 0; attempt < 2; attempt++) {
    const response = await callDreamOnce(
        lastError ? [...messages, {
          role: 'user' as const,
          content: 'Your previous output was not valid JSON (' + lastError +
              '). Output ONLY the JSON object.',
        }] :
                    messages);
    recordDreamUsage(response.usage, cfg);
    const raw = response.content;
    try {
      const result = validateResult(JSON.parse(extractJson(raw)));
      if (result) {
        return result;
      }
      lastError = 'missing required fields';
    } catch (e) {
      lastError = e instanceof Error ? e.message : String(e);
    }
  }
  throw new Error('invalid JSON after retry: ' + lastError);
}
