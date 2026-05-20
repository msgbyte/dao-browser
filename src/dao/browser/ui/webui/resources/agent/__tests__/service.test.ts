// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import {
  fetchUrl,
  getSearchSourceOverride,
  setSearchSourceOverride,
  webSearch,
} from '../web_search/service.js';
import { markJinaUnavailable } from '../web_search/circuit_breaker.js';

// callNativeFetch lives in agent_bridge.ts; mock it before importing the
// modules that use it. Vitest hoists vi.mock above imports automatically.
vi.mock('../agent_bridge.js', () => ({
  callNativeFetch: vi.fn(),
}));

import { callNativeFetch } from '../agent_bridge.js';

const mockedFetch = callNativeFetch as unknown as ReturnType<typeof vi.fn>;

beforeEach(() => {
  localStorage.clear();
  mockedFetch.mockReset();
});

afterEach(() => {
  localStorage.clear();
});

describe('search source override', () => {
  it('defaults to auto', () => {
    expect(getSearchSourceOverride()).toBe('auto');
  });

  it('round-trips a valid override', () => {
    setSearchSourceOverride('duckduckgo');
    expect(getSearchSourceOverride()).toBe('duckduckgo');
    setSearchSourceOverride('provider');
    expect(getSearchSourceOverride()).toBe('provider');
  });

  it('falls back to auto for unknown storage values', () => {
    localStorage.setItem('dao_search_source', 'bogus');
    expect(getSearchSourceOverride()).toBe('auto');
  });
});

describe('webSearch', () => {
  it('surfaces a failure when override is "provider" but model lacks it', async () => {
    setSearchSourceOverride('provider');
    const r = await webSearch('q', undefined, {
      provider: 'openai',
      model: 'gpt-3.5-turbo',  // not on the allowlist
    });
    expect(r.source).toBe('failed');
    expect(r.error).toMatch(/locked to "provider"/);
    expect(mockedFetch).not.toHaveBeenCalled();
  });

  it('falls through to DuckDuckGo when override matches provider support', async () => {
    setSearchSourceOverride('provider');
    mockedFetch.mockResolvedValue({
      ok: true,
      status: 200,
      body: `<div class="result">
        <a class="result__a" href="https://e.com">t</a>
        <span class="result__snippet">s</span>
      </div>`,
    });
    // Note: the orchestrator currently always falls through to DDG once
    // the provider availability gate passes — provider built-in search is
    // handled at the LLM-stream layer, not here.
    const r = await webSearch('q', 3, {
      provider: 'anthropic',
      model: 'claude-sonnet-4-5',
    });
    expect(r.source).toBe('duckduckgo');
    expect(r.results).toHaveLength(1);
  });

  it('clamps maxResults to HARD_MAX_RESULTS = 10', async () => {
    // Build 15 result rows; webSearch should clamp to 10.
    const rows = Array.from({ length: 15 }, (_, i) => `
      <div class="result">
        <a class="result__a" href="https://e.com/${i}">t${i}</a>
        <span class="result__snippet">s${i}</span>
      </div>`).join('');
    mockedFetch.mockResolvedValue({ ok: true, status: 200, body: rows });

    const r = await webSearch('q', 999, {
      provider: 'openai-compatible', model: 'something',
    });
    expect(r.results).toHaveLength(10);
  });

  it('treats undefined/NaN maxResults as the default of 5', async () => {
    const rows = Array.from({ length: 8 }, (_, i) => `
      <div class="result">
        <a class="result__a" href="https://e.com/${i}">t${i}</a>
        <span class="result__snippet">s${i}</span>
      </div>`).join('');
    mockedFetch.mockResolvedValue({ ok: true, status: 200, body: rows });

    const r = await webSearch('q', undefined, {
      provider: 'openai-compatible', model: 'x',
    });
    expect(r.results).toHaveLength(5);
  });
});

describe('fetchUrl', () => {
  it('rejects an invalid URL without fetching', async () => {
    const r = await fetchUrl('not a url');
    expect(r.source).toBe('failed');
    expect(r.error).toBe('invalid URL');
    expect(mockedFetch).not.toHaveBeenCalled();
  });

  it('returns failed when override is "provider"', async () => {
    setSearchSourceOverride('provider');
    const r = await fetchUrl('https://example.com');
    expect(r.source).toBe('failed');
    expect(r.error).toMatch(/no provider equivalent/);
    expect(mockedFetch).not.toHaveBeenCalled();
  });

  it('skips Jina when override is "duckduckgo"', async () => {
    setSearchSourceOverride('duckduckgo');
    mockedFetch.mockResolvedValue({
      ok: true,
      status: 200,
      body: '<html><body><main><p>Hello world</p></main></body></html>',
      finalUrl: 'https://example.com/article',
    });
    const r = await fetchUrl('https://example.com/article');
    expect(r.source).toBe('browser');
    // Only one fetch — Jina was bypassed.
    expect(mockedFetch).toHaveBeenCalledTimes(1);
    expect((mockedFetch.mock.calls[0] as unknown[])[0])
      .toBe('https://example.com/article');
  });

  it('uses Jina first in auto mode, succeeds without browser tier', async () => {
    mockedFetch.mockResolvedValueOnce({
      ok: true,
      status: 200,
      body: JSON.stringify({
        data: { title: 'T', content: '# Hello', url: 'https://example.com' },
      }),
    });
    const r = await fetchUrl('https://example.com');
    expect(r.source).toBe('jina');
    expect(r.content).toBe('# Hello');
    expect(mockedFetch).toHaveBeenCalledTimes(1);
  });

  it('falls through to browser tier when Jina returns non-ok', async () => {
    mockedFetch
      .mockResolvedValueOnce({ ok: false, status: 500, body: '' })  // jina
      .mockResolvedValueOnce({                                       // browser
        ok: true,
        status: 200,
        body: '<html><body><main><p>browser body</p></main></body></html>',
        finalUrl: 'https://example.com',
      });

    const r = await fetchUrl('https://example.com');
    expect(r.source).toBe('browser');
    expect(r.content).toContain('browser body');
    expect(mockedFetch).toHaveBeenCalledTimes(2);
  });

  it('marks Jina unavailable on 429 and falls through immediately on retry', async () => {
    mockedFetch
      .mockResolvedValueOnce({ ok: false, status: 429, body: '' })  // jina trips
      .mockResolvedValueOnce({                                       // browser
        ok: true,
        status: 200,
        body: '<html><body><main><p>x</p></main></body></html>',
        finalUrl: 'https://example.com',
      });

    const first = await fetchUrl('https://example.com');
    expect(first.source).toBe('browser');

    // Second call: circuit breaker should skip Jina entirely.
    mockedFetch.mockResolvedValueOnce({
      ok: true,
      status: 200,
      body: '<html><body><main><p>second</p></main></body></html>',
      finalUrl: 'https://example.com/2',
    });
    const second = await fetchUrl('https://example.com/2');
    expect(second.source).toBe('browser');
    // Jina call count should be 1 (only the original tripping call);
    // total fetches now: 3 (jina trip + browser + browser).
    expect(mockedFetch).toHaveBeenCalledTimes(3);
  });

  it('honours an already-tripped breaker on cold start', async () => {
    markJinaUnavailable();
    mockedFetch.mockResolvedValue({
      ok: true,
      status: 200,
      body: '<html><body><main><p>browser-only</p></main></body></html>',
      finalUrl: 'https://example.com',
    });
    const r = await fetchUrl('https://example.com');
    expect(r.source).toBe('browser');
    expect(mockedFetch).toHaveBeenCalledTimes(1);  // Jina skipped
  });

  it('returns failed when both tiers fail', async () => {
    mockedFetch
      .mockResolvedValueOnce({ ok: false, status: 502, body: '' })  // jina
      .mockResolvedValueOnce({                                       // browser
        ok: false, status: 500, body: '',
        error: 'http 500',
      });
    const r = await fetchUrl('https://example.com');
    expect(r.source).toBe('failed');
  });

  it('skips Jina and asks for cookies when same-origin and withCredentials=true', async () => {
    mockedFetch.mockResolvedValueOnce({
      ok: true,
      status: 200,
      body: '<html><body><main><p>logged-in body</p></main></body></html>',
      finalUrl: 'https://example.com/inbox',
    });
    const r = await fetchUrl('https://example.com/inbox', {
      activeTabUrl: 'https://example.com/dashboard',
      withCredentials: true,
    });
    expect(r.source).toBe('browser');
    expect(r.content).toContain('logged-in body');
    expect(mockedFetch).toHaveBeenCalledTimes(1);
    const init = (mockedFetch.mock.calls[0] as unknown[])[1] as
        {credentials?: string};
    expect(init.credentials).toBe('include_if_same_origin_active_tab');
  });

  it('keeps Jina-first path when same-origin but withCredentials=false (default)', async () => {
    mockedFetch.mockResolvedValueOnce({
      ok: true,
      status: 200,
      body: JSON.stringify({
        data: { title: 'T', content: '# x', url: 'https://example.com/inbox' },
      }),
    });
    const r = await fetchUrl('https://example.com/inbox', {
      activeTabUrl: 'https://example.com/dashboard',
      // withCredentials omitted → defaults to false
    });
    expect(r.source).toBe('jina');
    expect(mockedFetch).toHaveBeenCalledTimes(1);
    const init = (mockedFetch.mock.calls[0] as unknown[])[1] as
        {credentials?: string};
    expect(init?.credentials ?? 'omit').toBe('omit');
  });

  it('keeps Jina-first path when withCredentials=true but cross-origin', async () => {
    mockedFetch.mockResolvedValueOnce({
      ok: true,
      status: 200,
      body: JSON.stringify({
        data: { title: 'T', content: '# x', url: 'https://other.com' },
      }),
    });
    const r = await fetchUrl('https://other.com', {
      activeTabUrl: 'https://example.com/dashboard',
      withCredentials: true,
    });
    expect(r.source).toBe('jina');
    expect(mockedFetch).toHaveBeenCalledTimes(1);
    // Jina call must NOT request credentials.
    const init = (mockedFetch.mock.calls[0] as unknown[])[1] as
        {credentials?: string};
    expect(init?.credentials ?? 'omit').toBe('omit');
  });

  it('treats different ports as cross-origin (no cookie shortcut)', async () => {
    mockedFetch.mockResolvedValueOnce({
      ok: true,
      status: 200,
      body: JSON.stringify({
        data: { title: 'T', content: '# x', url: 'https://example.com:8443' },
      }),
    });
    const r = await fetchUrl('https://example.com:8443/api', {
      activeTabUrl: 'https://example.com/page',
      withCredentials: true,
    });
    // Different ports → not same-origin → Jina path runs.
    expect(r.source).toBe('jina');
  });

  it('does not enable cookie shortcut for non-http active tab', async () => {
    mockedFetch.mockResolvedValueOnce({
      ok: true,
      status: 200,
      body: JSON.stringify({
        data: { title: 'T', content: '# x', url: 'https://example.com' },
      }),
    });
    const r = await fetchUrl('https://example.com', {
      activeTabUrl: 'chrome://newtab/',
      withCredentials: true,
    });
    expect(r.source).toBe('jina');
  });
});
