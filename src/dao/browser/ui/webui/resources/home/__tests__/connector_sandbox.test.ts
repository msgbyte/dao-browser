// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {describe, expect, it, vi} from 'vitest';

import {
  ConnectorSandboxSession,
  PAGE_OPERATIONS,
} from '../connector_sandbox.js';

describe('Home connector sandbox', () => {
  it('exposes only the audited page facade operations', async () => {
    const callPage = vi.fn().mockResolvedValue({ok: true});
    const importer = vi.fn().mockResolvedValue({
      default: {
        collect: async (page: Record<string, (...args: unknown[]) => unknown>) => {
          expect(Object.keys(page)).toEqual(PAGE_OPERATIONS);
          expect((page as Record<string, unknown>)['window']).toBeUndefined();
          await page['queryAll']!('.item', {title: '.title'});
          return [{title: 'Safe data'}];
        },
      },
    });
    const session = new ConnectorSandboxSession(callPage, importer);

    await expect(session.run('export default {}', {})).resolves.toEqual([
      {title: 'Safe data'},
    ]);
    expect(callPage).toHaveBeenCalledWith(
        'queryAll', ['.item', {title: '.title'}]);
  });

  it('rejects modules without a collect function', async () => {
    const session = new ConnectorSandboxSession(
        vi.fn(), vi.fn().mockResolvedValue({default: {}}));

    await expect(session.run('export default {}', {}))
        .rejects.toThrow('collect');
  });

  it('revokes the module URL on both success and failure', async () => {
    const revoke = vi.fn();
    const create = vi.fn().mockReturnValue('blob:connector');
    vi.stubGlobal('URL', {createObjectURL: create, revokeObjectURL: revoke});
    vi.stubGlobal('Blob', class {});
    const importer = vi.fn().mockResolvedValue({
      default: {collect: () => ({ok: true})},
    });
    const session = new ConnectorSandboxSession(vi.fn(), importer);

    await session.run('module', {});
    expect(importer).toHaveBeenCalledWith('blob:connector');
    expect(revoke).toHaveBeenCalledWith('blob:connector');
  });
});
