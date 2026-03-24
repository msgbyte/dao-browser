// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
  soulPanel.style.display = tab === 'soul' ? '' : 'none';
  connectionPanel.style.display = tab === 'connection' ? '' : 'none';
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

function loadMemorySettings(): void {
  memoryEnabled.checked =
      localStorage.getItem('dao_memory_enabled') !== 'false';
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
}

function saveMemorySetting(key: string, value: string): void {
  localStorage.setItem(key, value);
}

memoryEnabled.addEventListener('change', () =>
    saveMemorySetting('dao_memory_enabled', String(memoryEnabled.checked)));
proactiveEnabled.addEventListener('change', () =>
    saveMemorySetting('dao_proactive_enabled', String(proactiveEnabled.checked)));
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
    saveMemorySetting('dao_proactive_threshold', s.dataset['value'] || 'balanced');
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
    if (!expanded) {
      endSession();
      sessionId = generateSessionId();
    }
  });
}

// ---- Proactive Suggestions ----

let currentSuggestionEpisodeId = 0;

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
  }, 150);
}

chipClose.addEventListener('click', (e) => {
  e.stopPropagation();
  hideChip();
  if (currentSuggestionEpisodeId) {
    callNativeArgs('dismissSuggestion', currentSuggestionEpisodeId).catch(() => {});
  }
});

proactiveChip.addEventListener('click', () => {
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
      (data: {text: string; episodeId: number}) => {
    showChip(data.text, data.episodeId);
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

function addToolMessage(name: string, result: string): void {
  const truncated =
      result.length > 200 ? result.substring(0, 200) + '...' : result;
  addMessageBubble('tool', '\u{1F527} ' + name + ': ' + truncated);
}

function showError(msg: string): void {
  addMessageBubble('error', msg);
}

// ---- Textarea auto-resize ----

userInput.addEventListener('input', () => {
  userInput.style.height = 'auto';
  userInput.style.height = Math.min(userInput.scrollHeight, 120) + 'px';
});

// ---- Send Message ----

userInput.addEventListener('keydown', (e: KeyboardEvent) => {
  if (e.key === 'Enter' && !e.shiftKey) {
    e.preventDefault();
    sendMessage();
  }
});
sendBtn.addEventListener('click', sendMessage);

async function sendMessage(): Promise<void> {
  const text = userInput.value.trim();
  if (!text || isStreaming) {
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
  isStreaming = true;
  sendBtn.disabled = true;

  // Hot-reload: read soul from localStorage on every conversation turn
  let soulContent = loadSoul();

  // Inject memory context if enabled
  if (memoryEnabled.checked && !memoryContextLoaded) {
    try {
      const pageInfo = await callNative('getPageInfo') as
          {url?: string; title?: string};
      if (pageInfo.url) {
        try {
          const urlObj = new URL(pageInfo.url);
          currentDomain = urlObj.hostname;
        } catch (_) {
          currentDomain = '';
        }
      }
      const ctx = await callNativeArgs('getMemoryContext',
          pageInfo.url || '', currentDomain, sessionId) as
          {preferences?: Array<{key: string; value: string}>;
           episodes?: Array<{intent: string; outcome: string}>;
           recentMessages?: Array<{role: string; content: string}>};

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
      }

      // First Memory Moment: show toast if this is the first time memory is used
      if (memoryBlock && !hasFirstMemory) {
        hasFirstMemory = true;
        if (!localStorage.getItem('dao_first_memory_shown')) {
          localStorage.setItem('dao_first_memory_shown', 'true');
          showToast('Memory activated — your Agent is learning');
        }
      }

      memoryContextLoaded = true;
    } catch (_) {
      // Memory context is best-effort
    }
  }

  const systemPrompt: ChatMessage = {
    role: 'system',
    content: soulContent,
  };

  try {
    let continueLoop = true;
    while (continueLoop) {
      continueLoop = false;

      const response = await callLLM([systemPrompt, ...messages]);

      if (response.error) {
        showError('API Error: ' + response.error);
        break;
      }

      const choice = response.choices && response.choices[0];
      if (!choice) {
        showError('No response from LLM');
        break;
      }

      const msg = choice.message;

      if (msg.tool_calls && msg.tool_calls.length > 0) {
        messages.push({
          role: 'assistant',
          content: msg.content || null,
          tool_calls: msg.tool_calls,
        });
        if (msg.content) {
          addMessageBubble('assistant', msg.content);
        }

        for (const tc of msg.tool_calls) {
          const fn = tc.function;
          let args: Record<string, string> = {};
          try {
            args = fn.arguments ? JSON.parse(fn.arguments) : {};
          } catch (_) {
            // ignore parse errors
          }

          let result: unknown;
          try {
            result = await executeTool(fn.name, args);
          } catch (e) {
            result = {error: (e as Error).message};
          }

          const resultStr =
              typeof result === 'string' ? result : JSON.stringify(result);
          addToolMessage(fn.name, resultStr);

          messages.push({
            role: 'tool',
            tool_call_id: tc.id,
            content: resultStr,
          });
        }

        continueLoop = true;
      } else {
        messages.push({role: 'assistant', content: msg.content || ''});
        addMessageBubble('assistant', msg.content || '', memoryContextLoaded);
      }
    }
  } catch (e) {
    showError('Error: ' + (e as Error).message);
  }

  isStreaming = false;
  sendBtn.disabled = false;
}

// ---- LLM API Call ----

async function callLLM(msgs: ChatMessage[]): Promise<LLMResponse> {
  const baseUrl = baseUrlInput.value.replace(/\/+$/, '');
  const url = baseUrl + '/chat/completions';

  const body = {
    model: modelInput.value,
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

  const resp = await fetch(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Authorization': 'Bearer ' + apiKeyInput.value,
    },
    body: JSON.stringify(body),
  });

  if (!resp.ok) {
    const text = await resp.text();
    return {error: resp.status + ' ' + text.substring(0, 200)};
  }

  return await resp.json();
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
