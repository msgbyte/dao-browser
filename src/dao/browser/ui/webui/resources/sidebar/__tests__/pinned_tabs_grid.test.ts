// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  clearActivePinnedItemDragId,
  PINNED_ITEM_DRAG_MIME_TYPE,
  SIDEBAR_POINTER_EXITED_EVENT,
  TAB_DRAG_MIME_TYPE,
} from '../sidebar_bridge.js';
import type {PinnedItemData, SidebarState} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

const originalAnimateDescriptor =
    Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'animate');

function item(extra: Partial<PinnedItemData> = {}): PinnedItemData {
  return {
    id: 'pin-1',
    title: 'GitHub',
    url: 'https://github.com/',
    faviconUrl: '',
    isOpen: true,
    openTabIndex: 0,
    isActive: false,
    ...extra,
  };
}

async function loadGrid() {
  vi.resetModules();
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  await import('../dao_pinned_tabs_grid.js');
  const el = document.createElement('dao-pinned-tabs-grid') as HTMLElement & {
    items: PinnedItemData[];
    sessionId: number;
    updateComplete: Promise<boolean>;
    willUpdate: (changedProperties: Map<PropertyKey, unknown>) => void;
    updated: (changedProperties: Map<PropertyKey, unknown>) => void;
  };
  document.body.appendChild(el);
  return {el, send};
}

interface FakeDataTransfer {
  dropEffect: string;
  effectAllowed: string;
  types: string[];
  getData: ReturnType<typeof vi.fn<[string], string>>;
  setData: ReturnType<typeof vi.fn<[string, string], void>>;
}

function fakeDataTransfer(initialData = ''): FakeDataTransfer {
  const data = new Map<string, string>();
  if (initialData) {
    data.set('text/plain', initialData);
  }
  const fake: FakeDataTransfer = {
    dropEffect: '',
    effectAllowed: '',
    types: initialData ? ['text/plain'] : [],
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

function protectedTabDragDataTransfer(payload: string): FakeDataTransfer {
  const dataTransfer = fakeDataTransfer();
  dataTransfer.types.push(TAB_DRAG_MIME_TYPE, 'text/plain');
  dataTransfer.getData.mockImplementation((format: string) => {
    if (format === TAB_DRAG_MIME_TYPE || format === 'text/plain') {
      return payload;
    }
    return '';
  });
  return dataTransfer;
}

function dragEvent(
    type: string,
    dataTransfer?: FakeDataTransfer,
    extra: Partial<DragEvent> = {}): DragEvent {
  const event = new Event(type, {
    bubbles: true,
    cancelable: true,
  }) as DragEvent;
  Object.defineProperty(event, 'dataTransfer', {
    value: dataTransfer ?? null,
  });
  for (const [key, value] of Object.entries(extra)) {
    Object.defineProperty(event, key, {value});
  }
  return event;
}

function renderedTileOrder(el: HTMLElement): string[] {
  return Array.from(
      el.shadowRoot!.querySelectorAll('.tile, .drag-placeholder'))
      .map(node => {
        const element = node as HTMLElement;
        if (element.classList.contains('drag-placeholder')) {
          return 'placeholder';
        }
        return element.getAttribute('aria-label') || '';
      });
}

function setTileBounds(element: HTMLElement, left: number, top: number) {
  element.getBoundingClientRect = () => ({
    top,
    bottom: top + 56,
    left,
    right: left + 56,
    width: 56,
    height: 56,
    x: left,
    y: top,
    toJSON: () => ({}),
  });
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

describe('dao-pinned-tabs-grid', () => {
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

  it('renders active, open, and dormant tile states', async () => {
    const {el} = await loadGrid();
    el.items = [
      item({id: 'active', isActive: true, title: 'Active'}),
      item({id: 'open', isActive: false, title: 'Open'}),
      item({id: 'dormant', isOpen: false, openTabIndex: -1, title: 'Dormant'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    expect(tiles).toHaveLength(3);
    expect(tiles[0]!.classList.contains('active')).toBe(true);
    expect(tiles[1]!.classList.contains('open')).toBe(true);
    expect(tiles[2]!.classList.contains('dormant')).toBe(true);
  });

  it('keeps empty responsive grid tracks for sparse pinned tiles', async () => {
    await loadGrid();
    const ctor = customElements.get('dao-pinned-tabs-grid') as
        typeof HTMLElement & {styles: {strings: string[]}};
    const cssText = ctor.styles.strings.join('');

    expect(cssText).toContain('grid-template-columns: repeat(auto-fill,');
    expect(cssText).toContain('minmax(');
    expect(cssText).not.toContain('grid-template-columns: repeat(auto-fit,');
    expect(cssText).not.toContain('grid-template-columns: repeat(3, 1fr)');
  });

  it('animates surviving pinned tiles when a pinned item disappears',
      async () => {
        const animate = installAnimateMock();
        const {el} = await loadGrid();
        el.items = [
          item({id: 'pin-a', title: 'A'}),
          item({id: 'pin-b', title: 'B'}),
          item({id: 'pin-c', title: 'C'}),
        ];
        await el.updateComplete;

        let tiles = el.shadowRoot!.querySelectorAll('.tile');
        setTileBounds(tiles[0] as HTMLElement, 0, 0);
        setTileBounds(tiles[1] as HTMLElement, 62, 0);
        setTileBounds(tiles[2] as HTMLElement, 124, 0);

        const changedProperties = new Map<PropertyKey, unknown>([
          ['items', el.items],
        ]);
        // The Lit test shim does not drive these hooks, so call them after
        // setting jsdom bounds for FLIP snapshots.
        el.willUpdate(changedProperties);
        el.items = [
          item({id: 'pin-b', title: 'B'}),
          item({id: 'pin-c', title: 'C'}),
        ];
        await el.updateComplete;

        tiles = el.shadowRoot!.querySelectorAll('.tile');
        setTileBounds(tiles[0] as HTMLElement, 0, 0);
        setTileBounds(tiles[1] as HTMLElement, 62, 0);
        el.updated(changedProperties);

        expect(animate).toHaveBeenCalledTimes(2);
        expect(animate).toHaveBeenNthCalledWith(
            1,
            [
              {transform: 'translate(62px, 0px)'},
              {transform: 'translate(0, 0)'},
            ],
            {
              duration: 140,
              easing: 'cubic-bezier(0.2, 0, 0, 1)',
            });
        expect(animate).toHaveBeenNthCalledWith(
            2,
            [
              {transform: 'translate(62px, 0px)'},
              {transform: 'translate(0, 0)'},
            ],
            {
              duration: 140,
              easing: 'cubic-bezier(0.2, 0, 0, 1)',
            });
      });

  it('skips close motion while a tab drag preview placeholder is visible',
      async () => {
        const animate = installAnimateMock();
        const {el} = await loadGrid();
        el.sessionId = 7;
        el.items = [
          item({id: 'pin-a', title: 'A'}),
          item({id: 'pin-b', title: 'B'}),
          item({id: 'pin-c', title: 'C'}),
        ];
        await el.updateComplete;

        const dataTransfer = protectedTabDragDataTransfer('dao-tab-drag:7:4');
        const tiles = el.shadowRoot!.querySelectorAll('.tile');
        tiles[1]!.dispatchEvent(dragEvent('dragover', dataTransfer));
        await el.updateComplete;
        expect(renderedTileOrder(el)).toEqual(['A', 'placeholder', 'B', 'C']);

        const renderedTiles = el.shadowRoot!.querySelectorAll('.tile');
        setTileBounds(renderedTiles[0] as HTMLElement, 0, 0);
        setTileBounds(renderedTiles[1] as HTMLElement, 62, 0);
        setTileBounds(renderedTiles[2] as HTMLElement, 124, 0);

        const changedProperties = new Map<PropertyKey, unknown>([
          ['items', el.items],
        ]);
        // The Lit test shim does not drive these hooks, so call them after
        // setting jsdom bounds for FLIP snapshots.
        el.willUpdate(changedProperties);
        el.items = [
          item({id: 'pin-b', title: 'B'}),
          item({id: 'pin-c', title: 'C'}),
        ];
        await el.updateComplete;
        el.updated(changedProperties);

        expect(animate).not.toHaveBeenCalled();
      });

  it('skips close motion while an internal pinned drag placeholder is visible',
      async () => {
        const animate = installAnimateMock();
        const {el} = await loadGrid();
        el.sessionId = 7;
        el.items = [
          item({id: 'pin-a', title: 'A'}),
          item({id: 'pin-b', title: 'B'}),
          item({id: 'pin-c', title: 'C'}),
        ];
        await el.updateComplete;

        let tiles = el.shadowRoot!.querySelectorAll('.tile');
        const dataTransfer = fakeDataTransfer();
        tiles[0]!.dispatchEvent(dragEvent('dragstart', dataTransfer));
        tiles[1]!.dispatchEvent(dragEvent('dragover', dataTransfer));
        await el.updateComplete;
        expect(renderedTileOrder(el)).toEqual(['B', 'placeholder', 'C']);

        tiles = el.shadowRoot!.querySelectorAll('.tile');
        setTileBounds(tiles[0] as HTMLElement, 0, 0);
        setTileBounds(tiles[1] as HTMLElement, 62, 0);

        const changedProperties = new Map<PropertyKey, unknown>([
          ['items', el.items],
        ]);
        // The Lit test shim does not drive these hooks, so call them after
        // setting jsdom bounds for FLIP snapshots.
        el.willUpdate(changedProperties);
        el.items = [
          item({id: 'pin-b', title: 'B'}),
          item({id: 'pin-c', title: 'C'}),
        ];
        await el.updateComplete;
        el.updated(changedProperties);

        expect(animate).not.toHaveBeenCalled();

        tiles = el.shadowRoot!.querySelectorAll('.tile');
        tiles[0]!.dispatchEvent(dragEvent('dragend', dataTransfer));
      });

  it('activates or opens the clicked pinned item', async () => {
    const {el, send} = await loadGrid();
    el.items = [item({id: 'pin-click'})];
    await el.updateComplete;

    const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
    tile.click();

    expect(send).toHaveBeenCalledWith(
        'activateOrOpenPinnedItem', ['pin-click']);
  });

  it('prevents favicon images from becoming the drag source', async () => {
    const {el} = await loadGrid();
    el.items = [item({
      id: 'pin-favicon',
      faviconUrl: 'chrome://favicon2/?url=https://ublockorigin.com/',
    })];
    await el.updateComplete;

    const favicon = el.shadowRoot!.querySelector('.favicon') as HTMLImageElement;
    expect(favicon.draggable).toBe(false);
  });

  it('renders pinned tiles as icons without visible labels', async () => {
    const {el} = await loadGrid();
    el.items = [item({id: 'pin-icon-only', title: 'GitHub'})];
    await el.updateComplete;

    const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
    expect(tile.querySelector('.title')).toBeNull();
    expect(tile.textContent?.trim()).toBe('');
    expect(tile.getAttribute('aria-label')).toBe('GitHub');
  });

  it('shows the full tab tooltip after hovering a pinned item', async () => {
    vi.useFakeTimers();
    try {
      const {el, send} = await loadGrid();
      const fullTitle = 'Pinned Docs With A Long Title';
      el.items = [item({id: 'pin-tooltip', title: fullTitle})];
      await el.updateComplete;

      const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
      tile.dispatchEvent(new MouseEvent('mouseenter', {
        screenX: 20,
        screenY: 30,
      }));
      vi.advanceTimersByTime(1499);
      expect(send).not.toHaveBeenCalled();

      vi.advanceTimersByTime(1);
      expect(send).toHaveBeenCalledWith(
          'showTabTooltip', [24, 34, fullTitle]);

      tile.dispatchEvent(new MouseEvent('mouseleave'));
      expect(send).toHaveBeenLastCalledWith('hideTabTooltip', []);
    } finally {
      vi.useRealTimers();
    }
  });

  it('does not show a scheduled pinned tooltip after drag starts', async () => {
    vi.useFakeTimers();
    try {
      const {el, send} = await loadGrid();
      el.sessionId = 9;
      el.items = [item({id: 'pin-tooltip-drag', title: 'Pinned Docs'})];
      await el.updateComplete;

      const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
      tile.dispatchEvent(new MouseEvent('mouseenter', {
        screenX: 20,
        screenY: 30,
      }));
      vi.advanceTimersByTime(1000);

      tile.dispatchEvent(dragEvent('dragstart', fakeDataTransfer()));
      vi.advanceTimersByTime(600);

      expect(send).not.toHaveBeenCalledWith(
          'showTabTooltip', [24, 34, 'Pinned Docs']);
    } finally {
      vi.useRealTimers();
    }
  });

  it('clears tooltip and suppresses hover when the sidebar pointer exits',
      async () => {
        vi.useFakeTimers();
        try {
          const {el, send} = await loadGrid();
          el.items = [item({id: 'pin-tooltip-exit', title: 'Pinned Docs'})];
          await el.updateComplete;

          const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
          tile.dispatchEvent(new MouseEvent('mouseenter', {
            screenX: 20,
            screenY: 30,
          }));
          vi.advanceTimersByTime(1000);

          window.dispatchEvent(new CustomEvent(SIDEBAR_POINTER_EXITED_EVENT));
          vi.advanceTimersByTime(600);
          await el.updateComplete;

          expect(send).toHaveBeenCalledWith('hideTabTooltip', []);
          expect(send).not.toHaveBeenCalledWith(
              'showTabTooltip', [24, 34, 'Pinned Docs']);
          expect(el.hasAttribute('hover-suppressed')).toBe(true);

          tile.dispatchEvent(new MouseEvent('mouseenter', {
            screenX: 22,
            screenY: 32,
          }));
          await el.updateComplete;
          expect(el.hasAttribute('hover-suppressed')).toBe(false);
        } finally {
          vi.useRealTimers();
        }
      });

  it('shows a pinned item context menu with the item id', async () => {
    const {el, send} = await loadGrid();
    el.items = [item({id: 'pin-menu'})];
    await el.updateComplete;

    const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
    tile.dispatchEvent(new MouseEvent('contextmenu', {
      bubbles: true,
      cancelable: true,
      screenX: 12,
      screenY: 34,
    }));

    expect(send).toHaveBeenCalledWith(
        'showPinnedItemContextMenu', ['pin-menu', 12, 34]);
  });

  it('moves a dragged pinned item to the dropped tile index', async () => {
    const {el, send} = await loadGrid();
    el.sessionId = 9;
    el.items = [
      item({id: 'pin-a', title: 'A', openTabIndex: 4}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dataTransfer = fakeDataTransfer();

    tiles[0]!.dispatchEvent(dragEvent('dragstart', dataTransfer));
    expect(dataTransfer.setData).toHaveBeenCalledWith(
        PINNED_ITEM_DRAG_MIME_TYPE, 'pin-a');
    expect(dataTransfer.setData).toHaveBeenCalledWith(
        TAB_DRAG_MIME_TYPE, 'dao-tab-drag:9:4');
    expect(dataTransfer.setData).toHaveBeenCalledWith(
        'text/plain', 'dao-tab-drag:9:4');
    expect(dataTransfer.effectAllowed).toBe('move');

    const dragOver = dragEvent('dragover', dataTransfer);
    tiles[1]!.dispatchEvent(dragOver);
    expect(dragOver.defaultPrevented).toBe(true);
    expect(dataTransfer.dropEffect).toBe('move');

    const drop = dragEvent('drop', dataTransfer);
    tiles[1]!.dispatchEvent(drop);

    expect(drop.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledWith('movePinnedItem', ['pin-a', 1]);
  });

  it('activates native tab drag as soon as an open pinned tab drag starts',
      async () => {
        const {el, send} = await loadGrid();
        el.sessionId = 9;
        el.items = [item({id: 'pin-a', title: 'A', openTabIndex: 4})];
        await el.updateComplete;

        Object.defineProperty(window, 'innerWidth', {
          configurable: true,
          value: 240,
        });
        Object.defineProperty(window, 'innerHeight', {
          configurable: true,
          value: 800,
        });

        const dataTransfer = fakeDataTransfer();
        const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
        const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
        tile.dispatchEvent(dragEvent('dragstart', dataTransfer));

        expect(send).toHaveBeenCalledWith('tabDragActive', [true]);

        grid.dispatchEvent(dragEvent('dragleave', dataTransfer, {
          clientX: 240,
          clientY: 120,
          relatedTarget: null,
        }));

        tile.dispatchEvent(dragEvent('dragend', dataTransfer));
        expect(send).toHaveBeenCalledWith('tabDragActive', [false]);
      });

  it('shows a grid placeholder at the pinned item drop position', async () => {
    const {el} = await loadGrid();
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    let tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dataTransfer = fakeDataTransfer();
    tiles[0]!.dispatchEvent(dragEvent('dragstart', dataTransfer));
    await el.updateComplete;

    expect(renderedTileOrder(el)).toEqual(['A', 'B', 'C']);

    tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dragOver = dragEvent('dragover', dataTransfer);
    tiles[1]!.dispatchEvent(dragOver);
    await el.updateComplete;

    expect(dragOver.defaultPrevented).toBe(true);
    expect(renderedTileOrder(el)).toEqual(['B', 'placeholder', 'C']);

    tiles[0]!.dispatchEvent(dragEvent('dragend', dataTransfer));
  });

  it('moves a pinned item dropped on the grid placeholder', async () => {
    const {el, send} = await loadGrid();
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    let tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dataTransfer = fakeDataTransfer();
    tiles[0]!.dispatchEvent(dragEvent('dragstart', dataTransfer));

    const dragOver = dragEvent('dragover', dataTransfer);
    tiles[1]!.dispatchEvent(dragOver);
    await el.updateComplete;

    const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
    const drop = dragEvent('drop', dataTransfer);
    grid.dispatchEvent(drop);

    expect(drop.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledWith('movePinnedItem', ['pin-a', 1]);

    tiles = el.shadowRoot!.querySelectorAll('.tile');
    tiles[0]!.dispatchEvent(dragEvent('dragend', dataTransfer));
  });

  it('centers a pinned item placeholder around the pointer on a pinned tile', async () => {
    const {el, send} = await loadGrid();
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    let tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dataTransfer = fakeDataTransfer();
    tiles[0]!.dispatchEvent(dragEvent('dragstart', dataTransfer));
    send.mockClear();

    tiles = el.shadowRoot!.querySelectorAll('.tile');
    vi.spyOn(tiles[1] as HTMLElement, 'getBoundingClientRect').mockReturnValue({
      bottom: 60,
      height: 56,
      left: 60,
      right: 116,
      top: 4,
      width: 56,
      x: 60,
      y: 4,
      toJSON: () => {},
    });

    const dragOver = dragEvent('dragover', dataTransfer);
    Object.defineProperties(dragOver, {
      clientX: {value: 70},
      clientY: {value: 30},
    });
    tiles[1]!.dispatchEvent(dragOver);
    await el.updateComplete;

    expect(dragOver.defaultPrevented).toBe(true);
    expect(renderedTileOrder(el)).toEqual(['placeholder', 'B', 'C']);

    const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
    const drop = dragEvent('drop', dataTransfer);
    grid.dispatchEvent(drop);
    await el.updateComplete;

    expect(drop.defaultPrevented).toBe(true);
    expect(send).not.toHaveBeenCalled();

    tiles = el.shadowRoot!.querySelectorAll('.tile');
    tiles[0]!.dispatchEvent(dragEvent('dragend', dataTransfer));
  });

  it('pins a same-window tab dropped on the pinned grid', async () => {
    const {el, send} = await loadGrid();
    el.sessionId = 7;
    el.items = [item({id: 'pin-a', title: 'A'})];
    await el.updateComplete;

    const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
    const dataTransfer = protectedTabDragDataTransfer('dao-tab-drag:7:3');
    dataTransfer.getData.mockReturnValueOnce('');
    const dragOver = dragEvent('dragover', dataTransfer);
    tile.dispatchEvent(dragOver);
    expect(dragOver.defaultPrevented).toBe(true);
    expect(dataTransfer.dropEffect).toBe('move');

    const drop = dragEvent('drop', dataTransfer);
    tile.dispatchEvent(drop);

    expect(drop.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledWith('pinTab', [3, 0]);
  });

  it('shows an end placeholder for a same-window tab dragged into the pinned grid', async () => {
    const {el, send} = await loadGrid();
    el.sessionId = 7;
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
    ];
    await el.updateComplete;

    const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
    const dataTransfer = protectedTabDragDataTransfer('dao-tab-drag:7:3');
    const dragOver = dragEvent('dragover', dataTransfer);
    grid.dispatchEvent(dragOver);
    await el.updateComplete;

    expect(dragOver.defaultPrevented).toBe(true);
    expect(dataTransfer.dropEffect).toBe('move');
    expect(renderedTileOrder(el)).toEqual(['A', 'B', 'placeholder']);

    const drop = dragEvent('drop', dataTransfer);
    grid.dispatchEvent(drop);
    await el.updateComplete;

    expect(drop.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledWith('pinTab', [3, 2]);
    expect(renderedTileOrder(el)).toEqual(['A', 'B']);
  });

  it('shows a middle placeholder for a same-window tab dragged over a pinned tile', async () => {
    const {el, send} = await loadGrid();
    el.sessionId = 7;
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    let tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dataTransfer = protectedTabDragDataTransfer('dao-tab-drag:7:4');
    const dragOver = dragEvent('dragover', dataTransfer);
    tiles[1]!.dispatchEvent(dragOver);
    await el.updateComplete;

    expect(dragOver.defaultPrevented).toBe(true);
    expect(dataTransfer.dropEffect).toBe('move');
    expect(renderedTileOrder(el)).toEqual(['A', 'placeholder', 'B', 'C']);

    const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
    const drop = dragEvent('drop', dataTransfer);
    grid.dispatchEvent(drop);
    await el.updateComplete;

    expect(drop.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledWith('pinTab', [4, 1]);

    tiles = el.shadowRoot!.querySelectorAll('.tile');
    tiles[0]!.dispatchEvent(dragEvent('dragend', dataTransfer));
  });

  it('keeps the tab placeholder before a pinned tile through its left two thirds', async () => {
    const {el, send} = await loadGrid();
    el.sessionId = 7;
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    vi.spyOn(tiles[1] as HTMLElement, 'getBoundingClientRect').mockReturnValue({
      bottom: 60,
      height: 56,
      left: 60,
      right: 116,
      top: 4,
      width: 56,
      x: 60,
      y: 4,
      toJSON: () => {},
    });

    const dataTransfer = protectedTabDragDataTransfer('dao-tab-drag:7:4');
    const dragOver = dragEvent('dragover', dataTransfer);
    Object.defineProperties(dragOver, {
      clientX: {value: 96},
      clientY: {value: 30},
    });
    tiles[1]!.dispatchEvent(dragOver);
    await el.updateComplete;

    expect(dragOver.defaultPrevented).toBe(true);
    expect(renderedTileOrder(el)).toEqual(['A', 'placeholder', 'B', 'C']);

    const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
    const drop = dragEvent('drop', dataTransfer);
    grid.dispatchEvent(drop);
    await el.updateComplete;

    expect(drop.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledWith('pinTab', [4, 1]);
  });

  it('moves the tab placeholder after a pinned tile in its right third', async () => {
    const {el, send} = await loadGrid();
    el.sessionId = 7;
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    vi.spyOn(tiles[1] as HTMLElement, 'getBoundingClientRect').mockReturnValue({
      bottom: 60,
      height: 56,
      left: 60,
      right: 116,
      top: 4,
      width: 56,
      x: 60,
      y: 4,
      toJSON: () => {},
    });

    const dataTransfer = protectedTabDragDataTransfer('dao-tab-drag:7:4');
    const dragOver = dragEvent('dragover', dataTransfer);
    Object.defineProperties(dragOver, {
      clientX: {value: 104},
      clientY: {value: 30},
    });
    tiles[1]!.dispatchEvent(dragOver);
    await el.updateComplete;

    expect(dragOver.defaultPrevented).toBe(true);
    expect(renderedTileOrder(el)).toEqual(['A', 'B', 'placeholder', 'C']);

    const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
    const drop = dragEvent('drop', dataTransfer);
    grid.dispatchEvent(drop);
    await el.updateComplete;

    expect(drop.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledWith('pinTab', [4, 2]);
  });

  it('keeps the tab placeholder stable when dragging over the placeholder', async () => {
    const {el} = await loadGrid();
    el.sessionId = 7;
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dataTransfer = protectedTabDragDataTransfer('dao-tab-drag:7:4');
    tiles[1]!.dispatchEvent(dragEvent('dragover', dataTransfer));
    await el.updateComplete;

    expect(renderedTileOrder(el)).toEqual(['A', 'placeholder', 'B', 'C']);

    const placeholder =
        el.shadowRoot!.querySelector('.drag-placeholder') as HTMLElement;
    const placeholderDragOver = dragEvent('dragover', dataTransfer);
    placeholder.dispatchEvent(placeholderDragOver);
    await el.updateComplete;

    expect(placeholderDragOver.defaultPrevented).toBe(true);
    expect(renderedTileOrder(el)).toEqual(['A', 'placeholder', 'B', 'C']);
  });

  it('keeps the tab placeholder stable when dragging through grid gaps', async () => {
    const {el} = await loadGrid();
    el.sessionId = 7;
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dataTransfer = protectedTabDragDataTransfer('dao-tab-drag:7:4');
    tiles[1]!.dispatchEvent(dragEvent('dragover', dataTransfer));
    await el.updateComplete;

    expect(renderedTileOrder(el)).toEqual(['A', 'placeholder', 'B', 'C']);

    const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
    const gapDragOver = dragEvent('dragover', dataTransfer);
    grid.dispatchEvent(gapDragOver);
    await el.updateComplete;

    expect(gapDragOver.defaultPrevented).toBe(true);
    expect(renderedTileOrder(el)).toEqual(['A', 'placeholder', 'B', 'C']);
  });

  it('moves the tab placeholder to the end when dragging past the last tile', async () => {
    const {el} = await loadGrid();
    el.sessionId = 7;
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dataTransfer = protectedTabDragDataTransfer('dao-tab-drag:7:4');
    tiles[1]!.dispatchEvent(dragEvent('dragover', dataTransfer));
    await el.updateComplete;

    expect(renderedTileOrder(el)).toEqual(['A', 'placeholder', 'B', 'C']);

    const lastTile = el.shadowRoot!.querySelectorAll('.tile')[2] as HTMLElement;
    vi.spyOn(lastTile, 'getBoundingClientRect').mockReturnValue({
      bottom: 60,
      height: 56,
      left: 120,
      right: 176,
      top: 4,
      width: 56,
      x: 120,
      y: 4,
      toJSON: () => {},
    });

    const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
    const endDragOver = dragEvent('dragover', dataTransfer);
    Object.defineProperties(endDragOver, {
      clientX: {value: 190},
      clientY: {value: 30},
    });
    grid.dispatchEvent(endDragOver);
    await el.updateComplete;

    expect(endDragOver.defaultPrevented).toBe(true);
    expect(renderedTileOrder(el)).toEqual(['A', 'B', 'C', 'placeholder']);
  });

  it('ignores cross-window tab drops on the pinned grid', async () => {
    const {el, send} = await loadGrid();
    el.sessionId = 7;
    el.items = [item({id: 'pin-a', title: 'A'})];
    await el.updateComplete;

    const grid = el.shadowRoot!.querySelector('.grid') as HTMLElement;
    const dataTransfer = fakeDataTransfer('dao-tab-drag:8:3');
    const dragOver = dragEvent('dragover', dataTransfer);
    grid.dispatchEvent(dragOver);
    expect(dragOver.defaultPrevented).toBe(false);

    const drop = dragEvent('drop', dataTransfer);
    grid.dispatchEvent(drop);
    expect(drop.defaultPrevented).toBe(false);

    expect(send).not.toHaveBeenCalled();
  });

  it('ignores same-item, missing, and unknown pinned item drops', async () => {
    const {el, send} = await loadGrid();
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    const sameItemData = fakeDataTransfer();
    tiles[0]!.dispatchEvent(dragEvent('dragstart', sameItemData));
    send.mockClear();
    tiles[0]!.dispatchEvent(dragEvent('drop', sameItemData));

    tiles[1]!.dispatchEvent(dragEvent('drop', fakeDataTransfer()));
    tiles[1]!.dispatchEvent(dragEvent('drop', fakeDataTransfer('missing')));

    expect(send).not.toHaveBeenCalled();
  });

  it('ignores external drops that look like pinned item ids', async () => {
    const {el, send} = await loadGrid();
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
    ];
    await el.updateComplete;

    const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
    const externalTextDragOver =
        dragEvent('dragover', fakeDataTransfer('pin-a'));
    tile.dispatchEvent(externalTextDragOver);
    expect(externalTextDragOver.defaultPrevented).toBe(false);

    const externalTextDrop = dragEvent('drop', fakeDataTransfer('pin-a'));
    tile.dispatchEvent(externalTextDrop);
    expect(externalTextDrop.defaultPrevented).toBe(false);

    const externalFileData = fakeDataTransfer();
    externalFileData.types.push('Files');
    const externalFileDrop = dragEvent('drop', externalFileData);
    tile.dispatchEvent(externalFileDrop);
    expect(externalFileDrop.defaultPrevented).toBe(false);

    expect(send).not.toHaveBeenCalled();
  });

  it('allows dormant pinned items in sidebar state payloads', () => {
    const state: SidebarState = {
      pinnedItems: [{
        id: 'dormant',
        title: 'Docs',
        url: 'https://docs.example/',
        faviconUrl: '',
        isOpen: false,
        openTabIndex: -1,
        isActive: false,
      }],
      pinnedTabs: [],
      unpinnedTabs: [],
      activeIndex: -1,
      sessionId: 1,
    };

    expect(state.pinnedItems[0]!.isOpen).toBe(false);
  });
});
