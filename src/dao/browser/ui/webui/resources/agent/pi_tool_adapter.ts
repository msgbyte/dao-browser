// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Bridges Dao's existing `ToolDefinition[]` (JSON Schema) into pi-agent-core's
// `AgentTool<TSchema>[]` contract so pi-web-ui's ChatPanel can consume the
// same 30 tools that PR1/PR2 shipped. Every `execute()` delegates to the
// existing `executeTool(name, args)` in agent_bridge so side effects like
// `lock_tab`, `save_memory`, and `save_skill` continue to work unchanged.

import {executeTool, recordToolCall, tools} from './agent_bridge.js';
import type {ToolDefinition} from './agent_bridge.js';
import {registerDaoToolRenderers} from './dao_tool_renderer.js';
import {isToolEnabled} from './tool_catalog.js';

// pi-agent-core's AgentTool is carried in the vendored runtime bundle. We
// intentionally import it type-only through `any` to avoid coupling to the
// bundle's internal TypeBox types — the structural shape is small and
// stable.
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type AgentTool = any;
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type AgentToolResult = {content: Array<{type: 'text'; text: string}>; details: any};

function describeError(e: unknown): string {
  if (e instanceof Error) return e.message || String(e);
  if (typeof e === 'string') return e;
  try {
    return JSON.stringify(e);
  } catch {
    return String(e);
  }
}

function resultToText(result: unknown): string {
  if (typeof result === 'string') return result;
  if (result === null || result === undefined) return '';
  try {
    return JSON.stringify(result, null, 2);
  } catch {
    return String(result);
  }
}

// Convert a single Dao ToolDefinition to an AgentTool usable by the agent
// runtime. The JSON Schema parameters object is passed through verbatim —
// TypeBox schemas are structurally compatible with JSON Schema at runtime,
// and pi-agent-core only inspects the schema when validating arguments.
function adaptOne(def: ToolDefinition): AgentTool {
  const name = def.function.name;
  return {
    name,
    description: def.function.description,
    parameters: def.function.parameters,
    label: name.replace(/_/g, ' '),
    execute: async (
        _toolCallId: string,
        params: Record<string, unknown>,
        signal?: AbortSignal): Promise<AgentToolResult> => {
      if (signal?.aborted) {
        throw Object.assign(new Error('aborted'), {name: 'AbortError'});
      }
      const args = (params ?? {}) as Record<string, unknown>;
      try {
        const raw = await executeTool(name, args);
        const text = resultToText(raw);
        recordToolCall(name);
        return {
          content: [{type: 'text', text: text || '(no output)'}],
          details: raw,
        };
      } catch (e) {
        recordToolCall(name);
        throw new Error(`${name} failed: ${describeError(e)}`);
      }
    },
  };
}

export function buildAgentTools(): AgentTool[] {
  // Filter out tools the user has disabled via Settings → Tools. Renderers
  // are still registered for every known tool name (cheap, idempotent) so a
  // re-enabled tool renders correctly on the next turn without re-init.
  const adapted = tools
      .filter(t => isToolEnabled(t.function.name))
      .map(adaptOne);
  registerDaoToolRenderers(tools.map(t => t.function.name));
  return adapted;
}
