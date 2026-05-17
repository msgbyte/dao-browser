// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {describe, it, expect, beforeEach, vi} from 'vitest';

const callNativeMock = vi.fn();
vi.mock('../agent_bridge.js', () => ({
  callNative: (...args: unknown[]) => callNativeMock(...args),
}));

import {executeWorkspaceTool, formatWorkspaceReply}
    from '../workspace/bridge.js';

describe('workspace dispatcher', () => {
  beforeEach(() => {
    callNativeMock.mockReset();
  });

  it('routes workspace_read to workspaceRead with default offset/limit',
     async () => {
    callNativeMock.mockResolvedValue({
      ok: true, content: 'hi\n', total_lines: 1, returned_lines: 1,
      truncated: false,
    });
    const reply = await executeWorkspaceTool('workspace_read', {
      path: 'a.md',
    });
    expect(callNativeMock).toHaveBeenCalledWith(
        'workspaceRead', {path: 'a.md', offset: 0, limit: 500});
    expect(reply).toMatchObject({
      ok: true, path: 'a.md', content: 'hi\n',
      total_lines: 1, returned_lines: 1, truncated: false,
    });
  });

  it('passes through explicit offset/limit', async () => {
    callNativeMock.mockResolvedValue({
      ok: true, content: '', total_lines: 0, returned_lines: 0,
      truncated: false,
    });
    await executeWorkspaceTool(
        'workspace_read', {path: 'a.md', offset: 4, limit: 10});
    expect(callNativeMock).toHaveBeenCalledWith(
        'workspaceRead', {path: 'a.md', offset: 4, limit: 10});
  });

  it('routes workspace_write and forwards the body', async () => {
    callNativeMock.mockResolvedValue({
      ok: true, bytes_written: 5, created: true,
    });
    await executeWorkspaceTool(
        'workspace_write', {path: 'a.md', content: 'hello'});
    expect(callNativeMock).toHaveBeenCalledWith(
        'workspaceWrite', {path: 'a.md', content: 'hello'});
  });

  it('routes workspace_edit translating old_text/new_text to old_str/new_str',
     async () => {
    callNativeMock.mockResolvedValue({ok: true, bytes_written: 4});
    await executeWorkspaceTool('workspace_edit', {
      path: 'a.md', old_text: 'foo', new_text: 'bar',
    });
    expect(callNativeMock).toHaveBeenCalledWith(
        'workspaceEdit',
        {path: 'a.md', old_str: 'foo', new_str: 'bar'});
  });

  it('routes apply_patch and forwards the raw patch string', async () => {
    callNativeMock.mockResolvedValue({
      ok: true, added: [], updated: [], deleted: [], moved: [],
    });
    await executeWorkspaceTool('apply_patch', {
      patch: '*** Begin Patch\n*** End Patch\n',
    });
    expect(callNativeMock).toHaveBeenCalledWith(
        'workspaceApplyPatch',
        {patch: '*** Begin Patch\n*** End Patch\n'});
  });

  it('rejects an unknown tool name without calling C++', async () => {
    const reply = await executeWorkspaceTool('workspace_bogus', {});
    expect(callNativeMock).not.toHaveBeenCalled();
    expect(reply).toMatchObject({
      ok: false, error: 'workspace_internal_error',
    });
  });

  it('rejects missing required args without calling C++', async () => {
    const reply =
        await executeWorkspaceTool('workspace_write', {path: 'a.md'});
    expect(callNativeMock).not.toHaveBeenCalled();
    expect(reply).toMatchObject({
      ok: false, error: 'workspace_path_invalid',
    });
  });

  it('normalizes C++ error replies (code -> error)', async () => {
    callNativeMock.mockResolvedValue({
      ok: false, code: 'quota_exceeded',
    });
    const reply =
        await executeWorkspaceTool(
            'workspace_write', {path: 'a.md', content: 'x'});
    expect(reply).toMatchObject({
      ok: false, error: 'workspace_quota_exceeded',
    });
  });

  it('maps edit_not_unique correctly', async () => {
    callNativeMock.mockResolvedValue({ok: false, code: 'edit_not_unique'});
    const reply = await executeWorkspaceTool(
        'workspace_edit', {path: 'a.md', old_text: 'x', new_text: 'y'});
    expect(reply).toMatchObject({
      ok: false, error: 'workspace_edit_not_unique',
    });
  });

  it('wraps callNative rejections into workspace_internal_error', async () => {
    callNativeMock.mockRejectedValue(new Error('Timeout calling workspaceRead'));
    const reply =
        await executeWorkspaceTool('workspace_read', {path: 'a.md'});
    expect(reply).toMatchObject({
      ok: false, error: 'workspace_internal_error',
    });
    expect((reply as {message: string}).message).toMatch(/Timeout/);
  });
});

describe('formatWorkspaceReply', () => {
  it('returns content with path and line range for read', () => {
    const out = formatWorkspaceReply({
      ok: true, path: 'a.md', content: 'hello\n',
      total_lines: 1, returned_lines: 1, truncated: false,
    });
    expect(out).toContain('hello');
    expect(out).toContain('a.md');
    expect(out).toContain('lines 0..1 of 1');
  });

  it('returns a one-line summary for write success', () => {
    const out = formatWorkspaceReply({
      ok: true, path: 'a.md', bytes_written: 5, created: true,
    });
    expect(out).toMatch(/created.*a\.md.*5 bytes/i);
  });

  it('returns a one-line summary for edit success', () => {
    const out = formatWorkspaceReply({
      ok: true, path: 'a.md', bytes_written: 4,
    });
    expect(out).toMatch(/edited.*a\.md/);
  });

  it('formats apply_patch summary', () => {
    const out = formatWorkspaceReply({
      ok: true, added: ['x.md'], updated: ['y.md'], deleted: [], moved: [],
    });
    expect(out).toContain('added: x.md');
    expect(out).toContain('updated: y.md');
  });

  it('formats error replies as "error: <code> — <message>"', () => {
    const out = formatWorkspaceReply({
      ok: false, error: 'workspace_not_found', message: 'No such file: a.md',
    });
    expect(out).toContain('workspace_not_found');
    expect(out).toContain('No such file: a.md');
  });
});
