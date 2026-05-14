// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Captures the active tab as a `{ url, title, markdown }` payload that
// the chat view splices into the user's outgoing message as a
// <current-webpage> block. The pipeline runs entirely inside the target
// page via DevTools Runtime.evaluate (the same channel the former
// `get_readable_content` tool used) by composing the vendored Readability
// and Turndown IIFEs in a single injected script.

import {callNative} from './agent_bridge.js';
import {READABILITY_BUNDLE_IIFE} from './readability_bundle.js';
import {TURNDOWN_BUNDLE_IIFE} from './turndown_bundle.js';

export interface PageInfo {
  url: string;
  title: string;
}

export interface PageCapture extends PageInfo {
  markdown: string;
  // True when Readability couldn't extract an article and we fell back
  // to `document.body.innerText`. The caller can still use the result;
  // the flag is mostly useful for diagnostics.
  fallback?: boolean;
}

// pi-web-ui's `Attachment` shape — matches what `loadAttachment()` returns
// and what `AgentInterface.sendMessage(text, attachments)` accepts. Only
// fields we actually populate are required here; the upstream type is
// a superset.
export interface PiAttachment {
  id: string;
  type: 'document' | 'image';
  fileName: string;
  mimeType: string;
  size: number;
  content: string;
  extractedText?: string;
  preview?: string;
  // Dao-owned extension: URL of the page this attachment was captured
  // from. pi-web-ui ignores unknown fields, but dao_chat_view reads it
  // to repaint the <attachment-tile> as a webpage card (favicon +
  // title + domain) instead of the generic document tile.
  daoPageUrl?: string;
  daoPageTitle?: string;
}

// Encodes a UTF-8 string to base64 without choking on multi-byte chars.
// `btoa` alone blows up on any codepoint > 0xFF.
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

// Turns a captured page into a pi-web-ui Attachment. The extracted text
// is wrapped in a `<current-webpage url="..." title="...">` block so the
// system-prompt contract (BASE_SYSTEM_PROMPT) still matches — the model
// sees the same delimiters whether the content arrived via text injection
// or via an attachment's extractedText.
export function buildPageAttachment(capture: PageCapture): PiAttachment {
  const safeTitle = capture.title.replace(/"/g, '&quot;');
  const safeUrl = capture.url.replace(/"/g, '&quot;');
  const extractedText =
      `<current-webpage url="${safeUrl}" title="${safeTitle}">\n${
          capture.markdown}\n</current-webpage>`;
  const rawName = (capture.title || hostFromUrl(capture.url) || 'page').trim();
  // Strip characters that could confuse downstream renderers (slashes
  // create fake paths; control chars don't belong in a filename). Keep
  // it short so the composer chip / bubble tile don't wrap awkwardly.
  const safeFileName =
      rawName.replace(/[\\/\n\r\t\x00-\x1f]+/g, ' ')
          .replace(/\s+/g, ' ')
          .slice(0, 80) + '.md';
  const id = `dao-page-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  return {
    id,
    type: 'document',
    fileName: safeFileName,
    mimeType: 'text/markdown',
    size: new TextEncoder().encode(capture.markdown).length,
    content: utf8ToBase64(capture.markdown),
    extractedText,
    daoPageUrl: capture.url,
    daoPageTitle: capture.title,
  };
}

function hostFromUrl(url: string): string {
  try {
    return new URL(url).hostname;
  } catch (_) {
    return '';
  }
}

// Lightweight URL + title probe used by the chip poller. Does not inject
// any script, so it's cheap enough to run on a 2s interval.
export async function fetchCurrentPageInfo(): Promise<PageInfo | null> {
  try {
    const raw = await callNative('getPageInfo') as
        {url?: string; title?: string} | null;
    if (!raw || !raw.url) return null;
    return {url: raw.url, title: raw.title || ''};
  } catch (_) {
    return null;
  }
}

// Returns true for URLs where capturing page content is either
// impossible (WebUI surfaces) or not useful (empty tab, data:). Used by
// both the chip visibility logic and the capture pipeline itself.
export function isCapturablePageUrl(url: string): boolean {
  if (!url) return false;
  if (url === 'about:blank') return false;
  if (url.startsWith('chrome://')) return false;
  if (url.startsWith('chrome-extension://')) return false;
  if (url.startsWith('devtools://')) return false;
  if (url.startsWith('view-source:')) return false;
  if (url.startsWith('data:')) return false;
  return true;
}

// Combined injection script. Runs Readability on a clone of the page,
// converts `article.content` (HTML) to markdown with Turndown, and
// returns a JSON payload. If Readability fails (login pages, SPAs with
// no clear article, etc.) we fall back to `document.body.innerText` so
// the user still gets *something* attached.
//
// The two vendor IIFEs each return their own `__VendorModule` namespace;
// capturing them into locals (`__RMOD`, `__TMOD`) avoids collision on
// the global.
//
// Scripts and styles are dropped via `turndown.remove(['script','style'])`
// so inline JS / CSS never leak into the markdown block sent to the
// model. Images are collapsed to `[image: <alt>]` to keep the token
// count bounded.
const CAPTURE_SCRIPT = `(function() {
  try {
    var __RMOD = ${READABILITY_BUNDLE_IIFE};
    var article = null;
    try {
      article = new __RMOD.Readability(document.cloneNode(true)).parse();
    } catch (_e) { article = null; }

    var url = location.href;
    var docTitle = document.title || '';

    if (!article || !article.content) {
      var txt = '';
      try { txt = (document.body && document.body.innerText) || ''; } catch (_e2) {}
      return JSON.stringify({
        url: url,
        title: docTitle,
        markdown: txt,
        fallback: true
      });
    }

    var __TMOD = ${TURNDOWN_BUNDLE_IIFE};
    var ts = new __TMOD.TurndownService({
      headingStyle: 'atx',
      bulletListMarker: '-',
      codeBlockStyle: 'fenced',
      emDelimiter: '*'
    });
    ts.remove(['script', 'style', 'noscript']);
    ts.addRule('img-alt-only', {
      filter: 'img',
      replacement: function(_content, node) {
        var alt = (node && node.getAttribute && node.getAttribute('alt')) || '';
        return '[image: ' + alt + ']';
      }
    });

    var md = '';
    try {
      md = ts.turndown(article.content);
    } catch (_e3) {
      md = article.textContent || '';
    }

    // CommonMark right-flanking rule: a closing star/underscore run
    // wedged between a Unicode-punctuation char and a non-space,
    // non-punct char does NOT close the emphasis. turndown happily
    // emits "**图片说明：**Claude" (from <strong>图片说明：</strong>Claude)
    // and marked then leaves the asterisks as literal text. Same
    // problem when the trailing char is a CJK letter
    // ("**图片说明：**说明"). Insert an ASCII space so the closer has
    // a whitespace neighbor and flanking succeeds. U+200B does not
    // qualify under marked's whitespace predicate, so a real space
    // is required; this matches typical CJK + Latin typography too.
    //
    // \p{L}\p{N} matches any Unicode letter/digit (ASCII and CJK).
    // The CJK-punct character class covers fullwidth forms,
    // CJK symbols/punct, plus the curly quotes / dashes / ellipsis
    // turndown typically passes through.
    // The regex literals below intentionally double every backslash. This
    // string is a TS template literal that gets sent verbatim through CDP
    // Runtime.evaluate; single \\* would be eaten by template-string escape
    // resolution and the injected page would see /(**|*|_)/ — a SyntaxError
    // ("Nothing to repeat") that aborts the whole capture before it can
    // reach the outer try/catch. Keep \\\\* / \\\\p{...} so the page actually
    // sees \\*\\* / \\p{L}\\p{N}.
    md = md.replace(
        /([　-〿＀-￯‘-‟…—–])(\\*\\*|__|\\*|_)(?=[\\p{L}\\p{N}])/gu,
        '$1$2 ');
    md = md.replace(
        /(?<=[\\p{L}\\p{N}])(\\*\\*|__|\\*|_)([　-〿＀-￯‘-‟…—–])/gu,
        ' $1$2');

    return JSON.stringify({
      url: url,
      title: article.title || docTitle,
      markdown: md
    });
  } catch (err) {
    return JSON.stringify({
      error: (err && err.message) || String(err)
    });
  }
})()`;

// Current text selection in the active tab. `text` is the plain string
// the user has highlighted (can be multi-line); `url` / `title` pin the
// selection to the page it was made on. Empty selections resolve to null.
export interface SelectionCapture {
  url: string;
  title: string;
  text: string;
}

// Tiny probe used by the selection chip poller. Runs `window.getSelection()
// .toString()` in the active tab. Cheap enough for the same 2s loop the
// page-chip watcher already uses. Returns null when there is no selection
// (or the page is non-capturable).
export async function fetchCurrentSelection():
    Promise<SelectionCapture | null> {
  const probe = `(function() {
    try {
      var s = (window.getSelection && window.getSelection().toString()) || '';
      return JSON.stringify({
        url: location.href,
        title: document.title || '',
        text: s,
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
    return null;
  }
  if (!raw || raw.error || !raw.result) return null;
  let payload: {url?: string; title?: string; text?: string; error?: string};
  try {
    payload = JSON.parse(raw.result);
  } catch (_) {
    return null;
  }
  if (payload.error || !payload.url) return null;
  const text = (payload.text || '').trim();
  if (!text) return null;
  return {
    url: payload.url,
    title: payload.title || '',
    text,
  };
}

// Unified probe result. `selection` carries the same shape
// fetchCurrentSelection returns (null when no non-empty selection or page is
// non-capturable). `hasFocusedInput` is true when the active tab's
// document.activeElement (descended through open shadow roots — closed
// shadow roots and cross-origin iframes are blind spots, both make
// activeElement report the host element) is a text-like <input>,
// <textarea>, or contenteditable — and is not disabled or readonly.
// Used by dao_chat_view to gate the code-block insert button.
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
      var TEXT_INPUT_TYPES = [
        'text','search','url','tel','email','password','number'
      ];
      if (tag === 'TEXTAREA' ||
          (tag === 'INPUT' &&
           TEXT_INPUT_TYPES.indexOf(
               (el.type || 'text').toLowerCase()) >= 0)) {
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

// Clear the active tab's selection. Called after a successful send so the
// same highlight isn't silently re-attached to the next message. Best-
// effort: failures are swallowed.
export async function clearCurrentSelection(): Promise<void> {
  const script = `(function() {
    try {
      var s = window.getSelection && window.getSelection();
      if (s && s.removeAllRanges) s.removeAllRanges();
    } catch (_e) {}
    return '';
  })()`;
  try {
    await callNative('executeScript', {code: script, lockTab: false});
  } catch (_) { /* best-effort */ }
}

// Wraps a selection capture in a `<selected-text>` block and packages it
// as a pi-web-ui Attachment. Mirrors buildPageAttachment so convertToLlm's
// `extractedText` splice works uniformly — the model sees a clearly
// delimited block with the source page's url/title for citation.
export function buildSelectionAttachment(capture: SelectionCapture):
    PiAttachment {
  const safeTitle = capture.title.replace(/"/g, '&quot;');
  const safeUrl = capture.url.replace(/"/g, '&quot;');
  const extractedText = `<selected-text url="${safeUrl}" title="${
      safeTitle}">\n${capture.text}\n</selected-text>`;
  const firstLine = capture.text.split(/\r?\n/)[0] || capture.text;
  const safeFileName =
      (firstLine.replace(/[\\/\n\r\t\x00-\x1f]+/g, ' ')
           .replace(/\s+/g, ' ')
           .slice(0, 60) ||
       'selection') +
      '.txt';
  const id =
      `dao-selection-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  return {
    id,
    type: 'document',
    fileName: safeFileName,
    mimeType: 'text/plain',
    size: new TextEncoder().encode(capture.text).length,
    content: utf8ToBase64(capture.text),
    extractedText,
    daoPageUrl: capture.url,
    daoPageTitle: capture.title,
  };
}

export async function captureCurrentPageMarkdown():
    Promise<PageCapture | null> {
  // PDF fast path. PDF tabs render PDFium inside the built-in PDF
  // Extension; Readability on the extension's top frame returns
  // ~nothing useful. Ask native to extract text via
  // pdf::PDFDocumentHelper instead. On any failure (non-PDF tab, load
  // timeout, encrypted PDF) we fall through to the original
  // Readability + Turndown path so HTML pages behave unchanged.
  try {
    const pdf = await callNative('getPdfText') as {
      isPdf?: boolean;
      url?: string;
      title?: string;
      pageCount?: number;
      text?: string;
      truncated?: boolean;
      truncatedAtPage?: number;
      error?: string;
    } | null;
    if (pdf && pdf.isPdf && !pdf.error && pdf.text && pdf.url) {
      const title = pdf.title || 'PDF';
      const pageCount = typeof pdf.pageCount === 'number' ? pdf.pageCount : 0;
      const header =
          `# ${title} (PDF, ${pageCount || '?'} pages)\n\n`;
      return {
        url: pdf.url,
        title,
        markdown: header + pdf.text,
        fallback: false,
      };
    }
    // isPdf === false (HTML tab), or isPdf === true with error — fall
    // through to Readability path below.
  } catch (_e) {
    // Native handler missing or threw — fall through.
  }

  let raw: {result?: string; error?: string};
  try {
    raw = await callNative(
        'executeScript', {code: CAPTURE_SCRIPT, lockTab: false}) as
        {result?: string; error?: string};
  } catch (e) {
    console.warn('[dao-capture] executeScript threw', e);
    return null;
  }
  if (!raw || raw.error || !raw.result) {
    console.warn('[dao-capture] native executeScript returned no usable result',
                 raw);
    return null;
  }
  let payload:
      {url?: string; title?: string; markdown?: string; fallback?: boolean;
       error?: string};
  try {
    payload = JSON.parse(raw.result);
  } catch (e) {
    console.warn('[dao-capture] failed to parse capture payload', e);
    return null;
  }
  if (payload.error || !payload.url) {
    console.warn('[dao-capture] capture script returned error or empty url',
                 payload);
    return null;
  }
  return {
    url: payload.url,
    title: payload.title || '',
    markdown: payload.markdown || '',
    fallback: !!payload.fallback,
  };
}
