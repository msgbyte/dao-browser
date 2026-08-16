# Agent Empty-Session Guide Placeholder

## Context

When the user opens the right-side agent panel on a freshly created (or
freshly cleared) conversation, the chat area is visually empty — the
`pi-chat-panel` component renders only its composer at the bottom and the
scroll region above it is blank. First-time users get no cue about what
the agent can do; returning users get a jarring wall of whitespace.

This spec adds a lightweight welcome placeholder that appears only when
the current session has zero messages, and fades out the moment the user
sends their first message.

## Goals

- Fill the empty scroll region with a minimal welcome cue.
- Never interfere with the composer input — the user must still be able
  to click/focus the textarea, paste attachments, and open the skill
  picker without the overlay swallowing events.
- Disappear automatically when the conversation becomes non-empty and
  stay hidden during streaming, compaction, and any non-empty state.

## Non-Goals

- No example-prompt cards, no feature tour, no capability list. The
  placeholder is *calm* — a single icon, one headline, one hint line.
- No new browser test. The behavior is a pure template condition; visual
  QA covers it.
- No i18n scaffolding. Copy ships as English strings alongside the rest
  of the agent WebUI.

## Design

### Placement

Rendered inside `dao-chat-view`'s template as a sibling of
`<pi-chat-panel>`, gated by a condition derived from existing component
state. `dao-chat-view` is already a flex column; the overlay uses
absolute positioning so it does not participate in flex sizing and does
not shrink the composer.

The host (`dao-chat-view`) needs `position: relative` so the absolute
child anchors correctly. It is currently a plain flex container without
an explicit `position`, so the rule is added to the component's scoped
`<style>` block in `render()`.

### Visibility Condition

Show the overlay when **all** of the following hold:

- `this.messageCount_ === 0`
- `!this.isStreaming_`
- `!this.compacting_`

`messageCount_` is already maintained by `dao_chat_view.ts` from pi's
`message_end` events and from session restore. `isStreaming_` and
`compacting_` are existing reactive fields used by the compact bar.

### Layout and Events

```
.dao-empty-guide {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 24px;
  pointer-events: none;   /* clicks fall through to pi-chat-panel */
  opacity: 1;
  transition: opacity 0.2s ease;
}
.dao-empty-guide[hidden] { display: none; }
```

`pointer-events: none` on the container — *and* on every descendant —
means the overlay is purely decorative. The composer underneath keeps
all its input semantics, including focus, paste, and the skill picker
popover. No inner element needs `pointer-events: auto` because the
placeholder has nothing interactive.

The overlay is declared *before* `<pi-chat-panel>` in the render
template. With equal z-index, later DOM order paints on top, so the
composer visually covers the overlay where they overlap — and the
overlay's centered content sits well above the composer in practice
anyway. No z-index stacking rules needed.

### Content

Three stacked elements, centered:

1. **Icon** — Lucide `sparkles` SVG, 32×32, `stroke="currentColor"`,
   `stroke-width="2"`, `fill="none"`, stroke linecap/linejoin round.
   Color: `var(--text-tertiary)` (matches the existing muted UI tone).
   SVG body copied verbatim from
   `https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/sparkles.svg`
   (current version uses bezier paths, not polygons — do not hand-write).

2. **Headline** — `How can I help?`
   - 16px, SemiBold (600).
   - Color: `var(--text-secondary)`.
   - Single line, no wrapping.

3. **Hint** — `Ask about the current page, summarize selections, or run a skill.`
   - 12px, Normal (400).
   - Color: `var(--text-tertiary)`.
   - `text-align: center; max-width: 260px; line-height: 1.5;`

### Behavior Summary

| State                          | Overlay visible? |
|--------------------------------|------------------|
| Fresh empty session            | Yes              |
| User focuses composer          | Yes (no change)  |
| User types but hasn't sent     | Yes              |
| User sends first message       | Hidden (messageCount_ becomes 1) |
| During streaming reply         | Hidden           |
| During compaction              | Hidden           |
| History-cleared session reset  | Yes              |
| Opened a non-empty session     | Hidden           |

## Implementation Outline

Single file touched: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`.

1. Add a getter or inline expression computing `showEmptyGuide` from
   the three state fields.
2. Add the `.dao-empty-guide`, `.dao-empty-guide-title`, and
   `.dao-empty-guide-hint` rules to `agent.css`, next to the existing
   `dao-chat-view > pi-chat-panel` rule. Also add
   `dao-chat-view { position: relative; }` there so the absolute child
   anchors correctly. (The component uses light DOM; scoping via
   `agent.css` matches the existing pattern for sibling-layout rules.)
3. In the template, add the overlay block immediately before the
   existing `<pi-chat-panel>` element, gated by `showEmptyGuide`.
4. Use the Lucide sparkles SVG inline as a `lit-html` expression.

No changes to:
- `pi-chat-panel` or vendor code
- Agent state / session management
- Browser-tests
- BUILD.gn

## Risks

- **Composer click-through**: verified by `pointer-events: none`
  applied to the overlay root. If a future designer wants an
  interactive element in the placeholder (example-prompt buttons,
  etc.), they must add `pointer-events: auto` on that element only.
- **Dark mode**: the three CSS variables (`--text-secondary`,
  `--text-tertiary`) already resolve correctly under both light and
  dark themes; no dark-mode-specific rules needed.
- **Lucide icon freshness**: per CLAUDE.md, fetch the SVG at
  implementation time from the upstream repo, do not recall paths from
  memory.

## Success Criteria

- A new tab in the agent panel shows the icon + headline + hint
  centered in the chat area.
- Typing and sending a message hides the overlay immediately; it does
  not reappear until the session is cleared or a new empty session is
  opened.
- The composer textarea is focusable and typable while the overlay is
  visible.
- `npm run rebuild` succeeds (no TS or GN changes trigger a rebuild
  surprise).
