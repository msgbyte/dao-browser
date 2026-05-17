// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Pure-TS dispatcher mapping the four workspace tool names exposed to the
// LLM onto the corresponding chrome.send method names registered by
// DaoAgentWorkspaceHandler. The dispatcher also normalizes the raw C++
// reply (which uses a {ok, code} error shape and snake_case fields) into
// the WorkspaceReply union declared in ./types.ts.

import {callNative} from '../agent_bridge.js';
import type {
  WorkspaceErrorCode,
  WorkspaceErrorReply,
  WorkspaceReply,
} from './types.js';

const C_CODE_TO_TS: Record<string, WorkspaceErrorCode> = {
  invalid_path: 'workspace_path_invalid',
  not_found: 'workspace_not_found',
  already_exists: 'workspace_internal_error',
  quota_exceeded: 'workspace_quota_exceeded',
  binary_rejected: 'workspace_binary_rejected',
  patch_parse_error: 'workspace_patch_parse_error',
  patch_context_mismatch: 'workspace_patch_apply_failed',
  edit_not_unique: 'workspace_edit_not_unique',
  io_error: 'workspace_internal_error',
};

const HUMAN_MESSAGES: Record<WorkspaceErrorCode, string> = {
  workspace_path_invalid: 'Path is invalid or outside the workspace.',
  workspace_not_found: 'No such workspace file.',
  workspace_binary_rejected:
      'Refusing to read or write binary content (workspace is text-only).',
  workspace_quota_exceeded:
      'Workspace quota exceeded; delete files or reduce write size.',
  workspace_edit_not_unique:
      'old_text matches multiple locations; widen the context.',
  workspace_patch_parse_error: 'Patch could not be parsed as V4A.',
  workspace_patch_apply_failed:
      'Patch parsed but a hunk did not apply cleanly.',
  workspace_internal_error: 'Internal workspace error.',
};

function internalError(message: string): WorkspaceErrorReply {
  return {ok: false, error: 'workspace_internal_error', message};
}

function pathInvalid(message: string): WorkspaceErrorReply {
  return {ok: false, error: 'workspace_path_invalid', message};
}

function getStr(args: Record<string, unknown>, key: string): string|null {
  const v = args[key];
  return typeof v === 'string' && v.length > 0 ? v : null;
}

function getInt(args: Record<string, unknown>, key: string,
                fallback: number): number {
  const v = args[key];
  return typeof v === 'number' && Number.isFinite(v) ? Math.trunc(v) : fallback;
}

function normalizeReply(
    path: string, raw: unknown): WorkspaceReply {
  if (!raw || typeof raw !== 'object') {
    return internalError('Empty reply from workspace handler');
  }
  const r = raw as Record<string, unknown>;
  if (r['ok'] === false) {
    const code = typeof r['code'] === 'string' ? r['code'] : 'io_error';
    const tsCode = C_CODE_TO_TS[code] ?? 'workspace_internal_error';
    return {
      ok: false,
      error: tsCode,
      message: HUMAN_MESSAGES[tsCode],
    };
  }
  // Success: attach `path` (C++ doesn't echo it) and pass through.
  return {path, ...r} as WorkspaceReply;
}

export async function executeWorkspaceTool(
    name: string, args: Record<string, unknown>): Promise<WorkspaceReply> {
  try {
    switch (name) {
      case 'workspace_read': {
        const path = getStr(args, 'path');
        if (!path) return pathInvalid('workspace_read requires "path"');
        const offset = getInt(args, 'offset', 0);
        const limit = getInt(args, 'limit', 500);
        const raw =
            await callNative('workspaceRead', {path, offset, limit});
        return normalizeReply(path, raw);
      }
      case 'workspace_write': {
        const path = getStr(args, 'path');
        if (!path) return pathInvalid('workspace_write requires "path"');
        if (typeof args['content'] !== 'string') {
          return pathInvalid('workspace_write requires string "content"');
        }
        const raw = await callNative(
            'workspaceWrite', {path, content: args['content']});
        return normalizeReply(path, raw);
      }
      case 'workspace_edit': {
        const path = getStr(args, 'path');
        if (!path) return pathInvalid('workspace_edit requires "path"');
        const oldText = args['old_text'];
        const newText = args['new_text'];
        if (typeof oldText !== 'string' || typeof newText !== 'string') {
          return pathInvalid(
              'workspace_edit requires string "old_text" and "new_text"');
        }
        const raw = await callNative(
            'workspaceEdit',
            {path, old_str: oldText, new_str: newText});
        return normalizeReply(path, raw);
      }
      case 'apply_patch': {
        const patch = getStr(args, 'patch');
        if (!patch) {
          return pathInvalid('apply_patch requires non-empty "patch"');
        }
        const raw = await callNative('workspaceApplyPatch', {patch});
        return normalizeReply('', raw);
      }
      case 'download': {
        const path = getStr(args, 'path');
        if (!path) return pathInvalid('download requires "path"');
        const url = getStr(args, 'url');
        const sourceArg = getStr(args, 'source');
        const source = sourceArg ?? (url ? 'url' : 'page');
        if (source === 'url' && !url) {
          return pathInvalid('download requires "url" when source="url"');
        }
        const payload: Record<string, unknown> = {path, source};
        if (url) payload['url'] = url;
        const headers = args['headers'];
        if (headers && typeof headers === 'object') {
          payload['headers'] = headers;
        }
        const raw = await callNative('workspaceDownload', payload);
        return normalizeReply(path, raw);
      }
      default:
        return internalError(`Unknown workspace tool: ${name}`);
    }
  } catch (e) {
    return internalError(e instanceof Error ? e.message : String(e));
  }
}

export function formatWorkspaceReply(reply: WorkspaceReply): string {
  if (reply.ok === false) {
    const hint = reply.hint ? ` (${reply.hint})` : '';
    return `error: ${reply.error} — ${reply.message}${hint}`;
  }
  if ('content' in reply) {
    const slice =
        `lines ${0}..${reply.returned_lines} of ${reply.total_lines}`;
    const tail = reply.truncated ? ' (truncated)' : '';
    return `[${reply.path} ${slice}${tail}]\n${reply.content}`;
  }
  if ('added' in reply) {
    const parts: string[] = [];
    if (reply.added.length) parts.push(`added: ${reply.added.join(', ')}`);
    if (reply.updated.length) {
      parts.push(`updated: ${reply.updated.join(', ')}`);
    }
    if (reply.deleted.length) {
      parts.push(`deleted: ${reply.deleted.join(', ')}`);
    }
    if (reply.moved.length) {
      parts.push(
          `moved: ${reply.moved.map(m => `${m.from}->${m.to}`).join(', ')}`);
    }
    return `applied patch: ${parts.join('; ') || '(no-op)'}`;
  }
  if ('source_url' in reply) {
    const verb = reply.created ? 'downloaded' : 'overwrote';
    const tail = reply.truncated ? ' (source truncated)' : '';
    const src = reply.source_url ? ` from ${reply.source_url}` : '';
    return `${verb} ${reply.path} (${reply.bytes_written} bytes)${src}${tail}`;
  }
  if ('created' in reply) {
    const verb = reply.created ? 'created' : 'wrote';
    return `${verb} ${reply.path} (${reply.bytes_written} bytes)`;
  }
  // edit
  return `edited ${reply.path} (${reply.bytes_written} bytes)`;
}
