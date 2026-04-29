// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PR3: this element renders in the light DOM so the global pi-web-ui
// Tailwind stylesheet loaded from agent.html (`vendor/pi_web_ui.css`)
// reaches descendants — most importantly the <pi-chat-panel> mounted by
// dao-chat-view. Scoped styles that used to live in `static styles` are
// inlined into the render tree with a `.dao-app-*` prefix to avoid
// collisions with pi-web-ui Tailwind utilities.

import {CrLitElement, html} from '//resources/lit/v3_0/lit.rollup.js';

import './dao_chat_view.js';
import './dao_settings_view.js';
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

  declare private activeTab_: string;
  declare private toastText_: string;
  declare private toastVisible_: boolean;
  private toastTimer_ = 0;

  constructor() {
    super();
    this.activeTab_ = 'chat';
    this.toastText_ = '';
    this.toastVisible_ = false;
  }


  override createRenderRoot(): HTMLElement | DocumentFragment {
    return this;
  }

  override connectedCallback() {
    super.connectedCallback();
    // WebContents visibility flips when DaoAgentSidebarView toggles SetVisible
    // AND when the browser window itself goes to background / gets minimized.
    // We can't tell those apart from this event alone, so do NOT abort the
    // in-flight stream on hidden — users expect replies to keep accumulating
    // while they switch apps. The stream is still torn down in
    // disconnectedCallback if the component unmounts, and the user can abort
    // explicitly via the composer's stop button or "new session".
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible') {
        setTimeout(() => this.getChatView_()?.focusInput(), 100);
      }
    });
    if (document.visibilityState === 'visible') {
      setTimeout(() => this.getChatView_()?.focusInput(), 100);
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

    // Hook for the C++ command bar: opens a fresh chat session and submits
    // `text` as the first user turn. Retries briefly while the chat view
    // finishes mounting — firstUpdated installs the sendMessage monkey-patch
    // asynchronously, so the first call from C++ can land before it is ready.
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (window as any).__daoExternalSubmit = (text: string) => {
      if (typeof text !== 'string' || !text) return;
      const deadline = Date.now() + 5000;
      const tryOnce = () => {
        if (this.activeTab_ !== 'chat') {
          this.activeTab_ = 'chat';
        }
        const view = this.getChatView_();
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        const iface: any = view?.querySelector('pi-chat-panel agent-interface');
        if (view && iface && typeof iface.sendMessage === 'function') {
          void view.submitExternalPrompt(text);
          return;
        }
        if (Date.now() < deadline) {
          setTimeout(tryOnce, 80);
        }
      };
      // Let the active-tab flip render before the first attempt.
      this.updateComplete.then(tryOnce);
    };
  }

  override render() {
    const newChatIcon = html`<svg width="16" height="16" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round">
      <path d="M12 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0
        2-2v-7"/>
      <path d="M18.375 2.625a1 1 0 0 1 3 3l-9.013 9.014a2 2 0 0
        1-.853.505l-2.873.84a.5.5 0 0 1-.62-.62l.84-2.873a2 2 0 0
        1 .506-.852z"/></svg>`;
    const historyIcon = html`<svg width="16" height="16" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round">
      <path d="M3 12a9 9 0 1 0 3-6.7L3 8"/>
      <path d="M3 3v5h5"/>
      <path d="M12 7v5l4 2"/></svg>`;
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
      <style>
        dao-agent-app {
          display: flex;
          flex-direction: column;
          height: 100%;
          overflow: hidden;
        }
        .dao-app-header {
          display: flex;
          align-items: center;
          justify-content: space-between;
          padding: 8px 12px;
          flex-shrink: 0;
          border-bottom: 1px solid rgba(255,255,255,0.12);
        }
        .dao-app-header-left {
          display: flex;
          align-items: center;
          gap: 6px;
          min-width: 0;
        }
        .dao-app-header-title {
          font-size: 13px;
          font-weight: 600;
          color: var(--text-secondary);
          letter-spacing: 0.2px;
        }
        .dao-app-tab-bar { display: flex; gap: 3px; }
        dao-agent-app .dao-app-tab {
          width: 28px; height: 28px;
          display: flex; align-items: center; justify-content: center;
          background: none; border: none; border-radius: 8px;
          color: var(--text-tertiary); cursor: pointer;
          transition: color 0.15s, background 0.15s;
        }
        dao-agent-app .dao-app-tab:hover {
          color: var(--text-secondary);
          background: rgba(255,255,255,0.18);
        }
        dao-agent-app .dao-app-tab.active {
          color: rgb(70, 120, 190);
          background: rgba(70, 120, 190, 0.15);
          box-shadow: 0 1px 4px rgba(70, 120, 190, 0.12);
        }
        .dao-app-toast {
          position: fixed; top: 16px; left: 50%;
          transform: translateX(-50%);
          background: var(--accent); color: white;
          padding: 8px 18px; border-radius: 10px;
          font-size: 12px; z-index: 200;
          box-shadow: 0 4px 16px rgba(70, 120, 190, 0.3);
          animation: daoAppToastIn 200ms ease-out;
        }
        @keyframes daoAppToastIn {
          from { opacity: 0; transform: translateX(-50%) translateY(-8px); }
          to { opacity: 1; transform: translateX(-50%) translateY(0); }
        }
      </style>
      <div class="dao-app-header">
        <div class="dao-app-header-left">
          <span class="dao-app-header-title">Dao Agent</span>
          <button class="dao-app-tab"
              @click=${this.onNewChatClick_} title="New chat">
            ${newChatIcon}
          </button>
          <button class="dao-app-tab"
              @click=${this.onHistoryClick_} title="Chat history">
            ${historyIcon}
          </button>
        </div>
        <div class="dao-app-tab-bar">
          <button class="dao-app-tab ${this.activeTab_ === 'chat' ? 'active' : ''}"
              @click=${() => this.onTabClick_('chat')} title="Chat">
            ${chatIcon}
          </button>
          <button class="dao-app-tab ${this.activeTab_ === 'settings' ? 'active' : ''}"
              @click=${() => this.onTabClick_('settings')} title="Settings">
            ${settingsIcon}
          </button>
        </div>
      </div>
      <dao-chat-view ?hidden=${this.activeTab_ !== 'chat'}></dao-chat-view>
      <dao-settings-view
          ?hidden=${this.activeTab_ !== 'settings'}></dao-settings-view>
      ${this.toastVisible_ ?
          html`<div class="dao-app-toast">${this.toastText_}</div>` : ''}
    `;
  }

  private onTabClick_(tab: string) {
    if (this.activeTab_ === tab) {
      chrome.send('closeSidebar');
    } else {
      this.activeTab_ = tab;
    }
  }

  private onNewChatClick_() {
    if (this.activeTab_ !== 'chat') {
      this.activeTab_ = 'chat';
      this.updateComplete.then(() => {
        this.getChatView_()?.startNewSession();
      });
    } else {
      this.getChatView_()?.startNewSession();
    }
    this.showToast_('Started a new chat', 1500);
  }

  private onHistoryClick_() {
    if (this.activeTab_ !== 'chat') {
      this.activeTab_ = 'chat';
      this.updateComplete.then(() => {
        this.getChatView_()?.openHistory();
      });
    } else {
      this.getChatView_()?.openHistory();
    }
  }

  private getChatView_(): DaoChatView|null {
    return this.querySelector('dao-chat-view');
  }

  private getSettingsView_(): DaoSettingsView|null {
    return this.querySelector('dao-settings-view');
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
