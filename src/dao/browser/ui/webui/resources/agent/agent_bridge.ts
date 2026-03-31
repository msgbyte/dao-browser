// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Shared non-UI utilities: chrome.send bridge, interfaces, LLM streaming,
// markdown rendering, constants.

import {READABILITY_INJECT_SCRIPT} from './readability_bundle.js';
import {saveUserSkill} from './skill_registry.js';

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

- **get_page_content** — Extract the raw text content of the current webpage (document.body.innerText). Use this as a fallback when get_readable_content fails or for non-article pages.
- **get_readable_content** — Extract the main article content using Mozilla Readability. Strips navigation, ads, sidebars, and boilerplate, returning only the article title, byline, excerpt, and clean text. **Prefer this over get_page_content** for news articles, blog posts, and documentation — it produces much smaller, focused output.
- **get_page_info** — Get the current page URL, title, and meta description. Use this for context about where the user is browsing.
- **click_element** — Click an element on the page by CSS selector. Use this when the user asks you to interact with the page (e.g. "click the login button", "close that popup").
- **execute_script** — Run JavaScript on the current page and return the result. Use this for advanced page interactions, data extraction, or DOM manipulation that the other tools don't cover.
- **update_soul** — Modify your SOUL.md (the personality prompt you see above). Two actions: \`replace_section\` (replace or add a specific ## section), \`replace_all\` (rewrite entirely).

## Guidelines

- When the user asks about page content, call \`get_readable_content\` first for articles/news/docs (smaller, cleaner output), or \`get_page_content\` for other pages — don't guess.
- Prefer precise CSS selectors when clicking elements.
- For \`execute_script\`, return serializable values (strings, numbers, plain objects). Avoid returning DOM nodes directly.
- Always tell the user what you did after using a tool.

## Soul Self-Update

Your personality is defined by a SOUL.md file loaded into every conversation. You can read and update it:

- When the user expresses a **persistent preference** (e.g. "reply in Chinese from now on", "be more concise"), use \`update_soul\` with \`replace_section\` to save it under a descriptive ## heading.
- When the user asks you to **change your behavior or personality**, use \`replace_section\` to update the relevant section.
- **Always prefer \`replace_section\` over \`replace_all\`** — only use \`replace_all\` when the user wants a complete rewrite.
- If the section already exists, \`replace_section\` will update it in place. If it doesn't exist, it will be added automatically.
- **Always tell the user** what you changed and why before or after updating.
- Do **not** modify your soul for one-off requests — only for things the user wants to persist.
- Your current soul is already in this system prompt above — no need to read it separately.
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

function updateSoulByAction(
    action: string, content: string,
    section?: string): {ok: boolean; message: string} {
  const current = loadSoul();

  switch (action) {
    case 'replace_section': {
      if (!section) {
        return {ok: false, message: 'Missing "section" for replace_section.'};
      }
      // Match the section heading and everything until the next same-level
      // heading or end of string.
      const level = (section.match(/^(#+)/) || ['', '##'])[1];
      const escaped = section.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      const pattern =
          new RegExp(`(${escaped}[^\\n]*)\\n[\\s\\S]*?(?=\\n${level} |$)`);
      if (!pattern.test(current)) {
        // Section not found — append as a new section.
        saveSoul(current + '\n\n' + section + '\n\n' + content);
        return {ok: true, message: `New section "${section}" added.`};
      }
      const updated = current.replace(pattern, `$1\n\n${content}`);
      saveSoul(updated);
      return {ok: true, message: `Section "${section}" updated.`};
    }

    case 'replace_all':
      saveSoul(content);
      return {ok: true, message: 'Soul replaced entirely.'};

    default:
      return {
        ok: false,
        message: 'Unknown action: ' + action +
            '. Use "replace_section" or "replace_all".',
      };
  }
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
  {
    type: 'function',
    function: {
      name: 'get_readable_content',
      description:
          'Extract the main article content from the current webpage using Mozilla Readability. Returns the cleaned article title, byline, excerpt, and text content with navigation/ads/sidebars removed. Best for news articles, blog posts, and documentation pages.',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'update_soul',
      description:
          'Update your SOUL.md. Use when the user asks you to change your personality, behavior, or expresses a persistent preference. Always tell the user what you changed.',
      parameters: {
        type: 'object',
        properties: {
          action: {
            type: 'string',
            description:
                'How to update: "replace_section" replaces a specific ## section (adds it if not found), "replace_all" replaces the entire soul. Prefer replace_section.',
          },
          section: {
            type: 'string',
            description:
                'For replace_section: the markdown heading to replace (e.g. "## Vibe"). Ignored for other actions.',
          },
          content: {
            type: 'string',
            description: 'The new content to write',
          },
        },
        required: ['action', 'content'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'save_skill',
      description:
          'Save a new user skill for the Dao Agent. The skill is defined as a SKILL.md file with YAML frontmatter (name, description, hosts, requiresPageContent) and markdown instructions.',
      parameters: {
        type: 'object',
        properties: {
          skill_id: {
            type: 'string',
            description:
                'Unique skill identifier (lowercase, hyphens, no spaces)',
          },
          skill_md: {
            type: 'string',
            description: 'Complete SKILL.md content including YAML frontmatter',
          },
          host: {
            type: 'string',
            description:
                'Target hostname for the skill. Use empty string for a global skill.',
          },
        },
        required: ['skill_id', 'skill_md', 'host'],
      },
    },
  },
];

// ---- Markdown Rendering (re-exported from markdown_renderer.ts) ----

export {renderMarkdown} from './markdown_renderer.js';

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
    case 'get_readable_content': {
      const raw = await callNative(
          'executeScript', {code: READABILITY_INJECT_SCRIPT}) as
          {result?: string; error?: string};
      if (raw.error) {
        return {error: raw.error};
      }
      try {
        return JSON.parse(raw.result || '{}');
      } catch {
        return {error: 'Failed to parse readability result'};
      }
    }
    case 'update_soul':
      return updateSoulByAction(
          args['action'] || '', args['content'] || '', args['section']);
    case 'save_skill': {
      const ok = await saveUserSkill(
          args['skill_id'] || '', args['skill_md'] || '',
          args['host'] || '');
      return ok
          ? {success: true, message: 'Skill saved successfully.'}
          : {success: false, message: 'Failed to save skill.'};
    }
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
