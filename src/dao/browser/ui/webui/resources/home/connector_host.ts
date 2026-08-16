// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  callHomeConnectorPage,
  finishHomeConnector,
  startHomeConnector,
  startHomeDraftConnector,
} from './home_bridge.js';

interface StartResult {
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
  callId?: number;
  operation?: string;
  args?: unknown[];
  result?: unknown;
  error?: string;
}

interface PendingCollection {
  resolve: (value: unknown) => void;
  reject: (error: Error) => void;
  timeout: ReturnType<typeof setTimeout>;
}

const MAX_COMPLETED_COLLECTIONS = 16;
export const COMPLETED_COLLECTION_TTL_MS = 10 * 60 * 1000;

interface CompletedCollection {
  value: unknown;
  collectedAt: number;
}

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
  private readonly inFlight_ = new Map<string, Promise<unknown>>();
  private readonly completed_ = new Map<string, CompletedCollection>();
  private queue_: Promise<void> = Promise.resolve();

  constructor(
      private readonly frame_: HTMLIFrameElement,
      private readonly revision_: string) {}

  async collect(connectorId: string, input: unknown): Promise<unknown> {
    return this.collectForDraft_('', connectorId, input);
  }

  async collectDraft(
      draftId: string, connectorId: string, input: unknown): Promise<unknown> {
    return this.collectForDraft_(draftId, connectorId, input);
  }

  private async collectForDraft_(
      draftId: string, connectorId: string, input: unknown): Promise<unknown> {
    let serializedInput: string;
    try {
      serializedInput = JSON.stringify(input);
    } catch {
      throw new Error('Home connector input must be JSON serializable.');
    }
    const key = `${draftId}\n${connectorId}\n${serializedInput}`;
    const completed = this.completed_.get(key);
    if (completed &&
        Date.now() - completed.collectedAt < COMPLETED_COLLECTION_TTL_MS) {
      return completed.value;
    }
    if (completed) {
      this.completed_.delete(key);
    }
    const inFlight = this.inFlight_.get(key);
    if (inFlight) {
      return inFlight;
    }
    const collection = this.queue_.then(
        () => this.collectOnce_(draftId, connectorId, input));
    this.inFlight_.set(key, collection);
    this.queue_ = collection.then(() => undefined, () => undefined);
    void collection.then(value => {
      if (this.inFlight_.get(key) !== collection) {
        return;
      }
      this.inFlight_.delete(key);
      this.completed_.set(key, {value, collectedAt: Date.now()});
      while (this.completed_.size > MAX_COMPLETED_COLLECTIONS) {
        const oldest = this.completed_.keys().next().value;
        if (oldest === undefined) {
          break;
        }
        this.completed_.delete(oldest);
      }
    }, () => {
      if (this.inFlight_.get(key) === collection) {
        this.inFlight_.delete(key);
      }
    });
    return collection;
  }

  private async collectOnce_(
      draftId: string, connectorId: string, input: unknown): Promise<unknown> {
    const started = await (draftId ?
        startHomeDraftConnector(draftId, connectorId, input) :
        startHomeConnector(
            this.revision_, connectorId, input)) as StartResult;
    if (started.error || !started.execution_id || !started.module) {
      throw connectorError(started, 'Unable to start Home connector.');
    }
    const result = new Promise<unknown>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending_.delete(started.execution_id!);
        void finishHomeConnector(started.execution_id!, null);
        reject(new HomeConnectorError(
            'The Home connector timed out.', 'timed_out'));
      }, 21_000);
      this.pending_.set(started.execution_id!, {resolve, reject, timeout});
    });
    this.frame_.contentWindow!.postMessage({
      daoHomeConnector: 1,
      type: 'run',
      executionId: started.execution_id,
      revision: this.revision_,
      module: started.module,
      input: started.input,
    }, '*');
    return result;
  }

  async handleMessage(event: MessageEvent): Promise<boolean> {
    const message = event.data as Partial<SandboxMessage>|null;
    if (event.source !== this.frame_.contentWindow ||
        event.origin !== 'null' ||
        message?.daoHomeConnector !== 1 ||
        typeof message.executionId !== 'string') {
      return false;
    }
    const pending = this.pending_.get(message.executionId);
    if (!pending) {
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
        this.replyPage_(message.executionId, message.callId, {result});
      } catch (error) {
        this.replyPage_(message.executionId, message.callId, {
          error: error instanceof Error ? error.message : String(error),
        });
      }
      return true;
    }
    if (message.type === 'complete') {
      this.pending_.delete(message.executionId);
      clearTimeout(pending.timeout);
      if (message.error) {
        try {
          await finishHomeConnector(message.executionId, null);
        } catch {
          // The sandbox error is the actionable failure for this collection.
        }
        pending.reject(new Error(message.error));
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
    for (const pending of this.pending_.values()) {
      clearTimeout(pending.timeout);
      pending.reject(new Error('Home connector host disconnected.'));
    }
    this.pending_.clear();
    this.inFlight_.clear();
    this.completed_.clear();
  }

  private replyPage_(
      executionId: string, callId: number,
      value: {result?: unknown; error?: string}): void {
    this.frame_.contentWindow!.postMessage({
      daoHomeConnector: 1,
      type: 'page_result',
      executionId,
      callId,
      ...value,
    }, '*');
  }
}
