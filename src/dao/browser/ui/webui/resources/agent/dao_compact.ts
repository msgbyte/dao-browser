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
interface PiUserWithAttachmentsMessage {
  role: 'user-with-attachments';
  content: string | Array<PiTextContent | PiImageContent>;
  attachments?: unknown[];
  timestamp: number;
  dao?: {autoCompactNotice?: boolean};
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
  dao?: {autoCompactNotice?: boolean};
}
interface PiToolResultMessage {
  role: 'toolResult';
  toolCallId: string;
  toolName: string;
  content: PiTextContent[];
  isError: boolean;
  timestamp: number;
}
type PiMessage = PiUserMessage | PiUserWithAttachmentsMessage |
    PiAssistantMessage | PiToolResultMessage;

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

const SUMMARY_PROMPT = `Summarize the conversation transcript below as a structured handoff for the next turn. Output ONLY a single <summary> XML block, no prose before or after, with these sections:

<summary>
1. Task overview: what the user is trying to accomplish in this session.
2. Current state: the latest verified facts, open files, last command output, what the assistant just did.
3. Important discoveries: code locations (file:line), API shapes, pitfalls, decisions made.
4. Next steps: the immediate planned action and any pending todos.
5. Context to preserve: user preferences, constraints, naming choices, key identifiers (URLs, ids, paths) that future turns must not lose.
</summary>

Be concise but lossless on identifiers, paths, and decisions. Skip greetings, transient tool errors that were resolved, and anything already superseded.`;

const APPROX_CHARS_PER_TOKEN = 4;
const DEFAULT_CONTEXT_WINDOW_TOKENS = 128_000;
const COMPACTION_REQUEST_CONTEXT_RATIO = 0.8;
const COMPACTION_KEEP_TAIL_CONTEXT_RATIO = 0.4;
const MIN_COMPACTION_TRANSCRIPT_TOKENS = 1024;

function isRecord(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object';
}

function messageRole(m: PiMessage): string {
  return isRecord(m) && typeof m.role === 'string' ? m.role : 'unknown';
}

function isUserLikeMessage(m: PiMessage): boolean {
  const role = messageRole(m);
  return role === 'user' || role === 'user-with-attachments';
}

function isLocalDaoMessage(m: PiMessage): boolean {
  return isRecord(m) && isRecord(m['dao']) &&
      m['dao']['autoCompactNotice'] === true;
}

function estimateTextTokens(text: string): number {
  return Math.ceil(text.length / APPROX_CHARS_PER_TOKEN);
}

function modelContextWindow(model: PiModel): number {
  const configured = Number(model.contextWindow);
  return Number.isFinite(configured) && configured > 0 ?
      configured :
      DEFAULT_CONTEXT_WINDOW_TOKENS;
}

function compactionRequestTokenBudget(
    model: PiModel, systemPrompt: string | undefined): number {
  const contextWindow = modelContextWindow(model);
  const maxRequestTokens =
      Math.floor(contextWindow * COMPACTION_REQUEST_CONTEXT_RATIO);
  const configuredOutput = Number(model.maxTokens);
  const outputReserve = Math.min(
      Math.max(
          Number.isFinite(configuredOutput) && configuredOutput > 0 ?
              configuredOutput :
              4096,
          2048),
      Math.floor(contextWindow * 0.25));
  const fixedPromptTokens = estimateTextTokens(SUMMARY_PROMPT) +
      estimateTextTokens(systemPrompt ?? '') + 256;
  return Math.max(
      MIN_COMPACTION_TRANSCRIPT_TOKENS,
      maxRequestTokens - outputReserve - fixedPromptTokens);
}

function safeJson(value: unknown): string {
  try {
    return JSON.stringify(value);
  } catch (_) {
    return '[unserializable]';
  }
}

function textFromContent(content: unknown): string {
  if (typeof content === 'string') return content;
  if (!Array.isArray(content)) return '';
  const out: string[] = [];
  for (const part of content) {
    if (!isRecord(part)) continue;
    if (part['type'] === 'text') {
      out.push(typeof part['text'] === 'string' ? part['text'] : '');
    } else if (part['type'] === 'image') {
      const mime =
          typeof part['mimeType'] === 'string' ? part['mimeType'] : 'image';
      const data = typeof part['data'] === 'string' ? part['data'] : '';
      out.push(`[image: ${mime}, ${data.length} base64 chars]`);
    } else if (part['type'] === 'toolCall') {
      const name =
          typeof part['name'] === 'string' ? part['name'] : 'unknown_tool';
      const id = typeof part['id'] === 'string' ? ` id=${part['id']}` : '';
      out.push(`[tool call: ${name}${id} ${safeJson(part['arguments'])}]`);
    }
  }
  return out.filter(Boolean).join('\n');
}

function attachmentSummaryText(attachment: unknown, index: number): string {
  if (!isRecord(attachment)) return '';
  const name = typeof attachment['fileName'] === 'string' ?
      attachment['fileName'] :
      `attachment ${index + 1}`;
  const extracted = typeof attachment['extractedText'] === 'string' ?
      attachment['extractedText'] :
      '';
  const mime = typeof attachment['mimeType'] === 'string' ?
      attachment['mimeType'] :
      '';
  const content = typeof attachment['content'] === 'string' ?
      attachment['content'] :
      '';
  const details: string[] = [];
  if (mime) details.push(mime);
  if (content) details.push(`${content.length} chars`);
  const suffix = details.length ? ` (${details.join(', ')})` : '';
  return extracted ?
      `[attachment: ${name}${suffix}]\n${extracted}` :
      `[attachment: ${name}${suffix}]`;
}

function attachmentsText(m: PiMessage): string {
  if (messageRole(m) !== 'user-with-attachments' || !isRecord(m) ||
      !Array.isArray(m.attachments)) {
    return '';
  }
  return m.attachments
      .map((attachment, index) => attachmentSummaryText(attachment, index))
      .filter(Boolean)
      .join('\n\n');
}

function truncateMiddle(text: string, maxChars: number): string {
  if (text.length <= maxChars) return text;
  if (maxChars <= 0) {
    return `[content truncated: ${text.length} chars omitted]`;
  }
  const marker =
      `\n\n[content truncated: ${text.length - maxChars} chars omitted]\n\n`;
  if (maxChars <= marker.length + 32) {
    return `[content truncated: ${text.length} chars omitted]`;
  }
  const available = maxChars - marker.length;
  const head = Math.ceil(available * 0.4);
  const tail = Math.max(0, available - head);
  return `${text.slice(0, head)}${marker}${tail > 0 ? text.slice(-tail) : ''}`;
}

function serializeMessageForSummary(m: PiMessage, index: number): {
  header: string;
  body: string;
} {
  const role = messageRole(m);
  const timestamp = isRecord(m) && typeof m.timestamp === 'number' ?
      ` timestamp=${new Date(m.timestamp).toISOString()}` :
      '';
  let header = `[${index + 1}] ${role}${timestamp}`;
  if (role === 'toolResult' && isRecord(m)) {
    const toolName = typeof m.toolName === 'string' ? m.toolName : '';
    const toolCallId = typeof m.toolCallId === 'string' ? m.toolCallId : '';
    const isError = typeof m.isError === 'boolean' ?
        ` isError=${m.isError}` :
        '';
    header += `${toolName ? ` tool=${toolName}` : ''}` +
        `${toolCallId ? ` id=${toolCallId}` : ''}${isError}`;
  }

  const parts = [textFromContent(isRecord(m) ? m.content : '')];
  const attachments = attachmentsText(m);
  if (attachments) parts.push(attachments);
  const body = parts.filter(Boolean).join('\n\n') || '(empty)';
  return {header, body};
}

function buildBoundedTranscript(messages: PiMessage[], maxChars: number):
    string {
  if (messages.length === 0) return '(no messages)';

  const entries = messages.map(serializeMessageForSummary);
  const structuralChars = entries.reduce(
      (sum, e) => sum + e.header.length + '\n\n'.length,
      0);
  const bodyBudget = Math.max(0, maxChars - structuralChars);
  const perBodyBudget = Math.max(0, Math.floor(bodyBudget / entries.length));
  const serialized = entries.map((entry) => {
    const body = truncateMiddle(entry.body, perBodyBudget);
    return `${entry.header}\n${body}`;
  }).join('\n\n');

  return truncateMiddle(serialized, maxChars);
}

function buildCompactionRequestContent(
    messages: PiMessage[], tokenBudget: number): string {
  const prelude = `${SUMMARY_PROMPT}\n\n` +
      `Conversation transcript to compact follows. Entries marked ` +
      `"content truncated" were clipped locally so this summarization ` +
      `request stays inside the active model context window.\n\n` +
      `<conversation>\n`;
  const closing = `\n</conversation>`;
  const maxChars = Math.max(0, tokenBudget * APPROX_CHARS_PER_TOKEN);
  const transcriptBudget = Math.max(0, maxChars - prelude.length -
      closing.length);
  const transcript = buildBoundedTranscript(messages, transcriptBudget);
  return `${prelude}${transcript}${closing}`;
}

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
    if (isUserLikeMessage(m)) {
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
  const allMessages = agent.state.messages;
  if (!Array.isArray(allMessages) || allMessages.length === 0) {
    throw new Error('No messages to compact');
  }
  const allMessagesLength = allMessages.length;
  const original = allMessages.filter(
      (message) => !isLocalDaoMessage(message));
  if (original.length === 0) {
    throw new Error('No messages to compact');
  }

  const model = agent.state.model;
  const keepTail = Math.max(0, options.keepTailUserTurns ?? 1);
  let tail = selectTail(original, keepTail);
  const tailStart = original.length - tail.length;
  let head = original.slice(0, tailStart);

  // The newest user turn is normally useful grounding, but it is exactly
  // where huge tool results accumulate after a failed long-running turn. If
  // keeping that tail would leave the compacted session near the model limit,
  // summarize it too so manual compaction can recover the conversation.
  const tailBudget = Math.floor(
      modelContextWindow(model) * COMPACTION_KEEP_TAIL_CONTEXT_RATIO);
  if (tail.length > 0 &&
      (head.length === 0 || estimateMessagesTokens(tail) > tailBudget)) {
    tail = [];
    head = original;
  }

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

  const apiKey = agent.getApiKey
      ? await Promise.resolve(agent.getApiKey(model.provider))
      : undefined;

  const now = Date.now();
  const requestContent = buildCompactionRequestContent(
      head,
      compactionRequestTokenBudget(model, agent.state.systemPrompt));
  const summaryRequest: PiUserMessage = {
    role: 'user',
    content: requestContent,
    timestamp: now,
  };
  const context: PiContext = {
    systemPrompt: agent.state.systemPrompt,
    // No tools — we want a deterministic text reply, not a tool-call detour.
    tools: [],
    messages: [summaryRequest],
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

  // Summarization is a multi-second network round-trip. If a new turn started
  // meanwhile (pi-agent-core reassigns state.messages by reference on send /
  // stream), overwriting now with our pre-await `tail` snapshot would silently
  // drop the freshly-sent message and desync the in-flight turn. Bail instead.
  if (agent.state.messages !== allMessages ||
      allMessages.length !== allMessagesLength || agent.state.isStreaming) {
    throw new Error('Conversation changed during compaction');
  }

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
    if (isLocalDaoMessage(m)) continue;
    if (isUserLikeMessage(m)) {
      const c = m.content;
      if (typeof c === 'string') chars += c.length;
      else if (Array.isArray(c)) {
        for (const p of c) {
          if (p.type === 'text') chars += p.text.length;
          else if (p.type === 'image') chars += 4000;
        }
      }
      const attachments = attachmentsText(m);
      if (attachments) chars += attachments.length;
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
