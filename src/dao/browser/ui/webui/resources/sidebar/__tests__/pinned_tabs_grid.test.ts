// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  PINNED_ITEM_DRAG_MIME_TYPE,
  TAB_DRAG_MIME_TYPE,
} from '../sidebar_bridge.js';
import type {PinnedItemData, SidebarState} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

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

function dragEvent(type: string, dataTransfer?: FakeDataTransfer): DragEvent {
  const event = new Event(type, {
    bubbles: true,
    cancelable: true,
  }) as DragEvent;
  Object.defineProperty(event, 'dataTransfer', {
    value: dataTransfer ?? null,
  });
  return event;
}

describe('dao-pinned-tabs-grid', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
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

  it('uses responsive grid columns for pinned tiles', async () => {
    await loadGrid();
    const ctor = customElements.get('dao-pinned-tabs-grid') as
        typeof HTMLElement & {styles: {strings: string[]}};
    const cssText = ctor.styles.strings.join('');

    expect(cssText).toContain('grid-template-columns: repeat(auto-fit,');
    expect(cssText).toContain('minmax(');
    expect(cssText).not.toContain('grid-template-columns: repeat(3, 1fr)');
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

  it('shows the tab tooltip after hovering a pinned item', async () => {
    vi.useFakeTimers();
    try {
      const {el, send} = await loadGrid();
      el.items = [item({id: 'pin-tooltip', title: 'Pinned Docs'})];
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
          'showTabTooltip', [24, 34, 'Pinned Docs']);

      tile.dispatchEvent(new MouseEvent('mouseleave'));
      expect(send).toHaveBeenLastCalledWith('hideTabTooltip', []);
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
    el.items = [
      item({id: 'pin-a', title: 'A'}),
      item({id: 'pin-b', title: 'B'}),
      item({id: 'pin-c', title: 'C'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    const dataTransfer = fakeDataTransfer();

    tiles[0]!.dispatchEvent(dragEvent('dragstart', dataTransfer));
    expect(dataTransfer.setData).toHaveBeenCalledWith('text/plain', 'pin-a');
    expect(dataTransfer.setData).toHaveBeenCalledWith(
        PINNED_ITEM_DRAG_MIME_TYPE, 'pin-a');
    expect(dataTransfer.effectAllowed).toBe('move');

    const dragOver = dragEvent('dragover', dataTransfer);
    tiles[1]!.dispatchEvent(dragOver);
    expect(dragOver.defaultPrevented).toBe(true);
    expect(dataTransfer.dropEffect).toBe('move');

    const drop = dragEvent('drop', dataTransfer);
    tiles[1]!.dispatchEvent(drop);

    expect(drop.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledTimes(1);
    expect(send).toHaveBeenCalledWith('movePinnedItem', ['pin-a', 1]);
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
    expect(send).toHaveBeenCalledWith('pinTab', [3]);
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
