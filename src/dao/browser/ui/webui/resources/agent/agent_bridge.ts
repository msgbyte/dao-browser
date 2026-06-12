// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Shared non-UI utilities: chrome.send bridge, interfaces, LLM streaming,
// markdown rendering, constants.

import {getReusableElementContexts, makeResolveElementContextScript, type ElementContextCapture} from './dao_element_context.js';
import {getActiveLLMConfig} from './llm_config.js';
import {saveUserSkill} from './skill_registry.js';
import {webSearch as wsRun, fetchUrl as wsFetch} from './web_search/index.js';
import {executeWorkspaceTool} from './workspace/bridge.js';

// ---- Interfaces ----

export interface ContentPart {
  type: 'text'|'image_url';
  text?: string;
  image_url?: {url: string; detail?: 'low'|'high'|'auto'};
}

export interface ChatMessage {
  role: 'system'|'user'|'assistant'|'tool';
  content: string|null|ContentPart[];
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

export interface NativeCallOptions {
  timeoutMs?: number|null;
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
const DEFAULT_NATIVE_CALL_TIMEOUT_MS = 15000;

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

// ---- WebUI Listener Infrastructure ----
// (mirrors Chromium's cr.ts: FireWebUIListener → webUIListenerCallback)

const webUiListenerMap:
    Record<string, Array<(...args: unknown[]) => void>> = {};

(cr as unknown as {
  webUIListenerCallback: (event: string, ...args: unknown[]) => void;
}).webUIListenerCallback = function(event: string, ...args: unknown[]) {
  const listeners = webUiListenerMap[event];
  if (!listeners) return;
  for (const cb of listeners) {
    cb(...args);
  }
};

/** Listen for events pushed from C++ via FireWebUIListener. */
export function addWebUIListener(
    event: string, callback: (...args: unknown[]) => void): void {
  (webUiListenerMap[event] = webUiListenerMap[event] || []).push(callback);
}

export function callNative(
    method: string, params?: Record<string, unknown>,
    options?: NativeCallOptions): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const id = method + '_' + (++callbackCounter);
    pendingCallbacks[id] = {resolve, reject};
    chrome.send(method, [id, params || {}]);
    const timeoutMs =
        options?.timeoutMs === undefined ? DEFAULT_NATIVE_CALL_TIMEOUT_MS :
                                           options.timeoutMs;
    if (timeoutMs === null || timeoutMs <= 0 ||
        !Number.isFinite(timeoutMs)) {
      return;
    }
    setTimeout(() => {
      if (pendingCallbacks[id]) {
        delete pendingCallbacks[id];
        reject(new Error('Timeout calling ' + method));
      }
    }, timeoutMs);
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
    }, DEFAULT_NATIVE_CALL_TIMEOUT_MS);
  });
}

// ---- Native Fetch Bridge ----
//
// CORS-bypass fetch routed through DaoAgentUIHandler::HandleNativeFetch.
// The dao://agent origin can't make cross-origin fetch() calls to e.g.
// html.duckduckgo.com (no Access-Control-Allow-Origin), so the WebUI
// asks the browser process to do the request via SimpleURLLoader, which
// has no CORS concept. This is used by web_search/* tiers.

export interface NativeFetchInit {
  method?: string;
  headers?: Record<string, string>;
  body?: string;
  // Credentials policy:
  //  - 'omit' (default): never attach the user's cookies. Used by all
  //    third-party endpoints (DuckDuckGo, Jina Reader, etc.).
  //  - 'include_if_same_origin_active_tab': attach the user's cookies
  //    ONLY when the browser process confirms the target URL is
  //    same-origin with the currently active tab. Used by fetch_url
  //    so the agent can read authenticated content the user is logged
  //    into. Same-origin enforcement happens server-side; the JS layer
  //    cannot trick this into leaking cookies cross-origin.
  credentials?: 'omit'|'include_if_same_origin_active_tab';
}

export interface NativeFetchResponse {
  ok: boolean;
  status: number;
  finalUrl: string;
  body: string;
  error?: string;
}

export function callNativeFetch(
    url: string, init?: NativeFetchInit): Promise<NativeFetchResponse> {
  return new Promise((resolve) => {
    const id = 'nativeFetch_' + (++callbackCounter);
    let timer: ReturnType<typeof setTimeout>|null = null;
    const settle = (response: unknown, source: 'native'|'reject'|'timeout',
                     extraError?: string) => {
      if (timer !== null) {
        clearTimeout(timer);
        timer = null;
      }
      const r = (response as Partial<NativeFetchResponse>) ?? {};
      resolve({
        ok: r.ok === true,
        status: typeof r.status === 'number' ? r.status : 0,
        finalUrl: typeof r.finalUrl === 'string' ? r.finalUrl : url,
        body: typeof r.body === 'string' ? r.body : '',
        error: typeof r.error === 'string'
            ? r.error
            : (source !== 'native' ? (extraError ?? source) : undefined),
      });
    };
    pendingCallbacks[id] = {
      resolve: (response: unknown) => settle(response, 'native'),
      // Native side resolves via ok:false rather than reject; this is a
      // defensive path for a misbehaving native handler.
      reject: (e: unknown) => {
        const msg = e instanceof Error ? e.message : String(e ?? 'unknown');
        settle({}, 'reject', 'reject: ' + msg);
      },
    };
    chrome.send('nativeFetch', [id, {
      url,
      method: init?.method ?? 'GET',
      headers: init?.headers ?? {},
      body: init?.body ?? '',
      credentials: init?.credentials ?? 'omit',
    }]);
    // Outer 35s guard — native loader's own timeout is 30s.
    timer = setTimeout(() => {
      if (pendingCallbacks[id]) {
        delete pendingCallbacks[id];
        settle({}, 'timeout', 'native fetch timeout (35s)');
      }
    }, 35000);
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

- **get_page_info** — Get the current page URL, title, and meta description. Use this for context about where the user is browsing.
- **click_element** — Click an element on the page by CSS selector. Use this when the user asks you to interact with the page (e.g. "click the login button", "close that popup").
- **agent_click** — Click an element with visual cursor animation. Shows a moving pointer, highlights the target, and clicks. Preferred over click_element when performing multi-step agent tasks.
- **move_cursor** — Move the visual cursor to viewport coordinates without clicking.
- **highlight_element** — Highlight an element on the page with a purple border overlay.
- **get_accessibility_tree** — Get a semantic accessibility tree of the current page with element roles, names, and ref_ids for interactive elements. **Use this before interacting with page elements** — it gives you a precise map of what's on the page. Use the ref_ids with \`click_by_ref\` for reliable clicking.
- **click_by_ref** — Click an interactive element by its ref_id from the accessibility tree. More reliable than CSS selectors because ref_ids are assigned deterministically.
- **resolve_element_context** — Resolve a reusable element selected by the user in the current page. Pass \`context_id\` when multiple \`<element-context>\` chips are attached. Returns a temporary selector, usually \`[data-dao-element-context="current"]\`, that can be passed to \`click_element\`, \`agent_click\`, \`highlight_element\`, or \`scroll_to_element\`.
- **capture_screenshot** — Capture a screenshot of the current page viewport. **Expensive — avoid unless necessary.** Each screenshot is a full base64-encoded image that balloons the conversation context. Only call when: (a) the user explicitly asks what the page looks like / to see something, (b) you need visual layout info that text tools cannot provide (e.g. colors, relative positions, image contents), or (c) a prior text tool returned ambiguous / empty output and a visual check is the only way forward. **Do NOT** screenshot to "verify" a click, scroll, or form fill — the tool already returns structured success info. **Do NOT** screenshot at the start of a task "to get oriented" — use \`get_accessibility_tree\` or the \`<current-webpage>\` block the user message already contains. **Do NOT** screenshot after every navigation; one per task at most unless the user asks for more.
- **scroll_down** / **scroll_up** — Scroll the page down or up by one viewport height. Returns scroll position info.
- **scroll_to_element** — Scroll a specific element into view. Accepts either a CSS selector or a ref_id from the accessibility tree.
- **press_key_chord** — Simulate a keyboard shortcut on the current page (e.g. "ctrl+a", "cmd+c", "Enter", "Tab", "Escape"). Use for form submission, copy/paste, navigation shortcuts, etc.
- **type_text** — Type text character-by-character into the currently focused element using CDP Input.insertText. Set clear=true to select-all and replace existing content first. Use this instead of execute_script for filling form fields.
- **list_tabs** — List all open tabs with their index, URL, title, and active status.
- **switch_tab** — Switch to a different tab by its index (from list_tabs).
- **open_tab** — Open a new tab with the given URL.
- **close_tab** — Close a tab by index (defaults to current active tab). Will refuse to close the last tab.
- **enable_network_tracking** — Start capturing network requests on the current tab. Call this before browsing to monitor API calls, resource loads, etc.
- **get_network_requests** — Retrieve captured network requests (URLs, methods, status codes, MIME types). Must call \`enable_network_tracking\` first.
- **clear_network_requests** — Clear all captured network requests.
- **enable_console_tracking** — Start capturing console messages (logs, warnings, errors) on the current tab.
- **get_console_messages** — Retrieve captured console messages. Optionally filter by type ("error", "warning", "log"). Must call \`enable_console_tracking\` first.
- **clear_console_messages** — Clear all captured console messages.
- **execute_script** — Run JavaScript on the current page and return the result. Use this for advanced page interactions, data extraction, or DOM manipulation that the other tools don't cover. Set \`lock_tab\` to true when the script will manipulate the page so the browser can briefly block user input and show an AI control state.
- **get_page_html** — Read the current page's full \`document.documentElement.outerHTML\` (up to 512 KiB). **Expensive — avoid unless necessary.** A single call can fill the context with hundreds of kilobytes of raw markup, style, and script. **Almost every "what's on this page / summarize this / extract data from this" task is better served by the \`<current-webpage>\` markdown block already attached to the user's message, or by \`get_accessibility_tree\` for interactive structure.** Only call get_page_html when you genuinely need raw markup the cheaper tools cannot give you: (a) reading an inline \`<script>\` payload such as \`__NEXT_DATA__\` / JSON-LD that didn't survive the markdown extraction, (b) inspecting raw \`<meta>\` / \`<head>\` tags or hidden attributes Readability strips, (c) the markdown block is empty / clearly broken and you must see the original DOM to diagnose. **Do NOT** call get_page_html to "see what's on the page", to summarize an article, to find a link or button, to verify an action, or as a general first step — those all have cheaper answers. Prefer \`search_in_resources\` / \`list_page_resources\` for content inside loaded JS/CSS bundles, and a targeted \`execute_script\` that returns only the substring you need (e.g. \`document.querySelector('script#__NEXT_DATA__').textContent\`) over downloading the whole document.
- **list_page_resources** — Enumerate every resource the page loaded at navigation time (HTML documents, scripts, stylesheets, images, fonts, XHRs, fetches). Returns \`[{url, type, mimeType, frameId}]\`. Pass \`type_filter\` ("Script", "Stylesheet", "Document", "Image", "Font", "XHR", "Fetch", or "all") to narrow. Use this as the entry point for reverse-engineering a site.
- **get_resource_content** — Fetch the full body of a single resource by URL (usually from \`list_page_resources\`). Text payloads are truncated to 512 KiB; binary payloads are returned as base64 (\`base64_encoded: true\`).
- **get_network_body** — Fetch the response body of a network request by \`request_id\` (from \`get_network_requests\`). Use this to inspect XHR / fetch payloads that \`list_page_resources\` doesn't cover. Requires \`enable_network_tracking\` to have run first.
- **search_in_resources** — Regex-search across all loaded text resources (scripts / stylesheets / documents). Returns the first N matches with URL, line number, and a short excerpt. Useful for locating a function, API endpoint, or string in a minified bundle without downloading each file.
- **update_soul** — Modify your SOUL.md (the personality prompt you see above). Two actions: \`replace_section\` (replace or add a specific ## section), \`replace_all\` (rewrite entirely).
- **save_memory** — Save a record of what you did on this page to long-term memory (intent + outcome). Use after completing a meaningful task so you have context next time the user visits this page.
- **save_skill** — Save a reusable skill as a Markdown document. Skills are user-defined automation recipes the agent can discover and execute in future sessions. Use when the user asks you to remember a workflow or create an automation for a specific site or task.

## Safety Rules

- **Never pretend an action succeeded.** If a tool call fails or returns an error, report the failure honestly — do not fabricate a success result.
- **Confirm before irreversible actions.** If a request could cause irreversible changes — form submissions, purchases, account changes, authentication, password resets, or destructive actions (deleting data, closing important tabs) — pause and ask the user to confirm before proceeding.
- **Respect permission denials.** When the user declines or cancels an action, do not attempt to use other tools or workarounds to achieve the same goal. Acknowledge the denial and ask how the user would prefer to proceed.

## Guidelines

- **Recommended workflow for reading page content:** The user's message usually already includes a \`<current-webpage url="..." title="...">\` markdown block (either inline or as an attached \`<title>.md\` document) — that block IS the page, extracted via Readability and converted to clean markdown. Answer questions like "summarize this", "what does this article say", "find X on this page" directly from that block. Do **not** call \`get_page_html\`, \`get_accessibility_tree\`, or \`capture_screenshot\` just to re-read content that is already in your context.
- **Recommended workflow for page interaction:**
  1. If the user selected reusable \`<element-context>\` chip(s), call \`resolve_element_context\` first. When there are multiple chips, pass the \`context_id\` from the matching \`<element-context>\` block.
  2. Otherwise, call \`get_accessibility_tree\` — understand what's on the page
  3. \`click_by_ref\` / \`scroll_*\` / \`type_text\` — interact using ref_ids
  4. Trust the tool's return value as proof the action happened. Only re-query the page (\`get_accessibility_tree\`) if you need to see a *new* state; never screenshot just to "verify". The user can see their own screen.
- **Tool cost ladder (cheapest → most expensive), pick the lowest rung that answers the question:** \`<current-webpage>\` attachment (free, already in context) → \`resolve_element_context\` (if the user selected an element) → \`get_page_info\` (a few hundred bytes) → \`get_accessibility_tree\` (compact tree of interactive elements) → \`search_in_resources\` / targeted \`execute_script\` (one regex hit or one querySelector value) → \`list_page_resources\` + \`get_resource_content\` (one specific file) → \`get_page_html\` (up to 512 KiB, last resort) → \`capture_screenshot\` (full base64 image).
- When the user attaches a document named \`<title>.md\` containing a \`<current-webpage url="..." title="...">\` block (or such a block appears inline in the user text), treat the block as the authoritative snapshot of the active tab at send time — answer from it directly instead of trying to re-read the page.
- Prefer \`click_by_ref\` with ref_ids from the accessibility tree over CSS selectors. Fall back to \`click_element\` with CSS selectors only if the accessibility tree is not available.
- For \`execute_script\`, return serializable values (strings, numbers, plain objects). Avoid returning DOM nodes directly.
- When \`execute_script\` will click, type, submit, scroll, or otherwise manipulate the page, set \`lock_tab\` to true. Leave it false for read-only extraction scripts.
- Always tell the user what you did after using a tool.
- After completing a significant task (e.g. summarizing a page, extracting data, helping with a form), use \`save_memory\` to record what you did so you have context next time the user visits this page.

## Soul Self-Update

Your personality is defined by a SOUL.md file loaded into every conversation. You can read and update it:

- When the user expresses a **persistent preference** (e.g. "reply in Chinese from now on", "be more concise"), use \`update_soul\` with \`replace_section\` to save it under a descriptive ## heading.
- When the user asks you to **change your behavior or personality**, use \`replace_section\` to update the relevant section.
- **Always prefer \`replace_section\` over \`replace_all\`** — only use \`replace_all\` when the user wants a complete rewrite.
- If the section already exists, \`replace_section\` will update it in place. If it doesn't exist, it will be added automatically.
- **Always tell the user** what you changed and why before or after updating.
- Do **not** modify your soul for one-off requests — only for things the user wants to persist.
- Your current soul is in the <soul>...</soul> block in this system prompt — no need to read it separately.
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

## Vibe

Be the assistant you'd actually want to talk to. Concise when needed, thorough when it matters. Not a corporate drone. Not a sycophant. Just... good.`;

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
      name: 'agent_click',
      description:
          'Click an element with visual cursor animation. Shows a purple pointer moving to the element, highlights it, and performs the click. Use this instead of click_element when the tab is being controlled by the agent.',
      parameters: {
        type: 'object',
        properties: {
          selector: {
            type: 'string',
            description: 'CSS selector of the element to click',
          },
          description: {
            type: 'string',
            description:
                'Human-readable description of the action (e.g. "Click the Submit button")',
          },
        },
        required: ['selector'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'move_cursor',
      description:
          'Move the visual cursor to viewport coordinates without clicking',
      parameters: {
        type: 'object',
        properties: {
          x: {type: 'number', description: 'Viewport X coordinate'},
          y: {type: 'number', description: 'Viewport Y coordinate'},
        },
        required: ['x', 'y'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'highlight_element',
      description:
          'Highlight an element on the page with a purple border overlay',
      parameters: {
        type: 'object',
        properties: {
          selector: {
            type: 'string',
            description: 'CSS selector of the element to highlight',
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
          lock_tab: {
            type: 'boolean',
            description:
                'Whether to temporarily lock the current tab while this script manipulates the page. Use false for read-only scripts.',
          },
        },
        required: ['code'],
      },
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
      name: 'save_memory',
      description:
          'Save a record of what you did on this page to long-term memory. Use after completing a meaningful task so you have context next time the user visits this page.',
      parameters: {
        type: 'object',
        properties: {
          intent: {
            type: 'string',
            description:
                'What the user wanted to do on this page',
          },
          outcome: {
            type: 'string',
            description:
                'What happened / the result of the interaction',
          },
        },
        required: ['intent', 'outcome'],
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
  {
    type: 'function',
    function: {
      name: 'get_accessibility_tree',
      description:
          'Get a semantic accessibility tree of the current page. Returns element hierarchy with roles, names, and ref_ids for interactive elements. Use ref_ids with click_by_ref for precise interaction.',
      parameters: {
        type: 'object',
        properties: {
          filter: {
            type: 'string',
            description:
                'Filter mode: "interactive" (only clickable/input elements, default), "visible" (viewport only), "all" (full page)',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'click_by_ref',
      description:
          'Click an interactive element by its ref_id from the accessibility tree. More reliable than CSS selectors.',
      parameters: {
        type: 'object',
        properties: {
          ref_id: {
            type: 'string',
            description:
                'The ref_id from the accessibility tree (e.g. "ref_3")',
          },
          description: {
            type: 'string',
            description:
                'Human-readable description of the click action',
          },
        },
        required: ['ref_id'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'resolve_element_context',
      description:
          'Resolve one reusable element selected by the user on the current page. When multiple element chips are selected, pass context_id or index. Returns a temporary selector for that element, usually [data-dao-element-context="current"].',
      parameters: {
        type: 'object',
        properties: {
          context_id: {
            type: 'string',
            description:
                'The context_id from an <element-context> block.',
          },
          index: {
            type: 'number',
            description:
                'Zero-based index from the resolver error contexts list.',
          },
          label: {
            type: 'string',
            description:
                'Element label to resolve when context_id is unavailable.',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'capture_screenshot',
      description:
          'Capture a screenshot of the current page viewport. Returns the screenshot as a base64-encoded image for visual analysis.',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'scroll_down',
      description:
          'Scroll down the page. Defaults to ~80% of viewport height. Optionally specify exact pixel amount.',
      parameters: {
        type: 'object',
        properties: {
          amount: {
            type: 'number',
            description:
                'Scroll amount in pixels. Omit to scroll by ~80% of viewport height.',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'scroll_up',
      description:
          'Scroll up the page. Defaults to ~80% of viewport height. Optionally specify exact pixel amount.',
      parameters: {
        type: 'object',
        properties: {
          amount: {
            type: 'number',
            description:
                'Scroll amount in pixels. Omit to scroll by ~80% of viewport height.',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'scroll_to_element',
      description:
          'Scroll a specific element into view. Accepts either a CSS selector or a ref_id from the accessibility tree.',
      parameters: {
        type: 'object',
        properties: {
          selector: {
            type: 'string',
            description: 'CSS selector of the element',
          },
          ref_id: {
            type: 'string',
            description: 'ref_id from accessibility tree',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'press_key_chord',
      description:
          'Simulate a keyboard shortcut (e.g. "ctrl+a", "cmd+c", "Enter", "Tab", "Escape", "Backspace"). Dispatches keydown/keyup events on the focused element.',
      parameters: {
        type: 'object',
        properties: {
          keys: {
            type: 'string',
            description:
                'Key combo string, e.g. "ctrl+a", "cmd+c", "Enter", "shift+Tab"',
          },
        },
        required: ['keys'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'type_text',
      description:
          'Type text into the currently focused input field character by character using CDP Input.insertText. Works with all input types including contentEditable and shadow DOM inputs.',
      parameters: {
        type: 'object',
        properties: {
          text: {
            type: 'string',
            description: 'The text to type into the focused element',
          },
          clear: {
            type: 'boolean',
            description:
                'If true, select all and replace existing content before typing. Default: false (append).',
          },
        },
        required: ['text'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'list_tabs',
      description:
          'List all open browser tabs with their index, URL, title, and active status.',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'switch_tab',
      description: 'Switch to a different browser tab by its index.',
      parameters: {
        type: 'object',
        properties: {
          index: {
            type: 'number',
            description: 'Tab index (from list_tabs)',
          },
        },
        required: ['index'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'open_tab',
      description: 'Open a new browser tab with the given URL.',
      parameters: {
        type: 'object',
        properties: {
          url: {
            type: 'string',
            description: 'URL to open (defaults to about:blank)',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'close_tab',
      description:
          'Close a browser tab by index. Defaults to closing the current active tab.',
      parameters: {
        type: 'object',
        properties: {
          index: {
            type: 'number',
            description: 'Tab index to close (defaults to active tab)',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'enable_network_tracking',
      description:
          'Start capturing network requests on the current tab. Call before browsing to monitor API calls and resource loads.',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'get_network_requests',
      description:
          'Get captured network requests (URLs, methods, status codes). Must call enable_network_tracking first.',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'clear_network_requests',
      description: 'Clear all captured network requests.',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'enable_console_tracking',
      description:
          'Start capturing console messages (logs, warnings, errors) on the current tab.',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'get_console_messages',
      description:
          'Get captured console messages. Optionally filter by type. Must call enable_console_tracking first.',
      parameters: {
        type: 'object',
        properties: {
          filter: {
            type: 'string',
            description:
                'Filter by message type: "error", "warning", "log", "info". Omit for all.',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'clear_console_messages',
      description: 'Clear all captured console messages.',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'get_page_html',
      description:
          'EXPENSIVE — last-resort tool that returns the full page outerHTML (up to 512 KiB) and balloons the context. Do NOT call this to summarize a page, find a link/button, or "see what is on the page" — the <current-webpage> markdown block already attached to the user message answers those, and get_accessibility_tree is far cheaper for interactive structure. Only use when you specifically need raw markup the other tools cannot give you (e.g. reading an inline __NEXT_DATA__ / JSON-LD script, or inspecting <meta>/<head> attributes Readability strips). Prefer a targeted execute_script that returns only the substring you need over downloading the whole document.',
      parameters: {type: 'object', properties: {}, required: []},
    },
  },
  {
    type: 'function',
    function: {
      name: 'list_page_resources',
      description:
          'List every resource the current page loaded (scripts, stylesheets, images, fonts, XHRs, etc.). Returns [{url, type, mimeType, frameId}]. Use this as the entry point for analyzing a site\'s source.',
      parameters: {
        type: 'object',
        properties: {
          type_filter: {
            type: 'string',
            description:
                'Optional filter: "Document" | "Stylesheet" | "Script" | "Image" | "Font" | "XHR" | "Fetch" | "all" (default).',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'get_resource_content',
      description:
          'Fetch the body of a single resource by URL (usually from list_page_resources). Text truncated at 512 KiB; binary returned as base64.',
      parameters: {
        type: 'object',
        properties: {
          url: {
            type: 'string',
            description: 'The resource URL (exact match, from list_page_resources).',
          },
          frame_id: {
            type: 'string',
            description: 'Optional frame id. Defaults to the main frame.',
          },
        },
        required: ['url'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'get_network_body',
      description:
          'Fetch the response body of a captured network request by request_id (from get_network_requests). Requires enable_network_tracking to have run before the request was made.',
      parameters: {
        type: 'object',
        properties: {
          request_id: {
            type: 'string',
            description: 'The CDP request id from get_network_requests.',
          },
        },
        required: ['request_id'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'search_in_resources',
      description:
          'Regex-search across all loaded text resources. Returns up to max_matches hits with {url, line, excerpt}. Cheaper than downloading every bundle when hunting for a function, endpoint, or string.',
      parameters: {
        type: 'object',
        properties: {
          pattern: {
            type: 'string',
            description: 'Regex pattern (JavaScript syntax).',
          },
          flags: {
            type: 'string',
            description: 'Regex flags (e.g. "i" for case-insensitive). Defaults to "i".',
          },
          types: {
            type: 'string',
            description:
                'Comma-separated resource types to search (default: "Script,Stylesheet,Document"). Binary types (Image, Font) are always skipped.',
          },
          max_matches: {
            type: 'number',
            description: 'Max matches to return (default 20).',
          },
        },
        required: ['pattern'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'web_search',
      description:
          'Search the web for up-to-date information. Returns a list of ' +
          'results with title, url, and snippet. Use fetch_url afterwards ' +
          'to read full articles.',
      parameters: {
        type: 'object',
        properties: {
          query: {
            type: 'string',
            description: 'The search query',
          },
          max_results: {
            type: 'integer',
            description: 'Max results, default 5, max 10',
          },
        },
        required: ['query'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'fetch_url',
      description:
          'Fetch the readable content of a web page as markdown. Use ' +
          'after web_search to read full articles.',
      parameters: {
        type: 'object',
        properties: {
          url: {
            type: 'string',
            description: 'The URL to fetch',
          },
          with_credentials: {
            type: 'boolean',
            description:
                'Optional. Defaults to false. When true, the request is ' +
                'sent with the user\'s cookies attached IF the target URL ' +
                'is same-origin (scheme + host + port) with the active ' +
                'tab — useful for reading authenticated content the user ' +
                'is currently logged into (e.g. their inbox, a paywalled ' +
                'article). Cross-origin URLs are still fetched without ' +
                'cookies regardless of this flag. Only applies to GET; ' +
                'ignored for any non-GET method.',
          },
        },
        required: ['url'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'workspace_read',
      description:
          'Read a text file from the agent workspace. Returns content ' +
          'paginated by line range; the workspace is text-only and ' +
          'enforces a quota.',
      parameters: {
        type: 'object',
        properties: {
          path: {type: 'string', description: 'Relative path in workspace.'},
          offset: {
            type: 'integer',
            description: 'Optional 0-based starting line offset (default 0).',
          },
          limit: {
            type: 'integer',
            description:
                'Optional max number of lines to return (default 500, max 5000).',
          },
        },
        required: ['path'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'workspace_write',
      description:
          'Write (create or overwrite) a text file in the agent ' +
          'workspace. Subject to the workspace text-only filter and quota.',
      parameters: {
        type: 'object',
        properties: {
          path: {type: 'string', description: 'Relative path in workspace.'},
          content: {
            type: 'string',
            description: 'Full text content to write.',
          },
        },
        required: ['path', 'content'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'workspace_edit',
      description:
          'Replace a unique substring in a workspace file. Fails if ' +
          'old_text matches zero or multiple locations — widen the ' +
          'context until unique.',
      parameters: {
        type: 'object',
        properties: {
          path: {type: 'string', description: 'Relative path in workspace.'},
          old_text: {
            type: 'string',
            description: 'Exact substring to replace; must be unique.',
          },
          new_text: {
            type: 'string',
            description: 'Replacement text.',
          },
        },
        required: ['path', 'old_text', 'new_text'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'list_files',
      description:
          'List entries in the agent workspace. Returns directories ' +
          '(trailing "/") and text files with their sizes and ' +
          'modification times. Workspace bookkeeping (dotfiles, ' +
          '.workspace_tmp, WORKSPACE.md) is hidden. Results are capped ' +
          'at 1000 entries; check `truncated` and pass a narrower ' +
          '`path` if more exist.',
      parameters: {
        type: 'object',
        properties: {
          path: {
            type: 'string',
            description:
                'Optional relative directory in the workspace; empty ' +
                'or omitted means the workspace root.',
          },
          recursive: {
            type: 'boolean',
            description:
                'If true, descend into subdirectories (default false).',
          },
        },
        required: [],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'apply_patch',
      description:
          'Apply a V4A-format multi-file patch to the workspace ' +
          '(atomic across files). Use for batched add/update/delete/move ' +
          'operations.',
      parameters: {
        type: 'object',
        properties: {
          patch: {
            type: 'string',
            description: 'Full V4A patch text (Begin Patch ... End Patch).',
          },
        },
        required: ['patch'],
      },
    },
  },
  {
    type: 'function',
    function: {
      name: 'download',
      description:
          'Save content directly into the agent workspace without ' +
          'echoing it through tool arguments — use this whenever you ' +
          'want to persist a page or URL, instead of capturing the ' +
          'body and pasting it into workspace_write. Sources: ' +
          '"page" captures the active tab\'s outerHTML; "url" fetches ' +
          'an arbitrary http(s) URL. Subject to the workspace text-' +
          'only filter and quota (binaries are rejected; 5 MiB body cap).',
      parameters: {
        type: 'object',
        properties: {
          path: {
            type: 'string',
            description:
                'Relative path in workspace (e.g. "baidu.html").',
          },
          source: {
            type: 'string',
            description:
                'Source of the content. One of "page" (active tab ' +
                'outerHTML) or "url" (fetch an http(s) URL). Defaults ' +
                'to "url" when "url" is provided, otherwise "page".',
          },
          url: {
            type: 'string',
            description:
                'Required when source="url". Must be http(s).',
          },
        },
        required: ['path'],
      },
    },
  },
];

// ---- Tool Execution ----

function getStringArg(args: Record<string, unknown>, key: string): string {
  return typeof args[key] === 'string' ? args[key] as string : '';
}

function getBooleanArg(args: Record<string, unknown>, key: string): boolean {
  return args[key] === true;
}

function getNumberArg(args: Record<string, unknown>, key: string):
    number | null {
  return typeof args[key] === 'number' && Number.isFinite(args[key]) ?
      args[key] as number :
      null;
}

function summarizeElementContexts(contexts: ElementContextCapture[]):
    Array<{context_id: string; label: string; index: number}> {
  return contexts.map((context, index) => ({
    context_id: context.contextId || '',
    label: context.label,
    index,
  }));
}

function chooseElementContext(
    contexts: ElementContextCapture[],
    args: Record<string, unknown>):
    {context?: ElementContextCapture; error?: Record<string, unknown>} {
  if (contexts.length === 0) {
    return {error: {error: 'No reusable element context selected.'}};
  }

  const contextId = getStringArg(args, 'context_id');
  if (contextId) {
    const context = contexts.find(item => item.contextId === contextId);
    if (context) return {context};
    return {
      error: {
        error: 'Reusable element context not found.',
        context_id: contextId,
        contexts: summarizeElementContexts(contexts),
      },
    };
  }

  const index = getNumberArg(args, 'index');
  if (index !== null) {
    const context = Number.isInteger(index) ? contexts[index] : undefined;
    if (context) return {context};
    return {
      error: {
        error: 'Reusable element context index out of range.',
        index,
        contexts: summarizeElementContexts(contexts),
      },
    };
  }

  const label = getStringArg(args, 'label').toLowerCase();
  if (label) {
    const exact =
        contexts.find(item => item.label.toLowerCase() === label);
    if (exact) return {context: exact};
    const partial =
        contexts.find(item => item.label.toLowerCase().includes(label));
    if (partial) return {context: partial};
    return {
      error: {
        error: 'Reusable element context label not found.',
        label,
        contexts: summarizeElementContexts(contexts),
      },
    };
  }

  if (contexts.length === 1) return {context: contexts[0]};

  return {
    error: {
      error:
          'Multiple reusable element contexts selected. Provide context_id or index.',
      contexts: summarizeElementContexts(contexts),
    },
  };
}

// Runtime guardrail against capture_screenshot spam. Prompts alone don't
// fully suppress the "screenshot after every action" pattern on weaker
// models; a time-based dedup per (url, viewport) window nudges without
// blocking legitimate re-captures across navigations. 2.5s is short
// enough that an actual navigation settles before the next call, long
// enough that back-to-back "click → screenshot → think → screenshot"
// loops get intercepted.
let lastScreenshotAt_ = 0;
let lastScreenshotUrl_ = '';
const SCREENSHOT_MIN_INTERVAL_MS = 2500;

export async function executeTool(
    name: string, args: Record<string, unknown>): Promise<unknown> {
  switch (name) {
    case 'workspace_read':
    case 'workspace_write':
    case 'workspace_edit':
    case 'apply_patch':
    case 'list_files':
    case 'download':
      return await executeWorkspaceTool(name, args);
    case 'get_page_info':
      return await callNative('getPageInfo');
    case 'click_element':
      return await callNative(
          'clickElement', {selector: getStringArg(args, 'selector')});
    case 'agent_click':
      return await callNative('agentClick', {
        selector: getStringArg(args, 'selector'),
        description: getStringArg(args, 'description'),
      });
    case 'move_cursor':
      return await callNative('moveCursor', {
        x: args['x'] as number,
        y: args['y'] as number,
      });
    case 'highlight_element':
      return await callNative('highlightElement', {
        selector: getStringArg(args, 'selector'),
      });
    case 'execute_script':
      return await callNative('executeScript', {
        code: getStringArg(args, 'code'),
        lockTab: getBooleanArg(args, 'lock_tab'),
      });
    case 'update_soul':
      return updateSoulByAction(
          getStringArg(args, 'action'), getStringArg(args, 'content'),
          getStringArg(args, 'section') || undefined);
    case 'save_memory': {
      const intent = getStringArg(args, 'intent');
      const outcome = getStringArg(args, 'outcome');
      if (!intent) {
        return {error: 'Missing intent for save_memory'};
      }
      let pageInfo: {url?: string; title?: string} = {};
      try {
        pageInfo = await callNative('getPageInfo') as
            {url?: string; title?: string};
      } catch (_) { /* best-effort */ }
      let domain = '';
      let pathTemplate = '';
      if (pageInfo.url) {
        try {
          const u = new URL(pageInfo.url);
          domain = u.hostname;
          pathTemplate = u.pathname;
        } catch (_) { /* ignore */ }
      }
      try {
        await callNative('saveEpisode', {
          domain,
          pathTemplate,
          url: pageInfo.url || '',
          title: pageInfo.title || '',
          intent,
          outcome,
          entities: '[]',
          toolsUsed: '[]',
          confidence: 0.7,
          userAction: intent,
          actionResult: '',
        });
        return {success: true, message: 'Memory saved for ' + domain};
      } catch (e) {
        return {error: 'Failed to save memory: ' + (e as Error).message};
      }
    }
    case 'save_skill': {
      const ok = await saveUserSkill(
          getStringArg(args, 'skill_id'), getStringArg(args, 'skill_md'),
          getStringArg(args, 'host'));
      return ok
          ? {success: true, message: 'Skill saved successfully.'}
          : {success: false, message: 'Failed to save skill.'};
    }
    case 'get_accessibility_tree':
      return await callNative('getAccessibilityTree', {
        filter: getStringArg(args, 'filter') || 'interactive',
      });
    case 'click_by_ref':
      return await callNative('clickByRef', {
        ref_id: getStringArg(args, 'ref_id'),
        description: getStringArg(args, 'description'),
      });
    case 'resolve_element_context': {
      const chosen = chooseElementContext(getReusableElementContexts(), args);
      if (chosen.error) return chosen.error;
      const context = chosen.context!;
      const raw = await callNative('executeScript', {
        code: makeResolveElementContextScript(context.locator),
        lockTab: false,
      }) as {result?: unknown; error?: string};
      if (raw.error) return {error: raw.error};
      const result = typeof raw.result === 'string' ? raw.result :
          JSON.stringify(raw.result ?? {});
      try {
        const parsed = JSON.parse(result);
        return {
          ...parsed,
          context_id: context.contextId,
          label: context.label,
        };
      } catch (_) {
        return {
          error: 'Invalid element context resolver response.',
          result,
        };
      }
    }
    case 'capture_screenshot': {
      const pageInfo = await callNative('getPageInfo') as {url?: string};
      const currentUrl = pageInfo?.url || '';
      const now = Date.now();
      const delta = now - lastScreenshotAt_;
      if (lastScreenshotAt_ > 0 && delta < SCREENSHOT_MIN_INTERVAL_MS &&
          currentUrl === lastScreenshotUrl_) {
        return {
          error:
              `You just captured this page ${delta}ms ago and the URL has ` +
              `not changed. Reuse the previous screenshot — do not call ` +
              `capture_screenshot to "verify" each action. If you need the ` +
              `post-action page state, use get_accessibility_tree instead.`,
        };
      }
      const result = await callNative('captureScreenshot') as
          {data?: string; error?: string};
      if (result.error) return {error: result.error};
      lastScreenshotAt_ = now;
      lastScreenshotUrl_ = currentUrl;
      return {
        screenshot_taken: true,
        _base64: result.data,
        message: 'Screenshot captured successfully.',
      };
    }
    case 'scroll_down':
      return await callNative('scrollPage', {direction: 'down', amount: args['amount'] as number});
    case 'scroll_up':
      return await callNative('scrollPage', {direction: 'up', amount: args['amount'] as number});
    case 'scroll_to_element':
      return await callNative('scrollToElement', {
        selector: getStringArg(args, 'selector'),
        ref_id: getStringArg(args, 'ref_id'),
      });
    case 'press_key_chord':
      return await callNative('pressKeyChord', {
        keys: getStringArg(args, 'keys'),
      });
    case 'list_tabs':
      return await callNative('listTabs');
    case 'switch_tab':
      return await callNative('switchTab', {
        index: args['index'] as number,
      });
    case 'open_tab':
      return await callNative('openTab', {
        url: getStringArg(args, 'url'),
      });
    case 'close_tab':
      return await callNative('closeTab', {
        index: args['index'] as number,
      });
    case 'enable_network_tracking':
      return await callNative('enableNetworkTracking');
    case 'get_network_requests':
      return await callNative('getNetworkRequests');
    case 'clear_network_requests':
      return await callNative('clearNetworkRequests');
    case 'enable_console_tracking':
      return await callNative('enableConsoleTracking');
    case 'get_console_messages':
      return await callNative('getConsoleMessages', {
        filter: getStringArg(args, 'filter'),
      });
    case 'clear_console_messages':
      return await callNative('clearConsoleMessages');
    case 'type_text':
      return await callNative('typeText', {
        text: getStringArg(args, 'text'),
        clear: args['clear'] as boolean,
      });
    case 'get_page_html':
      return await callNative('getPageHtml');
    case 'list_page_resources':
      return await callNative('listPageResources', {
        type_filter: getStringArg(args, 'type_filter') || 'all',
      });
    case 'get_resource_content':
      return await callNative('getResourceContent', {
        url: getStringArg(args, 'url'),
        frame_id: getStringArg(args, 'frame_id'),
      });
    case 'get_network_body':
      return await callNative('getNetworkBody', {
        request_id: getStringArg(args, 'request_id'),
      });
    case 'search_in_resources': {
      const pattern = getStringArg(args, 'pattern');
      if (!pattern) return {error: 'Missing pattern'};
      const flags = getStringArg(args, 'flags') || 'i';
      const typesRaw =
          getStringArg(args, 'types') || 'Script,Stylesheet,Document';
      const typesAllowed = new Set(
          typesRaw.split(',').map(s => s.trim()).filter(Boolean));
      const maxMatches =
          typeof args['max_matches'] === 'number' ? args['max_matches'] : 20;
      let re: RegExp;
      try {
        re = new RegExp(pattern, flags);
      } catch (e) {
        return {error: 'Invalid regex: ' + (e as Error).message};
      }
      // Always exclude binary types regardless of what the caller passed.
      for (const t of ['Image', 'Font', 'Media', 'Manifest']) {
        typesAllowed.delete(t);
      }
      const listRes = await callNative('listPageResources', {
        type_filter: 'all',
      }) as {resources?: Array<{url: string; type: string; mimeType?: string}>};
      const resources = listRes.resources || [];
      const targets = resources.filter(r => typesAllowed.has(r.type));
      const matches: Array<{url: string; line: number; excerpt: string}> = [];
      // Serial fetch to keep backend pressure low and to bail early once
      // we've hit the match cap.
      for (const r of targets) {
        if (matches.length >= maxMatches) break;
        const contentRes = await callNative('getResourceContent', {
          url: r.url,
          frame_id: '',
        }) as {content?: string; base64_encoded?: boolean; error?: string};
        if (contentRes.error || contentRes.base64_encoded || !contentRes.content) {
          continue;
        }
        const lines = contentRes.content.split('\n');
        for (let i = 0; i < lines.length && matches.length < maxMatches; i++) {
          const line = lines[i];
          if (line === undefined) continue;
          if (re.test(line)) {
            const excerpt =
                line.length > 240 ? line.slice(0, 240) + '…' : line;
            matches.push({url: r.url, line: i + 1, excerpt});
          }
        }
      }
      return {
        pattern,
        flags,
        searched: targets.length,
        matches,
        truncated: matches.length >= maxMatches,
      };
    }
    case 'web_search': {
      const query = getStringArg(args, 'query');
      const maxRaw = args['max_results'];
      const max = typeof maxRaw === 'number' ? maxRaw : undefined;
      const cfg = getActiveLLMConfig();
      const resp = await wsRun(
          query, max, {provider: cfg.provider, model: cfg.model});
      return resp;
    }
    case 'fetch_url': {
      const url = getStringArg(args, 'url');
      const withCredentials = getBooleanArg(args, 'with_credentials');
      // Only probe the active tab when the LLM actually opted in to
      // sending cookies — otherwise the same-origin shortcut never
      // fires and the extra IPC is pure waste.
      let activeTabUrl: string|undefined;
      if (withCredentials) {
        try {
          const info = await callNative('getPageInfo') as
              {url?: string} | null;
          if (info && typeof info.url === 'string') {
            activeTabUrl = info.url;
          }
        } catch (_) { /* best-effort */ }
      }
      const resp = await wsFetch(url, {activeTabUrl, withCredentials});
      return resp;
    }
    default:
      return {error: 'Unknown tool: ' + name};
  }
}

// ---- LLM Streaming ----

export interface UsageInfo {
  prompt_tokens: number;
  completion_tokens: number;
  total_tokens: number;
}

export interface StreamCallbacks {
  onToken: (text: string) => void;
  onToolCall: (toolCall: ToolCall) => void;
  onDone:
      (fullContent: string, toolCalls: ToolCall[],
       usage?: UsageInfo) => void;
  onError: (shortMsg: string, fullError: string) => void;
}

export async function callLLMStreaming(
    msgs: ChatMessage[], callbacks: StreamCallbacks,
    signal?: AbortSignal): Promise<void> {
  const cfg = getActiveLLMConfig();
  const {callLLMStreamingWithPi} = await import('./pi_llm_stream.js');
  return callLLMStreamingWithPi(msgs, tools, callbacks, {
    provider: cfg.provider,
    apiKey: cfg.apiKey,
    baseUrl: cfg.baseUrl,
    model: cfg.model,
    signal,
  });
}

// ---- Agent Stats ----

export interface AgentStats {
  apiCalls: number;
  toolCalls: Record<string, number>;
  promptTokens: number;
  completionTokens: number;
  totalTokens: number;
  estimatedCost: number;
  lastReset: number;
}

const STATS_KEY = 'dao_agent_stats';

function defaultStats(): AgentStats {
  return {
    apiCalls: 0, toolCalls: {}, promptTokens: 0,
    completionTokens: 0, totalTokens: 0,
    estimatedCost: 0, lastReset: Date.now(),
  };
}

function loadStats(): AgentStats {
  const base = defaultStats();
  try {
    const raw = localStorage.getItem(STATS_KEY);
    if (!raw) return base;
    const parsed = JSON.parse(raw) as Partial<AgentStats>;
    // Merge with defaults so missing fields from older schemas don't
    // produce NaN on accumulation.
    return {
      apiCalls: Number(parsed.apiCalls) || 0,
      toolCalls: parsed.toolCalls && typeof parsed.toolCalls === 'object' ?
          {...parsed.toolCalls} : {},
      promptTokens: Number(parsed.promptTokens) || 0,
      completionTokens: Number(parsed.completionTokens) || 0,
      totalTokens: Number(parsed.totalTokens) || 0,
      estimatedCost: Number(parsed.estimatedCost) || 0,
      lastReset: Number(parsed.lastReset) || base.lastReset,
    };
  } catch (_) {
    return base;
  }
}

let cachedStats: AgentStats = loadStats();

function saveStats() {
  localStorage.setItem(STATS_KEY, JSON.stringify(cachedStats));
}

// Cost rates follow pi-ai's convention: USD per 1,000,000 tokens.
// A token count of 1000 with costPerMTokPrompt=0.5 contributes
// 1000 * 0.5 / 1_000_000 = $0.0005.
export function recordApiCall(
    promptTokens: number, completionTokens: number,
    costPerMTokPrompt = 0, costPerMTokCompletion = 0) {
  const p = Number(promptTokens) || 0;
  const c = Number(completionTokens) || 0;
  cachedStats.apiCalls++;
  cachedStats.promptTokens += p;
  cachedStats.completionTokens += c;
  cachedStats.totalTokens += p + c;
  cachedStats.estimatedCost +=
      (p * costPerMTokPrompt + c * costPerMTokCompletion) / 1_000_000;
  saveStats();
}

export function recordToolCall(toolName: string) {
  cachedStats.toolCalls[toolName] =
      (cachedStats.toolCalls[toolName] || 0) + 1;
  saveStats();
}

export function getAgentStats(): AgentStats {
  return {...cachedStats, toolCalls: {...cachedStats.toolCalls}};
}

export function resetAgentStats() {
  cachedStats = defaultStats();
  saveStats();
}

// ---- Unique ID Generator ----

let uidCounter = 0;
export function uid(): string {
  return 'msg-' + (++uidCounter) + '-' + Date.now();
}

export function generateSessionId(): string {
  return Date.now() + '-' + Math.random().toString(36).slice(2, 8);
}
