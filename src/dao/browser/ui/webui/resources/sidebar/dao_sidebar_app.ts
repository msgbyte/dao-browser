// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative, addListener} from './sidebar_bridge.js';
import type {SidebarState, TabData} from './sidebar_bridge.js';

import './dao_favorites_view.js';
import './dao_new_tab_button.js';
import './dao_sidebar_section.js';
import './dao_tab_list.js';
import './dao_download_button.js';

export class DaoSidebarApp extends CrLitElement {
  static get is() {
    return 'dao-sidebar-app';
  }

  static override get styles() {
    return css`
      :host {
        display: flex;
        flex-direction: column;
        height: 100vh;
        overflow: hidden;
      }

      .sidebar-content {
        display: flex;
        flex-direction: column;
        flex: 1;
        min-height: 0;
        gap: 8px;
        padding: 4px 0;
      }

      .tab-section {
        flex: 1;
        min-height: 0;
        overflow-y: auto;
        overflow-x: hidden;
      }

      .bottom-section {
        flex-shrink: 0;
      }
    `;
  }

  static override get properties() {
    return {
      pinnedTabs_: {type: Array},
      unpinnedTabs_: {type: Array},
    };
  }

  protected pinnedTabs_: TabData[] = [];
  protected unpinnedTabs_: TabData[] = [];

  override connectedCallback() {
    super.connectedCallback();

    addListener('sidebarStateChanged', (...args: unknown[]) => {
      const state = args[0] as SidebarState;
      this.pinnedTabs_ = state.pinnedTabs;
      this.unpinnedTabs_ = state.unpinnedTabs;
    });

    addListener('tabUpdated', (...args: unknown[]) => {
      const updated = args[0] as TabData;
      // Update in pinned or unpinned list
      if (updated.isPinned) {
        this.pinnedTabs_ = this.pinnedTabs_.map(
            t => t.index === updated.index ? updated : t);
      } else {
        this.unpinnedTabs_ = this.unpinnedTabs_.map(
            t => t.index === updated.index ? updated : t);
      }
    });

    addListener('activeTabChanged', (...args: unknown[]) => {
      const data = args[0] as {activeIndex: number};
      this.pinnedTabs_ = this.pinnedTabs_.map(
          t => ({...t, isActive: t.index === data.activeIndex}));
      this.unpinnedTabs_ = this.unpinnedTabs_.map(
          t => ({...t, isActive: t.index === data.activeIndex}));
    });

    sendNative('getInitialState');
  }

  override render() {
    return html`
      <div class="sidebar-content">
        ${this.pinnedTabs_.length > 0 ? html`
          <dao-favorites-view
            .tabs=${this.pinnedTabs_}>
          </dao-favorites-view>
        ` : ''}

        <div class="tab-section">
          <dao-sidebar-section sectionTitle="Today">
            <dao-new-tab-button></dao-new-tab-button>
            <dao-tab-list
              .tabs=${this.unpinnedTabs_}>
            </dao-tab-list>
          </dao-sidebar-section>
        </div>

        <div class="bottom-section">
          <dao-download-button></dao-download-button>
        </div>
      </div>
    `;
  }
}

customElements.define('dao-sidebar-app', DaoSidebarApp);
