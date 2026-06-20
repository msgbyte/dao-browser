// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {
  animateSurvivingFlipElements,
  snapshotFlipElements,
  type FlipMotionSnapshot,
} from './dao_flip_motion.js';
import {
  clearActivePinnedItemDragId,
  getActivePinnedItemDragId,
  PINNED_ITEM_DRAG_MIME_TYPE,
  SIDEBAR_POINTER_EXITED_EVENT,
  setActivePinnedItemDragId,
  TAB_DRAG_MIME_TYPE,
  TAB_DRAG_PREFIX,
  isPointOutsideViewport,
  parseTabDragData,
  sendNative,
} from './sidebar_bridge.js';
import type {PinnedItemData} from './sidebar_bridge.js';

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
        grid-template-columns: repeat(auto-fill, minmax(56px, 1fr));
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

      .drag-placeholder {
        min-width: 0;
        height: 56px;
        border: 1px dashed rgba(70, 120, 190, 0.48);
        border-radius: var(--radius-tab, 12px);
        background: rgba(70, 120, 190, 0.12);
        box-sizing: border-box;
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

        .drag-placeholder {
          border-color: rgba(130, 170, 230, 0.54);
          background: rgba(130, 170, 230, 0.16);
        }
      }

      :host([hover-suppressed]) .tile:hover {
        background: transparent;
      }

      :host([hover-suppressed]) .tile.active:hover {
        background: var(--surface-active);
      }
    `;
  }

  static override get properties() {
    return {
      items: {type: Array},
      sessionId: {type: Number},
      hoverSuppressed_: {
        type: Boolean,
        reflect: true,
        attribute: 'hover-suppressed',
      },
    };
  }

  declare items: PinnedItemData[];
  declare sessionId: number;
  declare protected hoverSuppressed_: boolean;
  private dragPlaceholderIndex_: number = -1;
  private tabDragPlaceholderVisible_: boolean = false;
  private tabDragActivated_: boolean = false;
  private tooltipTimer_: number = 0;
  private tooltipScheduled_: boolean = false;
  private tooltipVisible_: boolean = false;
  private lastMouseX_: number = 0;
  private lastMouseY_: number = 0;
  private previousFlipSnapshot_: FlipMotionSnapshot | null = null;
  private boundSidebarPointerExited_ = () => this.onSidebarPointerExited_();

  constructor() {
    super();
    this.items = [];
    this.sessionId = 0;
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

  override willUpdate(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('items')) {
      this.previousFlipSnapshot_ = this.snapshotTiles_();
    }
  }

  override updated(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('items')) {
      this.animateCloseMotion_();
    }
  }

  override render() {
    return html`
      <div class="grid"
           @dragover=${(e: DragEvent) => this.onGridDragOver_(e)}
           @dragleave=${(e: DragEvent) => this.onGridDragLeave_(e)}
           @drop=${(e: DragEvent) => this.onGridDrop_(e)}>
        ${this.renderTiles_()}
      </div>
    `;
  }

  private renderTiles_() {
    const draggedId = getActivePinnedItemDragId();
    if (this.tabDragPlaceholderVisible_) {
      return this.renderItemsWithPlaceholder_(this.items, this.getPinnedDropIndex_());
    }

    if (!draggedId || this.dragPlaceholderIndex_ < 0) {
      return this.items.map(item => this.renderTile_(item));
    }

    const visibleItems = this.items.filter(item => item.id !== draggedId);
    return this.renderItemsWithPlaceholder_(
        visibleItems, Math.min(this.dragPlaceholderIndex_, visibleItems.length));
  }

  private renderItemsWithPlaceholder_(
      items: PinnedItemData[], placeholderIndex: number) {
    const boundedIndex = Math.min(Math.max(0, placeholderIndex), items.length);
    const fragments = [];
    for (let i = 0; i <= items.length; i++) {
      if (i === boundedIndex) {
        fragments.push(this.renderDragPlaceholder_());
      }
      if (i < items.length) {
        fragments.push(this.renderTile_(items[i]!));
      }
    }
    return fragments;
  }

  private renderDragPlaceholder_() {
    return html`<div class="drag-placeholder" aria-hidden="true"></div>`;
  }

  private renderTile_(item: PinnedItemData) {
    const stateClass = item.isActive ? 'active' : item.isOpen ? 'open' : 'dormant';
    const displayTitle = item.title || item.url;

    return html`
      <button class="tile ${stateClass}"
              data-pinned-item-id=${item.id}
              aria-label=${displayTitle}
              draggable=${true}
              @click=${() => this.onActivateOrOpen_(item)}
              @contextmenu=${(e: MouseEvent) => this.onContextMenu_(e, item)}
              @dragstart=${(e: DragEvent) => this.onDragStart_(e, item)}
              @dragover=${(e: DragEvent) => this.onDragOver_(e, item)}
              @drop=${(e: DragEvent) => this.onDrop_(e, item)}
              @dragend=${() => this.onDragEnd_()}
              @mouseenter=${(e: MouseEvent) => this.onShowTooltip_(e, item)}
              @mousemove=${(e: MouseEvent) => this.onTrackMouse_(e, item)}
              @mouseleave=${() => this.onHideTooltip_()}>
        ${item.faviconUrl
            ? html`<img class="favicon ${item.isFaviconLight ? 'light-icon' : ''}"
                        src=${item.faviconUrl} alt="" draggable="false">`
            : html`<div class="placeholder"></div>`}
        <div class="title">${displayTitle}</div>
      </button>
    `;
  }

  private snapshotTiles_(): FlipMotionSnapshot {
    return snapshotFlipElements(
        this.shadowRoot, '.tile',
        element => element.dataset['pinnedItemId'] || '');
  }

  private animateCloseMotion_() {
    animateSurvivingFlipElements(
        this.previousFlipSnapshot_, this.shadowRoot, '.tile',
        element => element.dataset['pinnedItemId'] || '',
        {
          skip: this.dragPlaceholderIndex_ >= 0 ||
              this.tabDragPlaceholderVisible_,
        });
    this.previousFlipSnapshot_ = null;
  }

  private onActivateOrOpen_(item: PinnedItemData) {
    sendNative('activateOrOpenPinnedItem', item.id);
  }

  private onDragStart_(e: DragEvent, item: PinnedItemData) {
    this.clearTooltip_(this.tooltipVisible_);
    setActivePinnedItemDragId(item.id);
    if (e.dataTransfer) {
      e.dataTransfer.setData(PINNED_ITEM_DRAG_MIME_TYPE, item.id);
      if (item.isOpen && item.openTabIndex >= 0) {
        const tabPayload =
            `${TAB_DRAG_PREFIX}${this.sessionId}:${item.openTabIndex}`;
        e.dataTransfer.setData(TAB_DRAG_MIME_TYPE, tabPayload);
        e.dataTransfer.setData('text/plain', tabPayload);
        this.activateNativeTabDrag_();
      } else {
        e.dataTransfer.setData('text/plain', item.id);
      }
      e.dataTransfer.effectAllowed = 'move';
    }
  }

  private onDragOver_(e: DragEvent, item: PinnedItemData) {
    const isInternalDrag = this.hasInternalDrag_(e);
    const isTabDrag = this.hasTabDrag_(e);
    if (!isInternalDrag && !isTabDrag) {
      return;
    }

    e.preventDefault();
    e.stopPropagation();
    this.setDragPlaceholderIndex_(
        this.getTileDropIndex_(e, item, isInternalDrag));
    if (isInternalDrag) {
      this.clearTabDragPlaceholder_();
    } else if (isTabDrag) {
      this.showTabDragPlaceholder_();
    }
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

    const toIndex = this.dragPlaceholderIndex_ >= 0 ?
        this.getPinnedDropIndex_() :
        this.getTileDropIndex_(e, item, true);
    this.movePinnedItem_(draggedId, toIndex);
  }

  private onGridDragOver_(e: DragEvent) {
    if (this.hasInternalDrag_(e)) {
      e.preventDefault();
      if (this.isDragPlaceholderTarget_(e)) {
        if (e.dataTransfer) {
          e.dataTransfer.dropEffect = 'move';
        }
        return;
      }
      if (this.dragPlaceholderIndex_ < 0) {
        this.setDragPlaceholderIndex_(this.items.length - 1);
      }
      if (e.dataTransfer) {
        e.dataTransfer.dropEffect = 'move';
      }
      return;
    }

    if (!this.hasTabDrag_(e)) {
      return;
    }

    e.preventDefault();
    if (this.shouldKeepCurrentTabDropIndex_(e)) {
      if (e.dataTransfer) {
        e.dataTransfer.dropEffect = 'move';
      }
      return;
    }
    this.setDragPlaceholderIndex_(this.items.length);
    if (e.dataTransfer) {
      e.dataTransfer.dropEffect = 'move';
    }
    this.showTabDragPlaceholder_();
  }

  private onGridDragLeave_(e: DragEvent) {
    const relatedTarget = e.relatedTarget as Node | null;
    if (relatedTarget && this.shadowRoot?.contains(relatedTarget)) {
      return;
    }
    this.clearTabDragPlaceholder_();

    if (this.hasTabDrag_(e) &&
        isPointOutsideViewport(
            e.clientX, e.clientY, window.innerWidth, window.innerHeight)) {
      this.activateNativeTabDrag_();
    }
  }

  private activateNativeTabDrag_() {
    if (this.tabDragActivated_) {
      return;
    }
    this.tabDragActivated_ = true;
    sendNative('tabDragActive', true);
  }

  private onGridDrop_(e: DragEvent) {
    const draggedId = this.getDraggedItemId_(e);
    if (draggedId) {
      e.preventDefault();
      e.stopPropagation();
      this.movePinnedItem_(draggedId, this.getPinnedDropIndex_());
      return;
    }

    const tabIndex = this.getSameWindowDraggedTabIndex_(e);
    if (tabIndex === null) {
      return;
    }

    e.preventDefault();
    e.stopPropagation();
    sendNative('pinTab', tabIndex, this.getPinnedDropIndex_());
    this.clearTabDragPlaceholder_();
  }

  private movePinnedItem_(draggedId: string, toIndex: number) {
    const draggedIndex =
        this.items.findIndex(pinnedItem => pinnedItem.id === draggedId);
    this.clearPinnedDrag_();

    if (!draggedId || draggedIndex === toIndex ||
        draggedIndex < 0 || toIndex < 0) {
      return;
    }

    sendNative('movePinnedItem', draggedId, toIndex);
  }

  private getPinnedDropIndex_(): number {
    return Math.min(
        Math.max(0, this.dragPlaceholderIndex_), this.items.length);
  }

  private getTileDropIndex_(
      e: DragEvent, item: PinnedItemData, adjustForDraggedItem: boolean): number {
    const targetIndex =
        this.items.findIndex(pinnedItem => pinnedItem.id === item.id);
    if (targetIndex < 0) {
      return -1;
    }

    let dropIndex = targetIndex;
    let usedPointerPosition = false;
    const target = e.currentTarget;
    if (target instanceof HTMLElement && e.clientX !== 0) {
      const rect = target.getBoundingClientRect();
      if (rect.width > 0 || rect.height > 0) {
        usedPointerPosition = true;
        const afterThresholdX = rect.left + rect.width * 2 / 3;
        if (e.clientX > afterThresholdX) {
          dropIndex++;
        }
      }
    }

    if (!adjustForDraggedItem) {
      return dropIndex;
    }

    if (!usedPointerPosition) {
      return dropIndex;
    }

    const draggedIndex = this.items.findIndex(
        pinnedItem => pinnedItem.id === getActivePinnedItemDragId());
    if (draggedIndex >= 0 && draggedIndex < dropIndex) {
      return dropIndex - 1;
    }
    return dropIndex;
  }

  private isDragPlaceholderTarget_(e: DragEvent): boolean {
    return e.target instanceof HTMLElement &&
        e.target.classList.contains('drag-placeholder');
  }

  private shouldKeepCurrentTabDropIndex_(e: DragEvent): boolean {
    if (this.isDragPlaceholderTarget_(e)) {
      return true;
    }
    if (!this.tabDragPlaceholderVisible_ || this.dragPlaceholderIndex_ < 0) {
      return false;
    }
    return !this.isAfterLastRenderedTile_(e);
  }

  private isAfterLastRenderedTile_(e: DragEvent): boolean {
    const elements =
        this.shadowRoot?.querySelectorAll('.tile, .drag-placeholder') || [];
    const last = elements[elements.length - 1];
    if (!(last instanceof HTMLElement)) {
      return false;
    }

    const rect = last.getBoundingClientRect();
    if (rect.width === 0 && rect.height === 0) {
      return false;
    }

    const clientX = e.clientX;
    const clientY = e.clientY;
    if (clientX === 0 && clientY === 0) {
      return false;
    }

    return clientY > rect.bottom ||
        (clientY >= rect.top && clientY <= rect.bottom &&
         clientX > rect.right);
  }

  private hasInternalDrag_(e: DragEvent): boolean {
    if (getActivePinnedItemDragId()) {
      return true;
    }
    return this.getDataTransferTypes_(e).includes(PINNED_ITEM_DRAG_MIME_TYPE);
  }

  private getDraggedItemId_(e: DragEvent): string {
    const markedId = e.dataTransfer?.getData(PINNED_ITEM_DRAG_MIME_TYPE) || '';
    return markedId || getActivePinnedItemDragId();
  }

  private getSameWindowDraggedTabIndex_(e: DragEvent): number|null {
    const dragData = e.dataTransfer?.getData(TAB_DRAG_MIME_TYPE) ||
        e.dataTransfer?.getData('text/plain') || '';
    const parsed = parseTabDragData(dragData);
    if (!parsed || parsed.sessionId !== this.sessionId) {
      return null;
    }
    return parsed.tabIndex;
  }

  private hasTabDrag_(e: DragEvent): boolean {
    return this.getDataTransferTypes_(e).includes(TAB_DRAG_MIME_TYPE);
  }

  private getDataTransferTypes_(e: DragEvent): string[] {
    if (!e.dataTransfer) {
      return [];
    }
    return Array.from(e.dataTransfer.types || []);
  }

  private onDragEnd_() {
    this.clearPinnedDrag_();
    if (this.tabDragActivated_) {
      this.tabDragActivated_ = false;
      sendNative('tabDragActive', false);
    }
  }

  private setDragPlaceholderIndex_(index: number) {
    const nextIndex = Math.max(0, index);
    if (this.dragPlaceholderIndex_ === nextIndex) {
      return;
    }
    this.dragPlaceholderIndex_ = nextIndex;
    this.requestUpdate();
  }

  private clearPinnedDrag_() {
    clearActivePinnedItemDragId();
    if (this.dragPlaceholderIndex_ === -1) {
      return;
    }
    this.dragPlaceholderIndex_ = -1;
    this.requestUpdate();
  }

  private showTabDragPlaceholder_() {
    if (this.tabDragPlaceholderVisible_) {
      return;
    }
    this.tabDragPlaceholderVisible_ = true;
    this.requestUpdate();
  }

  private clearTabDragPlaceholder_() {
    if (!this.tabDragPlaceholderVisible_) {
      return;
    }
    this.tabDragPlaceholderVisible_ = false;
    this.requestUpdate();
  }

  private onContextMenu_(e: MouseEvent, item: PinnedItemData) {
    e.preventDefault();
    e.stopPropagation();
    sendNative('showPinnedItemContextMenu', item.id, e.screenX, e.screenY);
  }

  private onTrackMouse_(e: MouseEvent, item: PinnedItemData) {
    this.setHoverSuppressed_(false);
    this.lastMouseX_ = e.screenX;
    this.lastMouseY_ = e.screenY;
    this.scheduleTooltip_(item);
  }

  private onShowTooltip_(e: MouseEvent, item: PinnedItemData) {
    this.setHoverSuppressed_(false);
    this.lastMouseX_ = e.screenX;
    this.lastMouseY_ = e.screenY;
    this.scheduleTooltip_(item);
  }

  private scheduleTooltip_(item: PinnedItemData) {
    this.clearTooltip_(false);
    this.tooltipScheduled_ = true;
    this.tooltipTimer_ = window.setTimeout(() => {
      this.tooltipTimer_ = 0;
      this.tooltipScheduled_ = false;
      this.tooltipVisible_ = true;
      const title = item.title || item.url || 'New Tab';
      sendNative(
          'showTabTooltip', this.lastMouseX_ + 4, this.lastMouseY_ + 4, title);
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
}

if (!customElements.get('dao-pinned-tabs-grid')) {
  customElements.define('dao-pinned-tabs-grid', DaoPinnedTabsGrid);
}
