// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Overlay panel listing persisted chat sessions. Uses pi-web-ui's
// SessionsStore (IndexedDB-backed, bootstrapped by pi_app_storage.ts).
//
// Emits CustomEvents up to DaoChatView:
//   - history-select  {detail: {id}}
//   - history-close
//   - history-deleted {detail: {id}}  — when the currently active session
//                                       was removed, so the host can reset.

import {CrLitElement, html} from '//resources/lit/v3_0/lit.rollup.js';

import {ensurePiAppStorage} from './pi_app_storage.js';

interface SessionMetadata {
  id: string;
  title: string;
  createdAt: string;
  lastModified: string;
  messageCount: number;
  preview?: string;
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type SessionsApi = any;

function relativeTime(iso: string): string {
  if (!iso) return '';
  const then = Date.parse(iso);
  if (Number.isNaN(then)) return '';
  const delta = Math.max(0, Date.now() - then);
  const min = 60 * 1000;
  const hour = 60 * min;
  const day = 24 * hour;
  if (delta < min) return 'just now';
  if (delta < hour) return `${Math.floor(delta / min)}m ago`;
  if (delta < day) return `${Math.floor(delta / hour)}h ago`;
  if (delta < 7 * day) return `${Math.floor(delta / day)}d ago`;
  return new Date(then).toLocaleDateString();
}

export class DaoChatHistoryPanel extends CrLitElement {
  static override get properties() {
    return {
      open: {type: Boolean, reflect: true},
      currentSessionId: {type: String},
      sessions_: {type: Array, state: true},
      loading_: {type: Boolean, state: true},
      query_: {type: String, state: true},
      renameId_: {type: String, state: true},
      renameDraft_: {type: String, state: true},
    };
  }

  declare open: boolean;
  declare currentSessionId: string;
  declare private sessions_: SessionMetadata[];
  declare private loading_: boolean;
  declare private query_: string;
  declare private renameId_: string;
  declare private renameDraft_: string;

  constructor() {
    super();
    this.currentSessionId = '';
    this.sessions_ = [];
    this.loading_ = false;
    this.query_ = '';
    this.renameId_ = '';
    this.renameDraft_ = '';
    this.open = false;
  }

  override createRenderRoot(): HTMLElement|DocumentFragment {
    return this;
  }

  override updated(changed: Map<PropertyKey, unknown>) {
    if (changed.has('open') && this.open) {
      void this.refresh_();
    }
    if (changed.has('open') && !this.open) {
      this.renameId_ = '';
      this.query_ = '';
    }
  }

  async refresh_() {
    this.loading_ = true;
    try {
      const storage = await ensurePiAppStorage() as SessionsApi;
      const items: SessionMetadata[] =
          await storage.sessions.getAllMetadata();
      items.sort((a, b) => b.lastModified.localeCompare(a.lastModified));
      this.sessions_ = items;
    } catch (_) {
      this.sessions_ = [];
    } finally {
      this.loading_ = false;
    }
  }

  private close_() {
    this.dispatchEvent(new CustomEvent(
        'history-close', {bubbles: true, composed: true}));
  }

  private onScrimClick_(e: MouseEvent) {
    if (e.target === e.currentTarget) {
      this.close_();
    }
  }

  private onSelect_(id: string) {
    if (this.renameId_) return;
    this.dispatchEvent(new CustomEvent('history-select', {
      detail: {id},
      bubbles: true,
      composed: true,
    }));
  }

  private async onDelete_(e: Event, id: string) {
    e.stopPropagation();
    if (!confirm('Delete this chat? This cannot be undone.')) return;
    try {
      const storage = await ensurePiAppStorage() as SessionsApi;
      await storage.sessions.deleteSession(id);
    } catch (_) { /* ignore */ }
    if (id === this.currentSessionId) {
      this.dispatchEvent(new CustomEvent('history-deleted', {
        detail: {id},
        bubbles: true,
        composed: true,
      }));
    }
    await this.refresh_();
  }

  private onBeginRename_(e: Event, item: SessionMetadata) {
    e.stopPropagation();
    this.renameId_ = item.id;
    this.renameDraft_ = item.title || '';
    this.updateComplete.then(() => {
      const input =
          this.querySelector('.dao-history-rename-input') as HTMLInputElement |
          null;
      input?.focus();
      input?.select();
    });
  }

  private async commitRename_() {
    const id = this.renameId_;
    const title = this.renameDraft_.trim();
    this.renameId_ = '';
    if (!id || !title) return;
    try {
      const storage = await ensurePiAppStorage() as SessionsApi;
      await storage.sessions.updateTitle(id, title);
    } catch (_) { /* ignore */ }
    await this.refresh_();
  }

  private onRenameKey_(e: KeyboardEvent) {
    if (e.key === 'Enter') {
      e.preventDefault();
      void this.commitRename_();
    } else if (e.key === 'Escape') {
      e.preventDefault();
      this.renameId_ = '';
    }
  }

  private onRenameInput_(e: Event) {
    const t = e.target as HTMLInputElement;
    this.renameDraft_ = t.value;
  }

  private onQueryInput_(e: Event) {
    const t = e.target as HTMLInputElement;
    this.query_ = t.value;
  }

  private filtered_(): SessionMetadata[] {
    const q = this.query_.trim().toLowerCase();
    if (!q) return this.sessions_;
    return this.sessions_.filter(
        s => (s.title || '').toLowerCase().includes(q) ||
            (s.preview || '').toLowerCase().includes(q));
  }

  override render() {
    if (!this.open) return html``;
    const items = this.filtered_();
    return html`
      <style>
        dao-chat-history-panel {
          /* Readability-first palette: every surface is fully opaque and
           * every text token has >= WCAG AA contrast against its surface.
           * Do NOT pull from agent.css's --surface / --text-* here —
           * those are translucent (~18% white) and were turning the panel
           * nearly invisible against the chat scrollback. */
          --dao-hist-panel-bg: rgb(252, 253, 255);
          --dao-hist-header-bg: rgb(244, 247, 251);
          --dao-hist-border: rgb(218, 224, 232);
          --dao-hist-border-soft: rgb(230, 234, 240);
          --dao-hist-text: rgb(24, 28, 36);
          --dao-hist-text-secondary: rgb(88, 96, 110);
          --dao-hist-text-tertiary: rgb(124, 132, 146);
          --dao-hist-input-bg: rgb(247, 249, 252);
          --dao-hist-input-bg-focus: rgb(240, 244, 249);
          --dao-hist-hover-bg: rgb(232, 238, 246);
          --dao-hist-active-bg: rgb(220, 232, 248);
          --dao-hist-active-text: rgb(28, 72, 138);
          --dao-hist-danger: rgb(196, 64, 64);
          position: absolute;
          inset: 0;
          z-index: 120;
          display: flex;
          align-items: stretch;
          justify-content: stretch;
          min-width: 0;
          min-height: 0;
          overflow: hidden;
        }
        @media (prefers-color-scheme: dark) {
          dao-chat-history-panel {
            --dao-hist-panel-bg: rgb(40, 44, 50);
            --dao-hist-header-bg: rgb(48, 53, 60);
            --dao-hist-border: rgb(72, 78, 86);
            --dao-hist-border-soft: rgb(58, 64, 72);
            --dao-hist-text: rgb(238, 240, 244);
            --dao-hist-text-secondary: rgb(180, 186, 196);
            --dao-hist-text-tertiary: rgb(140, 146, 158);
            --dao-hist-input-bg: rgb(34, 38, 44);
            --dao-hist-input-bg-focus: rgb(28, 32, 38);
            --dao-hist-hover-bg: rgb(58, 64, 72);
            --dao-hist-active-bg: rgb(46, 70, 104);
            --dao-hist-active-text: rgb(170, 200, 240);
            --dao-hist-danger: rgb(240, 120, 120);
          }
        }
        .dao-history-scrim {
          position: absolute;
          inset: 0;
          display: flex;
          align-items: stretch;
          justify-content: stretch;
          min-width: 0;
          min-height: 0;
          background: rgba(0, 0, 0, 0.45);
          backdrop-filter: blur(3px);
          animation: daoHistoryFadeIn 120ms ease-out;
          overflow: hidden;
        }
        @keyframes daoHistoryFadeIn {
          from { opacity: 0; }
          to { opacity: 1; }
        }
        .dao-history-panel {
          position: relative;
          margin: 12px;
          flex: 1 1 auto;
          display: flex;
          flex-direction: column;
          min-width: 0;
          min-height: 0;
          max-height: calc(100% - 24px);
          background: var(--dao-hist-panel-bg);
          color: var(--dao-hist-text);
          border: 1px solid var(--dao-hist-border);
          border-radius: 14px;
          box-shadow: 0 18px 50px rgba(0, 0, 0, 0.28),
                      0 2px 6px rgba(0, 0, 0, 0.12);
          overflow: hidden;
          animation: daoHistorySlideIn 160ms ease-out;
        }
        @keyframes daoHistorySlideIn {
          from { opacity: 0; transform: translateY(6px); }
          to { opacity: 1; transform: translateY(0); }
        }
        .dao-history-header {
          display: flex;
          align-items: center;
          gap: 8px;
          padding: 12px 14px;
          background: var(--dao-hist-header-bg);
          border-bottom: 1px solid var(--dao-hist-border);
          flex-shrink: 0;
        }
        .dao-history-title {
          font-size: 14px;
          font-weight: 600;
          color: var(--dao-hist-text);
          margin-right: auto;
          letter-spacing: 0.1px;
        }
        .dao-history-close {
          display: inline-flex;
          align-items: center;
          justify-content: center;
          width: 28px;
          height: 28px;
          border: none;
          background: transparent;
          color: var(--dao-hist-text-secondary);
          border-radius: 8px;
          cursor: pointer;
          transition: background 0.12s, color 0.12s;
        }
        .dao-history-close:hover {
          background: var(--dao-hist-hover-bg);
          color: var(--dao-hist-text);
        }
        .dao-history-search {
          padding: 10px 14px;
          flex-shrink: 0;
          border-bottom: 1px solid var(--dao-hist-border-soft);
        }
        .dao-history-search input {
          width: 100%;
          box-sizing: border-box;
          padding: 8px 12px;
          font: inherit;
          font-size: 13px;
          color: var(--dao-hist-text);
          background: var(--dao-hist-input-bg);
          border: 1px solid var(--dao-hist-border);
          border-radius: 8px;
          outline: none;
          transition: border-color 0.12s, box-shadow 0.12s;
        }
        .dao-history-search input::placeholder {
          color: var(--dao-hist-text-tertiary);
        }
        .dao-history-search input:focus {
          /* Pin the bg explicitly so UA styles can't flip it to white on
           * focus in dark mode; nudge slightly deeper so "active" is still
           * perceptible without a jarring tone flip. */
          background: var(--dao-hist-input-bg-focus);
          border-color: rgb(70, 120, 190);
          box-shadow: 0 0 0 3px rgba(70, 120, 190, 0.22);
        }
        .dao-history-list {
          flex: 1 1 auto;
          min-height: 0;
          overflow-y: auto;
          padding: 8px;
        }
        .dao-history-empty {
          text-align: center;
          color: var(--dao-hist-text-tertiary);
          font-size: 13px;
          padding: 48px 20px;
        }
        .dao-history-item {
          display: flex;
          align-items: center;
          gap: 10px;
          padding: 10px 12px;
          border-radius: 10px;
          cursor: pointer;
          transition: background 0.12s;
        }
        .dao-history-item + .dao-history-item {
          margin-top: 2px;
        }
        .dao-history-item:hover {
          background: var(--dao-hist-hover-bg);
        }
        .dao-history-item.active {
          background: var(--dao-hist-active-bg);
        }
        .dao-history-item.active .dao-history-item-title {
          color: var(--dao-hist-active-text);
        }
        .dao-history-item-body {
          flex: 1 1 auto;
          min-width: 0;
          display: flex;
          flex-direction: column;
          gap: 3px;
        }
        .dao-history-item-title {
          font-size: 13px;
          font-weight: 600;
          color: var(--dao-hist-text);
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
          line-height: 1.3;
        }
        .dao-history-item-meta {
          font-size: 11px;
          color: var(--dao-hist-text-secondary);
          display: flex;
          gap: 6px;
          align-items: center;
        }
        .dao-history-rename-input {
          flex: 1 1 auto;
          min-width: 0;
          padding: 4px 8px;
          font: inherit;
          font-size: 13px;
          font-weight: 600;
          color: var(--dao-hist-text);
          background: var(--dao-hist-input-bg-focus);
          border: 1px solid rgb(70, 120, 190);
          border-radius: 6px;
          outline: none;
          box-shadow: 0 0 0 3px rgba(70, 120, 190, 0.22);
        }
        .dao-history-actions {
          display: flex;
          gap: 2px;
          opacity: 0;
          transition: opacity 0.12s;
          flex-shrink: 0;
        }
        .dao-history-item:hover .dao-history-actions,
        .dao-history-item.active .dao-history-actions {
          opacity: 1;
        }
        .dao-history-actions button {
          width: 28px;
          height: 28px;
          display: inline-flex;
          align-items: center;
          justify-content: center;
          border: none;
          background: transparent;
          color: var(--dao-hist-text-secondary);
          border-radius: 6px;
          cursor: pointer;
          transition: background 0.12s, color 0.12s;
        }
        .dao-history-actions button:hover {
          background: var(--dao-hist-hover-bg);
          color: var(--dao-hist-text);
        }
        .dao-history-actions button.danger:hover {
          background: rgba(196, 64, 64, 0.14);
          color: var(--dao-hist-danger);
        }
      </style>
      <div class="dao-history-scrim" @click=${this.onScrimClick_}>
        <div class="dao-history-panel" @click=${(e: Event) => e.stopPropagation()}>
          <div class="dao-history-header">
            <span class="dao-history-title">Chat history</span>
            <button class="dao-history-close"
                title="Close" @click=${this.close_}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                  stroke="currentColor" stroke-width="2" stroke-linecap="round"
                  stroke-linejoin="round">
                <path d="M18 6 6 18"/><path d="m6 6 12 12"/>
              </svg>
            </button>
          </div>
          <div class="dao-history-search">
            <input type="text" placeholder="Search conversations…"
                .value=${this.query_}
                @input=${this.onQueryInput_}>
          </div>
          <div class="dao-history-list">
            ${this.loading_ ? html`
              <div class="dao-history-empty">Loading…</div>` :
                items.length === 0 ? html`
              <div class="dao-history-empty">
                ${this.query_
                    ? 'No matches.'
                    : 'No saved conversations yet.'}
              </div>` :
                items.map(item => this.renderItem_(item))}
          </div>
        </div>
      </div>
    `;
  }

  private renderItem_(item: SessionMetadata) {
    const active = item.id === this.currentSessionId;
    const renaming = this.renameId_ === item.id;
    const title = item.title || '(untitled)';
    return html`
      <div class="dao-history-item ${active ? 'active' : ''}"
          @click=${() => this.onSelect_(item.id)}>
        <div class="dao-history-item-body">
          ${renaming ? html`
            <input class="dao-history-rename-input"
                .value=${this.renameDraft_}
                @click=${(e: Event) => e.stopPropagation()}
                @input=${this.onRenameInput_}
                @keydown=${this.onRenameKey_}
                @blur=${() => this.commitRename_()}>` : html`
            <span class="dao-history-item-title" title=${title}>
              ${title}
            </span>`}
          <span class="dao-history-item-meta">
            <span>${relativeTime(item.lastModified)}</span>
            <span>·</span>
            <span>${item.messageCount} msg</span>
          </span>
        </div>
        <div class="dao-history-actions">
          <button title="Rename"
              @click=${(e: Event) => this.onBeginRename_(e, item)}>
            <svg width="13" height="13" viewBox="0 0 24 24" fill="none"
                stroke="currentColor" stroke-width="2" stroke-linecap="round"
                stroke-linejoin="round">
              <path d="M12 20h9"/>
              <path d="M16.376 3.622a1 1 0 0 1 3.002 3.002L7.368 18.635
                a2 2 0 0 1-.855.506l-2.872.838a.5.5 0 0 1-.62-.62l.838-2.872
                a2 2 0 0 1 .506-.854z"/>
            </svg>
          </button>
          <button class="danger" title="Delete"
              @click=${(e: Event) => this.onDelete_(e, item.id)}>
            <svg width="13" height="13" viewBox="0 0 24 24" fill="none"
                stroke="currentColor" stroke-width="2" stroke-linecap="round"
                stroke-linejoin="round">
              <path d="M3 6h18"/>
              <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/>
              <path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>
            </svg>
          </button>
        </div>
      </div>`;
  }
}

customElements.define('dao-chat-history-panel', DaoChatHistoryPanel);
