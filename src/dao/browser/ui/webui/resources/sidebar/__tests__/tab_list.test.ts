// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeAll, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  clearActivePinnedItemDragId,
  PINNED_ITEM_DRAG_MIME_TYPE,
  setActivePinnedItemDragId,
} from '../sidebar_bridge.js';
import type {TabData} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

interface TestTabList extends HTMLElement {
  tabs: TabData[];
  sessionId: number;
  updateComplete: Promise<boolean>;
}

interface FakeDataTransfer {
  dropEffect: string;
  effectAllowed: string;
  types: string[];
  getData: ReturnType<typeof vi.fn<[string], string>>;
  setData: ReturnType<typeof vi.fn<[string, string], void>>;
}

function tab(extra: Partial<TabData> = {}): TabData {
  return {
    tabId: 'tab-1',
    index: 0,
    title: 'Docs',
    url: 'https://docs.example/',
    faviconUrl: '',
    isActive: false,
    isPinned: false,
    isAudible: false,
    isMuted: false,
    ...extra,
  };
}

function fakeDataTransfer(initialData: Record<string, string>): FakeDataTransfer {
  const data = new Map<string, string>(Object.entries(initialData));
  const fake: FakeDataTransfer = {
    dropEffect: '',
    effectAllowed: '',
    types: Object.keys(initialData),
    getData: vi.fn((format: string) => data.get(format) || ''),
    setData: vi.fn((format: string, value: string) => {
      data.set(format, value);
      if (!fake.types.includes(format)) {
        fake.types.push(format);
      }
    }),
  };
  return fake;
}

function dragEvent(
    type: string, dataTransfer?: FakeDataTransfer,
    extra: Partial<DragEvent> = {}): DragEvent {
  const event = new Event(type, {
    bubbles: true,
    cancelable: true,
    composed: true,
  }) as DragEvent;
  Object.defineProperty(event, 'dataTransfer', {
    value: dataTransfer ?? null,
  });
  for (const [key, value] of Object.entries(extra)) {
    Object.defineProperty(event, key, {value});
  }
  return event;
}

describe('dao-tab-list', () => {
  beforeAll(async () => {
    await import('../dao_tab_list.js');
  });

  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    clearActivePinnedItemDragId();
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
  });

  function createList(send = vi.fn()) {
    (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
    const el = document.createElement('dao-tab-list') as TestTabList;
    el.sessionId = 7;
    el.tabs = [
      tab({tabId: 'tab-a', index: 1, title: 'A'}),
      tab({tabId: 'tab-b', index: 2, title: 'B'}),
    ];
    document.body.appendChild(el);
    return {el, send};
  }

  function setTabItemBounds(el: HTMLElement, top: number, height: number) {
    Object.defineProperty(el, 'offsetTop', {configurable: true, value: top});
    Object.defineProperty(el, 'offsetHeight', {
      configurable: true,
      value: height,
    });
    el.getBoundingClientRect = () => ({
      top,
      bottom: top + height,
      left: 0,
      right: 200,
      width: 200,
      height,
      x: 0,
      y: top,
      toJSON: () => ({}),
    });
  }

  it('unpins a pinned item at the normal tab list drop index', async () => {
    const {el, send} = createList();
    await el.updateComplete;

    const dataTransfer = fakeDataTransfer({
      [PINNED_ITEM_DRAG_MIME_TYPE]: 'pin-docs',
      'text/plain': 'pin-docs',
    });
    const tabItems = el.shadowRoot!.querySelectorAll('dao-tab-item');
    (tabItems[0] as unknown as {tabData: TabData}).tabData =
        tab({tabId: 'tab-a', index: 1, title: 'A'});
    (tabItems[1] as unknown as {tabData: TabData}).tabData =
        tab({tabId: 'tab-b', index: 2, title: 'B'});
    setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
    setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);
    const target = tabItems[1] as HTMLElement;

    const dragOver = dragEvent('dragover', dataTransfer, {clientY: 45});
    target.dispatchEvent(dragOver);
    expect(dragOver.defaultPrevented).toBe(true);
    expect(dataTransfer.dropEffect).toBe('move');

    const drop = dragEvent('drop', dataTransfer, {clientY: 45});
    target.dispatchEvent(drop);

    expect(drop.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledWith('unpinPinnedItem', ['pin-docs', 2]);
  });

  it('unpins a pinned item when drag data hides the custom MIME payload',
      async () => {
        const {el, send} = createList();
        await el.updateComplete;

        setActivePinnedItemDragId('pin-dormant');
        const dataTransfer = fakeDataTransfer({'text/plain': 'pin-dormant'});
        const tabItems = el.shadowRoot!.querySelectorAll('dao-tab-item');
        (tabItems[0] as unknown as {tabData: TabData}).tabData =
            tab({tabId: 'tab-a', index: 1, title: 'A'});
        (tabItems[1] as unknown as {tabData: TabData}).tabData =
            tab({tabId: 'tab-b', index: 2, title: 'B'});
        setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
        setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);
        const target = tabItems[1] as HTMLElement;

        const dragOver = dragEvent('dragover', dataTransfer, {clientY: 45});
        target.dispatchEvent(dragOver);
        expect(dragOver.defaultPrevented).toBe(true);
        expect(dataTransfer.dropEffect).toBe('move');

        const drop = dragEvent('drop', dataTransfer, {clientY: 45});
        target.dispatchEvent(drop);

        expect(drop.defaultPrevented).toBe(true);
        expect(send).toHaveBeenCalledWith(
            'unpinPinnedItem', ['pin-dormant', 2]);
      });

  it('activates native tab drag as soon as a tab drag starts', async () => {
    const {el, send} = createList();
    await el.updateComplete;

    const tabItem = el.shadowRoot!.querySelector('dao-tab-item') as HTMLElement;
    const dataTransfer = fakeDataTransfer({'text/plain': 'dao-tab-drag:7:1'});
    tabItem.dispatchEvent(dragEvent('dragstart', dataTransfer));

    expect(send).toHaveBeenCalledWith('tabDragActive', [true]);

    tabItem.dispatchEvent(dragEvent('dragend', dataTransfer));
    expect(send).toHaveBeenCalledWith('tabDragActive', [false]);
  });

  it('activates native tab drag when leaving at the viewport edge', async () => {
    const {el, send} = createList();
    await el.updateComplete;

    Object.defineProperty(window, 'innerWidth', {
      configurable: true,
      value: 240,
    });
    Object.defineProperty(window, 'innerHeight', {
      configurable: true,
      value: 800,
    });

    el.dispatchEvent(dragEvent('dragleave', undefined, {
      clientX: 240,
      clientY: 120,
      relatedTarget: null,
    }));

    expect(send).toHaveBeenCalledWith('setDropInsertIndex', [-1]);
    expect(send).toHaveBeenCalledWith('tabDragActive', [true]);
  });
});
