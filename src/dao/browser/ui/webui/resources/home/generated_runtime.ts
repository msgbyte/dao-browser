// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

interface HomeEnvelope {
  daoHome: 1;
  requestId: string;
  revision: string;
  method?: string;
  params?: unknown;
  result?: unknown;
  stale?: boolean;
  error?: string;
  code?: string;
}

interface PendingRequest {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
  onUpdate?: (value: unknown) => void;
  servedStale?: boolean;
}

interface ResolvedMedia {
  mime: string;
  base64: string;
}

interface HomePickerCommand {
  daoHomePicker?: number;
  type?: string;
  requestId?: string;
  x?: number;
  y?: number;
}

export function installHomeElementPickerHitTest(channel: GeneratedAppChannel):
    () => void {
  const handleMessage = (event: MessageEvent) => {
    const command = event.data as HomePickerCommand|null;
    if (event.source !== window.parent || event.origin !== 'dao://home' ||
        command?.daoHomePicker !== 1 ||
        typeof command.requestId !== 'string' ||
        (command.type !== 'hover' && command.type !== 'select') ||
        typeof command.x !== 'number' || !Number.isFinite(command.x) ||
        typeof command.y !== 'number' || !Number.isFinite(command.y)) {
      return;
    }
    const requestId = command.requestId;
    const element = document.elementFromPoint(command.x, command.y);
    if (!(element instanceof HTMLElement) ||
        element === document.documentElement || element === document.body) {
      window.parent.postMessage({
        daoHomePicker: 1,
        type: command.type,
        requestId,
        result: {status: 'empty'},
      }, 'dao://home');
      return;
    }
    const cleanText = (value: string|null|undefined, max: number): string => {
      const text = (value || '').replace(/\s+/g, ' ').trim();
      return text.length > max ? `${text.slice(0, max - 3)}...` : text;
    };
    const escapeCss = (value: string): string =>
      window.CSS?.escape ? window.CSS.escape(value) :
                           value.replace(/[^a-zA-Z0-9_-]/g, '\\$&');
    const attributeSelector = (name: string, value: string): string =>
      `[${name}="${value.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"]`;
    const isUnique = (selector: string): boolean => {
      try {
        return document.querySelectorAll(selector).length === 1;
      } catch {
        return false;
      }
    };
    const pathPart = (element: HTMLElement): string => {
      const tag = element.tagName.toLowerCase();
      const id = element.id;
      if (id && isUnique(`${tag}#${escapeCss(id)}`)) {
        return `${tag}#${escapeCss(id)}`;
      }
      for (const name of [
             'data-dao-node-id', 'data-dao-action', 'data-dao-feed-link',
             'data-testid', 'data-test', 'data-filter']) {
        const value = element.getAttribute(name);
        if (value) return `${tag}${attributeSelector(name, value)}`;
      }
      let index = 1;
      let sibling = element.previousElementSibling;
      while (sibling) {
        if (sibling.tagName === element.tagName) index++;
        sibling = sibling.previousElementSibling;
      }
      return `${tag}:nth-of-type(${index})`;
    };
    const stableSelector = (element: HTMLElement): string => {
      const tag = element.tagName.toLowerCase();
      for (const name of [
             'data-dao-node-id', 'data-dao-action', 'data-dao-feed-link',
             'data-testid', 'data-test', 'data-filter', 'id']) {
        const value = element.getAttribute(name);
        if (!value) continue;
        const selector = name === 'id' ? `${tag}#${escapeCss(value)}` :
                                        `${tag}${attributeSelector(name, value)}`;
        if (isUnique(selector)) return selector;
      }
      const parts: string[] = [];
      let current: HTMLElement|null = element;
      while (current && current !== document.body) {
        parts.unshift(pathPart(current));
        const candidate = parts.join(' > ');
        if (isUnique(candidate)) return candidate;
        current = current.parentElement;
      }
      return parts.join(' > ');
    };
    const finish = () => {
      const nodeId = element.dataset['daoNodeId'];
      const text = cleanText(element.innerText || element.textContent, 300);
      const name = cleanText(
          element.getAttribute('aria-label') ||
              element.getAttribute('title') || text,
          120);
      const bounds = element.getBoundingClientRect();
      const css = stableSelector(element);
      const attributes: Record<string, string> = {};
      for (const attribute of [
             'id', 'role', 'aria-label', 'title', 'href', 'data-dao-node-id',
             'data-dao-action', 'data-dao-feed-link', 'data-filter']) {
        const value = element.getAttribute(attribute);
        if (value) attributes[attribute] = cleanText(value, 160);
      }
      window.parent.postMessage({
        daoHomePicker: 1,
        type: command.type,
        requestId,
        result: {
          status: 'selected',
          url: location.href,
          title: document.title || '',
          label: name || nodeId || element.tagName.toLowerCase(),
          text,
          locator: {
            role: element.getAttribute('role') || 'generic',
            name,
            tag: element.tagName.toLowerCase(),
            text,
            attributes,
            css,
            fallbackPath: css,
            nearText: [],
            bounds: {
              x: Math.round(bounds.left),
              y: Math.round(bounds.top),
              width: Math.round(bounds.width),
              height: Math.round(bounds.height),
            },
          },
          viewport: {
            width: Math.round(window.innerWidth),
            height: Math.round(window.innerHeight),
          },
        },
      }, 'dao://home');
    };
    const nodeId = element.dataset['daoNodeId'];
    if (command.type === 'select' && nodeId) {
      void channel.call('selection.set', {nodeId})
          .then(finish, finish);
    } else {
      finish();
    }
  };
  window.addEventListener('message', handleMessage);
  return () => window.removeEventListener('message', handleMessage);
}

export function createMediaObjectUrl(
    value: unknown,
    create: (blob: Blob) => string = blob => URL.createObjectURL(blob)):
    string {
  const media = value as Partial<ResolvedMedia>|null;
  if (!media || typeof media.mime !== 'string' ||
      !/^(image|video)\//.test(media.mime) ||
      typeof media.base64 !== 'string') {
    throw new Error('Invalid Home media response');
  }
  const binary = atob(media.base64);
  if (!binary.length || binary.length > 5 * 1024 * 1024) {
    throw new Error('Home media exceeds its byte budget');
  }
  const bytes = Uint8Array.from(binary, character => character.charCodeAt(0));
  return create(new Blob([bytes], {type: media.mime}));
}

export class HomeMediaUrls {
  private readonly entries_ = new Map<string, {url: string; bytes: number}>();
  private bytes_ = 0;

  get(handle: string): string|undefined {
    return this.entries_.get(handle)?.url;
  }

  store(handle: string, blob: Blob): string {
    const existing = this.get(handle);
    if (existing) return existing;
    const url = URL.createObjectURL(blob);
    this.entries_.set(handle, {url, bytes: blob.size});
    this.bytes_ += blob.size;
    for (const id of this.entries_.keys()) {
      if (this.bytes_ <= 16 * 1024 * 1024 && this.entries_.size <= 1600) break;
      this.delete(id);
    }
    return url;
  }

  delete(handle: string): void {
    const entry = this.entries_.get(handle);
    if (entry) {
      URL.revokeObjectURL(entry.url);
      this.bytes_ -= entry.bytes;
      this.entries_.delete(handle);
    }
  }

  clear(): void {
    for (const handle of this.entries_.keys()) this.delete(handle);
  }
}

export class GeneratedAppChannel {
  private readonly pending_ = new Map<string, PendingRequest>();

  constructor(
      private readonly parent_: Window,
      private readonly trustedOrigin_: string,
      private readonly revision_: string,
      private readonly discardStaleDocument_: () => void = () => location.reload()) {}

  get pendingCountForTesting(): number {
    return this.pending_.size;
  }

  call(method: string, params: unknown = {},
       onUpdate?: (value: unknown) => void): Promise<unknown> {
    const requestId = crypto.getRandomValues(new Uint32Array(4)).join('-');
    const request = new Promise<unknown>((resolve, reject) => {
      this.pending_.set(requestId, {resolve, reject, onUpdate});
    });
    const envelope: HomeEnvelope = {
      daoHome: 1,
      requestId,
      revision: this.revision_,
      method,
      params,
    };
    const options = {
      targetOrigin: this.trustedOrigin_,
      includeUserActivation: method === 'navigation.open',
    };
    this.parent_.postMessage(envelope, options);
    return request;
  }

  handleMessage(event: MessageEvent): boolean {
    const envelope = event.data as Partial<HomeEnvelope>|null;
    if (event.source !== this.parent_ || event.origin !== this.trustedOrigin_ ||
        envelope?.daoHome !== 1 || envelope.revision !== this.revision_ ||
        typeof envelope.requestId !== 'string') {
      return false;
    }
    const pending = this.pending_.get(envelope.requestId);
    if (!pending) {
      return false;
    }
    if (envelope.stale === true) {
      pending.servedStale = true;
      pending.resolve(envelope.result);
      return true;
    }
    this.pending_.delete(envelope.requestId);
    if (typeof envelope.error === 'string') {
      const error = new Error(envelope.error) as Error&{code?: string};
      if (typeof envelope.code === 'string') {
        error.code = envelope.code;
      }
      pending.reject(error);
      if (pending.servedStale &&
          (error.code === 'auth_required' || error.code === 'session_changed')) {
        // A one-shot card may already have rendered data from the ended session.
        this.discardStaleDocument_();
      }
    } else if (pending.servedStale) {
      pending.onUpdate?.(envelope.result);
    } else {
      pending.resolve(envelope.result);
    }
    return true;
  }

  disconnect(): void {
    for (const pending of this.pending_.values()) {
      pending.reject(new Error('Home runtime disconnected'));
    }
    this.pending_.clear();
  }
}

export function installActionNavigation(
    root: Document,
    open: (actionId: string, url: string) => unknown): () => void {
  const handleClick = (event: Event) => {
    const action = event.target instanceof Element ?
        event.target.closest<HTMLElement>(
            '[data-dao-action][data-dao-action-url]') :
        null;
    const actionId = action?.dataset['daoAction'];
    const url = action?.dataset['daoActionUrl'];
    if (!actionId || !url) {
      return;
    }
    event.preventDefault();
    event.stopImmediatePropagation();
    void Promise.resolve(open(actionId, url)).catch(() => {});
  };
  root.addEventListener('click', handleClick, {capture: true});
  return () => root.removeEventListener('click', handleClick, {capture: true});
}

function installRuntime(): void {
  if (window.parent === window) {
    return;
  }
  const parts = location.pathname.split('/').filter(Boolean);
  const revision = parts[parts[0] === 'preview' ? 1 : 0] ?? '';
  const channel = new GeneratedAppChannel(window.parent, 'dao://home', revision);
  const session = new Map<string, unknown>();
  const mediaUrls = new HomeMediaUrls();
  const openNavigation = (url: string) =>
      channel.call('navigation.open', {url});
  const openAction = (actionId: string, url: string) =>
      channel.call('navigation.openAction', {actionId, url});
  const uninstallActionNavigation =
      installActionNavigation(document, openAction);
  const uninstallHomeElementPickerHitTest =
      installHomeElementPickerHitTest(channel);
  window.addEventListener('message', event => channel.handleMessage(event));
  window.addEventListener('error', () => {
    void channel.call('runtime.report', {kind: 'error'}).catch(() => {});
  });
  window.addEventListener('unhandledrejection', () => {
    void channel.call('runtime.report', {kind: 'unhandled_rejection'})
        .catch(() => {});
  });
  document.addEventListener('click', event => {
    const target = event.target instanceof Element ?
        event.target.closest<HTMLElement>('[data-dao-node-id]') : null;
    const nodeId = target?.dataset['daoNodeId'];
    if (nodeId && /^[a-zA-Z0-9._-]{1,128}$/.test(nodeId)) {
      void channel.call('selection.set', {nodeId});
    }
  }, {capture: true});
  window.addEventListener('pagehide', () => {
    uninstallActionNavigation();
    uninstallHomeElementPickerHitTest();
    channel.disconnect();
    session.clear();
    mediaUrls.clear();
  }, {once: true});

  (window as unknown as {dao: unknown}).dao = Object.freeze({
    sources: Object.freeze({
      collect: (connectorId: string, input: unknown = {},
                options?: {fresh?: boolean; onUpdate?: (value: unknown) => void}) =>
          channel.call('sources.collect', {connectorId, input, fresh: options?.fresh === true},
                       options?.onUpdate),
    }),
    session: Object.freeze({
      get: (key: string) => session.get(key),
      set: (key: string, value: unknown) => session.set(key, value),
    }),
    navigation: Object.freeze({
      open: openNavigation,
    }),
    media: Object.freeze({
      resolve: async (handle: string) => {
        if (mediaUrls.get(handle)) {
          try {
            await channel.call('media.resolve', {handle, checkOnly: true});
          } catch (error) {
            mediaUrls.delete(handle);
            throw error;
          }
          const existing = mediaUrls.get(handle);
          if (existing) return existing;
        }
        return createMediaObjectUrl(await channel.call('media.resolve', {handle}),
                                   blob => mediaUrls.store(handle, blob));
      },
    }),
  });
}

installRuntime();
