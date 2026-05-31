// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const callNativeMock = vi.hoisted(() => vi.fn());

vi.mock('../agent_bridge.js', () => ({
  callNative: (...args: unknown[]) => callNativeMock(...args),
}));

import {
  buildPageAttachment,
  buildSelectionAttachment,
  captureCurrentPageMarkdown,
  fetchCurrentSelection,
  fetchPageProbeState,
  isCapturablePageUrl,
} from '../dao_page_capture.js';

describe('dao_page_capture attachments', () => {
  beforeEach(() => {
    callNativeMock.mockReset();
    vi.spyOn(Date, 'now').mockReturnValue(1234);
    vi.spyOn(Math, 'random').mockReturnValue(0.25);
  });

  it('builds page attachments with safe filenames and escaped metadata', () => {
    const attachment = buildPageAttachment({
      url: 'https://example.com/article?x="quoted"',
      title: 'A / dangerous\t"title"',
      markdown: '# Hello\nWorld',
    });

    expect(attachment.fileName).toBe('A dangerous "title".md');
    expect(attachment.mimeType).toBe('text/markdown');
    expect(attachment.size).toBe(new TextEncoder().encode('# Hello\nWorld').length);
    expect(atob(attachment.content)).toBe('# Hello\nWorld');
    expect(attachment.extractedText).toContain('url="https://example.com/article?x=&quot;quoted&quot;"');
    expect(attachment.extractedText).toContain('title="A / dangerous\t&quot;title&quot;"');
    expect(attachment.daoPageUrl).toBe('https://example.com/article?x="quoted"');
  });

  it('builds selection attachments from the first selected line', () => {
    const attachment = buildSelectionAttachment({
      url: 'https://example.com',
      title: 'Example',
      text: 'first / line\nsecond line',
    });

    expect(attachment.fileName).toBe('first line.txt');
    expect(attachment.mimeType).toBe('text/plain');
    expect(atob(attachment.content)).toBe('first / line\nsecond line');
    expect(attachment.extractedText).toContain('<selected-text');
  });

  it('rejects browser-internal and empty URLs for page capture', () => {
    expect(isCapturablePageUrl('https://example.com')).toBe(true);
    expect(isCapturablePageUrl('http://localhost:3000')).toBe(true);
    expect(isCapturablePageUrl('')).toBe(false);
    expect(isCapturablePageUrl('about:blank')).toBe(false);
    expect(isCapturablePageUrl('chrome://settings')).toBe(false);
    expect(isCapturablePageUrl('chrome-extension://abc/page.html')).toBe(false);
    expect(isCapturablePageUrl('devtools://devtools/bundled')).toBe(false);
    expect(isCapturablePageUrl('view-source:https://example.com')).toBe(false);
    expect(isCapturablePageUrl('data:text/plain,hello')).toBe(false);
  });
});

describe('dao_page_capture native probes', () => {
  beforeEach(() => {
    callNativeMock.mockReset();
  });

  it('trims non-empty text selections and ignores empty selections', async () => {
    callNativeMock.mockResolvedValueOnce({
      result: JSON.stringify({
        url: 'https://example.com',
        title: 'Example',
        text: '  selected text  ',
      }),
    });
    await expect(fetchCurrentSelection()).resolves.toEqual({
      url: 'https://example.com',
      title: 'Example',
      text: 'selected text',
    });

    callNativeMock.mockResolvedValueOnce({
      result: JSON.stringify({
        url: 'https://example.com',
        title: 'Example',
        text: '   ',
      }),
    });
    await expect(fetchCurrentSelection()).resolves.toBeNull();
  });

  it('returns a combined probe state with selection and focused input status', async () => {
    callNativeMock.mockResolvedValue({
      result: JSON.stringify({
        url: 'https://example.com',
        title: 'Example',
        text: ' selection ',
        hasFocusedInput: true,
      }),
    });

    await expect(fetchPageProbeState()).resolves.toEqual({
      selection: {
        url: 'https://example.com',
        title: 'Example',
        text: 'selection',
      },
      hasFocusedInput: true,
    });
  });

  it('prefers native PDF extraction before falling back to page script capture', async () => {
    callNativeMock.mockResolvedValueOnce({
      isPdf: true,
      url: 'https://example.com/file.pdf',
      title: 'Spec',
      pageCount: 3,
      text: 'PDF text',
    });

    await expect(captureCurrentPageMarkdown()).resolves.toEqual({
      url: 'https://example.com/file.pdf',
      title: 'Spec',
      markdown: '# Spec (PDF, 3 pages)\n\nPDF text',
      fallback: false,
    });
    expect(callNativeMock).toHaveBeenCalledTimes(1);

    callNativeMock.mockReset();
    callNativeMock
        .mockResolvedValueOnce({isPdf: false})
        .mockResolvedValueOnce({
          result: JSON.stringify({
            url: 'https://example.com/html',
            title: 'HTML',
            markdown: 'Article',
            fallback: true,
          }),
        });

    await expect(captureCurrentPageMarkdown()).resolves.toEqual({
      url: 'https://example.com/html',
      title: 'HTML',
      markdown: 'Article',
      fallback: true,
    });
    expect(callNativeMock).toHaveBeenLastCalledWith(
        'executeScript',
        expect.objectContaining({lockTab: false}));
  });
});
