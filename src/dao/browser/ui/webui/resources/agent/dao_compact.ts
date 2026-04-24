// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provider-agnostic conversation compaction.
//
// The pi-mono ToolRunner ships an Anthropic-only client-side compactor and an
// Anthropic-beta server-side `compact_20260112` edit; neither is wired into
// our chat path (we run pi-ai's `stream()` directly via pi-agent-core). This
// module implements a portable equivalent that works for every provider Dao
// configures (OpenAI, Anthropic, Google, Ollama, openai-compatible custom).
//
// Strategy: ask the active model to produce a structured `<summary>` of the
// existing conversation, then collapse `agent.state.messages` down to a
// single synthetic user turn carrying that summary. The agent loop continues
// from there with a small, focused context. We keep the most recent
// `keepTailUserTurns` user/assistant pairs verbatim so the next turn still
// has live grounding. Tool-result messages and incomplete tool-call pairs
// are dropped from the kept tail because partial pairs would break provider
// validation.

// eslint-disable-next-line @typescript-eslint/no-explicit-any
import * as pi from './vendor/pi_runtime_bundle.js';

// ---- Minimal structural types mirroring pi-ai's Message/Context shape ----

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

interface PiModel {
  id: string;
  provider: string;
  reasoning?: boolean;
  contextWindow?: number;
  maxTokens?: number;
}

interface PiContext {
  systemPrompt?: string;
  messages: PiMessage[];
  tools?: Array<{name: string; description: string; parameters: object}>;
}

type PiEvent =
  | {type: 'text_delta'; delta: string}
  | {type: 'error'; error: {errorMessage?: string}}
  | {type: string};

// eslint-disable-next-line @typescript-eslint/no-explicit-any
const piStream = (pi as any).stream as
    (model: PiModel, context: PiContext, options?: {
      apiKey?: string;
      signal?: AbortSignal;
      reasoningEffort?: 'minimal' | 'low' | 'medium' | 'high' | 'xhigh';
    }) => AsyncIterable<PiEvent>;

// ---- Public agent surface ----

export interface CompactableAgent {
  state: {
    systemPrompt: string;
    model: PiModel;
    messages: PiMessage[];
    isStreaming: boolean;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    [key: string]: any;
  };
  getApiKey?: (provider: string) =>
      Promise<string | undefined> | string | undefined;
}

export interface CompactOptions {
  // Verbatim trailing user/assistant pairs to keep after the summary.
  // 0 keeps only the summary; default 1 keeps the most recent exchange so the
  // next turn still has fresh grounding without inheriting old tool noise.
  keepTailUserTurns?: number;
  signal?: AbortSignal;
}

export interface CompactResult {
  summary: string;
  // Total messages replaced by the summary (excluding kept tail).
  collapsedCount: number;
  // Messages kept verbatim from the tail.
  keptCount: number;
}

// Marker injected at the start of the synthetic summary message so the UI /
// future compactor passes can recognise prior summaries and avoid re-summarising
// what is already a summary.
const SUMMARY_PREFIX = '[Dao compacted summary]';

const SUMMARY_PROMPT = `Summarize the conversation above as a structured handoff for the next turn. Output ONLY a single <summary> XML block, no prose before or after, with these sections:

<summary>
1. Task overview: what the user is trying to accomplish in this session.
2. Current state: the latest verified facts, open files, last command output, what the assistant just did.
3. Important discoveries: code locations (file:line), API shapes, pitfalls, decisions made.
4. Next steps: the immediate planned action and any pending todos.
5. Context to preserve: user preferences, constraints, naming choices, key identifiers (URLs, ids, paths) that future turns must not lose.
</summary>

Be concise but lossless on identifiers, paths, and decisions. Skip greetings, transient tool errors that were resolved, and anything already superseded.`;

// ---- Helpers ----

function isCompactedSummaryMessage(m: PiMessage): boolean {
  if (m.role !== 'user') return false;
  const c = m.content;
  if (typeof c === 'string') return c.startsWith(SUMMARY_PREFIX);
  if (Array.isArray(c)) {
    const first = c.find((p) => p.type === 'text') as
        PiTextContent | undefined;
    return !!first && first.text.startsWith(SUMMARY_PREFIX);
  }
  return false;
}

// Walk back from the end and collect at most `n` user turns plus the
// assistant + tool messages that complete each turn. Stops if it would
// orphan a tool_use (assistant tool call without its matching toolResult)
// or a toolResult without its tool_use.
function selectTail(messages: PiMessage[], n: number): PiMessage[] {
  if (n <= 0) return [];
  const tail: PiMessage[] = [];
  let userTurnsSeen = 0;
  for (let i = messages.length - 1; i >= 0; i--) {
    const m = messages[i];
    if (!m) continue;
    tail.unshift(m);
    if (m.role === 'user') {
      userTurnsSeen++;
      if (userTurnsSeen >= n) break;
    }
  }
  // Drop a leading toolResult / leading assistant — both would dangle without
  // their preceding tool_use / user turn that we just left out of the slice.
  while (tail.length > 0) {
    const head = tail[0];
    if (head && (head.role === 'toolResult' || head.role === 'assistant')) {
      tail.shift();
    } else {
      break;
    }
  }
  // Drop a trailing assistant whose last block is a toolCall without a
  // matching toolResult — most providers reject that pair.
  const last = tail[tail.length - 1];
  if (last && last.role === 'assistant') {
    const blocks = last.content;
    const lastBlock = blocks[blocks.length - 1];
    if (lastBlock && typeof lastBlock !== 'string'
        && lastBlock.type === 'toolCall') {
      tail.pop();
    }
  }
  return tail;
}

function extractSummary(text: string): string {
  const m = text.match(/<summary>([\s\S]*?)<\/summary>/i);
  if (m && m[1]) return m[1].trim();
  // Model didn't honor the tag — fall back to the raw text so we don't lose
  // the summarization work.
  return text.trim();
}

// ---- Main entry point ----

export async function compactAgentMessages(
    agent: CompactableAgent,
    options: CompactOptions = {}): Promise<CompactResult> {
  if (agent.state.isStreaming) {
    throw new Error('Cannot compact while a turn is streaming');
  }
  const original = agent.state.messages;
  if (!Array.isArray(original) || original.length === 0) {
    throw new Error('No messages to compact');
  }

  const keepTail = Math.max(0, options.keepTailUserTurns ?? 1);
  const tail = selectTail(original, keepTail);
  const tailStart = original.length - tail.length;
  const head = original.slice(0, tailStart);

  if (head.length === 0) {
    throw new Error('Nothing to compact: all messages are inside the kept tail');
  }
  // Refuse to re-compact a freshly-compacted history (head is just the prior
  // summary marker).
  const onlyMessage = head[0];
  if (head.length === 1 && onlyMessage
      && isCompactedSummaryMessage(onlyMessage)) {
    throw new Error('History is already compacted');
  }

  const model = agent.state.model;
  const apiKey = agent.getApiKey
      ? await Promise.resolve(agent.getApiKey(model.provider))
      : undefined;

  const now = Date.now();
  const summaryRequest: PiUserMessage = {
    role: 'user',
    content: SUMMARY_PROMPT,
    timestamp: now,
  };
  const context: PiContext = {
    systemPrompt: agent.state.systemPrompt,
    // No tools — we want a deterministic text reply, not a tool-call detour.
    tools: [],
    messages: [...head, summaryRequest],
  };

  const streamOpts: {
    apiKey?: string;
    signal?: AbortSignal;
    reasoningEffort?: 'minimal' | 'low' | 'medium' | 'high' | 'xhigh';
  } = {
    apiKey: apiKey ?? undefined,
    signal: options.signal,
  };
  if (model.reasoning) {
    // Summarization is a reasoning-light task; minimal keeps latency low on
    // gpt-5-style reasoning models.
    streamOpts.reasoningEffort = 'minimal';
  }

  let raw = '';
  try {
    for await (const ev of piStream(model, context, streamOpts)) {
      if (options.signal?.aborted) {
        throw new DOMException('Aborted', 'AbortError');
      }
      if (ev.type === 'text_delta') {
        raw += (ev as {delta: string}).delta;
      } else if (ev.type === 'error') {
        const msg = (ev as {error: {errorMessage?: string}}).error
            ?.errorMessage ?? 'Compaction model returned an error';
        throw new Error(msg);
      }
    }
  } catch (e) {
    if ((e as Error).name === 'AbortError') throw e;
    throw new Error(
        `Compaction failed: ${(e as Error).message ?? String(e)}`);
  }

  if (!raw.trim()) {
    throw new Error('Compaction model returned an empty summary');
  }

  const summary = extractSummary(raw);
  const summaryMessage: PiUserMessage = {
    role: 'user',
    content:
        `${SUMMARY_PREFIX} The earlier turns of this conversation have been ` +
        `compacted into the structured summary below. Continue the session ` +
        `using this as the prior context.\n\n<summary>\n${summary}\n</summary>`,
    timestamp: Date.now(),
  };

  // Reassign through the state setter so pi-agent-core's slice-copy + Lit
  // change-detection both fire (same pattern dao_chat_view uses on
  // message_end). Mutating in place would leave the chat list stale.
  agent.state.messages = [summaryMessage, ...tail];

  return {
    summary,
    collapsedCount: head.length,
    keptCount: tail.length,
  };
}

// Heuristic: estimate the token weight of the message history so the UI can
// surface a "compact" affordance when context starts to fill up. We don't have
// real tokenizer access in the bundle for every provider, so use a simple
// chars / 4 approximation, which is within ~30% of OpenAI/Anthropic tokenizers
// for English text and is more than accurate enough to drive a UI hint.
export function estimateMessagesTokens(messages: PiMessage[]): number {
  let chars = 0;
  for (const m of messages) {
    if (m.role === 'user') {
      const c = m.content;
      if (typeof c === 'string') chars += c.length;
      else if (Array.isArray(c)) {
        for (const p of c) {
          if (p.type === 'text') chars += p.text.length;
          // Images cost real tokens but are bounded by provider; approximate
          // each image as 1k tokens (4k chars).
          else if (p.type === 'image') chars += 4000;
        }
      }
    } else if (m.role === 'assistant') {
      for (const b of m.content) {
        if (b.type === 'text') chars += b.text.length;
        else if (b.type === 'toolCall') {
          chars += b.name.length;
          try { chars += JSON.stringify(b.arguments).length; }
          catch (_) { /* ignore */ }
        }
      }
    } else if (m.role === 'toolResult') {
      for (const p of m.content) chars += p.text.length;
    }
  }
  return Math.ceil(chars / 4);
}
