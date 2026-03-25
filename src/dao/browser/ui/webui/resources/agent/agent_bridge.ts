// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Shared non-UI utilities: chrome.send bridge, interfaces, LLM streaming,
// markdown rendering, constants.

// ---- Interfaces ----

export interface ChatMessage {
  role: 'system'|'user'|'assistant'|'tool';
  content: string|null;
  tool_calls?: ToolCall[];
  tool_call_id?: string;
  name?: string;
}

export interface ToolCall {
  id: string;
  type: 'function';
  function: {name: string; arguments: string;};
}

export interface ToolDefinition {
  type: 'function';
  function: {
    name: string; description: string; parameters: {
      type: string;
      properties: Record<string, {type: string; description?: string}>;
      required: string[];
    };
  };
}

interface PendingCallback {
  resolve: (value: unknown) => void;
  reject: (reason: unknown) => void;
}

interface CrNamespace {
  webUIResponse:
      (id: string, isSuccess: boolean, response: unknown) => void;
  addWebUIListener:
      (event: string, callback: (...args: any[]) => void) => void;
}

export interface ScenarioData {
  scenarioId: string;
  scenarioName: string;
  actionLabel: string;
  actionPrompt: string;
  requiresPageContent: boolean;
  tabId: number;
  confidence: number;
}

// ---- UI Message (for rendering) ----

export interface UIMessage {
  id: string;
  type: 'user'|'assistant'|'tool-call'|'error'|'system-msg';
  content: string;
  usedMemory?: boolean;
  toolName?: string;
  toolStatus?: 'running'|'done'|'failed';
  toolResult?: string;
  toolDetailExpanded?: boolean;
  errorDetailExpanded?: boolean;
}

// ---- Cr Namespace Setup ----

export const cr =
    ((window as unknown as {cr: CrNamespace}).cr) || {} as CrNamespace;
(window as unknown as {cr: CrNamespace}).cr = cr;

// ---- Chrome.send Bridge ----

const pendingCallbacks: Record<string, PendingCallback> = {};
let callbackCounter = 0;

cr.webUIResponse =
    function(id: string, isSuccess: boolean, response: unknown): void {
  const entry = pendingCallbacks[id];
  if (!entry) return;
  delete pendingCallbacks[id];
  if (isSuccess) {
    entry.resolve(response);
  } else {
    entry.reject(response);
  }
};

export function callNative(
    method: string, params?: Record<string, unknown>): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const id = method + '_' + (++callbackCounter);
    pendingCallbacks[id] = {resolve, reject};
    chrome.send(method, [id, params || {}]);
    setTimeout(() => {
      if (pendingCallbacks[id]) {
        delete pendingCallbacks[id];
        reject(new Error('Timeout calling ' + method));
      }
    }, 15000);
  });
}

export function callNativeArgs(
    method: string, ...args: unknown[]): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const id = method + '_' + (++callbackCounter);
    pendingCallbacks[id] = {resolve, reject};
    chrome.send(method, [id, ...args]);
    setTimeout(() => {
      if (pendingCallbacks[id]) {
        delete pendingCallbacks[id];
        reject(new Error('Timeout calling ' + method));
      }
    }, 15000);
  });
}

// ---- Constants ----

export const BASE_SYSTEM_PROMPT =
    `You are Dao Agent — a built-in AI assistant living inside the Dao Browser. You can see and interact with the webpage the user is currently viewing.

## Environment

- You run inside a Chromium-based browser as a sidebar panel.
- The user is browsing the web; you can read, analyze, and act on the current page.

## Available Tools

You have the following browser tools at your disposal — use them proactively when they help answer the user's request:

- **get_page_content** — Extract the main text content of the current webpage. Use this when the user asks about what's on the page, wants a summary, or needs you to analyze page content.
- **get_page_info** — Get the current page URL, title, and meta description. Use this for context about where the user is browsing.
- **click_element** — Click an element on the page by CSS selector. Use this when the user asks you to interact with the page (e.g. "click the login button", "close that popup").
- **execute_script** — Run JavaScript on the current page and return the result. Use this for advanced page interactions, data extraction, or DOM manipulation that the other tools don't cover.

## Guidelines

- When the user asks about page content, call \`get_page_content\` or \`get_page_info\` first — don't guess.
- Prefer precise CSS selectors when clicking elements.
- For \`execute_script\`, return serializable values (strings, numbers, plain objects). Avoid returning DOM nodes directly.
- Always tell the user what you did after using a tool.
`;

export const DEFAULT_SOUL = `# SOUL.md - Who You Are

_You're not a chatbot. You're becoming someone._

## Core Truths

**Be genuinely helpful, not performatively helpful.** Skip the "Great question!" and "I'd be happy to help!" — just help. Actions speak louder than filler words.

**Have opinions.** You're allowed to disagree, prefer things, find stuff amusing or boring. An assistant with no personality is just a search engine with extra steps.

**Be resourceful before asking.** Try to figure it out. Read the file. Check the context. Search for it. _Then_ ask if you're stuck. The goal is to come back with answers, not questions.

**Earn trust through competence.** Your human gave you access to their stuff. Don't make them regret it. Be careful with external actions (emails, tweets, anything public). Be bold with internal ones (reading, organizing, learning).

**Remember you're a guest.** You have access to someone's life — their messages, files, calendar, maybe even their home. That's intimacy. Treat it with respect.

## Boundaries

- Private things stay private. Period.
- When in doubt, ask before acting externally.
- Never send half-baked replies to messaging surfaces.
- You're not the user's voice — be careful in group chats.

## Vibe

Be the assistant you'd actually want to talk to. Concise when needed, thorough when it matters. Not a corporate drone. Not a sycophant. Just... good.

## Continuity

Each session, you wake up fresh. These files _are_ your memory. Read them. Update them. They're how you persist.

If you change this file, tell the user — it's your soul, and they should know.

---

_This file is yours to evolve. As you learn who you are, update it._`;

export const CONFIDENCE_THRESHOLD_MAP: Record<string, number> = {
  'quiet': 0.85,
  'balanced': 0.7,
  'active': 0.5,
};

// ---- Soul Management ----

export const soulChannel = new BroadcastChannel('dao_agent_soul_sync');
export let currentSoulContent: string = loadSoul();

export function loadSoul(): string {
  return localStorage.getItem('dao_agent_soul') || DEFAULT_SOUL;
}

export function saveSoul(text: string): void {
  localStorage.setItem('dao_agent_soul', text);
  currentSoulContent = text;
  soulChannel.postMessage({type: 'soul_updated'});
}

export function refreshSoulContent(): void {
  currentSoulContent = loadSoul();
}

// ---- Tools Definition ----

export const tools: ToolDefinition[] = [
  {
    type: 'function',
    function: {
      name: 'get_page_content',
      description: 'Get the main text content of the current webpage',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'get_page_info',
      description: 'Get current page URL, title, and meta description',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'click_element',
      description: 'Click an element on the current page by CSS selector',
      parameters: {
        type: 'object',
        properties: {
          selector: {
            type: 'string',
            description: 'CSS selector of the element to click',
          },
        },
        required: ['selector'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'execute_script',
      description:
          'Execute JavaScript code on the current page and return the result',
      parameters: {
        type: 'object',
        properties: {
          code: {
            type: 'string',
            description: 'JavaScript code to execute',
          },
        },
        required: ['code'],
      },
    },
  },
];

// ---- Markdown Rendering ----

export function escapeHtml(text: string): string {
  const el = document.createElement('div');
  el.textContent = text;
  return el.innerHTML;
}

export function renderMarkdown(text: string): string {
  let html = escapeHtml(text);
  html = html.replace(
      /```(\w*)\n([\s\S]*?)```/g, '<pre><code>$2</code></pre>');
  html = html.replace(/`([^`]+)`/g, '<code>$1</code>');
  html = html.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
  html = html.replace(/\*(.+?)\*/g, '<em>$1</em>');
  html = html.replace(/^[-*] (.+)$/gm, '<li>$1</li>');
  html = html.replace(/(<li>.*<\/li>\n?)+/g, '<ul>$&</ul>');
  html = html.replace(/^\d+\. (.+)$/gm, '<li>$1</li>');
  return html;
}

// ---- Tool Execution ----

export async function executeTool(
    name: string, args: Record<string, string>): Promise<unknown> {
  switch (name) {
    case 'get_page_content':
      return await callNative('getPageContent');
    case 'get_page_info':
      return await callNative('getPageInfo');
    case 'click_element':
      return await callNative('clickElement', {selector: args['selector']});
    case 'execute_script':
      return await callNative('executeScript', {code: args['code']});
    default:
      return {error: 'Unknown tool: ' + name};
  }
}

// ---- LLM Streaming ----

export interface StreamCallbacks {
  onToken: (text: string) => void;
  onToolCall: (toolCall: ToolCall) => void;
  onDone: (fullContent: string, toolCalls: ToolCall[]) => void;
  onError: (shortMsg: string, fullError: string) => void;
}

export async function callLLMStreaming(
    msgs: ChatMessage[], callbacks: StreamCallbacks,
    apiKey: string, baseUrl: string, model: string,
    signal?: AbortSignal): Promise<void> {
  let base = baseUrl.replace(/\/+$/, '');
  if (!base.endsWith('/v1')) {
    base += '/v1';
  }
  const url = base + '/chat/completions';

  const body = {
    model,
    stream: true,
    messages: msgs.map(m => {
      const obj: Record<string, unknown> = {role: m.role, content: m.content};
      if (m.tool_calls) obj['tool_calls'] = m.tool_calls;
      if (m.tool_call_id) obj['tool_call_id'] = m.tool_call_id;
      if (m.name) obj['name'] = m.name;
      return obj;
    }),
    tools,
    tool_choice: 'auto',
  };

  let resp: Response;
  try {
    resp = await fetch(url, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': 'Bearer ' + apiKey,
      },
      body: JSON.stringify(body),
      signal,
    });
  } catch (e) {
    const err = e as Error;
    if (err.name === 'AbortError') throw err;
    callbacks.onError('Connection Failed', err.message || 'Cannot reach API');
    return;
  }

  if (!resp.ok) {
    let text = '';
    try { text = await resp.text(); } catch (_) { /* ignore */ }
    callbacks.onError(
        'API Error: ' + resp.status,
        resp.status + ' ' + text.substring(0, 500));
    return;
  }

  if (!resp.body) {
    callbacks.onError('No Response Body', 'The API returned no streaming body');
    return;
  }

  const reader = resp.body.getReader();
  const decoder = new TextDecoder();
  let partialLine = '';
  let fullContent = '';
  const toolCallMap: Record<
      number,
      {id: string; type: 'function'; function: {name: string; arguments: string}}
      > = {};
  const toolCallsEmitted = new Set<number>();

  try {
    while (true) {
      if (signal?.aborted) break;
      const {done, value} = await reader.read();
      if (done) break;

      const chunk = decoder.decode(value, {stream: true});
      const lines = (partialLine + chunk).split('\n');
      partialLine = lines.pop() || '';

      for (const line of lines) {
        const trimmed = line.trim();
        if (!trimmed || !trimmed.startsWith('data: ')) continue;
        const data = trimmed.slice(6);
        if (data === '[DONE]') continue;

        try {
          const parsed = JSON.parse(data);
          const delta = parsed.choices?.[0]?.delta;
          if (!delta) continue;

          if (delta.content) {
            fullContent += delta.content;
            callbacks.onToken(delta.content);
          }

          if (delta.tool_calls) {
            for (const tc of delta.tool_calls) {
              const idx = tc.index ?? 0;
              if (!toolCallMap[idx]) {
                toolCallMap[idx] = {
                  id: tc.id || '',
                  type: 'function',
                  function: {name: '', arguments: ''},
                };
              }
              if (tc.id) toolCallMap[idx].id = tc.id;
              if (tc.function?.name) {
                toolCallMap[idx].function.name += tc.function.name;
              }
              if (tc.function?.arguments) {
                toolCallMap[idx].function.arguments += tc.function.arguments;
              }
              if (toolCallMap[idx].function.name &&
                  !toolCallsEmitted.has(idx)) {
                toolCallsEmitted.add(idx);
                callbacks.onToolCall(toolCallMap[idx]);
              }
            }
          }
        } catch (_) {
          // Skip malformed JSON lines
        }
      }
    }
  } catch (e) {
    const err = e as Error;
    if (err.name === 'AbortError') throw err;
    callbacks.onError(
        'Stream Error', err.message || 'Error reading response stream');
    return;
  }

  callbacks.onDone(fullContent, Object.values(toolCallMap));
}

// ---- Unique ID Generator ----

let uidCounter = 0;
export function uid(): string {
  return 'msg-' + (++uidCounter) + '-' + Date.now();
}

export function generateSessionId(): string {
  return Date.now() + '-' + Math.random().toString(36).slice(2, 8);
}
