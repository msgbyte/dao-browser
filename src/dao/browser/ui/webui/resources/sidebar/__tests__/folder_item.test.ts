// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {
  afterEach,
  beforeAll,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from 'vitest';

import type {FolderData, TabData} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

interface TestFolderItem extends HTMLElement {
  folder: FolderData;
  matchedTabs: TabData[];
  sessionId: number;
  updateComplete: Promise<boolean>;
  willUpdate: (changedProperties: Map<PropertyKey, unknown>) => void;
  updated: (changedProperties: Map<PropertyKey, unknown>) => void;
}

const originalAnimateDescriptor =
    Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'animate');
const originalScrollIntoViewDescriptor =
    Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'scrollIntoView');

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

function folder(extra: Partial<FolderData> = {}): FolderData {
  return {
    type: 'folder',
    id: 'folder-1',
    name: 'Work',
    collapsed: false,
    children: [],
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

describe('dao-folder-item', () => {
  beforeAll(async () => {
    await import('../dao_folder_item.js');
    const ctor = customElements.get('dao-tab-item') as
        CustomElementConstructor & {
          invokeLifecycleCallbacksForTesting?: boolean;
        };
    ctor.invokeLifecycleCallbacksForTesting = true;
  });

  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    restoreDescriptor(
        HTMLElement.prototype, 'animate', originalAnimateDescriptor);
    restoreDescriptor(
        HTMLElement.prototype, 'scrollIntoView',
        originalScrollIntoViewDescriptor);
  });

  function createFolderItem() {
    const el = document.createElement('dao-folder-item') as TestFolderItem;
    el.sessionId = 7;
    el.folder = folder();
    el.matchedTabs = [
      tab({tabId: 'tab-a', index: 1, title: 'A'}),
      tab({tabId: 'tab-b', index: 2, title: 'B'}),
    ];
    document.body.appendChild(el);
    return el;
  }

  it('suppresses active auto-scroll for children while collapsed', async () => {
    const scrollIntoView = vi.fn();
    Object.defineProperty(HTMLElement.prototype, 'scrollIntoView', {
      configurable: true,
      value: scrollIntoView,
    });
    const el = createFolderItem();
    el.folder = folder({collapsed: true});
    el.matchedTabs = [tab({tabId: 'tab-a', isActive: false})];
    await el.updateComplete;

    const child = el.shadowRoot!.querySelector('dao-tab-item') as HTMLElement & {
      active: boolean;
      autoScrollToken: number;
      suppressActiveAutoScroll: boolean;
      updateComplete: Promise<boolean>;
    };
    await child.updateComplete;
    expect(child.suppressActiveAutoScroll).toBe(true);

    scrollIntoView.mockClear();
    child.active = true;
    await child.updateComplete;
    expect(scrollIntoView).not.toHaveBeenCalled();

    child.autoScrollToken = 1;
    await child.updateComplete;
    expect(scrollIntoView).toHaveBeenCalledWith({
      block: 'nearest',
      behavior: 'smooth',
    });
  });

  it('animates surviving folder children when a child tab is removed',
      async () => {
        const animate = installAnimateMock();
        const el = createFolderItem();
        await el.updateComplete;

        let tabItems = el.shadowRoot!.querySelectorAll(
            '.children-inner dao-tab-item');
        setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
        setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);

        el.willUpdate(new Map<PropertyKey, unknown>(
            [['matchedTabs', el.matchedTabs]]));
        el.matchedTabs = [tab({tabId: 'tab-b', index: 1, title: 'B'})];
        await el.updateComplete;

        tabItems = el.shadowRoot!.querySelectorAll(
            '.children-inner dao-tab-item');
        setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
        el.updated(new Map<PropertyKey, unknown>(
            [['matchedTabs', [
              tab({tabId: 'tab-a', index: 1, title: 'A'}),
              tab({tabId: 'tab-b', index: 2, title: 'B'}),
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

  it('skips folder child close motion while the folder is collapsed',
      async () => {
        const animate = installAnimateMock();
        const el = createFolderItem();
        await el.updateComplete;

        let tabItems = el.shadowRoot!.querySelectorAll(
            '.children-inner dao-tab-item');
        setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
        setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);

        el.willUpdate(new Map<PropertyKey, unknown>(
            [['matchedTabs', el.matchedTabs]]));
        el.folder = folder({collapsed: true});
        el.matchedTabs = [tab({tabId: 'tab-b', index: 1, title: 'B'})];
        await el.updateComplete;

        tabItems = el.shadowRoot!.querySelectorAll(
            '.children-inner dao-tab-item');
        setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
        el.updated(new Map<PropertyKey, unknown>(
            [['matchedTabs', [
              tab({tabId: 'tab-a', index: 1, title: 'A'}),
              tab({tabId: 'tab-b', index: 2, title: 'B'}),
            ]]]));

        expect(animate).not.toHaveBeenCalled();
      });

  it('skips folder child close motion while child drop is active',
      async () => {
        const animate = installAnimateMock();
        const el = createFolderItem();
        await el.updateComplete;

        let tabItems = el.shadowRoot!.querySelectorAll(
            '.children-inner dao-tab-item');
        setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
        setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);

        (el as unknown as {childDropIndex_: number}).childDropIndex_ = 0;
        el.willUpdate(new Map<PropertyKey, unknown>(
            [['matchedTabs', el.matchedTabs]]));
        el.matchedTabs = [tab({tabId: 'tab-b', index: 1, title: 'B'})];
        await el.updateComplete;

        tabItems = el.shadowRoot!.querySelectorAll(
            '.children-inner dao-tab-item');
        setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
        el.updated(new Map<PropertyKey, unknown>(
            [['matchedTabs', [
              tab({tabId: 'tab-a', index: 1, title: 'A'}),
              tab({tabId: 'tab-b', index: 2, title: 'B'}),
            ]]]));

        expect(animate).not.toHaveBeenCalled();
      });

  it('requests native context menu on folder right click', async () => {
    const el = createFolderItem();
    await el.updateComplete;

    let menuDetail: unknown = null;
    el.addEventListener('folder-context-menu', (e: Event) => {
      menuDetail = (e as CustomEvent).detail;
    });

    const row = el.shadowRoot!.querySelector('.folder-row')!;
    row.dispatchEvent(new MouseEvent('contextmenu', {
      bubbles: true,
      cancelable: true,
      composed: true,
      screenX: 42,
      screenY: 64,
      clientX: 42,
      clientY: 64,
    }));
    await el.updateComplete;

    expect(menuDetail).toEqual({
      folderId: 'folder-1',
      screenX: 42,
      screenY: 64,
    });
    expect(el.shadowRoot!.querySelector('.context-menu')).toBeNull();
  });
});
