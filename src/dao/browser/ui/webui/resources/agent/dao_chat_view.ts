// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, css, html, nothing} from
    '//resources/lit/v3_0/lit.rollup.js';

import {
  BASE_SYSTEM_PROMPT,
  callLLMStreaming,
  callNative,
  callNativeArgs,
  cr,
  currentSoulContent,
  executeTool,
  generateSessionId,
  refreshSoulContent,
  renderMarkdown,
  soulChannel,
  uid,
} from './agent_bridge.js';
import type {
  ChatMessage,
  ScenarioData,
  StreamCallbacks,
  ToolCall,
  UIMessage,
} from './agent_bridge.js';

interface SlashCommand {
  name: string;
  description: string;
  action: () => void;
}

const CHAT_HISTORY_KEY = 'dao_agent_chat_history';

export class DaoChatView extends CrLitElement {
  static override get properties() {
    return {
      uiMessages_: {type: Array, state: true},
      isStreaming_: {type: Boolean, state: true},
      isThinking_: {type: Boolean, state: true},
      streamingContent_: {type: String, state: true},
      chipText_: {type: String, state: true},
      chipVisible_: {type: Boolean, state: true},
      chipHiding_: {type: Boolean, state: true},
      slashMenuVisible_: {type: Boolean, state: true},
      slashMenuItems_: {type: Array, state: true},
      slashMenuIndex_: {type: Number, state: true},
    };
  }

  private uiMessages_: UIMessage[] = [];
  private isStreaming_ = false;
  private isThinking_ = false;
  private streamingContent_: string|null = null;
  private chipText_ = '';
  private chipVisible_ = false;
  private chipHiding_ = false;
  private slashMenuVisible_ = false;
  private slashMenuItems_: SlashCommand[] = [];
  private slashMenuIndex_ = -1;

  // Internal state not triggering re-render
  private messages_: ChatMessage[] = [];
  private currentAbortController_: AbortController|null = null;
  private memoryContextLoaded_ = false;
  private sessionId_ = generateSessionId();
  private currentDomain_ = '';
  private hasFirstMemory_ = false;
  private isComposing_ = false;
  private rawStreamingContent_ = '';
  private streamRenderTimer_ = 0;
  private currentSuggestionEpisodeId_ = 0;
  private currentScenarioData_: ScenarioData|null = null;
  private streamUsedMemory_ = false;

  private readonly slashCommands_: SlashCommand[] = [
    {name: '/clear', description: 'Clear chat history', action: () => {
      this.clearChatHistory_();
      this.fireToast_('Chat history cleared');
    }},
    {name: '/reset', description: 'Reset session & memory context',
      action: () => {
        this.clearChatHistory_();
        this.memoryContextLoaded_ = false;
        this.fireToast_('Session reset');
      }},
    {name: '/help', description: 'Show available commands', action: () => {
      const helpText = this.slashCommands_
          .map(c => c.name + ' — ' + c.description).join('\n');
      this.pushUIMessage_('system-msg', helpText);
    }},
  ];

  static override get styles() {
    return css`
      :host {
        display: flex;
        flex-direction: column;
        flex: 1;
        overflow: hidden;
      }
      :host([hidden]) { display: none !important; }

      /* Chip area */
      .chip-area {
        padding: 6px 14px 0; flex-shrink: 0;
        animation: chipSlideDown 200ms ease-out;
      }
      .chip-area.hiding {
        animation: chipFadeOut 150ms ease-in forwards;
      }
      .chip {
        display: flex; align-items: center; height: 32px;
        padding: 0 8px 0 10px; background: var(--accent-dim);
        border-radius: 8px; cursor: pointer; gap: 6px;
      }
      .chip:hover { background: rgba(140, 100, 220, 0.4); }
      .chip-icon { flex-shrink: 0; color: var(--accent); }
      .chip-text {
        flex: 1; font-size: 13px; color: var(--text);
        overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
      }
      .chip-close {
        width: 20px; height: 20px;
        display: flex; align-items: center; justify-content: center;
        background: none; border: none; border-radius: 4px;
        color: var(--text-secondary); cursor: pointer;
        font-size: 14px; flex-shrink: 0;
      }
      .chip-close:hover {
        background: rgba(255,255,255,0.1); color: var(--text);
      }
      @keyframes chipSlideDown {
        from { opacity: 0; transform: translateY(-8px); }
        to { opacity: 1; transform: translateY(0); }
      }
      @keyframes chipFadeOut {
        from { opacity: 1; } to { opacity: 0; }
      }

      /* Chat area */
      .chat-area {
        flex: 1; overflow-y: auto; padding: 10px 14px;
        display: flex; flex-direction: column; gap: 6px;
      }
      .chat-area::-webkit-scrollbar { width: 4px; }
      .chat-area::-webkit-scrollbar-track { background: transparent; }
      .chat-area::-webkit-scrollbar-thumb {
        background: rgba(255,255,255,0.15); border-radius: 2px;
      }

      /* Messages */
      .message {
        max-width: 95%; padding: 8px 12px; border-radius: var(--radius);
        font-size: 13px; line-height: 1.5;
        word-break: break-word;
      }
      .message.user {
        align-self: flex-end; background: var(--user-bubble);
        border-bottom-right-radius: 4px; white-space: pre-wrap;
      }
      .message.assistant {
        align-self: flex-start; background: var(--assistant-bubble);
        border-bottom-left-radius: 4px; position: relative;
      }
      .message.system-msg {
        align-self: center; color: var(--text-tertiary);
        font-size: 11px; text-align: center;
        background: none; padding: 2px 4px;
      }

      /* Markdown — Paragraphs */
      .message.assistant p {
        margin: 0 0 6px 0;
      }
      .message.assistant p:last-child { margin-bottom: 0; }

      /* Markdown — Headings */
      .message.assistant .md-heading {
        margin: 10px 0 4px 0; font-weight: 600; line-height: 1.3;
        color: var(--text);
      }
      .message.assistant h1.md-heading { font-size: 17px; }
      .message.assistant h2.md-heading { font-size: 15px; }
      .message.assistant h3.md-heading { font-size: 14px; }
      .message.assistant h4.md-heading,
      .message.assistant h5.md-heading,
      .message.assistant h6.md-heading { font-size: 13px; }
      .message.assistant .md-heading:first-child { margin-top: 0; }

      /* Markdown — Inline code */
      .message.assistant code {
        background: rgba(255,255,255,0.1);
        padding: 1px 5px; border-radius: 4px;
        font-family: ui-monospace, 'SF Mono', Menlo, monospace;
        font-size: 12px;
      }

      /* Markdown — Code blocks */
      .message.assistant .md-code-block {
        position: relative; margin: 6px 0;
        border-radius: 8px; overflow: hidden;
        background: rgba(0,0,0,0.35);
      }
      .message.assistant .md-code-block pre {
        margin: 0; padding: 10px 12px;
        overflow-x: auto; white-space: pre;
      }
      .message.assistant .md-code-block code {
        background: none; padding: 0; border-radius: 0;
        font-size: 12px; line-height: 1.45;
        color: rgba(255,255,255,0.82);
      }
      .message.assistant .md-code-lang {
        position: absolute; top: 0; right: 0;
        padding: 2px 8px;
        font-size: 10px; color: var(--text-tertiary);
        background: rgba(255,255,255,0.06);
        border-bottom-left-radius: 6px;
        text-transform: uppercase; letter-spacing: 0.5px;
        user-select: none;
      }

      /* Markdown — Blockquote */
      .message.assistant .md-blockquote {
        margin: 6px 0; padding: 4px 10px;
        border-left: 3px solid var(--accent);
        background: rgba(140,100,220,0.06);
        color: var(--text-secondary);
      }
      .message.assistant .md-blockquote p {
        margin: 2px 0;
      }

      /* Markdown — Lists */
      .message.assistant .md-list {
        padding-left: 18px; margin: 4px 0;
      }
      .message.assistant .md-list li {
        margin: 2px 0; line-height: 1.45;
      }

      /* Markdown — Tables */
      .message.assistant .md-table {
        width: 100%; border-collapse: collapse;
        margin: 6px 0; font-size: 12px;
      }
      .message.assistant .md-table th,
      .message.assistant .md-table td {
        padding: 4px 8px; text-align: left;
        border: 1px solid rgba(255,255,255,0.1);
      }
      .message.assistant .md-table th {
        background: rgba(255,255,255,0.06);
        font-weight: 600; color: var(--text);
      }
      .message.assistant .md-table tr:nth-child(even) td {
        background: rgba(255,255,255,0.02);
      }

      /* Markdown — Horizontal rule */
      .message.assistant .md-hr {
        border: none; height: 1px;
        background: rgba(255,255,255,0.1);
        margin: 8px 0;
      }

      /* Markdown — Links */
      .message.assistant a {
        color: var(--accent); text-decoration: none;
      }
      .message.assistant a:hover { text-decoration: underline; }

      /* Markdown — Bold / Italic / Strikethrough */
      .message.assistant strong { font-weight: 600; }
      .message.assistant em { font-style: italic; }
      .message.assistant del {
        text-decoration: line-through;
        color: var(--text-secondary);
      }

      /* Streaming cursor */
      .message.streaming::after {
        content: '\\25AE'; display: inline;
        animation: cursorBlink 0.8s step-end infinite;
        color: var(--text-tertiary); margin-left: 1px;
      }
      @keyframes cursorBlink {
        0%, 100% { opacity: 1; } 50% { opacity: 0; }
      }

      /* Memory badge */
      .memory-badge {
        position: absolute; bottom: 4px; right: 8px;
        font-size: 12px; color: var(--accent); opacity: 0.6;
        cursor: default; transition: opacity 150ms;
      }
      .memory-badge:hover { opacity: 1; }

      /* Thinking indicator */
      .typing-indicator {
        display: inline-block; line-height: 0px;
      }
      .typing-indicator span {
        display: inline-block; width: 5px; height: 5px;
        background: var(--text-tertiary); border-radius: 50%;
        margin: 0 1px; animation: typingBounce 1.2s infinite;
      }
      .typing-indicator span:nth-child(2) { animation-delay: 0.2s; }
      .typing-indicator span:nth-child(3) { animation-delay: 0.4s; }
      @keyframes typingBounce {
        0%, 60%, 100% { transform: translateY(0); }
        30% { transform: translateY(-4px); }
      }

      /* Tool call bubble */
      .message.tool-call {
        align-self: flex-start;
        background: rgba(255,255,255,0.04);
        border-left: 2px solid var(--accent);
        font-size: 12px; color: var(--text-secondary);
        padding: 4px 8px;
        display: flex; flex-direction: column; gap: 2px;
      }
      .tool-call-header {
        display: flex; align-items: center; gap: 5px;
      }
      .tool-call-icon { flex-shrink: 0; font-size: 11px; line-height: 1; }
      .tool-call-name { font-weight: 500; color: var(--text); }
      .tool-call-status {
        color: var(--text-tertiary); font-size: 11px;
      }
      .tool-call-spinner {
        display: inline-block; width: 10px; height: 10px;
        border: 1.5px solid var(--text-tertiary);
        border-top-color: var(--accent);
        border-radius: 50%;
        animation: toolSpin 0.6s linear infinite;
      }
      @keyframes toolSpin { to { transform: rotate(360deg); } }
      .tool-call-result {
        max-height: 0; overflow: hidden;
        transition: max-height 0.2s ease;
        font-size: 11px; color: var(--text-tertiary);
        font-family: ui-monospace, 'SF Mono', Menlo, monospace;
        white-space: pre-wrap; word-break: break-all;
      }
      .tool-call-result.expanded {
        max-height: 200px; overflow-y: auto;
        margin-top: 2px; padding-top: 2px;
        border-top: 1px solid var(--border);
      }
      .tool-call-toggle {
        background: none; border: none; color: var(--text-tertiary);
        font-size: 10px; cursor: pointer; padding: 0; font-family: inherit;
      }
      .tool-call-toggle:hover { color: var(--text-secondary); }
      .message.tool-call.success { border-left-color: #4ade80; }
      .message.tool-call.error { border-left-color: var(--error); }

      /* Error bubble */
      .message.error {
        align-self: flex-start;
        background: rgba(239,68,68,0.1); color: var(--text);
        font-size: 12px; cursor: pointer;
        display: flex; flex-direction: column; gap: 2px;
        border-left: 2px solid var(--error);
        padding: 4px 8px;
      }
      .error-summary { display: flex; align-items: center; gap: 5px; }
      .error-icon {
        color: var(--error); font-size: 12px; flex-shrink: 0;
        line-height: 1;
      }
      .error-text { color: var(--error); font-size: 12px; }
      .error-detail {
        max-height: 0; overflow: hidden;
        transition: max-height 0.2s ease;
        font-size: 11px; color: var(--text-tertiary);
        font-family: ui-monospace, 'SF Mono', Menlo, monospace;
        white-space: pre-wrap; word-break: break-all;
      }
      .error-detail.expanded {
        max-height: 300px; overflow-y: auto;
        margin-top: 2px; padding-top: 2px;
        border-top: 1px solid rgba(239,68,68,0.2);
      }

      /* Empty state */
      .empty-state {
        flex: 1; display: flex; flex-direction: column;
        align-items: center; justify-content: center;
        color: var(--text-tertiary); gap: 8px; padding: 40px 20px;
      }
      .empty-icon {
        color: var(--accent); opacity: 0.5; margin-bottom: 4px;
      }
      .empty-state .title { font-size: 14px; font-weight: 600; }
      .empty-state .hint { font-size: 12px; text-align: center; }
      .empty-cta { margin-top: 8px; }

      /* Slash menu */
      .slash-menu {
        margin: 0 14px; background: var(--bg);
        border: 1px solid var(--border); border-radius: 8px;
        overflow: hidden; flex-shrink: 0;
        animation: slashIn 100ms ease-out;
      }
      .slash-menu-item {
        display: flex; align-items: center; gap: 8px;
        padding: 8px 12px; cursor: pointer;
        transition: background 80ms;
      }
      .slash-menu-item:hover, .slash-menu-item.selected {
        background: var(--surface-hover);
      }
      .slash-menu-item-name {
        font-size: 13px; font-weight: 500; color: var(--accent);
      }
      .slash-menu-item-desc {
        font-size: 12px; color: var(--text-tertiary);
      }
      @keyframes slashIn {
        from { opacity: 0; transform: translateY(4px); }
        to { opacity: 1; transform: translateY(0); }
      }

      /* Input area */
      .input-area {
        display: flex; align-items: flex-end; gap: 6px;
        padding: 8px 14px;
        border-top: 1px solid var(--border); flex-shrink: 0;
      }
      .tool-btn {
        width: 28px; height: 28px;
        display: flex; align-items: center; justify-content: center;
        background: var(--surface); border: none; border-radius: 8px;
        color: var(--text-secondary); cursor: pointer;
        font-size: 14px; flex-shrink: 0;
      }
      .tool-btn:hover {
        background: var(--surface-hover); color: var(--text);
      }
      .tool-btn.active {
        background: var(--accent-dim); color: var(--accent);
      }
      .input-wrapper {
        flex: 1; display: flex; align-items: flex-end;
        background: var(--surface); border: 1px solid var(--border);
        border-radius: 12px; padding: 4px;
      }
      .input-wrapper:focus-within { border-color: var(--accent); }
      .input-wrapper textarea {
        flex: 1; background: transparent; border: none;
        color: var(--text); font-size: 13px; font-family: inherit;
        padding: 5px 8px; resize: none; outline: none;
        height: 24px; max-height: 120px; line-height: 1.4;
        overflow-y: hidden;
      }
      .input-wrapper textarea::placeholder { color: var(--text-tertiary); }
      .send-btn {
        width: 28px; height: 28px;
        display: flex; align-items: center; justify-content: center;
        background: var(--accent); border: none; border-radius: 8px;
        color: white; cursor: pointer; font-size: 14px; flex-shrink: 0;
      }
      .send-btn:hover { filter: brightness(1.15); }
      .send-btn.streaming { background: var(--error); }
      .send-btn.streaming .send-icon { display: none; }
      .send-btn.streaming .stop-icon { display: block !important; }

      /* Buttons */
      .btn-primary {
        padding: 6px 16px; background: var(--accent); border: none;
        border-radius: 8px; color: white;
        font-size: 12px; font-family: inherit; cursor: pointer;
      }
      .btn-primary:hover { filter: brightness(1.15); }
    `;
  }

  override connectedCallback() {
    super.connectedCallback();
    this.loadChatHistory_();

    if (cr.addWebUIListener) {
      cr.addWebUIListener('proactiveSuggestion',
          (data: {
            text: string; episodeId?: number; scenarioId?: string;
            scenarioName?: string; actionLabel?: string;
            actionPrompt?: string; requiresPageContent?: boolean;
            tabId?: number; confidence?: number;
          }) => {
        if (data.scenarioId) {
          this.currentScenarioData_ = {
            scenarioId: data.scenarioId,
            scenarioName: data.scenarioName || '',
            actionLabel: data.actionLabel || '',
            actionPrompt: data.actionPrompt || '',
            requiresPageContent: !!data.requiresPageContent,
            tabId: data.tabId || 0,
            confidence: data.confidence || 0,
          };
          this.showChip_(data.actionLabel || data.text);
        } else {
          this.currentScenarioData_ = null;
          this.currentSuggestionEpisodeId_ = data.episodeId || 0;
          this.showChip_(data.text);
        }
      });
    }

    soulChannel.addEventListener('message', () => {
      refreshSoulContent();
      this.fireToast_('Soul updated from another tab');
    });

    setTimeout(() => this.focusInput(), 100);
  }

  override render() {
    return html`
      ${this.chipVisible_ ? this.renderChip_() : nothing}
      <div class="chat-area" id="chatArea">
        ${this.uiMessages_.length === 0 && !this.isThinking_ &&
            this.streamingContent_ === null
          ? this.renderEmptyState_() : nothing}
        ${this.uiMessages_.map((m, i) => this.renderMessage_(m, i))}
        ${this.isThinking_ ? html`
          <div class="message assistant">
            <span class="typing-indicator">
              <span></span><span></span><span></span>
            </span>
          </div>` : nothing}
        ${this.streamingContent_ !== null ? html`<div
            class="message assistant streaming"
            .innerHTML=${renderMarkdown(this.streamingContent_)}></div>` : nothing}
      </div>
      ${this.slashMenuVisible_ ? this.renderSlashMenu_() : nothing}
      ${this.renderInputArea_()}
    `;
  }

  override updated() {
    const chatArea = this.shadowRoot!.getElementById('chatArea');
    if (chatArea) chatArea.scrollTop = chatArea.scrollHeight;
  }

  // ---- Public API (called by parent) ----

  focusInput() {
    const ta = this.shadowRoot!.querySelector('textarea');
    if (ta) ta.focus();
  }

  async endCurrentSession() {
    if (this.messages_.length === 0) return;
    const conversationEnabled =
        localStorage.getItem('dao_conversation_enabled') !== 'false';
    if (!conversationEnabled) return;
    const messageData = this.messages_.map(m => ({
      role: m.role,
      content: m.content || '',
      pageUrl: this.currentDomain_ ?
          ('https://' + this.currentDomain_) : '',
    }));
    try {
      await callNativeArgs('endSession', this.sessionId_, messageData);
    } catch (_) { /* best-effort */ }
    this.sessionId_ = generateSessionId();
  }

  // ---- Render Helpers ----

  private renderChip_() {
    const chipIcon = html`<svg class="chip-icon" width="14" height="14"
        viewBox="0 0 24 24" fill="none" stroke="currentColor"
        stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <path d="m21.64 3.64-1.28-1.28a1.21 1.21 0 0
        0-1.72 0L2.36 18.64a1.21 1.21 0 0 0 0 1.72l1.28 1.28a1.2 1.2 0
        0 0 1.72 0L21.64 5.36a1.2 1.2 0 0 0 0-1.72"/>
      <path d="m14 7 3 3"/><path d="M5 6v4"/><path d="M19 14v4"/>
      <path d="M10 2v2"/><path d="M7 8H3"/><path d="M21 16h-4"/>
      <path d="M11 3H9"/></svg>`;

    return html`
      <div class="chip-area ${this.chipHiding_ ? 'hiding' : ''}">
        <div class="chip" role="alert" tabindex="0"
            @click=${this.onChipClick_}
            @keydown=${this.onChipKeydown_}>
          ${chipIcon}
          <span class="chip-text">${this.chipText_}</span>
          <button class="chip-close" aria-label="Dismiss"
              @click=${this.onChipClose_}>&times;</button>
        </div>
      </div>`;
  }

  private renderEmptyState_() {
    const bulbIcon = html`<svg class="empty-icon" width="48" height="48"
        viewBox="0 0 24 24" fill="none" stroke="currentColor"
        stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">
      <path d="M12 2a7 7 0 0 1 7 7c0 2.38-1.19 4.47-3 5.74V17a2 2 0 0
        1-2 2H10a2 2 0 0 1-2-2v-2.26C6.19 13.47 5 11.38 5 9a7 7 0 0 1
        7-7z"/>
      <path d="M10 21v1a2 2 0 0 0 4 0v-1"/>
      <line x1="9" y1="17" x2="15" y2="17"/></svg>`;

    return html`
      <div class="empty-state">
        ${bulbIcon}
        <div class="title">Your memory is growing</div>
        <div class="hint">Chat with the Agent to build context.<br>
            The more you use it, the smarter it gets.</div>
        <button class="btn-primary empty-cta"
            @click=${() => this.focusInput()}>
          Start chatting &rarr;
        </button>
      </div>`;
  }

  private renderMessage_(m: UIMessage, _index: number) {
    switch (m.type) {
      case 'user':
        return html`<div class="message user">${m.content}</div>`;
      case 'assistant':
        return html`<div class="message assistant"
            .innerHTML=${renderMarkdown(m.content)}></div>
          ${m.usedMemory ? html`
            <span class="memory-badge" title="This response used your memory">
              \u2727</span>` : nothing}`;
      case 'tool-call':
        return this.renderToolCallBubble_(m);
      case 'error':
        return this.renderErrorBubble_(m);
      case 'system-msg':
        return html`<div class="message system-msg">${m.content}</div>`;
      default:
        return nothing;
    }
  }

  private renderToolCallBubble_(m: UIMessage) {
    const statusClass = m.toolStatus === 'done' ? 'success' :
        (m.toolStatus === 'failed' ? 'error' : '');
    const icon = m.toolStatus === 'running'
        ? html`<span class="tool-call-spinner"></span>`
        : html`<span class="tool-call-icon">
            ${m.toolStatus === 'done' ? '\u2713' : '\u2717'}</span>`;
    const statusText = m.toolStatus === 'running' ? 'running...' :
        (m.toolStatus === 'done' ? 'done' : 'failed');
    const truncated = (m.toolResult && m.toolResult.length > 300)
        ? m.toolResult.substring(0, 300) + '...' : m.toolResult;

    return html`
      <div class="message tool-call ${statusClass}">
        <div class="tool-call-header">
          ${icon}
          <span class="tool-call-name">${m.toolName}</span>
          <span class="tool-call-status">${statusText}</span>
          ${m.toolResult ? html`
            <button class="tool-call-toggle"
                @click=${(e: Event) => this.toggleToolDetail_(e, m)}>
              ${m.toolDetailExpanded ? 'hide detail' : 'show detail'}
            </button>` : nothing}
        </div>
        ${m.toolResult ? html`
          <div class="tool-call-result ${m.toolDetailExpanded ?
              'expanded' : ''}">${truncated}</div>` : nothing}
      </div>`;
  }

  private renderErrorBubble_(m: UIMessage) {
    const shortMsg = m.content.length > 80
        ? m.content.substring(0, 80) + '...' : m.content;
    return html`
      <div class="message error"
          @click=${() => this.toggleErrorDetail_(m)}>
        <div class="error-summary">
          <span class="error-icon">\u26A0</span>
          <span class="error-text">${shortMsg}</span>
        </div>
        <div class="error-detail ${m.errorDetailExpanded ?
            'expanded' : ''}">${m.content}</div>
      </div>`;
  }

  private renderSlashMenu_() {
    return html`
      <div class="slash-menu" role="listbox">
        ${this.slashMenuItems_.map((cmd, i) => html`
          <div class="slash-menu-item ${i === this.slashMenuIndex_ ?
              'selected' : ''}"
              role="option"
              @mouseenter=${() => this.slashMenuIndex_ = i}
              @click=${() => this.executeSlashMenuItem_(i)}>
            <span class="slash-menu-item-name">${cmd.name}</span>
            <span class="slash-menu-item-desc">${cmd.description}</span>
          </div>`)}
      </div>`;
  }

  private renderInputArea_() {
    const pageIcon = html`<svg width="16" height="16" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round">
      <path d="M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0
        0 2-2V7Z"/>
      <path d="M14 2v4a2 2 0 0 0 2 2h4"/></svg>`;
    const sendIcon = html`<svg class="send-icon" width="16" height="16"
        viewBox="0 0 24 24" fill="none" stroke="currentColor"
        stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <line x1="22" y1="2" x2="11" y2="13"/>
      <polygon points="22 2 15 22 11 13 2 9 22 2"/></svg>`;
    const stopIcon = html`<svg class="stop-icon" width="16" height="16"
        viewBox="0 0 24 24" fill="none" stroke="currentColor"
        stroke-width="2" stroke-linecap="round" stroke-linejoin="round"
        style="display:none;">
      <rect x="6" y="6" width="12" height="12" rx="2"/></svg>`;

    return html`
      <div class="input-area">
        <button class="tool-btn" title="Inject page content into context"
            @click=${this.onPageBtnClick_}>${pageIcon}</button>
        <div class="input-wrapper">
          <textarea rows="1" placeholder="Message Dao Agent..."
              @input=${this.onInput_}
              @keydown=${this.onKeydown_}
              @compositionstart=${() => this.isComposing_ = true}
              @compositionend=${() => this.isComposing_ = false}
          ></textarea>
        </div>
        <button class="send-btn ${this.isStreaming_ ? 'streaming' : ''}"
            title="${this.isStreaming_ ? 'Stop' : 'Send'}"
            @click=${this.onSendBtnClick_}>
          ${sendIcon}${stopIcon}
        </button>
      </div>`;
  }

  // ---- Event Handlers ----

  private onInput_(e: Event) {
    const ta = e.target as HTMLTextAreaElement;
    ta.style.height = '24px';
    const cs = getComputedStyle(ta);
    const paddingY = parseFloat(cs.paddingTop) + parseFloat(cs.paddingBottom);
    const newHeight = Math.min(ta.scrollHeight - paddingY, 120);
    ta.style.height = newHeight + 'px';
    ta.style.overflowY = newHeight >= 120 ? 'auto' : 'hidden';
    if (ta.value.startsWith('/') && !this.isStreaming_) {
      this.showSlashMenu_(ta.value);
    } else {
      this.hideSlashMenu_();
    }
  }

  private onKeydown_(e: KeyboardEvent) {
    if (this.isComposing_ || e.isComposing) return;
    if (this.slashMenuVisible_) {
      if (e.key === 'ArrowDown') {
        e.preventDefault();
        this.slashMenuIndex_ = Math.min(
            this.slashMenuIndex_ + 1, this.slashMenuItems_.length - 1);
        return;
      }
      if (e.key === 'ArrowUp') {
        e.preventDefault();
        this.slashMenuIndex_ = Math.max(this.slashMenuIndex_ - 1, 0);
        return;
      }
      if (e.key === 'Tab' || (e.key === 'Enter' && !e.shiftKey)) {
        e.preventDefault();
        if (this.slashMenuIndex_ >= 0) {
          this.executeSlashMenuItem_(this.slashMenuIndex_);
        }
        return;
      }
      if (e.key === 'Escape') {
        e.preventDefault();
        this.hideSlashMenu_();
        return;
      }
    }
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      this.sendMessage_();
    }
  }

  private onSendBtnClick_() {
    if (this.isStreaming_) {
      this.stopStreaming_();
    } else {
      this.sendMessage_();
    }
  }

  private async onPageBtnClick_(e: Event) {
    if (this.isStreaming_) return;
    const btn = e.currentTarget as HTMLElement;
    btn.classList.add('active');
    try {
      const content = await callNative('getPageContent') as {text?: string};
      const info = await callNative('getPageInfo') as
          {title?: string; url?: string};
      const contextMsg = 'Current page: ' + (info.title || 'Unknown') +
          '\nURL: ' + (info.url || 'Unknown') + '\n\nPage content:\n' +
          (content.text || '(empty)');
      this.messages_.push({
        role: 'user',
        content: '[Page context injected]\n\n' + contextMsg,
      });
      this.pushUIMessage_(
          'system-msg',
          '\u{1F4C4} Page content loaded (' +
              (content.text || '').length + ' chars)');
    } catch (e) {
      this.pushErrorMessage_((e as Error).message);
    }
    btn.classList.remove('active');
  }

  private onChipClick_() {
    this.acceptChip_();
  }

  private onChipClose_(e: Event) {
    e.stopPropagation();
    if (this.currentScenarioData_) {
      callNativeArgs('dismissSuggestion', {
        scenarioId: this.currentScenarioData_.scenarioId,
        actionLabel: this.currentScenarioData_.actionLabel,
      }).catch(() => {});
    } else if (this.currentSuggestionEpisodeId_) {
      callNativeArgs(
          'dismissSuggestion', this.currentSuggestionEpisodeId_
      ).catch(() => {});
    }
    this.hideChip_();
  }

  private onChipKeydown_(e: KeyboardEvent) {
    if (e.key === 'Enter') this.acceptChip_();
    if (e.key === 'Escape') this.onChipClose_(e);
  }

  private toggleToolDetail_(e: Event, m: UIMessage) {
    e.stopPropagation();
    m.toolDetailExpanded = !m.toolDetailExpanded;
    this.requestUpdate();
  }

  private toggleErrorDetail_(m: UIMessage) {
    m.errorDetailExpanded = !m.errorDetailExpanded;
    this.requestUpdate();
  }

  // ---- Chip ----

  private showChip_(text: string) {
    const proactiveEnabled =
        localStorage.getItem('dao_proactive_enabled') !== 'false';
    if (!proactiveEnabled) return;
    this.chipText_ = text;
    this.chipVisible_ = true;
    this.chipHiding_ = false;
  }

  private hideChip_() {
    this.chipHiding_ = true;
    setTimeout(() => {
      this.chipVisible_ = false;
      this.chipHiding_ = false;
      this.currentScenarioData_ = null;
    }, 150);
  }

  private async acceptChip_() {
    if (this.currentScenarioData_) {
      const scenario = this.currentScenarioData_;
      callNativeArgs('acceptSuggestion', {
        scenarioId: scenario.scenarioId,
        actionLabel: scenario.actionLabel,
      }).catch(() => {});
      this.hideChip_();
      let prompt = scenario.actionPrompt;
      if (scenario.requiresPageContent && scenario.tabId) {
        try {
          const result = await callNativeArgs(
              'getPageContentForScenario', scenario.tabId) as
              {text?: string}|null;
          if (result?.text) {
            prompt = prompt.replace('{page_content}', result.text);
          }
        } catch (_) { /* ignore */ }
      }
      this.setInputValue_(prompt);
      this.sendMessage_();
      return;
    }
    if (this.currentSuggestionEpisodeId_) {
      callNativeArgs(
          'acceptSuggestion', this.currentSuggestionEpisodeId_
      ).catch(() => {});
    }
    const text = this.chipText_;
    this.hideChip_();
    if (text) {
      this.setInputValue_(text);
      this.sendMessage_();
    }
  }

  // ---- Slash Menu ----

  private showSlashMenu_(filter: string) {
    const query = filter.toLowerCase();
    const filtered = this.slashCommands_.filter(
        c => c.name.startsWith(query) || query === '/');
    if (filtered.length === 0) {
      this.hideSlashMenu_();
      return;
    }
    this.slashMenuItems_ = filtered;
    this.slashMenuIndex_ = 0;
    this.slashMenuVisible_ = true;
  }

  private hideSlashMenu_() {
    this.slashMenuVisible_ = false;
    this.slashMenuIndex_ = -1;
  }

  private executeSlashMenuItem_(index: number) {
    const cmd = this.slashMenuItems_[index];
    if (!cmd) return;
    this.setInputValue_(cmd.name);
    this.hideSlashMenu_();
    this.sendMessage_();
  }

  // ---- Chat Logic ----

  private getInputValue_(): string {
    const ta = this.shadowRoot!.querySelector('textarea');
    return ta ? ta.value.trim() : '';
  }

  private setInputValue_(val: string) {
    const ta = this.shadowRoot!.querySelector('textarea');
    if (ta) {
      ta.value = val;
      ta.style.height = 'auto';
    }
  }

  private clearInput_() {
    const ta = this.shadowRoot!.querySelector('textarea');
    if (ta) {
      ta.value = '';
      ta.style.height = '24px';
      ta.style.overflowY = 'hidden';
    }
  }

  private async sendMessage_() {
    const text = this.getInputValue_();
    if (!text || this.isStreaming_) return;

    if (text.startsWith('/')) {
      this.clearInput_();
      this.hideSlashMenu_();
      const matched = this.slashCommands_.find(
          c => c.name === text.toLowerCase().trim());
      if (matched) {
        matched.action();
        return;
      }
      this.fireToast_('Unknown command: ' + text);
      return;
    }

    const apiKey = localStorage.getItem('dao_agent_api_key') || '';
    if (!apiKey) {
      this.pushErrorMessage_(
          'Please set your API Key in Settings first.');
      this.dispatchEvent(new CustomEvent('switch-tab', {
        bubbles: true, composed: true,
        detail: {tab: 'settings', subTab: 'connection'},
      }));
      return;
    }

    this.messages_.push({role: 'user', content: text});
    this.pushUIMessage_('user', text);
    this.saveChatHistory_();
    this.clearInput_();

    await this.runConversation_();
  }

  private stopStreaming_() {
    if (this.currentAbortController_) {
      this.currentAbortController_.abort();
      this.currentAbortController_ = null;
    }
  }

  private async runConversation_() {
    this.isStreaming_ = true;
    this.currentAbortController_ = new AbortController();

    const apiKey = localStorage.getItem('dao_agent_api_key') || '';
    const baseUrl = localStorage.getItem('dao_agent_base_url') ||
        'https://api.openai.com/v1';
    const model = localStorage.getItem('dao_agent_model') || 'gpt-5';
    const memoryEnabled = await this.isMemoryEnabled_();

    let memoryBlock = '';
    if (memoryEnabled && !this.memoryContextLoaded_) {
      try {
        const memoryTimeout = new Promise<never>((_, rej) =>
            setTimeout(() => rej(new Error('memory timeout')), 3000));
        const memoryLoad = (async () => {
          const pageInfo = await callNative('getPageInfo') as
              {url?: string; title?: string};
          if (pageInfo.url) {
            try {
              this.currentDomain_ = new URL(pageInfo.url).hostname;
            } catch (_) {
              this.currentDomain_ = '';
            }
          }
          return await callNativeArgs('getMemoryContext',
              pageInfo.url || '', this.currentDomain_,
              this.sessionId_) as {
            preferences?: Array<{key: string; value: string}>;
            episodes?: Array<{intent: string; outcome: string}>;
          };
        })();
        const ctx = await Promise.race([memoryLoad, memoryTimeout]);
        if (ctx.preferences && ctx.preferences.length > 0) {
          memoryBlock += '\n\n## User Preferences (from memory)\n';
          for (const p of ctx.preferences) {
            memoryBlock += '- ' + p.key + ': ' + p.value + '\n';
          }
        }
        if (ctx.episodes && ctx.episodes.length > 0) {
          memoryBlock += '\n## Previous context on this page\n';
          for (const e of ctx.episodes) {
            memoryBlock +=
                '- Intent: ' + e.intent + ' → Outcome: ' + e.outcome + '\n';
          }
        }
        if (memoryBlock && !this.hasFirstMemory_) {
          this.hasFirstMemory_ = true;
          if (!localStorage.getItem('dao_first_memory_shown')) {
            localStorage.setItem('dao_first_memory_shown', 'true');
            this.fireToast_('Memory activated — your Agent is learning');
          }
        }
        this.memoryContextLoaded_ = true;
      } catch (_) {
        this.memoryContextLoaded_ = true;
      }
    }

    try {
      let continueLoop = true;
      while (continueLoop) {
        refreshSoulContent();
        const systemPrompt: ChatMessage = {
          role: 'system',
          content: BASE_SYSTEM_PROMPT + '\n' + currentSoulContent +
              memoryBlock,
        };
        continueLoop = false;

        this.isThinking_ = true;
        this.streamingContent_ = null;
        this.rawStreamingContent_ = '';
        this.streamUsedMemory_ = this.memoryContextLoaded_;
        let hadError = false;
        const toolCallMessageIds: Record<string, string> = {};
        const signal = this.currentAbortController_?.signal;

        await new Promise<void>((resolve) => {
          const callbacks: StreamCallbacks = {
            onToken: (text: string) => {
              if (this.isThinking_) {
                this.isThinking_ = false;
                this.streamingContent_ = '';
                this.rawStreamingContent_ = '';
              }
              this.rawStreamingContent_ += text;
              if (!this.streamRenderTimer_) {
                this.streamRenderTimer_ = window.setTimeout(() => {
                  this.streamRenderTimer_ = 0;
                  this.streamingContent_ = this.rawStreamingContent_;
                }, 50);
              }
            },

            onToolCall: (tc: ToolCall) => {
              if (this.isThinking_) this.isThinking_ = false;
              const msgId = uid();
              toolCallMessageIds[tc.id] = msgId;
              this.uiMessages_ = [...this.uiMessages_, {
                id: msgId, type: 'tool-call',
                content: '', toolName: tc.function.name,
                toolStatus: 'running',
              }];
            },

            onDone: (fullContent: string, toolCalls: ToolCall[]) => {
              if (this.isThinking_) this.isThinking_ = false;

              if (toolCalls.length > 0) {
                this.finishStreaming_(false);
                this.messages_.push({
                  role: 'assistant',
                  content: fullContent || null,
                  tool_calls: toolCalls,
                });

                (async () => {
                  try {
                    for (const tc of toolCalls) {
                      const fn = tc.function;
                      let args: Record<string, string> = {};
                      try {
                        args = fn.arguments ?
                            JSON.parse(fn.arguments) : {};
                      } catch (_) { /* ignore */ }

                      let result: unknown;
                      let isToolError = false;
                      try {
                        result = await executeTool(fn.name, args);
                      } catch (e) {
                        result = {error: (e as Error).message};
                        isToolError = true;
                      }
                      const resultStr = typeof result === 'string'
                          ? result : JSON.stringify(result);

                      const msgId = toolCallMessageIds[tc.id];
                      if (msgId) {
                        const msg = this.uiMessages_.find(
                            m => m.id === msgId);
                        if (msg) {
                          msg.toolStatus =
                              isToolError ? 'failed' : 'done';
                          msg.toolResult = resultStr;
                          this.requestUpdate();
                        }
                      }
                      this.messages_.push({
                        role: 'tool', tool_call_id: tc.id,
                        content: resultStr,
                      });
                    }
                    continueLoop = true;
                  } catch (e) {
                    hadError = true;
                    this.pushErrorMessage_(
                        (e as Error).message || 'Tool execution failed');
                  }
                  resolve();
                })();
              } else {
                this.finishStreaming_(this.streamUsedMemory_);
                if (fullContent) {
                  this.messages_.push({
                    role: 'assistant', content: fullContent,
                  });
                  const lastUI =
                      this.uiMessages_[this.uiMessages_.length - 1];
                  if (!lastUI || lastUI.type !== 'assistant' ||
                      lastUI.content !== fullContent) {
                    this.pushUIMessage_(
                        'assistant', fullContent, this.streamUsedMemory_);
                  }
                }
                this.saveChatHistory_();
                resolve();
              }
            },

            onError: (shortMsg: string, _fullError: string) => {
              if (this.isThinking_) this.isThinking_ = false;
              hadError = true;
              this.pushErrorMessage_(shortMsg);
              resolve();
            },
          };

          callLLMStreaming(
              [systemPrompt, ...this.messages_],
              callbacks, apiKey, baseUrl, model, signal,
          ).catch((e: Error) => {
            if (this.isThinking_) this.isThinking_ = false;
            if (e.name === 'AbortError') {
              this.finishStreaming_(false);
              hadError = true;
              resolve();
              return;
            }
            hadError = true;
            this.pushErrorMessage_(
                e.message || 'Failed to connect to API');
            resolve();
          });
        });

        if (hadError) break;
      }
    } catch (e) {
      if ((e as Error).name !== 'AbortError') {
        this.pushErrorMessage_((e as Error).message || 'Unknown error');
      }
    } finally {
      this.currentAbortController_ = null;
      this.isStreaming_ = false;
    }
  }

  private finishStreaming_(usedMemory: boolean) {
    if (this.streamRenderTimer_) {
      clearTimeout(this.streamRenderTimer_);
      this.streamRenderTimer_ = 0;
    }
    if (this.streamingContent_ !== null ||
        this.rawStreamingContent_.length > 0) {
      const content = this.rawStreamingContent_;
      if (content) {
        this.pushUIMessage_('assistant', content, usedMemory);
      }
    }
    this.streamingContent_ = null;
    this.rawStreamingContent_ = '';
  }

  // ---- Message Helpers ----

  private pushUIMessage_(
      type: UIMessage['type'], content: string, usedMemory = false) {
    this.uiMessages_ = [...this.uiMessages_, {
      id: uid(), type, content, usedMemory,
    }];
  }

  private pushErrorMessage_(msg: string) {
    this.uiMessages_ = [...this.uiMessages_, {
      id: uid(), type: 'error', content: msg,
    }];
  }

  // ---- Persistence ----

  private saveChatHistory_() {
    const toSave = this.messages_.filter(
        m => m.role === 'user' || (m.role === 'assistant' && m.content));
    try {
      localStorage.setItem(CHAT_HISTORY_KEY, JSON.stringify(toSave));
    } catch (_) { /* storage full */ }
  }

  private loadChatHistory_() {
    try {
      const raw = localStorage.getItem(CHAT_HISTORY_KEY);
      if (!raw) return;
      const saved = JSON.parse(raw) as ChatMessage[];
      if (!Array.isArray(saved) || saved.length === 0) return;
      for (const msg of saved) {
        this.messages_.push(msg);
        this.pushUIMessage_(
            msg.role === 'user' ? 'user' : 'assistant',
            msg.content || '');
      }
    } catch (_) { /* corrupt data */ }
  }

  private clearChatHistory_() {
    this.messages_.length = 0;
    this.uiMessages_ = [];
    localStorage.removeItem(CHAT_HISTORY_KEY);
    this.memoryContextLoaded_ = false;
    this.sessionId_ = generateSessionId();
  }

  // ---- Utility ----

  private async isMemoryEnabled_(): Promise<boolean> {
    try {
      return !!(await callNativeArgs('getMemoryEnabled'));
    } catch (_) {
      return false;
    }
  }

  private fireToast_(text: string) {
    this.dispatchEvent(new CustomEvent('show-toast', {
      bubbles: true, composed: true, detail: {text},
    }));
  }
}

customElements.define('dao-chat-view', DaoChatView);
