// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  callHomeConnectorPage,
  cancelHomeConnector,
  finishHomeConnector,
  startHomeConnector,
  startHomeDraftConnector,
} from './home_bridge.js';

interface StartResult {
  cached?: unknown;
  execution_id?: string;
  revision?: string;
  module?: string;
  input?: unknown;
  error?: string;
  code?: string;
}

interface SandboxMessage {
  daoHomeConnector: 1;
  type: 'page_call'|'complete';
  executionId: string;
  code?: string;
  callId?: number;
  operation?: string;
  args?: unknown[];
  result?: unknown;
  error?: string;
}

interface PendingCollection {
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
  frame: HTMLIFrameElement;
  pageError?: HomeConnectorError;
}

const MAX_CONCURRENT_COLLECTIONS = 4;

export class HomeConnectorError extends Error {
  constructor(message: string, readonly code: string) {
    super(message);
    this.name = 'HomeConnectorError';
  }
}

function connectorError(
    value: {error?: unknown; code?: unknown}, fallback: string):
    HomeConnectorError {
  return new HomeConnectorError(
      typeof value.error === 'string' ? value.error : fallback,
      typeof value.code === 'string' ? value.code : 'temporarily_unavailable');
}

export class ConnectorHost {
  private readonly pending_ = new Map<string, PendingCollection>();
  private readonly inFlight_ = new Map<string, {
    promise: Promise<unknown>; reject: (error: Error) => void;
  }>();
  private readonly lastBySource_ = new Map<string, Promise<unknown>>();
  private readonly waiters_: Array<() => void> = [];
  private readonly lifetime_ = new AbortController();
  private running_ = 0;

  constructor(
      private readonly frame_: HTMLIFrameElement,
      private readonly revision_: string) {}

  async collect(connectorId: string, input: unknown, fresh = false): Promise<unknown> {
    return this.collectForDraft_('', connectorId, input, fresh);
  }

  async collectDraft(
      draftId: string, connectorId: string, input: unknown,
      fresh = false): Promise<unknown> {
    return this.collectForDraft_(draftId, connectorId, input, fresh);
  }

  private async collectForDraft_(
      draftId: string, connectorId: string, input: unknown,
      fresh = false): Promise<unknown> {
    this.lifetime_.signal.throwIfAborted();
    let key: string;
    try {
      key = JSON.stringify([draftId, connectorId, input, fresh]);
    } catch {
      throw new Error('Home connector input must be JSON serializable.');
    }
    const inFlight = this.inFlight_.get(key);
    if (inFlight) return inFlight.promise;

    const source = JSON.stringify([draftId, connectorId]);
    const previous = this.lastBySource_.get(source);
    const run = () => this.schedule_(draftId, connectorId, input, fresh);
    const request = Promise.withResolvers<unknown>();
    this.inFlight_.set(key, request);
    this.lastBySource_.set(source, request.promise);
    void (previous ? previous.then(run, run) : run()).then(request.resolve, request.reject);
    try {
      return await request.promise;
    } finally {
      this.inFlight_.delete(key);
      if (this.lastBySource_.get(source) === request.promise)
        this.lastBySource_.delete(source);
    }
  }

  private async schedule_(
      draftId: string, connectorId: string, input: unknown,
      fresh: boolean): Promise<unknown> {
    const signal = this.lifetime_.signal;
    signal.throwIfAborted();
    if (this.running_ >= MAX_CONCURRENT_COLLECTIONS) {
      await new Promise<void>(resolve => this.waiters_.push(resolve));
      signal.throwIfAborted();
    } else {
      ++this.running_;
    }
    try {
      const started = await (draftId ?
          startHomeDraftConnector(draftId, connectorId, input, fresh) :
          startHomeConnector(this.revision_, connectorId, input, fresh)) as StartResult;
      if (signal.aborted && started.execution_id)
        cancelHomeConnector(started.execution_id);
      signal.throwIfAborted();
      if (started.error) throw connectorError(started, 'Unable to start Home connector.');
      if ('cached' in started) return started.cached;
      if (!started.execution_id || !started.module)
        throw connectorError(started, 'Unable to start Home connector.');
      return await this.run_(started.execution_id, started.module, started.input);
    } finally {
      const next = this.waiters_.shift();
      if (next) next();
      else --this.running_;
    }
  }

  private async run_(executionId: string, module: string, input: unknown):
      Promise<unknown> {
    // Each module gets a fresh opaque realm, including its message listeners.
    const frame = this.frame_.cloneNode(false) as HTMLIFrameElement;
    frame.removeAttribute('data-test');
    let timeout: ReturnType<typeof setTimeout>;
    try {
      return await new Promise<unknown>((resolve, reject) => {
        this.pending_.set(executionId, {frame, resolve, reject});
        timeout = setTimeout(() => reject(new HomeConnectorError(
            'The Home connector timed out.', 'timed_out')), 21_000);
        frame.addEventListener('load', () => frame.contentWindow?.postMessage({
          daoHomeConnector: 1, type: 'run', executionId, module, input,
        }, '*'), {once: true});
        this.frame_.after(frame);
      });
    } catch (error) {
      cancelHomeConnector(executionId);
      throw error;
    } finally {
      clearTimeout(timeout!);
      this.pending_.delete(executionId);
      frame.remove();
    }
  }

  async handleMessage(event: MessageEvent): Promise<boolean> {
    const message = event.data as Partial<SandboxMessage>|null;
    if (event.origin !== 'null' ||
        message?.daoHomeConnector !== 1 ||
        typeof message.executionId !== 'string') {
      return false;
    }
    const pending = this.pending_.get(message.executionId);
    if (!pending || !event.source || event.source !== pending.frame.contentWindow) {
      return false;
    }
    if (message.type === 'page_call' && typeof message.callId === 'number' &&
        typeof message.operation === 'string' && Array.isArray(message.args)) {
      try {
        const result = await callHomeConnectorPage(
            message.executionId, message.operation, message.args) as {
          error?: string; code?: string;
        }|unknown;
        if (result && typeof result === 'object' &&
            typeof (result as {error?: unknown}).error === 'string') {
          throw connectorError(
              result as {error?: unknown; code?: unknown},
              'Unable to call the Home source page.');
        }
        this.replyPage_(pending.frame, message.executionId, message.callId, {result});
      } catch (error) {
        if (error instanceof HomeConnectorError) pending.pageError = error;
        this.replyPage_(pending.frame, message.executionId, message.callId, {
          error: error instanceof Error ? error.message : String(error),
          code: error instanceof HomeConnectorError ? error.code : undefined,
        });
      }
      return true;
    }
    if (message.type === 'complete') {
      pending.frame.remove();
      if (message.error) {
        // Only native error codes may flow back to an Agent or the CLI.
        pending.reject(pending.pageError && pending.pageError.code === message.code ?
            pending.pageError : new Error(message.error));
        return true;
      }
      try {
        const finished = await finishHomeConnector(
            message.executionId, message.result) as {
          result?: unknown; error?: string; code?: string;
        };
        if (finished.error) {
          throw connectorError(finished, 'Unable to finish Home connector.');
        }
        pending.resolve(finished.result);
      } catch (error) {
        pending.reject(
            error instanceof Error ? error : new Error(String(error)));
      }
      return true;
    }
    return false;
  }

  disconnect(): void {
    const error = new Error('Home connector host disconnected.');
    this.lifetime_.abort(error);
    for (const wake of this.waiters_.splice(0)) wake();
    for (const request of this.inFlight_.values()) request.reject(error);
    for (const pending of this.pending_.values()) pending.reject(error);
    this.pending_.clear();
  }

  private replyPage_(
      frame: HTMLIFrameElement, executionId: string, callId: number,
      value: {result?: unknown; error?: string; code?: string}): void {
    frame.contentWindow?.postMessage({
      daoHomeConnector: 1,
      type: 'page_result',
      executionId,
      callId,
      ...value,
    }, '*');
  }
}
