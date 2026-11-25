// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {addWebUIListener, callNative, removeWebUIListener} from './agent_bridge.js';
import {setProviderFetch} from './vendor/pi_runtime_bundle.js';

function isLocalModelUrl(url: URL): boolean {
  const host = url.hostname.replace(/\.$/, '');
  return (url.protocol === 'http:' || url.protocol === 'https:') &&
      (host === 'localhost' || host.endsWith('.localhost') ||
       host === '[::1]' || /^127(?:\.\d+){3}$/.test(host)) &&
      (url.pathname.endsWith('/v1/messages') ||
       url.pathname.endsWith('/v1/chat/completions'));
}

setProviderFetch(async (input: RequestInfo|URL, init?: RequestInit) => {
  const method = init?.method ?? (input instanceof Request ? input.method : 'GET');
  const url = new URL(input instanceof Request ? input.url : String(input),
                      location.href);
  if (method.toUpperCase() !== 'POST' || !isLocalModelUrl(url)) {
    return globalThis.fetch(input, init);
  }
  const request = new Request(input, init);
  const body = await request.text();
  request.signal.throwIfAborted();
  const id = crypto.randomUUID();
  const event = 'dao-model-stream-' + id;
  let controller: ReadableStreamDefaultController<Uint8Array>;
  let needsResume = false;
  const cleanup = () => {
    request.signal.removeEventListener('abort', abort);
    removeWebUIListener(event, onData);
  };
  const cancel = () => {
    cleanup();
    chrome.send('controlLocalModelStream', [id, 'cancel']);
  };
  const abort = () => {
    cancel();
    controller.error(request.signal.reason);
  };
  const onData = (chunk: unknown, error: unknown) => {
    if (typeof chunk === 'string') {
      needsResume = true;
      controller.enqueue(Uint8Array.from(atob(chunk), c => c.charCodeAt(0)));
    } else {
      cleanup();
      if (error) controller.error(new TypeError(String(error)));
      else controller.close();
    }
  };
  const stream = new ReadableStream<Uint8Array>({
    start(c) { controller = c; },
    pull() {
      if (needsResume) {
        needsResume = false;
        chrome.send('controlLocalModelStream', [id, 'resume']);
      }
    },
    cancel,
  }, {highWaterMark: 0});
  addWebUIListener(event, onData);
  request.signal.addEventListener('abort', abort, {once: true});
  try {
    const head = await callNative('nativeFetch', {
      streamId: id, url: request.url, method: 'POST',
      headers: Object.fromEntries(request.headers.entries()), body,
    }, {timeoutMs: null, signal: request.signal}) as {
      status: number; headers: Record<string, string>; error?: string;
    };
    if (!head.status) throw new TypeError(head.error);
    const empty = [204, 205, 304].includes(head.status);
    if (empty) await stream.cancel();
    return new Response(empty ? null : stream, head);
  } catch (error) {
    cancel();
    throw error;
  }
});
