// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

const bridge = vi.hoisted(() => ({
  detectImportSources: vi.fn(),
  getImportItemCount: vi.fn(),
  getBrowserMigrationState: vi.fn(),
  startBrowserMigration: vi.fn(),
  cancelBrowserMigration: vi.fn(),
  retryBrowserMigrationCategories: vi.fn(),
  addListener: vi.fn(() => ({eventName: 'event', uid: 1})),
  removeListener: vi.fn(),
}));

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('../../sidebar/__tests__/lit_test_shim.js');
});

vi.mock('//resources/js/load_time_data.js', () => ({
  loadTimeData: {
    getString: (key: string) => key,
    getStringF: (key: string, value: string) => `${key}:${value}`,
  },
}));

vi.mock('../import_bridge.js', () => bridge);

type ImportApp = HTMLElement&{
  sources_: Array<{
    id: string;
    kind: string;
    browserName: string;
    profileName: string;
    supportedCategories: string[];
  }>;
  selectedSourceId_: string;
  selectedCategories_: string[];
  categoryCounts_: Record<string, number|null|undefined>;
  step_: number;
  loading_: boolean;
  jobState_: unknown;
  updateComplete: Promise<boolean>;
};

async function createApp(): Promise<ImportApp> {
  await import('../dao_import_app.js');
  const appClass = customElements.get('dao-import-app') as typeof HTMLElement&{
    invokeLifecycleCallbacksForTesting?: boolean;
  };
  appClass.invokeLifecycleCallbacksForTesting = true;
  const element = document.createElement('dao-import-app') as ImportApp;
  document.body.appendChild(element);
  await vi.waitFor(() => expect(element.loading_).toBe(false));
  await element.updateComplete;
  return element;
}

describe('dao-import-app', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    bridge.detectImportSources.mockResolvedValue([]);
    bridge.getImportItemCount.mockResolvedValue(null);
    bridge.getBrowserMigrationState.mockResolvedValue(null);
  });

  afterEach(() => vi.clearAllMocks());

  it('renders an actionable empty state when no profiles are detected',
     async () => {
       const app = await createApp();
       await app.updateComplete;

       expect(app.shadowRoot!.querySelector('[data-test="empty"]'))
           .not.toBeNull();
       expect(app.shadowRoot!.querySelector('[data-test="rescan"]'))
           .not.toBeNull();
       const cards = [...app.shadowRoot!.querySelectorAll<HTMLButtonElement>(
           '[data-source-kind]')];
       expect(cards.map(card => card.dataset.sourceKind)).toEqual(
           ['chrome', 'arc', 'edge', 'safari', 'firefox']);
       expect(cards.every(card => card.disabled)).toBe(true);
       expect(app.shadowRoot!.querySelector<HTMLButtonElement>(
                  '[data-test="continue"]')!.disabled)
           .toBe(true);
     });

  it('disables only browser kinds without detected profiles', async () => {
    const app = await createApp();
    app.sources_ = [
      {
        id: 'chrome-personal',
        kind: 'chrome',
        browserName: 'Google Chrome',
        profileName: 'Personal',
        supportedCategories: ['bookmarks'],
      },
      {
        id: 'chrome-work',
        kind: 'chrome',
        browserName: 'Google Chrome',
        profileName: 'Work',
        supportedCategories: ['bookmarks'],
      },
      {
        id: 'firefox-default',
        kind: 'firefox',
        browserName: 'Firefox',
        profileName: 'default-release',
        supportedCategories: ['bookmarks'],
      },
    ];
    await app.updateComplete;

    const cards = [...app.shadowRoot!.querySelectorAll<HTMLButtonElement>(
        '[data-source-kind]')];
    expect(cards.map(card => [card.dataset.sourceKind, card.disabled])).toEqual([
      ['chrome', false],
      ['chrome', false],
      ['arc', true],
      ['edge', true],
      ['safari', true],
      ['firefox', false],
    ]);

    cards[1]!.click();
    await app.updateComplete;
    expect(app.selectedSourceId_).toBe('chrome-work');
  });

  it('disables Continue when the selected profile disappears', async () => {
    const app = await createApp();
    app.sources_ = [{
      id: 'chrome-default',
      kind: 'chrome',
      browserName: 'Google Chrome',
      profileName: 'Default',
      supportedCategories: ['bookmarks'],
    }];
    await app.updateComplete;

    (app.shadowRoot!.querySelector(
         '[data-source-id="chrome-default"]') as HTMLButtonElement)
        .click();
    app.sources_ = [];
    await app.updateComplete;

    expect(app.shadowRoot!.querySelector<HTMLButtonElement>(
               '[data-test="continue"]')!.disabled)
        .toBe(true);
  });

  it('selects one profile and advances to supported categories', async () => {
    const app = await createApp();
    app.sources_ = [{
      id: 'chrome-default',
      kind: 'chrome',
      browserName: 'Google Chrome',
      profileName: 'Personal',
      supportedCategories: ['bookmarks', 'history', 'passwords'],
    }];
    await app.updateComplete;

    (app.shadowRoot!.querySelector('[data-source-id="chrome-default"]') as
     HTMLButtonElement).click();
    await app.updateComplete;
    expect(app.selectedSourceId_).toBe('chrome-default');
    (app.shadowRoot!.querySelector('[data-test="continue"]') as
     HTMLButtonElement).click();
    await app.updateComplete;

    expect(app.step_).toBe(2);
    expect(app.selectedCategories_).toEqual([
      'bookmarks', 'history', 'passwords',
    ]);
  });

  it('shows the Keychain expectation tip when passwords are selected',
     async () => {
       const app = await createApp();
       app.sources_ = [{
         id: 'arc-default',
         kind: 'arc',
         browserName: 'Arc',
         profileName: 'Default',
         supportedCategories: ['bookmarks', 'passwords'],
       }];
       app.selectedSourceId_ = 'arc-default';
       app.selectedCategories_ = ['bookmarks', 'passwords'];
       app.step_ = 2;
       await app.updateComplete;

       expect(app.shadowRoot!.querySelector('[data-test="password-tip"]'))
           .not.toBeNull();
     });

  it('shows asynchronously scanned candidate counts by category', async () => {
    bridge.getImportItemCount.mockImplementation(
        (_sourceId: string, category: string) =>
            Promise.resolve(category === 'bookmarks' ? 1234 : null));
    const app = await createApp();
    app.sources_ = [{
      id: 'chrome-default',
      kind: 'chrome',
      browserName: 'Google Chrome',
      profileName: 'Personal',
      supportedCategories: ['bookmarks', 'history'],
    }];
    await app.updateComplete;

    (app.shadowRoot!.querySelector('[data-source-id="chrome-default"]') as
     HTMLButtonElement).click();
    await app.updateComplete;
    (app.shadowRoot!.querySelector('[data-test="continue"]') as
     HTMLButtonElement).click();
    await vi.waitFor(() => {
      expect(app.shadowRoot!.querySelector(
          '[data-category="bookmarks"] [data-test="candidate-count"]')
                 ?.textContent)
          .toContain('1,234');
    });

    expect(bridge.getImportItemCount).toHaveBeenCalledWith(
        'chrome-default', 'bookmarks');
    expect(bridge.getImportItemCount).toHaveBeenCalledWith(
        'chrome-default', 'history');
  });

  it('ignores a late candidate count after switching profiles', async () => {
    let resolveFirst!: (count: number|null) => void;
    bridge.getImportItemCount.mockImplementation((sourceId: string) => {
      if (sourceId === 'chrome-first') {
        return new Promise(resolve => resolveFirst = resolve);
      }
      return Promise.resolve(2);
    });
    const app = await createApp();
    app.sources_ = [
      {
        id: 'chrome-first',
        kind: 'chrome',
        browserName: 'Google Chrome',
        profileName: 'First',
        supportedCategories: ['bookmarks'],
      },
      {
        id: 'edge-second',
        kind: 'edge',
        browserName: 'Microsoft Edge',
        profileName: 'Second',
        supportedCategories: ['bookmarks'],
      },
    ];
    await app.updateComplete;

    (app.shadowRoot!.querySelector('[data-source-id="chrome-first"]') as
     HTMLButtonElement).click();
    await app.updateComplete;
    (app.shadowRoot!.querySelector('[data-source-id="edge-second"]') as
     HTMLButtonElement).click();
    await vi.waitFor(() => expect(app.categoryCounts_.bookmarks).toBe(2));
    resolveFirst(999);
    await Promise.resolve();

    expect(app.categoryCounts_.bookmarks).toBe(2);
  });

  it('does not apply completion-view sizing to completed progress segments',
     async () => {
       const app = await createApp();
       app.step_ = 2;
       await app.updateComplete;

       const appClass = customElements.get('dao-import-app') as unknown as {
         styles: {strings: string[]; values: unknown[]};
       };
       const cssText = appClass.styles.strings.reduce(
           (text, part, index) =>
               `${text}${part}${String(appClass.styles.values[index] ?? '')}`,
           '');
       const style = document.createElement('style');
       style.textContent = cssText;
       const fixture = document.createElement('div');
       fixture.innerHTML = `
         <div class="shell">
           <header><div class="rail"><span class="done"></span></div></header>
         </div>`;
       document.body.append(style, fixture);

       const completedSegment = fixture.querySelector('.rail span')!;
       expect(getComputedStyle(completedSegment).minHeight).not.toBe('390px');
     });

  it('starts migration with the selected profile and categories', async () => {
    const app = await createApp();
    app.sources_ = [{
      id: 'edge-work',
      kind: 'edge',
      browserName: 'Microsoft Edge',
      profileName: 'Work',
      supportedCategories: ['bookmarks', 'tabs'],
    }];
    app.selectedSourceId_ = 'edge-work';
    app.selectedCategories_ = ['bookmarks', 'tabs'];
    app.step_ = 2;
    await app.updateComplete;

    (app.shadowRoot!.querySelector('[data-test="start"]') as
     HTMLButtonElement).click();

    expect(bridge.startBrowserMigration).toHaveBeenCalledWith(
        'edge-work', ['bookmarks', 'tabs']);
  });

  it('reconnects directly to a running job', async () => {
    bridge.getBrowserMigrationState.mockResolvedValue({
      sourceId: 'chrome-default',
      terminal: false,
      cancelRequested: false,
      categories: [],
    });

    const app = await createApp();
    await app.updateComplete;

    expect(app.step_).toBe(3);
    expect(app.selectedSourceId_).toBe('chrome-default');
  });

  it('renders cancellation instead of successful completion', async () => {
    bridge.getBrowserMigrationState.mockResolvedValue({
      sourceId: 'chrome-default',
      terminal: true,
      cancelRequested: true,
      categories: [{
        category: 'bookmarks',
        phase: 'cancelled',
        imported: 3,
        skipped: 0,
        conflicted: 0,
        failed: 0,
        completedItems: 3,
        totalItems: 10,
        indeterminate: false,
        errorCode: '',
      }],
    });

    const app = await createApp();
    await app.updateComplete;

    expect(app.shadowRoot!.textContent).toContain('daoImportCancelledTitle');
    expect(app.shadowRoot!.textContent).not.toContain('daoImportDoneTitle');
  });
});
