// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {describe, expect, it} from 'vitest';

import {daoIndexPages} from '../dao_index_pages.js';

describe('dao index page registry', () => {
  it('lists custom Dao WebUI pages with dao URLs', () => {
    const urls = daoIndexPages.map(page => page.url);

    expect(urls).toContain('dao://agent');
    expect(urls).toContain('dao://skills');
    expect(urls).toContain('dao://dream');
    expect(urls).toContain('dao://memory');
    expect(urls).toContain('dao://index');
    expect(urls.every(url => url.startsWith('dao://'))).toBe(true);
  });
});
