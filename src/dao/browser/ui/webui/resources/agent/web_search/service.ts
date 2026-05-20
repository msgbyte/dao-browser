// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Orchestrator for the search/fetch fallback chain.
//
// Search:    Tier 1 (provider built-in) -> Tier 3 (DuckDuckGo HTML)
//            Jina's search endpoint requires an API key, so it is not
//            on the search chain. Provider built-in is the preferred
//            path; DuckDuckGo is the universal zero-config fallback.
//
// fetch_url: Tier 2 (Jina Reader, no key required) -> browser fetch.
//            Jina Reader stays free, so it remains the primary path
//            for converting articles to markdown.
//
// User override (Settings → "Search source") can pin a search tier.

import type {SearchResponse, FetchResponse, SearchSourceOverride}
    from './types.js';
import {isJinaAvailable} from './circuit_breaker.js';
import {isProviderSearchAvailable} from './provider_capabilities.js';
import {fetchUrlViaJina} from './tier_jina.js';
import {
  searchViaDuckDuckGo, fetchUrlViaBrowser,
} from './tier_duckduckgo.js';

const OVERRIDE_KEY = 'dao_search_source';

export function getSearchSourceOverride(): SearchSourceOverride {
  let raw: string|null = null;
  try {
    raw = localStorage.getItem(OVERRIDE_KEY);
  } catch (_) { /* ignore */ }
  switch (raw) {
    case 'provider':
    case 'duckduckgo':
      return raw;
    default:
      return 'auto';
  }
}

export function setSearchSourceOverride(v: SearchSourceOverride): void {
  try { localStorage.setItem(OVERRIDE_KEY, v); } catch (_) { /* ignore */ }
}

const DEFAULT_MAX_RESULTS = 5;
const HARD_MAX_RESULTS = 10;

function clampMax(n: number|undefined): number {
  if (typeof n !== 'number' || !Number.isFinite(n) || n <= 0) {
    return DEFAULT_MAX_RESULTS;
  }
  return Math.min(Math.floor(n), HARD_MAX_RESULTS);
}

export interface ProviderHint {
  provider: string;
  model: string;
}

// The local web_search tool body. Note: when provider built-in search
// is active, this function may not be called at all — pi_llm_stream
// strips the local web_search tool and the LLM uses the server-side
// tool instead. This path runs when:
//   - provider is not on the allowlist, OR
//   - the user override forces a non-provider tier, OR
//   - the LLM somehow chose the local tool despite the server-side
//     one being available (degrade gracefully).
export async function webSearch(
    query: string, maxResults: number|undefined,
    hint: ProviderHint): Promise<SearchResponse> {
  const max = clampMax(maxResults);
  const override = getSearchSourceOverride();

  // Provider tier is handled at the LLM-stream layer. If webSearch()
  // runs at all, either the provider didn't support a server tool or
  // the LLM chose the local fallback — both degrade to DDG. The one
  // exception is the "provider only" override on a model that doesn't
  // support it: we surface a clear failure instead of silently going
  // to DDG.
  if (override === 'provider' &&
      !isProviderSearchAvailable(hint.provider, hint.model)) {
    return {
      source: 'failed', query, results: [],
      error:
          'Search source is locked to "provider" but the active model ' +
          'does not support built-in web search.',
    };
  }
  return searchViaDuckDuckGo(query, max);
}

export interface FetchUrlOptions {
  // URL of the currently active tab. Used together with `withCredentials`
  // to decide whether to take the same-origin authenticated path.
  // Has no effect on its own.
  activeTabUrl?: string;
  // Opt-in: caller wants cookies attached. Defaults to false. Cookies
  // are still ONLY attached when `activeTabUrl` is same-origin
  // (scheme + host + port) with the target URL — the native side
  // re-validates this independently. Always false-equivalent for any
  // non-GET method (the native handler ignores the flag for non-GET).
  withCredentials?: boolean;
}

// Returns true if the two URLs share scheme + host + port. Both must
// parse cleanly and have a non-opaque origin (e.g. http(s)). Anything
// else (file:, data:, opaque blob:, malformed input) returns false so
// we never accidentally widen credential exposure.
function isSameOrigin(a: string, b: string): boolean {
  try {
    const ua = new URL(a);
    const ub = new URL(b);
    if (ua.protocol !== 'http:' && ua.protocol !== 'https:') return false;
    if (ub.protocol !== 'http:' && ub.protocol !== 'https:') return false;
    return ua.origin === ub.origin;
  } catch {
    return false;
  }
}

export async function fetchUrl(
    targetUrl: string,
    opts?: FetchUrlOptions): Promise<FetchResponse> {
  // Validate early so the LLM gets a clear error, not an opaque DOM
  // exception from URL parsing inside the tier.
  try { new URL(targetUrl); } catch {
    return {
      source: 'failed', url: targetUrl, title: '', content: '',
      error: 'invalid URL',
    };
  }

  const override = getSearchSourceOverride();

  // Authenticated same-origin shortcut. Triggers ONLY when the caller
  // explicitly opts in via `withCredentials` AND the target URL is
  // same-origin with the active tab. In that case we go straight to
  // the browser tier with credentials enabled — Jina Reader would just
  // hand back the public/anonymous view of the same URL, defeating the
  // whole point of opting in.
  //
  // The native side re-validates the same-origin check independently
  // before attaching any cookies and ignores the flag entirely for
  // non-GET methods, so the worst-case if the active tab changed mid-
  // request is an unauthenticated fetch — never a cross-origin leak.
  const wantsCredentials = opts?.withCredentials === true;
  const sameOriginAsActiveTab = !!opts?.activeTabUrl &&
      isSameOrigin(opts.activeTabUrl, targetUrl);
  if (wantsCredentials && sameOriginAsActiveTab &&
      override !== 'provider') {
    return fetchUrlViaBrowser(targetUrl, {
      includeCredentialsIfSameOriginActiveTab: true,
    });
  }

  // Override semantics for fetch_url:
  //  - 'provider'   : provider tier doesn't expose a reader, so we
  //                   honour the user's intent ("don't use third-party
  //                   infra") by returning failed.
  //  - 'duckduckgo' : browser fetch only (skip Jina Reader).
  //  - 'auto'       : Jina Reader then browser fetch.
  if (override === 'provider') {
    return {
      source: 'failed', url: targetUrl, title: '', content: '',
      error:
          'Search source is locked to "provider"; fetch_url has no ' +
          'provider equivalent.',
    };
  }
  if (override === 'duckduckgo') {
    return fetchUrlViaBrowser(targetUrl);
  }

  if (isJinaAvailable()) {
    const j = await fetchUrlViaJina(targetUrl);
    if (j) return j;
  }
  return fetchUrlViaBrowser(targetUrl);
}
