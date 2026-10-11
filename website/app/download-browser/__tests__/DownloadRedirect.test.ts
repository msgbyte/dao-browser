import { act, createElement } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { DownloadRedirect } from '../DownloadRedirect';
import zhCN from '../../../locales/zh-CN/download.json';

const mac = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15';
const config = {
  version: '1.2.3', chromiumVersion: '149.0.0.0', releasedAt: '2026-10-12',
  default: 'macArm64', platforms: {
    macArm64: { label: 'macOS (Apple Silicon)', url: 'https://cdn.example/mac.dmg' },
    win: { label: 'Windows (x64)', version: '1.2.2', url: 'https://cdn.example/windows.exe' },
  },
};
let container: HTMLDivElement;
let root: Root;

beforeEach(() => {
  vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true);
  vi.useFakeTimers();
  vi.spyOn(navigator, 'language', 'get').mockReturnValue('en-US');
  vi.spyOn(navigator, 'userAgent', 'get').mockReturnValue(mac);
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ ok: true, json: async () => config }));
  container = document.createElement('div');
  document.body.append(container);
  root = createRoot(container);
});

afterEach(async () => {
  await act(async () => root.unmount());
  container.remove();
  vi.useRealTimers();
  vi.unstubAllGlobals();
});

describe('download page', () => {
  it.each([
    'Mozilla/5.0 (Windows NT 10.0; Win64; x64)',
    'Mozilla/5.0 (X11; Linux x86_64)',
    'Mozilla/5.0 (Linux; Android 15)',
    'Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) Mobile/15E148',
    'Mozilla/5.0 (iPad; CPU OS 18_0 like Mac OS X) Mobile/15E148',
    '',
  ])('offers manual links without scheduling a download: %s', async userAgent => {
    vi.spyOn(navigator, 'userAgent', 'get').mockReturnValue(userAgent);
    await act(async () => root.render(createElement(DownloadRedirect)));
    expect([...container.querySelectorAll('a')].map(link => link.href)).toEqual([
      'https://cdn.example/mac.dmg', 'https://cdn.example/windows.exe',
    ]);
    expect(container.textContent).toContain('1.2.2');
    expect(vi.getTimerCount()).toBe(0);
  });

  it('treats an iPad with a desktop Mac user agent as a manual download', async () => {
    const touchPoints = Object.getOwnPropertyDescriptor(navigator, 'maxTouchPoints');
    Object.defineProperty(navigator, 'maxTouchPoints', { configurable: true, value: 5 });
    try {
      await act(async () => root.render(createElement(DownloadRedirect)));
      expect(container.querySelector('a[href="https://cdn.example/windows.exe"]')).not.toBeNull();
      expect(vi.getTimerCount()).toBe(0);
    } finally {
      if (touchPoints) Object.defineProperty(navigator, 'maxTouchPoints', touchPoints);
      else Reflect.deleteProperty(navigator, 'maxTouchPoints');
    }
  });

  it('schedules a Mac download and cancels it when leaving the page', async () => {
    await act(async () => root.render(createElement(DownloadRedirect)));
    expect(container.querySelector('a[href="https://cdn.example/mac.dmg"]')).not.toBeNull();
    expect(vi.getTimerCount()).toBe(1);
    await act(async () => root.unmount());
    expect(vi.getTimerCount()).toBe(0);
  });

  it('offers manual downloads when the Mac build is missing', async () => {
    vi.mocked(fetch).mockResolvedValue({
      ok: true, json: async () => ({ ...config, platforms: { win: config.platforms.win } }),
    } as Response);
    await act(async () => root.render(createElement(DownloadRedirect)));
    expect(container.querySelector('a[href="https://cdn.example/windows.exe"]')).not.toBeNull();
    expect(vi.getTimerCount()).toBe(0);
  });

  it('cancels automatic downloading when a manual link is selected', async () => {
    await act(async () => root.render(createElement(DownloadRedirect)));
    const link = container.querySelector<HTMLAnchorElement>('a[href="https://cdn.example/windows.exe"]')!;
    link.addEventListener('click', event => event.preventDefault());
    await act(async () => link.click());
    expect(vi.getTimerCount()).toBe(0);
  });

  it('only offers published platforms and uses Chinese download messages', async () => {
    vi.spyOn(navigator, 'userAgent', 'get').mockReturnValue('Windows');
    vi.spyOn(navigator, 'language', 'get').mockReturnValue('zh-CN');
    vi.mocked(fetch).mockResolvedValue({
      ok: true, json: async () => ({ ...config, platforms: { macArm64: config.platforms.macArm64 } }),
    } as Response);
    await act(async () => root.render(createElement(DownloadRedirect)));
    expect(container.querySelector('main')?.lang).toBe('zh-CN');
    expect(container.querySelector('h1')?.textContent).toBe(zhCN.title);
    expect([...container.querySelectorAll('a')].map(link => link.href)).toEqual([
      'https://cdn.example/mac.dmg',
    ]);
    expect(vi.getTimerCount()).toBe(0);
  });

  it('shows an error instead of an empty download list', async () => {
    vi.mocked(fetch).mockResolvedValue({
      ok: true, json: async () => ({ ...config, platforms: {} }),
    } as Response);
    await act(async () => root.render(createElement(DownloadRedirect)));
    expect(container.querySelector('a[href="https://github.com/msgbyte/dao-browser/releases"]')).not.toBeNull();
    expect(vi.getTimerCount()).toBe(0);
  });

  it('shows a release-page fallback if loading the download config fails', async () => {
    vi.mocked(fetch).mockRejectedValue(new Error('Offline'));
    await act(async () => root.render(createElement(DownloadRedirect)));
    expect(container.querySelector('a[href="https://github.com/msgbyte/dao-browser/releases"]')).not.toBeNull();
    expect(vi.getTimerCount()).toBe(0);
  });
});
