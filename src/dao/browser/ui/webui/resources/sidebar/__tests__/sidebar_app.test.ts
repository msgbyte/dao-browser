// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {readFileSync} from 'node:fs';

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  SIDEBAR_POINTER_EXITED_EVENT,
} from '../sidebar_bridge.js';
import type {PinnedItemData, UpdateStateData} from '../sidebar_bridge.js';

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
    updateState_: UpdateStateData | null;
    tabScrollbarVisible_: boolean;
    tabScrollbarHovered_: boolean;
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
    vi.useRealTimers();
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
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

  it('requests the initial sidebar and update states on connect', async () => {
    const {send} = await loadApp();

    expect(send).toHaveBeenCalledWith('getInitialState', []);
    expect(send).toHaveBeenCalledWith('requestUpdateState', []);
  });

  it('renders the update button before the plus menu at the toolbar end', async () => {
    const {el} = await loadApp();
    el.updateState_ = {
      state: 'ready',
      displayVersion: '1.2.3',
      label: 'Update',
      applyingLabel: 'Applying',
    };
    await el.updateComplete;

    const actions = el.shadowRoot!.querySelector('.toolbar-end-actions')!;
    const children = Array.from(actions.children);

    expect(children).toHaveLength(2);
    expect(children[0]!.tagName.toLowerCase()).toBe('dao-update-button');
    expect(children[1]!.classList.contains('plus-menu-container')).toBe(true);
  });

  it('replaces the native tab scrollbar with a thin overlay thumb', async () => {
    const {DaoSidebarApp} = await import('../dao_sidebar_app.js');
    const styles = (DaoSidebarApp as unknown as {
      styles: {strings: TemplateStringsArray};
    }).styles;
    const cssText = Array.from(styles.strings).join('');

    expect(cssText).toMatch(
        /\.tab-section-shell\s*{[^}]*position:\s*relative;/s);
    expect(cssText).toMatch(
        /\.tab-section\s*{[^}]*scrollbar-width:\s*none;/s);
    expect(cssText).toMatch(
        /\.tab-section::-webkit-scrollbar\s*{[^}]*display:\s*none;/s);
    expect(cssText).toMatch(
        /\.tab-scrollbar\s*{[^}]*right:\s*0;[^}]*width:\s*4px;/s);
    expect(cssText).toMatch(
        /\.tab-scrollbar-thumb\s*{[^}]*width:\s*4px;/s);
    expect(cssText).toMatch(
        /\.tab-scrollbar\.visible\.hovered\s*{[^}]*opacity:\s*1;/s);

    const {el} = await loadApp();
    await el.updateComplete;

    expect(el.shadowRoot!.querySelector('.tab-section-shell')).not.toBeNull();
    expect(el.shadowRoot!.querySelector('.tab-scrollbar')).not.toBeNull();
    expect(el.shadowRoot!.querySelector('.tab-scrollbar-thumb')).not.toBeNull();
  });

  it('hides the overlay scrollbar when the pointer leaves the sidebar window', async () => {
    const {el} = await loadApp();
    el.tabScrollbarVisible_ = true;
    el.tabScrollbarHovered_ = true;
    await el.updateComplete;

    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).not.toBeNull();

    window.dispatchEvent(new MouseEvent('mouseout', {
      bubbles: true,
      relatedTarget: null,
    }));
    await el.updateComplete;

    expect(el.tabScrollbarHovered_).toBe(false);
    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).toBeNull();
  });

  it('clears stale overlay scrollbar hover without a leave event', async () => {
    vi.useFakeTimers();

    const {el} = await loadApp();
    await el.updateComplete;

    const shell = el.shadowRoot!.querySelector('.tab-section-shell')!;
    shell.dispatchEvent(new Event('pointerenter'));
    await el.updateComplete;

    expect(el.tabScrollbarHovered_).toBe(true);
    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).not.toBeNull();

    vi.advanceTimersByTime(900);
    await el.updateComplete;

    expect(el.tabScrollbarHovered_).toBe(false);
    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).toBeNull();
  });

  it('hides the overlay scrollbar when the host sidebar exits', async () => {
    const {el} = await loadApp();
    el.tabScrollbarVisible_ = true;
    el.tabScrollbarHovered_ = true;
    await el.updateComplete;

    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).not.toBeNull();

    (window as unknown as {
      cr: {webUIListenerCallback: (event: string) => void};
    }).cr.webUIListenerCallback('sidebarPointerExited');
    await el.updateComplete;

    expect(el.tabScrollbarHovered_).toBe(false);
    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).toBeNull();
  });

  it('broadcasts a DOM pointer-exit event when the host sidebar exits',
      async () => {
        await loadApp();
        const listener = vi.fn();
        window.addEventListener(SIDEBAR_POINTER_EXITED_EVENT, listener);

        (window as unknown as {
          cr: {webUIListenerCallback: (event: string) => void};
        }).cr.webUIListenerCallback('sidebarPointerExited');

        expect(listener).toHaveBeenCalledTimes(1);
        window.removeEventListener(SIDEBAR_POINTER_EXITED_EVENT, listener);
      });

  it('hides page-level sidebar scrollbars until the page is hovered', () => {
    const cssText = readFileSync(
        'src/dao/browser/ui/webui/resources/sidebar/sidebar.css', 'utf8');

    expect(cssText).toMatch(/scrollbar-width:\s*none;/);
    expect(cssText).toMatch(/::-webkit-scrollbar\s*{[^}]*display:\s*none;/s);
  });
});
