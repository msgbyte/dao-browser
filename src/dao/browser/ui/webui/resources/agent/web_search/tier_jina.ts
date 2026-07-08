// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tier for Jina Reader (https://r.jina.ai/<url>) and Jina Search
// (https://s.jina.ai/?q=<query>). Goes through callNativeFetch so we don't
// trip CORS from the dao://agent origin. Returns null on any non-success path
// so the orchestrator can fall through to the next tier.
//
// Jina Search requires an API key. Without one, search returns null and
// web_search falls through to DuckDuckGo.

import {callNativeFetch} from '../agent_bridge.js';
import {markJinaUnavailable} from './circuit_breaker.js';
import type {FetchResponse, SearchResponse, SearchResult} from './types.js';

const READER_ENDPOINT = 'https://r.jina.ai/';
const SEARCH_ENDPOINT = 'https://s.jina.ai/';
export const JINA_API_KEY_STORAGE_KEY = 'dao_jina_api_key';

export function getJinaApiKey(): string {
  try {
    return (localStorage.getItem(JINA_API_KEY_STORAGE_KEY) ?? '').trim();
  } catch {
    return '';
  }
}

export function setJinaApiKey(value: string): void {
  try {
    const trimmed = value.trim();
    if (trimmed) {
      localStorage.setItem(JINA_API_KEY_STORAGE_KEY, trimmed);
    } else {
      localStorage.removeItem(JINA_API_KEY_STORAGE_KEY);
    }
  } catch { /* ignore storage failures */ }
}

function toSnippet(value: unknown): string {
  if (typeof value !== 'string') return '';
  const collapsed = value.replace(/\s+/g, ' ').trim();
  return collapsed.length > 600 ? collapsed.slice(0, 597) + '...' : collapsed;
}

function parseJinaSearchResponse(
    body: string, maxResults: number): SearchResult[]|null {
  let json: unknown;
  try {
    json = JSON.parse(body);
  } catch {
    return null;
  }

  const data = (json as {data?: unknown}).data;
  if (!Array.isArray(data)) return null;

  const out: SearchResult[] = [];
  for (const item of data) {
    if (out.length >= maxResults) break;
    const row = item as {
      title?: unknown;
      url?: unknown;
      content?: unknown;
      description?: unknown;
    };
    if (typeof row.title !== 'string' || typeof row.url !== 'string') {
      continue;
    }
    out.push({
      title: row.title.trim(),
      url: row.url,
      snippet: toSnippet(row.content) || toSnippet(row.description),
    });
  }

  return out.length > 0 ? out : null;
}

export async function searchViaJina(
    query: string, maxResults: number): Promise<SearchResponse|null> {
  const apiKey = getJinaApiKey();
  if (!apiKey) return null;

  const url = SEARCH_ENDPOINT + '?' + new URLSearchParams({q: query});
  const r = await callNativeFetch(url, {
    headers: {
      'Accept': 'application/json',
      'Authorization': 'Bearer ' + apiKey,
    },
  });
  if (!r.ok) return null;

  const results = parseJinaSearchResponse(r.body, maxResults);
  if (!results) return null;

  return {source: 'jina', query, results};
}

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
