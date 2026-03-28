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
  index: number;
  title: string;
  url: string;
  faviconUrl: string;
  isActive: boolean;
  isPinned: boolean;
  isAudible: boolean;
  isMuted: boolean;
  isAgentLocked?: boolean;
}

export interface SidebarState {
  pinnedTabs: TabData[];
  unpinnedTabs: TabData[];
  activeIndex: number;
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
}

export interface DownloadState {
  recentFiles: RecentFileData[];
  activeDownloads: ActiveDownloadData[];
}
