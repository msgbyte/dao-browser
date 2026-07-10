// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

const bridgeMocks = vi.hoisted(() => ({
  callNative: vi.fn(),
  callNativeArgs: vi.fn(),
}));

const shareMocks = vi.hoisted(() => ({
  renderDreamReportShareImage: vi.fn(async () =>
      new Blob(['png'], {type: 'image/png'})),
  copyPngBlobToClipboard: vi.fn(async () => undefined),
}));

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('../../sidebar/__tests__/lit_test_shim.js');
});

vi.mock('../dream_bridge.js', () => ({
  callNative: (...args: unknown[]) => bridgeMocks.callNative(...args),
  callNativeArgs: (...args: unknown[]) => bridgeMocks.callNativeArgs(...args),
}));

vi.mock('../dao_share_image.js', () => ({
  renderDreamReportShareImage: (...args: unknown[]) =>
      shareMocks.renderDreamReportShareImage(...args),
  copyPngBlobToClipboard: (...args: unknown[]) =>
      shareMocks.copyPngBlobToClipboard(...args),
}));

vi.mock('../i18n/i18n.js', () => ({
  initI18n: vi.fn(async () => undefined),
  t: (key: string, vars?: Record<string, string | number>) => {
    const templates: Record<string, string> = {
      'chat.dream.card_date': 'About {date}',
      'chat.dream.habits_title': 'I think I noticed...',
      'dream.page.copy_image': 'Copy image',
      'dream.page.copy_image_copied': 'Copied image',
      'dream.page.copy_image_failed': 'Copy failed',
      'dream.page.rerun_report': 'Rerun report',
      'dream.page.rerun_running': 'Dreaming...',
      'dream.page.rerun_failed': 'Rerun failed: {error}',
      'dream.page.title': 'Dream Report',
      'dream.page.source_domains_title': 'Domains used in this report',
      'dream.page.source_domains_add': 'Add to blacklist',
      'dream.page.source_domains_confirm':
          'Add {domain} to the blacklist? Future Dream analysis will ignore it.',
      'dream.page.source_domains_excluded': 'Already blacklisted',
      'dream.page.excluded_domains_adding': 'Adding...',
      'dream.page.excluded_add_failed': 'Add failed: {error}',
      'dream.page.source_domains_empty':
          'No domains were captured for this report. Rerun it to refresh choices.',
      'dream.debug.generated_at': 'Generated at: {time}',
      'dream.share.footer': 'Dreamed by Dao Browser',
    };
    const template = templates[key];
    if (!template) return key;
    if (!vars) return template;
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

function report(
    dreamDate: string,
    habitCandidates = '[]',
    materialStats = '{}') {
  return {
    id: Number(dreamDate.slice(-2)),
    dreamDate,
    reportMarkdown: `# ${dreamDate}`,
    habitCandidates,
    materialStats,
    debugMaterialJson: '',
    triggerKind: 'manual',
  };
}

function habitCandidates() {
  return JSON.stringify([{
    key: 'preferred_search',
    value: 'Uses documentation search for implementation details',
    confidence: 0.7,
    evidence: 'Opened API docs before coding',
    relation: 'new',
  }]);
}

function createDeferred<T>() {
  let resolve!: (value: T | PromiseLike<T>) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return {promise, resolve, reject};
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

function getCopyImageButton(el: TestDreamApp): HTMLButtonElement|null {
  return el.shadowRoot!.querySelector<HTMLButtonElement>('.copy-image-button');
}

function expectIconOnlyCopyButton(
    button: HTMLButtonElement|null, label = 'Copy image') {
  expect(button).toBeTruthy();
  expect(button!.getAttribute('aria-label')).toBe(label);
  expect(button!.title).toBe(label);
  expect(button!.textContent?.trim()).toBe('');
  expect(button!.querySelector('svg[aria-hidden="true"]')).toBeTruthy();
}

describe('dao-dream-app routing', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    bridgeMocks.callNative.mockReset();
    bridgeMocks.callNativeArgs.mockReset();
    bridgeMocks.callNativeArgs.mockResolvedValue({success: true});
    shareMocks.renderDreamReportShareImage.mockClear();
    shareMocks.renderDreamReportShareImage.mockResolvedValue(
        new Blob(['png'], {type: 'image/png'}));
    shareMocks.copyPngBlobToClipboard.mockClear();
    shareMocks.copyPngBlobToClipboard.mockResolvedValue(undefined);
    localStorage.clear();
  });

  afterEach(() => {
    document.body.innerHTML = '';
    localStorage.clear();
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

  it('reruns the current report from the header action', async () => {
    bridgeMocks.callNative.mockImplementation(async (method: string,
        params?: unknown) => {
      if (method === 'getDreamReports') {
        return [report('2026-06-19'), report('2026-06-18')];
      }
      if (method === 'startManualDream') {
        expect(params).toEqual({date: '2026-06-19'});
        return true;
      }
      return undefined;
    });

    const el = await mountDreamApp('/');
    expect(el.shadowRoot!.querySelector(
        'input[data-testid="dream-rerun-date-input"]')).toBeNull();
    expect(el.shadowRoot!.querySelector('button[data-rerun-date]')).toBeNull();

    const rerunButton = el.shadowRoot!.querySelector<HTMLButtonElement>(
        'button[data-testid="dream-rerun-current-button"]');
    expect(rerunButton).toBeTruthy();
    expect(rerunButton!.getAttribute('aria-label')).toBe('Rerun report');
    rerunButton!.click();
    await Promise.resolve();
    await Promise.resolve();
    await el.updateComplete;

    expect(bridgeMocks.callNative).toHaveBeenCalledWith(
        'startManualDream', {date: '2026-06-19'}, {timeoutMs: 360000});
    expect(bridgeMocks.callNative).toHaveBeenCalledWith(
        'getDreamReports', {limit: 30});
  });

  it('lists report source domains and adds one to exclusions', async () => {
    const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValue(true);
    bridgeMocks.callNative.mockResolvedValueOnce([
      report('2026-06-19', habitCandidates(), JSON.stringify({
        source_domains: ['docs.example', 'private.example'],
      })),
    ]);
    bridgeMocks.callNativeArgs.mockImplementation(async (method: string,
        ...args: unknown[]) => {
      if (method === 'getDreamExcludedDomains') {
        return ['private.example'];
      }
      if (method === 'addDreamExcludedDomain') {
        expect(args).toEqual(['docs.example']);
        return {domain: 'docs.example'};
      }
      return {success: true};
    });

    const el = await mountDreamApp('/');
    await Promise.resolve();
    await el.updateComplete;
    await Promise.resolve();
    await el.updateComplete;

    const picker = el.shadowRoot!.querySelector<HTMLDetailsElement>(
        'details.report-domain-picker');
    expect(picker).toBeTruthy();
    expect(picker!.open).toBe(false);
    expect(picker!.textContent).toContain('Domains used in this report');

    const summary = picker!.querySelector<HTMLElement>(
        'summary.report-domain-summary');
    expect(summary).toBeTruthy();
    summary!.click();
    expect(picker!.open).toBe(true);

    expect(picker!.textContent).toContain('docs.example');
    expect(picker!.textContent).toContain('private.example');
    expect(picker!.textContent).toContain('Already blacklisted');
    expect(picker!.querySelector('input[data-domain]')).toBeNull();
    const markdown = el.shadowRoot!.querySelector('.report-body');
    expect(markdown).toBeTruthy();
    expect(Boolean(
        markdown!.compareDocumentPosition(picker!) &
        Node.DOCUMENT_POSITION_FOLLOWING))
        .toBe(true);
    const habitHeading = Array.from(el.shadowRoot!.querySelectorAll('h2'))
                             .find(node => node.textContent?.includes(
                                 'I think I noticed'));
    expect(habitHeading).toBeTruthy();
    expect(Boolean(
        habitHeading!.compareDocumentPosition(picker!) &
        Node.DOCUMENT_POSITION_FOLLOWING))
        .toBe(true);

    const addButton = el.shadowRoot!.querySelector<HTMLButtonElement>(
        'button[data-testid="dream-add-domain-button"][data-domain="docs.example"]');
    expect(addButton).toBeTruthy();
    expect(addButton!.disabled).toBe(false);
    expect(addButton!.title).toBe('Add to blacklist');
    expect(addButton!.getAttribute('aria-label')).toBe('Add to blacklist');
    expect(addButton!.textContent?.trim()).toBe('');
    const exclusionIcon =
        addButton!.querySelector('svg[aria-hidden="true"]');
    expect(exclusionIcon).toBeTruthy();
    expect(exclusionIcon!.querySelector(
        'circle[cx="12"][cy="12"][r="10"]'))
        .toBeTruthy();
    expect(exclusionIcon!.querySelector(
        'path[d="M4.929 4.929 19.07 19.071"]'))
        .toBeTruthy();
    addButton!.dispatchEvent(
        new MouseEvent('click', {bubbles: true, composed: true}));
    await Promise.resolve();
    await Promise.resolve();
    await el.updateComplete;

    expect(confirmSpy).toHaveBeenCalledWith(
        'Add docs.example to the blacklist? Future Dream analysis will ignore it.');
    expect(bridgeMocks.callNativeArgs).toHaveBeenCalledWith(
        'addDreamExcludedDomain', 'docs.example');
    for (let i = 0; i < 5 &&
         !el.shadowRoot!.querySelector(
             '.report-domain-option.excluded [data-domain-label="docs.example"]');
         i++) {
      await Promise.resolve();
      await el.updateComplete;
    }
    expect(el.shadowRoot!.querySelector(
        '.report-domain-option.excluded [data-domain-label="docs.example"]'))
        .toBeTruthy();
  });

  it('does not exclude a report domain when confirmation is canceled',
      async () => {
    const confirmSpy = vi.spyOn(window, 'confirm').mockReturnValue(false);
    bridgeMocks.callNative.mockResolvedValueOnce([
      report('2026-06-19', habitCandidates(), JSON.stringify({
        source_domains: ['docs.example'],
      })),
    ]);

    const el = await mountDreamApp('/');
    await Promise.resolve();
    await el.updateComplete;
    await Promise.resolve();
    await el.updateComplete;

    const picker = el.shadowRoot!.querySelector<HTMLDetailsElement>(
        'details.report-domain-picker');
    expect(picker).toBeTruthy();
    picker!.open = true;
    const addButton = picker!.querySelector<HTMLButtonElement>(
        'button[data-domain="docs.example"]');
    expect(addButton).toBeTruthy();
    addButton!.click();
    await Promise.resolve();

    expect(confirmSpy).toHaveBeenCalledWith(
        'Add docs.example to the blacklist? Future Dream analysis will ignore it.');
    expect(bridgeMocks.callNativeArgs).not.toHaveBeenCalledWith(
        'addDreamExcludedDomain', 'docs.example');
    expect(addButton!.disabled).toBe(false);
  });

  it('shows the exclusion shortcut even when an older report has no domains',
      async () => {
    bridgeMocks.callNative.mockResolvedValueOnce([report('2026-06-20')]);

    const el = await mountDreamApp('/');
    await Promise.resolve();
    await el.updateComplete;

    const picker = el.shadowRoot!.querySelector('.report-domain-picker');
    expect(picker).toBeTruthy();
    expect(picker!.textContent).toContain('Domains used in this report');
    expect(picker!.textContent).toContain(
        'No domains were captured for this report');
    expect(picker!.querySelector('input[data-domain]')).toBeNull();
  });

  it('keeps habit feedback selected when the same report is reopened', async () => {
    bridgeMocks.callNative.mockResolvedValue([
      report('2026-06-12', habitCandidates()),
    ]);

    const el = await mountDreamApp('/');
    const confirmButton = Array.from(el.shadowRoot!.querySelectorAll('button'))
                              .find(button => button.textContent?.includes(
                                  'chat.dream.habit_confirm'));
    expect(confirmButton).toBeTruthy();

    confirmButton!.click();
    await el.updateComplete;

    expect(bridgeMocks.callNativeArgs).toHaveBeenCalledWith(
        'updatePreference', 'preferred_search',
        'Uses documentation search for implementation details', 0.95);
    expect(el.shadowRoot!.textContent).toContain(
        'chat.dream.habit_confirmed');

    document.body.innerHTML = '';
    const reopened = await mountDreamApp('/');

    expect(reopened.shadowRoot!.textContent).toContain(
        'chat.dream.habit_confirmed');
    expect(reopened.shadowRoot!.textContent).not.toContain(
        'chat.dream.habit_reject');
  });

  it('copies the current dream report as an image', async () => {
    bridgeMocks.callNative.mockResolvedValueOnce([
      {
        ...report('2026-06-19', habitCandidates()),
        reportMarkdown: '# Private report body',
        debugMaterialJson: '{"private":true}',
      },
    ]);

    const el = await mountDreamApp('/');
    const copyButton = getCopyImageButton(el);
    expectIconOnlyCopyButton(copyButton);

    copyButton!.dispatchEvent(
        new MouseEvent('click', {bubbles: true, composed: true}));
    await Promise.resolve();
    await Promise.resolve();
    await el.updateComplete;

    expect(shareMocks.renderDreamReportShareImage).toHaveBeenCalledWith({
      title: 'Dream Report',
      dateLabel: 'About 2026-06-19',
      markdown: '# Private report body',
      footer: 'Dreamed by Dao Browser',
    });
    const blob = await shareMocks.renderDreamReportShareImage.mock.results[0]!
                       .value;
    expect(shareMocks.copyPngBlobToClipboard).toHaveBeenCalledWith(blob);
    expectIconOnlyCopyButton(getCopyImageButton(el), 'Copied image');
  });

  it('shows the generated time inside debug details', async () => {
    bridgeMocks.callNative.mockResolvedValueOnce([
      {
        ...report('2026-06-19'),
        createdAt: Date.UTC(2026, 5, 19, 15, 42, 10),
        debugMaterialJson: '{"private":true}',
      },
    ]);

    const el = await mountDreamApp('/');
    const text = el.shadowRoot!.textContent || '';

    expect(text).toContain('Generated at:');
    expect(text).toContain('2026');
    expect(text).toContain('42');
    expect(text).toContain('{"private":true}');
  });

  it('disables copy image button while copy is in progress', async () => {
    bridgeMocks.callNative.mockResolvedValueOnce([
      {
        ...report('2026-06-19', habitCandidates()),
        reportMarkdown: '# Private report body',
        debugMaterialJson: '{"private":true}',
      },
    ]);

    const copyDeferred = createDeferred<void>();
    shareMocks.copyPngBlobToClipboard.mockReturnValueOnce(copyDeferred.promise);

    const el = await mountDreamApp('/');
    const copyButton = getCopyImageButton(el);
    expectIconOnlyCopyButton(copyButton);

    copyButton!.click();
    await Promise.resolve();
    await el.updateComplete;

    const copyingButton = getCopyImageButton(el);
    expect(copyingButton?.disabled).toBe(true);
    expectIconOnlyCopyButton(copyingButton);

    copyDeferred.resolve(undefined);
    await copyDeferred.promise;
    await Promise.resolve();
    await el.updateComplete;

    const restoredButton = getCopyImageButton(el);
    expect(restoredButton?.disabled).toBe(false);
    expectIconOnlyCopyButton(restoredButton, 'Copied image');
  });

  it('shows copy failure when image clipboard write fails', async () => {
    bridgeMocks.callNative.mockResolvedValueOnce([report('2026-06-19')]);
    shareMocks.copyPngBlobToClipboard.mockRejectedValueOnce(
        new Error('no clipboard'));

    const el = await mountDreamApp('/');
    const copyButton = getCopyImageButton(el);
    expectIconOnlyCopyButton(copyButton);

    copyButton!.dispatchEvent(
        new MouseEvent('click', {bubbles: true, composed: true}));
    await Promise.resolve();
    await Promise.resolve();
    await el.updateComplete;

    expectIconOnlyCopyButton(getCopyImageButton(el), 'Copy failed');
  });

  it('does not render copy image while the dream page is empty', async () => {
    bridgeMocks.callNative.mockResolvedValueOnce([]);

    const el = await mountDreamApp('/');

    expect(getCopyImageButton(el)).toBeNull();
    expect(shareMocks.renderDreamReportShareImage).not.toHaveBeenCalled();
  });

  it('does not render copy image when dream report loading fails', async () => {
    bridgeMocks.callNative.mockRejectedValueOnce(new Error('boom'));

    const el = await mountDreamApp('/');

    expect(getCopyImageButton(el)).toBeNull();
    expect(shareMocks.renderDreamReportShareImage).not.toHaveBeenCalled();
  });
});
