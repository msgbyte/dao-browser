// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

interface PendingCallback {
  resolve: (value: unknown) => void;
  reject: (reason: unknown) => void;
}

interface CrNamespace {
  webUIResponse:
      (id: string, isSuccess: boolean, response: unknown) => void;
}

export interface NativeCallOptions {
  timeoutMs?: number|null;
}

export const cr =
    ((window as unknown as {cr?: CrNamespace}).cr) || {} as CrNamespace;
(window as unknown as {cr: CrNamespace}).cr = cr;

const pendingCallbacks: Record<string, PendingCallback> = {};
let callbackCounter = 0;
const DEFAULT_NATIVE_CALL_TIMEOUT_MS = 15000;

cr.webUIResponse =
    function(id: string, isSuccess: boolean, response: unknown): void {
  const entry = pendingCallbacks[id];
  if (!entry) return;
  delete pendingCallbacks[id];
  if (isSuccess) {
    entry.resolve(response);
  } else {
    entry.reject(response);
  }
};

export function callNative(
    method: string, params?: Record<string, unknown>,
    options?: NativeCallOptions): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const id = method + '_' + (++callbackCounter);
    pendingCallbacks[id] = {resolve, reject};
    chrome.send(method, [id, params || {}]);
    const timeoutMs =
        options?.timeoutMs === undefined ? DEFAULT_NATIVE_CALL_TIMEOUT_MS :
                                           options.timeoutMs;
    if (timeoutMs === null || timeoutMs <= 0 ||
        !Number.isFinite(timeoutMs)) {
      return;
    }
    setTimeout(() => {
      if (pendingCallbacks[id]) {
        delete pendingCallbacks[id];
        reject(new Error('Timeout calling ' + method));
      }
    }, timeoutMs);
  });
}

export function callNativeArgs(
    method: string, ...args: unknown[]): Promise<unknown> {
  return new Promise((resolve, reject) => {
    const id = method + '_' + (++callbackCounter);
    pendingCallbacks[id] = {resolve, reject};
    chrome.send(method, [id, ...args]);
    setTimeout(() => {
      if (pendingCallbacks[id]) {
        delete pendingCallbacks[id];
        reject(new Error('Timeout calling ' + method));
      }
    }, DEFAULT_NATIVE_CALL_TIMEOUT_MS);
  });
}
