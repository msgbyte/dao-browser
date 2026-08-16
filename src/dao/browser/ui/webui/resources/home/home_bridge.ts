// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export interface HomeConnectorSnapshot {
  id: string;
  origins: string[];
  granted: boolean;
}

export interface HomeSnapshot {
  hasProject: boolean;
  revision: string;
  entry: string;
  connectors: HomeConnectorSnapshot[];
}

export interface HomeOperationError {
  error: string;
  code: string;
}

export interface HomeVersion {
  id: string;
  parent: string;
  restoredFrom: string;
  summary: string;
  kind: string;
  createdAtMs: number;
  changedFiles: string[];
}

export interface HomeSinglePermissionRequest {
  kind: 'single';
  id: string;
  draftId: string;
  baseRevision: string;
  connectorId: string;
  origins: string[];
  paths: string[];
  capabilities: string[];
  mode: 'read';
  previousLimits?: HomeResourceLimits;
  requestedLimits: HomeResourceLimits;
}

export interface HomeResourceLimits {
  maxResultBytes: number;
  maxItemsPerConnector: number;
}

export interface HomePermissionBatchItem {
  connectorId: string;
  label: string;
  origins: string[];
  paths: string[];
  capabilities: string[];
  mode: 'read';
  previousLimits?: HomeResourceLimits;
  requestedLimits: HomeResourceLimits;
  authenticationMayBeRequired: boolean;
}

export interface HomePermissionBatchRequest {
  kind: 'batch';
  id: string;
  draftId: string;
  baseRevision: string;
  items: HomePermissionBatchItem[];
}

export type HomePermissionRequest =
    HomeSinglePermissionRequest|HomePermissionBatchRequest;

export interface WebUiListener {
  eventName: string;
  uid: number;
}

let callbackId = 0;
let listenerId = 0;
const listeners:
    Record<string, Record<number, (...args: unknown[]) => void>> = {};

function webUIListenerCallback(event: string, ...args: unknown[]): void {
  for (const listener of Object.values(listeners[event] ?? {})) {
    listener(...args);
  }
}

(window as unknown as {
  cr?: {webUIListenerCallback: typeof webUIListenerCallback};
}).cr = {webUIListenerCallback};

export function addListener(
    eventName: string,
    listener: (...args: unknown[]) => void): WebUiListener {
  listeners[eventName] ||= {};
  const uid = ++listenerId;
  listeners[eventName]![uid] = listener;
  return {eventName, uid};
}

export function removeListener(listener: WebUiListener): void {
  delete listeners[listener.eventName]?.[listener.uid];
}

function sendAsync<T>(method: string, ...args: unknown[]): Promise<T> {
  return new Promise(resolve => {
    const eventName = `${method}_${++callbackId}`;
    const listener = addListener(eventName, result => {
      removeListener(listener);
      resolve(result as T);
    });
    chrome.send(method, [eventName, ...args]);
  });
}

export function getHomeSnapshot(): Promise<HomeSnapshot> {
  return sendAsync<HomeSnapshot>('getHomeSnapshot');
}

export function getHomeVersions(): Promise<HomeVersion[]> {
  return sendAsync<HomeVersion[]>('getHomeVersions');
}

export function getHomeFiles(revision: string): Promise<string[]> {
  return sendAsync<string[]>('getHomeFiles', revision);
}

export function readHomeFile(
    revision: string, path: string): Promise<string> {
  return sendAsync<string>('readHomeFile', revision, path);
}

export function getHomePermission(): Promise<HomePermissionRequest|null> {
  return sendAsync<HomePermissionRequest|null>('getHomePermission');
}

export function approveHomePermission(requestId: string): Promise<unknown> {
  return sendAsync('approveHomePermission', requestId);
}

export function cancelHomePermission(requestId: string): Promise<boolean> {
  return sendAsync<boolean>('cancelHomePermission', requestId);
}

export function resolveHomeBootstrapPermission(
    requestId: string, selectedConnectorIds: string[]): Promise<unknown> {
  return sendAsync(
      'resolveHomeBootstrapPermission', requestId, selectedConnectorIds);
}

export function openHomeAgent(mode: 'create'|'history'|'repair'): void {
  chrome.send('openHomeAgent', [mode]);
}

export function rollbackHome(
    baseRevision: string, targetRevision: string): Promise<HomeSnapshot> {
  return sendAsync<HomeSnapshot>(
      'rollbackHome', baseRevision, targetRevision);
}

export function resetHome(
    baseRevision: string): Promise<HomeSnapshot|HomeOperationError> {
  return sendAsync<HomeSnapshot|HomeOperationError>('resetHome', baseRevision);
}

export function exportHome(): Promise<string> {
  return sendAsync<string>('exportHome');
}

export function importHome(
    packageJson: string): Promise<HomeSnapshot|HomeOperationError> {
  return sendAsync<HomeSnapshot|HomeOperationError>('importHome', packageJson);
}

export function startHomeConnector(
    revision: string, connectorId: string, input: unknown): Promise<unknown> {
  return sendAsync('startHomeConnector', revision, connectorId, input);
}

export function startHomeDraftConnector(
    draftId: string, connectorId: string, input: unknown): Promise<unknown> {
  return sendAsync('startHomeDraftConnector', draftId, connectorId, input);
}

export function callHomeConnectorPage(
    executionId: string, operation: string, args: unknown[]): Promise<unknown> {
  return sendAsync('callHomeConnectorPage', executionId, operation, args);
}

export function finishHomeConnector(
    executionId: string, result: unknown): Promise<unknown> {
  return sendAsync('finishHomeConnector', executionId, result);
}

export function resolveHomeMedia(handle: string): Promise<unknown> {
  return sendAsync('resolveHomeMedia', handle);
}

export function completeHomeAgentConnector(
    requestId: string, result: unknown): void {
  chrome.send('completeHomeAgentConnector', [requestId, result]);
}

export function notifyHomeAgentPreviewLoaded(requestId: string): void {
  chrome.send('notifyHomeAgentPreviewLoaded', [requestId]);
}

export function recordHomeRuntimeError(
    revision: string, kind: 'error'|'unhandled_rejection'): void {
  chrome.send('recordHomeRuntimeError', [revision, kind]);
}

export function cancelHomeSession(): void {
  chrome.send('cancelHomeSession');
}

export function setHomeSelection(nodeId: string): void {
  chrome.send('setHomeSelection', [nodeId]);
}

export function openHomeNavigation(url: string): void {
  chrome.send('openHomeNavigation', [url]);
}
