// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {PinnedItemData} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

vi.mock('../dao_media_control.js', () => {
  if (!customElements.get('dao-media-control')) {
    customElements.define('dao-media-control', class extends HTMLElement {});
  }
  return {};
});

function pinnedItem(extra: Partial<PinnedItemData> = {}): PinnedItemData {
  return {
    id: 'pin-1',
    title: 'Pinned',
    url: 'https://example.com/',
    faviconUrl: '',
    isOpen: true,
    openTabIndex: 0,
    isActive: false,
    ...extra,
  };
}

async function loadApp() {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  await import('../dao_sidebar_app.js');
  const el = document.createElement('dao-sidebar-app') as HTMLElement & {
    pinnedItems_: PinnedItemData[];
    updateComplete: Promise<boolean>;
  };
  document.body.appendChild(el);
  return {el, send};
}

describe('dao-sidebar-app', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
    delete (window as unknown as {cr?: unknown}).cr;
  });

  it('renders pinned tabs above the new tab button', async () => {
    const {el} = await loadApp();
    el.pinnedItems_ = [pinnedItem()];
    await el.updateComplete;

    const children = Array.from(
        el.shadowRoot!.querySelector('.sidebar-content')!.children);
    const pinnedIndex = children.findIndex(
        child => child.tagName.toLowerCase() === 'dao-pinned-tabs-grid');
    const newTabIndex = children.findIndex(
        child => child.tagName.toLowerCase() === 'dao-new-tab-button');

    expect(pinnedIndex).toBeGreaterThanOrEqual(0);
    expect(newTabIndex).toBeGreaterThanOrEqual(0);
    expect(pinnedIndex).toBeLessThan(newTabIndex);
  });
});
