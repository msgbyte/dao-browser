// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Two layers decide whether a (provider, model) combo can use a
// server-side web-search tool:
//   1. ALLOWLIST: exact native provider id + model prefix (audit on
//      every Chromium / SDK upgrade).
//   2. KEYWORD_INFERENCE: openai-compatible only, for LiteLLM-style
//      proxies that forward server tools to native APIs. Other
//      OpenAI-shaped providers (groq, xai, openrouter) intentionally
//      skip inference: groq only serves its own models, xai uses a
//      separate Live Search API, and openrouter does not forward
//      server tools through its API shape.

export type ToolSpecKind =
    'anthropic'|'openai-responses'|'gemini-grounding';

interface AllowlistEntry {
  provider: string;       // matches LLM_PROVIDERS[].id in llm_config.ts
  modelPrefixes: string[];
  toolSpec: ToolSpecKind;
}

// Layer 1 — exact-provider allowlist.
//
// Coverage as of 2026-05. Re-audit on every Chromium upgrade and when
// providers ship new model families. Source-of-truth:
//   - Anthropic: https://platform.claude.com/docs/en/docs/agents-and-tools/tool-use/web-search-tool
//     (supports Sonnet/Opus/Haiku 4 family + 4.5, 4.6, 4.7 generations,
//     and the Mythos Preview)
//   - OpenAI Responses: web_search / web_search_preview supports
//     gpt-5 family, gpt-4o/4.1, o3, o4-mini.
//   - Gemini: google_search grounding supports 2.0 Flash, 2.5 Flash/
//     Flash-Lite/Pro, 3 Pro/Flash, 3.1 Pro/Flash. Older 1.x models
//     used a different (legacy) tool name and are excluded here.
const ALLOWLIST: AllowlistEntry[] = [
  {
    provider: 'anthropic',
    // Match the entire Claude 4-and-newer family. Versioned snapshots
    // (e.g. claude-sonnet-4-5-20250929, claude-opus-4-7-20260124) all
    // start with the family prefix. Older 3.x models are excluded —
    // they don't ship the web_search server tool.
    modelPrefixes: [
      'claude-sonnet-4',
      'claude-opus-4',
      'claude-haiku-4',
      'claude-mythos',
    ],
    toolSpec: 'anthropic',
  },
  {
    provider: 'openai',
    modelPrefixes: ['gpt-5', 'gpt-4o', 'gpt-4.1', 'o3', 'o4'],
    toolSpec: 'openai-responses',
  },
  {
    provider: 'google',
    modelPrefixes: ['gemini-2.0', 'gemini-2.5', 'gemini-3'],
    toolSpec: 'gemini-grounding',
  },
];

// Layer 2 — keyword inference rules. Each rule maps a substring (or
// prefix, when explicitly anchored) found in a model name to the
// server-tool spec we should send. Only consulted for
// provider === 'openai-compatible'.
//
// Substring match (not just prefix) because LiteLLM-style proxies
// often namespace the model (e.g. `anthropic/claude-sonnet-4-5`,
// `bedrock/claude-3-haiku`, `azure/gpt-4o`). The keyword lives
// inside the slug.
//
// Order matters: more specific keywords first so e.g. "gpt-4o" wins
// over a hypothetical broader "gpt".
interface InferenceRule {
  keywords: string[];
  toolSpec: ToolSpecKind;
}

const KEYWORD_INFERENCE: InferenceRule[] = [
  {
    keywords: [
      'claude-sonnet-4',
      'claude-opus-4',
      'claude-haiku-4',
      'claude-mythos',
    ],
    toolSpec: 'anthropic',
  },
  {
    keywords: ['gpt-5', 'gpt-4o', 'gpt-4.1', 'o3-', 'o4-'],
    toolSpec: 'openai-responses',
  },
  {
    keywords: ['gemini-2.0', 'gemini-2.5', 'gemini-3'],
    toolSpec: 'gemini-grounding',
  },
];

function inferFromModelName(model: string): ToolSpecKind|null {
  const lc = model.toLowerCase();
  for (const rule of KEYWORD_INFERENCE) {
    for (const kw of rule.keywords) {
      if (lc.includes(kw)) return rule.toolSpec;
    }
  }
  return null;
}

function findToolSpec(provider: string, model: string): ToolSpecKind|null {
  // Layer 1: exact provider id + model-prefix match.
  for (const e of ALLOWLIST) {
    if (e.provider !== provider) continue;
    for (const p of e.modelPrefixes) {
      if (model.startsWith(p)) return e.toolSpec;
    }
  }

  // Layer 2: openai-compatible proxy with a model name that looks
  // like a known family. The user is responsible for ensuring their
  // proxy actually forwards server tools (e.g. LiteLLM does); if the
  // proxy ignores the spec, the LLM falls back to the local
  // web_search tool which still works via DDG.
  if (provider === 'openai-compatible') {
    return inferFromModelName(model);
  }

  return null;
}

export function isProviderSearchAvailable(
    provider: string, model: string): boolean {
  return findToolSpec(provider, model) !== null;
}

export function getProviderToolSpecKind(
    provider: string, model: string): ToolSpecKind|null {
  return findToolSpec(provider, model);
}
