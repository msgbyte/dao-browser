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
}

// ---- Settings ----

const apiKeyInput =
    document.getElementById('apiKey') as HTMLInputElement;
const baseUrlInput =
    document.getElementById('baseUrl') as HTMLInputElement;
const modelInput =
    document.getElementById('model') as HTMLInputElement;
const settingsToggle =
    document.getElementById('settingsToggle') as HTMLElement;
const settingsPanel =
    document.getElementById('settingsPanel') as HTMLElement;

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

settingsToggle.addEventListener('click', () => {
  settingsPanel.classList.toggle('open');
  settingsToggle.classList.toggle('open');
});

// ---- Chat State ----

const messages: ChatMessage[] = [];
let isStreaming = false;
let pageContentInjected = false;

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
const cr = ((window as unknown as {cr: CrNamespace}).cr) || {} as CrNamespace;
(window as unknown as {cr: CrNamespace}).cr = cr;
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

function addMessageBubble(role: string, content: string): HTMLDivElement {
  emptyState.style.display = 'none';
  const div = document.createElement('div');
  div.className = 'message ' + role;
  if (role === 'assistant') {
    div.innerHTML = renderMarkdown(content);
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
    settingsPanel.classList.add('open');
    settingsToggle.classList.add('open');
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

  const systemPrompt: ChatMessage = {
    role: 'system',
    content:
        'You are Dao Agent, an AI assistant embedded in the Dao Browser sidebar. ' +
        'You can interact with the current webpage using the provided tools. ' +
        'Use tools when the user asks about page content or wants to perform actions on the page. ' +
        'Be concise and helpful. Reply in the same language the user uses.',
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
        addMessageBubble('assistant', msg.content || '');
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
