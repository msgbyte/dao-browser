// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Bottom-tier search + fetch_url for the agent web_search subsystem.
//
// Search:    DuckDuckGo HTML endpoint, server-rendered, no JS gate.
//            We fetch the page and parse with DOMParser. Selectors
//            target the stable .result__* classes; if DDG ever
//            restructures we surface `parse` failures so the
//            regression is visible in the LLM response and the console.
//
// fetch_url: privileged WebUI fetch + a small DOM-to-markdown walker
//            (`stripNoise` + `domToMarkdown`). The agent already
//            vendors `@mozilla/readability` and `turndown` for the
//            in-tab "convert page to markdown" feature, but those
//            bundles ship as IIFE strings designed for `executeScript`
//            injection — they are not directly consumable here. The
//            walker below is intentionally simple: this is the safety
//            net behind Jina Reader, which is the primary fetch path.

import {callNativeFetch} from '../agent_bridge.js';
import type {FetchResponse, SearchResponse, SearchResult} from './types.js';

const SEARCH_ENDPOINT = 'https://html.duckduckgo.com/html/';

export function parseDuckDuckGoHtml(html: string): SearchResult[] {
  const doc = new DOMParser().parseFromString(html, 'text/html');
  const nodes = doc.querySelectorAll('.result');
  if (nodes.length === 0) {
    // DDG sometimes returns an "anomaly" page with no .result nodes.
    // Treat as parse failure so the caller can return source: 'failed'.
    throw new Error('parse');
  }

  const out: SearchResult[] = [];
  for (const node of Array.from(nodes)) {
    const a = node.querySelector('.result__a') as HTMLAnchorElement|null;
    const snip = node.querySelector('.result__snippet');
    if (!a) continue;

    // DDG wraps real URLs in /l/?uddg=<encoded>
    let url = a.getAttribute('href') ?? '';
    if (url.startsWith('//duckduckgo.com/l/')) {
      url = 'https:' + url;
    }
    try {
      const u = new URL(url, 'https://duckduckgo.com');
      const real = u.searchParams.get('uddg');
      if (real) url = decodeURIComponent(real);
      else url = u.toString();
    } catch { /* keep as-is */ }

    out.push({
      title: (a.textContent ?? '').trim(),
      url,
      snippet: (snip?.textContent ?? '').trim(),
    });
  }
  return out;
}

export async function searchViaDuckDuckGo(
    query: string, maxResults: number): Promise<SearchResponse> {
  const form = new URLSearchParams({q: query});
  const r = await callNativeFetch(SEARCH_ENDPOINT, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/x-www-form-urlencoded',
      'Accept': 'text/html,application/xhtml+xml',
    },
    body: form.toString(),
  });

  if (!r.ok) {
    return {
      source: 'failed', query, results: [],
      error: r.error ?? ('http ' + r.status),
    };
  }

  let parsed: SearchResult[];
  try {
    parsed = parseDuckDuckGoHtml(r.body);
  } catch {
    console.warn('[web_search] DuckDuckGo HTML parse failed');
    return {
      source: 'failed', query, results: [],
      error: 'parse: DuckDuckGo HTML structure changed',
    };
  }

  return {
    source: 'duckduckgo',
    query,
    results: parsed.slice(0, maxResults),
  };
}

// Tags whose content carries no value for an LLM reading an article.
// We strip them whole-tree before walking the DOM.
const STRIP_TAGS = new Set([
  'SCRIPT', 'STYLE', 'NOSCRIPT', 'IFRAME', 'SVG', 'CANVAS',
  'NAV', 'HEADER', 'FOOTER', 'ASIDE', 'FORM', 'BUTTON',
]);

function stripNoise(root: Element): void {
  // Walk in a stable order: collect first, remove after, so we don't
  // mutate the live NodeList we're iterating.
  const victims: Element[] = [];
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_ELEMENT);
  let n: Node|null = walker.currentNode;
  while (n) {
    if (n.nodeType === Node.ELEMENT_NODE &&
        STRIP_TAGS.has((n as Element).tagName)) {
      victims.push(n as Element);
    }
    n = walker.nextNode();
  }
  for (const v of victims) v.remove();
}

// Turn a cleaned DOM into lightweight markdown:
//  - h1..h6 -> '# ' .. '###### '
//  - p, li  -> blank-line-separated paragraphs (li prefixed with '- ')
//  - a      -> [text](href)
// Ignores everything else; collapses whitespace.
function domToMarkdown(root: Element): string {
  const out: string[] = [];

  function walk(node: Node, listPrefix: string|null) {
    if (node.nodeType === Node.TEXT_NODE) {
      const t = (node.textContent ?? '').replace(/\s+/g, ' ');
      if (t.trim().length > 0) out.push(t);
      return;
    }
    if (node.nodeType !== Node.ELEMENT_NODE) return;

    const el = node as Element;
    const tag = el.tagName;

    if (/^H[1-6]$/.test(tag)) {
      const level = Number(tag[1]);
      out.push('\n\n' + '#'.repeat(level) + ' ' +
          (el.textContent ?? '').trim() + '\n\n');
      return;
    }
    if (tag === 'P') {
      out.push('\n\n');
      for (const c of Array.from(el.childNodes)) walk(c, null);
      out.push('\n\n');
      return;
    }
    if (tag === 'LI') {
      out.push('\n' + (listPrefix ?? '- '));
      for (const c of Array.from(el.childNodes)) walk(c, null);
      return;
    }
    if (tag === 'UL' || tag === 'OL') {
      out.push('\n');
      const prefix = tag === 'OL' ? '1. ' : '- ';
      for (const c of Array.from(el.childNodes)) walk(c, prefix);
      out.push('\n');
      return;
    }
    if (tag === 'A') {
      const href = (el as HTMLAnchorElement).getAttribute('href') ?? '';
      const text = (el.textContent ?? '').trim();
      if (href && text) {
        out.push('[' + text + '](' + href + ')');
      } else if (text) {
        out.push(text);
      }
      return;
    }
    if (tag === 'BR') {
      out.push('\n');
      return;
    }

    // Generic container — recurse.
    for (const c of Array.from(el.childNodes)) walk(c, listPrefix);
  }

  walk(root, null);
  return out.join('').replace(/\n{3,}/g, '\n\n').trim();
}

export async function fetchUrlViaBrowser(
    targetUrl: string): Promise<FetchResponse> {
  const r = await callNativeFetch(targetUrl, {
    headers: {'Accept': 'text/html,application/xhtml+xml'},
  });
  if (!r.ok) {
    return {
      source: 'failed', url: r.finalUrl || targetUrl, title: '',
      content: '', error: r.error ?? ('http ' + r.status),
    };
  }

  const doc = new DOMParser().parseFromString(r.body, 'text/html');
  const title = doc.title || '';
  const body = doc.body;
  if (!body) {
    return {
      source: 'failed', url: r.finalUrl || targetUrl, title,
      content: '', error: 'no-body',
    };
  }

  // Prefer <main> or <article> if present — they tend to contain just
  // the article. Otherwise use <body>.
  const main = body.querySelector('main, article') as Element|null;
  const root = main ?? body;

  stripNoise(root);
  const markdown = domToMarkdown(root);

  if (!markdown) {
    return {
      source: 'failed', url: r.finalUrl || targetUrl, title,
      content: '', error: 'no-content',
    };
  }

  return {
    source: 'browser',
    url: r.finalUrl || targetUrl,
    title,
    content: markdown,
  };
}
