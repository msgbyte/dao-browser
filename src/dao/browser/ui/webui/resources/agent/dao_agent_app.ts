// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, css, html} from
    '//resources/lit/v3_0/lit.rollup.js';

import './dao_chat_view.js';
import './dao_settings_view.js';
import {cr} from './agent_bridge.js';
import type {DaoChatView} from './dao_chat_view.js';
import type {DaoSettingsView} from './dao_settings_view.js';

export class DaoAgentApp extends CrLitElement {
  static override get properties() {
    return {
      activeTab_: {type: String, state: true},
      toastText_: {type: String, state: true},
      toastVisible_: {type: Boolean, state: true},
    };
  }

  private activeTab_ = 'chat';
  private toastText_ = '';
  private toastVisible_ = false;
  private toastTimer_ = 0;

  static override get styles() {
    return css`
      :host {
        display: flex;
        flex-direction: column;
        height: 100%;
        overflow: hidden;
      }
      .header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 8px 12px;
        flex-shrink: 0;
        border-bottom: 1px solid rgba(255,255,255,0.12);
      }
      .header-title {
        font-size: 13px;
        font-weight: 600;
        color: var(--text-secondary);
        letter-spacing: 0.2px;
      }
      .tab-bar { display: flex; gap: 3px; }
      .tab-bar .tab {
        width: 28px; height: 28px;
        display: flex; align-items: center; justify-content: center;
        background: none; border: none; border-radius: 8px;
        color: var(--text-tertiary); cursor: pointer;
        transition: color 0.15s, background 0.15s;
      }
      .tab-bar .tab:hover {
        color: var(--text-secondary);
        background: rgba(255,255,255,0.18);
      }
      .tab-bar .tab.active {
        color: var(--accent);
        background: var(--accent-dim);
        box-shadow: 0 1px 4px rgba(140, 100, 220, 0.12);
      }
      .toast {
        position: fixed; top: 16px; left: 50%;
        transform: translateX(-50%);
        background: var(--accent); color: white;
        padding: 8px 18px; border-radius: 10px;
        font-size: 12px; z-index: 200;
        box-shadow: 0 4px 16px rgba(140, 100, 220, 0.3);
        animation: toastIn 200ms ease-out;
      }
      @keyframes toastIn {
        from { opacity: 0; transform: translateX(-50%) translateY(-8px); }
        to { opacity: 1; transform: translateX(-50%) translateY(0); }
      }
    `;
  }

  override connectedCallback() {
    super.connectedCallback();
    if (cr.addWebUIListener) {
      cr.addWebUIListener('sidebarStateChanged', (expanded: boolean) => {
        if (expanded) {
          setTimeout(() => this.getChatView_()?.focusInput(), 100);
        } else {
          this.getChatView_()?.endCurrentSession();
        }
      });
    }
    this.addEventListener('show-toast', ((e: CustomEvent) => {
      this.showToast_(e.detail.text);
    }) as EventListener);
    this.addEventListener('switch-tab', ((e: CustomEvent) => {
      this.activeTab_ = e.detail.tab;
      if (e.detail.subTab) {
        this.updateComplete.then(() => {
          this.getSettingsView_()?.switchSubTab(e.detail.subTab);
        });
      }
    }) as EventListener);
  }

  override render() {
    // SVG icon paths
    const chatIcon = html`<svg width="16" height="16" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round">
      <path d="M7.9 20A9 9 0 1 0 4 16.1L2 22Z"/></svg>`;
    const settingsIcon = html`<svg width="16" height="16" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round">
      <path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2
        2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0
        .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2
        0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2
        0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0
        2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0
        0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2
        2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0
        0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0
        0 0-2-2z"/>
      <circle cx="12" cy="12" r="3"/></svg>`;

    return html`
      <div class="header">
        <span class="header-title">Dao Agent</span>
        <div class="tab-bar">
          <button class="tab ${this.activeTab_ === 'chat' ? 'active' : ''}"
              @click=${() => this.onTabClick_('chat')} title="Chat">
            ${chatIcon}
          </button>
          <button class="tab ${this.activeTab_ === 'settings' ? 'active' : ''}"
              @click=${() => this.onTabClick_('settings')} title="Settings">
            ${settingsIcon}
          </button>
        </div>
      </div>
      <dao-chat-view ?hidden=${this.activeTab_ !== 'chat'}></dao-chat-view>
      <dao-settings-view
          ?hidden=${this.activeTab_ !== 'settings'}></dao-settings-view>
      ${this.toastVisible_ ?
          html`<div class="toast">${this.toastText_}</div>` : ''}
    `;
  }

  private onTabClick_(tab: string) {
    if (this.activeTab_ === tab) {
      // Already on this tab — close the sidebar.
      chrome.send('closeSidebar');
    } else {
      this.activeTab_ = tab;
    }
  }

  private getChatView_(): DaoChatView|null {
    return this.shadowRoot!.querySelector('dao-chat-view');
  }

  private getSettingsView_(): DaoSettingsView|null {
    return this.shadowRoot!.querySelector('dao-settings-view');
  }

  private showToast_(text: string, duration = 3000) {
    this.toastText_ = text;
    this.toastVisible_ = true;
    clearTimeout(this.toastTimer_);
    this.toastTimer_ = window.setTimeout(() => {
      this.toastVisible_ = false;
    }, duration);
  }
}

customElements.define('dao-agent-app', DaoAgentApp);
