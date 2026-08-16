# Dao Agent Message Edit and Message Actions - Design

**Date:** 2026-06-18
**Status:** Approved design, pending implementation plan

## Summary

Dao Agent already persists chat sessions and renders assistant actions for the
latest assistant message. This design adds message-level editing and
message-level assistant actions inside the current conversation.

The selected scope is the current conversation's message history, not the
existing `Chat history` session list. Users can edit a prior user message,
review that message's previous versions, and regenerate from the edited turn.
Each assistant message can also be copied, copied as a share image, or
regenerated from its paired user prompt.

The first implementation keeps this behavior in the Agent WebUI session state.
It does not introduce a new native database, a branch tree, or changes under
`engine/`.

## Goals

- Allow users to edit any previous user message in the current conversation.
- Preserve older versions of edited user messages for lightweight review.
- Match the current Dao Agent visual pattern: actions appear below the bubble,
  outside the message body.
- Allow every assistant message to expose `Copy`, `Image`, and `Regenerate`,
  instead of only the latest assistant reply.
- Keep the behavior simple and predictable: editing or regenerating truncates
  later messages and generates a new continuation.
- Persist edits and version metadata with the existing IndexedDB session.
- Cover the behavior with focused WebUI tests and targeted browser tests.

## Non-goals

- No conversation branch tree in v1.
- No side-by-side comparison of old and new assistant branches in v1.
- No new C++ memory table or native conversation store.
- No rewrite of the existing `dao_chat_history_panel.ts` session-history panel.
- No attachment re-editing in v1. Attachments are preserved and included in
  version snapshots, but the inline editor only changes text.
- No automatic translation pass for generated locale files.
- No edits to generated vendor files under
  `src/dao/browser/ui/webui/resources/agent/vendor/`.

## Current State

Relevant existing pieces:

- `dao_chat_view.ts` owns the chat surface, send-message interception,
  session resume, session saving, assistant copy/share/retry actions, and
  current-page/selection/element attachment handling.
- `dao_chat_history_panel.ts` lists saved sessions through pi-web-ui's
  `SessionsStore`. This is session history, not the message-version history
  covered by this design.
- `pi_app_storage.ts` initializes pi-web-ui's IndexedDB-backed `AppStorage`.
  `saveCurrentSession_()` persists `agent.state` through
  `storage.sessions.saveSession(...)`.
- `dao_share_image.ts` renders a user question, optional page source, and
  assistant answer into a branded PNG for clipboard sharing.
- `refreshAssistantActions_()` currently injects one action row after the last
  `assistant-message` element. It provides copy text, copy as image, and retry.
- `retryLastAssistant_()` currently finds the latest user message, truncates
  after it, and calls `agent.continue()`.

The main gap is identity. Current action helpers infer the latest Q/A pair by
walking backward from the end of the message list. Message-level actions need
stable IDs and helpers that can act on a specific user or assistant message.

## Design Brief

Product behavior:

- The feature is for Dao Agent's chat view.
- Visual style should match the existing Dao Agent sidebar UI.
- Interactivity should be fully functional in the production WebUI.

Confirmed choices:

- Scope: current conversation message history.
- Editing behavior: edit a user message, truncate everything after it, and
  regenerate from the edited message.
- Assistant actions: every assistant message gets its own `Copy`, `Image`, and
  `Regenerate` actions.
- Placement: actions are below the bubble, outside the bubble body.

## User Experience

### User Message Actions

User-message actions are rendered below each user bubble, right aligned:

```text
                       [user message bubble]
                              Edit   Edited - v2
```

Rules:

- `Edit` is available for user and `user-with-attachments` messages.
- `Edited - vN` appears only after the message has at least one previous
  version.
- `Edited - vN` opens a lightweight popover anchored to that version label.
- Version history shows previous text snapshots and edit timestamps. It is
  read-only in v1.
- User-message action buttons are disabled while the agent is streaming. If an
  editor was already open before streaming started and the user saves during the
  stream, the save flow aborts the stream before mutating the conversation.

### Inline Edit State

Clicking `Edit` turns the original user bubble area into an editor:

- prefilled with the current user-visible text;
- same width and alignment as the original bubble;
- `Save` and `Cancel` controls below the editor;
- keyboard behavior: Escape cancels, Cmd/Ctrl+Enter saves;
- empty or whitespace-only saves are rejected with an inline/error button
  feedback.

Attachments remain displayed as read-only tiles during editing. The first
implementation does not support adding, removing, or editing attachments inside
this inline editor.

### Assistant Message Actions

Assistant actions are rendered below each assistant bubble, left aligned:

```text
[assistant message bubble]
Copy   Image   Regenerate
```

Rules:

- `Copy` copies the selected assistant message's visible text as both
  `text/plain` and `text/html` where supported.
- `Image` renders a share PNG from the selected assistant message, its nearest
  preceding user message, and the first Dao page-source attachment on that user
  message when present.
- `Regenerate` finds the nearest preceding user/user-with-attachments message,
  truncates the conversation after that user message, and calls
  `agent.continue()`.
- Actions are disabled while streaming.
- Tool-only assistant messages with no visible text do not receive an action
  row. If a row exists but text extraction returns empty, `Copy` and `Image`
  flash `Empty`.

## Alternatives Considered

### 1. Inline Actions Everywhere

All actions appear inside each message bubble. This is highly discoverable but
adds too much visual density in the narrow Agent sidebar.

### 2. Menu-first Actions

Each message gets a small more button, and actions live in a menu. This keeps
the transcript clean, but common operations require an extra click and are less
obvious.

### 3. Hybrid Actions Below Bubbles

The approved direction. Common assistant actions stay one click away below the
assistant bubble. User edit/version actions stay below user bubbles and only
show version history when relevant. This preserves the current Dao Agent action
row feel and keeps bubble content clean.

## Data Model

Store Dao-specific message metadata inside `agent.state.messages` so it persists
with existing sessions:

```ts
interface DaoMessageMetadata {
  id: string;
  editedAt?: string;
  editHistory?: DaoUserMessageVersion[];
}

interface DaoUserMessageVersion {
  content: unknown;
  attachments?: unknown[];
  editedAt: string;
}

interface DaoMessageWithMetadata {
  role: string;
  content: unknown;
  attachments?: unknown[];
  dao?: DaoMessageMetadata;
}
```

Rules:

- Every user, `user-with-attachments`, assistant, and toolResult message should
  receive a `dao.id` when it is first observed without one.
- `dao.id` is generated with `crypto.randomUUID()` when available, with a
  timestamp-based fallback for tests or older WebUI contexts.
- `editHistory` lives only on user and `user-with-attachments` messages.
- Each edit pushes the previous `content` and `attachments` into `editHistory`
  before replacing the current content.
- Edit history is UI-only. It is not included in `convertToLlm`.
- Persisting remains the responsibility of existing session save flow.

This model intentionally avoids changing pi-web-ui's storage schema. The saved
session is still the serialized agent state, only with additional metadata on
messages.

## Core Helpers

The implementation should add small helpers in `dao_chat_view.ts` rather than
growing latest-message-specific logic:

- `ensureMessageIds_()`: assigns missing `dao.id` values to current messages.
- `findMessageIndexByDaoId_(id)`: resolves a UI action target to the message
  array index.
- `isUserMessage_(msg)`: true for `user` and `user-with-attachments`.
- `isAssistantMessage_(msg)`: true for visible assistant messages.
- `extractVisibleText_(msg)`: generalized version of the current assistant
  text extraction helper.
- `findPromptForAssistant_(assistantIndex)`: returns the nearest preceding user
  message and optional Dao page source.
- `truncateAfterUserIndex_(userIndex)`: replaces `agent.state.messages` with a
  slice ending at that user message.
- `requestAgentRerender_()`: centralizes the `agent-interface.requestUpdate()`,
  metadata sync, decoration, and save scheduling needed after message mutation.

The goal is to make copy/image/regenerate/edit share the same targeting path
and avoid hidden "last message" assumptions.

## Data Flow

### Editing a User Message

```text
User clicks Edit below a user bubble
  -> dao_chat_view enters inline edit state for dao.id
  -> user saves new text
  -> if streaming: agent.abort()
  -> find message by dao.id
  -> append previous content/attachments to editHistory
  -> replace current content with edited text
  -> truncate messages after this user message
  -> assign/sync message IDs
  -> request agent-interface render
  -> agent.continue()
  -> scheduleSaveSession_()
```

If `agent.continue()` fails, the edited message remains. The user should see the
normal agent error path and can retry/regenerate again.

### Regenerating a Specific Assistant Message

```text
User clicks Regenerate below an assistant bubble
  -> find assistant by dao.id
  -> find nearest preceding user/user-with-attachments
  -> action is disabled while streaming
  -> truncate messages after that user message
  -> request agent-interface render
  -> agent.continue()
  -> scheduleSaveSession_()
```

This is the same mental model as editing: choose an earlier point in the
conversation, discard the later continuation, and generate a new continuation.

### Copying a Specific Assistant Message

```text
User clicks Copy below an assistant bubble
  -> find assistant by dao.id
  -> extract visible assistant text
  -> render Markdown to HTML
  -> write text/html and text/plain to clipboard when supported
  -> fall back to writeText
  -> flash copied/failed state on that button
```

### Copying a Specific Assistant Message as Image

```text
User clicks Image below an assistant bubble
  -> find assistant by dao.id
  -> find nearest preceding user/user-with-attachments
  -> extract user question text
  -> find first Dao page-source attachment on that user message
  -> renderShareImage({ question, source, answer })
  -> write image/png ClipboardItem
  -> flash copied/failed state on that button
```

## Rendering Strategy

The current code manually decorates rendered `assistant-message` elements. V1
can keep this pattern if it is made message-aware:

- assign message IDs before render;
- locate rendered user and assistant message elements in DOM order;
- pair each rendered bubble with the corresponding message index;
- inject action rows as siblings immediately after the message element;
- make injection idempotent by stamping the row with the target `dao.id`;
- remove stale rows before each decoration pass.

If pi-web-ui exposes a stable message property on rendered elements, prefer that
over DOM-order pairing. If not, DOM-order pairing is acceptable for v1 because
the message list is a direct projection of `agent.state.messages` in order.

The design should not put action buttons inside the bubble body. CSS should
preserve the current lightweight action-row feel:

- assistant rows: below bubble, left aligned;
- user rows: below bubble, right aligned;
- compact icon or text+icon buttons may be used, but labels/tooltips must be
  localized;
- rows should not shift bubble width or affect message text wrapping.

## Persistence

All edits and metadata persist through the existing session flow:

- `saveCurrentSession_()` continues calling `storage.sessions.saveSession(...)`;
- loaded sessions call `ensureMessageIds_()` after hydration so older sessions
  without `dao.id` still work;
- existing session titles remain unchanged after editing, matching the current
  "preserve user rename" behavior.

The first implementation does not need a storage migration because missing
metadata can be lazily added when sessions load or messages mutate.

## Internationalization

Add new user-facing strings to `i18n/locales/en.ts` and hand-authored
`i18n/locales/zh-CN.ts`.

Likely keys:

- `chat.message_actions.copy_tooltip`
- `chat.message_actions.share_tooltip`
- `chat.message_actions.regenerate_tooltip`
- `chat.message_actions.edit_tooltip`
- `chat.message_actions.edit_history_tooltip`
- `chat.message_actions.save_edit`
- `chat.message_actions.cancel_edit`
- `chat.message_actions.edited_version`
- `chat.message_actions.empty_edit`
- `chat.message_actions.regenerating`

Existing keys should be reused where they already match. Do not run `i18n.sh`
automatically.

## Error Handling and Edge Cases

- If a targeted message ID no longer exists, do nothing and optionally flash
  `Failed`.
- If the agent is streaming, message actions should be disabled. Save from an
  already-open editor may abort the stream before applying the edit.
- If an edited message has no visible text after trimming, reject the save.
- If `agent.continue()` throws, keep the edited/truncated conversation and rely
  on the existing agent error path.
- If clipboard rich write fails, fall back to text-only copy for `Copy`.
- If image clipboard APIs are unavailable, flash `Failed`.
- If an assistant message has no visible text, `Copy` and `Image` flash `Empty`.
- If a user message has attachments, version history preserves the previous
  attachment array with the previous content.
- If an old session has no metadata, IDs are lazily assigned on load before
  actions are injected.

## Testing

### WebUI Unit Tests

Extend `dao_chat_view.test.ts` with focused tests for:

- `ensureMessageIds_()` adds stable IDs without replacing existing IDs.
- Editing a user message appends the previous version to `editHistory`.
- Editing truncates every message after the edited user turn.
- Editing calls `agent.continue()` after truncation.
- Regenerating a specific assistant uses its nearest preceding user message,
  not the latest user message.
- Copying a specific assistant copies that assistant's visible text.
- Copying as image builds the Q/A pair for the selected assistant and its
  nearest preceding user message.
- Loaded sessions without metadata receive IDs and can show action rows.

Extend `dao_share_image.test.ts` only if the renderer needs new behavior. The
preferred change is to keep `renderShareImage` unchanged and test the
message-targeting helper that feeds it.

### Browser Tests

Update or add a focused browser test near `DaoAgentAssistantActionsTest`:

- multiple assistant bubbles receive one action row each;
- the row is below the bubble, not inside it;
- user bubbles receive an edit row below the bubble;
- injection remains idempotent after repeated refreshes.

### Manual Verification

After implementation:

- Open Dao Agent and send a multi-turn conversation.
- Edit the first user message and verify all later messages disappear and a new
  answer streams.
- Open `Edited - vN` and verify old text is visible.
- Copy and copy-image from an older assistant reply and verify the selected
  reply, not the latest reply, is used.
- Regenerate an older assistant reply and verify truncation starts from its
  paired user message.

## Verification Commands

For WebUI-only implementation, run:

```bash
npm run test:webui
npm run lint:lit
```

If the implementation touches C++ or Chromium integration, compile
confirmation must use:

```bash
npm run rebuild
```

Do not use other compile paths as substitutes.
