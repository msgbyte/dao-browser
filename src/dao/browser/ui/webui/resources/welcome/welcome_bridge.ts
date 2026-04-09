// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Bridge between welcome WebUI JavaScript and C++ via chrome.send().

// ---- WebUI Listener Infrastructure ----

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

// Expose on window.cr for C++ to call (FireWebUIListener).
const w = window as unknown as
    {cr?: {webUIListenerCallback: typeof webUIListenerCallback}};
(w as {cr: object}).cr = {webUIListenerCallback};

// ---- Chrome.send Bridge ----

export function sendNative(method: string, ...args: unknown[]): void {
  chrome.send(method, args);
}

export function addListener(
    event: string,
    callback: (...args: unknown[]) => void): WebUiListener {
  webUiListenerMap[event] = webUiListenerMap[event] || {};
  const uid = uidCounter++;
  webUiListenerMap[event]![uid] = callback;
  return {eventName: event, uid};
}

// ---- Welcome API ----

export function markWelcomeShown(): void {
  sendNative('markWelcomeShown');
}
