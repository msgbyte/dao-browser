// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, describe, expect, it, vi} from 'vitest';

import {
  createMediaObjectUrl,
  GeneratedAppChannel,
  installActionNavigation,
  installHomeElementPickerHitTest,
} from '../generated_runtime.js';

describe('installHomeElementPickerHitTest', () => {
  afterEach(() => {
    document.body.replaceChildren();
    delete (document as Document&{elementFromPoint?: unknown}).elementFromPoint;
    vi.restoreAllMocks();
  });

  it('selects a stable Home node by coordinates without dispatching a click',
     async () => {
    const open = vi.fn();
    const uninstallNavigation = installActionNavigation(document, open);
    const call = vi.fn().mockResolvedValue({selected: true});
    const uninstallPicker = installHomeElementPickerHitTest(
        {call} as unknown as GeneratedAppChannel);
    const postMessage = vi.spyOn(window.parent, 'postMessage')
                            .mockImplementation(() => {});
    const node = document.createElement('a');
    node.dataset['daoNodeId'] = 'feed-card';
    node.dataset['daoAction'] = 'bilibili';
    node.dataset['daoActionUrl'] = 'https://www.bilibili.com/';
    node.textContent = 'Following update';
    document.body.appendChild(node);
    Object.defineProperty(document, 'elementFromPoint', {
      configurable: true,
      value: vi.fn().mockReturnValue(node),
    });

    window.dispatchEvent(new MessageEvent('message', {
      data: {
        daoHomePicker: 1,
        type: 'select',
        requestId: 'request-1',
        x: 20,
        y: 30,
      },
      origin: 'dao://home',
      source: window.parent,
    }));
    await Promise.resolve();
    await Promise.resolve();

    expect(open).not.toHaveBeenCalled();
    expect(call).toHaveBeenCalledWith(
        'selection.set', {nodeId: 'feed-card'});
    expect(postMessage).toHaveBeenCalledWith(
        expect.objectContaining({
          daoHomePicker: 1,
          type: 'select',
          requestId: 'request-1',
          result: expect.objectContaining({
            status: 'selected',
            locator: expect.objectContaining({
              css: 'a[data-dao-node-id="feed-card"]',
            }),
          }),
        }),
        'dao://home');

    uninstallPicker();
    uninstallNavigation();
  });

  it('builds a stable selector for an existing Home without node IDs', () => {
    const uninstallPicker = installHomeElementPickerHitTest(
        {call: vi.fn()} as unknown as GeneratedAppChannel);
    const postMessage = vi.spyOn(window.parent, 'postMessage')
                            .mockImplementation(() => {});
    const section = document.createElement('section');
    section.className = 'feed-panel';
    const heading = document.createElement('h2');
    heading.id = 'feed-title';
    heading.textContent = 'Feed';
    section.appendChild(heading);
    document.body.appendChild(section);
    Object.defineProperty(document, 'elementFromPoint', {
      configurable: true,
      value: vi.fn().mockReturnValue(heading),
    });

    window.dispatchEvent(new MessageEvent('message', {
      data: {
        daoHomePicker: 1,
        type: 'hover',
        requestId: 'request-2',
        x: 40,
        y: 50,
      },
      origin: 'dao://home',
      source: window.parent,
    }));

    expect(postMessage).toHaveBeenCalledWith(
        expect.objectContaining({
          type: 'hover',
          requestId: 'request-2',
          result: expect.objectContaining({
            locator: expect.objectContaining({css: 'h2#feed-title'}),
          }),
        }),
        'dao://home');
    uninstallPicker();
  });
});

describe('installActionNavigation', () => {
  it('connects a generated launch action to trusted navigation', () => {
    document.body.innerHTML = `
      <button data-dao-action="github"
          data-dao-action-url="https://github.com/">
        <span id="github-label">GitHub</span>
      </button>`;
    const open = vi.fn();
    const uninstall = installActionNavigation(document, open);
    const event = new MouseEvent('click', {
      bubbles: true,
      cancelable: true,
    });

    document.querySelector('#github-label')!.dispatchEvent(event);

    expect(event.defaultPrevented).toBe(true);
    expect(open).toHaveBeenCalledOnce();
    expect(open).toHaveBeenCalledWith('github', 'https://github.com/');
    uninstall();
  });

  it('ignores elements without the complete launch-action contract', () => {
    document.body.innerHTML = `
      <button data-dao-action="github">GitHub</button>
      <button data-dao-action-url="https://github.com/">Other</button>`;
    const open = vi.fn();
    const uninstall = installActionNavigation(document, open);

    for (const button of document.querySelectorAll('button')) {
      button.click();
    }

    expect(open).not.toHaveBeenCalled();
    uninstall();
  });

  it('owns canonical action clicks before generated handlers run', () => {
    document.body.innerHTML = `
      <button data-dao-action="github"
          data-dao-action-url="https://github.com/">GitHub</button>`;
    const button = document.querySelector('button')!;
    const generatedHandler = vi.fn();
    button.addEventListener('click', generatedHandler);
    const open = vi.fn();
    const uninstall = installActionNavigation(document, open);

    button.click();

    expect(open).toHaveBeenCalledWith('github', 'https://github.com/');
    expect(generatedHandler).not.toHaveBeenCalled();
    uninstall();
  });
});

describe('GeneratedAppChannel', () => {
  it('accepts replies only from the exact parent and trusted host origin',
     async () => {
       const parent = {postMessage: vi.fn()} as unknown as Window;
       const channel = new GeneratedAppChannel(
           parent, 'dao://home', 'revision-1');
       const request = channel.call('session.get', {key: 'view'});
       const requestId = parent.postMessage.mock.calls[0]![0].requestId;

       expect(channel.handleMessage(new MessageEvent('message', {
         source: window,
         origin: 'dao://home',
         data: {daoHome: 1, requestId, revision: 'revision-1', result: 'bad'},
       }))).toBe(false);
       expect(channel.handleMessage(new MessageEvent('message', {
         source: parent,
         origin: 'https://attacker.test',
         data: {daoHome: 1, requestId, revision: 'revision-1', result: 'bad'},
       }))).toBe(false);
       expect(channel.handleMessage(new MessageEvent('message', {
         source: parent,
         origin: 'dao://home',
         data: {daoHome: 1, requestId, revision: 'stale', result: 'bad'},
       }))).toBe(false);

       expect(channel.handleMessage(new MessageEvent('message', {
         source: parent,
         origin: 'dao://home',
         data: {daoHome: 1, requestId, revision: 'revision-1', result: 'ok'},
       }))).toBe(true);
       await expect(request).resolves.toBe('ok');
     });

  it('rejects pending requests and clears ephemeral state on disconnect',
     async () => {
       const parent = {postMessage: vi.fn()} as unknown as Window;
       const channel = new GeneratedAppChannel(
           parent, 'dao://home', 'revision-1');
       const request = channel.call('sources.collect', {connectorId: 'feed'});

       channel.disconnect();

       await expect(request).rejects.toThrow('Home runtime disconnected');
       expect(channel.pendingCountForTesting).toBe(0);
     });

  it('preserves typed connector failure codes for card-local states',
     async () => {
       const parent = {postMessage: vi.fn()} as unknown as Window;
       const channel = new GeneratedAppChannel(
           parent, 'dao://home', 'revision-1');
       const request = channel.call('sources.collect', {connectorId: 'feed'});
       const requestId = parent.postMessage.mock.calls[0]![0].requestId;

       channel.handleMessage(new MessageEvent('message', {
         source: parent,
         origin: 'dao://home',
         data: {
           daoHome: 1,
           requestId,
           revision: 'revision-1',
           error: 'Sign in required',
           code: 'auth_required',
         },
       }));

       await expect(request).rejects.toMatchObject({
         message: 'Sign in required',
         code: 'auth_required',
       });
     });

  it('turns bounded media bytes into a local object URL', () => {
    const create = vi.fn().mockReturnValue('blob:home-media');

    expect(createMediaObjectUrl({
      mime: 'image/png',
      base64: btoa('png'),
    }, create)).toBe('blob:home-media');
    expect(create).toHaveBeenCalledWith(expect.any(Blob));
    expect(() => createMediaObjectUrl({
      mime: 'text/html',
      base64: btoa('unsafe'),
    }, create)).toThrow('Invalid Home media response');
  });
});
