// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeAll, beforeEach, describe, expect, it, vi} from 'vitest';

import type {UpdateStateData} from '../sidebar_bridge.js';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

interface TestUpdateButton extends HTMLElement {
  updateState: UpdateStateData | null;
  updateComplete: Promise<boolean>;
}

function updateState(extra: Partial<UpdateStateData> = {}): UpdateStateData {
  return {
    state: 'ready',
    displayVersion: '1.2.3',
    label: 'Update',
    applyingLabel: 'Applying',
    ...extra,
  };
}

async function loadButton() {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  const el = document.createElement('dao-update-button') as TestUpdateButton;
  document.body.appendChild(el);
  return {el, send};
}

describe('dao-update-button', () => {
  beforeAll(async () => {
    await import('../dao_update_button.js');
  });

  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
  });

  it('does not render a button while idle', async () => {
    const {el} = await loadButton();
    el.updateState = updateState({state: 'idle'});
    await el.updateComplete;

    expect(el.shadowRoot!.querySelector('button')).toBeNull();
  });

  it('hides the host while idle', async () => {
    const {el} = await loadButton();
    el.updateState = updateState({state: 'idle'});
    await el.updateComplete;

    const ctor = customElements.get('dao-update-button') as
        typeof HTMLElement & {styles: {strings: string[]}};
    const cssText = ctor.styles.strings.join('');
    expect(el.hidden).toBe(true);
    expect(cssText).toContain(':host([hidden])');
    expect(cssText).toContain('display: none;');
  });

  it('hides the host before native update state arrives', async () => {
    const {el} = await loadButton();
    el.updateState = null;
    await el.updateComplete;

    expect(el.hidden).toBe(true);
    expect(el.shadowRoot!.querySelector('button')).toBeNull();
  });

  it('keeps toolbar spacing out of the host', async () => {
    await loadButton();
    const ctor = customElements.get('dao-update-button') as
        typeof HTMLElement & {styles: {strings: string[]}};
    const cssText = ctor.styles.strings.join('');

    expect(cssText).toContain('align-items: center;');
    expect(cssText).not.toContain('padding: 0 6px 6px;');
  });

  it('centers the expanded label instead of left-aligning it', async () => {
    await loadButton();
    const ctor = customElements.get('dao-update-button') as
        typeof HTMLElement & {styles: {strings: string[]}};
    const cssText = ctor.styles.strings.join('');

    expect(cssText).toContain('position: absolute;');
    expect(cssText).toContain('inset: 0;');
    expect(cssText).toContain('text-align: center;');
  });

  it('uses a lighter blue chip in light mode', async () => {
    await loadButton();
    const ctor = customElements.get('dao-update-button') as
        typeof HTMLElement & {styles: {strings: string[]}};
    const cssText = ctor.styles.strings.join('');

    expect(cssText).toContain('background: rgb(218, 232, 249);');
    expect(cssText).toContain('color: rgb(46, 84, 132);');
    expect(cssText).not.toContain('background: rgb(70, 120, 190);');
  });

  it('renders the ready update button with its label and icon', async () => {
    const {el} = await loadButton();
    el.updateState = updateState({label: 'Update'});
    await el.updateComplete;

    const button = el.shadowRoot!.querySelector('button')!;
    expect(button.title).toContain('Update');
    expect(button.textContent).toContain('Update');
    expect(el.shadowRoot!.querySelector(
        'circle[cx="12"][cy="12"][r="10"]')).not.toBeNull();
  });

  it('sends applyReadyUpdate and disables itself on click', async () => {
    const {el, send} = await loadButton();
    el.updateState = updateState();
    await el.updateComplete;

    const button = el.shadowRoot!.querySelector('button')!;
    button.click();
    await el.updateComplete;

    expect(send).toHaveBeenCalledWith('applyReadyUpdate', []);
    expect(el.shadowRoot!.querySelector('button')!.disabled).toBe(true);
  });

  it('stays expanded after click until the pointer leaves', async () => {
    const {el} = await loadButton();
    el.updateState = updateState();
    await el.updateComplete;

    el.shadowRoot!.querySelector('button')!.click();
    await el.updateComplete;

    el.updateState = updateState({displayVersion: '1.2.4'});
    await el.updateComplete;

    let button = el.shadowRoot!.querySelector('button')!;
    expect(button.disabled).toBe(false);
    expect(button.classList.contains('expanded')).toBe(true);

    button.dispatchEvent(new Event('mouseleave'));
    await el.updateComplete;

    button = el.shadowRoot!.querySelector('button')!;
    expect(button.classList.contains('expanded')).toBe(false);
  });

  it('reenables when native sends a fresh ready state after click', async () => {
    const {el} = await loadButton();
    el.updateState = updateState({label: 'Update'});
    await el.updateComplete;

    el.shadowRoot!.querySelector('button')!.click();
    await el.updateComplete;
    expect(el.shadowRoot!.querySelector('button')!.disabled).toBe(true);

    el.updateState = updateState({
      displayVersion: '1.2.4',
      label: 'Update',
      applyingLabel: 'Applying',
      state: 'ready',
    });
    await el.updateComplete;

    const button = el.shadowRoot!.querySelector('button')!;
    expect(button.disabled).toBe(false);
    expect(button.title).toContain('Update');
    expect(button.textContent).toContain('Update');
    expect(button.textContent).not.toContain('Applying');
  });
});
