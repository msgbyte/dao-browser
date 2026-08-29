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

import type {ActiveDownloadData} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});
vi.mock('//resources/js/load_time_data.js', () => ({
  loadTimeData: {getString: (id: string) => id},
}));

function getStyles(): string {
  const ctor = customElements.get('dao-download-button') as unknown as {
    styles: {strings: string[]};
  };
  return ctor.styles.strings.join('');
}

interface TestDownloadButton extends HTMLElement {
  activeDownloads_: ActiveDownloadData[];
  updateComplete: Promise<boolean>;
}

function activeDownload(
    extra: Partial<ActiveDownloadData> = {}): ActiveDownloadData {
  return {
    id: 7,
    name: 'archive.zip',
    percent: 12,
    speed: '2.0 MB/s',
    sizeDetail: '12.0 MB of 100 MB - 12%',
    statusDetail: '2.0 MB/s - 44 secs left',
    ...extra,
  };
}

async function renderDownloadButton() {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  const el = document.createElement(
      'dao-download-button') as TestDownloadButton;
  el.activeDownloads_ = [activeDownload()];
  document.body.appendChild(el);
  await el.updateComplete;
  const row = el.shadowRoot!.querySelector('.active-item') as HTMLElement;
  return {el, row, send};
}

describe('dao-download-button', () => {
  beforeAll(async () => {
    await import('../dao_download_button.js');
  });

  beforeEach(() => {
    vi.useFakeTimers();
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.useRealTimers();
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
  });

  it('lets the recent downloads popup sit flush with sidebar edges', () => {
    const styles = getStyles();

    expect(styles).toMatch(/\.popup-stack\s*{[^}]*left:\s*-6px;/s);
    expect(styles).toMatch(/\.popup-stack\s*{[^}]*width:\s*100vw;/s);
    expect(styles).toMatch(/\.popup-stack\s*{[^}]*padding:\s*0 0 6px;/s);
    expect(styles).toMatch(/\.popup-stack\s*{[^}]*box-sizing:\s*border-box;/s);
    expect(styles).not.toMatch(/\.popup-stack\s*{[^}]*padding:\s*0 6px 6px;/s);
  });

  it('shows detailed progress after a settled 400ms row hover', async () => {
    const {row, send} = await renderDownloadButton();

    row.dispatchEvent(new MouseEvent('mousemove', {
      screenX: 20,
      screenY: 30,
    }));
    vi.advanceTimersByTime(399);
    expect(send).not.toHaveBeenCalledWith(
        'showDownloadTooltip', expect.anything());

    vi.advanceTimersByTime(1);
    expect(send).toHaveBeenCalledWith('showDownloadTooltip', [
      24,
      34,
      'archive.zip',
      '12.0 MB of 100 MB - 12%',
      '2.0 MB/s - 44 secs left',
    ]);
  });

  it('restarts the hover delay when the pointer moves', async () => {
    const {row, send} = await renderDownloadButton();

    row.dispatchEvent(new MouseEvent('mousemove', {
      screenX: 20,
      screenY: 30,
    }));
    vi.advanceTimersByTime(300);
    row.dispatchEvent(new MouseEvent('mousemove', {
      screenX: 40,
      screenY: 50,
    }));
    vi.advanceTimersByTime(399);
    expect(send).not.toHaveBeenCalledWith(
        'showDownloadTooltip', expect.anything());

    vi.advanceTimersByTime(1);
    expect(send).toHaveBeenCalledWith('showDownloadTooltip', [
      44,
      54,
      'archive.zip',
      '12.0 MB of 100 MB - 12%',
      '2.0 MB/s - 44 secs left',
    ]);
  });

  it('hides visible details immediately when the pointer leaves', async () => {
    const {row, send} = await renderDownloadButton();

    row.dispatchEvent(new MouseEvent('mousemove', {
      screenX: 20,
      screenY: 30,
    }));
    vi.advanceTimersByTime(400);
    row.dispatchEvent(new MouseEvent('mouseleave'));

    expect(send).toHaveBeenLastCalledWith('hideTabTooltip', []);
  });

  it('hides visible details when the active download is removed', async () => {
    const {row, send} = await renderDownloadButton();

    row.dispatchEvent(new MouseEvent('mousemove', {
      screenX: 20,
      screenY: 30,
    }));
    vi.advanceTimersByTime(400);
    (window as unknown as {
      cr: {webUIListenerCallback: (event: string, value: unknown) => void};
    }).cr.webUIListenerCallback('activeDownloadsChanged', []);

    expect(send).toHaveBeenLastCalledWith('hideTabTooltip', []);
  });

  it('keeps a completed download until it is opened, closed, or left after hover', async () => {
    const {el, send} = await renderDownloadButton();
    const notify = (window as unknown as {
      cr: {webUIListenerCallback: (event: string, value: unknown) => void};
    }).cr.webUIListenerCallback;
    const completed = {id: 42, name: 'tiny.txt'};

    notify('downloadCompleted', completed);
    await el.updateComplete;
    expect(el.shadowRoot!.querySelector('.completed-download')).not.toBeNull();

    (el.shadowRoot!.querySelector('.completed-open') as HTMLButtonElement).click();
    await el.updateComplete;
    expect(send).toHaveBeenCalledWith('openDownload', [42]);
    expect(el.shadowRoot!.querySelector('.completed-download')).toBeNull();

    notify('downloadCompleted', completed);
    await el.updateComplete;
    (el.shadowRoot!.querySelector('.completed-close') as HTMLButtonElement).click();
    await el.updateComplete;
    expect(el.shadowRoot!.querySelector('.completed-download')).toBeNull();
    expect(send.mock.calls.filter(([method]) => method === 'openDownload'))
        .toHaveLength(1);

    notify('downloadCompleted', completed);
    await el.updateComplete;
    const zone = el.shadowRoot!.querySelector('.trigger-zone') as HTMLElement;
    zone.dispatchEvent(new MouseEvent('mouseenter'));
    expect(send).toHaveBeenCalledWith('requestDownloadState', []);
    zone.dispatchEvent(new MouseEvent('mouseleave'));
    await el.updateComplete;
    expect(el.shadowRoot!.querySelector('.completed-download')).toBeNull();
  });
});
