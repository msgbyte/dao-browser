// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

vi.mock('../agent_bridge.js', () => ({
  callNativeFetch: vi.fn(),
}));

import {callNativeFetch} from '../agent_bridge.js';
import {
  fetchUrlViaBrowser,
  parseDuckDuckGoHtml,
  searchViaDuckDuckGo,
} from '../web_search/tier_duckduckgo.js';

const mockedFetch = callNativeFetch as unknown as ReturnType<typeof vi.fn>;

function ddgResultRow(
    title: string, href: string, snippet: string): string {
  return `
    <div class="result">
      <a class="result__a" href="${href}">${title}</a>
      <span class="result__snippet">${snippet}</span>
    </div>
  `;
}

describe('parseDuckDuckGoHtml', () => {
  it('extracts title, snippet and decodes uddg redirect URLs', () => {
    const html = `
      <html><body>
        ${ddgResultRow(
          'Example Domain',
          '//duckduckgo.com/l/?uddg=' + encodeURIComponent('https://example.com/x'),
          'A short blurb.')}
      </body></html>`;
    const out = parseDuckDuckGoHtml(html);
    expect(out).toHaveLength(1);
    expect(out[0]).toEqual({
      title: 'Example Domain',
      url: 'https://example.com/x',
      snippet: 'A short blurb.',
    });
  });

  it('preserves encoded characters inside decoded uddg target URLs', () => {
    const targetUrl = 'https://example.com/search?q=a%2Fb&next=%252Fdashboard';
    const html = `
      <html><body>
        ${ddgResultRow(
          'Encoded Query',
          '//duckduckgo.com/l/?uddg=' + encodeURIComponent(targetUrl),
          'A result with encoded query values.')}
      </body></html>`;

    const out = parseDuckDuckGoHtml(html);

    expect(out[0].url).toBe(targetUrl);
  });

  it('decodes root-relative uddg redirect URLs', () => {
    const html = `<html><body>${ddgResultRow(
      'Root Relative Redirect',
      '/l/?uddg=' + encodeURIComponent('https://example.net/path?a=1'),
      'snippet')}</body></html>`;

    const out = parseDuckDuckGoHtml(html);

    expect(out[0].url).toBe('https://example.net/path?a=1');
  });

  it('passes through non-redirect URLs unchanged', () => {
    const html = `<html><body>${ddgResultRow(
      'Direct', 'https://example.org/page', 'snippet')}</body></html>`;
    const out = parseDuckDuckGoHtml(html);
    expect(out[0].url).toBe('https://example.org/page');
  });

  it('normalizes relative non-redirect URLs against DuckDuckGo', () => {
    const html = `<html><body>${ddgResultRow(
      'DuckDuckGo Help', '/duckduckgo-help-pages/results/syntax/', 'snippet')}
      </body></html>`;

    const out = parseDuckDuckGoHtml(html);

    expect(out[0].url)
      .toBe('https://duckduckgo.com/duckduckgo-help-pages/results/syntax/');
  });

  it('keeps malformed href values unchanged', () => {
    const html = `<html><body>${ddgResultRow(
      'Malformed', 'http://[::1', 'snippet')}</body></html>`;

    const out = parseDuckDuckGoHtml(html);

    expect(out[0].url).toBe('http://[::1');
  });

  it('honours maxResults', () => {
    const rows = Array.from({ length: 5 }, (_, i) =>
      ddgResultRow(`t${i}`, `https://e.com/${i}`, `s${i}`));
    const out = parseDuckDuckGoHtml(`<body>${rows.join('')}</body>`, 3);
    expect(out).toHaveLength(3);
    expect(out.map(r => r.title)).toEqual(['t0', 't1', 't2']);
  });

  it('throws "parse" when the result container is missing', () => {
    expect(() => parseDuckDuckGoHtml('<html><body>nothing</body></html>'))
      .toThrow('parse');
  });

  it('skips a row that has a snippet but no anchor', () => {
    const html = `
      <html><body>
        <div class="result">
          <span class="result__snippet">orphan</span>
        </div>
        ${ddgResultRow('ok', 'https://example.com', 'real')}
      </body></html>`;
    const out = parseDuckDuckGoHtml(html);
    expect(out).toHaveLength(1);
    expect(out[0].title).toBe('ok');
  });

  it('trims whitespace from title and snippet', () => {
    const html = `
      <html><body>
        <div class="result">
          <a class="result__a" href="https://example.com">
            Padded Title
          </a>
          <span class="result__snippet">
            padded snippet
          </span>
        </div>
      </body></html>`;
    const out = parseDuckDuckGoHtml(html);
    expect(out[0].title).toBe('Padded Title');
    expect(out[0].snippet).toBe('padded snippet');
  });

  it('uses an empty snippet when a result row has no snippet node', () => {
    const html = `
      <html><body>
        <div class="result">
          <a class="result__a" href="https://example.com">No Snippet</a>
        </div>
      </body></html>`;

    const out = parseDuckDuckGoHtml(html);

    expect(out[0]).toEqual({
      title: 'No Snippet',
      url: 'https://example.com/',
      snippet: '',
    });
  });
});

describe('fetchUrlViaBrowser', () => {
  beforeEach(() => {
    mockedFetch.mockReset();
  });

  it('prefers article content, strips page chrome, and converts basic HTML to markdown',
     async () => {
    mockedFetch.mockResolvedValue({
      ok: true,
      status: 200,
      finalUrl: 'https://example.com/final',
      body: `
        <html>
          <head><title>Fallback Title</title></head>
          <body>
            <nav>Global nav</nav>
            <article>
              <h1>Article Title</h1>
              <p>Hello <a href="https://example.com/ref">reference</a>.</p>
              <ul><li>First</li><li>Second</li></ul>
              <script>window.noise = true;</script>
            </article>
            <aside>Related links</aside>
          </body>
        </html>`,
    });

    const out = await fetchUrlViaBrowser('https://example.com/original');

    expect(out).toMatchObject({
      source: 'browser',
      url: 'https://example.com/final',
      title: 'Fallback Title',
    });
    expect(out.content).toContain('# Article Title');
    expect(out.content).toContain(
        'Hello [reference](https://example.com/ref).');
    expect(out.content).toContain('- First');
    expect(out.content).toContain('- Second');
    expect(out.content).not.toContain('Global nav');
    expect(out.content).not.toContain('Related links');
    expect(out.content).not.toContain('window.noise');
  });

  it('returns no-content when the cleaned body has no readable text',
     async () => {
    mockedFetch.mockResolvedValue({
      ok: true,
      status: 200,
      body: `
        <html>
          <head><title>Empty</title></head>
          <body>
            <nav>Navigation</nav>
            <script>console.log('noise')</script>
            <style>body { color: red; }</style>
          </body>
        </html>`,
    });

    const out = await fetchUrlViaBrowser('https://example.com/empty');

    expect(out).toMatchObject({
      source: 'failed',
      url: 'https://example.com/empty',
      title: 'Empty',
      content: '',
      error: 'no-content',
    });
  });

  it('requests same-origin active-tab credentials only when asked',
     async () => {
    mockedFetch.mockResolvedValue({
      ok: true,
      status: 200,
      body: '<html><body><main><p>Private page</p></main></body></html>',
    });

    await fetchUrlViaBrowser('https://example.com/private', {
      includeCredentialsIfSameOriginActiveTab: true,
    });
    await fetchUrlViaBrowser('https://example.com/public');

    expect((mockedFetch.mock.calls[0] as unknown[])[1])
      .toMatchObject({credentials: 'include_if_same_origin_active_tab'});
    expect((mockedFetch.mock.calls[1] as unknown[])[1])
      .toMatchObject({credentials: 'omit'});
  });
});

describe('searchViaDuckDuckGo', () => {
  beforeEach(() => {
    mockedFetch.mockReset();
  });

  it('reports an anomaly page separately from HTML structure changes',
     async () => {
    mockedFetch.mockResolvedValue({
      ok: true,
      status: 202,
      body: `
        <html>
          <body>
            <div class="anomaly-modal__title">Please verify</div>
          </body>
        </html>`,
    });

    const out = await searchViaDuckDuckGo('tokio rust async runtime', 5);

    expect(out).toMatchObject({
      source: 'failed',
      query: 'tokio rust async runtime',
      results: [],
      error: 'DuckDuckGo returned an anomaly verification page',
    });
  });
});
