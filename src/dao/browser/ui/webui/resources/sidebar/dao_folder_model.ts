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

  private matchesTabRef_(
      ref: SidebarTabRef,
      tab: Pick<TabData, 'tabId'|'url'|'title'>): boolean {
    if (ref.tabId && tab.tabId) {
      return ref.tabId === tab.tabId;
    }
    return ref.url === tab.url && ref.title === tab.title;
  }

  private toTabRef_(tab: Pick<TabData, 'tabId'|'url'|'title'>): SidebarTabRef {
    return {
      type: 'tab',
      tabId: tab.tabId,
      url: tab.url,
      title: tab.title,
    };
  }

  private toPersistedItem_(item: SidebarItem): SidebarItem {
    if (item.type === 'tab') {
      return {
        type: 'tab',
        url: item.url,
        title: item.title,
      };
    }

    return {
      type: 'folder',
      id: item.id,
      name: item.name,
      collapsed: item.collapsed,
      children: item.children.map(child => ({
        type: 'tab',
        url: child.url,
        title: child.title,
      })),
    };
  }

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
      items: this.items_.map(item => this.toPersistedItem_(item)),
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
   * Move a tab (identified by stable tabId, with URL + title fallback)
   * into a folder. The tab is removed from its current position and
   * appended to the folder's children. If the folder is collapsed,
   * it is auto-expanded.
   *
   * @param tab  Runtime tab identity to move
   * @param folderId  Target folder ID
   * @param sourceFolderId  If the tab is already in a folder, its ID
   */
  moveTabToFolder(
      tab: Pick<TabData, 'tabId'|'url'|'title'>, folderId: string,
      sourceFolderId?: string): void {
    const targetFolder = this.findFolder_(folderId);
    if (!targetFolder) return;

    // Remove from source location.
    let removed: SidebarTabRef | null = null;
    if (sourceFolderId) {
      const sourceFolder = this.findFolder_(sourceFolderId);
      if (sourceFolder) {
        const childIdx = sourceFolder.children.findIndex(
            c => this.matchesTabRef_(c, tab));
        if (childIdx !== -1) {
          removed = sourceFolder.children.splice(childIdx, 1)[0]!;
        }
      }
    } else {
      // Remove from top-level items.
      const idx = this.items_.findIndex(
          item => item.type === 'tab' &&
              this.matchesTabRef_(item, tab));
      if (idx !== -1) {
        removed = this.items_.splice(idx, 1)[0] as SidebarTabRef;
      }
    }

    if (!removed) {
      // Tab not found in model — create a new ref.
      removed = this.toTabRef_(tab);
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
   * @param tab  Runtime tab identity to remove
   * @param folderId  Source folder ID
   * @param insertIndex  Optional: top-level items index to insert at
   */
  removeTabFromFolder(
      tab: Pick<TabData, 'tabId'|'url'|'title'>, folderId: string,
      insertIndex?: number): void {
    const folder = this.findFolder_(folderId);
    if (!folder) return;

    const childIdx = folder.children.findIndex(
        c => this.matchesTabRef_(c, tab));
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
   * session restore. Matches stored tab refs to actual tabs by stable
   * tabId first, then falls back to URL. Unmatched actual tabs become loose
   * tabs appended at the end. Stored refs with no match are discarded.
   */
  reconcile(actualTabs: TabData[]): void {
    const remaining = [...actualTabs];

    const consume = (ref: SidebarTabRef): TabData | null => {
      let idx = ref.tabId ?
          remaining.findIndex(tab => tab.tabId === ref.tabId) : -1;
      if (idx === -1) {
        idx = remaining.findIndex(tab => tab.url === ref.url);
      }
      if (idx === -1) return null;
      return remaining.splice(idx, 1)[0]!;
    };

    // Walk the stored items tree, matching each tab ref to an actual tab.
    const newItems: SidebarItem[] = [];

    for (const item of this.items_) {
      if (item.type === 'tab') {
        const matched = consume(item);
        if (matched) {
          newItems.push(this.toTabRef_(matched));
        }
        // If no match, discard this stored entry.
      } else if (item.type === 'folder') {
        const folder = item as FolderData;
        const newChildren: SidebarTabRef[] = [];
        for (const child of folder.children) {
          const matched = consume(child);
          if (matched) {
            newChildren.push(this.toTabRef_(matched));
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

    // Remaining unmatched actual tabs — insert near their tab-strip
    // neighbor so that e.g. duplicated tabs appear next to the original
    // rather than at the very bottom.
    for (const tab of remaining) {
      const actualIdx = actualTabs.indexOf(tab);
      let inserted = false;

      if (actualIdx === 0) {
        newItems.unshift(this.toTabRef_(tab));
        inserted = true;
      } else if (actualIdx > 0) {
        const pred = actualTabs[actualIdx - 1]!;
        for (let i = 0; i < newItems.length; i++) {
          const item = newItems[i]!;
          if (item.type === 'tab' &&
              this.matchesTabRef_(item as SidebarTabRef, pred)) {
            newItems.splice(i + 1, 0, this.toTabRef_(tab));
            inserted = true;
            break;
          }
          if (item.type === 'folder') {
            const folder = item as FolderData;
            if (folder.children.some(
                    c => this.matchesTabRef_(c, pred))) {
              newItems.splice(i + 1, 0, this.toTabRef_(tab));
              inserted = true;
              break;
            }
          }
        }
      }

      if (!inserted) {
        newItems.push(this.toTabRef_(tab));
      }
    }

    this.items_ = newItems;

    // Bring split-group siblings together using actualTabs as the source of
    // truth. C++ side already moved split tabs to adjacent indices via
    // PlaceGroupAroundAnchor; reconcile would otherwise re-emit them in
    // stored (stale) order and the visual grouping would not appear.
    this.consolidateSplitGroups_(actualTabs);
  }

  /**
   * After reconcile, walk actualTabs to find each split-group run (consecutive
   * tabs with isInSplit=true). For each run, locate its members at the top
   * level of items_ and move the trailing members to sit immediately after
   * the first member, preserving the actualTabs order. Folder-bound members
   * are left in place — only loose, top-level tabs get reordered.
   */
  private consolidateSplitGroups_(actualTabs: TabData[]): void {
    // Fast path: reconcile runs on every sidebarStateChanged push (titles,
    // favicons). Skip the full scan when no split tabs exist — the common case.
    if (!actualTabs.some(t => t.isInSplit)) return;

    const indexOfRunMember = (member: TabData): number =>
        this.items_.findIndex(
            it => it.type === 'tab' &&
                  this.matchesTabRef_(it as SidebarTabRef, member));

    let i = 0;
    while (i < actualTabs.length) {
      if (!actualTabs[i]!.isInSplit) {
        i++;
        continue;
      }
      let j = i;
      const run: TabData[] = [];
      while (j < actualTabs.length && actualTabs[j]!.isInSplit) {
        run.push(actualTabs[j]!);
        j++;
      }
      i = j;
      if (run.length < 2) continue;

      const anchorIdx = indexOfRunMember(run[0]!);
      if (anchorIdx === -1) continue;  // anchor lives in a folder, skip

      let insertAfter = anchorIdx;
      for (let k = 1; k < run.length; k++) {
        const memberIdx = indexOfRunMember(run[k]!);
        if (memberIdx === -1) continue;  // in a folder
        const targetIdx = insertAfter + 1;
        if (memberIdx === targetIdx) {
          insertAfter = targetIdx;
          continue;
        }
        reorderArray(this.items_, memberIdx, targetIdx);
        insertAfter = memberIdx < targetIdx ? targetIdx - 1 : targetIdx;
      }
    }
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
   * Find which folder contains the given tab.
   * Returns the folder ID or null if the tab is not in any folder.
   */
  findTabFolder(tab: Pick<TabData, 'tabId'|'url'|'title'>): string | null {
    for (const item of this.items_) {
      if (item.type !== 'folder') continue;
      const folder = item as FolderData;
      for (const child of folder.children) {
        if (this.matchesTabRef_(child, tab)) {
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
