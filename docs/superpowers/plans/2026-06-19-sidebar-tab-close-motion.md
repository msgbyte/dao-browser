# Sidebar Tab Close Motion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a lightweight FLIP motion so surviving sidebar tabs glide into place whenever a tab disappears from the current sidebar surface.

**Architecture:** Add one WebUI-only FLIP helper and wire it into the three containers that own tab layout: `dao-tab-list`, `dao-folder-item`, and `dao-pinned-tabs-grid`. Each container snapshots visible item bounds before a Lit update, renders normally, then animates surviving DOM nodes from their old positions to their final positions when an identity was removed.

**Tech Stack:** Lit WebUI TypeScript, Web Animations API, Vitest, jsdom test environment.

---

## Project Constraints

- Do not edit `engine/`.
- Do not run `autoninja`, `ninja`, `siso`, direct Chromium build tools, `gn gen`, `npm run build`, `npm run build:debug`, or `npm run test:build`.
- For this WebUI-only change, use `npm run test:webui -- <test-file>` for focused tests, then `npm run test:webui` and `npm run lint:lit`.
- Do not run state-changing git commands unless the user explicitly authorizes the exact action. This plan therefore has no automatic `git add` or `git commit` steps.

## File Structure

- Create `src/dao/browser/ui/webui/resources/sidebar/dao_flip_motion.ts`
  - Shared FLIP snapshot and animation helper.
  - WebUI-only, no native C++ dependencies.
  - Owns reduced-motion checks and WAAPI animation setup.
- Create `src/dao/browser/ui/webui/resources/sidebar/__tests__/dao_flip_motion.test.ts`
  - Unit tests for the helper independent of Lit.
- Modify `src/dao/browser/ui/webui/resources/sidebar/dao_tab_list.ts`
  - Add DOM identities to rendered `dao-tab-item` elements.
  - Capture and run close FLIP for normal unpinned tabs.
- Modify `src/dao/browser/ui/webui/resources/sidebar/__tests__/tab_list.test.ts`
  - Add tests for close FLIP, non-removal updates, and drag suppression.
- Modify `src/dao/browser/ui/webui/resources/sidebar/dao_folder_item.ts`
  - Add DOM identities to child `dao-tab-item` elements.
  - Capture and run close FLIP inside expanded folders only.
- Create `src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_item.test.ts`
  - Add focused tests for folder child close FLIP and collapsed-folder suppression.
- Modify `src/dao/browser/ui/webui/resources/sidebar/dao_pinned_tabs_grid.ts`
  - Add DOM identities to pinned tiles.
  - Capture and run close FLIP for pinned grid removal.
- Modify `src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts`
  - Add tests for pinned tile FLIP and drag suppression.

---

### Task 1: Add FLIP Helper Tests

**Files:**
- Create: `src/dao/browser/ui/webui/resources/sidebar/__tests__/dao_flip_motion.test.ts`

- [ ] **Step 1: Create failing helper tests**

Create `src/dao/browser/ui/webui/resources/sidebar/__tests__/dao_flip_motion.test.ts` with:

```ts
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, describe, expect, it, vi} from 'vitest';

import {
  animateSurvivingFlipElements,
  snapshotFlipElements,
  type FlipMotionSnapshot,
} from '../dao_flip_motion.js';

function rect(left: number, top: number, width = 100, height = 36): DOMRect {
  return {
    left,
    top,
    width,
    height,
    right: left + width,
    bottom: top + height,
    x: left,
    y: top,
    toJSON: () => ({}),
  } as DOMRect;
}

function setRect(element: HTMLElement, bounds: DOMRect) {
  element.getBoundingClientRect = () => bounds;
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

describe('dao_flip_motion', () => {
  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
  });

  it('captures visible elements by stable identity', () => {
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="b"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));

    const snapshot = snapshotFlipElements(
        root, '.item', element => element.dataset.id || '');

    expect([...snapshot.ids]).toEqual(['a', 'b']);
    expect(snapshot.rects.get('b')!.top).toBe(40);
  });

  it('animates surviving elements when an identity was removed', () => {
    const animate = installAnimateMock();
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="c"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['a', 'b', 'c']),
      rects: new Map([
        ['a', rect(0, 0)],
        ['b', rect(0, 40)],
        ['c', rect(0, 80)],
      ]),
    };

    const animations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');

    expect(animations).toHaveLength(1);
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

  it('does not animate when no identity was removed', () => {
    const animate = installAnimateMock();
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
        <div class="item" data-id="b"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const items = root.querySelectorAll('.item') as NodeListOf<HTMLElement>;
    setRect(items[0]!, rect(0, 0));
    setRect(items[1]!, rect(0, 40));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['a', 'b']),
      rects: new Map([
        ['a', rect(0, 0)],
        ['b', rect(0, 80)],
      ]),
    };

    const animations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');

    expect(animations).toHaveLength(0);
    expect(animate).not.toHaveBeenCalled();
  });

  it('skips animation when reduced motion is requested', () => {
    const animate = installAnimateMock();
    vi.spyOn(window, 'matchMedia').mockReturnValue({
      matches: true,
      media: '(prefers-reduced-motion: reduce)',
      onchange: null,
      addListener: vi.fn(),
      removeListener: vi.fn(),
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
      dispatchEvent: vi.fn(),
    } as unknown as MediaQueryList);
    document.body.innerHTML = `
      <div id="root">
        <div class="item" data-id="a"></div>
      </div>
    `;
    const root = document.querySelector('#root')!;
    const item = root.querySelector('.item') as HTMLElement;
    setRect(item, rect(0, 0));
    const previous: FlipMotionSnapshot = {
      ids: new Set(['gone', 'a']),
      rects: new Map([
        ['gone', rect(0, 0)],
        ['a', rect(0, 40)],
      ]),
    };

    const animations = animateSurvivingFlipElements(
        previous, root, '.item', element => element.dataset.id || '');

    expect(animations).toHaveLength(0);
    expect(animate).not.toHaveBeenCalled();
  });
});
```

- [ ] **Step 2: Run the helper test and verify it fails because the helper does not exist**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/dao_flip_motion.test.ts
```

Expected: FAIL with an import error for `../dao_flip_motion.js`.

---

### Task 2: Implement FLIP Helper

**Files:**
- Create: `src/dao/browser/ui/webui/resources/sidebar/dao_flip_motion.ts`
- Test: `src/dao/browser/ui/webui/resources/sidebar/__tests__/dao_flip_motion.test.ts`

- [ ] **Step 1: Add the helper implementation**

Create `src/dao/browser/ui/webui/resources/sidebar/dao_flip_motion.ts`:

```ts
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export interface FlipMotionSnapshot {
  ids: Set<string>;
  rects: Map<string, DOMRectReadOnly>;
}

export interface FlipMotionOptions {
  duration?: number;
  easing?: string;
  skip?: boolean;
}

const DEFAULT_DURATION_MS = 140;
const DEFAULT_EASING = 'cubic-bezier(0.2, 0, 0, 1)';
const MINIMUM_DELTA_PX = 0.5;

export function snapshotFlipElements<T extends HTMLElement>(
    root: ParentNode | null,
    selector: string,
    getIdentity: (element: T) => string): FlipMotionSnapshot {
  const ids = new Set<string>();
  const rects = new Map<string, DOMRectReadOnly>();
  if (!root) {
    return {ids, rects};
  }

  const elements = root.querySelectorAll(selector) as NodeListOf<T>;
  for (const element of elements) {
    const id = getIdentity(element);
    if (!id || ids.has(id)) {
      continue;
    }
    const bounds = element.getBoundingClientRect();
    ids.add(id);
    rects.set(id, bounds);
  }
  return {ids, rects};
}

export function animateSurvivingFlipElements<T extends HTMLElement>(
    previous: FlipMotionSnapshot | null,
    root: ParentNode | null,
    selector: string,
    getIdentity: (element: T) => string,
    options: FlipMotionOptions = {}): Animation[] {
  if (!previous || options.skip || prefersReducedMotion()) {
    return [];
  }
  if (!root) {
    return [];
  }

  const current = snapshotFlipElements(root, selector, getIdentity);
  const removedIdentity = [...previous.ids].some(id => !current.ids.has(id));
  if (!removedIdentity) {
    return [];
  }

  const animations: Animation[] = [];
  const duration = options.duration ?? DEFAULT_DURATION_MS;
  const easing = options.easing ?? DEFAULT_EASING;
  const elements = root.querySelectorAll(selector) as NodeListOf<T>;

  for (const element of elements) {
    const id = getIdentity(element);
    const oldBounds = previous.rects.get(id);
    const newBounds = current.rects.get(id);
    if (!oldBounds || !newBounds) {
      continue;
    }

    const deltaX = oldBounds.left - newBounds.left;
    const deltaY = oldBounds.top - newBounds.top;
    if (Math.abs(deltaX) < MINIMUM_DELTA_PX &&
        Math.abs(deltaY) < MINIMUM_DELTA_PX) {
      continue;
    }

    element.getAnimations?.().forEach(animation => animation.cancel());
    const animation = element.animate(
        [
          {transform: `translate(${deltaX}px, ${deltaY}px)`},
          {transform: 'translate(0, 0)'},
        ],
        {duration, easing});
    animations.push(animation);
  }

  return animations;
}

function prefersReducedMotion(): boolean {
  return window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ??
      false;
}
```

- [ ] **Step 2: Run the helper test and verify it passes**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/dao_flip_motion.test.ts
```

Expected: PASS.

---

### Task 3: Wire Normal Tab List Motion

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/dao_tab_list.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/tab_list.test.ts`

- [ ] **Step 1: Add failing `dao-tab-list` motion tests**

In `src/dao/browser/ui/webui/resources/sidebar/__tests__/tab_list.test.ts`, extend `TestTabList`:

```ts
interface TestTabList extends HTMLElement {
  tabs: TabData[];
  sessionId: number;
  updateComplete: Promise<boolean>;
  willUpdate: (changedProperties: Map<PropertyKey, unknown>) => void;
  updated: (changedProperties: Map<PropertyKey, unknown>) => void;
}
```

Add these helpers near the existing test helpers:

```ts
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
```

Add these tests inside `describe('dao-tab-list', () => { ... })`:

```ts
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

    const tabItems = el.shadowRoot!.querySelectorAll('dao-tab-item');
    setTabItemBounds(tabItems[0] as HTMLElement, 0, 36);
    setTabItemBounds(tabItems[1] as HTMLElement, 38, 36);

    el.willUpdate(new Map<PropertyKey, unknown>([['tabs', el.tabs]]));
    el.tabs = [
      tab({tabId: 'tab-a', index: 1, title: 'A', isActive: false}),
      tab({tabId: 'tab-b', index: 2, title: 'B', isActive: true}),
    ];
    await el.updateComplete;
    el.updated(new Map<PropertyKey, unknown>([['tabs', el.tabs]]));

    expect(animate).not.toHaveBeenCalled();
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
```

- [ ] **Step 2: Run the `dao-tab-list` test and verify the new tests fail**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/tab_list.test.ts
```

Expected: FAIL because `willUpdate` and close FLIP are not implemented in `dao-tab-list`.

- [ ] **Step 3: Implement normal tab list motion**

In `src/dao/browser/ui/webui/resources/sidebar/dao_tab_list.ts`, add imports:

```ts
import {
  animateSurvivingFlipElements,
  snapshotFlipElements,
  type FlipMotionSnapshot,
} from './dao_flip_motion.js';
```

Add a private field:

```ts
  private previousFlipSnapshot_: FlipMotionSnapshot | null = null;
```

Add stable DOM identity where `dao-tab-item` is rendered in all three locations in this file:

```ts
            data-tab-id=${this.getTabIdentity_(tab)}
```

Add lifecycle methods and helpers:

```ts
  override willUpdate(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('tabs') ||
        changedProperties.has('folderModelVersion')) {
      this.previousFlipSnapshot_ = this.snapshotTabItems_();
    }
  }

  override updated(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('tabs') ||
        changedProperties.has('folderModelVersion')) {
      this.animateCloseMotion_();
    }
  }

  private snapshotTabItems_(): FlipMotionSnapshot {
    return snapshotFlipElements(
        this.shadowRoot,
        'dao-tab-item',
        (element: HTMLElement) => element.dataset.tabId || '');
  }

  private animateCloseMotion_() {
    animateSurvivingFlipElements(
        this.previousFlipSnapshot_,
        this.shadowRoot,
        'dao-tab-item',
        (element: HTMLElement) => element.dataset.tabId || '',
        {skip: this.tabDragActivated_});
    this.previousFlipSnapshot_ = null;
  }

  private getTabIdentity_(tab: TabData): string {
    return tab.tabId || `${tab.index}:${tab.url}:${tab.title}`;
  }
```

- [ ] **Step 4: Run the `dao-tab-list` test and verify it passes**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/tab_list.test.ts
```

Expected: PASS.

---

### Task 4: Wire Folder Child Motion

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/dao_folder_item.ts`
- Create: `src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_item.test.ts`

- [ ] **Step 1: Add failing folder item tests**

Create `src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_item.test.ts`:

```ts
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeAll, beforeEach, describe, expect, it, vi} from 'vitest';

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
    name: 'Folder',
    collapsed: false,
    children: [],
    ...extra,
  };
}

function setBounds(element: HTMLElement, top: number, height: number) {
  Object.defineProperty(element, 'offsetTop', {configurable: true, value: top});
  Object.defineProperty(element, 'offsetHeight', {
    configurable: true,
    value: height,
  });
  element.getBoundingClientRect = () => ({
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

describe('dao-folder-item', () => {
  beforeAll(async () => {
    await import('../dao_folder_item.js');
  });

  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
  });

  function createFolderItem() {
    const el = document.createElement('dao-folder-item') as TestFolderItem;
    el.folder = folder();
    el.sessionId = 7;
    el.matchedTabs = [
      tab({tabId: 'tab-a', index: 1, title: 'A'}),
      tab({tabId: 'tab-b', index: 2, title: 'B'}),
    ];
    document.body.appendChild(el);
    return el;
  }

  it('animates surviving folder children when a child tab is removed',
      async () => {
        const animate = installAnimateMock();
        const el = createFolderItem();
        await el.updateComplete;

        let items = el.shadowRoot!.querySelectorAll('dao-tab-item');
        setBounds(items[0] as HTMLElement, 0, 36);
        setBounds(items[1] as HTMLElement, 38, 36);

        el.willUpdate(
            new Map<PropertyKey, unknown>([['matchedTabs', el.matchedTabs]]));
        el.matchedTabs = [tab({tabId: 'tab-b', index: 1, title: 'B'})];
        await el.updateComplete;

        items = el.shadowRoot!.querySelectorAll('dao-tab-item');
        setBounds(items[0] as HTMLElement, 0, 36);
        el.updated(
            new Map<PropertyKey, unknown>([['matchedTabs', el.matchedTabs]]));

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

        const items = el.shadowRoot!.querySelectorAll('dao-tab-item');
        setBounds(items[0] as HTMLElement, 0, 36);
        setBounds(items[1] as HTMLElement, 38, 36);

        el.willUpdate(
            new Map<PropertyKey, unknown>([['matchedTabs', el.matchedTabs]]));
        el.folder = folder({collapsed: true});
        el.matchedTabs = [tab({tabId: 'tab-b', index: 1, title: 'B'})];
        await el.updateComplete;
        el.updated(
            new Map<PropertyKey, unknown>([['matchedTabs', el.matchedTabs]]));

        expect(animate).not.toHaveBeenCalled();
      });
});
```

- [ ] **Step 2: Run the folder item test and verify it fails**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_item.test.ts
```

Expected: FAIL because folder FLIP is not implemented.

- [ ] **Step 3: Implement folder child motion**

In `src/dao/browser/ui/webui/resources/sidebar/dao_folder_item.ts`, add imports:

```ts
import {
  animateSurvivingFlipElements,
  snapshotFlipElements,
  type FlipMotionSnapshot,
} from './dao_flip_motion.js';
```

Add a private field:

```ts
  private previousFlipSnapshot_: FlipMotionSnapshot | null = null;
```

Add `data-tab-id` to child `dao-tab-item` render:

```ts
              data-tab-id=${this.getTabIdentity_(tab)}
```

Add lifecycle methods and preserve the existing rename focus behavior:

```ts
  override willUpdate(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('matchedTabs') ||
        changedProperties.has('folder')) {
      this.previousFlipSnapshot_ = this.snapshotChildTabs_();
    }
  }

  override updated(changedProperties: Map<PropertyKey, unknown>) {
    // Auto-focus rename input when entering rename mode.
    if (this.isRenaming_) {
      const input = this.shadowRoot!.querySelector(
          '.rename-input') as HTMLInputElement | null;
      if (input) {
        input.focus();
        input.select();
      }
    }

    if (changedProperties.has('matchedTabs') ||
        changedProperties.has('folder')) {
      this.animateChildCloseMotion_();
    }
  }

  private snapshotChildTabs_(): FlipMotionSnapshot {
    return snapshotFlipElements(
        this.shadowRoot,
        '.children-inner dao-tab-item',
        (element: HTMLElement) => element.dataset.tabId || '');
  }

  private animateChildCloseMotion_() {
    animateSurvivingFlipElements(
        this.previousFlipSnapshot_,
        this.shadowRoot,
        '.children-inner dao-tab-item',
        (element: HTMLElement) => element.dataset.tabId || '',
        {skip: this.folder.collapsed || this.childDropIndex_ >= 0});
    this.previousFlipSnapshot_ = null;
  }

  private getTabIdentity_(tab: TabData): string {
    return tab.tabId || `${tab.index}:${tab.url}:${tab.title}`;
  }
```

- [ ] **Step 4: Run the folder item test and verify it passes**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_item.test.ts
```

Expected: PASS.

---

### Task 5: Wire Pinned Grid Motion

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/dao_pinned_tabs_grid.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts`

- [ ] **Step 1: Add failing pinned grid motion tests**

In `src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts`, extend the element type in `loadGrid()`:

```ts
  const el = document.createElement('dao-pinned-tabs-grid') as HTMLElement & {
    items: PinnedItemData[];
    sessionId: number;
    updateComplete: Promise<boolean>;
    willUpdate: (changedProperties: Map<PropertyKey, unknown>) => void;
    updated: (changedProperties: Map<PropertyKey, unknown>) => void;
  };
```

Add helpers near the existing helpers:

```ts
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
```

Add these tests inside `describe('dao-pinned-tabs-grid', () => { ... })`:

```ts
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

        el.willUpdate(new Map<PropertyKey, unknown>([['items', el.items]]));
        el.items = [
          item({id: 'pin-a', title: 'A'}),
          item({id: 'pin-c', title: 'C'}),
        ];
        await el.updateComplete;

        tiles = el.shadowRoot!.querySelectorAll('.tile');
        setTileBounds(tiles[0] as HTMLElement, 0, 0);
        setTileBounds(tiles[1] as HTMLElement, 62, 0);
        el.updated(new Map<PropertyKey, unknown>([['items', el.items]]));

        expect(animate).toHaveBeenCalledWith(
            [
              {transform: 'translate(62px, 0px)'},
              {transform: 'translate(0, 0)'},
            ],
            {
              duration: 140,
              easing: 'cubic-bezier(0.2, 0, 0, 1)',
            });
      });

  it('skips pinned grid close motion while a tab drag preview is visible',
      async () => {
        const animate = installAnimateMock();
        const {el} = await loadGrid();
        el.items = [
          item({id: 'pin-a', title: 'A'}),
          item({id: 'pin-b', title: 'B'}),
        ];
        await el.updateComplete;

        const tiles = el.shadowRoot!.querySelectorAll('.tile');
        setTileBounds(tiles[0] as HTMLElement, 0, 0);
        setTileBounds(tiles[1] as HTMLElement, 62, 0);

        (el as unknown as {tabDragPlaceholderVisible_: boolean})
            .tabDragPlaceholderVisible_ = true;
        el.willUpdate(new Map<PropertyKey, unknown>([['items', el.items]]));
        el.items = [item({id: 'pin-b', title: 'B'})];
        await el.updateComplete;
        el.updated(new Map<PropertyKey, unknown>([['items', el.items]]));

        expect(animate).not.toHaveBeenCalled();
      });
```

- [ ] **Step 2: Run pinned grid tests and verify the new tests fail**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts
```

Expected: FAIL because pinned grid FLIP is not implemented.

- [ ] **Step 3: Implement pinned grid motion**

In `src/dao/browser/ui/webui/resources/sidebar/dao_pinned_tabs_grid.ts`, add imports:

```ts
import {
  animateSurvivingFlipElements,
  snapshotFlipElements,
  type FlipMotionSnapshot,
} from './dao_flip_motion.js';
```

Add a private field:

```ts
  private previousFlipSnapshot_: FlipMotionSnapshot | null = null;
```

Add stable identity to the pinned tile button:

```ts
              data-pinned-item-id=${item.id}
```

Add lifecycle methods and helpers:

```ts
  override willUpdate(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('items')) {
      this.previousFlipSnapshot_ = this.snapshotTiles_();
    }
  }

  override updated(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('items')) {
      this.animateCloseMotion_();
    }
  }

  private snapshotTiles_(): FlipMotionSnapshot {
    return snapshotFlipElements(
        this.shadowRoot,
        '.tile',
        (element: HTMLElement) => element.dataset.pinnedItemId || '');
  }

  private animateCloseMotion_() {
    animateSurvivingFlipElements(
        this.previousFlipSnapshot_,
        this.shadowRoot,
        '.tile',
        (element: HTMLElement) => element.dataset.pinnedItemId || '',
        {
          skip: this.dragPlaceholderIndex_ >= 0 ||
              this.tabDragPlaceholderVisible_,
        });
    this.previousFlipSnapshot_ = null;
  }
```

- [ ] **Step 4: Run pinned grid tests and verify they pass**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts
```

Expected: PASS.

---

### Task 6: Focused Regression Sweep

**Files:**
- Verify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/dao_flip_motion.test.ts`
- Verify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/tab_list.test.ts`
- Verify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_item.test.ts`
- Verify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts`

- [ ] **Step 1: Run focused WebUI tests for the changed surface**

Run:

```bash
npm run test:webui -- \
  src/dao/browser/ui/webui/resources/sidebar/__tests__/dao_flip_motion.test.ts \
  src/dao/browser/ui/webui/resources/sidebar/__tests__/tab_list.test.ts \
  src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_item.test.ts \
  src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts
```

Expected: PASS.

- [ ] **Step 2: Run full WebUI tests**

Run:

```bash
npm run test:webui
```

Expected: PASS.

- [ ] **Step 3: Run Lit reactive field lint**

Run:

```bash
npm run lint:lit
```

Expected: PASS.

- [ ] **Step 4: Manual QA checklist**

Use a local Dao debug build that already exists. Do not compile with any
forbidden build command. Exercise these flows and confirm surviving sidebar
items glide into place with the quiet 120-150 ms feel:

- Close an unpinned tab with the row close button.
- Close an unpinned tab with Cmd+W.
- Close an unpinned tab from the context menu.
- Close a tab inside an expanded folder.
- Remove or close a pinned item from the pinned grid.
- Drag a tab out of the current window.
- Close two tabs quickly and confirm the surviving tabs animate to the final
  state without a delayed queue.
- Enable reduced motion at the system/browser level if practical and confirm
  the close motion is skipped.

---

## Execution Notes

- Keep the implementation WebUI-only. No C++ or patch files are required for
  this feature.
- Keep `dao-tab-item` presentation-focused. Stable DOM identity belongs in the
  rendered attributes; list-level movement belongs in parent containers.
- If a test needs to call `willUpdate()` or `updated()` directly, follow the
  existing `tab_item.test.ts` pattern of casting the element to expose the
  lifecycle method.
- Do not stage or commit until the user explicitly authorizes the exact git
  operation. If authorized later, use a Conventional Commit message such as
  `feat(sidebar): smooth tab close motion`.
