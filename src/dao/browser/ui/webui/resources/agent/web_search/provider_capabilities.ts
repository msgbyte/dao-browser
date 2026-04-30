// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Hardcoded allowlist of (provider, model-prefix) pairs that ship a
// server-side web-search tool. We match on prefix because models are
// versioned (`claude-sonnet-4-5-20250929` etc.) and the search tool
// lives on the model family, not a specific snapshot.
//
// When a Chromium upgrade lands we audit this list along with the
// provider tool-spec strings in tier_provider.ts.

export type ToolSpecKind =
    'anthropic'|'openai-responses'|'gemini-grounding';

interface AllowlistEntry {
  provider: string;       // matches LLM_PROVIDERS[].id in llm_config.ts
  modelPrefixes: string[];
  toolSpec: ToolSpecKind;
}

const ALLOWLIST: AllowlistEntry[] = [
  {
    provider: 'anthropic',
    modelPrefixes: ['claude-sonnet-4', 'claude-opus-4', 'claude-haiku-4'],
    toolSpec: 'anthropic',
  },
  {
    provider: 'openai',
    modelPrefixes: ['gpt-4o', 'gpt-4.1', 'o3', 'o4'],
    toolSpec: 'openai-responses',
  },
  {
    provider: 'google',
    modelPrefixes: ['gemini-2.0', 'gemini-2.5'],
    toolSpec: 'gemini-grounding',
  },
];

function findEntry(provider: string, model: string): AllowlistEntry|null {
  for (const e of ALLOWLIST) {
    if (e.provider !== provider) continue;
    for (const p of e.modelPrefixes) {
      if (model.startsWith(p)) return e;
    }
  }
  return null;
}

export function isProviderSearchAvailable(
    provider: string, model: string): boolean {
  return findEntry(provider, model) !== null;
}

export function getProviderToolSpecKind(
    provider: string, model: string): ToolSpecKind|null {
  return findEntry(provider, model)?.toolSpec ?? null;
}
