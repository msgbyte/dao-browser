// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

export class DaoSidebarSection extends CrLitElement {
  static get is() {
    return 'dao-sidebar-section';
  }

  static override get styles() {
    return css`
      :host {
        display: flex;
        flex-direction: column;
      }

      .section-header {
        font-size: 10px;
        font-weight: 600;
        color: var(--text-muted);
        text-transform: uppercase;
        letter-spacing: 0.5px;
        padding: 4px 14px 4px;
      }

      .section-content {
        display: flex;
        flex-direction: column;
      }
    `;
  }

  static override get properties() {
    return {
      sectionTitle: {type: String},
    };
  }

  declare sectionTitle: string;

  constructor() {
    super();
    this.sectionTitle = '';
  }

  override render() {
    return html`
      ${this.sectionTitle ? html`
        <div class="section-header">${this.sectionTitle}</div>
      ` : ''}
      <div class="section-content">
        <slot></slot>
      </div>
    `;
  }
}

customElements.define('dao-sidebar-section', DaoSidebarSection);
