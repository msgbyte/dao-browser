// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Custom pi-web-ui tool-call renderer. Collapses tool Input/Output by
// default — surface just the tool name + status indicator in the chat
// stream so tool chatter does not drown out the assistant's own text.
// A settings toggle (`dao_tool_call_show_details`, read at render time)
// lets the user expand every call for debugging.
//
// We use a native <details>/<summary> element so the toggle is keyboard-
// accessible and free of any JS wiring. The `open` attribute is set on
// first render from the setting; subsequent user clicks on the summary
// take over and stay in whatever state the user last left them in.

// eslint-disable-next-line @typescript-eslint/no-explicit-any
import * as pi from './vendor/pi_runtime_bundle.js';

const SHOW_DETAILS_KEY = 'dao_tool_call_show_details';

function shouldDefaultExpand(): boolean {
  try {
    return localStorage.getItem(SHOW_DETAILS_KEY) === 'true';
  } catch (_) {
    return false;
  }
}

function prettyJson(input: unknown): string {
  if (input === null || input === undefined) return '';
  if (typeof input === 'string') {
    try {
      return JSON.stringify(JSON.parse(input), null, 2);
    } catch {
      return input;
    }
  }
  try {
    return JSON.stringify(input, null, 2);
  } catch {
    return String(input);
  }
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function extractOutput(result: any): {text: string; language: string} {
  if (!result) return {text: '', language: 'text'};
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const text = (result.content || [])
                   .filter((c: {type: string}) => c.type === 'text')
                   // eslint-disable-next-line @typescript-eslint/no-explicit-any
                   .map((c: any) => c.text)
                   .join('\n') ||
      '(no output)';
  try {
    const parsed = JSON.parse(text);
    return {text: JSON.stringify(parsed, null, 2), language: 'json'};
  } catch {
    return {text, language: 'text'};
  }
}

const SOURCE_BADGE: Record<string, string> = {
  provider: '🌐 Provider',
  jina: '⚡ Jina',
  duckduckgo: '🦆 DuckDuckGo',
  failed: '❌ Failed',
  browser: '📄 Browser',
};

function safeJson<T>(text: string): T|null {
  try { return JSON.parse(text) as T; } catch { return null; }
}

function domainOf(url: string): string {
  try { return new URL(url).hostname; } catch { return url; }
}

interface RenderedSearch {
  source: string;
  query: string;
  results: Array<{title: string; url: string; snippet: string}>;
  error?: string;
}

interface RenderedFetch {
  source: string;
  url: string;
  title: string;
  content: string;
  error?: string;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function renderWebSearch(this: unknown, _params: string, result: any,
                          isStreaming: boolean):
    {content: unknown; isCustom: boolean} {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const html = (pi as any).html;
  const open = shouldDefaultExpand();
  const state = result ? (result.isError ? 'error' : 'complete') :
                         (isStreaming ? 'inprogress' : 'complete');

  // Result payload arrives as content[].text JSON.
  const text = (result?.content || [])
                   .filter((c: {type: string}) => c.type === 'text')
                   // eslint-disable-next-line @typescript-eslint/no-explicit-any
                   .map((c: any) => c.text).join('\n');
  const parsed = text ? safeJson<RenderedSearch>(text) : null;

  if (!parsed) {
    // Streaming or failed-to-parse — fall back to the generic renderer.
    return renderDao('web_search', _params, result, isStreaming);
  }

  const badge = SOURCE_BADGE[parsed.source] ?? parsed.source;
  const summaryLabel =
      `${badge}  Searched: "${parsed.query}"`;

  return {
    content: html`
      <details class="dao-tool-call dao-tool-call-${state} dao-search-card"
               ?open=${open}>
        <summary class="dao-tool-call-summary">
          <span class="dao-tool-call-dot" aria-hidden="true"></span>
          <span class="dao-tool-call-name">${summaryLabel}</span>
          <svg class="dao-tool-call-chevron" aria-hidden="true"
              xmlns="http://www.w3.org/2000/svg"
              width="14" height="14" viewBox="0 0 24 24" fill="none"
              stroke="currentColor" stroke-width="2"
              stroke-linecap="round" stroke-linejoin="round">
            <path d="m6 9 6 6 6-6"></path>
          </svg>
        </summary>
        <div class="dao-tool-call-body dao-search-card-body">
          ${parsed.error ? html`
            <div class="dao-search-error">${parsed.error}</div>
          ` : ''}
          ${parsed.results.length === 0 && !parsed.error ? html`
            <div class="dao-search-empty">No results.</div>
          ` : ''}
          <ul class="dao-search-results">
            ${parsed.results.map((r) => html`
              <li>
                <a href=${r.url} target="_blank"
                   rel="noopener noreferrer"
                   class="dao-search-result-title">${r.title}</a>
                <span class="dao-search-result-domain">
                  ${domainOf(r.url)}
                </span>
                <div class="dao-search-result-snippet">${r.snippet}</div>
              </li>
            `)}
          </ul>
        </div>
      </details>
    `,
    isCustom: true,
  };
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function renderFetchUrl(this: unknown, _params: string, result: any,
                         isStreaming: boolean):
    {content: unknown; isCustom: boolean} {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const html = (pi as any).html;
  const open = shouldDefaultExpand();
  const state = result ? (result.isError ? 'error' : 'complete') :
                         (isStreaming ? 'inprogress' : 'complete');

  const text = (result?.content || [])
                   .filter((c: {type: string}) => c.type === 'text')
                   // eslint-disable-next-line @typescript-eslint/no-explicit-any
                   .map((c: any) => c.text).join('\n');
  const parsed = text ? safeJson<RenderedFetch>(text) : null;

  if (!parsed) {
    return renderDao('fetch_url', _params, result, isStreaming);
  }

  const badge = SOURCE_BADGE[parsed.source] ?? parsed.source;
  const summaryLabel = parsed.title ?
      `${badge}  Read: ${parsed.title}` :
      `${badge}  Read: ${parsed.url}`;

  return {
    content: html`
      <details class="dao-tool-call dao-tool-call-${state} dao-fetch-card"
               ?open=${open}>
        <summary class="dao-tool-call-summary">
          <span class="dao-tool-call-dot" aria-hidden="true"></span>
          <span class="dao-tool-call-name">${summaryLabel}</span>
          <svg class="dao-tool-call-chevron" aria-hidden="true"
              xmlns="http://www.w3.org/2000/svg"
              width="14" height="14" viewBox="0 0 24 24" fill="none"
              stroke="currentColor" stroke-width="2"
              stroke-linecap="round" stroke-linejoin="round">
            <path d="m6 9 6 6 6-6"></path>
          </svg>
        </summary>
        <div class="dao-tool-call-body">
          ${parsed.error ? html`
            <div class="dao-search-error">${parsed.error}</div>
          ` : html`
            <code-block .code=${parsed.content} language="markdown">
            </code-block>
          `}
        </div>
      </details>
    `,
    isCustom: true,
  };
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function renderDao(this: unknown, toolName: string, params: string, result: any,
                   isStreaming: boolean): {content: unknown; isCustom: boolean} {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const html = (pi as any).html;
  const state = result ? (result.isError ? 'error' : 'complete') :
                         (isStreaming ? 'inprogress' : 'complete');
  const paramsText = params ? prettyJson(params) : '';
  const output = extractOutput(result);
  const open = shouldDefaultExpand();

  const statusClass = 'dao-tool-call dao-tool-call-' + state;
  const summaryLabel = isStreaming && !result ?
      (paramsText ? toolName : 'Preparing ' + toolName + '...') :
      toolName;

  return {
    content: html`
      <details class=${statusClass} ?open=${open}>
        <summary class="dao-tool-call-summary">
          <span class="dao-tool-call-dot" aria-hidden="true"></span>
          <span class="dao-tool-call-name">${summaryLabel}</span>
          <svg class="dao-tool-call-chevron" aria-hidden="true"
              xmlns="http://www.w3.org/2000/svg"
              width="14" height="14" viewBox="0 0 24 24" fill="none"
              stroke="currentColor" stroke-width="2"
              stroke-linecap="round" stroke-linejoin="round">
            <path d="m6 9 6 6 6-6"></path>
          </svg>
        </summary>
        <div class="dao-tool-call-body">
          ${paramsText ? html`
            <div class="dao-tool-call-section">
              <div class="dao-tool-call-label">Input</div>
              <code-block .code=${paramsText} language="json"></code-block>
            </div>
          ` : ''}
          ${result ? html`
            <div class="dao-tool-call-section">
              <div class="dao-tool-call-label">Output</div>
              <code-block .code=${output.text} language=${output.language}></code-block>
            </div>
          ` : ''}
        </div>
      </details>
    `,
    isCustom: true,
  };
}

let registered_ = false;

export function registerDaoToolRenderers(toolNames: string[]) {
  if (registered_) return;
  registered_ = true;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const registerToolRenderer = (pi as any).registerToolRenderer;
  if (typeof registerToolRenderer !== 'function') return;
  for (const name of toolNames) {
    if (name === 'web_search') {
      registerToolRenderer(name, {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        render(params: string, result: any, isStreaming: boolean) {
          return renderWebSearch(params, result, isStreaming);
        },
      });
      continue;
    }
    if (name === 'fetch_url') {
      registerToolRenderer(name, {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        render(params: string, result: any, isStreaming: boolean) {
          return renderFetchUrl(params, result, isStreaming);
        },
      });
      continue;
    }
    registerToolRenderer(name, {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      render(params: string, result: any, isStreaming: boolean) {
        return renderDao(name, params, result, isStreaming);
      },
    });
  }
}
