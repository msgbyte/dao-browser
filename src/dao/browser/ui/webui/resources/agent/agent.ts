// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Chromium WebUI enforces Trusted Types. Create a default policy that
// passes HTML through — our markdown renderer already escapes user input.
{
  const w = window as unknown as
      {trustedTypes?: {createPolicy: (name: string, rules: object) => void}};
  if (w.trustedTypes && w.trustedTypes.createPolicy) {
    w.trustedTypes.createPolicy('default', {
      createHTML: (s: string) => s,
      createScript: (s: string) => s,
      createScriptURL: (s: string) => s,
    });
  }
}

// ---- Interfaces ----

interface ChatMessage {
  role: 'system'|'user'|'assistant'|'tool';
  content: string|null;
  tool_calls?: ToolCall[];
  tool_call_id?: string;
  name?: string;
}

interface ToolCall {
  id: string;
  type: 'function';
  function: {name: string; arguments: string;};
}

interface LLMResponse {
  error?: string;
  choices?: Array<{
    message: {
      content: string | null; tool_calls?: ToolCall[];
    };
  }>;
}

interface ToolDefinition {
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
  addWebUIListener: (event: string, callback: (...args: any[]) => void) => void;
}

// ---- Default Soul Template ----

const DEFAULT_SOUL = `# SOUL.md - Who You Are

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

// ---- Soul Management ----

function loadSoul(): string {
  return localStorage.getItem('dao_agent_soul') || DEFAULT_SOUL;
}

function saveSoul(text: string): void {
  localStorage.setItem('dao_agent_soul', text);
}

// ---- Settings (Connection) ----

const apiKeyInput =
    document.getElementById('apiKey') as HTMLInputElement;
const baseUrlInput =
    document.getElementById('baseUrl') as HTMLInputElement;
const modelInput =
    document.getElementById('model') as HTMLInputElement;

function loadSettings(): void {
  apiKeyInput.value = localStorage.getItem('dao_agent_api_key') || '';
  baseUrlInput.value =
      localStorage.getItem('dao_agent_base_url') || 'https://api.openai.com/v1';
  modelInput.value =
      localStorage.getItem('dao_agent_model') || 'gpt-5';
}

function saveSettings(): void {
  localStorage.setItem('dao_agent_api_key', apiKeyInput.value);
  localStorage.setItem('dao_agent_base_url', baseUrlInput.value);
  localStorage.setItem('dao_agent_model', modelInput.value);
}

apiKeyInput.addEventListener('change', saveSettings);
baseUrlInput.addEventListener('change', saveSettings);
modelInput.addEventListener('change', saveSettings);
loadSettings();

// ---- Tab Switching ----

const chatView = document.getElementById('chatView') as HTMLElement;
const settingsView = document.getElementById('settingsView') as HTMLElement;
const mainTabs =
    document.querySelectorAll('.tab-bar .tab') as NodeListOf<HTMLElement>;
const subTabs =
    document.querySelectorAll('.settings-sub-tabs .sub-tab') as NodeListOf<HTMLElement>;
const soulPanel = document.getElementById('soulPanel') as HTMLElement;
const connectionPanel =
    document.getElementById('connectionPanel') as HTMLElement;
const soulEditor =
    document.getElementById('soulEditor') as HTMLTextAreaElement;
const saveSoulBtn =
    document.getElementById('saveSoulBtn') as HTMLButtonElement;
const resetSoulBtn =
    document.getElementById('resetSoulBtn') as HTMLButtonElement;
const saveStatus = document.getElementById('saveStatus') as HTMLElement;

function switchMainTab(tab: string): void {
  mainTabs.forEach(t => t.classList.toggle('active', t.dataset['tab'] === tab));
  chatView.style.display = tab === 'chat' ? '' : 'none';
  settingsView.style.display = tab === 'settings' ? '' : 'none';

  if (tab === 'settings') {
    soulEditor.value = loadSoul();
  }
}

function switchSettingsTab(tab: string): void {
  subTabs.forEach(
      t => t.classList.toggle('active', t.dataset['subtab'] === tab));
  connectionPanel.style.display = tab === 'connection' ? '' : 'none';
  soulPanel.style.display = tab === 'soul' ? '' : 'none';
  memoryPanel.style.display = tab === 'memory' ? '' : 'none';
  if (tab === 'memory') {
    loadStorageStats();
  }
}

mainTabs.forEach(t => {
  t.addEventListener('click', () => switchMainTab(t.dataset['tab'] || 'chat'));
});

subTabs.forEach(t => {
  t.addEventListener(
      'click', () => switchSettingsTab(t.dataset['subtab'] || 'soul'));
});

// Soul save/reset handlers
saveSoulBtn.addEventListener('click', () => {
  saveSoul(soulEditor.value);
  saveStatus.textContent = 'Saved';
  saveStatus.classList.add('visible');
  setTimeout(() => saveStatus.classList.remove('visible'), 2000);
});

resetSoulBtn.addEventListener('click', () => {
  soulEditor.value = DEFAULT_SOUL;
  saveSoul(DEFAULT_SOUL);
  saveStatus.textContent = 'Reset to default';
  saveStatus.classList.add('visible');
  setTimeout(() => saveStatus.classList.remove('visible'), 2000);
});

// Load soul editor content on startup
soulEditor.value = loadSoul();

// ---- Memory Settings ----

const memoryPanel =
    document.getElementById('memoryPanel') as HTMLElement;
const memoryEnabled =
    document.getElementById('memoryEnabled') as HTMLInputElement;
const proactiveEnabled =
    document.getElementById('proactiveEnabled') as HTMLInputElement;
const pageContextEnabled =
    document.getElementById('pageContextEnabled') as HTMLInputElement;
const conversationEnabled =
    document.getElementById('conversationEnabled') as HTMLInputElement;
const clearAllBtn =
    document.getElementById('clearAllBtn') as HTMLButtonElement;
const confirmScrim =
    document.getElementById('confirmScrim') as HTMLElement;
const confirmCancel =
    document.getElementById('confirmCancel') as HTMLButtonElement;
const confirmClear =
    document.getElementById('confirmClear') as HTMLButtonElement;
const chipArea =
    document.getElementById('chipArea') as HTMLElement;
const chipText =
    document.getElementById('chipText') as HTMLElement;
const chipClose =
    document.getElementById('chipClose') as HTMLButtonElement;
const proactiveChip =
    document.getElementById('proactiveChip') as HTMLElement;
const toastEl =
    document.getElementById('toast') as HTMLElement;
const startChatBtn =
    document.getElementById('startChatBtn') as HTMLButtonElement;

// Segment selector
const segments =
    document.querySelectorAll('.segment') as NodeListOf<HTMLButtonElement>;

// Map UI threshold names to numeric confidence values for the C++ engine.
const CONFIDENCE_THRESHOLD_MAP: Record<string, number> = {
  'quiet': 0.85,
  'balanced': 0.7,
  'active': 0.5,
};

function loadMemorySettings(): void {
  // Master switch is read from C++ pref (async); default to off until loaded.
  memoryEnabled.checked = false;
  proactiveEnabled.checked =
      localStorage.getItem('dao_proactive_enabled') !== 'false';
  pageContextEnabled.checked =
      localStorage.getItem('dao_page_context_enabled') !== 'false';
  conversationEnabled.checked =
      localStorage.getItem('dao_conversation_enabled') !== 'false';
  const threshold = localStorage.getItem('dao_proactive_threshold') || 'balanced';
  segments.forEach(s => {
    const isActive = s.dataset['value'] === threshold;
    s.classList.toggle('active', isActive);
    s.setAttribute('aria-checked', String(isActive));
  });

  // Fetch the real pref value from C++.
  callNativeArgs('getMemoryEnabled').then(enabled => {
    memoryEnabled.checked = !!enabled;
  }).catch(() => {});

  // Sync proactive settings to C++ engine on load.
  callNativeArgs('setProactiveEnabled', proactiveEnabled.checked).catch(() => {});
  callNativeArgs('setConfidenceThreshold',
      CONFIDENCE_THRESHOLD_MAP[threshold] ?? 0.7).catch(() => {});
}

function saveMemorySetting(key: string, value: string): void {
  localStorage.setItem(key, value);
}

memoryEnabled.addEventListener('change', () => {
  callNativeArgs('setMemoryEnabled', memoryEnabled.checked).catch(() => {});
});
proactiveEnabled.addEventListener('change', () => {
  saveMemorySetting('dao_proactive_enabled', String(proactiveEnabled.checked));
  callNativeArgs('setProactiveEnabled', proactiveEnabled.checked).catch(() => {});
});
pageContextEnabled.addEventListener('change', () =>
    saveMemorySetting('dao_page_context_enabled', String(pageContextEnabled.checked)));
conversationEnabled.addEventListener('change', () =>
    saveMemorySetting('dao_conversation_enabled', String(conversationEnabled.checked)));

segments.forEach(s => {
  s.addEventListener('click', () => {
    segments.forEach(seg => {
      seg.classList.remove('active');
      seg.setAttribute('aria-checked', 'false');
    });
    s.classList.add('active');
    s.setAttribute('aria-checked', 'true');
    const value = s.dataset['value'] || 'balanced';
    saveMemorySetting('dao_proactive_threshold', value);
    callNativeArgs('setConfidenceThreshold',
        CONFIDENCE_THRESHOLD_MAP[value] ?? 0.7).catch(() => {});
  });
});

// Arrow key navigation for segment selector
const segmentSelector = document.querySelector('.segment-selector');
if (segmentSelector) {
  segmentSelector.addEventListener('keydown', (e: Event) => {
    const ke = e as KeyboardEvent;
    if (ke.key !== 'ArrowLeft' && ke.key !== 'ArrowRight') return;
    const active = segmentSelector.querySelector('.segment.active') as HTMLButtonElement;
    const sibling = ke.key === 'ArrowRight'
        ? active?.nextElementSibling as HTMLButtonElement
        : active?.previousElementSibling as HTMLButtonElement;
    if (sibling) sibling.click();
  });
}

loadMemorySettings();

// ---- Session Management ----

let sessionId = Date.now() + '-' + Math.random().toString(36).slice(2, 8);
let currentDomain = '';
let hasFirstMemory = false;

function generateSessionId(): string {
  return Date.now() + '-' + Math.random().toString(36).slice(2, 8);
}

async function endSession(): Promise<void> {
  if (messages.length === 0 || !conversationEnabled.checked) return;
  const messageData = messages.map(m => ({
    role: m.role,
    content: m.content || '',
    pageUrl: currentDomain ? ('https://' + currentDomain) : '',
  }));
  try {
    await callNativeArgs('endSession', sessionId, messageData);
  } catch (_) {
    // Best-effort save
  }
}

// Set up the cr namespace early so listeners can be registered.
const cr = ((window as unknown as {cr: CrNamespace}).cr) || {} as CrNamespace;
(window as unknown as {cr: CrNamespace}).cr = cr;

// Listen for sidebar state changes (C++ push)
if (cr.addWebUIListener) {
  cr.addWebUIListener('sidebarStateChanged', (expanded: boolean) => {
    if (expanded) {
      // Delay to ensure WebView has rendered before focusing
      setTimeout(() => userInput.focus(), 100);
    } else {
      endSession();
      sessionId = generateSessionId();
    }
  });
}

// ---- Proactive Suggestions ----

let currentSuggestionEpisodeId = 0;

// Structured scenario data for the current suggestion (null = legacy episode).
let currentScenarioData: {
  scenarioId: string;
  scenarioName: string;
  actionLabel: string;
  actionPrompt: string;
  requiresPageContent: boolean;
  tabId: number;
  confidence: number;
} | null = null;

function showChip(text: string, episodeId: number): void {
  if (!proactiveEnabled.checked || !memoryEnabled.checked) return;
  chipText.textContent = text;
  currentSuggestionEpisodeId = episodeId;
  chipArea.style.display = '';
  chipArea.classList.remove('hiding');
}

function hideChip(): void {
  chipArea.classList.add('hiding');
  setTimeout(() => {
    chipArea.style.display = 'none';
    chipArea.classList.remove('hiding');
    currentScenarioData = null;
  }, 150);
}

chipClose.addEventListener('click', (e) => {
  e.stopPropagation();
  if (currentScenarioData) {
    // Structured scenario dismissal
    callNativeArgs('dismissSuggestion', {
      scenarioId: currentScenarioData.scenarioId,
      actionLabel: currentScenarioData.actionLabel,
    }).catch(() => {});
  } else if (currentSuggestionEpisodeId) {
    callNativeArgs('dismissSuggestion', currentSuggestionEpisodeId).catch(() => {});
  }
  hideChip();
});

proactiveChip.addEventListener('click', async () => {
  if (currentScenarioData) {
    // Structured scenario acceptance → fetch page content → LLM
    const scenario = currentScenarioData;
    callNativeArgs('acceptSuggestion', {
      scenarioId: scenario.scenarioId,
      actionLabel: scenario.actionLabel,
    }).catch(() => {});
    hideChip();

    let prompt = scenario.actionPrompt;
    if (scenario.requiresPageContent && scenario.tabId) {
      try {
        const result = await callNativeArgs(
            'getPageContentForScenario', scenario.tabId) as
            {text?: string} | null;
        if (result && result.text) {
          prompt = prompt.replace('{page_content}', result.text);
        }
      } catch (_) {
        // If page content fetch fails, send prompt without substitution
      }
    }
    // Send the assembled prompt to the LLM
    userInput.value = prompt;
    sendMessage();
    return;
  }

  // Legacy episode-based flow
  if (currentSuggestionEpisodeId) {
    callNativeArgs('acceptSuggestion', currentSuggestionEpisodeId).catch(() => {});
  }
  const text = chipText.textContent || '';
  hideChip();
  if (text) {
    userInput.value = text;
    sendMessage();
  }
});

proactiveChip.addEventListener('keydown', (e: KeyboardEvent) => {
  if (e.key === 'Enter') proactiveChip.click();
  if (e.key === 'Escape') chipClose.click();
});

// Listen for proactive suggestions from C++
if (cr.addWebUIListener) {
  cr.addWebUIListener('proactiveSuggestion',
      (data: {
        text: string;
        episodeId?: number;
        actionType?: number;
        scenarioId?: string;
        scenarioName?: string;
        actionLabel?: string;
        actionPrompt?: string;
        requiresPageContent?: boolean;
        tabId?: number;
        confidence?: number;
      }) => {
    if (data.scenarioId) {
      // Structured scenario suggestion
      currentScenarioData = {
        scenarioId: data.scenarioId,
        scenarioName: data.scenarioName || '',
        actionLabel: data.actionLabel || '',
        actionPrompt: data.actionPrompt || '',
        requiresPageContent: !!data.requiresPageContent,
        tabId: data.tabId || 0,
        confidence: data.confidence || 0,
      };
      showChip(data.actionLabel || data.text, 0);
    } else {
      // Legacy episode-based suggestion
      currentScenarioData = null;
      showChip(data.text, data.episodeId || 0);
    }
  });
}

// ---- Toast ----

function showToast(text: string, duration = 3000): void {
  toastEl.textContent = text;
  toastEl.style.display = '';
  setTimeout(() => { toastEl.style.display = 'none'; }, duration);
}

// ---- Clear All Memory ----

clearAllBtn.addEventListener('click', () => {
  confirmScrim.style.display = '';
});

confirmCancel.addEventListener('click', () => {
  confirmScrim.style.display = 'none';
});

confirmScrim.addEventListener('click', (e) => {
  if (e.target === confirmScrim) confirmScrim.style.display = 'none';
});

confirmClear.addEventListener('click', async () => {
  confirmScrim.style.display = 'none';
  try {
    await callNativeArgs('clearAllMemory');
    showToast('All memory cleared');
    loadStorageStats();
  } catch (_) {
    showToast('Failed to clear memory');
  }
});

// ---- Storage Stats ----

async function loadStorageStats(): Promise<void> {
  try {
    const stats = await callNativeArgs('getStorageStats') as
        {totalSize?: number; conversationCount?: number;
         episodeCount?: number; preferenceCount?: number};
    const el = (id: string) => document.getElementById(id);
    const setCount = (id: string, v?: number) => {
      const node = el(id);
      if (node) node.textContent = String(v || 0);
    };
    setCount('statConversations', stats.conversationCount);
    setCount('statPreferences', stats.preferenceCount);
    setCount('statEpisodes', stats.episodeCount);
    const totalEl = el('statTotal');
    if (totalEl) {
      const kb = ((stats.totalSize || 0) / 1024).toFixed(1);
      totalEl.textContent = 'Total: ' + kb + ' KB';
    }
  } catch (_) {
    // Stats are non-critical
  }
}

// ---- Chat State ----

const messages: ChatMessage[] = [];
let isStreaming = false;
let currentAbortController: AbortController|null = null;
let pageContentInjected = false;
let memoryContextLoaded = false;

const chatArea = document.getElementById('chatArea') as HTMLElement;
const emptyState = document.getElementById('emptyState') as HTMLElement;
const userInput =
    document.getElementById('userInput') as HTMLTextAreaElement;
const sendBtn =
    document.getElementById('sendBtn') as HTMLButtonElement;
const pageBtn =
    document.getElementById('pageBtn') as HTMLButtonElement;

// ---- Chat History Persistence ----

const CHAT_HISTORY_KEY = 'dao_agent_chat_history';

function saveChatHistory(): void {
  // Save only user and assistant messages (skip tool internals for simplicity)
  const toSave = messages.filter(
      m => m.role === 'user' || (m.role === 'assistant' && m.content));
  try {
    localStorage.setItem(CHAT_HISTORY_KEY, JSON.stringify(toSave));
  } catch (_) {
    // Storage full — best effort
  }
}

function loadChatHistory(): void {
  try {
    const raw = localStorage.getItem(CHAT_HISTORY_KEY);
    if (!raw) return;
    const saved = JSON.parse(raw) as ChatMessage[];
    if (!Array.isArray(saved) || saved.length === 0) return;

    emptyState.style.display = 'none';
    for (const msg of saved) {
      messages.push(msg);
      addMessageBubble(msg.role, msg.content || '');
    }
  } catch (_) {
    // Corrupt data — ignore
  }
}

function clearChatHistory(): void {
  messages.length = 0;
  localStorage.removeItem(CHAT_HISTORY_KEY);
  // Clear chat area DOM and show empty state
  while (chatArea.firstChild) {
    chatArea.removeChild(chatArea.firstChild);
  }
  // Re-add the empty state
  chatArea.appendChild(emptyState);
  emptyState.style.display = '';
  // Reset session
  memoryContextLoaded = false;
  pageContentInjected = false;
  sessionId = generateSessionId();
}

// Restore history on load
loadChatHistory();

// ---- Tools Definition ----

const tools: ToolDefinition[] = [
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

// ---- Chrome.send Bridge ----

const pendingCallbacks: Record<string, PendingCallback> = {};
let callbackCounter = 0;

// Set up the cr.webUIResponse callback that Chromium's
// ResolveJavascriptCallback / RejectJavascriptCallback invokes.
cr.webUIResponse =
    function(id: string, isSuccess: boolean, response: unknown): void {
  const entry = pendingCallbacks[id];
  if (!entry) {
    return;
  }
  delete pendingCallbacks[id];
  if (isSuccess) {
    entry.resolve(response);
  } else {
    entry.reject(response);
  }
};

function callNative(
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

// Variant that sends multiple positional args (not wrapped in an object)
function callNativeArgs(
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

// ---- Markdown Rendering ----

function escapeHtml(text: string): string {
  const el = document.createElement('div');
  el.textContent = text;
  return el.innerHTML;
}

function renderMarkdown(text: string): string {
  let html = escapeHtml(text);

  // Code blocks
  html = html.replace(
      /```(\w*)\n([\s\S]*?)```/g, '<pre><code>$2</code></pre>');
  // Inline code
  html = html.replace(/`([^`]+)`/g, '<code>$1</code>');
  // Bold
  html = html.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
  // Italic
  html = html.replace(/\*(.+?)\*/g, '<em>$1</em>');
  // Unordered lists
  html = html.replace(/^[-*] (.+)$/gm, '<li>$1</li>');
  html = html.replace(/(<li>.*<\/li>\n?)+/g, '<ul>$&</ul>');
  // Ordered lists
  html = html.replace(/^\d+\. (.+)$/gm, '<li>$1</li>');

  return html;
}

// ---- UI Helpers ----

function addMessageBubble(
    role: string, content: string, usedMemory = false): HTMLDivElement {
  emptyState.style.display = 'none';
  const div = document.createElement('div');
  div.className = 'message ' + role;
  if (role === 'assistant') {
    div.innerHTML = renderMarkdown(content);
    if (usedMemory && memoryContextLoaded) {
      const badge = document.createElement('span');
      badge.className = 'memory-badge';
      badge.textContent = '\u2727';
      badge.title = 'This response used your memory';
      div.appendChild(badge);
    }
  } else {
    div.textContent = content;
  }
  chatArea.appendChild(div);
  chatArea.scrollTop = chatArea.scrollHeight;
  return div;
}

// ---- Thinking Indicator ----

function addThinkingIndicator(): HTMLDivElement {
  emptyState.style.display = 'none';
  const div = document.createElement('div');
  div.className = 'message assistant';
  div.innerHTML = '<span class="typing-indicator"><span></span><span></span><span></span></span>';
  chatArea.appendChild(div);
  chatArea.scrollTop = chatArea.scrollHeight;
  return div;
}

function removeThinkingIndicator(el: HTMLDivElement|null): void {
  if (el && el.parentNode) {
    el.parentNode.removeChild(el);
  }
}

// ---- Streaming Bubble ----

interface StreamingBubble {
  el: HTMLDivElement;
  appendToken: (text: string) => void;
  finish: (usedMemory: boolean) => void;
}

function createStreamingBubble(): StreamingBubble {
  emptyState.style.display = 'none';
  const div = document.createElement('div');
  div.className = 'message assistant streaming';
  chatArea.appendChild(div);
  chatArea.scrollTop = chatArea.scrollHeight;

  let buffer = '';
  let renderTimer: ReturnType<typeof setTimeout>|null = null;

  function render(): void {
    renderTimer = null;
    div.innerHTML = renderMarkdown(buffer);
    chatArea.scrollTop = chatArea.scrollHeight;
  }

  return {
    el: div,
    appendToken(text: string): void {
      buffer += text;
      if (!renderTimer) {
        renderTimer = setTimeout(render, 50);
      }
    },
    finish(usedMemory: boolean): void {
      if (renderTimer) {
        clearTimeout(renderTimer);
      }
      div.classList.remove('streaming');
      div.innerHTML = renderMarkdown(buffer);
      if (usedMemory) {
        const badge = document.createElement('span');
        badge.className = 'memory-badge';
        badge.textContent = '\u2727';
        badge.title = 'This response used your memory';
        div.appendChild(badge);
      }
      chatArea.scrollTop = chatArea.scrollHeight;
    },
  };
}

// ---- Tool Call Bubble ----

function addToolCallBubble(name: string): HTMLDivElement {
  const div = document.createElement('div');
  div.className = 'message tool-call';
  div.innerHTML =
      '<div class="tool-call-header">' +
      '<span class="tool-call-spinner"></span>' +
      '<span class="tool-call-name">' + escapeHtml(name) + '</span>' +
      '<span class="tool-call-status">running...</span>' +
      '</div>' +
      '<div class="tool-call-result"></div>';
  chatArea.appendChild(div);
  chatArea.scrollTop = chatArea.scrollHeight;
  return div;
}

function updateToolCallBubble(
    el: HTMLDivElement, result: string, isError: boolean): void {
  const header = el.querySelector('.tool-call-header');
  const statusEl = el.querySelector('.tool-call-status');
  const spinnerEl = el.querySelector('.tool-call-spinner');
  const resultEl = el.querySelector('.tool-call-result');

  if (spinnerEl) {
    spinnerEl.outerHTML = isError ? '<span class="tool-call-icon">\u2717</span>'
                                  : '<span class="tool-call-icon">\u2713</span>';
  }
  if (statusEl) {
    statusEl.textContent = isError ? 'failed' : 'done';
  }
  el.classList.add(isError ? 'error' : 'success');

  if (resultEl && result) {
    const truncated = result.length > 300 ? result.substring(0, 300) + '...' : result;
    resultEl.textContent = truncated;
    // Add toggle button
    const toggle = document.createElement('button');
    toggle.className = 'tool-call-toggle';
    toggle.textContent = 'show detail';
    toggle.addEventListener('click', (e) => {
      e.stopPropagation();
      const expanded = resultEl.classList.toggle('expanded');
      toggle.textContent = expanded ? 'hide detail' : 'show detail';
    });
    if (header) {
      header.appendChild(toggle);
    }
  }
  chatArea.scrollTop = chatArea.scrollHeight;
}

// ---- Error Bubble ----

function showErrorBubble(shortMsg: string, fullError: string): void {
  emptyState.style.display = 'none';
  const div = document.createElement('div');
  div.className = 'message error';
  div.innerHTML =
      '<div class="error-summary">' +
      '<span class="error-icon">\u26A0</span>' +
      '<span class="error-text">' + escapeHtml(shortMsg) + '</span>' +
      '</div>' +
      '<div class="error-detail">' + escapeHtml(fullError) + '</div>';
  div.addEventListener('click', () => {
    const detail = div.querySelector('.error-detail');
    if (detail) {
      detail.classList.toggle('expanded');
    }
  });
  chatArea.appendChild(div);
  chatArea.scrollTop = chatArea.scrollHeight;
}

function showError(msg: string): void {
  showErrorBubble(msg.length > 80 ? msg.substring(0, 80) + '...' : msg, msg);
}

// ---- Textarea auto-resize ----

userInput.addEventListener('input', () => {
  userInput.style.height = 'auto';
  userInput.style.height = Math.min(userInput.scrollHeight, 120) + 'px';
});

// ---- Send Message ----

let isComposing = false;
userInput.addEventListener('compositionstart', () => { isComposing = true; });
userInput.addEventListener('compositionend', () => { isComposing = false; });

userInput.addEventListener('keydown', (e: KeyboardEvent) => {
  // Skip during IME composition
  if (isComposing || e.isComposing) return;

  // Slash menu keyboard navigation
  if (slashMenuVisible) {
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      selectSlashMenuItem(
          Math.min(slashMenuIndex + 1, slashMenuFiltered.length - 1));
      return;
    }
    if (e.key === 'ArrowUp') {
      e.preventDefault();
      selectSlashMenuItem(Math.max(slashMenuIndex - 1, 0));
      return;
    }
    if (e.key === 'Tab' || (e.key === 'Enter' && !e.shiftKey)) {
      e.preventDefault();
      if (slashMenuIndex >= 0) {
        executeSlashMenuItem(slashMenuIndex);
      }
      return;
    }
    if (e.key === 'Escape') {
      e.preventDefault();
      hideSlashMenu();
      return;
    }
  }

  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    sendMessage();
  }
});
sendBtn.addEventListener('click', () => {
  if (isStreaming) {
    stopStreaming();
  } else {
    sendMessage();
  }
});

function stopStreaming(): void {
  if (currentAbortController) {
    currentAbortController.abort();
    currentAbortController = null;
  }
}

function setStreamingUI(streaming: boolean): void {
  isStreaming = streaming;
  sendBtn.disabled = false;
  sendBtn.classList.toggle('streaming', streaming);
}

// ---- Slash Commands ----

interface SlashCommand {
  name: string;
  description: string;
  action: () => void;
}

const slashCommands: SlashCommand[] = [
  {name: '/clear', description: 'Clear chat history', action: () => {
    clearChatHistory();
    showToast('Chat history cleared');
  }},
  {name: '/reset', description: 'Reset session & memory context', action: () => {
    clearChatHistory();
    memoryContextLoaded = false;
    showToast('Session reset');
  }},
  {name: '/help', description: 'Show available commands', action: () => {
    const helpText = slashCommands.map(c => c.name + ' — ' + c.description).join('\n');
    addMessageBubble('system-msg', helpText);
  }},
];

// ---- Slash Menu UI ----

const slashMenu = document.getElementById('slashMenu') as HTMLElement;
let slashMenuIndex = -1;
let slashMenuVisible = false;
let slashMenuFiltered: SlashCommand[] = [];

function showSlashMenu(filter: string): void {
  const query = filter.toLowerCase();
  slashMenuFiltered = slashCommands.filter(
      c => c.name.startsWith(query) || query === '/');
  if (slashMenuFiltered.length === 0) {
    hideSlashMenu();
    return;
  }
  slashMenuIndex = 0;
  slashMenu.innerHTML = '';
  for (let i = 0; i < slashMenuFiltered.length; i++) {
    const cmd = slashMenuFiltered[i]!;
    const item = document.createElement('div');
    item.className = 'slash-menu-item' + (i === 0 ? ' selected' : '');
    item.setAttribute('role', 'option');
    item.innerHTML =
        '<span class="slash-menu-item-name">' + escapeHtml(cmd.name) + '</span>' +
        '<span class="slash-menu-item-desc">' + escapeHtml(cmd.description) + '</span>';
    item.addEventListener('mouseenter', () => {
      selectSlashMenuItem(i);
    });
    item.addEventListener('click', () => {
      executeSlashMenuItem(i);
    });
    slashMenu.appendChild(item);
  }
  slashMenu.style.display = '';
  slashMenuVisible = true;
}

function hideSlashMenu(): void {
  slashMenu.style.display = 'none';
  slashMenuVisible = false;
  slashMenuIndex = -1;
}

function selectSlashMenuItem(index: number): void {
  const items = slashMenu.querySelectorAll('.slash-menu-item');
  items.forEach((el, i) => el.classList.toggle('selected', i === index));
  slashMenuIndex = index;
}

function executeSlashMenuItem(index: number): void {
  const cmd = slashMenuFiltered[index];
  if (!cmd) return;
  userInput.value = cmd.name;
  hideSlashMenu();
  sendMessage();
}

// Show/hide slash menu on input
userInput.addEventListener('input', () => {
  const text = userInput.value;
  if (text.startsWith('/') && !isStreaming) {
    showSlashMenu(text);
  } else {
    hideSlashMenu();
  }
});

function handleSlashCommand(text: string): boolean {
  const cmd = text.toLowerCase().trim();
  const matched = slashCommands.find(c => c.name === cmd);
  if (matched) {
    matched.action();
    return true;
  }
  return false;
}

async function sendMessage(): Promise<void> {
  const text = userInput.value.trim();
  if (!text || isStreaming) {
    return;
  }

  // Handle slash commands
  if (text.startsWith('/')) {
    userInput.value = '';
    userInput.style.height = 'auto';
    hideSlashMenu();
    if (handleSlashCommand(text)) return;
    // Unknown command — show error
    showToast('Unknown command: ' + text);
    return;
  }

  if (!apiKeyInput.value) {
    showError('Please set your API Key in Settings first.');
    switchMainTab('settings');
    switchSettingsTab('connection');
    return;
  }

  messages.push({role: 'user', content: text});
  addMessageBubble('user', text);
  saveChatHistory();
  userInput.value = '';
  userInput.style.height = 'auto';

  await runConversation();
}

// ---- Page Content Injection ----

pageBtn.addEventListener('click', async () => {
  if (isStreaming) {
    return;
  }
  pageBtn.classList.add('active');
  try {
    const content = await callNative('getPageContent') as
        {text?: string};
    const info = await callNative('getPageInfo') as
        {title?: string; url?: string};

    const contextMsg = 'Current page: ' + (info.title || 'Unknown') +
        '\nURL: ' + (info.url || 'Unknown') + '\n\nPage content:\n' +
        (content.text || '(empty)');

    messages.push(
        {role: 'user', content: '[Page context injected]\n\n' + contextMsg});
    addMessageBubble(
        'system-msg',
        '\u{1F4C4} Page content loaded (' +
            (content.text || '').length + ' chars)');
    pageContentInjected = true;
  } catch (e) {
    showError('Failed to get page content: ' + (e as Error).message);
  }
  pageBtn.classList.remove('active');
});

// ---- Conversation Loop (with function calling) ----

async function runConversation(): Promise<void> {
  setStreamingUI(true);
  currentAbortController = new AbortController();

  // Hot-reload: read soul from localStorage on every conversation turn
  let soulContent = loadSoul();

  // Inject memory context if enabled (best-effort, non-blocking with 3s timeout).
  if (memoryEnabled.checked && !memoryContextLoaded) {
    try {
      const memoryTimeout = new Promise<never>((_, rej) =>
          setTimeout(() => rej(new Error('memory timeout')), 3000));
      const memoryLoad = (async () => {
        const pageInfo = await callNative('getPageInfo') as
            {url?: string; title?: string};
        if (pageInfo.url) {
          try {
            currentDomain = new URL(pageInfo.url).hostname;
          } catch (_) {
            currentDomain = '';
          }
        }
        return await callNativeArgs('getMemoryContext',
            pageInfo.url || '', currentDomain, sessionId) as
            {preferences?: Array<{key: string; value: string}>;
             episodes?: Array<{intent: string; outcome: string}>};
      })();
      const ctx = await Promise.race([memoryLoad, memoryTimeout]);

      let memoryBlock = '';
      if (ctx.preferences && ctx.preferences.length > 0) {
        memoryBlock += '\n\n## User Preferences (from memory)\n';
        for (const p of ctx.preferences) {
          memoryBlock += '- ' + p.key + ': ' + p.value + '\n';
        }
      }
      if (ctx.episodes && ctx.episodes.length > 0) {
        memoryBlock += '\n## Previous context on this page\n';
        for (const e of ctx.episodes) {
          memoryBlock += '- Intent: ' + e.intent + ' → Outcome: ' + e.outcome + '\n';
        }
      }
      if (memoryBlock) {
        soulContent += memoryBlock;
        if (!hasFirstMemory) {
          hasFirstMemory = true;
          if (!localStorage.getItem('dao_first_memory_shown')) {
            localStorage.setItem('dao_first_memory_shown', 'true');
            showToast('Memory activated — your Agent is learning');
          }
        }
      }
      memoryContextLoaded = true;
    } catch (_) {
      memoryContextLoaded = true;
    }
  }

  const systemPrompt: ChatMessage = {role: 'system', content: soulContent};

  try {
    let continueLoop = true;
    while (continueLoop) {
      continueLoop = false;

      // Show thinking indicator immediately
      let thinkingEl: HTMLDivElement|null = addThinkingIndicator();
      let streamBubble: StreamingBubble|null = null;
      let hadError = false;
      const toolCallBubbles: Record<string, HTMLDivElement> = {};
      const signal = currentAbortController?.signal;

      await new Promise<void>((resolve) => {
        callLLMStreaming([systemPrompt, ...messages], {
          onToken(text: string): void {
            // First token: replace thinking indicator with streaming bubble
            if (thinkingEl) {
              removeThinkingIndicator(thinkingEl);
              thinkingEl = null;
              streamBubble = createStreamingBubble();
            }
            if (streamBubble) {
              streamBubble.appendToken(text);
            }
          },

          onToolCall(tc: ToolCall): void {
            // Remove thinking indicator if still showing
            if (thinkingEl) {
              removeThinkingIndicator(thinkingEl);
              thinkingEl = null;
            }
            const bubble = addToolCallBubble(tc.function.name);
            toolCallBubbles[tc.id] = bubble;
          },

          onDone(fullContent: string, toolCalls: ToolCall[]): void {
            // Clean up thinking indicator if no content was received
            if (thinkingEl) {
              removeThinkingIndicator(thinkingEl);
              thinkingEl = null;
            }

            if (toolCalls.length > 0) {
              // Finish any streaming content first
              if (streamBubble) {
                streamBubble.finish(false);
                streamBubble = null;
              }

              messages.push({
                role: 'assistant',
                content: fullContent || null,
                tool_calls: toolCalls,
              });

              // Execute tool calls and continue
              (async () => {
                try {
                  for (const tc of toolCalls) {
                    const fn = tc.function;
                    let args: Record<string, string> = {};
                    try {
                      args = fn.arguments ? JSON.parse(fn.arguments) : {};
                    } catch (_) {
                      // ignore parse errors
                    }

                    const bubble = toolCallBubbles[tc.id];

                    let result: unknown;
                    let isToolError = false;
                    try {
                      result = await executeTool(fn.name, args);
                    } catch (e) {
                      result = {error: (e as Error).message};
                      isToolError = true;
                    }

                    const resultStr =
                        typeof result === 'string' ? result : JSON.stringify(result);

                    if (bubble) {
                      updateToolCallBubble(bubble, resultStr, isToolError);
                    }

                    messages.push({
                      role: 'tool',
                      tool_call_id: tc.id,
                      content: resultStr,
                    });
                  }

                  continueLoop = true;
                } catch (e) {
                  hadError = true;
                  showErrorBubble('Tool Error', (e as Error).message || 'Tool execution failed');
                }
                resolve();
              })();
            } else {
              // No tool calls — just finish the streaming bubble
              if (streamBubble) {
                streamBubble.finish(memoryContextLoaded);
              } else if (fullContent) {
                addMessageBubble('assistant', fullContent, memoryContextLoaded);
              }
              messages.push({role: 'assistant', content: fullContent || ''});
              saveChatHistory();
              resolve();
            }
          },

          onError(shortMsg: string, fullError: string): void {
            if (thinkingEl) {
              removeThinkingIndicator(thinkingEl);
              thinkingEl = null;
            }
            hadError = true;
            showErrorBubble(shortMsg, fullError);
            resolve();
          },
        }, signal).catch((e: Error) => {
          if (thinkingEl) {
            removeThinkingIndicator(thinkingEl);
            thinkingEl = null;
          }
          if (e.name === 'AbortError') {
            // User cancelled — finish any partial streaming content
            if (streamBubble) {
              streamBubble.finish(false);
            }
            hadError = true;
            resolve();
            return;
          }
          hadError = true;
          showErrorBubble(
              'Network Error',
              e.message || 'Failed to connect to API');
          resolve();
        });
      });

      if (hadError) break;
    }
  } catch (e) {
    if ((e as Error).name !== 'AbortError') {
      showErrorBubble('Error', (e as Error).message || 'Unknown error');
    }
  } finally {
    currentAbortController = null;
    setStreamingUI(false);
  }
}

// ---- LLM API Call (Streaming) ----

interface StreamCallbacks {
  onToken: (text: string) => void;
  onToolCall: (toolCall: ToolCall) => void;
  onDone: (fullContent: string, toolCalls: ToolCall[]) => void;
  onError: (shortMsg: string, fullError: string) => void;
}

async function callLLMStreaming(
    msgs: ChatMessage[], callbacks: StreamCallbacks,
    signal?: AbortSignal): Promise<void> {
  let baseUrl = baseUrlInput.value.replace(/\/+$/, '');
  if (!baseUrl.endsWith('/v1')) {
    baseUrl += '/v1';
  }
  const url = baseUrl + '/chat/completions';

  const body = {
    model: modelInput.value,
    stream: true,
    messages: msgs.map(m => {
      const obj: Record<string, unknown> = {role: m.role, content: m.content};
      if (m.tool_calls) {
        obj['tool_calls'] = m.tool_calls;
      }
      if (m.tool_call_id) {
        obj['tool_call_id'] = m.tool_call_id;
      }
      if (m.name) {
        obj['name'] = m.name;
      }
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
        'Authorization': 'Bearer ' + apiKeyInput.value,
      },
      body: JSON.stringify(body),
      signal,
    });
  } catch (e) {
    // fetch itself can throw (network error, CORS, AbortError, etc.)
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
  const toolCallMap: Record<number, {id: string; type: 'function'; function: {name: string; arguments: string}}> = {};
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
              if (tc.function?.name) toolCallMap[idx].function.name += tc.function.name;
              if (tc.function?.arguments) toolCallMap[idx].function.arguments += tc.function.arguments;

              if (toolCallMap[idx].function.name && !toolCallsEmitted.has(idx)) {
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
    // Stream read error — still deliver whatever we have
    callbacks.onError('Stream Error', err.message || 'Error reading response stream');
    return;
  }

  const allToolCalls = Object.values(toolCallMap);
  callbacks.onDone(fullContent, allToolCalls);
}

// ---- Tool Execution ----

async function executeTool(
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

// ---- Start Chatting Button ----

startChatBtn.addEventListener('click', () => {
  userInput.focus();
});

// Auto-focus input on page load
setTimeout(() => userInput.focus(), 100);
