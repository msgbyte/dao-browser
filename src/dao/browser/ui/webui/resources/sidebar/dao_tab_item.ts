// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative, TAB_DRAG_PREFIX} from './sidebar_bridge.js';
import type {TabData} from './sidebar_bridge.js';

export class DaoTabItem extends CrLitElement {
  static get is() {
    return 'dao-tab-item';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
      }

      .tab-row {
        display: flex;
        align-items: center;
        height: 36px;
        padding: 0 10px;
        border-radius: var(--radius-tab, 12px);
        cursor: default;
        gap: 8px;
        position: relative;
        transition: background 0.15s ease;
      }

      .tab-row:hover {
        background: var(--ink-drop);
      }

      :host([active]) .tab-row {
        background: var(--surface-active);
        box-shadow: 0 1px 3px rgba(0, 0, 0, 0.08),
                    0 1px 2px rgba(0, 0, 0, 0.06);
      }

      .favicon {
        width: 16px;
        height: 16px;
        flex-shrink: 0;
        border-radius: 2px;
        position: relative;
      }

      .favicon img {
        width: 16px;
        height: 16px;
        border-radius: 2px;
      }

      .favicon.light-icon {
        background: rgba(0, 0, 0, 0.12);
        border-radius: 4px;
        padding: 2px;
        width: 20px;
        height: 20px;
        margin: -2px;
      }

      .favicon .placeholder {
        width: 16px;
        height: 16px;
        border-radius: 2px;
        background: var(--surface);
      }

      .agent-lock-dot {
        position: absolute;
        bottom: -2px;
        right: -2px;
        width: 6px;
        height: 6px;
        border-radius: 50%;
        background: var(--accent);
        animation: agent-lock-pulse 1.2s ease-in-out infinite;
      }

      @keyframes agent-lock-pulse {
        0%, 100% { opacity: 0.5; }
        50% { opacity: 1.0; }
      }

      .title {
        flex: 1;
        min-width: 0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        font-size: 13px;
        color: var(--text-primary);
      }

      .audio-btn {
        width: 16px;
        height: 16px;
        border: none;
        background: transparent;
        color: var(--text-secondary);
        cursor: default;
        padding: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        flex-shrink: 0;
      }

      .close-btn {
        width: 16px;
        height: 16px;
        border: none;
        background: transparent;
        color: var(--text-muted);
        cursor: default;
        padding: 0;
        opacity: 0;
        flex-shrink: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        transition: opacity 0.1s ease;
      }

      .tab-row:hover .close-btn {
        opacity: 1;
      }

      .close-btn:hover {
        color: var(--text-primary);
      }
    `;
  }

  static override get properties() {
    return {
      tabData: {type: Object},
      active: {type: Boolean, reflect: true},
      sessionId: {type: Number},
    };
  }

  tabData: TabData = {
    tabId: '',
    index: -1,
    title: '',
    url: '',
    faviconUrl: '',
    isActive: false,
    isPinned: false,
    isAudible: false,
    isMuted: false,
  };
  active: boolean = false;
  sessionId: number = 0;

  override render() {
    const tab = this.tabData;
    const showAudio = tab.isAudible || tab.isMuted;

    return html`
      <div class="tab-row"
           draggable="true"
           @click=${this.onActivate_}
           @dragstart=${this.onDragStart_}
           @contextmenu=${this.onContextMenu_}>
        <div class="favicon">
          ${tab.faviconUrl
              ? html`<img src=${tab.faviconUrl} alt=""
                          class=${tab.isFaviconLight ? 'light-icon' : ''}>`
              : html`<div class="placeholder"></div>`}
          ${tab.isAgentLocked
              ? html`<div class="agent-lock-dot"></div>`
              : ''}
        </div>
        ${showAudio && tab.isMuted ? html`
          <button class="audio-btn"
                  title="Unmute"
                  @click=${this.onToggleMute_}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round">
              <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"></polygon>
              <line x1="23" y1="9" x2="17" y2="15"></line>
              <line x1="17" y1="9" x2="23" y2="15"></line>
            </svg>
          </button>
        ` : ''}
        ${showAudio && !tab.isMuted ? html`
          <button class="audio-btn"
                  title="Mute"
                  @click=${this.onToggleMute_}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round">
              <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"></polygon>
              <path d="M19.07 4.93a10 10 0 0 1 0 14.14"></path>
              <path d="M15.54 8.46a5 5 0 0 1 0 7.07"></path>
            </svg>
          </button>
        ` : ''}
        <span class="title">${tab.title || tab.url || 'New Tab'}</span>
        <button class="close-btn"
                title="Close tab"
                @click=${this.onClose_}>
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
               stroke="currentColor" stroke-width="2"
               stroke-linecap="round" stroke-linejoin="round">
            <line x1="18" y1="6" x2="6" y2="18"></line>
            <line x1="6" y1="6" x2="18" y2="18"></line>
          </svg>
        </button>
      </div>
    `;
  }

  private onActivate_() {
    sendNative('activateTab', this.tabData.index);
  }

  private onClose_(e: Event) {
    e.stopPropagation();
    sendNative('closeTab', this.tabData.index);
  }

  private onToggleMute_(e: Event) {
    e.stopPropagation();
    sendNative('toggleMute', this.tabData.index);
  }

  private onDragStart_(e: DragEvent) {
    if (!e.dataTransfer) return;
    e.dataTransfer.setData('text/plain',
        `${TAB_DRAG_PREFIX}${this.sessionId}:${this.tabData.index}`);
    e.dataTransfer.effectAllowed = 'move';
  }

  private onContextMenu_(e: MouseEvent) {
    e.preventDefault();
    e.stopPropagation();
    this.dispatchEvent(new CustomEvent('tab-context-menu', {
      bubbles: true,
      composed: true,
      detail: {
        index: this.tabData.index,
        screenX: e.screenX,
        screenY: e.screenY,
      },
    }));
  }

}

customElements.define('dao-tab-item', DaoTabItem);
