# Agent Code Block "Insert into Focused Input" Button — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a pencil-line "insert" button next to each `<code-block>`'s existing copy button in the Agent chat panel. When the active tab has a focused text input, clicking inserts the code at the input's cursor. Visibility is controlled by a CSS class on the chat-panel root — DOM stays mounted, only CSS hides.

**Architecture:** Extend the existing `refreshChips_` polling probe to also return `hasFocusedInput`, and toggle `dao-has-focused-input` on the chat panel based on it. Decorate every `<code-block>` (assistant + tool result) in the same hook that injects the assistant-action row, inserting our button as a sibling immediately before the vendor `<copy-button>`. Click handler reads `<code>` text and round-trips through a new `insertTextIntoFocusedInput` IPC.

**Tech Stack:** TypeScript / lit / Chromium WebUI / vendored pi-web-ui (`<code-block>` is a light-DOM lit element with `<copy-button>` as a child).

---

## File Inventory

| File | Action | Responsibility |
|---|---|---|
| `src/dao/browser/ui/webui/resources/agent/dao_page_capture.ts` | Modify | Add `fetchPageProbeState` (selection + hasFocusedInput in one IPC) and `insertTextIntoFocusedInput`. Keep `fetchCurrentSelection` as a thin wrapper for back-compat. |
| `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts` | Modify | Wire `fetchPageProbeState` into `refreshChips_`, toggle the class on the chat panel root, add `decorateCodeBlocks_` and call it from the assistant-action hooks. |
| `src/dao/browser/ui/webui/resources/agent/agent.css` | Modify | Style `.dao-code-insert-btn` (hidden by default, revealed under `.dao-has-focused-input`). |
| `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts` | Modify | Add 4 new `chat.code_block.*` keys. |

No new files. No GN / patch changes (all touched files are already in `dao_agent_ui_sources` and `dao_strings.grd` is not involved — these keys are WebUI-side).

---

## Task 1: Page-Capture API — `fetchPageProbeState` and `insertTextIntoFocusedInput`

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_page_capture.ts` (extend the selection probe at lines 234–297; add new exports below)

**Why this task is first:** `dao_chat_view.ts` depends on both new functions. Implementing them first lets us verify the IPC contract in isolation before any UI work.

- [ ] **Step 1: Add the `PageProbeState` interface and `fetchPageProbeState` function**

Locate the existing `SelectionCapture` interface and `fetchCurrentSelection` function (around line 234). Add the new interface above `fetchCurrentSelection`, and add the new exported function below `fetchCurrentSelection` (keeping the existing function intact so we don't break callers that haven't migrated yet).

Edit `dao_page_capture.ts` — add after the `SelectionCapture` interface and after the `fetchCurrentSelection` function body (right before `clearCurrentSelection`):

```ts
// Unified probe result. `selection` carries the same shape
// fetchCurrentSelection returns (null when no non-empty selection or page is
// non-capturable). `hasFocusedInput` is true when the active tab's
// document.activeElement (descending through same-origin shadow roots) is a
// text-like <input>, <textarea>, or contenteditable — and is not disabled
// or readonly. Used by dao_chat_view to gate the code-block insert button.
export interface PageProbeState {
  selection: SelectionCapture | null;
  hasFocusedInput: boolean;
}

// Single executeScript that combines the selection probe with focused-input
// detection. Returning both in one round trip keeps the 800ms watch loop
// cheap (one IPC instead of two).
//
// `hasFocusedInput` matches the input-type filter the insert path uses, so
// the visibility class and the insert action stay in sync: if the probe
// says we have a focused input, the insert script will accept it (modulo
// races where focus changes during the 0–800ms window — handled by the
// caller via flashButtonLabel on failure).
export async function fetchPageProbeState(): Promise<PageProbeState> {
  const probe = `(function() {
    try {
      var selText =
          (window.getSelection && window.getSelection().toString()) || '';

      var el = document.activeElement;
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
        hasFocusedInput: hasFocusedInput,
      });
    } catch (e) {
      return JSON.stringify({error: (e && e.message) || String(e)});
    }
  })()`;

  let raw: {result?: string; error?: string};
  try {
    raw = await callNative(
        'executeScript', {code: probe, lockTab: false}) as
        {result?: string; error?: string};
  } catch (_) {
    return {selection: null, hasFocusedInput: false};
  }
  if (!raw || raw.error || !raw.result) {
    return {selection: null, hasFocusedInput: false};
  }

  let payload: {
    url?: string;
    title?: string;
    text?: string;
    hasFocusedInput?: boolean;
    error?: string;
  };
  try {
    payload = JSON.parse(raw.result);
  } catch (_) {
    return {selection: null, hasFocusedInput: false};
  }
  if (payload.error || !payload.url) {
    return {selection: null, hasFocusedInput: false};
  }

  const text = (payload.text || '').trim();
  const selection: SelectionCapture | null = text ? {
    url: payload.url,
    title: payload.title || '',
    text,
  } : null;

  return {
    selection,
    hasFocusedInput: !!payload.hasFocusedInput,
  };
}
```

- [ ] **Step 2: Add `insertTextIntoFocusedInput`**

Add directly after `fetchPageProbeState`:

```ts
// Inserts `text` into the active tab's currently focused text input /
// textarea / contenteditable element at the cursor (replacing any current
// selection). Dispatches a bubbling `input` event so frameworks like
// React / Vue pick up the change. Returns false when there is no eligible
// focused element, the element is disabled/readonly, or the IPC failed —
// the caller flashes "No input focused" in that case.
//
// The text is JSON.stringify'd before string-concatenation into the script
// body so quotes / newlines / unicode round-trip safely through CDP
// Runtime.evaluate.
export async function insertTextIntoFocusedInput(
    text: string): Promise<boolean> {
  const payload = JSON.stringify(text);
  const script = `(function() {
    try {
      var text = ${payload};
      var el = document.activeElement;
      while (el && el.shadowRoot && el.shadowRoot.activeElement) {
        el = el.shadowRoot.activeElement;
      }
      if (!el) return JSON.stringify({ok: false});

      var tag = el.tagName;
      if (tag === 'TEXTAREA' || tag === 'INPUT') {
        if (el.disabled || el.readOnly) {
          return JSON.stringify({ok: false});
        }
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
  })()`;

  let raw: {result?: string; error?: string};
  try {
    raw = await callNative(
        'executeScript', {code: script, lockTab: false}) as
        {result?: string; error?: string};
  } catch (_) {
    return false;
  }
  if (!raw || raw.error || !raw.result) return false;
  try {
    const parsed = JSON.parse(raw.result) as {ok?: boolean};
    return !!parsed.ok;
  } catch (_) {
    return false;
  }
}
```

- [ ] **Step 3: Verify the build compiles**

Run from the repo root:
```bash
npm run rebuild
```

Expected: build succeeds. No new TypeScript errors. The new exports are only referenced internally so far; an unused-import warning is acceptable but not expected (we did not add any imports).

If you see a `gn`/`siso` state mismatch error, follow CLAUDE.md: `gn clean out/dao-debug` then `npm run build:debug`. **Never** run `autoninja` / `ninja` / `siso` directly.

- [ ] **Step 4: Commit**

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_page_capture.ts
git commit -m "feat(agent): add fetchPageProbeState and insertTextIntoFocusedInput page IPCs"
```

---

## Task 2: i18n keys

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts`

- [ ] **Step 1: Add the four new keys**

Open `en.ts`. Find the existing block of `chat.message_actions.*` keys (lines 38–46). Add a new commented block immediately after it (before `chat.toast.wait_for_turn` at line 48):

```ts
  // Per-code-block "Insert into focused input" button (next to vendor
  // copy-button inside <code-block>). Visible only when the active tab
  // has a focused text input.
  'chat.code_block.insert_tooltip': 'Insert into focused input on this page',
  'chat.code_block.inserted': 'Inserted',
  'chat.code_block.no_input': 'No input focused',
  'chat.code_block.empty': 'Empty',
```

The resulting file fragment around lines 38–50 should read:

```ts
  // Per-message action buttons (Copy / Share-as-image / Regenerate).
  'chat.message_actions.copy_tooltip': 'Copy answer text',
  'chat.message_actions.share_tooltip': 'Copy as image',
  'chat.message_actions.regenerate_tooltip': 'Regenerate response',
  // Transient labels flashed on the action buttons.
  'chat.message_actions.empty': 'Empty',
  'chat.message_actions.copied': 'Copied',
  'chat.message_actions.shared': 'Shared',
  'chat.message_actions.failed': 'Failed',

  // Per-code-block "Insert into focused input" button (next to vendor
  // copy-button inside <code-block>). Visible only when the active tab
  // has a focused text input.
  'chat.code_block.insert_tooltip': 'Insert into focused input on this page',
  'chat.code_block.inserted': 'Inserted',
  'chat.code_block.no_input': 'No input focused',
  'chat.code_block.empty': 'Empty',

  // Toast shown when the user submits while a turn is still streaming.
  'chat.toast.wait_for_turn': 'Wait for the current turn to finish',
```

- [ ] **Step 2: Do NOT regenerate other locales**

Per CLAUDE.md: `i18n.sh` is **manually** invoked by the user. Other locale files (`zh-CN.ts`, `ja.ts`, etc.) will fall back to the English value via the `t()` runtime missing-key behavior until the user chooses to run the script. This task does not run `i18n.sh`.

- [ ] **Step 3: Commit**

```bash
git add src/dao/browser/ui/webui/resources/agent/i18n/locales/en.ts
git commit -m "feat(agent): add code-block insert button i18n keys (en only)"
```

---

## Task 3: CSS for `.dao-code-insert-btn`

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/agent.css` (add a new block; the existing assistant-action rules at lines 585–635 are the visual reference but are NOT modified)

- [ ] **Step 1: Add the new CSS block**

Open `agent.css`. Find the closing of the existing assistant-action block (around line 635, after `.dao-share-btn.is-flashing { … }`). Insert the new block immediately after it:

```css
/* Dao-owned "Insert into focused input" button injected into every
 * <code-block> by dao_chat_view's decorateCodeBlocks_. Sits as a sibling
 * immediately before the vendor <copy-button>. Hidden by default —
 * revealed only when the chat-panel root carries `.dao-has-focused-input`
 * (toggled by refreshChips_ on every page-probe tick).
 *
 * Visual parity: vendor copy-button is a small ghost button with a
 * 16×16 icon, neutral muted color, and a soft hover background. We
 * mirror that with our own classed element so styling stays portable
 * across vendor bundle updates. */
.dao-code-insert-btn {
  display: none;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  padding: 0;
  margin-right: 2px;
  border: none;
  border-radius: 6px;
  background: transparent;
  color: rgba(30, 20, 40, 0.55);
  cursor: pointer;
  transition: background-color 0.12s, color 0.12s;
}
.dao-code-insert-btn:hover {
  background: rgba(30, 20, 40, 0.08);
  color: rgba(30, 20, 40, 0.92);
}
.dao-code-insert-btn:active {
  background: rgba(70, 120, 190, 0.16);
}
.dao-code-insert-btn svg {
  width: 16px;
  height: 16px;
}
.dao-code-insert-btn.is-flashing {
  pointer-events: none;
  background: rgba(70, 120, 190, 0.18);
  color: rgba(30, 20, 40, 0.92);
}

.dao-has-focused-input .dao-code-insert-btn {
  display: inline-flex;
}

@media (prefers-color-scheme: dark) {
  .dao-code-insert-btn {
    color: rgba(255, 255, 255, 0.55);
  }
  .dao-code-insert-btn:hover {
    background: rgba(255, 255, 255, 0.10);
    color: rgba(255, 255, 255, 0.92);
  }
  .dao-code-insert-btn:active {
    background: rgba(70, 120, 190, 0.22);
  }
  .dao-code-insert-btn.is-flashing {
    background: rgba(70, 120, 190, 0.22);
    color: rgba(255, 255, 255, 0.92);
  }
}
```

- [ ] **Step 2: Commit (defer build verification — JS isn't using this class yet)**

```bash
git add src/dao/browser/ui/webui/resources/agent/agent.css
git commit -m "feat(agent): style dao-code-insert-btn for code-block insert"
```

The class is unused after this commit. Build verification happens in Task 4 when JS wires it up.

---

## Task 4: Wire up `dao_chat_view.ts`

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
  - Imports block (top of file, near the existing `dao_page_capture.js` imports)
  - `refreshChips_` (lines 1426–1467) — swap selection-only probe for unified probe
  - New methods `decorateCodeBlocks_`, `buildCodeInsertButton_`, `insertCodeBlock_`
  - Hook calls inside `refreshAssistantActions_` (line 1229) and the post-restore retry block (line 2106)

- [ ] **Step 1: Update imports**

Find the existing import of `dao_page_capture` (search for `from './dao_page_capture`). Add `fetchPageProbeState` and `insertTextIntoFocusedInput` to that import list. The exact pre-existing import statement varies, but it should look like:

```ts
import {
  // ...existing names...
  fetchCurrentSelection,
  // ...existing names...
} from './dao_page_capture.js';
```

Add the two new names:

```ts
import {
  // ...existing names...
  fetchCurrentSelection,
  fetchPageProbeState,
  insertTextIntoFocusedInput,
  // ...existing names...
} from './dao_page_capture.js';
```

Keep `fetchCurrentSelection` in the import — it may still be referenced by other code paths and we are not deleting it from `dao_page_capture.ts`.

- [ ] **Step 2: Replace the body of `refreshChips_` to use the unified probe**

Locate `refreshChips_` (around line 1427). Inside the `try` block, the current code calls `fetchCurrentPageInfo()` then later `fetchCurrentSelection()`. Replace the selection call with `fetchPageProbeState()` and toggle the chat-panel class. The page-info logic (used for the page chip) stays as-is.

Find these lines inside `refreshChips_` (current behavior — they appear around lines 1454–1463):

```ts
      // Selection chip: script injection, only when the page itself is
      // capturable (guarded above).
      const sel = await fetchCurrentSelection();
      const currentSel = this.pendingSelection_;
      if (!sel) {
        if (currentSel !== null) this.pendingSelection_ = null;
      } else if (!currentSel || currentSel.text !== sel.text ||
                 currentSel.url !== sel.url) {
        this.pendingSelection_ = sel;
      }
```

Replace with:

```ts
      // Selection chip + focused-input gate: one unified probe returns
      // both the active tab's text selection and whether its active
      // element is a writable text input. Halves the executeScript count
      // versus two separate probes.
      const probe = await fetchPageProbeState();
      const sel = probe.selection;
      const currentSel = this.pendingSelection_;
      if (!sel) {
        if (currentSel !== null) this.pendingSelection_ = null;
      } else if (!currentSel || currentSel.text !== sel.text ||
                 currentSel.url !== sel.url) {
        this.pendingSelection_ = sel;
      }
      // Code-block insert button visibility. Class lives on the chat
      // panel root because it is the closest ancestor of every
      // <code-block> rendered in the message list; scoping the CSS rule
      // there keeps the blast radius minimal.
      this.panel_?.classList.toggle(
          'dao-has-focused-input', probe.hasFocusedInput);
```

The early-return branch at the top of the try block (when `info` is non-capturable) does NOT toggle the class — we want the class cleared in that case. Add the cleanup just before the existing `return`. Locate this fragment near line 1432:

```ts
      const info = await fetchCurrentPageInfo();
      if (!info || !isCapturablePageUrl(info.url)) {
        if (this.pendingPageAttachment_ !== null) {
          this.pendingPageAttachment_ = null;
        }
        if (this.pendingSelection_ !== null) {
          this.pendingSelection_ = null;
        }
        return;
      }
```

Update to:

```ts
      const info = await fetchCurrentPageInfo();
      if (!info || !isCapturablePageUrl(info.url)) {
        if (this.pendingPageAttachment_ !== null) {
          this.pendingPageAttachment_ = null;
        }
        if (this.pendingSelection_ !== null) {
          this.pendingSelection_ = null;
        }
        // Non-capturable page → no focused-input gate either.
        this.panel_?.classList.remove('dao-has-focused-input');
        return;
      }
```

- [ ] **Step 3: Add the three new methods**

Find `refreshAssistantActions_` (line 1229). Insert the three new methods immediately after `refreshAssistantActions_`'s closing brace (before `flashButtonLabel_` at line 1296). Use this exact code:

```ts
  // Decorates every <code-block> in the message list with a Dao-owned
  // "Insert into focused input" button placed immediately before the
  // vendor <copy-button>. Idempotent via the `data-dao-insert-decorated`
  // attribute. Skips blocks whose internal <copy-button> hasn't rendered
  // yet (streaming) — the next call (driven by refreshAssistantActions_
  // or the loadSession_ retry chain) picks them up.
  //
  // Decorates BOTH assistant-message code blocks AND tool-result code
  // blocks; the .dao-has-focused-input gate (CSS-only) handles when the
  // button is actually visible, so over-decoration is harmless.
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

  // Builds the insert-button DOM (lucide pencil-line icon, i18n'd
  // tooltip + aria-label). Click handler captures the owning code-block
  // by closure so each button knows which snippet to insert.
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
        '<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0' +
        ' 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0' +
        ' 0 .83-.497z"></path>' +
        '</svg>';
    btn.addEventListener(
        'click', () => void this.insertCodeBlock_(btn, block));
    return btn;
  }

  // Handles a click on a code-block insert button. Reads the rendered
  // code via textContent (vendor stores the raw source on the <code-block>
  // .code property in base64-encoded form when fed from markdown-block,
  // so we read the post-highlight DOM text instead — always the decoded
  // source). Round-trips through executeScript; flashes a transient
  // label on success/failure using the same visual treatment the
  // copy/share buttons use.
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

- [ ] **Step 4: Call `decorateCodeBlocks_` from `refreshAssistantActions_`**

Find `refreshAssistantActions_` (line 1229). Locate the final line — `last.insertAdjacentElement('afterend', row);` at line 1293. Add `this.decorateCodeBlocks_();` immediately after it:

```ts
    last.insertAdjacentElement('afterend', row);
    // Re-decorate code blocks: streaming may have added new <code-block>
    // elements (or finalized their internal <copy-button>) since the
    // last decoration pass. Idempotent.
    this.decorateCodeBlocks_();
  }
```

- [ ] **Step 5: Call `decorateCodeBlocks_` from the loadSession_ retry chain**

Find the retry block in `loadSession_` (lines 2106–2117). Add a call inside `tryInject`:

Current code:

```ts
      const tryInject = (delay: number, remaining: number) => {
        setTimeout(() => {
          this.refreshAssistantActions_();
          this.decoratePageAttachments_();
          const injected = !!this.panel_?.querySelector(
              '.dao-assistant-actions');
          if (!injected && remaining > 0) {
            tryInject(delay * 2, remaining - 1);
          }
        }, delay);
      };
      tryInject(80, 3);
```

Updated:

```ts
      const tryInject = (delay: number, remaining: number) => {
        setTimeout(() => {
          this.refreshAssistantActions_();
          this.decoratePageAttachments_();
          this.decorateCodeBlocks_();
          const injected = !!this.panel_?.querySelector(
              '.dao-assistant-actions');
          if (!injected && remaining > 0) {
            tryInject(delay * 2, remaining - 1);
          }
        }, delay);
      };
      tryInject(80, 3);
```

`refreshAssistantActions_` itself now also calls `decorateCodeBlocks_` (Step 4), so under steady state this retry adds defense-in-depth: if `refreshAssistantActions_` returned early (streaming) on the first tick, the retry still catches new code blocks on the backoff.

- [ ] **Step 6: Build and verify TypeScript compiles**

```bash
npm run rebuild
```

Expected: build succeeds with no TS errors. The build output should report copying the updated `dao_chat_view.ts` and `dao_page_capture.ts` into `engine/src/dao/...`.

If you see a "Cannot find name `t`" or similar — the `t` import is already in `dao_chat_view.ts` (it's used by the existing action buttons). Double-check the import line at the top of the file.

- [ ] **Step 7: Commit**

```bash
git add src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts
git commit -m "feat(agent): wire code-block insert button to focused-input probe"
```

---

## Task 5: Manual QA

**Files:** none modified — this task validates behavior.

- [ ] **Step 1: Launch the built browser**

```bash
npm run start:debug
```

This is per CLAUDE.md ("start with stderr logging") so any IPC errors surface immediately.

- [ ] **Step 2: Verify "no focused input" state**

1. Open a new tab to `https://example.com` (a page with no focused input by default).
2. Open the Dao Agent sidebar.
3. Start a new chat.
4. Send a prompt that elicits code, e.g.: `Write a Python hello world.`
5. Wait for the response to complete.
6. **Expected:** Code block renders with the vendor copy button visible in its header. **No** pencil-line insert button is visible (display:none under the absent `.dao-has-focused-input` class on the chat panel).

Use the DevTools inspector (Cmd+Option+I on the agent panel) to confirm the `<button class="dao-code-insert-btn">` element exists in the DOM as a sibling immediately before `<copy-button>` — only its computed style is `display: none`.

- [ ] **Step 3: Verify focused-input reveals the button**

1. Click into the body of `example.com` — note no input. Then navigate to `https://google.com` (whose search box gets autofocus).
2. Click into the search box on google.com. **Within 1 second** (the 800ms watch timer + one IPC), check the Agent panel.
3. **Expected:** All `<code-block>` elements in the chat now show the pencil-line insert button to the LEFT of the copy button.

- [ ] **Step 4: Verify insert action works**

1. With the google.com search box focused, click the insert button on the code block.
2. **Expected:** The Python snippet appears in the google search box. Button briefly flashes a check-mark + "Inserted" aria label.
3. Switch focus back to a non-input area (click on the page background outside the search box). Wait ~1 s.
4. **Expected:** Insert buttons disappear from all code blocks; copy buttons remain.

- [ ] **Step 5: Verify graceful failure**

1. Re-focus the google.com search box. Confirm insert buttons appear.
2. Quickly: click into the page background (defocus the input), then immediately click the insert button before the 800ms timer fires (button still visible because class hasn't updated yet).
3. **Expected:** Button flashes an X + "No input focused" instead of "Inserted". Nothing is inserted anywhere.

- [ ] **Step 6: Verify tool-result code blocks also get the button**

1. With a focused input on the page, send a prompt that triggers a tool call producing code/JSON output (e.g. `Use the web search tool to find the latest version of Node.js.`).
2. Expand a tool call `<details>` in the chat panel.
3. **Expected:** Code blocks inside the tool call display also have the pencil-line insert button (per spec — bonus behavior from the panel-wide `querySelectorAll('code-block')`).

- [ ] **Step 7: Verify dark mode**

1. Switch macOS to dark mode (System Settings → Appearance → Dark).
2. Refocus a page input. Confirm the insert button is visible with the dark-mode color treatment (white-ish icon, darker hover background).
3. Click insert; confirm it still works.

- [ ] **Step 8: Verify history reload**

1. Close the Agent sidebar, reopen it (triggers `loadSession_` → `tryInject`).
2. **Expected:** Insert buttons appear on historical code blocks once page focus is set, same as freshly-streamed ones (the retry chain calls `decorateCodeBlocks_`).

- [ ] **Step 9: Stop the browser**

`Cmd+Q` or close the window. No commit needed for this task — it's pure verification.

---

## Self-Review (already performed)

**Spec coverage:**
- ✅ `fetchPageProbeState` (spec §Components.1) → Task 1
- ✅ `insertTextIntoFocusedInput` (spec §Components.1) → Task 1
- ✅ `refreshChips_` integration + class toggle (spec §Components.2) → Task 4 steps 2
- ✅ `decorateCodeBlocks_` idempotent decoration (spec §Components.2) → Task 4 step 3
- ✅ Hook into `refreshAssistantActions_` + post-restore retry (spec §Components.2 / Decoration trigger) → Task 4 steps 4–5
- ✅ `.dao-code-insert-btn` CSS + visibility gate (spec §Components.3) → Task 3
- ✅ Four new i18n keys (spec §Components.4) → Task 2
- ✅ Light & dark mode (spec §Components.3) → Task 3
- ✅ Tool-result code blocks decorated (spec §Edge Cases) → Task 4 step 3 (panel-wide scan)
- ✅ Manual QA flow (spec §Testing) → Task 5

**Placeholder scan:** No "TBD" / "TODO" / "implement later" / "similar to Task N" anywhere. Each step includes the exact code or command.

**Type consistency:**
- `PageProbeState` (Task 1) → consumed in `refreshChips_` (Task 4 step 2) with same field names: `selection`, `hasFocusedInput`. ✅
- `insertTextIntoFocusedInput(text: string): Promise<boolean>` (Task 1) → called as `await insertTextIntoFocusedInput(text)` (Task 4 step 3). ✅
- `decorateCodeBlocks_` / `buildCodeInsertButton_` / `insertCodeBlock_` (Task 4 step 3) → all three reference the same names later in the same task (steps 4, 5). ✅
- i18n keys `chat.code_block.insert_tooltip` / `inserted` / `no_input` / `empty` (Task 2) → consumed by Task 4 step 3 with identical strings. ✅
