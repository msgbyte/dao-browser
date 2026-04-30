// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tier for Jina Reader (https://r.jina.ai/<url>) — used by fetch_url.
// Goes through callNativeFetch so we don't trip CORS from the
// dao://agent origin. Returns null on any non-success path so the
// orchestrator can fall through to the browser tier.
//
// Note: We previously also used s.jina.ai for search, but that endpoint
// now requires an API key. fetch_url still uses r.jina.ai because
// reader access remains free; web_search falls back directly from
// Tier 1 (provider built-in) to Tier 3 (DuckDuckGo).

import {callNativeFetch} from '../agent_bridge.js';
import {markJinaUnavailable} from './circuit_breaker.js';
import type {FetchResponse} from './types.js';

const READER_ENDPOINT = 'https://r.jina.ai/';

export async function fetchUrlViaJina(
    targetUrl: string): Promise<FetchResponse|null> {
  const r = await callNativeFetch(READER_ENDPOINT + targetUrl, {
    headers: {'Accept': 'application/json'},
  });

  if (r.status === 429) {
    markJinaUnavailable();
    return null;
  }
  if (!r.ok) return null;

  let body: unknown;
  try {
    body = JSON.parse(r.body);
  } catch {
    return null;
  }

  const data = (body as {
    data?: {title?: string; content?: string; url?: string}
  }).data;
  if (!data || typeof data.content !== 'string') return null;

  return {
    source: 'jina',
    url: data.url ?? targetUrl,
    title: data.title ?? '',
    content: data.content,
  };
}
