// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeAll, describe, expect, it, vi} from 'vitest';
import {marked} from '../vendor/pi_runtime_bundle.js';

const hookState = vi.hoisted(() => ({
  preprocess: undefined as undefined | ((markdown: string) => string),
}));

vi.mock('../vendor/pi_runtime_bundle.js', async importOriginal => {
  const actual = await importOriginal<
      typeof import('../vendor/pi_runtime_bundle.js')>();
  return {
    marked: {
      parse: actual.marked.parse.bind(actual.marked),
      use(ext: {hooks?: {preprocess?: (markdown: string) => string}}) {
        if (typeof ext?.hooks?.preprocess === 'function') {
          hookState.preprocess = ext.hooks.preprocess;
        }
        actual.marked.use(ext);
      },
    },
  };
});

vi.mock('../dao_agent_app.js', () => ({}));

beforeAll(async () => {
  await import('../agent.js');
});

function preprocess(markdown: string): string {
  expect(hookState.preprocess).toBeTypeOf('function');
  return hookState.preprocess!(markdown);
}

describe('agent markdown preprocess', () => {
  it('normalizes trailing spaces before emphasis closers', () => {
    const normalized = preprocess('**fafa **');
    expect(normalized).toBe('**fafa**');
    expect(marked.parse(normalized, {async: false}))
        .toContain('<strong>fafa</strong>');
  });

  it('normalizes spaces inside both emphasis delimiters', () => {
    const normalized = preprocess('** fafa **');
    expect(normalized).toBe('**fafa**');
    expect(marked.parse(normalized, {async: false}))
        .toContain('<strong>fafa</strong>');
  });
});
