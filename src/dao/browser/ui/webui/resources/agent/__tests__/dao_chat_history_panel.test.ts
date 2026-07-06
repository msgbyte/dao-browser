// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {describe, expect, it, vi} from 'vitest';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('../../sidebar/__tests__/lit_test_shim.js');
});

vi.mock('../pi_app_storage.js', () => ({
  ensurePiAppStorage: vi.fn(async () => ({
    sessions: {
      getAllMetadata: vi.fn(async () => []),
      deleteSession: vi.fn(async () => undefined),
      updateTitle: vi.fn(async () => undefined),
    },
  })),
}));

import {DaoChatHistoryPanel} from '../dao_chat_history_panel.js';

function templateText(value: unknown): string {
  if (value === null || value === undefined || value === false) {
    return '';
  }
  if (Array.isArray(value)) {
    return value.map(templateText).join('');
  }
  if (typeof value === 'object' && 'strings' in value && 'values' in value) {
    const result = value as {
      strings: ArrayLike<string>;
      values: unknown[];
    };
    let out = '';
    for (let i = 0; i < result.values.length; i++) {
      out += result.strings[i] ?? '';
      out += templateText(result.values[i]);
    }
    out += result.strings[result.strings.length - 1] ?? '';
    return out;
  }
  return String(value);
}

describe('dao-chat-history-panel layout', () => {
  it('constrains the session list to an internal scroll area', () => {
    const panel = new DaoChatHistoryPanel();
    panel.open = true;

    const text = templateText(panel.render());

    expect(text).toMatch(
        /\.dao-history-scrim\s*{[^}]*display:\s*flex;/s);
    expect(text).toMatch(
        /\.dao-history-scrim\s*{[^}]*min-height:\s*0;/s);
    expect(text).toMatch(
        /\.dao-history-panel\s*{[^}]*max-height:\s*calc\(100%\s*-\s*24px\);/s);
    expect(text).toMatch(
        /\.dao-history-list\s*{[^}]*min-height:\s*0;/s);
    expect(text).toMatch(
        /\.dao-history-list\s*{[^}]*overflow-y:\s*auto;/s);
  });
});
