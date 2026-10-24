import { act, createElement } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { renderToStaticMarkup } from 'react-dom/server';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { InlineMarkdown } from '../InlineMarkdown';
import { ChangelogTimeline } from '../ChangelogTimeline';
import type { ChangelogPlatform } from '@/lib/changelog';

const platforms: ChangelogPlatform[] = [
  { id: 'desktop', title: 'Desktop', releases: [{ version: '1.0.1', date: '2026-10-23', entries: ['Desktop entry.'] }] },
  { id: 'android', title: 'Android', releases: [{ version: '0.1.0', date: '2026-10-22', entries: ['Android entry.'] }] },
  { id: 'ios', title: 'iOS', releases: [] },
];

describe('changelog inline Markdown', () => {
  it('preserves code and safe links without interpreting HTML or unsafe URLs', () => {
    const html = renderToStaticMarkup(createElement(InlineMarkdown, {
      text: 'Use `chrome://settings` and [help](https://example.com/help). <script>alert(1)</script> [bad](javascript:alert) [relative](../changelog.md)',
    }));
    const element = document.createElement('div');
    element.innerHTML = html;
    expect(element.querySelector('code')?.textContent).toBe('chrome://settings');
    expect(element.querySelectorAll('a')).toHaveLength(2);
    expect(element.querySelector('a')?.href).toBe('https://example.com/help');
    expect(element.querySelectorAll('a')[1].href).toBe('https://github.com/msgbyte/dao-browser/blob/main/docs/changelog.md');
    expect(element.querySelector('script')).toBeNull();
    expect(element.textContent).toContain('[bad](javascript:alert)');
  });
});

describe('changelog navigation', () => {
  let container: HTMLDivElement;
  let root: Root;
  const scrollIntoView = vi.fn();

  beforeEach(() => {
    vi.stubGlobal('IS_REACT_ACT_ENVIRONMENT', true);
    Object.defineProperty(Element.prototype, 'scrollIntoView', { configurable: true, value: scrollIntoView });
    scrollIntoView.mockClear();
    window.history.replaceState(null, '', '/changelog');
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(async () => {
    await act(async () => root.unmount());
    container.remove();
    vi.unstubAllGlobals();
  });

  it('opens a shared Android version and preserves its anchor when changing language', async () => {
    window.history.replaceState(null, '', '/changelog#android-0.1.0');
    await act(async () => root.render(createElement(ChangelogTimeline, { platforms, locale: 'en' })));
    expect(container.querySelector<HTMLElement>('#android')?.hidden).toBe(false);
    expect(container.querySelector<HTMLElement>('#desktop')?.hidden).toBe(true);
    expect(scrollIntoView).toHaveBeenCalled();
    expect(container.querySelector('a[lang="zh-CN"]')?.getAttribute('href')).toBe('/changelog/zh-CN#android-0.1.0');
  });

  it('follows hash changes and browser history, including the localized empty platform', async () => {
    await act(async () => root.render(createElement(ChangelogTimeline, { platforms, locale: 'zh-CN' })));
    await act(async () => {
      window.history.replaceState(null, '', '#ios');
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(container.querySelector<HTMLElement>('#ios')?.hidden).toBe(false);
    expect(container.querySelector('#ios')?.textContent).toContain('暂无已发布的更新日志');
    expect(container.querySelector('a[aria-current="true"]')?.getAttribute('href')).toBe('#ios');
    await act(async () => {
      window.history.replaceState(null, '', '/changelog');
      window.dispatchEvent(new HashChangeEvent('hashchange'));
    });
    expect(container.querySelector<HTMLElement>('#desktop')?.hidden).toBe(false);
  });
});
