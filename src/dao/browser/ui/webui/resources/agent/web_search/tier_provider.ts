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

import {getProviderToolSpecKind} from './provider_capabilities.js';
import {getSearchSourceOverride} from './service.js';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
export type ProviderToolSpec = any;

export interface InjectionDecision {
  injectSpec: ProviderToolSpec|null;
  stripLocalWebSearch: boolean;
}

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

  switch (kind) {
    case 'anthropic':
      return {
        injectSpec: {
          type: 'web_search_20250305',
          name: 'web_search',
          max_uses: 5,
        },
        stripLocalWebSearch: true,
      };
    case 'openai-responses':
      return {
        injectSpec: {type: 'web_search_preview'},
        stripLocalWebSearch: true,
      };
    case 'gemini-grounding':
      return {
        injectSpec: {google_search: {}},
        stripLocalWebSearch: true,
      };
  }
}
