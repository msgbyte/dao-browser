// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, css, html} from '//resources/lit/v3_0/lit.rollup.js';

import {daoIndexPages} from './dao_index_pages.js';
import {initI18n, t} from './i18n/i18n.js';

export class DaoIndexApp extends CrLitElement {
  static get is() {
    return 'dao-index-app';
  }

  static override get properties() {
    return {
      ready_: {type: Boolean, state: true},
    };
  }

  declare private ready_: boolean;

  constructor() {
    super();
    this.ready_ = false;
  }

  override connectedCallback() {
    super.connectedCallback();
    void this.init_();
  }

  static override get styles() {
    return css`
      :host {
        display: block;
        min-height: 100vh;
        color: rgb(29, 35, 42);
        background:
          linear-gradient(180deg, rgb(246, 248, 251) 0%, rgb(238, 243, 248) 100%);
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      }

      .page {
        box-sizing: border-box;
        width: min(880px, calc(100vw - 32px));
        margin: 0 auto;
        padding: 42px 0 56px;
      }

      header {
        margin-bottom: 22px;
      }

      h1 {
        margin: 0;
        color: rgb(24, 30, 38);
        font-size: 28px;
        font-weight: 760;
        letter-spacing: 0;
        line-height: 1.14;
      }

      .subtitle {
        margin-top: 7px;
        color: rgba(29, 35, 42, 0.58);
        font-size: 13px;
        font-weight: 520;
      }

      .pages {
        display: grid;
        gap: 8px;
      }

      .page-link {
        display: grid;
        grid-template-columns: minmax(0, 1fr) auto;
        align-items: center;
        gap: 18px;
        min-height: 58px;
        padding: 11px 14px;
        border: 1px solid rgba(70, 120, 190, 0.16);
        border-radius: 8px;
        background: rgba(255, 255, 255, 0.72);
        color: inherit;
        text-decoration: none;
      }

      .page-link:hover {
        border-color: rgba(70, 120, 190, 0.38);
        background: rgba(255, 255, 255, 0.94);
      }

      .page-title {
        display: block;
        color: rgb(25, 31, 38);
        font-size: 14px;
        font-weight: 730;
      }

      .page-desc {
        display: block;
        margin-top: 3px;
        color: rgba(29, 35, 42, 0.56);
        font-size: 12px;
        line-height: 1.42;
      }

      .page-url {
        color: rgba(38, 67, 106, 0.72);
        font: 12px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
        white-space: nowrap;
      }

      .loading {
        display: grid;
        min-height: 100vh;
        place-items: center;
        color: rgba(29, 35, 42, 0.48);
        font-size: 13px;
      }

      @media (max-width: 640px) {
        .page {
          width: min(100vw - 24px, 880px);
          padding: 28px 0 42px;
        }

        .page-link {
          grid-template-columns: 1fr;
          gap: 7px;
        }

        .page-url {
          white-space: normal;
          overflow-wrap: anywhere;
        }
      }
    `;
  }

  override render() {
    if (!this.ready_) {
      return html`<div class="loading">${t('index.loading')}</div>`;
    }

    return html`
      <main class="page">
        <header>
          <h1>${t('index.title')}</h1>
          <div class="subtitle">${t('index.subtitle')}</div>
        </header>
        <nav class="pages">
          ${daoIndexPages.map(page => html`
            <a class="page-link" href=${page.url}>
              <span>
                <span class="page-title">${t(page.titleKey)}</span>
                <span class="page-desc">${t(page.descriptionKey)}</span>
              </span>
              <span class="page-url">${page.url}</span>
            </a>
          `)}
        </nav>
      </main>
    `;
  }

  private async init_(): Promise<void> {
    await initI18n();
    this.ready_ = true;
  }
}

customElements.define(DaoIndexApp.is, DaoIndexApp);
