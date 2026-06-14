// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  const lit = await import('../../sidebar/__tests__/lit_test_shim.js');
  Object.defineProperty(lit.CrLitElement.prototype, 'disconnectedCallback', {
    configurable: true,
    value() {},
  });
  return lit;
});

vi.mock('../i18n/i18n.js', () => ({
  initI18n: vi.fn(async () => undefined),
  t: (key: string, vars?: Record<string, string | number>) => {
    if (!vars) return key;
    return Object.entries(vars).reduce(
        (text, [name, value]) => text.replaceAll(`{${name}}`, String(value)),
        key);
  },
}));

interface TestMemoryApp extends HTMLElement {
  updateComplete: Promise<boolean>;
}

function textCell(value: string) {
  return {
    type: 'text',
    value,
  };
}

async function mountMemoryApp(): Promise<TestMemoryApp> {
  const send = vi.fn((method: string, args: unknown[]) => {
    const callbackId = args[0] as string;
    const response = method === 'memoryGetTables' ? {
      ok: true,
      error: '',
      columns: ['name', 'type'],
      rows: [[textCell('memories'), textCell('table')]],
      truncated: false,
    } : {
      ok: true,
      error: '',
      columns: ['id', 'text'],
      rows: [[textCell('1'), textCell('alpha')]],
      truncated: false,
    };
    queueMicrotask(() => {
      (globalThis as unknown as {
        cr: {webUIResponse: (id: string, ok: boolean, value: unknown) => void};
      }).cr.webUIResponse(callbackId, true, response);
    });
  });
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};

  await import('../dao_memory_app.js');

  const el = document.createElement('dao-memory-app') as TestMemoryApp;
  document.body.appendChild(el);
  for (let i = 0; i < 10; i++) {
    await el.updateComplete;
    await Promise.resolve();
    if (el.shadowRoot?.querySelector('.filter-button')) {
      break;
    }
  }
  return el;
}

describe('dao-memory-app', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
    vi.restoreAllMocks();
  });

  it('keeps the filter action in the table toolbar', async () => {
    const el = await mountMemoryApp();
    const root = el.shadowRoot!;
    const toolbar = root.querySelector('.results-toolbar');
    const filterButton = root.querySelector('.filter-button');

    expect(toolbar).toBeTruthy();
    expect(filterButton).toBeTruthy();
    expect(toolbar!.contains(filterButton!)).toBe(true);
    expect(root.querySelector('.table-tools')).toBeNull();
  });

  it('renders the filter action as an icon-only button', async () => {
    const el = await mountMemoryApp();
    const root = el.shadowRoot!;
    const filterButton = root.querySelector('.filter-button');
    const iconPath = filterButton?.querySelector('svg path');

    expect(filterButton).toBeTruthy();
    expect(filterButton!.querySelector('svg')).toBeTruthy();
    expect(iconPath?.getAttribute('d')).toBe(
        'M10 20a1 1 0 0 0 .553.895l2 1A1 1 0 0 0 14 21v-7a2 2 0 0 1 .517-1.341L21.74 4.67A1 1 0 0 0 21 3H3a1 1 0 0 0-.742 1.67l7.225 7.989A2 2 0 0 1 10 14z');
    expect(filterButton!.getAttribute('aria-label')).toBe(
        'memory.filter_button');
    expect(filterButton!.textContent?.trim()).toBe('');
  });

  it('closes the filter popup when clicking outside it', async () => {
    const el = await mountMemoryApp();
    const root = el.shadowRoot!;
    const filterButton = root.querySelector('.filter-button') as HTMLElement;

    filterButton.click();
    await el.updateComplete;

    expect(root.querySelector('.filter-panel')).toBeTruthy();

    document.body.dispatchEvent(new Event('pointerdown', {
      bubbles: true,
      composed: true,
    }));
    await el.updateComplete;

    expect(root.querySelector('.filter-panel')).toBeNull();
  });

  it('lets table cells fill their column without an inner fixed width', async () => {
    const el = await mountMemoryApp();
    const root = el.shadowRoot!;
    const firstColumn = root.querySelector('col');
    const firstCellContent = root.querySelector('td .cell-content');

    expect(firstColumn?.getAttribute('style')).toContain('width:');
    expect(firstCellContent).toBeTruthy();
    expect(firstCellContent!.getAttribute('style')).toBeNull();
  });
});
