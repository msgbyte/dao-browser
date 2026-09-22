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
    vi.unstubAllGlobals();
    delete (Element.prototype as unknown as {animate?: unknown}).animate;
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

  it('keeps the completed drag source when the pointer leaves before dragstart', async () => {
    const {el, send} = await renderDownloadButton();
    const notify = (window as unknown as {
      cr: {webUIListenerCallback: (event: string, value: unknown) => void};
    }).cr.webUIListenerCallback;
    notify('downloadCompleted', {id: 42, name: 'tiny.txt'});
    await el.updateComplete;

    const button = el.shadowRoot!.querySelector(
        '.completed-open') as HTMLButtonElement;
    expect(button.draggable).toBe(true);
    expect((el.shadowRoot!.querySelector(
        '.completed-close') as HTMLButtonElement).draggable).toBe(false);

    const zone = el.shadowRoot!.querySelector('.trigger-zone')!;
    zone.dispatchEvent(new MouseEvent('mouseenter'));
    button.dispatchEvent(new MouseEvent('mousedown', {button: 0, buttons: 1}));
    zone.dispatchEvent(new MouseEvent('mouseleave', {buttons: 1}));
    await el.updateComplete;
    expect(el.shadowRoot!.querySelector('.completed-open')).toBe(button);

    const drag = new Event('dragstart', {bubbles: true, cancelable: true});
    button.dispatchEvent(drag);
    button.click();
    await el.updateComplete;

    expect(drag.defaultPrevented).toBe(true);
    expect(send).toHaveBeenCalledWith('startDownloadDrag', [42]);
    expect(send).not.toHaveBeenCalledWith('openDownload', expect.anything());
    expect(el.classList.contains('expanded')).toBe(false);
    expect(el.shadowRoot!.querySelector('.completed-download')).toBeNull();
  });

  it.each(['mouseup', 'blur'])(
      'dismisses a pending completed label on %s without opening it', async event => {
        const {el, send} = await renderDownloadButton();
        (window as unknown as {
          cr: {webUIListenerCallback: (event: string, value: unknown) => void};
        }).cr.webUIListenerCallback('downloadCompleted', {id: 42, name: 'tiny.txt'});
        await el.updateComplete;

        const zone = el.shadowRoot!.querySelector('.trigger-zone')!;
        zone.dispatchEvent(new MouseEvent('mouseenter'));
        el.shadowRoot!.querySelector('.completed-open')!.dispatchEvent(
            new MouseEvent('mousedown', {button: 0, buttons: 1}));
        zone.dispatchEvent(new MouseEvent('mouseleave', {buttons: 1}));
        await el.updateComplete;
        expect(el.shadowRoot!.querySelector('.completed-download')).not.toBeNull();

        window.dispatchEvent(new Event(event));
        await el.updateComplete;
        expect(el.shadowRoot!.querySelector('.completed-download')).toBeNull();
        expect(el.classList.contains('expanded')).toBe(false);
        expect(send).not.toHaveBeenCalledWith('openDownload', expect.anything());
        expect(send).not.toHaveBeenCalledWith('startDownloadDrag', expect.anything());
      });

  it('preserves clicking after a held pointer leaves and returns', async () => {
    const {el, send} = await renderDownloadButton();
    (window as unknown as {
      cr: {webUIListenerCallback: (event: string, value: unknown) => void};
    }).cr.webUIListenerCallback('downloadCompleted', {id: 42, name: 'tiny.txt'});
    await el.updateComplete;

    const zone = el.shadowRoot!.querySelector('.trigger-zone')!;
    const button = el.shadowRoot!.querySelector('.completed-open') as HTMLButtonElement;
    zone.dispatchEvent(new MouseEvent('mouseenter'));
    button.dispatchEvent(new MouseEvent('mousedown', {button: 0, buttons: 1}));
    zone.dispatchEvent(new MouseEvent('mouseleave', {buttons: 1}));
    zone.dispatchEvent(new MouseEvent('mouseenter', {buttons: 1}));
    window.dispatchEvent(new MouseEvent('mouseup'));
    await el.updateComplete;
    expect(el.shadowRoot!.querySelector('.completed-open')).toBe(button);
    button.click();
    expect(send).toHaveBeenCalledWith('openDownload', [42]);
    expect(send).not.toHaveBeenCalledWith('startDownloadDrag', expect.anything());
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

  it('targets the rendered icon even when a download completes immediately', async () => {
    const {el, send} = await renderDownloadButton();
    const notify = (window as unknown as {
      cr: {webUIListenerCallback: (event: string, value?: unknown) => void};
    }).cr.webUIListenerCallback;
    const measure = vi.spyOn(Element.prototype, 'getBoundingClientRect')
        .mockReturnValue(new DOMRect(10, 500, 14, 14));
    const animate = vi.fn((_frames: Keyframe[], _options: KeyframeAnimationOptions) =>
        ({cancel: vi.fn()}));
    vi.stubGlobal('matchMedia', () => ({matches: false}));
    Object.defineProperty(Element.prototype, 'animate', {
      configurable: true, value: animate,
    });

    notify('downloadStarted', 42);
    notify('downloadCompleted', {id: 42, name: 'tiny.txt'});
    notify('activeDownloadsChanged', []);
    await el.updateComplete;
    await Promise.resolve();
    expect(send).toHaveBeenCalledWith(
        'showDownloadStartedAnimation', [42, 17, 507, false]);
    expect(measure.mock.instances.at(-1)).toBe(
        el.shadowRoot!.querySelector('.completed-open svg'));
    expect(animate).not.toHaveBeenCalled();

    notify('downloadAnimationFinished');
    expect(animate).toHaveBeenCalledOnce();
    const cancel = animate.mock.results[0]!.value.cancel;
    notify('downloadAnimationFinished');
    expect(cancel).toHaveBeenCalledOnce();
    expect(animate).toHaveBeenCalledTimes(2);

    el.remove();
    send.mockClear();
    notify('downloadStarted', 43);
    notify('downloadAnimationFinished');
    await Promise.resolve();
    expect(send).not.toHaveBeenCalled();
    expect(animate).toHaveBeenCalledTimes(2);
  });

  it('requests reduced motion and highlights without moving the icon', async () => {
    const {el, send} = await renderDownloadButton();
    const notify = (window as unknown as {
      cr: {webUIListenerCallback: (event: string, value?: unknown) => void};
    }).cr.webUIListenerCallback;
    vi.spyOn(Element.prototype, 'getBoundingClientRect')
        .mockReturnValue(new DOMRect(10, 500, 14, 14));
    vi.stubGlobal('matchMedia', () => ({matches: true}));
    const animate = vi.fn((_frames: Keyframe[], _options: KeyframeAnimationOptions) =>
        ({cancel: vi.fn()}));
    Object.defineProperty(Element.prototype, 'animate', {
      configurable: true, value: animate,
    });
    notify('downloadStarted', 7);
    await el.updateComplete;
    await Promise.resolve();
    expect(send).toHaveBeenCalledWith(
        'showDownloadStartedAnimation', [7, 17, 507, true]);
    notify('downloadAnimationFinished');
    expect(animate).toHaveBeenCalledOnce();
    expect(animate.mock.calls[0]![0]).not.toEqual(
        expect.arrayContaining([expect.objectContaining({transform: expect.anything()})]));
  });
});
