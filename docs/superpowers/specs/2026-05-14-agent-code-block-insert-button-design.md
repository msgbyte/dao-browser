# Agent Code Block "Insert into Focused Input" Button — Design

**Date**: 2026-05-14
**Status**: Approved, awaiting implementation plan.

## Problem

When the user is filling out a form / search box / text editor in a tab and asks Dao Agent for a code snippet, the only way to move that snippet into the page is **copy → switch focus → paste**. The Agent UI knows the snippet; the active tab knows the focused input; the gap between them is keystrokes the user shouldn't have to make.

## Goal

Add an "insert" button next to each code block's existing copy button in the Agent chat panel. When the active tab has a focused text input, clicking the button inserts the code-block content at the cursor position in that input. When there is no focused input, the button is hidden via a CSS class on a shared ancestor — the DOM node stays mounted, only its visibility toggles.

## Non-Goals

- No "insert entire assistant message" button. Only fenced code blocks (the ` ``` ` triple-backtick rendered as `<code-block>`).
- No persistent content script in the active tab. All page interaction goes through Dao's existing `executeScript` IPC, on the same 2-second polling cadence as the selection chip.
- No new MutationObserver. Decoration runs in the same hook as `refreshAssistantActions_`, piggy-backing on message-list changes.
- No changes to the `refreshAssistantActions_` behavior (copy / share / retry stay only on the last assistant message).

## User Flow

1. User focuses a text input in the active tab (e.g., the search field on github.com).
2. Within ≤ 2 s, every `<code-block>` in the Agent chat panel reveals a pencil-line "insert" button to the **left** of its existing "copy code" button.
3. User clicks insert on any code block.
4. The code block's text is inserted at the cursor in the focused input. The input fires `input` events so React / Vue / framework state stays in sync.
5. Button flashes a check-mark + "Inserted" aria label for 2 s (existing `flashButtonLabel_` pattern).
6. If the user defocused the input during the 0–2 s polling window, the click finds no focused element and the button flashes "No input focused" instead.
7. When the user defocuses the input (or navigates away), the insert buttons fade away on the next poll tick (≤ 2 s). The buttons stay in the DOM — only the CSS class on the chat panel ancestor toggles.

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│  DaoChatView (WebUI)                                           │
│                                                                │
│  refreshChips_  (existing 2s timer + hover/focus listeners)    │
│   └─ fetchPageProbeState()                                     │
│       └─ ONE executeScript: returns                            │
│            { selection, hasFocusedInput }                      │
│   └─ pendingSelection_ updates (unchanged)                     │
│   └─ panel.classList.toggle(                                   │
│        'dao-has-focused-input', hasFocusedInput)               │
│                                                                │
│  decorateCodeBlocks_  (called by existing                      │
│                        pendingDecorateTimer_ hook)             │
│   └─ panel.querySelectorAll('code-block')                      │
│   └─ skip if data-dao-insert-decorated already set             │
│   └─ skip if internal <copy-button> not yet rendered           │
│   └─ build <button class="dao-code-insert-btn">                │
│        with pencil-line SVG                                    │
│   └─ insertAdjacentElement('beforebegin', btn) on copy-button  │
│   └─ mark host with data-dao-insert-decorated                  │
│                                                                │
│  onInsertButtonClick_(codeBlockEl)                             │
│   └─ text = codeBlockEl.querySelector('code')?.textContent     │
│   └─ insertTextIntoFocusedInput(text)                          │
│   └─ flashButtonLabel_('inserted' | 'no_input')                │
└────────────────────────────────────────────────────────────────┘
                              │ callNative('executeScript', ...)
                              ▼
┌────────────────────────────────────────────────────────────────┐
│  Active Tab                                                    │
│                                                                │
│  Probe script: read document.activeElement (descend through    │
│  same-origin shadow roots), classify as text-like input /      │
│  textarea / contenteditable. Returns hasFocusedInput: bool.    │
│                                                                │
│  Insert script: locate the same focused element, call          │
│    - setRangeText(text, start, end, 'end') for input/textarea  │
│    - execCommand('insertText', false, text) for contenteditable│
│  Dispatch `input` (bubbles) so framework state syncs.          │
└────────────────────────────────────────────────────────────────┘
```

## Components

### 1. `dao_page_capture.ts`

**Replaces** `fetchCurrentSelection` with `fetchPageProbeState` (or adds the new function and routes `fetchCurrentSelection` through it for back-compat). The probe script returns both pieces of state in one round trip:

```ts
export interface PageProbeState {
  selection: SelectionCapture | null;
  hasFocusedInput: boolean;
}
export async function fetchPageProbeState(): Promise<PageProbeState>;
```

Injected JS:

```js
(function() {
  try {
    var selText = (window.getSelection && window.getSelection().toString()) || '';

    var el = document.activeElement;
    // Descend through same-origin shadow roots — many sites (e.g. shoelace,
    // gh search) wrap their inputs in custom elements.
    while (el && el.shadowRoot && el.shadowRoot.activeElement) {
      el = el.shadowRoot.activeElement;
    }

    var TEXT_INPUT_TYPES = [
      'text','search','url','tel','email','password','number'
    ];
    var hasFocusedInput = false;
    if (el) {
      var tag = el.tagName;
      if (tag === 'TEXTAREA') {
        hasFocusedInput = !el.disabled && !el.readOnly;
      } else if (tag === 'INPUT') {
        var t = (el.type || 'text').toLowerCase();
        hasFocusedInput = TEXT_INPUT_TYPES.indexOf(t) >= 0
          && !el.disabled && !el.readOnly;
      } else if (el.isContentEditable) {
        hasFocusedInput = true;
      }
    }

    return JSON.stringify({
      url: location.href,
      title: document.title || '',
      text: selText,
      hasFocusedInput: hasFocusedInput
    });
  } catch (e) {
    return JSON.stringify({error: (e && e.message) || String(e)});
  }
})()
```

**Adds** `insertTextIntoFocusedInput`:

```ts
export async function insertTextIntoFocusedInput(text: string): Promise<boolean>;
```

The script string is built by string-concatenating `JSON.stringify(text)` so quoting and newlines round-trip safely:

```js
(function() {
  try {
    var text = /* JSON.stringify(text) injected */ ;
    var el = document.activeElement;
    while (el && el.shadowRoot && el.shadowRoot.activeElement) {
      el = el.shadowRoot.activeElement;
    }
    if (!el) return JSON.stringify({ok: false});

    var tag = el.tagName;
    if (tag === 'TEXTAREA' || tag === 'INPUT') {
      if (el.disabled || el.readOnly) return JSON.stringify({ok: false});
      var start = el.selectionStart;
      var end = el.selectionEnd;
      if (typeof start === 'number' && typeof end === 'number') {
        el.setRangeText(text, start, end, 'end');
      } else {
        el.value = (el.value || '') + text;
      }
      el.dispatchEvent(new Event('input', {bubbles: true}));
      return JSON.stringify({ok: true});
    }
    if (el.isContentEditable) {
      var inserted = document.execCommand('insertText', false, text);
      return JSON.stringify({ok: !!inserted});
    }
    return JSON.stringify({ok: false});
  } catch (e) {
    return JSON.stringify({ok: false, error: (e && e.message) || String(e)});
  }
})()
```

Returns `true` only on `{ok: true}`. Cross-origin iframe failures, locked tabs, missing focus all return `false`.

### 2. `dao_chat_view.ts`

**`refreshChips_` change**: swap the dual probe (selection-only) for `fetchPageProbeState`. Toggle the class on the chat panel root:

```ts
const state = await fetchPageProbeState();
// ...existing selection update logic using state.selection...
this.panel_?.classList.toggle('dao-has-focused-input', state.hasFocusedInput);
```

The class lives on `this.panel_` (the chat panel root). It does **not** live on the `<dao-chat-view>` host — the chat panel is the closest ancestor of every `<code-block>` and gives the smallest blast radius for the CSS rule.

**`decorateCodeBlocks_` (new)**: scan + decorate. Idempotent. Called from the same place `refreshAssistantActions_` is currently called (look near line 2099 — the post-render hook that observes `<assistant-message>` arrivals). Also called from `refreshAssistantActions_` itself so streaming code blocks pick up the button as soon as the inner `<copy-button>` materializes.

```ts
private decorateCodeBlocks_(): void {
  const panel = this.panel_;
  if (!panel) return;
  const blocks = panel.querySelectorAll('code-block');
  for (const block of blocks) {
    if (block.hasAttribute('data-dao-insert-decorated')) continue;
    const copyBtn = block.querySelector('copy-button');
    if (!copyBtn) continue;  // Vendor lit element hasn't rendered yet.
    const btn = this.buildCodeInsertButton_(block);
    copyBtn.insertAdjacentElement('beforebegin', btn);
    block.setAttribute('data-dao-insert-decorated', '');
  }
}

private buildCodeInsertButton_(block: Element): HTMLButtonElement {
  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'dao-code-insert-btn';
  btn.title = t('chat.code_block.insert_tooltip');
  btn.setAttribute('aria-label', t('chat.code_block.insert_tooltip'));
  btn.innerHTML =
      '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
      ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
      ' aria-hidden="true">' +
      '<path d="M13 21h8"></path>' +
      '<path d="m15 5 4 4"></path>' +
      '<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 ' +
      '0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 ' +
      '.83-.497z"></path>' +
      '</svg>';
  btn.addEventListener('click', () => void this.insertCodeBlock_(btn, block));
  return btn;
}

private async insertCodeBlock_(
    btn: HTMLButtonElement, block: Element): Promise<void> {
  const codeEl = block.querySelector('code');
  const text = codeEl?.textContent ?? '';
  if (!text) {
    this.flashButtonLabel_(btn, t('chat.code_block.empty'), false);
    return;
  }
  const ok = await insertTextIntoFocusedInput(text);
  if (ok) {
    this.flashButtonLabel_(btn, t('chat.code_block.inserted'), true);
  } else {
    this.flashButtonLabel_(btn, t('chat.code_block.no_input'), false);
  }
}
```

**Reading code text**: pull from `block.querySelector('code')?.textContent`, not from the `.code` lit property. The DOM text is always the rendered/decoded form; the property may be base64-encoded depending on how `markdown-block` constructed the element (vendor encodes via `btoa(unescape(encodeURIComponent(...)))` when it streams). textContent is the lowest-coupling read.

**Decoration trigger**: call `decorateCodeBlocks_` from
- `refreshAssistantActions_` (handles steady-state and stream-end)
- The existing post-render hook around line 2099 (whichever tick first sees a new `<assistant-message>` arrive)

No new timer. Idempotent because of the `data-dao-insert-decorated` attribute. If vendor rebuilds the subtree (e.g. on remount), the attribute is gone and decoration re-runs — safe.

### 3. `agent.css`

Add a button-style rule that mirrors `<copy-button>`'s visual: vendor renders it as a small ghost-variant button with hover-tinted background. Add `.dao-code-insert-btn` to the existing flashing rule too.

```css
.dao-code-insert-btn {
  /* Visual parity with vendor copy-button (ghost / sm). */
  display: none;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  background: transparent;
  border: none;
  border-radius: 6px;
  color: rgba(30, 20, 40, 0.6);
  cursor: pointer;
  padding: 0;
  margin-right: 2px;  /* small gap before the existing copy-button */
}
.dao-code-insert-btn:hover {
  background: rgba(30, 20, 40, 0.06);
  color: rgba(30, 20, 40, 0.9);
}
.dao-code-insert-btn svg {
  width: 16px;
  height: 16px;
}
.dao-code-insert-btn.is-flashing {
  /* reuse existing flashing color treatment */
}

.dao-has-focused-input .dao-code-insert-btn {
  display: inline-flex;
}

@media (prefers-color-scheme: dark) {
  .dao-code-insert-btn { color: rgba(255, 255, 255, 0.6); }
  .dao-code-insert-btn:hover {
    background: rgba(255, 255, 255, 0.08);
    color: rgba(255, 255, 255, 0.9);
  }
}
```

Final sizing tuned against vendor copy-button during implementation — the rule above is the starting point.

### 4. `i18n/locales/en.ts`

Three new keys under a new `chat.code_block.*` namespace:

```ts
'chat.code_block.insert_tooltip': 'Insert into focused input on this page',
'chat.code_block.inserted': 'Inserted',
'chat.code_block.no_input': 'No input focused',
'chat.code_block.empty': 'Empty',
```

Other locale files are populated when the user manually runs `i18n.sh` (per CLAUDE.md, never invoked automatically).

## Data Flow

```
[every 2 s OR pointerenter/focusin]
  → refreshChips_()
  → fetchPageProbeState()           ─┐
  → state.hasFocusedInput            │ one executeScript
  → panel.classList.toggle(          │
       'dao-has-focused-input', …)  ─┘
  → CSS reveals / hides .dao-code-insert-btn

[click .dao-code-insert-btn]
  → insertCodeBlock_(btn, block)
  → text = block.querySelector('code').textContent
  → insertTextIntoFocusedInput(text)  ─── one executeScript
  → flashButtonLabel_('inserted' | 'no_input')
```

## Edge Cases

| Case | Behavior |
|---|---|
| Code block streams in mid-response | `<copy-button>` hasn't rendered yet → `decorateCodeBlocks_` skips this tick; the next call (triggered by `refreshAssistantActions_` on message update / stream end) catches it. |
| 2 s polling window: user defocuses between poll and click | Button still visible, but `insertTextIntoFocusedInput` returns false → button flashes "No input focused". |
| Cross-origin iframe holds the focused input | `document.activeElement` in the top frame returns the `<iframe>` element, not the inner input. Probe returns `hasFocusedInput: false`. Known limitation; no workaround attempted. |
| Page is non-capturable (chrome://, NTP) | `refreshChips_` already early-returns. Class is never set, buttons stay hidden. |
| Tab switched | Next `refreshChips_` tick re-probes the new tab; class updates ≤ 2 s. |
| Disabled / read-only input is focused | Probe classifies as no focused input → buttons hidden. Defensive check repeated in insert script. |
| `data-dao-insert-decorated` host gets rebuilt by vendor (stream re-render) | Attribute lost with the subtree, `decorateCodeBlocks_` re-decorates. No leak, idempotent. |
| Tool result code blocks (inside `<details class="dao-tool-call">`) | Decorated by the same `querySelectorAll('code-block')` scan. Bonus: tool JSON / command output also gets the insert button. Confirmed desired behavior. |
| Very long code block (10 KB+) | `setRangeText` handles arbitrary length. No size guard needed. |
| `execCommand('insertText')` deprecated | Still the most reliable contenteditable insertion path in Chromium 134 (Selection API equivalents have inconsistent undo-stack behavior). Acceptable for the same reasons mainline Chrome's own DevTools snippet runner still uses it. |

## Testing

WebUI-only feature, no `browser_tests` changes. Manual QA path:

1. Focus a search box on any web page (e.g. google.com).
2. Ask Dao Agent for a code snippet ("write hello world in python").
3. Verify: code block's right-side action row shows pencil-line button **left** of copy button.
4. Click insert → search box receives the code text.
5. Click main page somewhere blank to defocus → wait 2 s → insert buttons disappear, copy buttons remain.
6. Re-focus the search box → wait 2 s → insert buttons return.
7. Tool calls (anything that renders `<code-block>` for command output) also have insert buttons.
8. Cross-check dark mode: button colors flip with the system theme.

## Open Implementation Questions

None blocking. Sizing/spacing of `.dao-code-insert-btn` will be tuned during implementation to visually match the vendor `<copy-button>`.
