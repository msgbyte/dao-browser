// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative} from './sidebar_bridge.js';
import type {PinnedItemData} from './sidebar_bridge.js';

const PINNED_ITEM_DRAG_MIME_TYPE = 'application/x-dao-pinned-item-id';

export class DaoPinnedTabsGrid extends CrLitElement {
  static get is() {
    return 'dao-pinned-tabs-grid';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
      }

      .grid {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(56px, 1fr));
        gap: 6px;
        padding: 4px 10px;
      }

      .tile {
        min-width: 0;
        height: 56px;
        border: none;
        border-radius: var(--radius-tab, 12px);
        background: transparent;
        color: var(--text-primary);
        cursor: default;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 5px;
        padding: 6px;
        transition: background 0.12s ease, box-shadow 0.12s ease,
            opacity 0.12s ease;
      }

      .tile:hover {
        background: var(--ink-drop-strong, rgba(0, 0, 0, 0.06));
      }

      .tile.active,
      .tile.active:hover {
        background: var(--surface-active);
        box-shadow: 0 0 0 0.5px rgba(70, 120, 190, 0.24),
                    0 1px 3px rgba(0, 0, 0, 0.10),
                    0 1px 2px rgba(0, 0, 0, 0.06);
      }

      .tile.open {
        opacity: 1;
      }

      .tile.dormant {
        opacity: 0.72;
      }

      .favicon {
        width: 18px;
        height: 18px;
        flex-shrink: 0;
        border-radius: 4px;
      }

      .favicon.light-icon {
        filter: invert(1);
      }

      .placeholder {
        width: 18px;
        height: 18px;
        flex-shrink: 0;
        border-radius: 4px;
        background: var(--surface);
      }

      .title {
        max-width: 100%;
        min-width: 0;
        color: var(--text-primary);
        font-size: 11px;
        line-height: 14px;
        overflow: hidden;
        text-align: center;
        text-overflow: ellipsis;
        white-space: nowrap;
      }

      .tile.active .title {
        font-weight: 500;
      }

      @media (prefers-color-scheme: dark) {
        .tile:hover {
          background: var(--ink-drop-strong, rgba(255, 255, 255, 0.10));
        }

        .tile.active,
        .tile.active:hover {
          box-shadow: 0 0 0 0.5px rgba(255, 255, 255, 0.14),
                      0 1px 3px rgba(0, 0, 0, 0.30),
                      0 1px 2px rgba(0, 0, 0, 0.18);
        }
      }
    `;
  }

  static override get properties() {
    return {
      items: {type: Array},
    };
  }

  declare items: PinnedItemData[];
  private draggedItemId_: string = '';
  private tooltipTimer_: number = 0;
  private lastMouseX_: number = 0;
  private lastMouseY_: number = 0;

  constructor() {
    super();
    this.items = [];
  }

  override render() {
    return html`
      <div class="grid">
        ${this.items.map(item => this.renderTile_(item))}
      </div>
    `;
  }

  private renderTile_(item: PinnedItemData) {
    const stateClass = item.isActive ? 'active' : item.isOpen ? 'open' : 'dormant';
    const displayTitle = item.title || item.url;

    return html`
      <button class="tile ${stateClass}"
              aria-label=${displayTitle}
              draggable=${true}
              @click=${() => this.onActivateOrOpen_(item)}
              @contextmenu=${(e: MouseEvent) => this.onContextMenu_(e, item)}
              @dragstart=${(e: DragEvent) => this.onDragStart_(e, item)}
              @dragover=${(e: DragEvent) => this.onDragOver_(e)}
              @drop=${(e: DragEvent) => this.onDrop_(e, item)}
              @dragend=${() => this.onDragEnd_()}
              @mouseenter=${(e: MouseEvent) => this.onShowTooltip_(e, item)}
              @mousemove=${(e: MouseEvent) => this.onTrackMouse_(e, item)}
              @mouseleave=${() => this.onHideTooltip_()}>
        ${item.faviconUrl
            ? html`<img class="favicon ${item.isFaviconLight ? 'light-icon' : ''}"
                        src=${item.faviconUrl} alt="">`
            : html`<div class="placeholder"></div>`}
        <div class="title">${displayTitle}</div>
      </button>
    `;
  }

  private onActivateOrOpen_(item: PinnedItemData) {
    sendNative('activateOrOpenPinnedItem', item.id);
  }

  private onDragStart_(e: DragEvent, item: PinnedItemData) {
    this.draggedItemId_ = item.id;
    if (e.dataTransfer) {
      e.dataTransfer.setData(PINNED_ITEM_DRAG_MIME_TYPE, item.id);
      e.dataTransfer.setData('text/plain', item.id);
      e.dataTransfer.effectAllowed = 'move';
    }
  }

  private onDragOver_(e: DragEvent) {
    if (!this.hasInternalDrag_(e)) {
      return;
    }

    e.preventDefault();
    if (e.dataTransfer) {
      e.dataTransfer.dropEffect = 'move';
    }
  }

  private onDrop_(e: DragEvent, item: PinnedItemData) {
    const draggedId = this.getDraggedItemId_(e);
    if (!draggedId) {
      return;
    }

    e.preventDefault();
    e.stopPropagation();

    const toIndex = this.items.findIndex(pinnedItem => pinnedItem.id === item.id);
    const draggedIndex =
        this.items.findIndex(pinnedItem => pinnedItem.id === draggedId);
    this.draggedItemId_ = '';

    if (!draggedId || draggedId === item.id ||
        draggedIndex < 0 || toIndex < 0) {
      return;
    }

    sendNative('movePinnedItem', draggedId, toIndex);
  }

  private hasInternalDrag_(e: DragEvent): boolean {
    if (this.draggedItemId_) {
      return true;
    }
    return this.getDataTransferTypes_(e).includes(PINNED_ITEM_DRAG_MIME_TYPE);
  }

  private getDraggedItemId_(e: DragEvent): string {
    const markedId = e.dataTransfer?.getData(PINNED_ITEM_DRAG_MIME_TYPE) || '';
    return markedId || this.draggedItemId_;
  }

  private getDataTransferTypes_(e: DragEvent): string[] {
    if (!e.dataTransfer) {
      return [];
    }
    return Array.from(e.dataTransfer.types || []);
  }

  private onDragEnd_() {
    this.draggedItemId_ = '';
  }

  private onContextMenu_(e: MouseEvent, item: PinnedItemData) {
    e.preventDefault();
    e.stopPropagation();
    sendNative('showPinnedItemContextMenu', item.id, e.screenX, e.screenY);
  }

  private onTrackMouse_(e: MouseEvent, item: PinnedItemData) {
    this.lastMouseX_ = e.screenX;
    this.lastMouseY_ = e.screenY;
    this.scheduleTooltip_(item);
  }

  private onShowTooltip_(e: MouseEvent, item: PinnedItemData) {
    this.lastMouseX_ = e.screenX;
    this.lastMouseY_ = e.screenY;
    this.scheduleTooltip_(item);
  }

  private scheduleTooltip_(item: PinnedItemData) {
    window.clearTimeout(this.tooltipTimer_);
    this.tooltipTimer_ = window.setTimeout(() => {
      const title = item.title || item.url || 'New Tab';
      sendNative(
          'showTabTooltip', this.lastMouseX_ + 4, this.lastMouseY_ + 4, title);
    }, 1500);
  }

  private onHideTooltip_() {
    window.clearTimeout(this.tooltipTimer_);
    sendNative('hideTabTooltip');
  }
}

if (!customElements.get('dao-pinned-tabs-grid')) {
  customElements.define('dao-pinned-tabs-grid', DaoPinnedTabsGrid);
}
