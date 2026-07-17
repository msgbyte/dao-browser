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

import {callNative} from './agent_bridge.js';
import './dao_chat_view.js';
import './dao_dream_dispatcher.js';
import './dao_settings_view.js';
import type {DaoChatView} from './dao_chat_view.js';
import type {DaoSettingsView} from './dao_settings_view.js';
import {initI18n} from './i18n/i18n.js';
import {refreshSkillRegistryIfStale} from './skill_registry.js';

// Kick off locale loading at module import time so the dictionary is in
// place before most t() calls from child views. The first render can still
// beat the async locale import, so connectedCallback refreshes mounted views
// when this promise resolves.
const i18nReady = initI18n();

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
        // Cross-WebUI sync: skills created in the dao://skills tab while
        // this sidebar was hidden won't have propagated to our cache.
        // Throttled — no-op when the registry is already fresh.
        void refreshSkillRegistryIfStale();
      }
    });
    if (document.visibilityState === 'visible') {
      setTimeout(() => this.getChatView_()?.focusInput(), 100);
    }
    this.addEventListener('show-toast', ((e: CustomEvent) => {
      this.showToast_(e.detail.text);
    }) as EventListener);

    // Delegated link interception: any anchor click anywhere inside the
    // agent UI (markdown-rendered chat messages, tool result panels like
    // search results, etc.) should open in the main browser webcontent
    // as a new foreground tab, NOT inside this `dao://agent` WebUI frame.
    // The WebUI itself can't navigate to arbitrary http(s) URLs in its
    // own context, and `target="_blank"` on a chrome:// page either
    // no-ops or pops a stray window — both are bad UX. We listen on the
    // capture phase so we win against any descendant handlers.
    this.addEventListener(
        'click', this.onDelegatedLinkClick_ as EventListener, true);
    this.addEventListener(
        'auxclick', this.onDelegatedLinkClick_ as EventListener, true);
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
    // Options.includePageContext defaults to true (Cmd+L behavior); the
    // Cmd+T path explicitly passes false to omit current-tab content from
    // the first turn.
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (window as any).__daoExternalSubmit =
        (text: string,
         options?: {includePageContext?: boolean}) => {
      if (typeof text !== 'string' || !text) return;
      const includePageContext = options?.includePageContext !== false;
      const deadline = Date.now() + 5000;
      const tryOnce = () => {
        if (this.activeTab_ !== 'chat') {
          this.activeTab_ = 'chat';
        }
        const view = this.getChatView_();
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        const iface: any = view?.querySelector('pi-chat-panel agent-interface');
        if (view && iface && typeof iface.sendMessage === 'function') {
          void view.submitExternalPrompt(text, {includePageContext});
          return;
        }
        if (Date.now() < deadline) {
          setTimeout(tryOnce, 80);
        }
      };
      // Let the active-tab flip render before the first attempt.
      this.updateComplete.then(tryOnce);
    };

    void i18nReady.then(() => {
      if (!this.isConnected) return;
      this.refreshLocalizedViews_();
    });
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

  // Walks up the composed event path (handles Shadow DOM and slotted
  // content) to find the nearest <a> ancestor of the click target.
  private findAnchor_(e: MouseEvent): HTMLAnchorElement|null {
    const path = e.composedPath();
    for (const node of path) {
      if (node instanceof HTMLAnchorElement) {
        return node;
      }
    }
    return null;
  }

  private onDelegatedLinkClick_ = (e: MouseEvent) => {
    // Only handle plain left-click and middle-click. Right-click opens
    // the context menu; we leave that alone.
    if (e.type === 'click' && e.button !== 0) return;
    if (e.type === 'auxclick' && e.button !== 1) return;

    const anchor = this.findAnchor_(e);
    if (!anchor) return;

    // `download` anchors aren't navigations.
    if (anchor.hasAttribute('download')) return;

    // Resolve the href against the document. anchor.href is already
    // absolute; we just need to filter on protocol.
    const raw = anchor.getAttribute('href');
    if (!raw) return;
    // Skip in-page anchors and javascript: pseudo-URLs outright.
    if (raw.startsWith('#') ||
        raw.toLowerCase().startsWith('javascript:')) {
      return;
    }

    let url: URL;
    try {
      url = new URL(anchor.href);
    } catch {
      return;
    }

    // Only route real web URLs to the main webcontent. Internal
    // chrome:// / dao:// links (if any future feature uses them) keep
    // their default behavior.
    const protocol = url.protocol;
    if (protocol !== 'http:' && protocol !== 'https:' &&
        protocol !== 'mailto:' && protocol !== 'ftp:') {
      return;
    }

    // We're going to handle this — block the WebUI's default handling
    // (which would either no-op or pop an unwanted window).
    e.preventDefault();
    e.stopPropagation();

    void callNative('openTab', {url: url.href});
  };

  private getChatView_(): DaoChatView|null {
    return this.querySelector('dao-chat-view');
  }

  private getSettingsView_(): DaoSettingsView|null {
    return this.querySelector('dao-settings-view');
  }

  private refreshLocalizedViews_() {
    this.requestUpdate();
    const root = this.shadowRoot ?? this;
    for (const el of root.querySelectorAll('*')) {
      const litElement = el as HTMLElement & {requestUpdate?: () => void};
      litElement.requestUpdate?.();
    }
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
