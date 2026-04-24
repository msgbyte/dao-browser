// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Thin adapter that runs the legacy `callLLMStreaming` callback contract on
// top of the vendored pi-ai SDK. PR1 only wires the OpenAI-compatible path
// (to preserve existing behavior byte-for-byte); PR1-6 adds provider routing
// for anthropic / google / etc.
//
// The vendored bundle (`./vendor/pi_runtime_bundle.ts`) ships with
// `@ts-nocheck`, so every import from it is typed as `any`. We re-declare
// the bits we actually use as local structural types to keep the adapter
// type-safe on the consuming side.

// eslint-disable-next-line @typescript-eslint/no-explicit-any
import * as piAgent from './vendor/pi_runtime_bundle.js';

import type {
  ChatMessage,
  StreamCallbacks,
  ToolCall,
  ToolDefinition,
  UsageInfo,
} from './agent_bridge.js';

// ---- Local structural types for pi-ai values imported above ----
// Kept minimal — only fields the adapter reads or constructs.

interface PiModel {
  id: string;
  name: string;
  api: string;
  provider: string;
  baseUrl: string;
  reasoning: boolean;
  input: Array<'text'|'image'>;
  cost: {input: number; output: number; cacheRead: number; cacheWrite: number};
  contextWindow: number;
  maxTokens: number;
}

interface PiTextContent { type: 'text'; text: string; }
interface PiImageContent { type: 'image'; mimeType: string; data: string; }
interface PiToolCallBlock {
  type: 'toolCall';
  id: string;
  name: string;
  arguments: Record<string, unknown>;
}

interface PiUserMessage {
  role: 'user';
  content: string | Array<PiTextContent | PiImageContent>;
  timestamp: number;
}
interface PiAssistantMessage {
  role: 'assistant';
  content: Array<PiTextContent | PiToolCallBlock>;
  api: string;
  provider: string;
  model: string;
  usage: {input: number; output: number; cacheRead: number; cacheWrite: number};
  stopReason: string;
  timestamp: number;
  errorMessage?: string;
}
interface PiToolResultMessage {
  role: 'toolResult';
  toolCallId: string;
  toolName: string;
  content: PiTextContent[];
  isError: boolean;
  timestamp: number;
}
type PiMessage = PiUserMessage | PiAssistantMessage | PiToolResultMessage;

interface PiContext {
  systemPrompt?: string;
  messages: PiMessage[];
  tools?: Array<{name: string; description: string; parameters: object}>;
}

// Discriminated union of the events we care about. The bundle emits other
// event types we ignore.
type PiEvent =
  | {type: 'text_delta'; delta: string}
  | {type: 'toolcall_end'; toolCall: {id: string; name: string; arguments: Record<string, unknown>}}
  | {type: 'done'; message: {usage?: {input?: number; output?: number}}}
  | {type: 'error'; error: {errorMessage?: string}}
  | {type: string};  // fallback

// eslint-disable-next-line @typescript-eslint/no-explicit-any
const piStream = (piAgent as any).stream as
    (model: PiModel, context: PiContext, options?: {
      apiKey?: string;
      signal?: AbortSignal;
      reasoningEffort?: 'minimal' | 'low' | 'medium' | 'high' | 'xhigh';
    }) => AsyncIterable<PiEvent> & {abort?: () => void};

// eslint-disable-next-line @typescript-eslint/no-explicit-any
const piGetModel = (piAgent as any).getModel as
    (provider: string, modelId: string) => PiModel;

export interface LLMProviderConfig {
  // One of pi-ai's KnownProvider values, or "openai-compatible" to use the
  // classic endpoint path (baseUrl/v1/chat/completions).
  provider: string;
  apiKey: string;
  baseUrl?: string;
  model: string;
  signal?: AbortSignal;
}

function buildOpenAICompatModel(modelId: string, baseUrl: string): PiModel {
  // pi-ai appends `/chat/completions` itself when the API is
  // `openai-completions`; strip trailing slashes and ensure `/v1`.
  let base = baseUrl.replace(/\/+$/, '');
  if (!base.endsWith('/v1')) base += '/v1';
  return {
    id: modelId,
    name: modelId,
    api: 'openai-completions',
    provider: 'openai',
    baseUrl: base,
    reasoning: false,
    input: ['text', 'image'],
    cost: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
    contextWindow: 128000,
    maxTokens: 16384,
  };
}

function convertTools(tools: ToolDefinition[]):
    Array<{name: string; description: string; parameters: object}> {
  return tools.map((t) => ({
    name: t.function.name,
    description: t.function.description,
    parameters: t.function.parameters,
  }));
}

function convertMessages(msgs: ChatMessage[]):
    {systemPrompt: string | undefined; messages: PiMessage[]} {
  let systemPrompt: string | undefined;
  const out: PiMessage[] = [];
  const now = Date.now();

  for (const m of msgs) {
    if (m.role === 'system') {
      if (typeof m.content === 'string') {
        systemPrompt = (systemPrompt ?? '') + m.content;
      }
      continue;
    }

    if (m.role === 'user') {
      if (Array.isArray(m.content)) {
        const content: Array<PiTextContent | PiImageContent> = [];
        for (const part of m.content) {
          if (part.type === 'image_url' && part.image_url?.url) {
            const url = part.image_url.url;
            const match = /^data:([^;]+);base64,(.*)$/.exec(url);
            if (match) {
              content.push({
                type: 'image',
                mimeType: match[1] ?? '',
                data: match[2] ?? '',
              });
            } else {
              content.push({type: 'text', text: `[image: ${url}]`});
            }
          } else {
            content.push({type: 'text', text: part.text ?? ''});
          }
        }
        out.push({role: 'user', content, timestamp: now});
      } else {
        out.push({role: 'user', content: m.content ?? '', timestamp: now});
      }
      continue;
    }

    if (m.role === 'assistant') {
      const blocks: Array<PiTextContent | PiToolCallBlock> = [];
      if (typeof m.content === 'string' && m.content.length > 0) {
        blocks.push({type: 'text', text: m.content});
      }
      if (m.tool_calls) {
        for (const tc of m.tool_calls) {
          let args: Record<string, unknown> = {};
          try {
            args = JSON.parse(tc.function.arguments || '{}');
          } catch {
            args = {};
          }
          blocks.push({
            type: 'toolCall',
            id: tc.id,
            name: tc.function.name,
            arguments: args,
          });
        }
      }
      out.push({
        role: 'assistant',
        content: blocks,
        api: 'openai-completions',
        provider: 'openai',
        model: '',
        usage: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
        stopReason: 'stop',
        timestamp: now,
      });
      continue;
    }

    if (m.role === 'tool') {
      const text = typeof m.content === 'string'
          ? m.content
          : (Array.isArray(m.content)
              ? m.content.map((p) => p.text ?? '').join('')
              : '');
      out.push({
        role: 'toolResult',
        toolCallId: m.tool_call_id ?? '',
        toolName: m.name ?? '',
        content: [{type: 'text', text}],
        isError: false,
        timestamp: now,
      });
      continue;
    }
  }

  return {systemPrompt, messages: out};
}

export async function callLLMStreamingWithPi(
    msgs: ChatMessage[], tools: ToolDefinition[],
    callbacks: StreamCallbacks, config: LLMProviderConfig): Promise<void> {
  const model = config.provider === 'openai-compatible'
      ? buildOpenAICompatModel(
          config.model,
          config.baseUrl ?? 'https://api.openai.com')
      : piGetModel(config.provider, config.model);

  const {systemPrompt, messages} = convertMessages(msgs);
  const context: PiContext = {
    systemPrompt,
    messages,
    tools: convertTools(tools),
  };

  let events: AsyncIterable<PiEvent>;
  try {
    // Reasoning-capable catalog models (e.g. gpt-5) reject `reasoning.effort:
    // "none"` on the OpenAI Responses API — pi-ai's default when no effort is
    // provided. Pass a safe default for any model advertising reasoning.
    const streamOpts: {
      apiKey?: string;
      signal?: AbortSignal;
      reasoningEffort?: 'minimal' | 'low' | 'medium' | 'high' | 'xhigh';
    } = {
      apiKey: config.apiKey,
      signal: config.signal,
    };
    if (model.reasoning) {
      streamOpts.reasoningEffort = 'medium';
    }
    events = piStream(model, context, streamOpts);
  } catch (e) {
    const err = e as Error;
    if (err.name === 'AbortError') throw err;
    callbacks.onError(
        'Connection Failed', err.message || 'Cannot reach API');
    return;
  }

  let fullContent = '';
  const emittedToolCalls: ToolCall[] = [];
  let usage: UsageInfo | undefined;
  let aborted = false;

  try {
    for await (const ev of events) {
      if (config.signal?.aborted) {
        aborted = true;
        break;
      }
      if (ev.type === 'text_delta') {
        const delta = (ev as {delta: string}).delta;
        fullContent += delta;
        callbacks.onToken(delta);
      } else if (ev.type === 'toolcall_end') {
        const tc = (ev as {toolCall: {id: string; name: string; arguments: Record<string, unknown>}}).toolCall;
        const outCall: ToolCall = {
          id: tc.id,
          type: 'function',
          function: {
            name: tc.name,
            arguments: JSON.stringify(tc.arguments ?? {}),
          },
        };
        emittedToolCalls.push(outCall);
        callbacks.onToolCall(outCall);
      } else if (ev.type === 'done') {
        const u = (ev as {message?: {usage?: {input?: number; output?: number}}})
            .message?.usage;
        if (u) {
          usage = {
            prompt_tokens: u.input ?? 0,
            completion_tokens: u.output ?? 0,
            total_tokens: (u.input ?? 0) + (u.output ?? 0),
          };
        }
      } else if (ev.type === 'error') {
        const msg = (ev as {error?: {errorMessage?: string}}).error?.errorMessage
            ?? 'Stream error';
        callbacks.onError('API Error', msg);
        return;
      }
    }
  } catch (e) {
    const err = e as Error;
    if (err.name === 'AbortError') throw err;
    callbacks.onError('Stream Error', err.message || 'Error reading stream');
    return;
  }

  if (aborted) {
    throw Object.assign(new Error('aborted'), {name: 'AbortError'});
  }

  callbacks.onDone(fullContent, emittedToolCalls, usage);
}
