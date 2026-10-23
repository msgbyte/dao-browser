// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, expect, it} from 'vitest';

import {CrLitElement, html} from './lit_test_shim.js';

class BooleanAttributeFixture extends CrLitElement {
  disabled = true;

  override render() {
    return html`<button ?disabled=${this.disabled}>Action</button>`;
  }
}

customElements.define('boolean-attribute-fixture', BooleanAttributeFixture);

afterEach(() => {
  document.body.innerHTML = '';
});

it('adds and removes boolean attributes when their values change', async () => {
  const el = document.createElement('boolean-attribute-fixture') as
      BooleanAttributeFixture;
  document.body.appendChild(el);
  await el.updateComplete;

  expect(el.shadowRoot!.querySelector('button')!.disabled).toBe(true);

  el.disabled = false;
  el.requestUpdate();
  await el.updateComplete;

  expect(el.shadowRoot!.querySelector('button')!.disabled).toBe(false);
  expect(el.shadowRoot!.querySelector('button')!.hasAttribute('disabled'))
      .toBe(false);
});
