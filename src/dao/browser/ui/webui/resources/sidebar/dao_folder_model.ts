// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {
  FolderData, FolderFileData, SidebarItem, SidebarTabRef, TabData
} from './sidebar_bridge.js';

/**
 * Generates a short unique folder ID using crypto.randomUUID().
 * Takes the first 8 characters for a compact identifier.
 */
function generateFolderId(): string {
  return crypto.randomUUID().substring(0, 8);
}

/**
 * Reorder an element within an array from one index to another.
 */
function reorderArray<T>(arr: T[], fromIndex: number, toIndex: number): void {
  if (fromIndex < 0 || fromIndex >= arr.length) return;
  if (toIndex < 0 || toIndex > arr.length) return;
  if (fromIndex === toIndex) return;
  const [item] = arr.splice(fromIndex, 1);
  if (!item) return;
  const adjustedTo = fromIndex < toIndex ? toIndex - 1 : toIndex;
  arr.splice(adjustedTo, 0, item);
}

/**
 * Pure data layer managing the sidebar items tree (folders + tab refs).
 * All folder logic lives here — components call methods and re-render
 * from getOrderedItems().
 */
export class FolderModel {
  private items_: SidebarItem[] = [];
  private version_: number = 1;

  /**
   * Parse stored JSON data into the items tree.
   * Returns true if data was loaded successfully, false otherwise.
   */
  loadFromJson(json: string): boolean {
    if (!json || json.trim() === '') {
      this.items_ = [];
      return false;
    }
    try {
      const data = JSON.parse(json) as FolderFileData;
      if (data.version && Array.isArray(data.items)) {
        this.version_ = data.version;
        this.items_ = data.items;
        return true;
      }
    } catch (e) {
      console.error('FolderModel: failed to parse folder data', e);
    }
    this.items_ = [];
    return false;
  }

  /**
   * Serialize the items tree to JSON for persistence.
   */
  toJson(): string {
    const data: FolderFileData = {
      version: this.version_,
      items: this.items_,
    };
    return JSON.stringify(data, null, 2);
  }

  /**
   * Create a new folder with the given name, appended to the items list.
   * Returns the new folder data.
   */
  addFolder(name: string): FolderData {
    const folder: FolderData = {
      type: 'folder',
      id: generateFolderId(),
      name,
      collapsed: false,
      children: [],
    };
    this.items_.push(folder);
    return folder;
  }

  /**
   * Delete a folder by ID. Child tabs become loose tabs at the
   * folder's position in the items array.
   */
  deleteFolder(folderId: string): void {
    const index = this.items_.findIndex(
        item => item.type === 'folder' && item.id === folderId);
    if (index === -1) return;

    const folder = this.items_[index] as FolderData;
    // Replace folder with its children (released as loose tabs).
    this.items_.splice(index, 1, ...folder.children);
  }

  /**
   * Rename a folder by ID.
   */
  renameFolder(folderId: string, name: string): void {
    const folder = this.findFolder_(folderId);
    if (folder) {
      folder.name = name;
    }
  }

  /**
   * Toggle the collapsed state of a folder.
   */
  toggleCollapse(folderId: string): void {
    const folder = this.findFolder_(folderId);
    if (folder) {
      folder.collapsed = !folder.collapsed;
    }
  }

  /**
   * Move a tab (identified by URL + title match at a flat position)
   * into a folder. The tab is removed from its current position and
   * appended to the folder's children. If the folder is collapsed,
   * it is auto-expanded.
   *
   * @param tabUrl  URL of the tab to move
   * @param tabTitle  Title of the tab to move
   * @param folderId  Target folder ID
   * @param sourceFolderId  If the tab is already in a folder, its ID
   */
  moveTabToFolder(
      tabUrl: string, tabTitle: string, folderId: string,
      sourceFolderId?: string): void {
    const targetFolder = this.findFolder_(folderId);
    if (!targetFolder) return;

    // Remove from source location.
    let removed: SidebarTabRef | null = null;
    if (sourceFolderId) {
      const sourceFolder = this.findFolder_(sourceFolderId);
      if (sourceFolder) {
        const childIdx = sourceFolder.children.findIndex(
            c => c.url === tabUrl && c.title === tabTitle);
        if (childIdx !== -1) {
          removed = sourceFolder.children.splice(childIdx, 1)[0]!;
        }
      }
    } else {
      // Remove from top-level items.
      const idx = this.items_.findIndex(
          item => item.type === 'tab' &&
              item.url === tabUrl && item.title === tabTitle);
      if (idx !== -1) {
        removed = this.items_.splice(idx, 1)[0] as SidebarTabRef;
      }
    }

    if (!removed) {
      // Tab not found in model — create a new ref.
      removed = {type: 'tab', url: tabUrl, title: tabTitle};
    }

    targetFolder.children.push(removed);

    // Auto-expand the folder.
    if (targetFolder.collapsed) {
      targetFolder.collapsed = false;
    }
  }

  /**
   * Remove a tab from a folder, placing it as a loose tab immediately
   * after the folder in the items array.
   *
   * @param tabUrl  URL of the tab
   * @param tabTitle  Title of the tab
   * @param folderId  Source folder ID
   * @param insertIndex  Optional: top-level items index to insert at
   */
  removeTabFromFolder(
      tabUrl: string, tabTitle: string, folderId: string,
      insertIndex?: number): void {
    const folder = this.findFolder_(folderId);
    if (!folder) return;

    const childIdx = folder.children.findIndex(
        c => c.url === tabUrl && c.title === tabTitle);
    if (childIdx === -1) return;

    const removed = folder.children.splice(childIdx, 1)[0]!;

    if (insertIndex !== undefined && insertIndex >= 0) {
      const clamped = Math.min(insertIndex, this.items_.length);
      this.items_.splice(clamped, 0, removed);
    } else {
      // Insert immediately after the folder.
      const folderIdx = this.items_.indexOf(folder);
      this.items_.splice(folderIdx + 1, 0, removed);
    }
  }

  /**
   * Reorder a child within a folder from one position to another.
   */
  reorderWithinFolder(
      folderId: string, fromIndex: number, toIndex: number): void {
    const folder = this.findFolder_(folderId);
    if (folder) {
      reorderArray(folder.children, fromIndex, toIndex);
    }
  }

  /**
   * Reorder a top-level item from one index to another.
   * Used for drag-drop reordering of folders and loose tabs.
   */
  reorder(fromIndex: number, toIndex: number): void {
    reorderArray(this.items_, fromIndex, toIndex);
  }

  /**
   * Reconcile stored folder data with actual browser tabs after
   * session restore. Matches stored tab refs to actual tabs by URL,
   * consuming each match once. Unmatched actual tabs become loose
   * tabs appended at the end. Stored refs with no match are discarded.
   */
  reconcile(actualTabs: TabData[]): void {
    // Build a pool of available actual tabs (URL -> list of TabData).
    const pool = new Map<string, TabData[]>();
    for (const tab of actualTabs) {
      const list = pool.get(tab.url) || [];
      list.push(tab);
      pool.set(tab.url, list);
    }

    // Consume helper: find and remove a matching tab from the pool.
    const consume = (url: string): TabData | null => {
      const list = pool.get(url);
      if (!list || list.length === 0) return null;
      return list.shift()!;
    };

    // Walk the stored items tree, matching each tab ref to an actual tab.
    const newItems: SidebarItem[] = [];

    for (const item of this.items_) {
      if (item.type === 'tab') {
        const matched = consume(item.url);
        if (matched) {
          // Update title from actual tab (may have changed).
          newItems.push({
            type: 'tab',
            url: matched.url,
            title: matched.title,
          });
        }
        // If no match, discard this stored entry.
      } else if (item.type === 'folder') {
        const folder = item as FolderData;
        const newChildren: SidebarTabRef[] = [];
        for (const child of folder.children) {
          const matched = consume(child.url);
          if (matched) {
            newChildren.push({
              type: 'tab',
              url: matched.url,
              title: matched.title,
            });
          }
          // If no match, discard this stored child entry.
        }
        // Only keep the folder if it still has matched children.
        // Folders whose children all belong to other windows are dropped.
        if (newChildren.length > 0) {
          newItems.push({
            type: 'folder',
            id: folder.id,
            name: folder.name,
            collapsed: folder.collapsed,
            children: newChildren,
          });
        }
      }
    }

    // Remaining unmatched actual tabs become loose tabs at the end.
    for (const [, list] of pool) {
      for (const tab of list) {
        newItems.push({
          type: 'tab',
          url: tab.url,
          title: tab.title,
        });
      }
    }

    this.items_ = newItems;
  }

  /**
   * Return the items tree for rendering.
   */
  getOrderedItems(): SidebarItem[] {
    return this.items_;
  }

  /**
   * Return all folders in the model.
   */
  getFolders(): FolderData[] {
    return this.items_.filter(
        item => item.type === 'folder') as FolderData[];
  }

  /**
   * Check if the model has any data (folders or organized tabs).
   */
  hasData(): boolean {
    return this.items_.length > 0;
  }

  /**
   * Find which folder contains a tab with the given URL and title.
   * Returns the folder ID or null if the tab is not in any folder.
   */
  findTabFolder(url: string, title: string): string | null {
    for (const item of this.items_) {
      if (item.type !== 'folder') continue;
      const folder = item as FolderData;
      for (const child of folder.children) {
        if (child.url === url && child.title === title) {
          return folder.id;
        }
      }
    }
    return null;
  }

  /**
   * Find a folder by ID in the items array.
   */
  private findFolder_(folderId: string): FolderData | null {
    const item = this.items_.find(
        i => i.type === 'folder' && i.id === folderId);
    return item ? item as FolderData : null;
  }
}
