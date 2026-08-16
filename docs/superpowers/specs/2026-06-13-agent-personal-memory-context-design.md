# Agent Personal Memory Context - Design

**Date:** 2026-06-13
**Status:** Approved design, pending implementation plan

## Summary

Dao already has the hard parts of a personal agent: long-term memory,
page episodes, preferences, proactive suggestions, skills, and Dream
Analysis. The missing loop is online use. A memory can only make the agent
"understand you better over time" if relevant memories are present in the
model context at the moment the user asks for help.

This design adds a low-disruption memory context injection path to the Agent
chat send flow. Before each user message is sent to the LLM, the WebUI asks
the existing native memory service for relevant memories and attaches a hidden
`<memory-context>` block to the LLM request. The block is not shown as chat
text. It contains high-confidence preferences, a current-domain summary, a
small number of current-domain episodes, and recent same-session continuity
when available.

The first implementation does not create a new memory system. It makes the
existing one useful during live turns.

## Goals

- Make repeated use of Dao visibly improve the Agent's answers and actions.
- Reuse the existing `DaoAgentMemoryService`, `DaoAgentMemoryStore`,
  `Preference`, `Episode`, `ConversationSummary`, and Dream habit pipeline.
- Inject relevant memory into each LLM turn without cluttering the chat UI.
- Keep current user instructions and current page content higher priority
  than historical memory.
- Keep privacy controls simple: memory disabled means no memory context
  injection.
- Add tests around the send-time context builder and native serialization.

## Non-goals

- No new database tables in v1.
- No new standalone memory UI in v1.
- No automatic SOUL.md rewrite from Dream habits in v1.
- No automatic skill or scenario generation in v1.
- No browsing history scan during normal chat sends.
- No inclusion of Dream debug material in ordinary chat context.
- No forced writeback after every turn.

## Current State

The backend already exposes a combined memory query:

- `DaoAgentMemoryService::GetMemoryContext(url, domain, session_id)`
  returns up to five high-confidence preferences, up to three domain
  episodes, recent messages for the current session, and a relevant domain
  summary.
- `DaoAgentMemoryHandler::HandleGetMemoryContext` serializes this query to
  WebUI, but currently omits the relevant summary from the response.
- `save_memory` already writes page episodes through `HandleSaveEpisode`.
- Dream Analysis already merges new and reinforced habits into preferences
  through `MergePreference`.
- The Agent WebUI send path already intercepts `agent-interface.sendMessage`
  and appends hidden page, selection, and element-context attachments before
  passing the request into pi-web-ui.

The main gap is that the WebUI does not currently attach memory context to
normal chat turns.

## Product Behavior

When memory is enabled and the user sends a message, Dao quietly retrieves
relevant personal context and makes it available to the model. The user's
chat bubble stays clean. The agent may use this context to:

- remember durable preferences, such as preferred response style or recurring
  interests;
- remember how similar tasks were handled on the same domain;
- avoid asking for background the user already gave in prior sessions;
- make better choices about tool usage and explanation depth.

The agent must treat memory as a hint, not a source of truth. The current
user message, selected element context, selected text, and current page
snapshot always win over historical memory.

## Architecture

### 1. WebUI Context Builder

Add a `maybeAttachMemoryContext_` helper in `dao_chat_view.ts`, called from
the same send-message interception path as `maybeAttachPage_`,
`maybeAttachSelection_`, and `maybeAttachElementContext_`.

Responsibilities:

- discover the current page URL, title, and domain;
- discover or reuse the current Agent session id;
- call `callNative('getMemoryContext', url, domain, sessionId)`;
- skip injection if memory is disabled, native returns an empty object, the
  current URL has no useful domain, or the native call fails;
- render a bounded `<memory-context>` string;
- append it as a hidden attachment whose extracted text is spliced into the
  LLM request by the existing `convertToLlm` path.

The helper should be independent enough to unit test without mounting the
whole browser.

### 2. Native Retrieval

Reuse `DaoAgentMemoryService::GetMemoryContext`. Keep the existing limits for
v1 unless tests show they are too noisy:

- preferences: top 5 with confidence at least 0.6;
- episodes: up to 3 for the current domain;
- recent messages: up to 20 only for the active session;
- summary: one relevant summary by current domain.

Update `DaoAgentMemoryHandler::HandleGetMemoryContext` to serialize
`relevantSummary` in addition to `preferences`, `episodes`, and
`recentMessages`.

### 3. Existing Writeback

Continue to use the current tool-driven write path:

- `save_memory` records useful completed tasks as episodes;
- Dream Analysis writes habit candidates into preferences;
- explicit user behavior or personality changes continue through
  `update_soul` or preference updates.

The base system prompt already tells the model to call `save_memory` after
meaningful tasks. V1 should not add an automatic end-of-turn summarizer. That
can come later after the read path is proven useful.

## Data Flow

```text
User sends message
  -> dao_chat_view.ts sendMessage wrapper
  -> beginAgentTurn
  -> refresh model, tools, and system prompt
  -> maybeAttachPage_
  -> maybeAttachSelection_
  -> maybeAttachElementContext_
  -> maybeAttachMemoryContext_
       -> callNative('getMemoryContext', url, domain, sessionId)
       -> native DaoAgentMemoryHandler
       -> DaoAgentMemoryService::GetMemoryContext
       -> DaoAgentMemoryStore queries
       -> WebUI receives compact memory payload
       -> WebUI builds <memory-context>
  -> original pi-web-ui sendMessage
  -> convertToLlm splices attachment extractedText into LLM turn
  -> endAgentTurn
```

`maybeAttachMemoryContext_` should run after current-page and selected
context attachment so the final LLM input naturally reads as: current page,
selected user context, remembered personal context, then the user's message.
The system prompt will still state that current user input overrides memory.

## Memory Context Format

The injected text should be compact and explicit:

```xml
<memory-context source="dao-agent-memory" domain="example.com">
  <instruction>
    These are historical hints. Use them only when relevant. Current user
    instructions and current page content take priority.
  </instruction>
  <preferences>
    <preference key="response.style" confidence="0.92">...</preference>
  </preferences>
  <summary domain="example.com">...</summary>
  <episodes>
    <episode confidence="0.77" title="...">
      <intent>...</intent>
      <outcome>...</outcome>
    </episode>
  </episodes>
</memory-context>
```

The exact string can be generated without an XML parser as long as text is
escaped safely for `<`, `>`, `&`, and quotes in attributes.

## Token Budget And Ranking

The memory context should be capped to roughly 1200-1800 tokens in v1. Since
the implementation will count characters rather than tokens, use a
conservative character budget, such as 6000-8000 characters.

Priority order:

1. High-confidence global preferences.
2. Current-domain conversation summary.
3. Current-domain episodes.
4. Recent same-session messages.

When trimming, preserve structure and metadata first, then truncate long
values. Prefer fewer complete memories over many half-cut snippets.

## Prompt Contract

Add a short memory rule to `BASE_SYSTEM_PROMPT`:

- memory context is historical and may be stale;
- use it only when relevant to the current task;
- never contradict the current user request because of memory;
- do not expose hidden memory verbatim unless the user asks what is
  remembered;
- if memory seems wrong, follow the user and optionally offer to update or
  delete it.

This keeps model behavior aligned even when the injected block contains a
strong-looking but outdated habit.

## Privacy And Controls

- If `kDaoAgentMemoryEnabled` is false, no memory context is injected.
- Empty results are skipped.
- Native call failures are swallowed after logging a warning.
- Dream debug material is never included.
- The helper does not query browsing history directly.
- The memory block is not rendered in the visible chat bubble.
- `clearAllMemory` and existing memory settings continue to control the data
  source.

## Error Handling

| Failure | Behavior |
|---|---|
| Native call rejects | Log a warning, send the message without memory. |
| Memory disabled | Native returns empty or disabled state; skip injection. |
| No useful URL/domain | Skip injection. |
| Empty preferences, summary, episodes, and recent messages | Skip injection. |
| Malformed native payload | Ignore malformed sections and keep valid ones. |
| Oversized payload | Trim to budget before attaching. |

The user message must never fail merely because personal memory retrieval
failed.

## Testing

### WebUI unit tests

Extend `dao_chat_view.test.ts`:

- sends normally when `getMemoryContext` returns empty data;
- appends a `<memory-context>` attachment when preferences or episodes exist;
- includes `relevantSummary` when native returns it;
- escapes XML-sensitive characters;
- trims oversized memory content;
- continues sending when `getMemoryContext` rejects.

The existing send-message wrapper tests already mock `callNative`, so this
surface is a good fit.

### Native/browser tests

Add or extend a focused test for `HandleGetMemoryContext` serialization:

- seed a preference, episode, and conversation summary;
- call `getMemoryContext`;
- verify the response includes preferences, episodes, and
  `relevantSummary`.

If direct WebUI handler testing is awkward, extend an existing agent browser
test with the smallest native round trip that proves the serialized response.

### Manual verification

1. Enable Agent memory.
2. Save or seed an episode for a domain.
3. Open a page on the same domain.
4. Send a new Agent message.
5. Inspect the LLM request path or test instrumentation to confirm the
   `<memory-context>` block is present.
6. Confirm the Agent can use the prior episode without the user restating it.

## Implementation Surface

Expected files:

- `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
- `src/dao/browser/ui/webui/dao_agent_ui.cc`
- `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`
- possibly an existing C++ browser test under `src/dao/browser/agent/`

No changes are expected under `engine/`.

## Future Phases

After this read path is proven useful:

- add a conservative automatic episode summarizer after successful multi-step
  tasks;
- add a memory inspection chip or settings surface that explains what was
  used in the current answer;
- allow confirmed Dream habits to suggest SOUL.md updates;
- allow repeated domain episodes to propose user skills or personal
  scenarios.

These are intentionally outside v1 so the first release stays focused on the
core loop: existing memory enters live reasoning.

## Success Criteria

- A saved current-domain episode appears in the next same-domain LLM request
  as part of `<memory-context>`.
- High-confidence preferences appear in the LLM request when memory is
  enabled.
- Empty or failed memory retrieval does not block sending a user message.
- Current page and current user instructions remain higher priority than
  memory.
- Tests prove attachment generation, trimming, failure fallback, and native
  summary serialization.
