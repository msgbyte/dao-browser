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
    registerToolRenderer(name, {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      render(params: string, result: any, isStreaming: boolean) {
        return renderDao(name, params, result, isStreaming);
      },
    });
  }
}
