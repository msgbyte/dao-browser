// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeAll, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  SIDEBAR_POINTER_EXITED_EVENT,
  TAB_DRAG_MIME_TYPE,
} from '../sidebar_bridge.js';
import type {TabData} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

interface TestTabItem extends HTMLElement {
  tabData: TabData;
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
    index: 3,
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

function fakeDataTransfer(): FakeDataTransfer {
  const data = new Map<string, string>();
  const fake: FakeDataTransfer = {
    dropEffect: '',
    effectAllowed: '',
    types: [],
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

function dragEvent(type: string, dataTransfer?: FakeDataTransfer): DragEvent {
  const event = new Event(type, {
    bubbles: true,
    cancelable: true,
    composed: true,
  }) as DragEvent;
  Object.defineProperty(event, 'dataTransfer', {
    value: dataTransfer ?? null,
  });
  return event;
}

describe('dao-tab-item', () => {
  beforeAll(async () => {
    await import('../dao_tab_item.js');
  });

  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
  });

  it('marks tab drags with a Dao tab MIME type', async () => {
    const send = vi.fn();
    (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
    const el = document.createElement('dao-tab-item') as TestTabItem;
    el.sessionId = 7;
    el.tabData = tab({index: 3});
    document.body.appendChild(el);
    await el.updateComplete;

    const dataTransfer = fakeDataTransfer();
    const event = dragEvent('dragstart', dataTransfer);
    (el as unknown as {onDragStart_: (e: DragEvent) => void})
        .onDragStart_(event);

    expect(dataTransfer.setData).toHaveBeenCalledWith(
        TAB_DRAG_MIME_TYPE, 'dao-tab-drag:7:3');
    expect(dataTransfer.setData).toHaveBeenCalledWith(
        'text/plain', 'dao-tab-drag:7:3');
    expect(dataTransfer.effectAllowed).toBe('move');
  });

  it('does not show a scheduled hover tooltip after drag starts', async () => {
    vi.useFakeTimers();
    try {
      const send = vi.fn();
      (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
      const el = document.createElement('dao-tab-item') as TestTabItem;
      el.sessionId = 7;
      el.tabData = tab({index: 3, title: 'Docs'});
      document.body.appendChild(el);
      await el.updateComplete;

      (el as unknown as {onShowTooltip_: (e: MouseEvent) => void})
          .onShowTooltip_(new MouseEvent('mouseenter', {
            screenX: 20,
            screenY: 30,
          }));
      vi.advanceTimersByTime(1000);

      const dataTransfer = fakeDataTransfer();
      (el as unknown as {onDragStart_: (e: DragEvent) => void})
          .onDragStart_(dragEvent('dragstart', dataTransfer));
      vi.advanceTimersByTime(600);

      expect(send).not.toHaveBeenCalledWith(
          'showTabTooltip', [24, 34, 'Docs']);
    } finally {
      vi.useRealTimers();
    }
  });

  it('clears tooltip and suppresses hover when the sidebar pointer exits',
      async () => {
        vi.useFakeTimers();
        try {
          const send = vi.fn();
          (globalThis as unknown as {chrome: {send: typeof send}}).chrome =
              {send};
          const el = document.createElement('dao-tab-item') as TestTabItem;
          el.sessionId = 7;
          el.tabData = tab({index: 3, title: 'Docs'});
          document.body.appendChild(el);
          await el.updateComplete;

          (el as unknown as {onShowTooltip_: (e: MouseEvent) => void})
              .onShowTooltip_(new MouseEvent('mouseenter', {
                screenX: 20,
                screenY: 30,
              }));
          vi.advanceTimersByTime(1000);

          window.dispatchEvent(new CustomEvent(SIDEBAR_POINTER_EXITED_EVENT));
          vi.advanceTimersByTime(600);
          await el.updateComplete;

          expect(send).toHaveBeenCalledWith('hideTabTooltip', []);
          expect(send).not.toHaveBeenCalledWith(
              'showTabTooltip', [24, 34, 'Docs']);
          expect(el.hasAttribute('hover-suppressed')).toBe(true);

          (el as unknown as {onShowTooltip_: (e: MouseEvent) => void})
              .onShowTooltip_(new MouseEvent('mouseenter', {
                screenX: 22,
                screenY: 32,
              }));
          await el.updateComplete;
          expect(el.hasAttribute('hover-suppressed')).toBe(false);
        } finally {
          vi.useRealTimers();
        }
      });
});
