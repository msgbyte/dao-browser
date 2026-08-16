# Agent Message Rewind Design

## Summary

Add a manual rewind affordance to older assistant replies in the Dao Agent
chat surface. The action keeps the assistant reply the user clicked and removes
every message after it, letting the user continue the conversation from that
point with a new question or extra context.

The latest assistant reply does not show this action because there are no later
messages to remove.

## Goals

- Let users branch from an earlier assistant reply without starting a new
  session.
- Keep the interaction local, predictable, and user controlled.
- Persist the truncated conversation so session history matches what the user
  sees after refresh or resume.
- Reuse the existing assistant message action row pattern.

## Non-Goals

- Do not create a model-callable tool.
- Do not create multiple conversation branches or a branch picker.
- Do not regenerate automatically after rewinding.
- Do not add an undo stack in the first version.
- Do not change native C++ Agent plumbing.

## User Experience

Each non-latest assistant message gets a rewind icon button in its existing
action row. The recommended order is:

```text
Copy / Share / Regenerate / Rewind
```

The first three actions operate on the selected reply. Rewind changes the
conversation state after that reply, so it belongs at the end of the row.

Clicking rewind on assistant message `A` keeps all messages up to and including
`A`, removes every later user, assistant, and tool-result message, focuses the
composer, and lets the user type a new follow-up.

No confirmation dialog is shown in the first version. The action is explicit,
scoped to an older reply, and only appears when it can remove later messages.

## Availability Rules

- Show rewind only for assistant messages that are not the last assistant
  message in the current conversation.
- Hide rewind for the latest assistant message.
- Disable rewind while the agent is streaming, matching the existing message
  action disabled state.
- Do not show rewind on user messages.
- Do not show rewind for non-visible internal tool-result messages.

## Architecture

The feature lives in the Agent WebUI layer, primarily in
`src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`.

`DaoChatView` already decorates pi-web-ui-rendered `assistant-message` nodes
with Dao-owned action rows. Rewind should extend that decoration path instead
of changing the vendor chat components.

The action uses the existing canonical message array:

```text
agent.state.messages
```

When a rewind button is clicked for assistant message index `i`, the next state
is:

```text
agent.state.messages = agent.state.messages.slice(0, i + 1)
```

The array must be replaced, not mutated in place, so pi-web-ui and Lit observe
the update.

## Components

### Assistant Action Row

Extend `buildAssistantActionRow_` to optionally include a rewind button. The
row builder needs to know whether the current assistant message has any later
assistant reply. Passing this as a boolean keeps the row builder simple and
keeps message ordering logic in `refreshMessageActions_`.

### Rewind Handler

Add a private handler that:

1. Finds the assistant message by Dao message id.
2. Verifies the message is still an assistant message.
3. Verifies it is not the latest assistant message.
4. Replaces `agent.state.messages` with the truncated slice.
5. Clears transient edit/debug menus that may point to removed messages.
6. Requests the pi-web-ui agent interface to update.
7. Updates message metadata and token estimates.
8. Saves the current session.
9. Focuses the composer.

The handler must not call `agent.continue()`.

### Icon And I18n

Use a Lucide rewind-style icon copied from the current upstream SVG child nodes
when implementing. Add localized strings through the Agent WebUI i18n system:

- `chat.message_actions.rewind_tooltip`: `Rewind to this response`

Only the English source and hand-authored `zh-CN` strings should be updated by
the implementation. Do not run `i18n.sh`.

## Data Flow

Before:

```text
U1 -> A1 -> U2 -> A2 -> U3 -> A3
```

Click rewind under `A1`:

```text
U1 -> A1
```

The next user message appends after `A1` as the normal next turn:

```text
U1 -> A1 -> U-new -> A-new
```

The current session id is preserved. The stored session is overwritten with the
truncated message list.

## Persistence

After truncating messages, call the existing session-save path so IndexedDB
history matches the visible chat state. Session metadata should keep the
existing title and session id, matching current save behavior for edited and
continued sessions.

## Error Handling

- If the message id cannot be found, no-op.
- If the found message is not an assistant message, no-op.
- If the selected assistant message is already the latest assistant reply,
  no-op.
- If session save fails, keep the visible truncated state. This matches the
  existing best-effort persistence behavior.
- If the agent is streaming, the button is disabled and the handler should
  return without changing state.

## Testing

### WebUI Unit Tests

Extend `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`
with focused coverage:

- Older assistant messages render a rewind button.
- The latest assistant message does not render a rewind button.
- Clicking or invoking rewind keeps the selected assistant message and removes
  all later messages.
- Rewind does not call `agent.continue()`.
- Rewind updates the pi-web-ui interface and saves the current session.
- Rewind no-ops for the latest assistant message.

### Browser Test

Extend `DaoAgentAssistantActionsTest` in
`src/dao/browser/ui/views/dao_browser_browsertest.cc` to verify that older
assistant rows include rewind and the latest assistant row does not.

### Verification Commands

For WebUI-only implementation, run:

```bash
npm run test:webui
npm run lint:lit
```

No Chromium compile confirmation is required unless the implementation expands
beyond WebUI files. If C++ or Chromium integration files change, the only
allowed compile confirmation is:

```bash
npm run rebuild
```
