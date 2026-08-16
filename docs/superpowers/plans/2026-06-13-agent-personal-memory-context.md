# Agent Personal Memory Context Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Dao Agent use existing long-term memory during live chat turns by injecting a hidden, bounded `<memory-context>` attachment into each relevant LLM request.

**Architecture:** Add a small WebUI helper module that converts the native memory payload into a safe pi-web-ui attachment, then call it from the existing `dao_chat_view.ts` send-message interception path. Reuse `DaoAgentMemoryService::GetMemoryContext` and extend the native WebUI handler serialization to include the existing domain summary.

**Tech Stack:** Chromium C++ WebUI handlers, Dao Agent Lit/TypeScript WebUI, pi-web-ui attachment shape, Vitest, Dao browser tests.

---

## Notes For Execution

- Do not edit anything under `engine/`.
- Do not run `npm run build`, `npm run build:debug`, direct `gn`, `ninja`, `autoninja`, or `siso`.
- Compile confirmation, if needed after implementation, must be `npm run rebuild`.
- WebUI verification should use `npm run test:webui` and `npm run lint:lit`.
- Git state-changing commands in the commit steps require explicit user authorization in this repository. The commit steps are included for handoff completeness; do not run them without that authorization.

## File Structure

- Create `src/dao/browser/ui/webui/resources/agent/dao_memory_context.ts`
  - Pure TypeScript helper for native payload types, XML escaping, budget trimming, text rendering, and attachment creation.
- Create `src/dao/browser/ui/webui/resources/agent/__tests__/dao_memory_context.test.ts`
  - Focused Vitest coverage for the helper without mounting `<dao-chat-view>`.
- Modify `src/dao/browser/ui/webui/resources/agent/BUILD.gn`
  - Add `dao_memory_context.ts` to `ts_files`.
- Modify `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
  - Import the helper and add `maybeAttachMemoryContext_` to the existing send wrapper.
- Modify `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`
  - Add integration tests around the send wrapper and mocked `callNative`.
- Modify `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
  - Add memory-context guidance to `BASE_SYSTEM_PROMPT`.
- Modify `src/dao/browser/ui/webui/dao_agent_ui.cc`
  - Serialize `MemoryContext::relevant_summary` as `relevantSummary`.
- Modify `src/dao/browser/agent/dao_dream_browsertest.cc`
  - Add a focused service-level regression test that saved domain summaries are returned by `GetMemoryContext`.

## Task 1: Add Pure Memory Context Helper

**Files:**
- Create: `src/dao/browser/ui/webui/resources/agent/dao_memory_context.ts`
- Create: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_memory_context.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/BUILD.gn`

- [ ] **Step 1: Write failing helper tests**

Create `src/dao/browser/ui/webui/resources/agent/__tests__/dao_memory_context.test.ts`:

```ts
import {describe, expect, it} from 'vitest';

import {
  buildMemoryContextAttachment,
  buildMemoryContextText,
  hasMemoryContextPayload,
  type NativeMemoryContext,
} from '../dao_memory_context.js';

describe('dao memory context helper', () => {
  it('detects empty memory payloads', () => {
    expect(hasMemoryContextPayload({})).toBe(false);
    expect(hasMemoryContextPayload({
      preferences: [],
      episodes: [],
      recentMessages: [],
      relevantSummary: null,
    })).toBe(false);
  });

  it('renders preferences, summary, and episodes with escaped XML', () => {
    const payload: NativeMemoryContext = {
      preferences: [{
        key: 'response.style',
        value: 'Use <short> answers & cite "files".',
        confidence: 0.91,
      }],
      relevantSummary: {
        summary: 'User reviews PRs on this domain.',
        primaryDomain: 'github.com',
      },
      episodes: [{
        title: 'Pull request',
        intent: 'Review auth changes',
        outcome: 'Pointed out missing tests',
        confidence: 0.77,
      }],
    };

    const text = buildMemoryContextText({
      url: 'https://github.com/example/repo/pull/1',
      domain: 'github.com',
      payload,
    });

    expect(text).toContain('<memory-context source="dao-agent-memory" domain="github.com"');
    expect(text).toContain('Use &lt;short&gt; answers &amp; cite &quot;files&quot;.');
    expect(text).toContain('<summary domain="github.com">');
    expect(text).toContain('<episode confidence="0.77" title="Pull request">');
  });

  it('builds a hidden pi attachment with extracted text', () => {
    const attachment = buildMemoryContextAttachment({
      url: 'https://example.com/a',
      domain: 'example.com',
      payload: {
        preferences: [{
          key: 'interest.dao',
          value: 'Likes Dao Browser implementation details',
          confidence: 0.8,
        }],
      },
    });

    expect(attachment).not.toBeNull();
    expect(attachment?.type).toBe('document');
    expect(attachment?.fileName).toBe('dao-memory-context.md');
    expect(attachment?.mimeType).toBe('text/markdown');
    expect(attachment?.extractedText).toContain('<memory-context');
    expect(attachment?.preview).toBe('');
  });

  it('trims oversized memory values while preserving closing tags', () => {
    const longValue = 'A'.repeat(12000);
    const text = buildMemoryContextText({
      url: 'https://example.com/a',
      domain: 'example.com',
      charBudget: 1600,
      payload: {
        preferences: [{
          key: 'long.pref',
          value: longValue,
          confidence: 0.9,
        }],
        episodes: [{
          intent: longValue,
          outcome: longValue,
          confidence: 0.7,
        }],
      },
    });

    expect(text.length).toBeLessThanOrEqual(1900);
    expect(text).toContain('truncated');
    expect(text).toContain('</memory-context>');
  });
});
```

- [ ] **Step 2: Run the focused helper test and verify it fails**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_memory_context.test.ts
```

Expected: FAIL because `../dao_memory_context.js` does not exist.

- [ ] **Step 3: Create the helper implementation**

Create `src/dao/browser/ui/webui/resources/agent/dao_memory_context.ts`:

```ts
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {PiAttachment} from './dao_page_capture.js';

export interface NativePreference {
  key?: string;
  value?: string;
  confidence?: number;
}

export interface NativeEpisode {
  title?: string;
  intent?: string;
  outcome?: string;
  confidence?: number;
}

export interface NativeRecentMessage {
  role?: string;
  content?: string;
}

export interface NativeRelevantSummary {
  summary?: string;
  primaryDomain?: string;
}

export interface NativeMemoryContext {
  preferences?: NativePreference[];
  episodes?: NativeEpisode[];
  recentMessages?: NativeRecentMessage[];
  relevantSummary?: NativeRelevantSummary|null;
}

interface BuildMemoryContextOptions {
  url: string;
  domain: string;
  payload: NativeMemoryContext;
  charBudget?: number;
}

const DEFAULT_CHAR_BUDGET = 7000;
const MAX_FIELD_CHARS = 900;
const MAX_RECENT_MESSAGES = 6;

function utf8ToBase64(s: string): string {
  const bytes = new TextEncoder().encode(s);
  let binary = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode.apply(
        null, Array.from(bytes.subarray(i, i + chunk)) as number[]);
  }
  return btoa(binary);
}

function textOrEmpty(value: unknown): string {
  return typeof value === 'string' ? value.replace(/\s+/g, ' ').trim() : '';
}

function confidence(value: unknown): string {
  return typeof value === 'number' && Number.isFinite(value) ?
      Math.max(0, Math.min(1, value)).toFixed(2) :
      '0.00';
}

function truncateField(value: string, maxChars = MAX_FIELD_CHARS): string {
  if (value.length <= maxChars) return value;
  return value.slice(0, Math.max(0, maxChars - 16)).trimEnd() +
      ' ... [truncated]';
}

function escapeXmlText(value: string): string {
  return value
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
}

function escapeXmlAttr(value: string): string {
  return escapeXmlText(value)
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&apos;');
}

function safeDomain(domain: string, url: string): string {
  const clean = textOrEmpty(domain);
  if (clean) return clean;
  try {
    return new URL(url).hostname;
  } catch (_) {
    return '';
  }
}

function listHasEntries<T>(items: T[]|undefined, predicate: (item: T) => boolean):
    boolean {
  return Array.isArray(items) && items.some(predicate);
}

export function hasMemoryContextPayload(payload: NativeMemoryContext): boolean {
  if (listHasEntries(payload.preferences, p =>
        !!textOrEmpty(p.key) && !!textOrEmpty(p.value))) {
    return true;
  }
  if (listHasEntries(payload.episodes, e =>
        !!textOrEmpty(e.intent) || !!textOrEmpty(e.outcome))) {
    return true;
  }
  if (listHasEntries(payload.recentMessages, m =>
        !!textOrEmpty(m.content))) {
    return true;
  }
  return !!textOrEmpty(payload.relevantSummary?.summary);
}

function pushPreferences(lines: string[], preferences: NativePreference[]|undefined) {
  const valid = (preferences || [])
      .filter(p => textOrEmpty(p.key) && textOrEmpty(p.value))
      .slice(0, 5);
  if (valid.length === 0) return;
  lines.push('  <preferences>');
  for (const pref of valid) {
    lines.push(
        `    <preference key="${escapeXmlAttr(textOrEmpty(pref.key))}" ` +
        `confidence="${confidence(pref.confidence)}">` +
        `${escapeXmlText(truncateField(textOrEmpty(pref.value)))}` +
        `</preference>`);
  }
  lines.push('  </preferences>');
}

function pushSummary(lines: string[], summary: NativeRelevantSummary|null|undefined,
                     domain: string) {
  const text = textOrEmpty(summary?.summary);
  if (!text) return;
  const summaryDomain = textOrEmpty(summary?.primaryDomain) || domain;
  lines.push(
      `  <summary domain="${escapeXmlAttr(summaryDomain)}">` +
      `${escapeXmlText(truncateField(text, 1200))}</summary>`);
}

function pushEpisodes(lines: string[], episodes: NativeEpisode[]|undefined) {
  const valid = (episodes || [])
      .filter(e => textOrEmpty(e.intent) || textOrEmpty(e.outcome))
      .slice(0, 3);
  if (valid.length === 0) return;
  lines.push('  <episodes>');
  for (const episode of valid) {
    const title = textOrEmpty(episode.title);
    const titleAttr = title ? ` title="${escapeXmlAttr(truncateField(title, 160))}"` : '';
    lines.push(
        `    <episode confidence="${confidence(episode.confidence)}"${titleAttr}>`);
    const intent = textOrEmpty(episode.intent);
    if (intent) {
      lines.push(`      <intent>${escapeXmlText(truncateField(intent))}</intent>`);
    }
    const outcome = textOrEmpty(episode.outcome);
    if (outcome) {
      lines.push(`      <outcome>${escapeXmlText(truncateField(outcome))}</outcome>`);
    }
    lines.push('    </episode>');
  }
  lines.push('  </episodes>');
}

function pushRecentMessages(lines: string[], messages: NativeRecentMessage[]|undefined) {
  const valid = (messages || [])
      .filter(m => textOrEmpty(m.content))
      .slice(-MAX_RECENT_MESSAGES);
  if (valid.length === 0) return;
  lines.push('  <recent-messages>');
  for (const message of valid) {
    const role = textOrEmpty(message.role) || 'unknown';
    lines.push(
        `    <message role="${escapeXmlAttr(role)}">` +
        `${escapeXmlText(truncateField(textOrEmpty(message.content), 500))}` +
        `</message>`);
  }
  lines.push('  </recent-messages>');
}

export function buildMemoryContextText(options: BuildMemoryContextOptions): string {
  const domain = safeDomain(options.domain, options.url);
  const budget = options.charBudget || DEFAULT_CHAR_BUDGET;
  const lines: string[] = [
    `<memory-context source="dao-agent-memory" domain="${escapeXmlAttr(domain)}" url="${escapeXmlAttr(options.url)}">`,
    '  <instruction>These are historical hints. Use them only when relevant. Current user instructions and current page content take priority.</instruction>',
  ];
  pushPreferences(lines, options.payload.preferences);
  pushSummary(lines, options.payload.relevantSummary, domain);
  pushEpisodes(lines, options.payload.episodes);
  pushRecentMessages(lines, options.payload.recentMessages);

  let body = lines.join('\n') + '\n</memory-context>';
  if (body.length <= budget) return body;

  const truncatedLines = lines.slice(0, 2);
  pushPreferences(truncatedLines, options.payload.preferences);
  pushSummary(truncatedLines, options.payload.relevantSummary, domain);
  body = truncatedLines.join('\n');
  if (body.length > budget - 80) {
    body = body.slice(0, Math.max(0, budget - 80)).trimEnd() +
        '\n  <truncated>true</truncated>';
  } else {
    body += '\n  <truncated>true</truncated>';
  }
  return body + '\n</memory-context>';
}

export function buildMemoryContextAttachment(options: BuildMemoryContextOptions):
    PiAttachment|null {
  if (!hasMemoryContextPayload(options.payload)) return null;
  const extractedText = buildMemoryContextText(options);
  return {
    id: `dao-memory-${Date.now()}-${Math.random().toString(36).slice(2)}`,
    type: 'document',
    fileName: 'dao-memory-context.md',
    mimeType: 'text/markdown',
    size: new TextEncoder().encode(extractedText).length,
    content: utf8ToBase64(extractedText),
    extractedText,
    preview: '',
  };
}
```

- [ ] **Step 4: Add the helper to WebUI build inputs**

Modify `src/dao/browser/ui/webui/resources/agent/BUILD.gn` and insert the new file in `ts_files` near `dao_markdown.ts`:

```gn
    "dao_markdown.ts",
    "dao_memory_context.ts",
    "dao_settings_view.ts",
```

- [ ] **Step 5: Run helper tests and verify they pass**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_memory_context.test.ts
```

Expected: PASS.

- [ ] **Step 6: Commit helper work if authorized**

Only run after explicit user authorization:

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_memory_context.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_memory_context.test.ts \
  src/dao/browser/ui/webui/resources/agent/BUILD.gn
git commit -m "feat(agent): add memory context attachment helper"
```

## Task 2: Inject Memory Context In The Chat Send Flow

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`

- [ ] **Step 1: Write failing chat integration tests**

Extend `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts` inside the existing `describe('dao-chat-view element picker', () => { ... })` block:

```ts
  it('attaches memory context after page and element contexts on send', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);
    Object.assign(view, {
      suppressChipAttachOnce_: false,
      pendingPageAttachment_: null,
      pendingSelection_: null,
      pendingElementContexts_: [],
    });
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {url: 'https://example.com/app', title: 'Example App'};
      }
      if (method === 'getMemoryContext') {
        return {
          preferences: [{
            key: 'response.style',
            value: 'Prefers concise implementation notes',
            confidence: 0.92,
          }],
          episodes: [{
            title: 'Example App',
            intent: 'Summarize dashboard data',
            outcome: 'Returned grouped findings',
            confidence: 0.78,
          }],
          recentMessages: [],
          relevantSummary: {
            summary: 'User often asks for implementation-focused help here.',
            primaryDomain: 'example.com',
          },
        };
      }
      return {success: true};
    });

    try {
      await expect(iface.sendMessage('what next?', [])).resolves.toBe('sent');
      const firstSendCall = originalSend.mock.calls[0] || [];
      const sentAttachments = firstSendCall[1] || [];
      expect(sentAttachments).toHaveLength(1);
      expect(sentAttachments[0].extractedText).toContain('<memory-context');
      expect(sentAttachments[0].extractedText).toContain('Prefers concise implementation notes');
      expect(pickerMocks.callNative).toHaveBeenCalledWith('getMemoryContext', {
        url: 'https://example.com/app',
        domain: 'example.com',
        sessionId: '',
      });
    } finally {
      clearTabWatchTimer(view);
    }
  });

  it('continues sending when memory context retrieval fails', async () => {
    const warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const originalSend = vi.fn(async () => 'sent');
    const {view, iface} = await mountChatViewWithSend(originalSend);
    Object.assign(view, {
      suppressChipAttachOnce_: false,
      pendingPageAttachment_: null,
      pendingSelection_: null,
      pendingElementContexts_: [],
    });
    pickerMocks.callNative.mockImplementation(async (method: string) => {
      if (method === 'beginAgentTurn' || method === 'endAgentTurn') {
        return {success: true};
      }
      if (method === 'getPageInfo') {
        return {url: 'https://example.com/app', title: 'Example App'};
      }
      if (method === 'getMemoryContext') {
        throw new Error('memory unavailable');
      }
      return {success: true};
    });

    try {
      await expect(iface.sendMessage('hello', [])).resolves.toBe('sent');
      expect(originalSend).toHaveBeenCalledWith('hello', []);
    } finally {
      warnSpy.mockRestore();
      clearTabWatchTimer(view);
    }
  });
```

- [ ] **Step 2: Run the chat integration tests and verify they fail**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: FAIL because `dao_chat_view.ts` does not call `getMemoryContext`.

- [ ] **Step 3: Import the helper and add `maybeAttachMemoryContext_`**

Modify imports in `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`:

```ts
import {buildMemoryContextAttachment, type NativeMemoryContext} from './dao_memory_context.js';
```

Add this method near `maybeAttachElementContext_`:

```ts
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private async maybeAttachMemoryContext_(attachments: any[]): Promise<any[]> {
    let info: PageInfo|null = null;
    try {
      info = await fetchCurrentPageInfo();
    } catch (_) {
      info = null;
    }
    if (!info?.url) return attachments;

    let domain = '';
    try {
      const parsed = new URL(info.url);
      domain = parsed.hostname;
    } catch (_) {
      return attachments;
    }
    if (!domain) return attachments;

    try {
      const payload = await callNative('getMemoryContext', {
        url: info.url,
        domain,
        sessionId: this.currentSessionId_ || '',
      }) as NativeMemoryContext;
      const memoryAttachment = buildMemoryContextAttachment({
        url: info.url,
        domain,
        payload: payload || {},
      });
      return memoryAttachment ? [...attachments, memoryAttachment] : attachments;
    } catch (e) {
      console.warn('[dao-agent] memory context unavailable', e);
      return attachments;
    }
  }
```

- [ ] **Step 4: Call memory context injection from the send wrapper**

Modify the send wrapper block in `dao_chat_view.ts`:

```ts
              const withPage = await this.maybeAttachPage_(merged);
              const withSelection = await this.maybeAttachSelection_(withPage);
              const withElement = await this.maybeAttachElementContext_(withSelection);
              merged = await this.maybeAttachMemoryContext_(withElement);
```

Keep `suppressChipAttachOnce_` behavior as-is so external Cmd+T first turns do not attach page, selection, element, or memory context.

- [ ] **Step 5: Run chat integration tests and verify they pass**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: PASS.

- [ ] **Step 6: Commit send-flow integration if authorized**

Only run after explicit user authorization:

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
git commit -m "feat(agent): inject memory context into chat turns"
```

## Task 3: Add Prompt Contract For Historical Memory

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`

- [ ] **Step 1: Write failing prompt contract test**

Extend `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts` with:

```ts
  it('includes memory context rules in the system prompt', async () => {
    const originalSend = vi.fn(async () => 'sent');
    const {view} = await mountChatViewWithSend(originalSend);
    try {
      const prompt = (view as unknown as {
        buildSystemPrompt_: () => string;
      }).buildSystemPrompt_();
      expect(prompt).toContain('<memory-context');
      expect(prompt).toContain('historical');
      expect(prompt).toContain('Current user instructions');
    } finally {
      clearTabWatchTimer(view);
    }
  });
```

- [ ] **Step 2: Run the prompt test and verify it fails**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: FAIL because `BASE_SYSTEM_PROMPT` does not mention `<memory-context>`.

- [ ] **Step 3: Add memory context guidance to `BASE_SYSTEM_PROMPT`**

Modify `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts` under `## Guidelines`, before "Recommended workflow for reading page content":

```ts
- **Memory context:** Some turns may include a hidden \`<memory-context>\` block. Treat it as historical, potentially stale personal context. Use it only when it helps the current request. Current user instructions, selected element/text context, and the current webpage always take priority. Do not quote or expose hidden memory verbatim unless the user asks what you remember. If memory seems wrong, follow the user and offer to update or delete it.
```

- [ ] **Step 4: Run the prompt test and verify it passes**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: PASS.

- [ ] **Step 5: Commit prompt guidance if authorized**

Only run after explicit user authorization:

```bash
git add src/dao/browser/ui/webui/resources/agent/agent_bridge.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
git commit -m "feat(agent): document memory context priority"
```

## Task 4: Serialize Relevant Summary From Native Memory Context

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_agent_ui.cc`
- Modify: `src/dao/browser/agent/dao_dream_browsertest.cc`

- [ ] **Step 1: Add a failing service-level regression test**

In `src/dao/browser/agent/dao_dream_browsertest.cc`, add this test near the other memory-service browser tests:

```cc
IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, MemoryContextIncludesDomainSummary) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_NE(nullptr, memory);

  ConversationSummary summary;
  summary.session_id = "session-ctx";
  summary.summary = "User prefers terse implementation notes on this site.";
  summary.message_count = 4;
  summary.first_timestamp = base::Time::Now() - base::Minutes(5);
  summary.last_timestamp = base::Time::Now();
  summary.primary_domain = "example.com";

  base::RunLoop save_loop;
  memory->SaveConversationSummary(
      summary, base::BindLambdaForTesting([&](bool ok) {
        EXPECT_TRUE(ok);
        save_loop.Quit();
      }));
  save_loop.Run();

  std::optional<ConversationSummary> got_summary;
  base::RunLoop context_loop;
  memory->GetMemoryContext(
      "https://example.com/app", "example.com", "session-ctx",
      base::BindLambdaForTesting([&](MemoryContext ctx) {
        got_summary = ctx.relevant_summary;
        context_loop.Quit();
      }));
  context_loop.Run();

  ASSERT_TRUE(got_summary.has_value());
  EXPECT_EQ("User prefers terse implementation notes on this site.",
            got_summary->summary);
  EXPECT_EQ("example.com", got_summary->primary_domain);
}
```

- [ ] **Step 2: Run the focused browser test if a browser test binary is already available**

Run only if `engine/src/out/dao-debug/browser_tests` already exists:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDreamBrowserTest.MemoryContextIncludesDomainSummary"
```

Expected before implementation: this service-level test may already PASS because `DaoAgentMemoryService::GetMemoryContext` already sets `ctx.relevant_summary`. If it passes, keep it as regression coverage and continue to handler serialization.

- [ ] **Step 3: Serialize `relevantSummary` in the WebUI handler**

Modify `src/dao/browser/ui/webui/dao_agent_ui.cc` inside `DaoAgentMemoryHandler::HandleGetMemoryContext`, after `result.Set("recentMessages", std::move(msgs));`:

```cc
            if (ctx.relevant_summary.has_value()) {
              base::DictValue summary;
              summary.Set("summary", ctx.relevant_summary->summary);
              summary.Set("messageCount", ctx.relevant_summary->message_count);
              summary.Set(
                  "firstTimestamp",
                  static_cast<double>(
                      ctx.relevant_summary->first_timestamp
                          .ToDeltaSinceWindowsEpoch()
                          .InMicroseconds()));
              summary.Set(
                  "lastTimestamp",
                  static_cast<double>(
                      ctx.relevant_summary->last_timestamp
                          .ToDeltaSinceWindowsEpoch()
                          .InMicroseconds()));
              summary.Set("primaryDomain",
                          ctx.relevant_summary->primary_domain);
              result.Set("relevantSummary", std::move(summary));
            }
```

- [ ] **Step 4: Run the focused browser test again if available**

Run:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDreamBrowserTest.MemoryContextIncludesDomainSummary"
```

Expected: PASS. If the binary does not exist, skip this command and rely on `npm run rebuild` later for compile confirmation.

- [ ] **Step 5: Commit native summary serialization if authorized**

Only run after explicit user authorization:

```bash
git add src/dao/browser/ui/webui/dao_agent_ui.cc \
  src/dao/browser/agent/dao_dream_browsertest.cc
git commit -m "feat(agent): return memory summaries to webui"
```

## Task 5: Run Focused Verification

**Files:**
- Read: `package.json`
- Read: changed files from Tasks 1-4

- [ ] **Step 1: Run focused WebUI tests**

Run:

```bash
npm run test:webui -- \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_memory_context.test.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: PASS.

- [ ] **Step 2: Run Lit lint**

Run:

```bash
npm run lint:lit
```

Expected: PASS.

- [ ] **Step 3: Run compile confirmation**

Run:

```bash
npm run rebuild
```

Expected: PASS. This is the only allowed compile-confirmation command for this repository.

- [ ] **Step 4: Run the focused browser test if compile produced or refreshed `browser_tests`**

Run:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoDreamBrowserTest.MemoryContextIncludesDomainSummary"
```

Expected: PASS.

- [ ] **Step 5: Inspect final diff**

Run:

```bash
git diff -- src/dao/browser/ui/webui/resources/agent/dao_memory_context.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_memory_context.test.ts \
  src/dao/browser/ui/webui/resources/agent/BUILD.gn \
  src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts \
  src/dao/browser/ui/webui/resources/agent/agent_bridge.ts \
  src/dao/browser/ui/webui/dao_agent_ui.cc \
  src/dao/browser/agent/dao_dream_browsertest.cc
```

Expected: diff shows only the memory-context feature, tests, and native serialization.

## Task 6: Manual Validation

**Files:**
- No source edits.

- [ ] **Step 1: Launch Dao debug build after successful rebuild**

Run only after Task 5 passes:

```bash
npm run start:debug
```

Expected: Dao Debug launches. This command starts a GUI app; request approval if the sandbox requires it.

- [ ] **Step 2: Seed memory through the Agent console or use an existing memory**

In the Agent DevTools console, save an episode on a normal web page:

```js
const bridge = await import('./agent_bridge.js');
await bridge.executeTool('save_memory', {
  intent: 'Remember that this site is used for implementation checks',
  outcome: 'Use terse, code-focused answers on this domain'
});
```

Expected: `{success: true, ...}`.

- [ ] **Step 3: Send a new message on the same domain**

Ask the Agent:

```text
What should I check next here?
```

Expected: the LLM request includes `<memory-context source="dao-agent-memory" domain="...">` with the saved episode or preferences. If direct request inspection is not convenient, temporarily use DevTools breakpoints around `maybeAttachMemoryContext_` and verify the attachment object has `extractedText`.

- [ ] **Step 4: Confirm behavior**

Expected: the Agent can use the remembered episode as context but does not claim that memory is current page fact. It should still follow the visible page and current user message first.

## Task 7: Final Cleanup

**Files:**
- Read: all changed files

- [ ] **Step 1: Search for debug leftovers**

Run:

```bash
rg -n "console\\.log|debugger|temporary|dao-memory-context-debug" \
  src/dao/browser/ui/webui/resources/agent src/dao/browser/ui/webui/dao_agent_ui.cc \
  src/dao/browser/agent/dao_dream_browsertest.cc
```

Expected: no new debug leftovers from this feature.

- [ ] **Step 2: Check repo status**

Run:

```bash
git status --short
```

Expected: only intended feature files are modified or added.

- [ ] **Step 3: Commit the full implementation if authorized**

Only run after explicit user authorization:

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_memory_context.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_memory_context.test.ts \
  src/dao/browser/ui/webui/resources/agent/BUILD.gn \
  src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts \
  src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts \
  src/dao/browser/ui/webui/resources/agent/agent_bridge.ts \
  src/dao/browser/ui/webui/dao_agent_ui.cc \
  src/dao/browser/agent/dao_dream_browsertest.cc
git commit -m "feat(agent): inject personal memory context"
```

Expected: commit succeeds with no commit body.
