// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {
  SIDEBAR_POINTER_EXITED_EVENT,
  sendNative,
} from './sidebar_bridge.js';
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

      :host([hover-suppressed]) .fav-btn:hover {
        background: transparent;
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
      hoverSuppressed_: {
        type: Boolean,
        reflect: true,
        attribute: 'hover-suppressed',
      },
    };
  }

  declare tabs: TabData[];
  declare protected hoverSuppressed_: boolean;

  private tooltipTimer_: number = 0;
  private tooltipScheduled_: boolean = false;
  private tooltipVisible_: boolean = false;
  private lastMouseX_: number = 0;
  private lastMouseY_: number = 0;
  private boundSidebarPointerExited_ = () => this.onSidebarPointerExited_();

  constructor() {
    super();
    this.tabs = [];
    this.hoverSuppressed_ = false;
  }

  override connectedCallback() {
    super.connectedCallback();
    window.addEventListener(
        SIDEBAR_POINTER_EXITED_EVENT, this.boundSidebarPointerExited_);
  }

  override disconnectedCallback() {
    window.removeEventListener(
        SIDEBAR_POINTER_EXITED_EVENT, this.boundSidebarPointerExited_);
    super.disconnectedCallback?.();
  }

  override render() {
    return html`
      ${this.tabs.map(tab => html`
        <button class="fav-btn"
                aria-label=${this.getTooltipTitle_(tab)}
                @click=${() => this.onActivate_(tab.index)}
                @mouseenter=${(e: MouseEvent) => this.onShowTooltip_(e, tab)}
                @mousemove=${(e: MouseEvent) => this.onTrackMouse_(e, tab)}
                @mouseleave=${() => this.onHideTooltip_()}>
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

  private onTrackMouse_(e: MouseEvent, tab: TabData) {
    this.setHoverSuppressed_(false);
    this.lastMouseX_ = e.screenX;
    this.lastMouseY_ = e.screenY;
    this.scheduleTooltip_(tab);
  }

  private onShowTooltip_(e: MouseEvent, tab: TabData) {
    this.setHoverSuppressed_(false);
    this.lastMouseX_ = e.screenX;
    this.lastMouseY_ = e.screenY;
    this.scheduleTooltip_(tab);
  }

  private scheduleTooltip_(tab: TabData) {
    this.clearTooltip_(false);
    this.tooltipScheduled_ = true;
    this.tooltipTimer_ = window.setTimeout(() => {
      this.tooltipTimer_ = 0;
      this.tooltipScheduled_ = false;
      this.tooltipVisible_ = true;
      sendNative(
          'showTabTooltip',
          this.lastMouseX_ + 4,
          this.lastMouseY_ + 4,
          this.getTooltipTitle_(tab));
    }, 1500);
  }

  private onHideTooltip_() {
    this.clearTooltip_(true);
  }

  private onSidebarPointerExited_() {
    this.setHoverSuppressed_(true);
    this.clearTooltip_(true);
  }

  private setHoverSuppressed_(suppressed: boolean) {
    this.hoverSuppressed_ = suppressed;
    this.toggleAttribute('hover-suppressed', suppressed);
  }

  private clearTooltip_(sendHide: boolean) {
    const shouldHide = sendHide &&
        (this.tooltipScheduled_ || this.tooltipVisible_);
    if (this.tooltipTimer_) {
      window.clearTimeout(this.tooltipTimer_);
      this.tooltipTimer_ = 0;
    }
    this.tooltipScheduled_ = false;
    this.tooltipVisible_ = false;
    if (shouldHide) {
      sendNative('hideTabTooltip');
    }
  }

  private getTooltipTitle_(tab: TabData): string {
    return tab.title || tab.url || 'New Tab';
  }
}

customElements.define('dao-favorites-view', DaoFavoritesView);
