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
  cancelElementPicker,
  captureCurrentPageMarkdown,
  fetchCurrentSelection,
  fetchPageProbeState,
  isCapturablePageUrl,
  startElementPicker,
} from '../dao_page_capture.js';
import {buildElementContextAttachment} from '../dao_element_context.js';

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

  it('builds element context attachments with locator metadata', () => {
    const attachment = buildElementContextAttachment({
      url: 'https://example.com/login',
      title: 'Login',
      contextId: 'ctx_sign_in',
      label: 'Sign in button',
      text: 'Sign in',
      locator: {
        role: 'button',
        name: 'Sign in',
        tag: 'button',
        text: 'Sign in',
        attributes: {
          type: 'submit',
          'data-testid': 'login-submit',
        },
        css: '[data-testid="login-submit"]',
        fallbackPath: 'body > form > button:nth-of-type(1)',
        nearText: ['Email', 'Password'],
        bounds: {x: 12, y: 34, width: 80, height: 32},
      },
    });

    expect(attachment.fileName).toBe('Sign in button.element.json');
    expect(attachment.mimeType).toBe('application/json');
    expect(attachment.daoPageUrl).toBe('https://example.com/login');
    expect(attachment.extractedText).toContain('<element-context');
    expect(attachment.extractedText).toContain('context_id="ctx_sign_in"');
    expect(attachment.extractedText).toContain('label="Sign in button"');
    expect(attachment.extractedText).toContain('role: button');
    expect(attachment.extractedText).toContain('name: Sign in');
    expect(attachment.extractedText).toContain('near: Email, Password');
    expect(attachment.extractedText).not.toContain('"data-testid"');
    expect(atob(attachment.content)).toContain('"role": "button"');
    expect(atob(attachment.content)).toContain('"contextId": "ctx_sign_in"');
    expect(atob(attachment.content)).toContain('"data-testid": "login-submit"');
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

  it('polls the injected element picker until a selected element is returned', async () => {
    vi.useFakeTimers();
    callNativeMock
        .mockResolvedValueOnce({result: JSON.stringify({started: true})})
        .mockResolvedValueOnce({result: JSON.stringify({status: 'pending'})})
        .mockResolvedValueOnce({
          result: JSON.stringify({
            status: 'selected',
            url: 'https://example.com/login',
            title: 'Login',
            label: 'Sign in',
            text: 'Sign in',
            locator: {
              role: 'button',
              name: 'Sign in',
              tag: 'button',
              text: 'Sign in',
              attributes: {type: 'submit'},
              css: 'button[type="submit"]',
              fallbackPath: 'body > form > button:nth-of-type(1)',
              nearText: ['Email'],
              bounds: {x: 1, y: 2, width: 3, height: 4},
            },
          }),
        });

    const promise = startElementPicker({pollIntervalMs: 25, timeoutMs: 200});
    await vi.advanceTimersByTimeAsync(25);
    await vi.advanceTimersByTimeAsync(25);

    await expect(promise).resolves.toEqual({
      url: 'https://example.com/login',
      title: 'Login',
      label: 'Sign in',
      text: 'Sign in',
      locator: {
        role: 'button',
        name: 'Sign in',
        tag: 'button',
        text: 'Sign in',
        attributes: {type: 'submit'},
        css: 'button[type="submit"]',
        fallbackPath: 'body > form > button:nth-of-type(1)',
        nearText: ['Email'],
        bounds: {x: 1, y: 2, width: 3, height: 4},
      },
    });
    expect(callNativeMock).toHaveBeenCalledWith(
        'executeScript',
        expect.objectContaining({lockTab: false}));
    vi.useRealTimers();
  });

  it('cancels the injected element picker without throwing', async () => {
    callNativeMock.mockResolvedValueOnce({result: JSON.stringify({ok: true})});

    await expect(cancelElementPicker()).resolves.toBeUndefined();
    expect(callNativeMock).toHaveBeenCalledWith(
        'executeScript',
        expect.objectContaining({lockTab: false}));
  });
});
