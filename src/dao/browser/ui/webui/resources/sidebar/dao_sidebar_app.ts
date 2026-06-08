// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {
  SIDEBAR_POINTER_EXITED_EVENT,
  sendNative,
  addListener,
  removeListener,
  loadFolders,
  saveFolders,
  parseTabDragData,
} from './sidebar_bridge.js';
import type {
  SidebarState, TabData, PinnedItemData, FolderAction, UpdateStateData
} from './sidebar_bridge.js';
import {FolderModel} from './dao_folder_model.js';

import './dao_update_button.js';
import './dao_favorites_view.js';
import './dao_new_tab_button.js';
import './dao_pinned_tabs_grid.js';
import './dao_sidebar_section.js';
import './dao_tab_list.js';
import './dao_download_button.js';
import './dao_media_control.js';

const TAB_SCROLLBAR_STALE_HOVER_MS = 600;

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

      .tab-section-shell {
        position: relative;
        flex: 1;
        min-height: 0;
      }

      .tab-section {
        height: 100%;
        overflow-y: auto;
        overflow-x: hidden;
        scrollbar-width: none;
      }

      .tab-section::-webkit-scrollbar {
        display: none;
        width: 0;
        height: 0;
      }

      .tab-scrollbar {
        position: absolute;
        top: 4px;
        right: 0;
        bottom: 4px;
        width: 4px;
        pointer-events: none;
        opacity: 0;
        transition: opacity 120ms ease;
      }

      .tab-scrollbar.visible.hovered {
        opacity: 1;
      }

      .tab-scrollbar-thumb {
        position: absolute;
        top: 0;
        right: 0;
        width: 4px;
        min-height: 18px;
        background: var(--scrollbar-thumb);
        border-radius: 2px;
      }

      .tab-scrollbar.visible.hovered .tab-scrollbar-thumb {
        background: var(--scrollbar-thumb-hover);
      }

      .plus-menu-container {
        position: relative;
      }

      .toolbar-end-actions {
        display: flex;
        align-items: center;
        justify-content: flex-end;
        gap: 6px;
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

      @media (prefers-color-scheme: dark) {
        .plus-btn:hover {
          background: rgba(255, 255, 255, 0.08);
        }
        .plus-menu {
          background: rgba(70, 76, 82, 0.92);
          border-color: rgba(255, 255, 255, 0.14);
          box-shadow: 0 4px 16px rgba(0, 0, 0, 0.40);
        }
        .plus-menu-item:hover {
          background: rgba(255, 255, 255, 0.08);
        }
      }
    `;
  }

  static override get properties() {
    return {
      pinnedItems_: {type: Array},
      pinnedTabs_: {type: Array},
      unpinnedTabs_: {type: Array},
      sessionId_: {type: Number},
      folderModelVersion_: {type: Number},
      showPlusMenu_: {type: Boolean},
      newTabHighlighted_: {type: Boolean},
      updateState_: {type: Object},
      tabScrollbarVisible_: {type: Boolean},
      tabScrollbarHovered_: {type: Boolean},
      tabScrollbarThumbTop_: {type: Number},
      tabScrollbarThumbHeight_: {type: Number},
    };
  }

  // Use `declare` + constructor assignment so TypeScript's emitted class
  // fields do not shadow Lit's reactive accessors. With `target: ESNext` and
  // `useDefineForClassFields: true` (Chromium's default), a plain
  // `protected foo: T = ...` initializer would emit Object.defineProperty()
  // on `this`, overriding the reactive accessor Lit installs from
  // `static get properties()`. That makes property assignments silently
  // skip update scheduling.
  declare protected pinnedItems_: PinnedItemData[];
  declare protected pinnedTabs_: TabData[];
  declare protected unpinnedTabs_: TabData[];
  declare protected sessionId_: number;
  declare protected folderModelVersion_: number;
  declare protected showPlusMenu_: boolean;
  declare protected newTabHighlighted_: boolean;
  declare protected updateState_: UpdateStateData | null;
  declare protected tabScrollbarVisible_: boolean;
  declare protected tabScrollbarHovered_: boolean;
  declare protected tabScrollbarThumbTop_: number;
  declare protected tabScrollbarThumbHeight_: number;

  // Non-reactive internals — plain fields are fine here because Lit doesn't
  // install accessors for these.
  private folderModel_ = new FolderModel();
  private foldersLoaded_: boolean = false;
  private initialStateReceived_: boolean = false;
  private boundClosePlusMenu_: ((e: MouseEvent) => void) | null = null;
  private tabScrollbarHoverTimeout_: number | null = null;
  private listenerHandles_: Array<ReturnType<typeof addListener>> = [];
  private boundWindowMouseOut_ = (e: MouseEvent) => {
    if (!e.relatedTarget) {
      this.clearPointerState_();
    }
  };
  private boundWindowBlur_ = () => {
    this.clearPointerState_();
  };
  private boundVisibilityChange_ = () => {
    if (document.visibilityState !== 'visible') {
      this.clearPointerState_();
    }
  };

  constructor() {
    super();
    this.pinnedItems_ = [];
    this.pinnedTabs_ = [];
    this.unpinnedTabs_ = [];
    this.sessionId_ = 0;
    this.folderModelVersion_ = 0;
    this.showPlusMenu_ = false;
    this.newTabHighlighted_ = false;
    this.updateState_ = null;
    this.tabScrollbarVisible_ = false;
    this.tabScrollbarHovered_ = false;
    this.tabScrollbarThumbTop_ = 0;
    this.tabScrollbarThumbHeight_ = 0;
  }

  override connectedCallback() {
    super.connectedCallback();

    this.addSidebarListener_('sidebarStateChanged', (...args: unknown[]) => {
      const state = args[0] as SidebarState;
      this.pinnedItems_ = state.pinnedItems || [];
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

    this.addSidebarListener_('tabUpdated', (...args: unknown[]) => {
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

    this.addSidebarListener_('activeTabChanged', (...args: unknown[]) => {
      const data = args[0] as {activeIndex: number};
      this.pinnedTabs_ = this.pinnedTabs_.map(
          t => ({...t, isActive: t.index === data.activeIndex}));
      this.unpinnedTabs_ = this.unpinnedTabs_.map(
          t => ({...t, isActive: t.index === data.activeIndex}));
    });

    this.addSidebarListener_('newTabButtonHighlight', (...args: unknown[]) => {
      this.newTabHighlighted_ = args[0] as boolean;
    });

    this.addSidebarListener_(
        'sidebarPointerExited', () => this.clearPointerState_());

    this.addSidebarListener_('updateStateChanged', (...args: unknown[]) => {
      this.updateState_ = args[0] as UpdateStateData;
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
    sendNative('requestUpdateState');

    window.addEventListener('mouseout', this.boundWindowMouseOut_);
    window.addEventListener('blur', this.boundWindowBlur_);
    document.addEventListener('visibilitychange', this.boundVisibilityChange_);
  }

  override disconnectedCallback() {
    super.disconnectedCallback?.();
    this.clearTabScrollbarHoverTimeout_();
    for (const handle of this.listenerHandles_) {
      removeListener(handle);
    }
    this.listenerHandles_ = [];
    window.removeEventListener('mouseout', this.boundWindowMouseOut_);
    window.removeEventListener('blur', this.boundWindowBlur_);
    document.removeEventListener(
        'visibilitychange', this.boundVisibilityChange_);
  }

  override updated() {
    this.updateTabScrollbar_();
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

  private onTabSectionScroll_ = () => {
    this.showTabScrollbar_();
  };

  private onTabSectionPointerEnter_ = () => {
    this.showTabScrollbar_();
  };

  private onTabSectionPointerMove_ = () => {
    this.showTabScrollbar_();
  };

  private onTabSectionPointerLeave_ = () => {
    this.hideTabScrollbar_();
  };

  private showTabScrollbar_() {
    if (!this.tabScrollbarHovered_) {
      this.tabScrollbarHovered_ = true;
    }
    this.updateTabScrollbar_();
    this.armTabScrollbarHoverTimeout_();
  }

  private hideTabScrollbar_() {
    this.clearTabScrollbarHoverTimeout_();
    if (this.tabScrollbarHovered_) {
      this.tabScrollbarHovered_ = false;
    }
  }

  private addSidebarListener_(
      event: string, callback: (...args: unknown[]) => void) {
    this.listenerHandles_.push(addListener(event, callback));
  }

  private clearPointerState_() {
    this.hideTabScrollbar_();
    window.dispatchEvent(new CustomEvent(SIDEBAR_POINTER_EXITED_EVENT));
  }

  private armTabScrollbarHoverTimeout_() {
    this.clearTabScrollbarHoverTimeout_();
    this.tabScrollbarHoverTimeout_ = window.setTimeout(() => {
      this.tabScrollbarHoverTimeout_ = null;
      this.hideTabScrollbar_();
    }, TAB_SCROLLBAR_STALE_HOVER_MS);
  }

  private clearTabScrollbarHoverTimeout_() {
    if (this.tabScrollbarHoverTimeout_ === null) return;
    window.clearTimeout(this.tabScrollbarHoverTimeout_);
    this.tabScrollbarHoverTimeout_ = null;
  }

  private getTabScrollbarThumbStyle_(): string {
    return `height: ${this.tabScrollbarThumbHeight_}px; ` +
        `transform: translateY(${this.tabScrollbarThumbTop_}px);`;
  }

  private updateTabScrollbar_() {
    const scroller = this.shadowRoot?.querySelector('.tab-section') as
        HTMLElement | null;
    if (!scroller) return;

    const clientHeight = scroller.clientHeight;
    const scrollHeight = scroller.scrollHeight;
    const scrollRange = scrollHeight - clientHeight;
    const visible = clientHeight > 0 && scrollRange > 1;

    let thumbTop = 0;
    let thumbHeight = 0;
    if (visible) {
      const trackHeight = Math.max(0, clientHeight - 8);
      thumbHeight = Math.min(
          trackHeight,
          Math.max(18, Math.round(trackHeight * clientHeight / scrollHeight)));
      const maxThumbTop = Math.max(0, trackHeight - thumbHeight);
      thumbTop = Math.round(scroller.scrollTop / scrollRange * maxThumbTop);
    }

    if (this.tabScrollbarVisible_ !== visible ||
        this.tabScrollbarThumbTop_ !== thumbTop ||
        this.tabScrollbarThumbHeight_ !== thumbHeight) {
      this.tabScrollbarVisible_ = visible;
      this.tabScrollbarThumbTop_ = thumbTop;
      this.tabScrollbarThumbHeight_ = thumbHeight;
    }
  }

  override render() {
    return html`
      <div class="sidebar-content">
        ${this.pinnedItems_.length > 0 ? html`
          <dao-pinned-tabs-grid
            .items=${this.pinnedItems_}
            .sessionId=${this.sessionId_}>
          </dao-pinned-tabs-grid>
        ` : this.pinnedTabs_.length > 0 ? html`
          <dao-favorites-view
            .tabs=${this.pinnedTabs_}>
          </dao-favorites-view>
        ` : ''}

        <dao-new-tab-button ?highlighted=${this.newTabHighlighted_}></dao-new-tab-button>

        <div class="tab-section-shell"
             @pointerenter=${this.onTabSectionPointerEnter_}
             @pointermove=${this.onTabSectionPointerMove_}
             @pointerleave=${this.onTabSectionPointerLeave_}>
          <div class="tab-section" @scroll=${this.onTabSectionScroll_}
               @pointerenter=${this.onTabSectionPointerEnter_}>
            <dao-sidebar-section sectionTitle="Today">
              <dao-tab-list
                .tabs=${this.newTabHighlighted_ ? this.unpinnedTabs_.map(t => ({...t, isActive: false})) : this.unpinnedTabs_}
                .sessionId=${this.sessionId_}
                .folderModel=${this.foldersLoaded_ ? this.folderModel_ : null}
                .folderModelVersion=${this.folderModelVersion_}>
              </dao-tab-list>
            </dao-sidebar-section>
          </div>
          <div class="tab-scrollbar ${this.tabScrollbarVisible_ ? 'visible' : ''} ${this.tabScrollbarHovered_ ? 'hovered' : ''}">
            <div class="tab-scrollbar-thumb"
                 style="${this.getTabScrollbarThumbStyle_()}"></div>
          </div>
        </div>

        <dao-media-control></dao-media-control>

        <dao-download-button>
          <div class="toolbar-end-actions" slot="toolbar-end">
            <dao-update-button .updateState=${this.updateState_}></dao-update-button>
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
        </dao-download-button>
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
