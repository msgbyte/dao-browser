// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative, addListener, loadFolders, saveFolders, parseTabDragData} from './sidebar_bridge.js';
import type {SidebarState, TabData, FolderAction} from './sidebar_bridge.js';
import {FolderModel} from './dao_folder_model.js';

import './dao_favorites_view.js';
import './dao_new_tab_button.js';
import './dao_sidebar_section.js';
import './dao_tab_list.js';
import './dao_download_button.js';
import './dao_media_control.js';

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
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 0 6px 6px;
      }

      .plus-menu-container {
        position: relative;
      }

      .plus-btn {
        width: 26px;
        height: 26px;
        border: none;
        border-radius: 8px;
        background: transparent;
        color: var(--text-secondary);
        cursor: default;
        display: flex;
        align-items: center;
        justify-content: center;
        transition: background 0.15s ease;
      }

      .plus-btn:hover {
        background: rgba(0, 0, 0, 0.08);
      }

      .plus-menu {
        position: absolute;
        bottom: 32px;
        right: 0;
        background: rgba(255, 255, 255, 0.9);
        backdrop-filter: blur(20px);
        -webkit-backdrop-filter: blur(20px);
        border: 1px solid rgba(0, 0, 0, 0.1);
        border-radius: 8px;
        padding: 4px;
        min-width: 150px;
        z-index: 1000;
        box-shadow: 0 4px 16px rgba(0, 0, 0, 0.12);
      }

      .plus-menu-item {
        display: flex;
        align-items: center;
        gap: 8px;
        height: 30px;
        padding: 0 10px;
        border: none;
        border-radius: 6px;
        background: transparent;
        color: var(--text-primary);
        font-family: var(--font-family, system-ui);
        font-size: 12px;
        cursor: default;
        width: 100%;
        text-align: left;
      }

      .plus-menu-item:hover {
        background: rgba(0, 0, 0, 0.06);
      }

      .plus-menu-item svg {
        flex-shrink: 0;
      }
    `;
  }

  static override get properties() {
    return {
      pinnedTabs_: {type: Array},
      unpinnedTabs_: {type: Array},
      sessionId_: {type: Number},
      folderModelVersion_: {type: Number},
      showPlusMenu_: {type: Boolean},
      newTabHighlighted_: {type: Boolean},
    };
  }

  protected pinnedTabs_: TabData[] = [];
  protected unpinnedTabs_: TabData[] = [];
  protected sessionId_: number = 0;
  protected folderModelVersion_: number = 0;
  protected showPlusMenu_: boolean = false;
  protected newTabHighlighted_: boolean = false;

  private folderModel_ = new FolderModel();
  private foldersLoaded_: boolean = false;
  private initialStateReceived_: boolean = false;
  private boundClosePlusMenu_: ((e: MouseEvent) => void) | null = null;

  override connectedCallback() {
    super.connectedCallback();

    addListener('sidebarStateChanged', (...args: unknown[]) => {
      const state = args[0] as SidebarState;
      this.pinnedTabs_ = state.pinnedTabs;
      this.unpinnedTabs_ = state.unpinnedTabs;
      this.sessionId_ = state.sessionId;

      // On the very first state push, reconcile folder model with
      // actual tabs (session restore matching).
      if (!this.initialStateReceived_) {
        this.initialStateReceived_ = true;
        this.initFolders_();
      } else if (this.foldersLoaded_) {
        // Keep runtime tab identities in sync after duplicate/move/close so
        // folder operations target the exact rendered tab, not a URL match.
        this.folderModel_.reconcile(this.unpinnedTabs_);
        this.folderModelVersion_++;
      }
    });

    addListener('tabUpdated', (...args: unknown[]) => {
      const updated = args[0] as TabData;
      // Update in pinned or unpinned list
      if (updated.isPinned) {
        this.pinnedTabs_ = this.pinnedTabs_.map(
            t => t.tabId === updated.tabId ? updated : t);
      } else {
        this.unpinnedTabs_ = this.unpinnedTabs_.map(
            t => t.tabId === updated.tabId ? updated : t);
      }
    });

    addListener('activeTabChanged', (...args: unknown[]) => {
      const data = args[0] as {activeIndex: number};
      this.pinnedTabs_ = this.pinnedTabs_.map(
          t => ({...t, isActive: t.index === data.activeIndex}));
      this.unpinnedTabs_ = this.unpinnedTabs_.map(
          t => ({...t, isActive: t.index === data.activeIndex}));
    });

    addListener('newTabButtonHighlight', (...args: unknown[]) => {
      this.newTabHighlighted_ = args[0] as boolean;
    });

    // Listen for folder actions from child components.
    this.addEventListener('folder-action', ((e: CustomEvent<FolderAction>) => {
      this.handleFolderAction_(e.detail);
    }) as EventListener);

    this.addEventListener('contextmenu', (e: MouseEvent) => {
      e.preventDefault();
      sendNative('showSidebarContextMenu', e.screenX, e.screenY);
    });

    sendNative('getInitialState');
  }

  /**
   * Load folder data from C++ and reconcile with current tabs.
   */
  private async initFolders_() {
    try {
      const json = await loadFolders();
      this.folderModel_.loadFromJson(json);

      // Reconcile with actual tabs.
      if (this.unpinnedTabs_.length > 0) {
        this.folderModel_.reconcile(this.unpinnedTabs_);
        this.saveFolders_();
      }

      this.foldersLoaded_ = true;
      this.folderModelVersion_++;
    } catch (e) {
      console.error('DaoSidebarApp: failed to load folders', e);
      this.foldersLoaded_ = true;
    }
  }

  /**
   * Save current folder state and trigger re-render.
   */
  private saveFolders_() {
    saveFolders(this.folderModel_.toJson());
    this.folderModelVersion_++;
  }

  /**
   * Handle folder actions dispatched from child components.
   */
  private handleFolderAction_(detail: FolderAction) {
    switch (detail.action) {
      case 'toggleCollapse':
        this.folderModel_.toggleCollapse(detail.folderId);
        this.saveFolders_();
        break;

      case 'rename':
        this.folderModel_.renameFolder(detail.folderId, detail.name);
        this.saveFolders_();
        break;

      case 'delete':
        this.folderModel_.deleteFolder(detail.folderId);
        this.saveFolders_();
        break;

      case 'tabDrop':
        this.handleTabDropOnFolder_(detail.folderId, detail.dragData);
        break;

      case 'removeFromFolder': {
        const idx = detail.toModelIndex;
        const tab = this.findUnpinnedTabById_(detail.tabId);
        if (!tab) break;
        this.folderModel_.removeTabFromFolder(
            tab, detail.folderId,
            idx !== undefined && idx >= 0 ? idx : undefined);
        this.saveFolders_();
        break;
      }

      case 'childReorder':
        this.handleChildReorder_(
            detail.folderId, detail.dragData, detail.dropIndex);
        break;

      case 'reorderModel':
        this.handleModelReorder_(detail.tabId, detail.toModelIndex);
        break;

      case 'reorderFolder':
        this.handleFolderReorder_(detail.folderId, detail.toModelIndex);
        break;
    }
  }

  /**
   * Handle a tab being dropped onto a folder.
   * Parses the drag data, finds the tab, and moves it into the folder.
   */
  private handleTabDropOnFolder_(folderId: string, dragData: string) {
    const tab = this.resolveTabFromDrag_(dragData);
    if (!tab) return;

    const sourceFolderId = this.folderModel_.findTabFolder(tab);
    this.folderModel_.moveTabToFolder(tab, folderId, sourceFolderId || undefined);
    this.saveFolders_();
  }

  /**
   * Handle reordering a folder in the model's top-level items.
   */
  private handleFolderReorder_(folderId: string, toModelIndex: number) {
    const items = this.folderModel_.getOrderedItems();
    const fromIndex = items.findIndex(
        item => item.type === 'folder' && item.id === folderId);
    if (fromIndex === -1) return;
    if (fromIndex === toModelIndex) return;

    this.folderModel_.reorder(fromIndex, toModelIndex);
    this.saveFolders_();
  }

  /**
   * Handle reordering a loose tab in the model's top-level items.
   */
  private handleModelReorder_(tabId: string, toModelIndex: number) {
    const tab = this.findUnpinnedTabById_(tabId);
    if (!tab) return;
    const items = this.folderModel_.getOrderedItems();
    const fromIndex = items.findIndex(
        item => item.type === 'tab' &&
            ((item.tabId && item.tabId === tab.tabId) ||
             (!item.tabId && item.url === tab.url && item.title === tab.title)));
    if (fromIndex === -1) return;
    if (fromIndex === toModelIndex) return;

    this.folderModel_.reorder(fromIndex, toModelIndex);
    this.saveFolders_();
  }

  /**
   * Handle reordering a tab within a folder's children.
   */
  private handleChildReorder_(
      folderId: string, dragData: string, dropIndex: number) {
    const tab = this.resolveTabFromDrag_(dragData);
    if (!tab) return;

    const folder = this.folderModel_.getFolders().find(
        f => f.id === folderId);
    if (!folder) return;

    const sourceFolderId = this.folderModel_.findTabFolder(tab);

    if (sourceFolderId === folderId) {
      const fromChildIndex = folder.children.findIndex(
          c => (c.tabId && c.tabId === tab.tabId) ||
              (!c.tabId && c.url === tab.url && c.title === tab.title));
      if (fromChildIndex !== -1 && fromChildIndex !== dropIndex) {
        this.folderModel_.reorderWithinFolder(
            folderId, fromChildIndex, dropIndex);
        this.saveFolders_();
      }
    } else {
      this.folderModel_.moveTabToFolder(tab, folderId, sourceFolderId || undefined);
      const currentIdx = folder.children.length - 1;
      if (currentIdx !== dropIndex && dropIndex >= 0) {
        this.folderModel_.reorderWithinFolder(
            folderId, currentIdx, dropIndex);
      }
      this.saveFolders_();
    }
  }

  /**
   * Parse drag data and resolve to an unpinned tab.
   */
  private resolveTabFromDrag_(dragData: string): TabData | null {
    const parsed = parseTabDragData(dragData);
    if (!parsed) return null;
    return this.unpinnedTabs_.find(t => t.index === parsed.tabIndex) || null;
  }

  private findUnpinnedTabById_(tabId: string): TabData | null {
    return this.unpinnedTabs_.find(t => t.tabId === tabId) || null;
  }

  override render() {
    return html`
      <div class="sidebar-content">
        <dao-new-tab-button ?highlighted=${this.newTabHighlighted_}></dao-new-tab-button>

        ${this.pinnedTabs_.length > 0 ? html`
          <dao-favorites-view
            .tabs=${this.pinnedTabs_}>
          </dao-favorites-view>
        ` : ''}

        <div class="tab-section">
          <dao-sidebar-section sectionTitle="Today">
            <dao-tab-list
              .tabs=${this.newTabHighlighted_ ? this.unpinnedTabs_.map(t => ({...t, isActive: false})) : this.unpinnedTabs_}
              .sessionId=${this.sessionId_}
              .folderModel=${this.foldersLoaded_ ? this.folderModel_ : null}
              .folderModelVersion=${this.folderModelVersion_}>
            </dao-tab-list>
          </dao-sidebar-section>
        </div>

        <dao-media-control></dao-media-control>

        <div class="bottom-section">
          <dao-download-button></dao-download-button>
          <div class="plus-menu-container">
            ${this.showPlusMenu_ ? html`
              <div class="plus-menu">
                <button class="plus-menu-item" @click=${this.onNewTab_}>
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                       stroke="currentColor" stroke-width="2"
                       stroke-linecap="round" stroke-linejoin="round">
                    <line x1="12" y1="5" x2="12" y2="19"></line>
                    <line x1="5" y1="12" x2="19" y2="12"></line>
                  </svg>
                  New Tab
                </button>
                <button class="plus-menu-item" @click=${this.onNewFolder_}>
                  <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                       stroke="currentColor" stroke-width="2"
                       stroke-linecap="round" stroke-linejoin="round">
                    <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
                  </svg>
                  New Folder
                </button>
              </div>
            ` : nothing}
            <button class="plus-btn" @click=${this.onPlusClick_}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                   stroke="currentColor" stroke-width="2"
                   stroke-linecap="round" stroke-linejoin="round">
                <line x1="12" y1="5" x2="12" y2="19"></line>
                <line x1="5" y1="12" x2="19" y2="12"></line>
              </svg>
            </button>
          </div>
        </div>
      </div>
    `;
  }

  private onPlusClick_(e: Event) {
    e.stopPropagation();
    if (this.showPlusMenu_) {
      this.closePlusMenu_();
    } else {
      this.showPlusMenu_ = true;
      this.boundClosePlusMenu_ = (me: MouseEvent) => {
        // Close if click is outside the menu container.
        const container = this.shadowRoot!.querySelector('.plus-menu-container');
        if (container && !container.contains(me.target as Node)) {
          this.closePlusMenu_();
        }
      };
      setTimeout(() => {
        document.addEventListener('click', this.boundClosePlusMenu_!);
      }, 0);
    }
  }

  private closePlusMenu_() {
    this.showPlusMenu_ = false;
    if (this.boundClosePlusMenu_) {
      document.removeEventListener('click', this.boundClosePlusMenu_);
      this.boundClosePlusMenu_ = null;
    }
  }

  private onNewTab_(e: Event) {
    e.stopPropagation();
    this.closePlusMenu_();
    sendNative('showCommandBarForNewTab');
  }

  private async onNewFolder_(e: Event) {
    e.stopPropagation();
    this.closePlusMenu_();
    const folder = this.folderModel_.addFolder('Untitled');
    this.saveFolders_();

    await this.updateComplete;
    const tabList = this.shadowRoot!.querySelector('dao-tab-list') as
        HTMLElement & {startFolderRename: (id: string) => void} | null;
    if (tabList) {
      tabList.startFolderRename(folder.id);
    }
  }
}

customElements.define('dao-sidebar-app', DaoSidebarApp);
