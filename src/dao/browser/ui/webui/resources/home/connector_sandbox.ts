// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export const PAGE_OPERATIONS = [
  'navigate',
  'waitFor',
  'exists',
  'query',
  'queryAll',
  'getText',
  'getAttribute',
  'getComputedStyle',
  'scroll',
  'snapshot',
] as const;

type PageOperation = typeof PAGE_OPERATIONS[number];
type PageCall = (operation: PageOperation, args: unknown[]) => Promise<unknown>;
type ModuleImporter = (url: string) => Promise<unknown>;

interface ConnectorModule {
  default?: {collect?: (page: object, input: unknown) => Promise<unknown>|unknown};
}

export class ConnectorSandboxSession {
  constructor(
      private readonly callPage_: PageCall,
      private readonly importer_: ModuleImporter =
          url => import(/* webpackIgnore: true */ url)) {}

  async run(moduleSource: string, input: unknown): Promise<unknown> {
    const moduleUrl = URL.createObjectURL(
        new Blob([moduleSource], {type: 'text/javascript'}));
    try {
      const loaded = await this.importer_(moduleUrl) as ConnectorModule;
      if (typeof loaded.default?.collect !== 'function') {
        throw new Error('Connector module must export a collect function.');
      }
      const page = Object.freeze(Object.fromEntries(PAGE_OPERATIONS.map(
          operation => [
            operation,
            (...args: unknown[]) => this.callPage_(operation, args),
          ])));
      return await loaded.default.collect(page, input);
    } finally {
      URL.revokeObjectURL(moduleUrl);
    }
  }
}

interface SandboxMessage {
  daoHomeConnector: 1;
  type: 'run'|'page_result';
  executionId: string;
  revision?: string;
  module?: string;
  input?: unknown;
  callId?: number;
  result?: unknown;
  error?: string;
}

function installConnectorSandbox(): void {
  if (window.parent === window) {
    return;
  }
  const pending = new Map<
      number, {resolve: (value: unknown) => void; reject: (error: Error) => void}>();
  let nextCallId = 0;
  let activeExecution = '';

  window.addEventListener('message', async event => {
    const message = event.data as Partial<SandboxMessage>|null;
    if (event.source !== window.parent || event.origin !== 'dao://home' ||
        message?.daoHomeConnector !== 1) {
      return;
    }
    if (message.type === 'page_result' &&
        message.executionId === activeExecution &&
        typeof message.callId === 'number') {
      const request = pending.get(message.callId);
      if (!request) return;
      pending.delete(message.callId);
      if (message.error) request.reject(new Error(message.error));
      else request.resolve(message.result);
      return;
    }
    if (message.type !== 'run' || typeof message.executionId !== 'string' ||
        typeof message.module !== 'string' || activeExecution) {
      return;
    }
    activeExecution = message.executionId;
    const send = (payload: Record<string, unknown>) =>
        window.parent.postMessage({
          daoHomeConnector: 1,
          executionId: activeExecution,
          ...payload,
        }, 'dao://home');
    const callPage: PageCall = (operation, args) => {
      const callId = ++nextCallId;
      const result = new Promise<unknown>((resolve, reject) => {
        pending.set(callId, {resolve, reject});
      });
      send({type: 'page_call', callId, operation, args});
      return result;
    };
    try {
      const result = await new ConnectorSandboxSession(callPage)
                         .run(message.module, message.input);
      send({type: 'complete', result});
    } catch (error) {
      send({
        type: 'complete',
        error: error instanceof Error ? error.message : String(error),
      });
    } finally {
      for (const request of pending.values())
        request.reject(new Error('Connector execution ended.'));
      pending.clear();
      activeExecution = '';
    }
  });
}

installConnectorSandbox();
