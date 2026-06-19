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
import {FolderModel} from '../dao_folder_model.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

interface TestTabList extends HTMLElement {
  tabs: TabData[];
  sessionId: number;
  folderModel: FolderModel | null;
  folderModelVersion: number;
  updateComplete: Promise<boolean>;
  willUpdate: (changedProperties: Map<PropertyKey, unknown>) => void;
  updated: (changedProperties: Map<PropertyKey, unknown>) => void;
}

interface FakeDataTransfer {
  dropEffect: string;
  effectAllowed: string;
  types: string[];
  getData: ReturnType<typeof vi.fn<[string], string>>;
  setData: ReturnType<typeof vi.fn<[string, string], void>>;
}

const originalAnimateDescriptor =
    Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'animate');

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

function installAnimateMock() {
  const animate = vi.fn(() => ({
    cancel: vi.fn(),
    finished: Promise.resolve(),
  } as unknown as Animation));
  Object.defineProperty(HTMLElement.prototype, 'animate', {
    configurable: true,
    value: animate,
  });
  return animate;
}

function restoreDescriptor(
    target: object, key: PropertyKey, descriptor?: PropertyDescriptor) {
  if (descriptor) {
    Object.defineProperty(target, key, descriptor);
    return;
  }

  Reflect.deleteProperty(target, key);
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
    restoreDescriptor(
        HTMLElement.prototype, 'animate', originalAnimateDescriptor);
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

  function createModelList(
      model: FolderModel, tabs: TabData[], send = vi.fn()) {
    (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
    const el = document.createElement('dao-tab-list') as TestTabList;
    el.sessionId = 7;
    el.tabs = tabs;
    el.folderModel = model;
    document.body.appendChild(el);
    return {el, send};
  }

  function createFolderModel(items: unknown[]): FolderModel {
    const model = new FolderModel();
    expect(model.loadFromJson(JSON.stringify({version: 1, items}))).toBe(true);
    return model;
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

  it('animates surviving tabs when a tab is removed', async () => {
    const animate = installAnimateMock();
    const {el} = createList();
    await el.updateComplete;

    let tabItems = el.shadowRoot!.querySelectorAll('dao-tab-item');
    setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
    setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);

    el.willUpdate(new Map<PropertyKey, unknown>([['tabs', el.tabs]]));
    el.tabs = [tab({tabId: 'tab-b', index: 1, title: 'B'})];
    await el.updateComplete;

    tabItems = el.shadowRoot!.querySelectorAll('dao-tab-item');
    setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
    el.updated(new Map<PropertyKey, unknown>([['tabs', [
      tab({tabId: 'tab-a', index: 0, title: 'A'}),
      tab({tabId: 'tab-b', index: 1, title: 'B'}),
    ]]]));

    expect(animate).toHaveBeenCalledWith(
        [
          {transform: 'translate(0px, 38px)'},
          {transform: 'translate(0, 0)'},
        ],
        {
          duration: 140,
          easing: 'cubic-bezier(0.2, 0, 0, 1)',
        });
  });

  it('does not animate normal tabs for active-state-only updates', async () => {
    const animate = installAnimateMock();
    const {el} = createList();
    await el.updateComplete;

    let tabItems = el.shadowRoot!.querySelectorAll('dao-tab-item');
    setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
    setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);

    el.willUpdate(new Map<PropertyKey, unknown>([['tabs', el.tabs]]));
    el.tabs = [
      tab({tabId: 'tab-a', index: 1, title: 'A', isActive: false}),
      tab({tabId: 'tab-b', index: 2, title: 'B', isActive: true}),
    ];
    await el.updateComplete;

    tabItems = el.shadowRoot!.querySelectorAll('dao-tab-item');
    setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
    setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);
    el.updated(new Map<PropertyKey, unknown>([['tabs', el.tabs]]));

    expect(animate).not.toHaveBeenCalled();
  });

  it('does not animate when folder model version changes without removals',
      async () => {
        const animate = installAnimateMock();
        const tabA = tab({
          tabId: 'tab-a',
          index: 0,
          title: 'A',
          url: 'https://a.example/',
        });
        const tabB = tab({
          tabId: 'tab-b',
          index: 1,
          title: 'B',
          url: 'https://b.example/',
        });
        const tabC = tab({
          tabId: 'tab-c',
          index: 2,
          title: 'C',
          url: 'https://c.example/',
        });
        const model = createFolderModel([
          {
            type: 'folder',
            id: 'folder-f',
            name: 'Folder',
            collapsed: false,
            children: [
              {
                type: 'tab',
                tabId: 'tab-a',
                url: tabA.url,
                title: tabA.title,
              },
              {
                type: 'tab',
                tabId: 'tab-b',
                url: tabB.url,
                title: tabB.title,
              },
            ],
          },
          {
            type: 'tab',
            tabId: 'tab-c',
            url: tabC.url,
            title: tabC.title,
          },
        ]);
        const {el} = createModelList(model, [tabA, tabB, tabC]);
        await el.updateComplete;

        let surfaceItems = el.shadowRoot!.querySelectorAll(
            'dao-tab-item, dao-folder-item') as NodeListOf<HTMLElement>;
        setTabItemBounds(surfaceItems[0]!, 0, 120);
        setTabItemBounds(surfaceItems[1]!, 122, 36);

        const oldFolderModelVersion = el.folderModelVersion;
        el.willUpdate(new Map<PropertyKey, unknown>(
            [['folderModelVersion', oldFolderModelVersion]]));
        el.folderModelVersion = oldFolderModelVersion + 1;
        await el.updateComplete;

        surfaceItems = el.shadowRoot!.querySelectorAll(
            'dao-tab-item, dao-folder-item') as NodeListOf<HTMLElement>;
        setTabItemBounds(surfaceItems[0]!, 0, 120);
        setTabItemBounds(surfaceItems[1]!, 82, 36);
        el.updated(new Map<PropertyKey, unknown>(
            [['folderModelVersion', oldFolderModelVersion]]));

        expect(animate).not.toHaveBeenCalled();
      });

  it('animates a surviving folder host when a loose tab above it is removed',
      async () => {
        const animate = installAnimateMock();
        const tabA = tab({
          tabId: 'tab-a',
          index: 0,
          title: 'A',
          url: 'https://a.example/',
        });
        const tabB = tab({
          tabId: 'tab-b',
          index: 1,
          title: 'B',
          url: 'https://b.example/',
        });
        const tabC = tab({
          tabId: 'tab-c',
          index: 2,
          title: 'C',
          url: 'https://c.example/',
        });
        const model = createFolderModel([
          {
            type: 'tab',
            tabId: 'tab-a',
            url: tabA.url,
            title: tabA.title,
          },
          {
            type: 'folder',
            id: 'folder-f',
            name: 'Folder',
            collapsed: false,
            children: [{
              type: 'tab',
              tabId: 'tab-b',
              url: tabB.url,
              title: tabB.title,
            }],
          },
          {
            type: 'tab',
            tabId: 'tab-c',
            url: tabC.url,
            title: tabC.title,
          },
        ]);
        const {el} = createModelList(model, [tabA, tabB, tabC]);
        await el.updateComplete;

        let surfaceItems = el.shadowRoot!.querySelectorAll(
            'dao-tab-item, dao-folder-item') as NodeListOf<HTMLElement>;
        setTabItemBounds(surfaceItems[0]!, 0, 36);
        setTabItemBounds(surfaceItems[1]!, 38, 80);
        setTabItemBounds(surfaceItems[2]!, 120, 36);

        el.willUpdate(new Map<PropertyKey, unknown>([['tabs', el.tabs]]));
        el.tabs = [tabB, tabC];
        await el.updateComplete;

        surfaceItems = el.shadowRoot!.querySelectorAll(
            'dao-tab-item, dao-folder-item') as NodeListOf<HTMLElement>;
        setTabItemBounds(surfaceItems[0]!, 0, 80);
        setTabItemBounds(surfaceItems[1]!, 82, 36);
        el.updated(new Map<PropertyKey, unknown>(
            [['tabs', [tabA, tabB, tabC]]]));

        const folderHost =
            el.shadowRoot!.querySelector('dao-folder-item') as HTMLElement;
        expect(animate.mock.contexts).toContain(folderHost);
        expect(animate).toHaveBeenCalledWith(
            [
              {transform: 'translate(0px, 38px)'},
              {transform: 'translate(0, 0)'},
            ],
            {
              duration: 140,
              easing: 'cubic-bezier(0.2, 0, 0, 1)',
            });
      });

  it('animates a following top-level tab when a folder child is removed',
      async () => {
        const animate = installAnimateMock();
        const tabA = tab({
          tabId: 'tab-a',
          index: 0,
          title: 'A',
          url: 'https://a.example/',
        });
        const tabB = tab({
          tabId: 'tab-b',
          index: 1,
          title: 'B',
          url: 'https://b.example/',
        });
        const tabC = tab({
          tabId: 'tab-c',
          index: 2,
          title: 'C',
          url: 'https://c.example/',
        });
        const model = createFolderModel([
          {
            type: 'folder',
            id: 'folder-f',
            name: 'Folder',
            collapsed: false,
            children: [
              {
                type: 'tab',
                tabId: 'tab-a',
                url: tabA.url,
                title: tabA.title,
              },
              {
                type: 'tab',
                tabId: 'tab-b',
                url: tabB.url,
                title: tabB.title,
              },
            ],
          },
          {
            type: 'tab',
            tabId: 'tab-c',
            url: tabC.url,
            title: tabC.title,
          },
        ]);
        const {el} = createModelList(model, [tabA, tabB, tabC]);
        await el.updateComplete;

        let surfaceItems = el.shadowRoot!.querySelectorAll(
            'dao-tab-item, dao-folder-item') as NodeListOf<HTMLElement>;
        setTabItemBounds(surfaceItems[0]!, 0, 120);
        setTabItemBounds(surfaceItems[1]!, 122, 36);

        const oldFolderModelVersion = el.folderModelVersion;
        el.willUpdate(new Map<PropertyKey, unknown>(
            [['folderModelVersion', oldFolderModelVersion]]));
        el.tabs = [tabB, tabC];
        el.folderModelVersion = oldFolderModelVersion + 1;
        await el.updateComplete;

        surfaceItems = el.shadowRoot!.querySelectorAll(
            'dao-tab-item, dao-folder-item') as NodeListOf<HTMLElement>;
        setTabItemBounds(surfaceItems[0]!, 0, 80);
        setTabItemBounds(surfaceItems[1]!, 82, 36);
        el.updated(new Map<PropertyKey, unknown>(
            [['folderModelVersion', oldFolderModelVersion]]));

        const followingTab =
            el.shadowRoot!.querySelector('dao-tab-item') as HTMLElement;
        expect(animate.mock.contexts).toContain(followingTab);
        expect(animate).toHaveBeenCalledWith(
            [
              {transform: 'translate(0px, 40px)'},
              {transform: 'translate(0, 0)'},
            ],
            {
              duration: 140,
              easing: 'cubic-bezier(0.2, 0, 0, 1)',
            });
      });

  it('skips normal tab close motion while a tab drag is active', async () => {
    const animate = installAnimateMock();
    const {el} = createList();
    await el.updateComplete;

    const tabItems = el.shadowRoot!.querySelectorAll('dao-tab-item');
    setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
    setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);

    (el as unknown as {tabDragActivated_: boolean}).tabDragActivated_ = true;
    el.willUpdate(new Map<PropertyKey, unknown>([['tabs', el.tabs]]));
    el.tabs = [tab({tabId: 'tab-b', index: 1, title: 'B'})];
    await el.updateComplete;
    const updatedItems = el.shadowRoot!.querySelectorAll('dao-tab-item');
    setTabItemBounds(updatedItems[0] as HTMLElement, 0, 36);
    el.updated(new Map<PropertyKey, unknown>([['tabs', el.tabs]]));

    expect(animate).not.toHaveBeenCalled();
  });

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

  it('keeps duplicate URL tabs distinct when computing context menu order',
      async () => {
        const send = vi.fn();
        const duplicateA = tab({
          tabId: 'tab-a',
          index: 0,
          title: 'Docs',
          url: 'https://docs.example/',
        });
        const duplicateB = tab({
          tabId: 'tab-b',
          index: 1,
          title: 'Docs',
          url: 'https://docs.example/',
        });
        const tabC = tab({
          tabId: 'tab-c',
          index: 2,
          title: 'C',
          url: 'https://c.example/',
        });
        const model = createFolderModel([
          {
            type: 'folder',
            id: 'folder-f',
            name: 'Folder',
            collapsed: false,
            children: [
              {
                type: 'tab',
                url: duplicateA.url,
                title: duplicateA.title,
              },
              {
                type: 'tab',
                url: duplicateB.url,
                title: duplicateB.title,
              },
            ],
          },
          {
            type: 'tab',
            tabId: 'tab-c',
            url: tabC.url,
            title: tabC.title,
          },
        ]);
        const {el} = createModelList(model, [duplicateA, duplicateB, tabC], send);
        await el.updateComplete;

        el.dispatchEvent(new CustomEvent('tab-context-menu', {
          bubbles: true,
          composed: true,
          detail: {
            index: 1,
            screenX: 20,
            screenY: 30,
          },
        }));

        expect(send).toHaveBeenCalledWith(
            'showTabContextMenu', [1, 20, 30, [0, 1], [0, 1, 2]]);
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
