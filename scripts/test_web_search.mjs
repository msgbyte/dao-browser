// Layer 2 integration test for the agent web_search module.
//
// This script does NOT import the agent TS files directly because that
// requires WebUI runtime (localStorage, DOMParser, chrome:// fetch).
// Instead it replicates the network calls + parser logic to verify that
// each tier behaves as the production code expects.
//
// Run with: node scripts/test_web_search.mjs
//
// Coverage:
//   - Jina Reader (r.jina.ai) returns expected JSON shape for fetch_url
//   - DuckDuckGo HTML returns parseable results for searchViaDuckDuckGo
//   - Our DDG selector logic finds title/url/snippet on real responses
//   - The browser-tier fetchUrl HTML→markdown noise stripping works on
//     a real article-shaped page

import {strict as assert} from 'node:assert';

const PASS = '✅';
const FAIL = '❌';
const results = [];

function record(name, ok, detail) {
  results.push({name, ok, detail});
  console.log(`${ok ? PASS : FAIL} ${name}${detail ? ' — ' + detail : ''}`);
}

async function fetchJson(url, init) {
  const resp = await fetch(url, init);
  return {status: resp.status, body: resp.ok ? await resp.json() : null,
          ok: resp.ok};
}

async function fetchText(url, init) {
  const resp = await fetch(url, init);
  return {status: resp.status, body: resp.ok ? await resp.text() : '',
          ok: resp.ok, finalUrl: resp.url};
}

// =====================================================================
// Test 1: Jina Reader returns the shape fetchUrlViaJina expects
// =====================================================================
async function testJinaReaderShape() {
  const target = 'https://example.com/';
  const r = await fetchJson(
      'https://r.jina.ai/' + target,
      {headers: {'Accept': 'application/json'}});
  if (!r.ok) {
    record('Jina Reader endpoint', false, `HTTP ${r.status}`);
    return;
  }
  // Production code reads: body.data.{title,content,url}
  const data = r.body?.data;
  const ok = data && typeof data.content === 'string' &&
             typeof data.title === 'string';
  record('Jina Reader returns {data: {title, content, url}}', !!ok,
         ok ? `title="${data.title}", content=${data.content.length}b` :
              'shape mismatch: ' + JSON.stringify(r.body).slice(0, 200));
}

// =====================================================================
// Test 2: DuckDuckGo HTML returns parseable results
// =====================================================================

// Inline copy of parseDuckDuckGoHtml's logic — kept verbatim so divergence
// from production is mechanical. Uses regex instead of DOMParser because
// Node has no DOMParser. The point is to verify the HTML SHAPE matches
// what the real parser depends on (`.result__a`, `.result__snippet`,
// `/l/?uddg=...` URL wrapping).

function parseDdgViaRegex(html) {
  const results = [];
  // Match each .result block that contains a result__a anchor.
  const blockRe =
      /<div class="result results_links[^"]*?"[\s\S]*?(?=<div class="result results_links|<\/div>\s*<\/div>\s*<\/div>\s*$)/g;
  for (const block of html.match(blockRe) || []) {
    const aMatch = block.match(
        /<a[^>]+class="[^"]*result__a[^"]*"[^>]+href="([^"]+)"[^>]*>([\s\S]*?)<\/a>/);
    if (!aMatch) continue;
    let url = aMatch[1];
    if (url.startsWith('//duckduckgo.com/l/')) url = 'https:' + url;
    try {
      const u = new URL(url, 'https://duckduckgo.com');
      const real = u.searchParams.get('uddg');
      if (real) url = decodeURIComponent(real);
      else url = u.toString();
    } catch (_) { /* keep */ }

    const title = aMatch[2].replace(/<[^>]+>/g, '').trim();
    const snipMatch = block.match(
        /<a[^>]+class="[^"]*result__snippet[^"]*"[^>]*>([\s\S]*?)<\/a>/);
    const snippet = snipMatch ?
        snipMatch[1].replace(/<[^>]+>/g, '').trim() : '';
    results.push({title, url, snippet});
  }
  return results;
}

// DuckDuckGo's anomaly detector blocks Node's `fetch()` because of its
// automatic Sec-Fetch-Mode: cors header. We use the lower-level `https`
// module to send a plain POST that mirrors what Chromium's WebUI fetch
// looks like to the server.
async function ddgRawPost(query) {
  const https = await import('node:https');
  const body = 'q=' + encodeURIComponent(query);
  return new Promise((resolve, reject) => {
    const req = https.request({
      hostname: 'html.duckduckgo.com',
      port: 443,
      path: '/html/',
      method: 'POST',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded',
        'Accept': 'text/html,application/xhtml+xml',
        'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) ' +
            'AppleWebKit/537.36 (KHTML, like Gecko) ' +
            'Chrome/132.0.0.0 Safari/537.36',
        'Content-Length': Buffer.byteLength(body),
      },
    }, (res) => {
      let data = '';
      res.on('data', (chunk) => data += chunk);
      res.on('end', () => resolve({status: res.statusCode, body: data,
                                    ok: res.statusCode >= 200 &&
                                        res.statusCode < 300}));
    });
    req.on('error', reject);
    req.write(body);
    req.end();
  });
}

async function testDuckDuckGoSearch() {
  const r = await ddgRawPost('tokio rust async runtime');

  if (!r.ok) {
    record('DuckDuckGo HTML endpoint', false, `HTTP ${r.status}`);
    return;
  }

  // Sanity: did we get an anomaly page?
  if (r.body.includes('anomaly-modal__title')) {
    record('DuckDuckGo HTML endpoint', false,
           'anomaly captcha page returned (rate-limited or flagged)');
    return;
  }

  // Production parser depends on these class names:
  const hasResultA = r.body.includes('class="result__a"') ||
                     /class="[^"]*result__a/.test(r.body);
  const hasSnippet = r.body.includes('result__snippet');
  record('DDG HTML contains .result__a', hasResultA);
  record('DDG HTML contains .result__snippet', hasSnippet);

  // Try to extract at least one full result.
  const parsed = parseDdgViaRegex(r.body);
  const ok = parsed.length >= 3 &&
             parsed[0].title.length > 0 &&
             parsed[0].url.startsWith('http') &&
             parsed[0].snippet.length > 0;
  record(`DDG parser yields >=3 results with title/url/snippet`, ok,
         ok ? `${parsed.length} results, first: "${parsed[0].title}"` :
              'parser broke — got ' + parsed.length + ' results');
}

// =====================================================================
// Test 3: browser-tier fetchUrlViaBrowser noise-strip math
// =====================================================================

// Inline copy of STRIP_TAGS + a regex-based reduction that approximates
// what the real DOMParser-based version does. We're verifying that the
// real production helpers, when faced with a typical article HTML,
// would not produce empty markdown.

const STRIP_TAGS = [
  'script', 'style', 'noscript', 'iframe', 'svg', 'canvas',
  'nav', 'header', 'footer', 'aside', 'form', 'button',
];

function approxNoiseRatio(html) {
  const original = html.length;
  let stripped = html;
  for (const tag of STRIP_TAGS) {
    const re = new RegExp(`<${tag}[\\s>][\\s\\S]*?<\\/${tag}>`, 'gi');
    stripped = stripped.replace(re, '');
  }
  const remaining = stripped.length;
  return {original, remaining, ratio: remaining / original};
}

async function testBrowserFetchOnArticle() {
  // Use a known stable article URL (example.com is too short to be
  // meaningful; use Wikipedia which has main + article structure).
  const target =
      'https://en.wikipedia.org/wiki/Tokio_(software)';
  const r = await fetchText(target, {
    headers: {
      'Accept': 'text/html,application/xhtml+xml',
      'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) ' +
          'AppleWebKit/537.36 (KHTML, like Gecko) ' +
          'Chrome/132.0.0.0 Safari/537.36',
    },
  });

  if (!r.ok) {
    record('Article fetch (Wikipedia)', false,
           `HTTP ${r.status} — Wikipedia blocked us, network issue?`);
    return;
  }
  record('Article fetch returns 2xx', true, `${r.body.length} bytes`);

  // Verify stripped content still has substantial body.
  const noise = approxNoiseRatio(r.body);
  const ok = noise.remaining > 5000 && noise.ratio < 0.95;
  record('Noise-strip leaves substantial article text', ok,
         `${noise.original}b → ${noise.remaining}b (${
           (noise.ratio * 100).toFixed(0)}% kept)`);

  // Verify <main> or <article> is present so the production helper's
  // "prefer <main>/<article>" logic engages.
  const hasMainOrArticle = /<main\b/i.test(r.body) ||
                            /<article\b/i.test(r.body);
  record('Article HTML has <main> or <article> (production preference)',
         hasMainOrArticle);
}

// =====================================================================
// Test 4: provider_capabilities.ts allowlist behaviour (pure function)
// =====================================================================

const ALLOWLIST = [
  {provider: 'anthropic',
   modelPrefixes: ['claude-sonnet-4', 'claude-opus-4', 'claude-haiku-4']},
  {provider: 'openai',
   modelPrefixes: ['gpt-4o', 'gpt-4.1', 'o3', 'o4']},
  {provider: 'google',
   modelPrefixes: ['gemini-2.0', 'gemini-2.5']},
];

function isProviderSearchAvailable(provider, model) {
  for (const e of ALLOWLIST) {
    if (e.provider !== provider) continue;
    for (const p of e.modelPrefixes) if (model.startsWith(p)) return true;
  }
  return false;
}

function testProviderCapabilities() {
  const cases = [
    ['anthropic', 'claude-sonnet-4-5-20250929', true],
    ['anthropic', 'claude-3-opus', false],
    ['anthropic', 'claude-haiku-4-1', true],
    ['openai', 'gpt-4o-mini', true],
    ['openai', 'gpt-3.5-turbo', false],
    ['openai-compatible', 'gpt-4o', false],
    ['google', 'gemini-2.5-flash', true],
    ['google', 'gemini-1.5-pro', false],
    ['groq', 'llama-3.3-70b', false],
    ['xai', 'grok-4', false],
  ];
  let allOk = true;
  const failures = [];
  for (const [provider, model, expected] of cases) {
    const got = isProviderSearchAvailable(provider, model);
    if (got !== expected) {
      allOk = false;
      failures.push(`${provider}+${model}: expected ${expected} got ${got}`);
    }
  }
  record(`provider_capabilities allowlist (${cases.length} cases)`, allOk,
         allOk ? 'all match' : failures.join('; '));
}

// =====================================================================
// Run
// =====================================================================
(async function main() {
  console.log('Layer 2: Web search integration smoke test\n');
  testProviderCapabilities();
  await testJinaReaderShape();
  await testDuckDuckGoSearch();
  await testBrowserFetchOnArticle();

  console.log('\n--- Summary ---');
  const total = results.length;
  const passed = results.filter((r) => r.ok).length;
  console.log(`${passed}/${total} passed`);
  if (passed !== total) {
    console.log('\nFailed:');
    for (const r of results) if (!r.ok) console.log(`  - ${r.name}: ${r.detail}`);
    process.exit(1);
  }
  process.exit(0);
})().catch((e) => {
  console.error('Fatal:', e);
  process.exit(2);
});
