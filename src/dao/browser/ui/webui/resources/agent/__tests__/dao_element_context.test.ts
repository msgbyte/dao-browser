// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it} from 'vitest';

import {
  addReusableElementContext,
  clearReusableElementContext,
  consumeReusableElementContexts,
  getReusableElementContexts,
  getReusableElementContext,
  makeResolveElementContextScript,
  removeReusableElementContext,
  REUSABLE_ELEMENT_CONTEXT_STORAGE_KEY,
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

describe('dao_element_context store', () => {
  beforeEach(() => {
    clearReusableElementContext();
    localStorage.clear();
  });

  it('stores and clears the reusable element context', () => {
    const context = sampleContext();

    setReusableElementContext(context);
    expect(getReusableElementContext()).toMatchObject(context);
    expect(getReusableElementContext()?.contextId).toBeTruthy();

    clearReusableElementContext();
    expect(getReusableElementContext()).toBeNull();
  });

  it('adds, persists, and removes multiple reusable element contexts', () => {
    const first = addReusableElementContext(sampleContext());
    const second = addReusableElementContext(
        sampleContext('Forgot password link', 'forgot-password', 'Forgot password'));

    expect(first.contextId).toBeTruthy();
    expect(second.contextId).toBeTruthy();
    expect(first.contextId).not.toBe(second.contextId);
    expect(getReusableElementContexts().map(item => item.label))
        .toEqual(['Sign in button', 'Forgot password link']);

    const stored = JSON.parse(
        localStorage.getItem(REUSABLE_ELEMENT_CONTEXT_STORAGE_KEY) || '[]');
    expect(stored).toHaveLength(2);

    removeReusableElementContext(first.contextId);
    expect(getReusableElementContexts().map(item => item.label))
        .toEqual(['Forgot password link']);
  });

  it('consumes reusable element contexts as a one-shot attachment payload', () => {
    const first = addReusableElementContext(sampleContext());
    const second = addReusableElementContext(
        sampleContext('Forgot password link', 'forgot-password', 'Forgot password'));

    expect(consumeReusableElementContexts().map(item => item.contextId))
        .toEqual([first.contextId, second.contextId]);
    expect(getReusableElementContexts()).toEqual([]);
    expect(localStorage.getItem(REUSABLE_ELEMENT_CONTEXT_STORAGE_KEY))
        .toBeNull();
  });

  it('restores multiple reusable element contexts from local storage', () => {
    const contexts = [
      {...sampleContext(), contextId: 'ctx_sign_in'},
      {
        ...sampleContext('Forgot password link', 'forgot-password', 'Forgot password'),
        contextId: 'ctx_forgot',
      },
    ];

    localStorage.setItem(
        REUSABLE_ELEMENT_CONTEXT_STORAGE_KEY, JSON.stringify(contexts));

    expect(getReusableElementContexts().map(item => item.contextId))
        .toEqual(['ctx_sign_in', 'ctx_forgot']);
  });

  it('restores the reusable element context from local storage', () => {
    const context = sampleContext();

    localStorage.setItem(
        REUSABLE_ELEMENT_CONTEXT_STORAGE_KEY, JSON.stringify(context));

    expect(getReusableElementContext()).toMatchObject(context);
    expect(getReusableElementContext()?.contextId).toBeTruthy();
  });
});

describe('dao_element_context resolver script', () => {
  it('resolves a selected element by stable data attributes first', () => {
    document.body.innerHTML = `
      <form>
        <label>Email <input name="email"></label>
        <label>Password <input name="password"></label>
        <button type="submit" data-testid="login-submit">Sign in</button>
      </form>
    `;

    const script = makeResolveElementContextScript(sampleContext().locator);
    const result = JSON.parse(window.eval(script));

    expect(result).toMatchObject({
      resolved: true,
      selector: '[data-dao-element-context="current"]',
      matchedBy: expect.arrayContaining(['data-testid']),
    });
    expect(result.score).toBeGreaterThanOrEqual(90);
    expect(document.querySelector('[data-dao-element-context="current"]')?.textContent)
        .toBe('Sign in');
  });
});
