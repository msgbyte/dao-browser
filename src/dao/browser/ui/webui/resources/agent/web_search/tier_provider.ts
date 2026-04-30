// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tier 1 lives at the pi_llm_stream layer, not as a runnable function.
// This file exposes the *tool spec* that pi_llm_stream needs to splice
// into the request payload, plus the helper that decides whether to
// strip the local web_search tool from the request.
//
// Three providers, three shapes. Anthropic uses a typed tool entry;
// OpenAI Responses uses {type:'web_search_preview'}; Gemini uses
// grounding via google_search.
//
// Strip-vs-keep policy:
//   - For NATIVE providers on the exact-provider allowlist (anthropic,
//     openai, google) we strip the local web_search tool. The
//     server-side tool is guaranteed to work; offering both confuses
//     the LLM.
//   - For openai-compatible KEYWORD-INFERRED matches (e.g. user has
//     LiteLLM in front of Anthropic) we KEEP the local tool too. The
//     proxy may or may not forward server tools; if it ignores the
//     spec and the LLM doesn't see results, the LLM can fall back to
//     the local DDG path.

import {getProviderToolSpecKind} from './provider_capabilities.js';
import {getSearchSourceOverride} from './service.js';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type ProviderToolSpec = any;

export interface InjectionDecision {
  injectSpec: ProviderToolSpec|null;
  stripLocalWebSearch: boolean;
}

// Native providers that we trust to actually execute server-side
// search. For these, stripping the local tool is safe.
const NATIVE_PROVIDERS = new Set(['anthropic', 'openai', 'google']);

export function decideProviderInjection(
    provider: string, model: string): InjectionDecision {
  const override = getSearchSourceOverride();

  // User pinned a non-provider tier — never inject.
  if (override === 'duckduckgo') {
    return {injectSpec: null, stripLocalWebSearch: false};
  }

  const kind = getProviderToolSpecKind(provider, model);

  // User pinned 'provider' but model doesn't support it — webSearch()
  // will return a failed response with an explanation; here we just
  // make sure no spec is injected.
  if (override === 'provider' && kind === null) {
    return {injectSpec: null, stripLocalWebSearch: false};
  }

  if (kind === null) {
    return {injectSpec: null, stripLocalWebSearch: false};
  }

  // Strip the local tool only when we trust the server-side path.
  const stripLocal = NATIVE_PROVIDERS.has(provider);

  switch (kind) {
    case 'anthropic':
      // web_search_20250305 is the basic, standalone server tool that
      // works on every Claude 4+ family member without any extra
      // dependencies. The newer web_search_20260209 enables dynamic
      // filtering but requires the code_execution tool to also be
      // enabled — we don't ship that, so stay on the basic version.
      return {
        injectSpec: {
          type: 'web_search_20250305',
          name: 'web_search',
          max_uses: 5,
        },
        stripLocalWebSearch: stripLocal,
      };
    case 'openai-responses':
      return {
        injectSpec: {type: 'web_search_preview'},
        stripLocalWebSearch: stripLocal,
      };
    case 'gemini-grounding':
      return {
        injectSpec: {google_search: {}},
        stripLocalWebSearch: stripLocal,
      };
  }
}
