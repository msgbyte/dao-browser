// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {markWelcomeShown} from './welcome_bridge.js';

interface ShortcutStep {
  keys: string[];
  text: string;
  action: string;
}

const STEPS: ShortcutStep[] = [
  {keys: ['\u2318', 'T'], text: 'Create a new tab in the sidebar', action: 'newTab'},
  {keys: ['\u2318', 'W'], text: 'Close the current tab', action: 'closeTab'},
  {keys: ['\u2318', 'L'], text: 'Open the command bar for quick navigation', action: 'commandBar'},
  {keys: ['\u2318', 'S'], text: 'Toggle the sidebar', action: 'toggleSidebar'},
  {keys: ['\u2318', 'E'], text: 'Open the AI Agent panel', action: 'agentPanel'},
  {keys: ['\u2318', 'D'], text: 'Duplicate the current tab', action: 'dupTab'},
  {keys: ['\u2318', '\u21E7', 'C'], text: 'Copy the current page URL', action: 'copyUrl'},
];

const INTERVAL_MS = 4000;

export class DaoWelcomeApp extends CrLitElement {
  static get is() {
    return 'dao-welcome-app';
  }

  static override get properties() {
    return {
      currentStep_: {type: Number},
      sidebarCollapsed_: {type: Boolean},
      cmdOverlayVisible_: {type: Boolean},
      agentPanelVisible_: {type: Boolean},
      toastVisible_: {type: Boolean},
      tab3Visible_: {type: Boolean},
      tab3Active_: {type: Boolean},
      tab3Anim_: {type: String},
    };
  }

  static override get styles() {
    return css`
      :host {
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        min-height: 100vh;
        padding: 40px 20px;
      }

      .page { max-width: 900px; width: 100%; }

      /* Hero */
      .hero { text-align: center; padding: 40px 0 32px; }
      .hero h1 {
        font-size: 42px;
        font-weight: 700;
        color: #fff;
        text-shadow:
          0 0 10px rgba(255,255,255,0.15),
          0 0 30px rgba(255,255,255,0.08),
          0 0 60px rgba(255,255,255,0.04);
      }
      .hero p {
        color: rgba(255,255,255,0.45);
        font-size: 14px;
        margin-top: 8px;
      }

      /* Browser frame */
      .browser-wrapper { position: relative; max-width: 720px; margin: 0 auto; }
      .browser-frame {
        background: #1c1c1c;
        border-radius: 0 16px 16px 0;
        overflow: hidden;
        box-shadow: 0 8px 40px rgba(0,0,0,0.5);
      }
      .browser-inner {
        display: flex;
        height: 360px;
        position: relative;
      }

      /* Sidebar skeleton */
      .sk-sidebar {
        width: 180px;
        min-width: 180px;
        background: rgb(28,28,28);
        padding: 12px;
        display: flex;
        flex-direction: column;
        gap: 4px;
        transition: min-width 0.6s ease, width 0.6s ease,
                    opacity 0.6s ease, padding 0.6s ease;
        overflow: hidden;
      }
      .sk-sidebar.collapsed {
        min-width: 4px; width: 4px;
        opacity: 0.3; padding: 0;
      }
      .sk-search {
        height: 32px;
        background: rgba(255,255,255,0.06);
        border-radius: 8px;
        margin-bottom: 8px;
        display: flex;
        align-items: center;
        padding: 0 10px;
        flex-shrink: 0;
      }
      .sk-search-text {
        font-size: 10px;
        color: rgba(255,255,255,0.25);
        white-space: nowrap;
      }
      .sk-tab {
        height: 36px; min-height: 36px;
        border-radius: 8px;
        padding: 0 10px;
        display: flex;
        align-items: center;
        gap: 8px;
        overflow: hidden;
      }
      .sk-tab.active { background: rgba(255,255,255,0.1); }
      .sk-favicon {
        width: 14px; height: 14px;
        border-radius: 4px;
        background: rgba(255,255,255,0.1);
        flex-shrink: 0;
      }
      .sk-title {
        height: 8px;
        border-radius: 3px;
        background: rgba(255,255,255,0.1);
        flex: 1;
      }
      .sk-tab.active .sk-title { background: rgba(255,255,255,0.25); }
      .sk-close {
        width: 12px; height: 12px;
        border-radius: 3px;
        background: rgba(255,255,255,0.06);
        flex-shrink: 0;
        opacity: 0;
        transition: opacity 0.3s;
      }
      .sk-tab.active .sk-close { opacity: 1; }
      .sk-newtab {
        height: 32px; min-height: 32px;
        border-radius: 8px;
        margin-top: 4px;
        display: flex;
        align-items: center;
        justify-content: center;
        border: 1px dashed rgba(255,255,255,0.1);
        flex-shrink: 0;
      }
      .sk-newtab-icon { font-size: 14px; color: rgba(255,255,255,0.2); }
      .sk-spacer { flex: 1; }
      .sk-bottom-item {
        height: 28px; min-height: 28px;
        border-radius: 6px;
        background: rgba(255,255,255,0.04);
        flex-shrink: 0;
      }

      /* Content area */
      .sk-content-wrapper {
        flex: 1; position: relative;
        display: flex; flex-direction: column;
      }
      .sk-content {
        flex: 1;
        background: rgba(255,255,255,0.03);
        margin: 6px;
        border-radius: 10px;
        padding: 20px;
        display: flex;
        flex-direction: column;
        gap: 10px;
        overflow: hidden;
      }
      .sk-content-bar {
        height: 10px;
        border-radius: 4px;
        background: rgba(255,255,255,0.04);
        flex-shrink: 0;
      }

      /* Agent panel */
      .agent-panel {
        width: 0;
        background: #1c1c1c;
        transition: width 0.5s ease, min-width 0.5s ease;
        overflow: hidden;
        display: flex;
        flex-direction: column;
      }
      .agent-panel.show { width: 260px; min-width: 260px; }
      .agent-inner {
        width: 260px;
        padding: 16px;
        border-left: 1px solid rgba(255,255,255,0.08);
        height: 100%;
      }
      .agent-header {
        font-size: 12px;
        font-weight: 600;
        color: rgba(255,255,255,0.7);
        margin-bottom: 12px;
      }
      .agent-msg {
        height: 8px;
        background: rgba(255,255,255,0.06);
        border-radius: 4px;
        margin-bottom: 6px;
      }

      /* Command bar overlay */
      .command-overlay {
        position: absolute; inset: 0;
        background: rgba(0,0,0,0.5);
        display: flex;
        align-items: flex-start;
        justify-content: center;
        padding-top: 60px;
        opacity: 0;
        transition: opacity 0.4s;
        pointer-events: none;
        z-index: 10;
      }
      .command-overlay.show { opacity: 1; }
      .command-bar {
        width: 380px;
        background: #242424;
        border-radius: 14px;
        padding: 12px;
        box-shadow: 0 8px 30px rgba(0,0,0,0.5);
        border: 1px solid rgba(255,255,255,0.1);
      }
      .command-input {
        height: 36px;
        background: rgba(255,255,255,0.06);
        border-radius: 10px;
        display: flex;
        align-items: center;
        padding: 0 12px;
      }
      .command-input-text {
        font-size: 11px;
        color: rgba(255,255,255,0.3);
      }
      .typing-cursor {
        display: inline-block;
        width: 1px; height: 14px;
        background: rgba(255,255,255,0.7);
        margin-left: 2px;
        animation: blink 0.8s infinite;
      }
      @keyframes blink {
        0%, 50% { opacity: 1; }
        51%, 100% { opacity: 0; }
      }

      /* Toast */
      .toast {
        position: absolute;
        bottom: 20px; left: 50%;
        transform: translateX(-50%) translateY(20px);
        background: rgba(255,255,255,0.85);
        color: #111;
        padding: 8px 20px;
        border-radius: 8px;
        font-size: 12px;
        opacity: 0;
        transition: all 0.4s ease;
        z-index: 10;
        white-space: nowrap;
      }
      .toast.show {
        opacity: 1;
        transform: translateX(-50%) translateY(0);
      }

      /* Tab animations */
      .sk-tab.appearing {
        animation: tabAppear 0.5s ease forwards;
      }
      @keyframes tabAppear {
        from { transform: translateX(-100%); opacity: 0; }
        to { transform: translateX(0); opacity: 1; }
      }
      .sk-tab.closing {
        animation: tabClose 0.4s ease forwards;
      }
      @keyframes tabClose {
        from { transform: translateX(0); opacity: 1; }
        to { transform: translateX(-100%); opacity: 0; }
      }

      /* Annotation */
      .annotation {
        text-align: center;
        margin-top: 24px;
        min-height: 70px;
      }
      .shortcut-badge {
        display: inline-flex;
        align-items: center;
        gap: 6px;
        background: rgba(255,255,255,0.1);
        border: 1px solid rgba(255,255,255,0.2);
        border-radius: 10px;
        padding: 8px 20px;
        margin-bottom: 10px;
        animation: slideIn 0.5s ease forwards;
      }
      .shortcut-badge kbd {
        background: rgba(255,255,255,0.1);
        border-radius: 5px;
        padding: 3px 8px;
        font-size: 13px;
        font-family: system-ui;
        font-weight: 600;
        color: rgba(255,255,255,0.9);
      }
      .shortcut-badge .plus {
        color: rgba(255,255,255,0.3);
        font-size: 12px;
      }
      .annotation-text {
        font-size: 15px;
        color: rgba(255,255,255,0.7);
        animation: slideIn 0.5s ease forwards;
      }
      @keyframes slideIn {
        from { opacity: 0; transform: translateY(8px); }
        to { opacity: 1; transform: translateY(0); }
      }

      /* Navigation */
      .nav-row {
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 16px;
        margin-top: 28px;
        padding-bottom: 10px;
      }
      .nav-arrow {
        width: 36px; height: 36px;
        border-radius: 50%;
        background: rgba(255,255,255,0.06);
        border: 1px solid rgba(255,255,255,0.1);
        display: flex;
        align-items: center;
        justify-content: center;
        cursor: pointer;
        transition: all 0.2s;
        color: rgba(255,255,255,0.5);
        font-size: 16px;
        user-select: none;
      }
      .nav-arrow:hover {
        background: rgba(255,255,255,0.1);
        border-color: rgba(255,255,255,0.2);
        color: rgba(255,255,255,0.9);
      }
      .nav-arrow:active { transform: scale(0.92); }
      .nav-dots {
        display: flex;
        gap: 8px;
      }
      .dot {
        width: 10px; height: 10px;
        border-radius: 50%;
        background: rgba(255,255,255,0.12);
        cursor: pointer;
        transition: all 0.3s;
      }
      .dot.active {
        background: rgba(255,255,255,0.7);
        transform: scale(1.2);
      }
      .dot:hover:not(.active) {
        background: rgba(255,255,255,0.25);
      }
      /* GitHub link */
      .github-link {
        position: fixed;
        bottom: 20px;
        right: 24px;
        display: flex;
        align-items: center;
        gap: 8px;
        color: rgba(255,255,255,0.35);
        font-size: 12px;
        text-decoration: none;
        transition: color 0.2s;
      }
      .github-link:hover {
        color: rgba(255,255,255,0.7);
      }
      .github-link svg {
        width: 18px;
        height: 18px;
        fill: currentColor;
        flex-shrink: 0;
      }
    `;
  }

  currentStep_: number = 0;
  sidebarCollapsed_: boolean = false;
  cmdOverlayVisible_: boolean = false;
  agentPanelVisible_: boolean = false;
  toastVisible_: boolean = false;
  tab3Visible_: boolean = false;
  tab3Active_: boolean = false;
  tab3Anim_: string = '';

  private intervalId_: number = 0;
  private actionTimeouts_: number[] = [];

  override connectedCallback() {
    super.connectedCallback();
    markWelcomeShown();
    this.runAction_(STEPS[0]!.action);
    this.startAutoplay_();
    document.addEventListener('keydown', this.onKeyDown_);
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.stopAutoplay_();
    this.clearActionTimeouts_();
    document.removeEventListener('keydown', this.onKeyDown_);
  }

  private onKeyDown_ = (e: KeyboardEvent) => {
    if (e.key === 'ArrowRight') {
      this.goNext_();
    } else if (e.key === 'ArrowLeft') {
      this.goPrev_();
    }
  };

  private startAutoplay_() {
    this.stopAutoplay_();
    this.intervalId_ = window.setInterval(() => this.goNext_(), INTERVAL_MS);
  }

  private stopAutoplay_() {
    if (this.intervalId_) {
      window.clearInterval(this.intervalId_);
      this.intervalId_ = 0;
    }
  }

  private clearActionTimeouts_() {
    for (const id of this.actionTimeouts_) {
      window.clearTimeout(id);
    }
    this.actionTimeouts_ = [];
  }

  private addTimeout_(fn: () => void, ms: number) {
    this.actionTimeouts_.push(window.setTimeout(fn, ms));
  }

  private goTo_(index: number) {
    this.currentStep_ = index;
    this.runAction_(STEPS[index]!.action);
    this.startAutoplay_();
  }

  private goNext_() {
    this.goTo_((this.currentStep_ + 1) % STEPS.length);
  }

  private goPrev_() {
    this.goTo_((this.currentStep_ - 1 + STEPS.length) % STEPS.length);
  }

  private resetState_() {
    this.clearActionTimeouts_();
    this.tab3Visible_ = false;
    this.tab3Active_ = false;
    this.tab3Anim_ = '';
    this.cmdOverlayVisible_ = false;
    this.agentPanelVisible_ = false;
    this.toastVisible_ = false;
    this.sidebarCollapsed_ = false;
  }

  private runAction_(action: string) {
    this.resetState_();

    switch (action) {
      case 'newTab':
        this.addTimeout_(() => {
          this.tab3Visible_ = true;
          this.tab3Anim_ = 'appearing';
          this.requestUpdate();
        }, 400);
        break;
      case 'closeTab':
        this.tab3Visible_ = true;
        this.tab3Anim_ = '';
        this.addTimeout_(() => {
          this.tab3Anim_ = 'closing';
          this.requestUpdate();
        }, 500);
        break;
      case 'commandBar':
        this.addTimeout_(() => {
          this.cmdOverlayVisible_ = true;
          this.requestUpdate();
        }, 300);
        this.addTimeout_(() => {
          this.cmdOverlayVisible_ = false;
          this.requestUpdate();
        }, 2800);
        break;
      case 'toggleSidebar':
        this.addTimeout_(() => {
          this.sidebarCollapsed_ = true;
          this.requestUpdate();
        }, 300);
        this.addTimeout_(() => {
          this.sidebarCollapsed_ = false;
          this.requestUpdate();
        }, 1800);
        break;
      case 'agentPanel':
        this.addTimeout_(() => {
          this.agentPanelVisible_ = true;
          this.requestUpdate();
        }, 300);
        this.addTimeout_(() => {
          this.agentPanelVisible_ = false;
          this.requestUpdate();
        }, 2800);
        break;
      case 'dupTab':
        this.addTimeout_(() => {
          this.tab3Visible_ = true;
          this.tab3Active_ = true;
          this.tab3Anim_ = 'appearing';
          this.requestUpdate();
        }, 400);
        break;
      case 'copyUrl':
        this.addTimeout_(() => {
          this.toastVisible_ = true;
          this.requestUpdate();
        }, 300);
        this.addTimeout_(() => {
          this.toastVisible_ = false;
          this.requestUpdate();
        }, 2200);
        break;
    }
  }

  override render() {
    const step = STEPS[this.currentStep_]!;

    return html`
      <div class="page">
        <div class="hero">
          <h1>Welcome to Dao</h1>
          <p>An opinionated browser. Keyboard-first, shortcut-driven.</p>
        </div>

        <div class="browser-wrapper">
          <div class="browser-frame">
            <div class="browser-inner">
              <div class="sk-sidebar ${this.sidebarCollapsed_ ? 'collapsed' : ''}">
                <div class="sk-search">
                  <span class="sk-search-text">Search or enter URL...</span>
                </div>
                <div class="sk-tab active">
                  <div class="sk-favicon"></div>
                  <div class="sk-title" style="width:70%"></div>
                  <div class="sk-close"></div>
                </div>
                <div class="sk-tab">
                  <div class="sk-favicon"></div>
                  <div class="sk-title" style="width:55%"></div>
                  <div class="sk-close"></div>
                </div>
                ${this.tab3Visible_ ? html`
                  <div class="sk-tab ${this.tab3Active_ ? 'active' : ''} ${this.tab3Anim_}">
                    <div class="sk-favicon"></div>
                    <div class="sk-title" style="width:65%"></div>
                    <div class="sk-close"></div>
                  </div>
                ` : ''}
                <div class="sk-newtab">
                  <span class="sk-newtab-icon">+</span>
                </div>
                <div class="sk-spacer"></div>
                <div class="sk-bottom-item"></div>
              </div>

              <div class="sk-content-wrapper">
                <div class="sk-content">
                  <div class="sk-content-bar" style="width:75%"></div>
                  <div class="sk-content-bar" style="width:90%"></div>
                  <div class="sk-content-bar" style="width:60%"></div>
                  <div class="sk-content-bar" style="width:85%"></div>
                  <div class="sk-content-bar" style="width:45%"></div>
                  <div style="flex:1"></div>
                  <div class="sk-content-bar" style="width:70%"></div>
                  <div class="sk-content-bar" style="width:80%"></div>
                </div>
              </div>

              <div class="agent-panel ${this.agentPanelVisible_ ? 'show' : ''}">
                <div class="agent-inner">
                  <div class="agent-header">Dao Agent</div>
                  <div class="agent-msg" style="width:80%"></div>
                  <div class="agent-msg" style="width:60%"></div>
                  <div class="agent-msg" style="width:90%"></div>
                  <div class="agent-msg" style="width:50%"></div>
                </div>
              </div>

              <div class="command-overlay ${this.cmdOverlayVisible_ ? 'show' : ''}">
                <div class="command-bar">
                  <div class="command-input">
                    <span class="command-input-text">Type a URL or search...</span>
                    <span class="typing-cursor"></span>
                  </div>
                </div>
              </div>

              <div class="toast ${this.toastVisible_ ? 'show' : ''}">
                URL copied to clipboard
              </div>
            </div>
          </div>
        </div>

        <div class="annotation">
          <div class="shortcut-badge">
            ${step.keys.map((key, i) => html`
              ${i > 0 ? html`<span class="plus">+</span>` : ''}
              <kbd>${key}</kbd>
            `)}
          </div>
          <div class="annotation-text">${step.text}</div>
        </div>

        <div class="nav-row">
          <div class="nav-arrow" @click=${() => this.goPrev_()}>&#8592;</div>
          <div class="nav-dots">
            ${STEPS.map((_, i) => html`
              <div class="dot ${i === this.currentStep_ ? 'active' : ''}"
                   @click=${() => this.goTo_(i)}></div>
            `)}
          </div>
          <div class="nav-arrow" @click=${() => this.goNext_()}>&#8594;</div>
        </div>

      </div>

      <a class="github-link" href="https://github.com/msgbyte/dao-browser" target="_blank">
        <svg viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg">
          <path d="M12 2C6.477 2 2 6.484 2 12.017c0 4.425 2.865 8.18 6.839 9.504.5.092.682-.217.682-.483 0-.237-.008-.868-.013-1.703-2.782.605-3.369-1.343-3.369-1.343-.454-1.158-1.11-1.466-1.11-1.466-.908-.62.069-.608.069-.608 1.003.07 1.531 1.032 1.531 1.032.892 1.53 2.341 1.088 2.91.832.092-.647.35-1.088.636-1.338-2.22-.253-4.555-1.113-4.555-4.951 0-1.093.39-1.988 1.029-2.688-.103-.253-.446-1.272.098-2.65 0 0 .84-.27 2.75 1.026A9.564 9.564 0 0112 6.844c.85.004 1.705.115 2.504.337 1.909-1.296 2.747-1.027 2.747-1.027.546 1.379.202 2.398.1 2.651.64.7 1.028 1.595 1.028 2.688 0 3.848-2.339 4.695-4.566 4.943.359.309.678.92.678 1.855 0 1.338-.012 2.419-.012 2.747 0 .268.18.58.688.482A10.019 10.019 0 0022 12.017C22 6.484 17.522 2 12 2z"/>
        </svg>
        Like Dao? Give us a star!
      </a>
    `;
  }
}

customElements.define(DaoWelcomeApp.is, DaoWelcomeApp);
