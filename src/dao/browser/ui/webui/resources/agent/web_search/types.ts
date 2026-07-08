// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Type contracts for the agent web-search subsystem. Kept in one file
// because all consumers (service, tiers, bridge, renderer) need to agree
// on the shape of a search result.

export type SearchSource = 'provider' | 'jina' | 'duckduckgo' | 'failed';

export type FetchSource = 'jina' | 'browser' | 'failed';

// Note: 'jina' is intentionally absent here because it is an optional
// Auto-mode fallback when a Jina API key is configured, not a separate
// source override surface. Jina Reader is still used for fetch_url.
export type SearchSourceOverride = 'auto' | 'provider' | 'duckduckgo';

export interface SearchResult {
  title: string;
  url: string;
  snippet: string;
}

export interface SearchResponse {
  source: SearchSource;
  query: string;
  results: SearchResult[];
  error?: string;
}

export interface FetchResponse {
  source: FetchSource;
  url: string;
  title: string;
  content: string;  // markdown
  error?: string;
}

export interface SearchOptions {
  maxResults?: number;  // default 5, max 10
}
