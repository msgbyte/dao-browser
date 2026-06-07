// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative} from './sidebar_bridge.js';
import type {UpdateStateData} from './sidebar_bridge.js';

export class DaoUpdateButton extends CrLitElement {
  static get is() {
    return 'dao-update-button';
  }

  static override get styles() {
    return css`
      :host {
        display: flex;
        align-items: center;
        justify-content: flex-end;
        flex-shrink: 0;
      }

      :host([hidden]) {
        display: none;
      }

      .update-btn {
        position: relative;
        display: flex;
        align-items: center;
        justify-content: flex-start;
        gap: 6px;
        width: 26px;
        height: 26px;
        min-width: 26px;
        max-width: 160px;
        overflow: hidden;
        border: none;
        border-radius: 8px;
        background: rgb(218, 232, 249);
        color: rgb(46, 84, 132);
        cursor: default;
        padding: 0 7px;
        font-family: var(--font-family, system-ui);
        font-size: 12px;
        font-weight: 600;
        white-space: nowrap;
        box-shadow: inset 0 0 0 1px rgba(70, 120, 190, 0.14),
                    0 1px 3px rgba(40, 73, 110, 0.08);
        transition: width 0.16s ease, background 0.12s ease,
                    box-shadow 0.12s ease, opacity 0.12s ease;
      }

      .update-btn:hover,
      .update-btn.expanded {
        width: 120px;
        background: rgb(204, 224, 247);
        box-shadow: inset 0 0 0 1px rgba(70, 120, 190, 0.18),
                    0 2px 7px rgba(40, 73, 110, 0.12);
      }

      .update-btn:disabled {
        opacity: 0.72;
      }

      .icon {
        width: 12px;
        height: 12px;
        flex: 0 0 12px;
        z-index: 1;
      }

      .label {
        position: absolute;
        inset: 0;
        box-sizing: border-box;
        padding: 0 24px;
        overflow: hidden;
        text-overflow: ellipsis;
        line-height: 26px;
        text-align: center;
        opacity: 0;
        pointer-events: none;
        transition: opacity 0.12s ease;
      }

      .update-btn:hover .label,
      .update-btn.expanded .label {
        opacity: 1;
      }

      @media (prefers-color-scheme: dark) {
        .update-btn {
          background: rgb(86, 145, 220);
          color: white;
          box-shadow: 0 1px 4px rgba(0, 0, 0, 0.28);
        }

        .update-btn:hover,
        .update-btn.expanded {
          background: rgb(96, 158, 234);
          box-shadow: 0 2px 8px rgba(0, 0, 0, 0.34);
        }
      }
    `;
  }

  static override get properties() {
    return {
      updateState: {type: Object},
      applying_: {type: Boolean},
      expanded_: {type: Boolean},
    };
  }

  declare updateState: UpdateStateData | null;
  declare protected applying_: boolean;
  declare protected expanded_: boolean;

  private lastUpdateState_: UpdateStateData|null = null;

  constructor() {
    super();
    this.updateState = null;
    this.applying_ = false;
    this.expanded_ = false;
  }

  override connectedCallback() {
    this.hidden = !this.isVisibleState_();
    super.connectedCallback();
  }

  override updated(changed: Map<PropertyKey, unknown>) {
    if (changed.has('updateState')) {
      this.syncFromUpdateState_();
    }
  }

  override render() {
    this.syncFromUpdateState_();
    if (!this.isVisibleState_()) {
      return html`${nothing}`;
    }

    const isApplying = this.isApplying_();
    const label = isApplying ?
        this.updateState?.applyingLabel || '' :
        this.updateState?.label || '';

    return isApplying ?
        html`
          <button class="${this.buttonClass_()}" title=${label} disabled
                  @click=${() => this.onClick_()}
                  @mouseleave=${() => this.onMouseLeave_()}>
            ${this.renderIcon_()}
            <span class="label">${label}</span>
          </button>
        ` :
        html`
          <button class="${this.buttonClass_()}" title=${label}
                  @click=${() => this.onClick_()}
                  @mouseleave=${() => this.onMouseLeave_()}>
            ${this.renderIcon_()}
            <span class="label">${label}</span>
          </button>
        `;
  }

  private onClick_() {
    this.syncFromUpdateState_();
    if (this.isApplying_()) {
      return;
    }

    this.applying_ = true;
    this.expanded_ = true;
    sendNative('applyReadyUpdate');
  }

  private onMouseLeave_() {
    this.expanded_ = false;
  }

  private renderIcon_() {
    return html`
      <svg class="icon" viewBox="0 0 24 24" fill="none"
           stroke="currentColor" stroke-width="2"
           stroke-linecap="round" stroke-linejoin="round">
        <circle cx="12" cy="12" r="10"></circle>
        <path d="m16 12-4-4-4 4"></path>
        <path d="M12 16V8"></path>
      </svg>
    `;
  }

  private syncFromUpdateState_() {
    this.hidden = !this.isVisibleState_();
    if (this.hidden && this.expanded_) {
      this.expanded_ = false;
    }
    if (this.lastUpdateState_ === this.updateState) {
      return;
    }

    this.lastUpdateState_ = this.updateState;
    if (this.updateState?.state !== 'applying' && this.applying_) {
      this.applying_ = false;
    }
  }

  private isVisibleState_(): boolean {
    return this.updateState?.state === 'ready' ||
        this.updateState?.state === 'applying';
  }

  private isApplying_(): boolean {
    return this.updateState?.state === 'applying' || this.applying_;
  }

  private buttonClass_(): string {
    return this.expanded_ ? 'update-btn expanded' : 'update-btn';
  }
}

customElements.define('dao-update-button', DaoUpdateButton);
