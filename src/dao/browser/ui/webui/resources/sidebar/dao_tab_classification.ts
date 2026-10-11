// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {createTabRefMatchPool, FolderModel} from './dao_folder_model.js';
import type {TabData} from './sidebar_bridge.js';

export interface ClassificationFolder {id: string; name: string;}
interface ClassificationGroup {
  folderId: string;
  name: string;
  tabIds: string[];
}

function eligible(tab: TabData): boolean {
  return !tab.isPinned && !tab.isInSplit && !tab.isAgentLocked &&
      !tab.isMcpControlled && /^https?:\/\//i.test(tab.url);
}

export function getClassificationTabs(
    model: FolderModel, tabs: TabData[], belowTabId?: string): TabData[] {
  const items = model.getOrderedItems();
  const {consume, remaining} = createTabRefMatchPool(items, tabs);
  const result: TabData[] = [];
  let below = belowTabId === undefined;
  const visit = (tab: TabData|null, inFolder: boolean) => {
    if (!tab) return;
    if (below && !inFolder && eligible(tab)) result.push(tab);
    if (tab.tabId === belowTabId) below = true;
  };
  for (const item of items) {
    if (item.type === 'folder') {
      for (const child of item.children) visit(consume(child), true);
    } else {
      visit(consume(item), false);
    }
  }
  for (const tab of remaining) visit(tab, false);
  return result;
}

export function applyTabClassification(
    model: FolderModel, currentTabs: TabData[], requestedTabs: TabData[],
    offeredFolders: ClassificationFolder[], output: unknown): number {
  if (!Array.isArray(output) || !output.length ||
      output.length > requestedTabs.length) {
    throw new Error('Invalid classification');
  }
  const requested = new Set(requestedTabs.map(tab => tab.tabId));
  const assigned = new Set<string>();
  const names = new Set<string>();
  const groups: ClassificationGroup[] = [];
  for (const value of output) {
    if (!value || typeof value !== 'object' ||
        typeof value.folderId !== 'string' || typeof value.name !== 'string' ||
        !value.name.trim() || (!value.folderId && [...value.name.trim()].length > 100) ||
        !Array.isArray(value.tabIds) || !value.tabIds.length) {
      throw new Error('Invalid classification');
    }
    const name = value.name.trim();
    const normalized = name.toLowerCase();
    if (names.has(normalized)) throw new Error('Duplicate classification');
    names.add(normalized);
    const offered = offeredFolders.find(folder => value.folderId ?
        folder.id === value.folderId :
        folder.name.trim().toLowerCase() === normalized);
    if (value.folderId && (!offered || offered.name !== value.name)) {
      throw new Error('Unknown classification');
    }
    if (offered) {
      const current = model.getFolders().find(folder => folder.id === offered.id);
      if (!current || current.name !== offered.name) {
        throw new Error('Folders changed');
      }
    }
    for (const id of value.tabIds) {
      if (typeof id !== 'string' || !requested.has(id) || assigned.has(id)) {
        throw new Error('Invalid classification tab');
      }
      assigned.add(id);
    }
    groups.push({folderId: offered?.id ?? value.folderId, name, tabIds: value.tabIds});
  }
  if (assigned.size !== requested.size) throw new Error('Missing tabs');

  const available = new Map(getClassificationTabs(model, currentTabs)
      .map(tab => [tab.tabId, tab]));
  let moved = 0;
  for (const group of groups) {
    const ids = new Set(group.tabIds);
    const tabs = requestedTabs.flatMap(original => {
      const current = available.get(original.tabId);
      return ids.has(original.tabId) && current && current.url === original.url ?
          [current] : [];
    });
    if (!tabs.length) continue;
    const folder = model.getFolders().find(candidate => group.folderId ?
        candidate.id === group.folderId :
        candidate.name.trim().toLowerCase() === group.name.toLowerCase()) ??
        model.addFolder(group.name);
    model.moveTabsToFolder(tabs, folder.id);
    moved += tabs.length;
  }
  return moved;
}
