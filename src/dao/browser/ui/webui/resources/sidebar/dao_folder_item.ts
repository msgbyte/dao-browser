// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {TAB_DRAG_PREFIX, FOLDER_MIME_TYPE} from './sidebar_bridge.js';
import type {FolderData, FolderAction, TabData} from './sidebar_bridge.js';
import './dao_tab_item.js';

export class DaoFolderItem extends CrLitElement {
  static get is() {
    return 'dao-folder-item';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
      }

      .folder-row {
        display: flex;
        align-items: center;
        height: 32px;
        padding: 0 10px;
        margin: 1px 4px;
        border-radius: 10px;
        cursor: default;
        gap: 6px;
        transition: background 0.15s ease;
      }

      .folder-row:hover {
        background: var(--ink-drop);
      }

      .folder-row.drag-over {
        background: rgba(70, 120, 190, 0.15);
      }

      .chevron {
        width: 12px;
        height: 12px;
        flex-shrink: 0;
        color: var(--text-secondary);
        transition: transform 0.15s ease;
      }

      .chevron.expanded {
        transform: rotate(90deg);
      }

      .folder-icon {
        width: 14px;
        height: 14px;
        flex-shrink: 0;
        color: rgba(70, 120, 190, 0.8);
      }

      .folder-name {
        flex: 1;
        min-width: 0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        font-size: 13px;
        font-weight: 500;
        color: var(--text-primary);
      }

      .rename-input {
        flex: 1;
        min-width: 0;
        font-size: 13px;
        font-weight: 500;
        font-family: var(--font-family, system-ui);
        color: var(--text-primary);
        background: var(--surface);
        border: 1px solid var(--accent);
        border-radius: 4px;
        padding: 0 4px;
        height: 22px;
        outline: none;
      }

      .count {
        width: 16px;
        text-align: center;
        font-size: 11px;
        color: var(--text-muted);
        flex-shrink: 0;
      }

      .children-container {
        overflow: hidden;
        transition: max-height 0.2s ease;
      }

      .children-container.collapsed {
        max-height: 0 !important;
      }

      .children-inner {
        margin-left: 14px;
        padding-left: 8px;
        position: relative;
      }

      .children-inner::before {
        content: '';
        position: absolute;
        left: 0;
        top: 4px;
        bottom: 4px;
        width: 2px;
        background: rgba(70, 120, 190, 0.4);
        border-radius: 1px;
      }

      .child-drop-indicator {
        height: 2px;
        margin: 0 8px;
        background: var(--accent);
        border-radius: 1px;
        display: none;
      }

      .child-drop-indicator.visible {
        display: block;
      }

      .context-menu {
        position: fixed;
        background: rgba(255, 255, 255, 0.9);
        backdrop-filter: blur(20px);
        -webkit-backdrop-filter: blur(20px);
        border: 1px solid rgba(0, 0, 0, 0.1);
        border-radius: 8px;
        padding: 4px;
        min-width: 140px;
        z-index: 1000;
        box-shadow: 0 4px 16px rgba(0, 0, 0, 0.12);
      }

      .context-menu-item {
        display: flex;
        align-items: center;
        gap: 8px;
        height: 28px;
        padding: 0 8px;
        border: none;
        border-radius: 4px;
        background: transparent;
        color: var(--text-primary);
        font-family: var(--font-family, system-ui);
        font-size: 12px;
        cursor: default;
        width: 100%;
        text-align: left;
      }

      .context-menu-item:hover {
        background: rgba(0, 0, 0, 0.06);
      }

      .context-menu-item.danger:hover {
        background: rgba(220, 60, 60, 0.10);
        color: rgb(200, 50, 50);
      }

      @media (prefers-color-scheme: dark) {
        .context-menu {
          background: rgba(70, 76, 82, 0.92);
          border-color: rgba(255, 255, 255, 0.14);
          box-shadow: 0 4px 16px rgba(0, 0, 0, 0.40);
        }
        .context-menu-item:hover {
          background: rgba(255, 255, 255, 0.08);
        }
        .context-menu-item.danger:hover {
          background: rgba(239, 68, 68, 0.22);
          color: rgb(248, 140, 140);
        }
      }
    `;
  }

  static override get properties() {
    return {
      folder: {type: Object},
      matchedTabs: {type: Array},
      sessionId: {type: Number},
      isRenaming_: {type: Boolean},
      showContextMenu_: {type: Boolean},
      contextMenuX_: {type: Number},
      contextMenuY_: {type: Number},
      childDropIndex_: {type: Number},
    };
  }

  folder: FolderData = {
    type: 'folder',
    id: '',
    name: '',
    collapsed: false,
    children: [],
  };
  matchedTabs: TabData[] = [];
  sessionId: number = 0;

  protected isRenaming_: boolean = false;
  protected showContextMenu_: boolean = false;
  protected contextMenuX_: number = 0;
  protected contextMenuY_: number = 0;
  protected childDropIndex_: number = -1;

  private boundOnDocumentClick_: ((e: MouseEvent) => void) | null = null;

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.hideContextMenu_();
  }

  override render() {
    const isExpanded = !this.folder.collapsed;
    const childCount = this.matchedTabs.length;

    // Compute max-height for animation: each tab item is ~40px.
    const maxHeight = isExpanded ? `${childCount * 44 + 8}px` : '0';

    return html`
      <div class="folder-row"
           draggable="true"
           @click=${this.onToggleCollapse_}
           @dblclick=${this.onStartRename_}
           @contextmenu=${this.onContextMenu_}
           @dragstart=${this.onFolderDragStart_}
           @dragover=${this.onDragOver_}
           @dragleave=${this.onDragLeave_}
           @drop=${this.onDrop_}>
        <svg class="chevron ${isExpanded ? 'expanded' : ''}"
             viewBox="0 0 24 24" fill="none"
             stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
          <polyline points="9 18 15 12 9 6"></polyline>
        </svg>
        <svg class="folder-icon" viewBox="0 0 24 24" fill="none"
             stroke="currentColor" stroke-width="2"
             stroke-linecap="round" stroke-linejoin="round">
          <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path>
        </svg>
        ${this.isRenaming_ ? html`
          <input class="rename-input"
                 .value=${this.folder.name}
                 @keydown=${this.onRenameKeydown_}
                 @blur=${this.onRenameBlur_}
                 @click=${(e: Event) => e.stopPropagation()}>
        ` : html`
          <span class="folder-name">${this.folder.name}</span>
        `}
        <span class="count">${childCount > 0 ? childCount : ''}</span>
      </div>

      <div class="children-container ${this.folder.collapsed ? 'collapsed' : ''}"
           style="max-height: ${maxHeight}">
        <div class="children-inner"
             @dragover=${this.onChildDragOver_}
             @dragleave=${this.onChildDragLeave_}
             @drop=${this.onChildDrop_}>
          ${this.matchedTabs.map((tab, i) => html`
            <div class="child-drop-indicator
                        ${this.childDropIndex_ === i ? 'visible' : ''}"></div>
            <dao-tab-item
              .tabData=${tab}
              .sessionId=${this.sessionId}
              ?active=${tab.isActive}
              data-folder-id=${this.folder.id}>
            </dao-tab-item>
          `)}
          <div class="child-drop-indicator
                      ${this.childDropIndex_ === this.matchedTabs.length ?
                        'visible' : ''}"></div>
        </div>
      </div>

      ${this.showContextMenu_ ? html`
        <div class="context-menu"
             style="left: ${this.contextMenuX_}px; top: ${this.contextMenuY_}px">
          <button class="context-menu-item"
                  @click=${this.onContextRename_}>
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round">
              <path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path>
              <path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path>
            </svg>
            Rename
          </button>
          <button class="context-menu-item danger"
                  @click=${this.onContextDelete_}>
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round">
              <polyline points="3 6 5 6 21 6"></polyline>
              <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
            </svg>
            Delete Folder
          </button>
        </div>
      ` : nothing}
    `;
  }

  override updated() {
    // Auto-focus rename input when entering rename mode.
    if (this.isRenaming_) {
      const input = this.shadowRoot!.querySelector(
          '.rename-input') as HTMLInputElement | null;
      if (input) {
        input.focus();
        input.select();
      }
    }
  }

  /**
   * Programmatically enter rename mode (used for newly created folders).
   */
  startRename() {
    this.isRenaming_ = true;
  }

  private onFolderDragStart_(e: DragEvent) {
    if (!e.dataTransfer) return;
    e.dataTransfer.setData(FOLDER_MIME_TYPE, this.folder.id);
    e.dataTransfer.effectAllowed = 'move';
  }

  private dispatchFolderAction_(detail: FolderAction) {
    this.dispatchEvent(new CustomEvent('folder-action', {
      bubbles: true,
      composed: true,
      detail,
    }));
  }

  private onToggleCollapse_(e: Event) {
    // Don't toggle if we're renaming.
    if (this.isRenaming_) return;
    e.stopPropagation();
    this.dispatchFolderAction_({action: 'toggleCollapse', folderId: this.folder.id});
  }

  private onStartRename_(e: Event) {
    e.stopPropagation();
    this.isRenaming_ = true;
  }

  private onRenameKeydown_(e: KeyboardEvent) {
    if (e.key === 'Enter') {
      this.commitRename_(e);
    } else if (e.key === 'Escape') {
      this.isRenaming_ = false;
    }
  }

  private onRenameBlur_(e: FocusEvent) {
    this.commitRename_(e);
  }

  private commitRename_(e: Event) {
    const input = e.target as HTMLInputElement;
    const newName = input.value.trim();
    this.isRenaming_ = false;
    if (newName && newName !== this.folder.name) {
      this.dispatchFolderAction_(
          {action: 'rename', folderId: this.folder.id, name: newName});
    }
  }

  private onContextMenu_(e: MouseEvent) {
    e.preventDefault();
    e.stopPropagation();

    // Clamp position so the menu stays within the viewport.
    // Approximate menu size: 140px wide, 68px tall (2 items).
    const menuW = 148;
    const menuH = 68;
    const x = Math.min(e.clientX, window.innerWidth - menuW);
    const y = Math.min(e.clientY, window.innerHeight - menuH);
    this.contextMenuX_ = Math.max(0, x);
    this.contextMenuY_ = Math.max(0, y);
    this.showContextMenu_ = true;

    // Close on next click anywhere.
    this.boundOnDocumentClick_ = () => this.hideContextMenu_();
    setTimeout(() => {
      document.addEventListener('click', this.boundOnDocumentClick_!);
    }, 0);
  }

  private hideContextMenu_() {
    this.showContextMenu_ = false;
    if (this.boundOnDocumentClick_) {
      document.removeEventListener('click', this.boundOnDocumentClick_);
      this.boundOnDocumentClick_ = null;
    }
  }

  private onContextRename_(e: Event) {
    e.stopPropagation();
    this.hideContextMenu_();
    this.isRenaming_ = true;
  }

  private onContextDelete_(e: Event) {
    e.stopPropagation();
    this.hideContextMenu_();
    this.dispatchFolderAction_({action: 'delete', folderId: this.folder.id});
  }

  // ---- Drag-and-drop: folder is a drop target for tabs ----

  private onDragOver_(e: DragEvent) {
    if (!e.dataTransfer) return;
    if (!e.dataTransfer.types.includes('text/plain')) return;
    e.preventDefault();
    e.stopPropagation();
    e.dataTransfer.dropEffect = 'move';
    const row = this.shadowRoot!.querySelector('.folder-row');
    if (row) row.classList.add('drag-over');
  }

  private onDragLeave_(_e: DragEvent) {
    const row = this.shadowRoot!.querySelector('.folder-row');
    if (row) row.classList.remove('drag-over');
  }

  private onDrop_(e: DragEvent) {
    const row = this.shadowRoot!.querySelector('.folder-row');
    if (row) row.classList.remove('drag-over');

    if (!e.dataTransfer) return;
    const data = e.dataTransfer.getData('text/plain');
    if (!data.startsWith(TAB_DRAG_PREFIX)) return;

    e.preventDefault();
    e.stopPropagation();

    this.dispatchFolderAction_(
        {action: 'tabDrop', folderId: this.folder.id, dragData: data});
  }

  // ---- Drag-and-drop within folder children area ----

  private onChildDragOver_(e: DragEvent) {
    if (!e.dataTransfer) return;
    if (!e.dataTransfer.types.includes('text/plain')) return;
    e.preventDefault();
    e.stopPropagation();
    e.dataTransfer.dropEffect = 'move';

    // Compute which slot the cursor is closest to.
    const items = this.shadowRoot!.querySelectorAll(
        '.children-inner dao-tab-item');
    const y = e.clientY;
    let insertAt = this.matchedTabs.length;  // default: after last

    for (let i = 0; i < items.length; i++) {
      const rect = (items[i] as HTMLElement).getBoundingClientRect();
      if (y < rect.top + rect.height / 2) {
        insertAt = i;
        break;
      }
    }

    this.childDropIndex_ = insertAt;
  }

  private onChildDragLeave_(e: DragEvent) {
    const related = e.relatedTarget as Node | null;
    const inner = this.shadowRoot!.querySelector('.children-inner');
    if (related && inner && inner.contains(related)) return;
    this.childDropIndex_ = -1;
  }

  private onChildDrop_(e: DragEvent) {
    const dropIndex = this.childDropIndex_;
    this.childDropIndex_ = -1;

    if (!e.dataTransfer) return;
    const data = e.dataTransfer.getData('text/plain');
    if (!data.startsWith(TAB_DRAG_PREFIX)) return;

    e.preventDefault();
    e.stopPropagation();

    this.dispatchFolderAction_({
      action: 'childReorder',
      folderId: this.folder.id,
      dragData: data,
      dropIndex,
    });
  }
}

customElements.define('dao-folder-item', DaoFolderItem);
