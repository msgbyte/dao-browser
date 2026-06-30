// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeAll, describe, expect, it, vi} from 'vitest';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

function getStyles(): string {
  const ctor = customElements.get('dao-download-button') as unknown as {
    styles: {strings: string[]};
  };
  return ctor.styles.strings.join('');
}

describe('dao-download-button', () => {
  beforeAll(async () => {
    await import('../dao_download_button.js');
  });

  it('lets the recent downloads popup sit flush with sidebar edges', () => {
    const styles = getStyles();

    expect(styles).toMatch(/\.popup-stack\s*{[^}]*left:\s*-6px;/s);
    expect(styles).toMatch(/\.popup-stack\s*{[^}]*width:\s*100vw;/s);
    expect(styles).toMatch(/\.popup-stack\s*{[^}]*padding:\s*0 0 6px;/s);
    expect(styles).toMatch(/\.popup-stack\s*{[^}]*box-sizing:\s*border-box;/s);
    expect(styles).not.toMatch(/\.popup-stack\s*{[^}]*padding:\s*0 6px 6px;/s);
  });
});
