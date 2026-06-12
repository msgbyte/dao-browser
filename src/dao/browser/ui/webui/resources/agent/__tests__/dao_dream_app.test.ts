// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

const bridgeMocks = vi.hoisted(() => ({
  callNative: vi.fn(),
  callNativeArgs: vi.fn(),
}));

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('../../sidebar/__tests__/lit_test_shim.js');
});

vi.mock('../dream_bridge.js', () => ({
  callNative: (...args: unknown[]) => bridgeMocks.callNative(...args),
  callNativeArgs: (...args: unknown[]) => bridgeMocks.callNativeArgs(...args),
}));

vi.mock('../i18n/i18n.js', () => ({
  initI18n: vi.fn(async () => undefined),
  t: (key: string, vars?: Record<string, string | number>) => {
    const templates: Record<string, string> = {
      'chat.dream.card_date': 'About {date}',
    };
    const template = templates[key] || key;
    if (!vars) return key;
    return Object.entries(vars).reduce(
        (text, [name, value]) => text.replaceAll(`{${name}}`, String(value)),
        template);
  },
}));

vi.mock('../dao_markdown.js', () => ({
  renderDaoMarkdown: (markdown: string) => markdown,
}));

vi.mock('../vendor/pi_runtime_bundle.js', () => ({
  marked: {parse: (markdown: string) => markdown},
}));

import '../dao_dream_app.js';

type TestDreamApp = HTMLElement & {updateComplete: Promise<boolean>};

function report(dreamDate: string) {
  return {
    id: Number(dreamDate.slice(-2)),
    dreamDate,
    reportMarkdown: `# ${dreamDate}`,
    habitCandidates: '[]',
    debugMaterialJson: '',
    triggerKind: 'manual',
  };
}

async function mountDreamApp(pathname: string): Promise<TestDreamApp> {
  window.history.pushState({}, '', pathname);
  const el = document.createElement('dao-dream-app') as TestDreamApp;
  document.body.appendChild(el);
  await el.updateComplete;
  await Promise.resolve();
  await el.updateComplete;
  return el;
}

describe('dao-dream-app routing', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    bridgeMocks.callNative.mockReset();
    bridgeMocks.callNativeArgs.mockReset();
    bridgeMocks.callNativeArgs.mockResolvedValue({success: true});
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
  });

  it('loads dream history for dao://dream/', async () => {
    bridgeMocks.callNative.mockResolvedValueOnce([
      report('2026-06-12'),
      report('2026-06-11'),
    ]);

    const el = await mountDreamApp('/');

    expect(bridgeMocks.callNative).toHaveBeenCalledWith(
        'getDreamReports', {limit: 30});
    expect(el.shadowRoot!.textContent).toContain('2026-06-12');
    expect(el.shadowRoot!.textContent).toContain('2026-06-11');
  });

  it('loads dream history for dao://dream/history', async () => {
    bridgeMocks.callNative.mockResolvedValueOnce([report('2026-06-10')]);

    const el = await mountDreamApp('/history');

    expect(bridgeMocks.callNative).toHaveBeenCalledWith(
        'getDreamReports', {limit: 30});
    expect(el.shadowRoot!.textContent).toContain('2026-06-10');
  });

  it('loads today report for dao://dream/today', async () => {
    bridgeMocks.callNative.mockResolvedValueOnce(report('2026-06-13'));

    const el = await mountDreamApp('/today');

    expect(bridgeMocks.callNative).toHaveBeenCalledWith('getTodayDreamReport');
    expect(bridgeMocks.callNative).not.toHaveBeenCalledWith(
        'getDreamReports', {limit: 30});
    expect(el.shadowRoot!.textContent).toContain('2026-06-13');
  });
});
