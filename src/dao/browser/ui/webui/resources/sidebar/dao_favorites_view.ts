// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative} from './sidebar_bridge.js';
import type {TabData} from './sidebar_bridge.js';

export class DaoFavoritesView extends CrLitElement {
  static get is() {
    return 'dao-favorites-view';
  }

  static override get styles() {
    return css`
      :host {
        display: flex;
        flex-direction: row;
        flex-wrap: wrap;
        gap: 4px;
        padding: 4px 10px;
      }

      .fav-btn {
        width: 30px;
        height: 30px;
        border: none;
        border-radius: var(--radius-favorite, 8px);
        background: transparent;
        cursor: default;
        display: flex;
        align-items: center;
        justify-content: center;
        padding: 0;
        transition: background 0.15s ease;
      }

      .fav-btn:hover {
        background: var(--ink-drop);
      }

      .fav-btn:active {
        background: var(--surface);
      }

      .fav-btn img {
        width: 20px;
        height: 20px;
        border-radius: 4px;
      }

      .fav-btn .placeholder {
        width: 20px;
        height: 20px;
        border-radius: 4px;
        background: var(--surface);
      }
    `;
  }

  static override get properties() {
    return {
      tabs: {type: Array},
    };
  }

  tabs: TabData[] = [];

  override render() {
    return html`
      ${this.tabs.map(tab => html`
        <button class="fav-btn"
                title=${tab.title}
                @click=${() => this.onActivate_(tab.index)}>
          ${tab.faviconUrl
              ? html`<img src=${tab.faviconUrl} alt="">`
              : html`<div class="placeholder"></div>`}
        </button>
      `)}
    `;
  }

  private onActivate_(index: number) {
    sendNative('activateTab', index);
  }
}

customElements.define('dao-favorites-view', DaoFavoritesView);
