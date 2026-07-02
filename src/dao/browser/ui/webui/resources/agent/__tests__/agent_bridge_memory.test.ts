// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

import {cr, executeTool, tools} from '../agent_bridge.js';

function installChromeMock() {
  const send = vi.fn((method: string, args: unknown[]) => {
    const [id, params] = args as [string, Record<string, unknown>];
    if (method === 'getPageInfo') {
      cr.webUIResponse(id, true, {
        url: 'https://github.com/dao/dao-browser/pull/42',
        title: 'Improve proactive suggestions',
      });
      return;
    }
    if (method === 'saveEpisode') {
      cr.webUIResponse(id, true, {success: true, params});
      return;
    }
    cr.webUIResponse(id, false, {error: 'Unexpected native call: ' + method});
  });
  vi.stubGlobal('chrome', {send});
  return send;
}

describe('agent bridge save_memory tool', () => {
  beforeEach(() => {
    vi.unstubAllGlobals();
  });

  it('requires an explicit reusable signal in the tool schema', () => {
    const tool = tools.find(t => t.function.name === 'save_memory');

    expect(tool?.function.parameters.required).toContain('reusable');
  });

  it('skips saving one-off memories when reusable is not true', async () => {
    const send = installChromeMock();

    await expect(executeTool('save_memory', {
      intent: 'Summarize this PR',
      outcome: 'Summarized the pull request',
    })).resolves.toMatchObject({
      success: true,
      saved: false,
      reason: 'not_reusable',
    });

    expect(send).not.toHaveBeenCalled();
  });

  it('saves reusable successful work as a helpful episode', async () => {
    const send = installChromeMock();

    await expect(executeTool('save_memory', {
      intent: 'Review pull requests on GitHub',
      outcome: 'Reviewed the pull request and explained risky changes',
      reusable: true,
    })).resolves.toMatchObject({
      success: true,
      saved: true,
    });

    expect(send).toHaveBeenCalledWith('getPageInfo', expect.any(Array));
    expect(send).toHaveBeenCalledWith(
        'saveEpisode',
        expect.arrayContaining([
          expect.any(String),
          expect.objectContaining({
            domain: 'github.com',
            pathTemplate: '/dao/dao-browser/pull/42',
            actionResult: 'helpful',
            confidence: 0.8,
          }),
        ]));
  });
});
