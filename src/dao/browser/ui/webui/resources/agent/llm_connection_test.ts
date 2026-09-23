// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Runs the Settings page's "Test connection" request inside dao://agent so it
// takes the same pi-ai path as real conversations. The browser process
// forwards the unsaved form values here and relays the result to Settings.

import {addWebUIListener} from './agent_bridge.js';
import {callLLMStreamingWithPi} from './pi_llm_stream.js';

export const CONNECTION_TEST_TIMEOUT_MS = 30_000;

interface ConnectionTestRequest {
  requestId: string;
  provider: string;
  model: string;
  apiKey: string;
  baseUrl: string;
}

export interface ConnectionTestResult {
  ok: boolean;
  latencyMs: number;
  errorCode?: 'provider'|'timeout';
  error?: string;
}

function parseRequest(payload: unknown): ConnectionTestRequest|null {
  if (typeof payload !== 'object' || payload === null) return null;
  const p = payload as Record<string, unknown>;
  const fields = ['requestId', 'provider', 'model', 'apiKey', 'baseUrl'];
  if (!fields.every(key => typeof p[key] === 'string') || !p['requestId']) {
    return null;
  }
  return p as unknown as ConnectionTestRequest;
}

async function testLlmConnection({provider, model, apiKey, baseUrl}:
                                     ConnectionTestRequest):
    Promise<ConnectionTestResult> {
  const started = Date.now();
  const controller = new AbortController();
  const timer =
      setTimeout(() => controller.abort(), CONNECTION_TEST_TIMEOUT_MS);
  let error: string|undefined;
  try {
    await callLLMStreamingWithPi(
        [{role: 'user', content: 'Reply with OK.'}], [], {
          onToken: () => {},
          onToolCall: () => {},
          onDone: () => {},
          onError: (shortMsg, fullError) => {
            error = `${shortMsg}: ${fullError}`;
          },
        },
        {provider, model, apiKey, baseUrl, signal: controller.signal});
  } catch (e) {
    error = e instanceof Error ? e.message : String(e);
  } finally {
    clearTimeout(timer);
  }
  const latencyMs = Date.now() - started;
  if (controller.signal.aborted) {
    return {ok: false, latencyMs, errorCode: 'timeout'};
  }
  return error === undefined ?
      {ok: true, latencyMs} :
      {ok: false, latencyMs, errorCode: 'provider', error};
}

addWebUIListener('dao-agent-connection-test', (payload: unknown) => {
  const request = parseRequest(payload);
  if (!request) return;
  void testLlmConnection(request).then(result => {
    chrome.send('daoAgentConnectionTestResult', [request.requestId, result]);
  });
});
// Registering only after the listener exists means the browser never routes
// a test to a page that cannot answer it.
chrome.send('registerDaoAgentConnectionTester');
