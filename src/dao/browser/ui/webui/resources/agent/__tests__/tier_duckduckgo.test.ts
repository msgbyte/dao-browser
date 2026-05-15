// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { describe, expect, it } from 'vitest';

import { parseDuckDuckGoHtml } from '../web_search/tier_duckduckgo.js';

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

  it('passes through non-redirect URLs unchanged', () => {
    const html = `<html><body>${ddgResultRow(
      'Direct', 'https://example.org/page', 'snippet')}</body></html>`;
    const out = parseDuckDuckGoHtml(html);
    expect(out[0].url).toBe('https://example.org/page');
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
});
