// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Bridge between sidebar WebUI JavaScript and C++ via chrome.send().
// Implements the cr namespace in-process (same pattern as Chromium's cr.ts)
// to avoid chrome:// import resolution issues with TypeScript.

// ---- WebUI Listener Infrastructure ----
// (mirrors Chromium's cr.ts: addWebUiListener / webUIListenerCallback)

interface WebUiListener {
  eventName: string;
  uid: number;
}

let uidCounter = 1;
const webUiListenerMap:
    Record<string, Record<number, (...args: unknown[]) => void>> = {};

export const SIDEBAR_POINTER_EXITED_EVENT =
    'dao-sidebar-pointer-exited';

function webUIListenerCallback(event: string, ...args: unknown[]): void {
  const listeners = webUiListenerMap[event];
  if (!listeners) return;
  for (const id in listeners) {
    listeners[id]!(...args);
  }
}

// Expose on window.cr for C++ to call (FireWebUIListener → webUIListenerCallback).
const w = window as unknown as
    {cr?: {webUIListenerCallback: typeof webUIListenerCallback}};
(w as {cr: object}).cr = {webUIListenerCallback};

// ---- Chrome.send Bridge ----

/**
 * Send a fire-and-forget message to C++ (no response expected).
 */
export function sendNative(method: string, ...args: unknown[]): void {
  chrome.send(method, args);
}

/**
 * Listen for events pushed from C++ via FireWebUIListener.
 */
export function addListener(
    event: string,
    callback: (...args: unknown[]) => void): WebUiListener {
  webUiListenerMap[event] = webUiListenerMap[event] || {};
  const uid = uidCounter++;
  webUiListenerMap[event]![uid] = callback;
  return {eventName: event, uid};
}

/**
 * Remove a listener previously added with addListener.
 */
export function removeListener(listener: WebUiListener): boolean {
  const map = webUiListenerMap[listener.eventName];
  if (map && map[listener.uid]) {
    delete map[listener.uid];
    return true;
  }
  return false;
}

// ---- Tab Data Types ----

export interface TabData {
  tabId: string;
  index: number;
  title: string;
  url: string;
  faviconUrl: string;
  isActive: boolean;
  isPinned: boolean;
  isAudible: boolean;
  isMuted: boolean;
  isAgentLocked?: boolean;
  isMcpControlled?: boolean;
  isInSplit?: boolean;
  isFaviconLight?: boolean;
  lastActiveTimeMs?: number;
}

export type PinnedItemState = 'open'|'dormant'|'reconciling';

export interface PinnedItemData {
  id: string;
  state: PinnedItemState;
  title: string;
  url: string;
  faviconUrl: string;
  isOpen: boolean;
  openTabIndex: number;
  openTabId?: string;
  isActive: boolean;
  isFaviconLight?: boolean;
}

export interface SidebarState {
  pinnedItems: PinnedItemData[];
  pinnedTabs: TabData[];
  unpinnedTabs: TabData[];
  activeIndex: number;
  sessionId: number;
  scrollTargetTabId?: string;
}

// ---- Download Data Types ----

export interface RecentFileData {
  index: number;
  name: string;
  iconUrl: string;
  hasThumbnail: boolean;
}

export interface ActiveDownloadData {
  id: number;
  name: string;
  percent: number;
  speed: string;
  sizeDetail: string;
  statusDetail: string;
}

export interface DownloadState {
  recentFiles: RecentFileData[];
  activeDownloads: ActiveDownloadData[];
}

// ---- Application Update Data Types ----

export type UpdateState = 'idle' | 'ready' | 'applying' | 'unsupported';
export interface UpdateStateData {
  state: UpdateState;
  displayVersion: string;
  label: string;
  applyingLabel: string;
}

// ---- Media Playback Data Types ----

export interface MediaPlaybackState {
  isPlaying: boolean;
  tabIndex: number;
  title: string;
  sourceTitle: string;
  faviconUrl: string;
  isMuted: boolean;
  hasPrev: boolean;
  hasNext: boolean;
}

// ---- Folder Data Types ----

export interface FolderData {
  type: 'folder';
  id: string;
  name: string;
  collapsed: boolean;
  children: SidebarTabRef[];
}

export interface SidebarTabRef {
  type: 'tab';
  tabId?: string;
  url: string;
  title: string;
}

export type SidebarItem = SidebarTabRef | FolderData;

export interface FolderFileData {
  version: number;
  items: SidebarItem[];
}

// ---- Drag-and-Drop Constants ----

export const TAB_DRAG_PREFIX = 'dao-tab-drag:';
export const TAB_DRAG_MIME_TYPE = 'application/x-dao-tab-drag';
export const FOLDER_MIME_TYPE = 'application/x-dao-folder';
export const PINNED_ITEM_DRAG_MIME_TYPE =
    'application/x-dao-pinned-item-id';

let activePinnedItemDragId = '';

export function setActivePinnedItemDragId(id: string) {
  activePinnedItemDragId = id;
}

export function getActivePinnedItemDragId(): string {
  return activePinnedItemDragId;
}

export function clearActivePinnedItemDragId() {
  activePinnedItemDragId = '';
}

/**
 * Parse a window/tab drag, optionally carrying its stable tab identity.
 */
export function parseTabDragData(data: string):
    {sessionId: number; tabIndex: number; tabId?: string} | null {
  if (!data.startsWith(TAB_DRAG_PREFIX)) return null;
  const parts = data.substring(TAB_DRAG_PREFIX.length).split(':');
  if (parts.length !== 2 && parts.length !== 3) return null;
  if (!/^\d+$/.test(parts[0]!) || !/^\d+$/.test(parts[1]!)) return null;
  const sessionId = Number(parts[0]);
  const tabIndex = Number(parts[1]);
  if (sessionId <= 0 || sessionId > 0x7fffffff || tabIndex > 0x7fffffff) {
    return null;
  }
  const tabId = parts[2];
  if (tabId !== undefined && (!tabId || /\s/.test(tabId))) return null;
  return tabId === undefined ? {sessionId, tabIndex} :
      {sessionId, tabIndex, tabId};
}

// ---- Folder Action Types (discriminated union) ----

export type FolderAction =
  | {action: 'toggleCollapse'; folderId: string}
  | {action: 'rename'; folderId: string; name: string}
  | {action: 'unfolder'; folderId: string}
  | {action: 'tabDrop'; folderId: string; dragData: string}
  | {action: 'childReorder'; folderId: string; dragData: string;
     dropIndex: number}
  | {action: 'removeFromFolder'; folderId: string; tabId: string;
     toModelIndex?: number}
  | {action: 'reorderModel'; tabId: string; toModelIndex: number}
  | {action: 'reorderFolder'; folderId: string; toModelIndex: number};

// ---- Request-Response Bridge ----

let asyncCallbackId = 0;

/**
 * Send a message to C++ and return a Promise that resolves with the response.
 * The C++ handler must call FireWebUIListener with the callback ID.
 */
export function sendNativeAsync<T>(method: string, ...args: unknown[]): Promise<T> {
  return new Promise<T>((resolve) => {
    const callbackId = `${method}_${asyncCallbackId++}`;
    const listener = addListener(callbackId, (...responseArgs: unknown[]) => {
      removeListener(listener);
      resolve(responseArgs[0] as T);
    });
    chrome.send(method, [callbackId, ...args]);
  });
}

// ---- Folder Bridge Functions ----

/**
 * Load folder data from dao_folders.json via C++.
 * Returns the raw JSON string, or empty string if file doesn't exist.
 */
export function loadFolders(): Promise<string> {
  return sendNativeAsync<string>('loadFolders');
}

/**
 * Submit each edit with its base snapshot. Native code merges the delta into
 * the latest profile file on a sequenced writer. Do not debounce here: another
 * window's refresh must never replace an edit that has not been submitted.
 */
export function saveFolders(json: string, baseJson: string = ''): void {
  sendNative('saveFolders', json, baseJson);
}
