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

import type {TabData} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

interface TestFavoritesView extends HTMLElement {
  tabs: TabData[];
  updateComplete: Promise<boolean>;
}

function tab(extra: Partial<TabData> = {}): TabData {
  return {
    tabId: 'tab-1',
    index: 3,
    title: 'Docs',
    url: 'https://docs.example/',
    faviconUrl: '',
    isActive: false,
    isPinned: true,
    isAudible: false,
    isMuted: false,
    ...extra,
  };
}

describe('dao-favorites-view', () => {
  beforeAll(async () => {
    await import('../dao_favorites_view.js');
  });

  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
  });

  it('shows the full tab tooltip after hovering a pinned tab', async () => {
    vi.useFakeTimers();
    try {
      const send = vi.fn();
      (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
      const el = document.createElement(
          'dao-favorites-view') as TestFavoritesView;
      const fullTitle = 'Pinned Docs With A Long Title';
      el.tabs = [tab({title: fullTitle})];
      document.body.appendChild(el);
      await el.updateComplete;

      const button = el.shadowRoot!.querySelector('.fav-btn') as HTMLElement;
      button.dispatchEvent(new MouseEvent('mouseenter', {
        screenX: 20,
        screenY: 30,
      }));
      vi.advanceTimersByTime(1499);
      expect(send).not.toHaveBeenCalled();

      vi.advanceTimersByTime(1);
      expect(send).toHaveBeenCalledWith(
          'showTabTooltip', [24, 34, fullTitle]);

      button.dispatchEvent(new MouseEvent('mouseleave'));
      expect(send).toHaveBeenLastCalledWith('hideTabTooltip', []);
    } finally {
      vi.useRealTimers();
    }
  });
});
