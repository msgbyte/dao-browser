// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative} from './sidebar_bridge.js';

export class DaoNewTabButton extends CrLitElement {
  static get is() {
    return 'dao-new-tab-button';
  }

  static override get properties() {
    return {
      highlighted: {type: Boolean, reflect: true},
    };
  }

  highlighted: boolean = false;

  static override get styles() {
    return css`
      :host {
        display: block;
      }

      .new-tab-btn {
        display: flex;
        align-items: center;
        gap: 8px;
        width: 100%;
        height: 36px;
        padding: 0 10px;
        border: none;
        border-radius: var(--radius-tab, 12px);
        background: transparent;
        color: var(--text-secondary);
        font-family: var(--font-family, system-ui);
        font-size: 13px;
        cursor: default;
        transition: background 0.15s ease, color 0.15s ease;
      }

      .new-tab-btn:hover {
        background: var(--ink-drop);
      }

      .new-tab-btn:active {
        background: var(--surface);
      }

      :host([highlighted]) .new-tab-btn {
        background: var(--surface-active);
        color: var(--text-primary);
        box-shadow: 0 1px 3px rgba(0, 0, 0, 0.08),
                    0 1px 2px rgba(0, 0, 0, 0.06);
      }

      .icon {
        width: 16px;
        height: 16px;
        flex-shrink: 0;
      }
    `;
  }

  override render() {
    return html`
      <button class="new-tab-btn" @click=${this.onClick_}>
        <svg class="icon" viewBox="0 0 24 24" fill="none"
             stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
          <line x1="12" y1="5" x2="12" y2="19"></line>
          <line x1="5" y1="12" x2="19" y2="12"></line>
        </svg>
        New Tab
      </button>
    `;
  }

  private onClick_() {
    sendNative('showCommandBarForNewTab');
  }
}

customElements.define('dao-new-tab-button', DaoNewTabButton);
