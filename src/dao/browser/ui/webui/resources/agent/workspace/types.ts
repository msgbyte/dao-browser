// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Shared TypeScript shapes for the workspace tool family. These mirror
// the actual wire format used by DaoAgentWorkspaceHandler in C++.

export type WorkspaceErrorCode =
  | 'workspace_path_invalid'
  | 'workspace_not_found'
  | 'workspace_binary_rejected'
  | 'workspace_quota_exceeded'
  | 'workspace_edit_not_unique'
  | 'workspace_patch_parse_error'
  | 'workspace_patch_apply_failed'
  | 'workspace_internal_error';

export interface WorkspaceErrorReply {
  ok: false;
  error: WorkspaceErrorCode;
  message: string;
  hint?: string;
}

export interface WorkspaceReadReply {
  ok: true;
  path: string;
  content: string;
  total_lines: number;
  returned_lines: number;
  truncated: boolean;
}

export interface WorkspaceWriteReply {
  ok: true;
  path: string;
  bytes_written: number;
  created: boolean;
}

export interface WorkspaceEditReply {
  ok: true;
  path: string;
  bytes_written: number;
}

export interface WorkspaceApplyPatchReply {
  ok: true;
  added: string[];
  updated: string[];
  deleted: string[];
  moved: Array<{from: string; to: string}>;
}

export interface WorkspaceDownloadReply {
  ok: true;
  path: string;
  bytes_written: number;
  created: boolean;
  source_url: string;
  truncated: boolean;
}

export type WorkspaceReply =
  | WorkspaceReadReply
  | WorkspaceWriteReply
  | WorkspaceEditReply
  | WorkspaceApplyPatchReply
  | WorkspaceDownloadReply
  | WorkspaceErrorReply;

export function isWorkspaceError(r: WorkspaceReply): r is WorkspaceErrorReply {
  return (r as WorkspaceErrorReply).ok === false;
}
