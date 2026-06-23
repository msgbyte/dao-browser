// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {
  clearActivePinnedItemDragId,
  sendNative,
  getActivePinnedItemDragId,
  TAB_DRAG_PREFIX,
  FOLDER_MIME_TYPE,
  PINNED_ITEM_DRAG_MIME_TYPE,
  isPointOutsideViewport,
} from './sidebar_bridge.js';
import type {TabData, FolderData, FolderAction} from './sidebar_bridge.js';
import type {FolderModel} from './dao_folder_model.js';
import {
  animateSurvivingFlipElements,
  snapshotFlipElements,
  type FlipMotionSnapshot,
} from './dao_flip_motion.js';
import './dao_tab_item.js';
import './dao_folder_item.js';

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
        gap: 2px;
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
        padding: 2px 0 2px 6px;  /* 6px = 2px stroke + 4px gap */
        display: flex;
        flex-direction: column;
        gap: 2px;
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
      sessionId: {type: Number},
      folderModel: {type: Object},
      folderModelVersion: {type: Number},
      autoScrollTabId: {type: String},
      autoScrollToken: {type: Number},
    };
  }

  // `declare` + constructor assignment so TS class-field emit does not
  // override Lit's reactive accessors. See dao_sidebar_app.ts for the full
  // explanation. Without this, parent `.tabs=${...}` bindings update the
  // child's plain property but do not trigger a re-render.
  declare tabs: TabData[];
  declare sessionId: number;
  declare folderModel: FolderModel | null;
  declare folderModelVersion: number;
  declare autoScrollTabId: string;
  declare autoScrollToken: number;

  private dropIndicatorY_: number = 0;
  private dropInsertIndex_: number = -1;
  private dropModelIndex_: number = -1;
  private tabDragActivated_: boolean = false;
  private draggedTabIndex_: number = -1;
  private previousFlipSnapshot_: FlipMotionSnapshot | null = null;
  private previousFolderChildCounts_: Map<string, number> | null = null;

  constructor() {
    super();
    this.tabs = [];
    this.sessionId = 0;
    this.folderModel = null;
    this.folderModelVersion = 0;
    this.autoScrollTabId = '';
    this.autoScrollToken = 0;
  }

  override connectedCallback() {
    super.connectedCallback();
    this.addEventListener('dragstart', this.onDragStart_.bind(this));
    this.addEventListener('dragover', this.onDragOver_.bind(this));
    this.addEventListener('dragleave', this.onDragLeave_.bind(this));
    this.addEventListener('drop', this.onDrop_.bind(this));
    this.addEventListener('dragend', this.onDragEnd_.bind(this));
    this.addEventListener('tab-context-menu',
        ((e: CustomEvent) => this.onTabContextMenu_(e)) as EventListener);
    this.addEventListener('folder-context-menu',
        ((e: CustomEvent) => this.onFolderContextMenu_(e)) as EventListener);

    // Intercept folder-action events that involve drops to clean up
    // drag state (folder's stopPropagation prevents our onDrop_).
    // The event itself continues to bubble to DaoSidebarApp.
    this.addEventListener('folder-action', ((e: CustomEvent) => {
      const action = e.detail?.action;
      if (action === 'tabDrop' || action === 'childReorder') {
        this.classList.remove('drag-over');
        this.dropInsertIndex_ = -1;
        this.dropModelIndex_ = -1;
      }
    }) as EventListener);
  }

  override render() {
    // If a folder model is available and has data, render from the
    // model's ordered items tree. Otherwise, fall back to the flat
    // tab array (original behavior).
    if (this.folderModel && this.folderModel.hasData()) {
      return this.renderFromModel_();
    }
    return this.renderFlat_();
  }

  override willUpdate(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('tabs') ||
        changedProperties.has('folderModelVersion')) {
      this.previousFlipSnapshot_ = this.snapshotSurfaceItems_();
      this.previousFolderChildCounts_ = this.snapshotFolderChildCounts_();
    }
  }

  override updated(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('tabs') ||
        changedProperties.has('folderModelVersion')) {
      this.animateCloseMotion_();
    }
  }

  /**
   * Render from the folder model's items tree.
   * Matches stored tab refs to actual TabData by URL for each item.
   */
  private renderFromModel_() {
    const items = this.folderModel!.getOrderedItems();

    const remaining = [...this.tabs];

    const consume = (
        ref: {tabId?: string; url: string; title: string}): TabData | null => {
      let idx = ref.tabId ?
          remaining.findIndex(tab => tab.tabId === ref.tabId) : -1;
      if (idx === -1) {
        idx = remaining.findIndex(
            tab => tab.url === ref.url && tab.title === ref.title);
      }
      if (idx === -1) {
        idx = remaining.findIndex(tab => tab.url === ref.url);
      }
      if (idx === -1) return null;
      return remaining.splice(idx, 1)[0]!;
    };

    const fragments: unknown[] = [];
    let splitRun: TabData[] = [];

    const flushSplitRun = () => {
      if (splitRun.length > 0) {
        const items = splitRun.map(tab => html`
          <dao-tab-item
            data-tab-id=${this.getTabIdentity_(tab)}
            .tabData=${tab}
            .sessionId=${this.sessionId}
            .autoScrollToken=${this.getAutoScrollTokenForTab_(tab)}
            ?active=${tab.isActive}>
          </dao-tab-item>
        `);
        fragments.push(html`<div class="split-group">${items}</div>`);
        splitRun = [];
      }
    };

    const pushTab = (tab: TabData) => {
      if (tab.isInSplit) {
        splitRun.push(tab);
      } else {
        flushSplitRun();
        fragments.push(html`
          <dao-tab-item
            data-tab-id=${this.getTabIdentity_(tab)}
            .tabData=${tab}
            .sessionId=${this.sessionId}
            .autoScrollToken=${this.getAutoScrollTokenForTab_(tab)}
            ?active=${tab.isActive}>
          </dao-tab-item>
        `);
      }
    };

    for (const item of items) {
      if (item.type === 'tab') {
        const matched = consume(item);
        if (matched) {
          pushTab(matched);
        }
      } else if (item.type === 'folder') {
        // Folder breaks any active split run — flush first.
        flushSplitRun();
        const folder = item as FolderData;
        // Match folder children to actual tabs.
        const matchedChildren: TabData[] = [];
        for (const child of folder.children) {
          const matched = consume(child);
          if (matched) {
            matchedChildren.push(matched);
          }
        }

        fragments.push(html`
          <dao-folder-item
            data-folder-id=${folder.id}
            data-folder-child-count=${matchedChildren.length}
            .folder=${folder}
            .matchedTabs=${matchedChildren}
            .sessionId=${this.sessionId}
            .autoScrollTabId=${this.autoScrollTabId}
            .autoScrollToken=${this.autoScrollToken}>
          </dao-folder-item>
        `);
      }
    }
    flushSplitRun();

    // Any remaining unmatched tabs from the pool — render as loose tabs,
    // still honoring split grouping.
    for (const tab of remaining) {
      pushTab(tab);
    }
    flushSplitRun();

    return html`
      <div class="drop-indicator"
           style="top: ${this.dropIndicatorY_}px"></div>
      ${fragments}
    `;
  }

  /**
   * Original flat rendering — used when no folder model data exists.
   */
  private renderFlat_() {
    const tabs = [...this.tabs];

    // Group consecutive split tabs into wrapped containers.
    const fragments: unknown[] = [];
    let splitRun: TabData[] = [];

    const flushSplitRun = () => {
      if (splitRun.length > 0) {
        const items = splitRun.map(tab => html`
          <dao-tab-item
            data-tab-id=${this.getTabIdentity_(tab)}
            .tabData=${tab}
            .sessionId=${this.sessionId}
            .autoScrollToken=${this.getAutoScrollTokenForTab_(tab)}
            ?active=${tab.isActive}>
          </dao-tab-item>
        `);
        fragments.push(html`<div class="split-group">${items}</div>`);
        splitRun = [];
      }
    };

    for (const tab of tabs) {
      if (tab.isInSplit) {
        splitRun.push(tab);
      } else {
        flushSplitRun();
        fragments.push(html`
          <dao-tab-item
            data-tab-id=${this.getTabIdentity_(tab)}
            .tabData=${tab}
            .sessionId=${this.sessionId}
            .autoScrollToken=${this.getAutoScrollTokenForTab_(tab)}
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

  private getAutoScrollTokenForTab_(tab: TabData): number {
    return tab.tabId === this.autoScrollTabId ? this.autoScrollToken : 0;
  }

  private snapshotSurfaceItems_(): FlipMotionSnapshot {
    return snapshotFlipElements(
        this.shadowRoot, 'dao-tab-item, dao-folder-item',
        element => this.getSurfaceItemIdentity_(element));
  }

  private animateCloseMotion_() {
    const force = this.didAnyFolderShrink_();
    animateSurvivingFlipElements(
        this.previousFlipSnapshot_, this.shadowRoot,
        'dao-tab-item, dao-folder-item',
        element => this.getSurfaceItemIdentity_(element),
        {force, skip: this.tabDragActivated_});
    this.previousFlipSnapshot_ = null;
    this.previousFolderChildCounts_ = null;
  }

  private getTabIdentity_(tab: TabData): string {
    return tab.tabId || `${tab.index}:${tab.url}:${tab.title}`;
  }

  private getSurfaceItemIdentity_(element: HTMLElement): string {
    const tabId = element.dataset['tabId'];
    if (tabId) {
      return `tab:${tabId}`;
    }
    const folderId = element.dataset['folderId'];
    if (folderId) {
      return `folder:${folderId}`;
    }
    return '';
  }

  private snapshotFolderChildCounts_(): Map<string, number> {
    const counts = new Map<string, number>();
    const folders =
        this.shadowRoot?.querySelectorAll<HTMLElement>('dao-folder-item');
    if (!folders) {
      return counts;
    }
    for (const folder of folders) {
      const folderId = folder.dataset['folderId'];
      if (!folderId) {
        continue;
      }
      const childCount = Number(folder.dataset['folderChildCount']);
      if (!Number.isFinite(childCount)) {
        continue;
      }
      counts.set(folderId, childCount);
    }
    return counts;
  }

  private didAnyFolderShrink_(): boolean {
    if (!this.previousFolderChildCounts_) {
      return false;
    }
    const currentCounts = this.snapshotFolderChildCounts_();
    for (const [folderId, previousCount] of this.previousFolderChildCounts_) {
      const currentCount = currentCounts.get(folderId);
      if (currentCount !== undefined && currentCount < previousCount) {
        return true;
      }
    }
    return false;
  }

  private onDragStart_(e: DragEvent) {
    // Capture the dragged tab index from the bubbled event data.
    if (!e.dataTransfer) return;
    // The data is set by dao-tab-item; extract the tab index from the
    // composed event. We can't read dataTransfer in dragstart due to
    // protection, so find the source tab-item element.
    const target = e.composedPath().find(
        el => (el as HTMLElement).tagName === 'DAO-TAB-ITEM') as
        (HTMLElement & {tabData: TabData}) | undefined;
    if (target) {
      this.draggedTabIndex_ = target.tabData.index;
      this.activateNativeTabDrag_();
    }
  }

  private isInternalReorder_(e: DragEvent): boolean {
    if (!e.dataTransfer) return false;
    if (getActivePinnedItemDragId()) return true;
    return e.dataTransfer.types.includes('text/plain') ||
        e.dataTransfer.types.includes(PINNED_ITEM_DRAG_MIME_TYPE) ||
        e.dataTransfer.types.includes(FOLDER_MIME_TYPE);
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

    const useModel = this.folderModel && this.folderModel.hasData();

    if (useModel) {
      this.computeModelDropPosition_(e);
    } else {
      this.computeFlatDropPosition_(e, isExternal);
    }
  }

  /**
   * Compute drop position when folder model is active.
   * Uses all top-level rendered items (tabs + folders) in model order.
   */
  private computeModelDropPosition_(e: DragEvent) {
    const allItems = this.shadowRoot!.querySelectorAll(
        'dao-tab-item, dao-folder-item');
    const clientY = e.clientY;
    let indicatorY = 0;
    let modelIndex = allItems.length;  // default: after last

    if (allItems.length === 0) {
      indicatorY = 0;
      modelIndex = 0;
    } else {
      let found = false;
      for (let i = 0; i < allItems.length; i++) {
        const el = allItems[i] as HTMLElement;
        const rect = el.getBoundingClientRect();
        if (clientY < rect.top + rect.height / 2) {
          indicatorY = el.offsetTop;
          modelIndex = i;
          found = true;
          break;
        }
      }
      if (!found) {
        const lastEl = allItems[allItems.length - 1] as HTMLElement;
        indicatorY = lastEl.offsetTop + lastEl.offsetHeight;
        modelIndex = allItems.length;
      }
    }

    // Also compute a Chromium tab index for sendNative('moveTab').
    let insertIndex = -1;
    const tabItems = this.shadowRoot!.querySelectorAll('dao-tab-item');
    if (tabItems.length > 0) {
      for (const item of tabItems) {
        const el = item as HTMLElement;
        const rect = el.getBoundingClientRect();
        if (clientY < rect.top + rect.height / 2) {
          const tabData = (item as unknown as {tabData: TabData}).tabData;
          insertIndex = tabData.index;
          break;
        }
      }
      if (insertIndex === -1) {
        const lastTab = (tabItems[tabItems.length - 1] as unknown as
            {tabData: TabData}).tabData;
        insertIndex = lastTab.index + 1;
      }
    }

    this.dropIndicatorY_ = indicatorY;
    const indicator =
        this.shadowRoot!.querySelector('.drop-indicator') as HTMLElement;
    if (indicator) {
      indicator.style.top = `${indicatorY}px`;
    }

    this.dropInsertIndex_ = insertIndex;
    this.dropModelIndex_ = modelIndex;
  }

  /**
   * Original flat drop position calculation (no folder model).
   */
  private computeFlatDropPosition_(e: DragEvent, isExternal: boolean) {
    const items = this.shadowRoot!.querySelectorAll('dao-tab-item');
    const clientY = e.clientY;
    let indicatorY = 0;
    let insertIndex = -1;  // -1 = append at end

    if (items.length === 0) {
      indicatorY = 0;
      insertIndex = -1;
    } else {
      let found = false;
      for (const item of items) {
        const el = item as HTMLElement;
        const rect = el.getBoundingClientRect();
        if (clientY < rect.top + rect.height / 2) {
          indicatorY = el.offsetTop;
          const tabData = (item as unknown as {tabData: TabData}).tabData;
          insertIndex = tabData.index;
          found = true;
          break;
        }
      }
      if (!found) {
        const lastEl = items[items.length - 1] as HTMLElement;
        indicatorY = lastEl.offsetTop + lastEl.offsetHeight;
        const lastTab =
            (items[items.length - 1] as unknown as {tabData: TabData}).tabData;
        insertIndex = lastTab.index + 1;
      }

      // Prevent inserting between split-group tabs.
      const splitGroups =
          this.shadowRoot!.querySelectorAll('.split-group');
      for (const splitGroup of splitGroups) {
        const group = splitGroup as HTMLElement;
        const groupRect = group.getBoundingClientRect();
        const splitItems = group.querySelectorAll('dao-tab-item');
        if (splitItems.length > 0 &&
            clientY >= groupRect.top && clientY <= groupRect.bottom) {
          const midGroup = groupRect.top + groupRect.height / 2;
          if (clientY < midGroup) {
            indicatorY = group.offsetTop;
            const firstTab =
                (splitItems[0] as unknown as {tabData: TabData}).tabData;
            insertIndex = firstTab.index;
          } else {
            indicatorY = group.offsetTop + group.offsetHeight;
            const lastTab =
                (splitItems[splitItems.length - 1] as unknown as
                     {tabData: TabData})
                    .tabData;
            insertIndex = lastTab.index + 1;
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
      sendNative('setDropInsertIndex', insertIndex);
    }

    this.dropInsertIndex_ = insertIndex;
    this.dropModelIndex_ = -1;
  }

  private onDragLeave_(e: DragEvent) {
    // Only clear if actually leaving the component
    const related = e.relatedTarget as Node | null;
    if (related && this.contains(related)) return;
    this.classList.remove('drag-over');
    this.dropInsertIndex_ = -1;
    this.dropModelIndex_ = -1;
    sendNative('setDropInsertIndex', -1);

    // Fallback for tab drags that did not originate from dao-tab-item.
    if (this.isPointOutsideSidebar_(e.clientX, e.clientY)) {
      this.activateNativeTabDrag_();
    }
  }

  private activateNativeTabDrag_() {
    if (this.tabDragActivated_) {
      return;
    }
    console.error('[Dao-Xwin-JS] activating tabDragActive');
    this.tabDragActivated_ = true;
    sendNative('tabDragActive', true);
  }

  private onDragEnd_(e: DragEvent) {
    console.error('[Dao-Xwin-JS] onDragEnd_: dropEffect=' +
        (e.dataTransfer?.dropEffect ?? 'null') +
        ' tabDragActivated=' + this.tabDragActivated_ +
        ' draggedTabIndex=' + this.draggedTabIndex_ +
        ' outside=' + this.isPointOutsideSidebar_(e.clientX, e.clientY));
    // Defensive cleanup: ensure drag-over state is always cleared
    // when any drag ends, preventing the sidebar from getting stuck.
    this.classList.remove('drag-over');
    this.dropInsertIndex_ = -1;
    this.dropModelIndex_ = -1;

    // If no target accepted the drop, detach to a new window at cursor.
    // Two guards prevent a fast drag+release inside the sidebar from
    // being mistaken for a drag-out:
    //   1. tabDragActivated_ must be true — this is a Dao tab drag.
    //   2. The release point must be outside the sidebar's viewport —
    //      if the pointer is still inside, the user was reordering
    //      within the sidebar and released before any dragover target
    //      had a chance to preventDefault.
    if (e.dataTransfer && e.dataTransfer.dropEffect === 'none' &&
        this.draggedTabIndex_ >= 0 &&
        this.tabDragActivated_ &&
        this.isPointOutsideSidebar_(e.clientX, e.clientY)) {
      // Remove from folder model before detaching so the folder
      // membership doesn't persist after the tab leaves this window.
      this.maybeRemoveFromFolder_(this.draggedTabIndex_);
      sendNative('detachTabToNewWindow', this.draggedTabIndex_,
          e.screenX, e.screenY);
    }
    this.draggedTabIndex_ = -1;

    if (this.tabDragActivated_) {
      this.tabDragActivated_ = false;
      sendNative('tabDragActive', false);
    }
  }

  /**
   * Returns true if (clientX, clientY) is outside the sidebar
   * WebContents viewport. A release inside the viewport means the
   * user did not drag out of the sidebar, so the tab should not be
   * detached. Negative coordinates indicate the release was outside
   * the WebContents entirely.
   */
  private isPointOutsideSidebar_(clientX: number, clientY: number): boolean {
    return isPointOutsideViewport(
        clientX, clientY, window.innerWidth, window.innerHeight);
  }

  private onDrop_(e: DragEvent) {
    this.classList.remove('drag-over');

    // Handle internal tab reorder or cross-window tab move
    if (e.dataTransfer) {
      const pinnedItemId = e.dataTransfer.getData(PINNED_ITEM_DRAG_MIME_TYPE) ||
          getActivePinnedItemDragId();
      if (pinnedItemId) {
        e.preventDefault();
        e.stopPropagation();
        sendNative('unpinPinnedItem', pinnedItemId, this.dropInsertIndex_);
        clearActivePinnedItemDragId();
        this.dropInsertIndex_ = -1;
        this.dropModelIndex_ = -1;
        return;
      }

      const data = e.dataTransfer.getData('text/plain');
      console.error('[Dao-Xwin-JS] onDrop_: data=' + JSON.stringify(data) +
          ' thisSessionId=' + this.sessionId +
          ' dropInsertIndex=' + this.dropInsertIndex_);
      if (data.startsWith(TAB_DRAG_PREFIX)) {
        e.preventDefault();
        e.stopPropagation();

        const useModel = this.folderModel && this.folderModel.hasData();

        const parts = data.substring(TAB_DRAG_PREFIX.length).split(':');
        console.error('[Dao-Xwin-JS] parts=' + JSON.stringify(parts));
        if (parts.length === 2) {
          const sourceSessionId = parseInt(parts[0]!, 10);
          const fromIndex = parseInt(parts[1]!, 10);
          console.error('[Dao-Xwin-JS] parsed sourceSessionId=' +
              sourceSessionId + ' fromIndex=' + fromIndex +
              ' sameWindow=' + (sourceSessionId === this.sessionId));
          if (!isNaN(sourceSessionId) && !isNaN(fromIndex) &&
              this.dropInsertIndex_ >= 0) {
            // Check if the dragged tab is inside a folder — if so,
            // remove it from the folder first.
            const wasInFolder = this.maybeRemoveFromFolder_(fromIndex);

            if (sourceSessionId === this.sessionId) {
              // Same window: reorder in Chromium tab strip
              let toIndex = this.dropInsertIndex_;
              if (fromIndex < toIndex) {
                toIndex--;
              }
              if (fromIndex !== toIndex) {
                sendNative('moveTab', fromIndex, toIndex);
              }

              // Also reorder in the folder model if active.
              if (useModel && !wasInFolder && this.dropModelIndex_ >= 0) {
                this.dispatchModelReorder_(fromIndex, this.dropModelIndex_);
              }
            } else {
              // Cross-window: move tab from source window to this window
              console.error('[Dao-Xwin-JS] firing moveTabCrossWindow ' +
                  sourceSessionId + ' ' + fromIndex + ' ' +
                  this.dropInsertIndex_);
              sendNative('moveTabCrossWindow', sourceSessionId, fromIndex,
                  this.dropInsertIndex_);
            }
          }
        } else if (parts.length === 1) {
          // Legacy format: "<prefix><tabIndex>" (same-window only)
          const fromIndex = parseInt(parts[0]!, 10);
          if (!isNaN(fromIndex) && this.dropInsertIndex_ >= 0) {
            const wasInFolder = this.maybeRemoveFromFolder_(fromIndex);

            let toIndex = this.dropInsertIndex_;
            if (fromIndex < toIndex) {
              toIndex--;
            }
            if (fromIndex !== toIndex) {
              sendNative('moveTab', fromIndex, toIndex);
            }

            if (useModel && !wasInFolder && this.dropModelIndex_ >= 0) {
              this.dispatchModelReorder_(fromIndex, this.dropModelIndex_);
            }
          }
        }
        this.dropInsertIndex_ = -1;
        this.dropModelIndex_ = -1;
        return;
      }

      // Handle folder reorder drag (custom MIME type)
      const folderData = e.dataTransfer.getData(FOLDER_MIME_TYPE);
      if (folderData && this.folderModel && this.dropModelIndex_ >= 0) {
        e.preventDefault();
        e.stopPropagation();
        this.dispatchFolderAction_({
          action: 'reorderFolder',
          folderId: folderData,
          toModelIndex: this.dropModelIndex_,
        });
        this.dropInsertIndex_ = -1;
        this.dropModelIndex_ = -1;
        return;
      }
    }

    // External file drops — don't prevent default, let the renderer handle
    // so OpenURLFromTab can intercept the navigation.
    this.dropInsertIndex_ = -1;
    this.dropModelIndex_ = -1;
  }

  private dispatchFolderAction_(detail: FolderAction) {
    this.dispatchEvent(new CustomEvent('folder-action', {
      bubbles: true, composed: true, detail,
    }));
  }

  /**
   * Trigger rename mode on a folder item by ID.
   * Used by the parent app after creating a new folder.
   */
  async startFolderRename(folderId: string) {
    await this.updateComplete;
    const items = this.shadowRoot!.querySelectorAll('dao-folder-item');
    for (const item of items) {
      const fi = item as HTMLElement & {folder: {id: string}; startRename: () => void};
      if (fi.folder?.id === folderId) {
        fi.startRename();
        return;
      }
    }
  }

  /**
   * Dispatch a model reorder action for a loose tab.
   */
  private dispatchModelReorder_(tabIndex: number, toModelIndex: number) {
    const tab = this.tabs.find(t => t.index === tabIndex);
    if (!tab) return;
    this.dispatchFolderAction_({
      action: 'reorderModel',
      tabId: tab.tabId,
      toModelIndex,
    });
  }

  /**
   * Handle right-click on a tab item. Computes visual tab order and
   * folder tab indices, then sends context menu request to C++.
   */
  private onTabContextMenu_(e: CustomEvent) {
    const {index, screenX, screenY} = e.detail;
    const {visualOrder, folderTabIndices} = this.computeContextMenuData_();
    sendNative('showTabContextMenu', index, screenX, screenY,
        folderTabIndices, visualOrder);
  }

  /**
   * Handle right-click on a folder item by asking C++ to show a native menu.
   */
  private onFolderContextMenu_(e: CustomEvent) {
    const {folderId, screenX, screenY} = e.detail;
    sendNative('showFolderContextMenu', folderId, screenX, screenY);
  }

  /**
   * Compute both visual tab order and folder tab indices in a single pass.
   */
  private computeContextMenuData_():
      {visualOrder: number[]; folderTabIndices: number[]} {
    if (!this.folderModel || !this.folderModel.hasData()) {
      return {
        visualOrder: this.tabs.map(t => t.index),
        folderTabIndices: [],
      };
    }

    const remaining = [...this.tabs];

    const consume = (
        ref: {tabId?: string; url: string; title: string}): TabData | null => {
      let idx = ref.tabId ?
          remaining.findIndex(tab => tab.tabId === ref.tabId) : -1;
      if (idx === -1) {
        idx = remaining.findIndex(
            tab => tab.url === ref.url && tab.title === ref.title);
      }
      if (idx === -1) {
        idx = remaining.findIndex(tab => tab.url === ref.url);
      }
      if (idx === -1) {
        return null;
      }
      return remaining.splice(idx, 1)[0]!;
    };

    const items = this.folderModel.getOrderedItems();
    const visualOrder: number[] = [];
    const folderTabIndices: number[] = [];

    for (const item of items) {
      if (item.type === 'tab') {
        const matched = consume(item);
        if (matched) visualOrder.push(matched.index);
      } else if (item.type === 'folder') {
        const folder = item as import('./sidebar_bridge.js').FolderData;
        for (const child of folder.children) {
          const matched = consume(child);
          if (matched) {
            visualOrder.push(matched.index);
            folderTabIndices.push(matched.index);
          }
        }
      }
    }

    // Remaining unmatched tabs.
    for (const tab of remaining) {
      visualOrder.push(tab.index);
    }

    return {visualOrder, folderTabIndices};
  }

  /**
   * If the tab at the given index is inside a folder in the model,
   * dispatch a removeFromFolder action so the app updates the model.
   * Returns true if the tab was found in a folder.
   */
  private maybeRemoveFromFolder_(tabIndex: number): boolean {
    if (!this.folderModel) return false;
    const tab = this.tabs.find(t => t.index === tabIndex);
    if (!tab) return false;

    const tabRef = {tabId: tab.tabId, url: tab.url, title: tab.title};
    const folderId = this.folderModel.findTabFolder(tabRef);
    if (!folderId) return false;

    this.dispatchFolderAction_({
      action: 'removeFromFolder',
      folderId,
      tabId: tab.tabId,
      toModelIndex: this.dropModelIndex_ >= 0 ?
          this.dropModelIndex_ : undefined,
    });
    return true;
  }
}

customElements.define('dao-tab-list', DaoTabList);
