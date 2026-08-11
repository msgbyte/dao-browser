// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export interface SourceProfile {
  id: string;
  kind: string;
  browserName: string;
  profileName: string;
  supportedCategories: string[];
}

export interface CategoryState {
  category: string;
  phase: string;
  imported: number;
  skipped: number;
  conflicted: number;
  failed: number;
  completedItems: number;
  totalItems: number;
  indeterminate: boolean;
  errorCode: string;
}

export interface MigrationState {
  sourceId: string;
  terminal: boolean;
  cancelRequested: boolean;
  categories: CategoryState[];
}

export interface WebUiListener {
  eventName: string;
  uid: number;
}

let callbackId = 0;
let listenerId = 0;
const listeners:
    Record<string, Record<number, (...args: unknown[]) => void>> = {};

function webUIListenerCallback(event: string, ...args: unknown[]): void {
  const eventListeners = listeners[event];
  if (!eventListeners) {
    return;
  }
  for (const listener of Object.values(eventListeners)) {
    listener(...args);
  }
}

const bridgeWindow = window as unknown as {
  cr?: {webUIListenerCallback: typeof webUIListenerCallback};
};
bridgeWindow.cr = {webUIListenerCallback};

export function addListener(
    eventName: string,
    listener: (...args: unknown[]) => void): WebUiListener {
  listeners[eventName] ||= {};
  const uid = ++listenerId;
  listeners[eventName]![uid] = listener;
  return {eventName, uid};
}

export function removeListener(listener: WebUiListener): boolean {
  const eventListeners = listeners[listener.eventName];
  if (!eventListeners?.[listener.uid]) {
    return false;
  }
  delete eventListeners[listener.uid];
  return true;
}

function sendAsync<T>(method: string, ...args: unknown[]): Promise<T> {
  return new Promise(resolve => {
    const callbackEvent = `${method}_${callbackId++}`;
    const listener = addListener(callbackEvent, result => {
      removeListener(listener);
      resolve(result as T);
    });
    chrome.send(method, [callbackEvent, ...args]);
  });
}

export function detectImportSources(): Promise<SourceProfile[]> {
  return sendAsync<SourceProfile[]>('detectImportSources');
}

export function getImportItemCount(
    sourceId: string, category: string): Promise<number|null> {
  return sendAsync<number|null>('getImportItemCount', sourceId, category);
}

export function getBrowserMigrationState(): Promise<MigrationState|null> {
  return sendAsync<MigrationState|null>('getBrowserMigrationState');
}

export function startBrowserMigration(
    sourceId: string, categories: string[]): void {
  chrome.send('startBrowserMigration', [sourceId, categories]);
}

export function cancelBrowserMigration(): void {
  chrome.send('cancelBrowserMigration');
}

export function retryBrowserMigrationCategories(categories: string[]): void {
  chrome.send('retryBrowserMigrationCategories', [categories]);
}
