// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

import {executeTool} from '../agent_bridge.js';
import {
  addReusableElementContext,
  clearReusableElementContext,
  setReusableElementContext,
  type ElementContextCapture,
} from '../dao_element_context.js';

function sampleContext(
    label = 'Sign in button',
    testId = 'login-submit',
    text = 'Sign in'): ElementContextCapture {
  return {
    url: 'https://example.com/login',
    title: 'Login',
    label,
    text,
    locator: {
      role: 'button',
      name: text,
      tag: 'button',
      text,
      attributes: {
        type: 'submit',
        'data-testid': testId,
      },
      css: `[data-testid="${testId}"]`,
      fallbackPath: 'body > form > button:nth-of-type(1)',
      nearText: ['Email', 'Password'],
      bounds: {x: 12, y: 34, width: 80, height: 32},
    },
  };
}

describe('agent bridge element context tool', () => {
  beforeEach(() => {
    clearReusableElementContext();
    vi.unstubAllGlobals();
  });

  it('returns an error when no reusable element context is selected', async () => {
    await expect(executeTool('resolve_element_context', {}))
        .resolves.toEqual({error: 'No reusable element context selected.'});
  });

  it('resolves the selected reusable element context on the active page', async () => {
    setReusableElementContext(sampleContext());
    const send = vi.fn((method: string, args: unknown[]) => {
      const [id, params] = args as [string, {code: string; lockTab: boolean}];
      expect(method).toBe('executeScript');
      expect(params.code).toContain('login-submit');
      expect(params.lockTab).toBe(false);
      (window as unknown as {
        cr: {webUIResponse: (id: string, ok: boolean, value: unknown) => void};
      }).cr.webUIResponse(id, true, {
        result: JSON.stringify({
          resolved: true,
          selector: '[data-dao-element-context="current"]',
          score: 95,
          matchedBy: ['data-testid'],
        }),
      });
    });
    vi.stubGlobal('chrome', {send});

    await expect(executeTool('resolve_element_context', {}))
        .resolves.toMatchObject({
          resolved: true,
          selector: '[data-dao-element-context="current"]',
          matchedBy: ['data-testid'],
    });
    expect(send).toHaveBeenCalledTimes(1);
  });

  it('asks for a specific reusable element when multiple are selected', async () => {
    const first = addReusableElementContext(sampleContext());
    const second = addReusableElementContext(
        sampleContext('Forgot password link', 'forgot-password', 'Forgot password'));

    await expect(executeTool('resolve_element_context', {}))
        .resolves.toMatchObject({
          error: 'Multiple reusable element contexts selected. Provide context_id or index.',
          contexts: [
            {context_id: first.contextId, label: 'Sign in button', index: 0},
            {context_id: second.contextId, label: 'Forgot password link', index: 1},
          ],
        });
  });

  it('resolves one reusable element context by context_id', async () => {
    addReusableElementContext(sampleContext());
    const second = addReusableElementContext(
        sampleContext('Forgot password link', 'forgot-password', 'Forgot password'));
    const send = vi.fn((method: string, args: unknown[]) => {
      const [id, params] = args as [string, {code: string; lockTab: boolean}];
      expect(method).toBe('executeScript');
      expect(params.code).toContain('forgot-password');
      expect(params.code).not.toContain('login-submit');
      expect(params.lockTab).toBe(false);
      (window as unknown as {
        cr: {webUIResponse: (id: string, ok: boolean, value: unknown) => void};
      }).cr.webUIResponse(id, true, {
        result: JSON.stringify({
          resolved: true,
          selector: '[data-dao-element-context="current"]',
          score: 95,
          matchedBy: ['data-testid'],
        }),
      });
    });
    vi.stubGlobal('chrome', {send});

    await expect(executeTool(
                     'resolve_element_context', {context_id: second.contextId}))
        .resolves.toMatchObject({
          resolved: true,
          context_id: second.contextId,
          label: 'Forgot password link',
          selector: '[data-dao-element-context="current"]',
        });
  });
});
