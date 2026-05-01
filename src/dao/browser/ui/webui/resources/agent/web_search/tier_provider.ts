// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Strip-vs-keep policy:
//   - Native providers (anthropic / openai / google): strip the local
//     web_search tool because the server-side tool is guaranteed to
//     work; offering both confuses the LLM.
//   - openai-compatible keyword-inferred matches (e.g. LiteLLM in
//     front of Anthropic): KEEP the local tool — the proxy may or may
//     not forward server tools, and the LLM needs a fallback.

import {getProviderToolSpecKind, type ToolSpecKind}
    from './provider_capabilities.js';
import {getSearchSourceOverride} from './service.js';

export type ProviderToolSpec = Record<string, unknown>;

export interface InjectionDecision {
  injectSpec: ProviderToolSpec|null;
  stripLocalWebSearch: boolean;
}

const NATIVE_PROVIDERS = new Set(['anthropic', 'openai', 'google']);

const NO_INJECTION: InjectionDecision = {
  injectSpec: null,
  stripLocalWebSearch: false,
};

const TOOL_SPEC_BY_KIND: Record<ToolSpecKind, ProviderToolSpec> = {
  // web_search_20250305 is the basic standalone server tool. The newer
  // web_search_20260209 adds dynamic filtering but requires
  // code_execution, which we don't ship.
  'anthropic': {
    type: 'web_search_20250305',
    name: 'web_search',
    max_uses: 5,
  },
  'openai-responses': {type: 'web_search_preview'},
  'gemini-grounding': {google_search: {}},
};

export function decideProviderInjection(
    provider: string, model: string): InjectionDecision {
  if (getSearchSourceOverride() === 'duckduckgo') return NO_INJECTION;

  const kind = getProviderToolSpecKind(provider, model);
  if (kind === null) return NO_INJECTION;

  return {
    injectSpec: TOOL_SPEC_BY_KIND[kind],
    stripLocalWebSearch: NATIVE_PROVIDERS.has(provider),
  };
}
