// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative} from './sidebar_bridge.js';
import type {TabData} from './sidebar_bridge.js';
import './dao_tab_item.js';

export class DaoTabList extends CrLitElement {
  static get is() {
    return 'dao-tab-list';
  }

  static override get styles() {
    return css`
      :host {
        display: flex;
        flex-direction: column;
        position: relative;
      }

      .drop-indicator {
        position: absolute;
        left: 12px;
        right: 12px;
        height: 2px;
        background: var(--accent);
        border-radius: 1px;
        pointer-events: none;
        display: none;
        z-index: 10;
      }

      .drop-indicator::before,
      .drop-indicator::after {
        content: '';
        position: absolute;
        top: -2px;
        width: 6px;
        height: 6px;
        border-radius: 50%;
        background: var(--accent);
      }

      .drop-indicator::before { left: -3px; }
      .drop-indicator::after { right: -3px; }

      :host(.drag-over) .drop-indicator {
        display: block;
      }

      .split-group {
        position: relative;
        margin-left: 10px;
        padding: 2px 0;
      }

      .split-group::before {
        content: '';
        position: absolute;
        left: 0;
        top: 4px;
        bottom: 4px;
        width: 2px;
        background: var(--accent);
        border-radius: 1px;
        opacity: 0.4;
      }
    `;
  }

  static override get properties() {
    return {
      tabs: {type: Array},
    };
  }

  tabs: TabData[] = [];

  private dropIndicatorY_: number = 0;
  private dropInsertIndex_: number = -1;
  private tabDragActivated_: boolean = false;

  override connectedCallback() {
    super.connectedCallback();
    this.addEventListener('dragover', this.onDragOver_.bind(this));
    this.addEventListener('dragleave', this.onDragLeave_.bind(this));
    this.addEventListener('drop', this.onDrop_.bind(this));
    this.addEventListener('dragend', this.onDragEnd_.bind(this));
  }

  override render() {
    // Show tabs in reverse order (newest at top) to match C++ behavior
    const reversed = [...this.tabs].reverse();

    // Group consecutive split tabs into wrapped containers.
    const fragments: unknown[] = [];
    let splitRun: TabData[] = [];

    const flushSplitRun = () => {
      if (splitRun.length > 0) {
        const items = splitRun.map(tab => html`
          <dao-tab-item
            .tabData=${tab}
            ?active=${tab.isActive}>
          </dao-tab-item>
        `);
        fragments.push(html`<div class="split-group">${items}</div>`);
        splitRun = [];
      }
    };

    for (const tab of reversed) {
      if (tab.isInSplit) {
        splitRun.push(tab);
      } else {
        flushSplitRun();
        fragments.push(html`
          <dao-tab-item
            .tabData=${tab}
            ?active=${tab.isActive}>
          </dao-tab-item>
        `);
      }
    }
    flushSplitRun();

    return html`
      <div class="drop-indicator"
           style="top: ${this.dropIndicatorY_}px"></div>
      ${fragments}
    `;
  }

  private isInternalReorder_(e: DragEvent): boolean {
    if (!e.dataTransfer) return false;
    return e.dataTransfer.types.includes('text/plain');
  }

  private isExternalFileDrop_(e: DragEvent): boolean {
    if (!e.dataTransfer) return false;
    return e.dataTransfer.types.includes('Files');
  }

  private onDragOver_(e: DragEvent) {
    const isInternal = this.isInternalReorder_(e);
    const isExternal = this.isExternalFileDrop_(e);

    if (!isInternal && !isExternal) return;

    e.preventDefault();
    e.stopPropagation();
    if (e.dataTransfer) {
      e.dataTransfer.dropEffect = isInternal ? 'move' : 'copy';
    }

    this.classList.add('drag-over');

    // Compute indicator position and insertion index.
    // Tabs are rendered in reverse model order (highest index = top).
    const items = this.shadowRoot!.querySelectorAll('dao-tab-item');
    const y = e.offsetY;
    let indicatorY = 0;
    let insertIndex = -1;  // -1 = append at end

    if (items.length === 0) {
      indicatorY = 0;
      insertIndex = -1;
    } else {
      let found = false;
      for (const item of items) {
        const el = item as HTMLElement;
        const midY = el.offsetTop + el.offsetHeight / 2;
        if (y < midY) {
          indicatorY = el.offsetTop;
          const tabData = (item as unknown as {tabData: TabData}).tabData;
          insertIndex = tabData.index + 1;
          found = true;
          break;
        }
      }
      if (!found) {
        const lastEl = items[items.length - 1] as HTMLElement;
        indicatorY = lastEl.offsetTop + lastEl.offsetHeight;
        const lastTab =
            (items[items.length - 1] as unknown as {tabData: TabData}).tabData;
        insertIndex = lastTab.index;
      }

      // Prevent inserting between split-group tabs.
      // If the insertion point lands inside a split group, snap to the
      // nearest boundary (above or below the whole group).
      const splitGroups =
          this.shadowRoot!.querySelectorAll('.split-group');
      for (const splitGroup of splitGroups) {
        const group = splitGroup as HTMLElement;
        const groupTop = group.offsetTop;
        const groupBottom = groupTop + group.offsetHeight;
        const splitItems = group.querySelectorAll('dao-tab-item');
        if (splitItems.length > 0 && y >= groupTop && y <= groupBottom) {
          const midGroup = groupTop + group.offsetHeight / 2;
          if (y < midGroup) {
            indicatorY = groupTop;
            const firstTab =
                (splitItems[0] as unknown as {tabData: TabData}).tabData;
            insertIndex = firstTab.index + 1;
          } else {
            indicatorY = groupBottom;
            const lastTab =
                (splitItems[splitItems.length - 1] as unknown as
                     {tabData: TabData})
                    .tabData;
            insertIndex = lastTab.index;
          }
          break;
        }
      }
    }

    this.dropIndicatorY_ = indicatorY;
    const indicator =
        this.shadowRoot!.querySelector('.drop-indicator') as HTMLElement;
    if (indicator) {
      indicator.style.top = `${indicatorY}px`;
    }

    if (isExternal) {
      // Tell C++ the desired insert position for file drops
      sendNative('setDropInsertIndex', insertIndex);
    }

    this.dropInsertIndex_ = insertIndex;
  }

  private onDragLeave_(e: DragEvent) {
    // Only clear if actually leaving the component
    const related = e.relatedTarget as Node | null;
    if (related && this.contains(related)) return;
    this.classList.remove('drag-over');
    this.dropInsertIndex_ = -1;
    sendNative('setDropInsertIndex', -1);

    // Activate native event blocking so DaoSplitView can receive the
    // drag that just left the sidebar WebView.  This must happen AFTER
    // the drag leaves, not at dragstart, because the interceptor covers
    // the entire window and would kill HTML5 drag inside the WebView.
    if (!this.tabDragActivated_) {
      this.tabDragActivated_ = true;
      sendNative('tabDragActive', true);
    }
  }

  private onDragEnd_(_e: DragEvent) {
    // Defensive cleanup: ensure drag-over state is always cleared
    // when any drag ends, preventing the sidebar from getting stuck.
    this.classList.remove('drag-over');
    this.dropInsertIndex_ = -1;
    if (this.tabDragActivated_) {
      this.tabDragActivated_ = false;
      sendNative('tabDragActive', false);
    }
  }

  private onDrop_(e: DragEvent) {
    this.classList.remove('drag-over');

    // Handle internal tab reorder
    if (e.dataTransfer) {
      const data = e.dataTransfer.getData('text/plain');
      if (data.startsWith('dao-tab-drag:')) {
        e.preventDefault();
        e.stopPropagation();
        const fromIndex = parseInt(data.substring('dao-tab-drag:'.length), 10);
        if (!isNaN(fromIndex) && this.dropInsertIndex_ >= 0) {
          // Adjust target: if moving down, the target shifts after removal
          let toIndex = this.dropInsertIndex_;
          if (fromIndex < toIndex) {
            toIndex--;
          }
          if (fromIndex !== toIndex) {
            sendNative('moveTab', fromIndex, toIndex);
          }
        }
        this.dropInsertIndex_ = -1;
        return;
      }
    }

    // External file drops — don't prevent default, let the renderer handle
    // so OpenURLFromTab can intercept the navigation.
    this.dropInsertIndex_ = -1;
  }
}

customElements.define('dao-tab-list', DaoTabList);
