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
import type {ElementContextCapture} from './dao_element_context.js';
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

export interface ScreenshotClip {
  x: number;
  y: number;
  width: number;
  height: number;
  scale: number;
}

const CAMERA_CURSOR_SVG =
    '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" ' +
    'viewBox="0 0 24 24" fill="none" stroke="rgb(70,120,190)" ' +
    'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' +
    '<path d="M13.997 4a2 2 0 0 1 1.76 1.05l.486.9A2 2 0 0 0 18.003 7H20a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2h1.997a2 2 0 0 0 1.759-1.048l.489-.904A2 2 0 0 1 10.004 4z" />' +
    '<circle cx="12" cy="13" r="3" />' +
    '</svg>';

function svgDataUri(svg: string): string {
  return 'data:image/svg+xml,' + encodeURIComponent(svg);
}

const CAMERA_CURSOR_DATA_URI = svgDataUri(CAMERA_CURSOR_SVG);

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

export function clampScreenshotClip(
    bounds: {x: number; y: number; width: number; height: number},
    viewport: {width: number; height: number}): ScreenshotClip | null {
  const left = Math.max(0, bounds.x);
  const top = Math.max(0, bounds.y);
  const right = Math.min(viewport.width, bounds.x + bounds.width);
  const bottom = Math.min(viewport.height, bounds.y + bounds.height);
  const width = right - left;
  const height = bottom - top;
  if (!Number.isFinite(width) || !Number.isFinite(height) ||
      width <= 0 || height <= 0) {
    return null;
  }
  return {x: left, y: top, width, height, scale: 1};
}

export function buildElementScreenshotAttachment(
    capture: ElementContextCapture, data: string, format: string):
    PiAttachment {
  const rawName =
      (capture.label || capture.locator.name || capture.locator.role ||
       'element')
          .trim();
  const safeFileName =
      rawName.replace(/[\\/\n\r\t\x00-\x1f]+/g, ' ')
          .replace(/\s+/g, ' ')
          .slice(0, 60) + '.jpg';
  return {
    id: `dao-element-shot-${Date.now()}-${
        Math.random().toString(36).slice(2)}`,
    type: 'image',
    fileName: safeFileName,
    mimeType: format === 'png' ? 'image/png' : 'image/jpeg',
    size: Math.ceil(data.length * 3 / 4),
    content: data,
    preview: data,
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

const ELEMENT_PICKER_START_SCRIPT = `(function() {
  try {
    var existing = window.__dao_element_picker__;
    if (existing && existing.cancel) existing.cancel();

    var useCameraCursor = __DAO_ELEMENT_PICKER_USE_CAMERA_CURSOR__;
    var previousCursor = document.documentElement.style.cursor;
    var cameraCursor = 'url("${CAMERA_CURSOR_DATA_URI}") 12 12, crosshair';
    var pickerCursor = useCameraCursor ? cameraCursor : 'crosshair';
    var cursorStyle = null;
    if (useCameraCursor) {
      cursorStyle = document.createElement('style');
      cursorStyle.setAttribute('data-dao-element-picker-cursor', '1');
      cursorStyle.textContent = '* { cursor: ' + cameraCursor + ' !important; }';
    }
    var state = {active: true, result: null};
    var overlay = document.createElement('div');
    overlay.setAttribute('data-dao-element-picker-overlay', '1');
    overlay.style.position = 'fixed';
    overlay.style.left = '0';
    overlay.style.top = '0';
    overlay.style.width = '0';
    overlay.style.height = '0';
    overlay.style.border = '2px solid rgba(70, 120, 190, 0.95)';
    overlay.style.borderRadius = '8px';
    overlay.style.background = 'rgba(70, 120, 190, 0.12)';
    overlay.style.boxShadow = '0 0 0 9999px rgba(15, 23, 42, 0.08)';
    overlay.style.pointerEvents = 'none';
    overlay.style.zIndex = '2147483647';
    overlay.style.display = 'none';
    if (cursorStyle) document.documentElement.appendChild(cursorStyle);
    document.documentElement.appendChild(overlay);
    document.documentElement.style.cursor = pickerCursor;

    function cleanText(value, max) {
      var text = String(value || '').replace(/\\s+/g, ' ').trim();
      if (text.length > max) text = text.slice(0, max - 3) + '...';
      return text;
    }

    function getAttr(el, name) {
      try {
        var value = el.getAttribute(name);
        return value ? cleanText(value, 160) : '';
      } catch (_e) {
        return '';
      }
    }

    function cssEscape(value) {
      if (window.CSS && window.CSS.escape) return window.CSS.escape(value);
      return String(value).replace(/[^a-zA-Z0-9_-]/g, '\\\\$&');
    }

    function cssAttr(name, value) {
      return '[' + name + '="' +
          String(value).replace(/\\\\/g, '\\\\\\\\').replace(/"/g, '\\\\"') +
          '"]';
    }

    function isUnique(selector) {
      try {
        return document.querySelectorAll(selector).length === 1;
      } catch (_e) {
        return false;
      }
    }

    function getRole(el) {
      var role = getAttr(el, 'role');
      if (role) return role;
      var tag = el.tagName.toLowerCase();
      var type = getAttr(el, 'type').toLowerCase();
      if (tag === 'a') return 'link';
      if (tag === 'button') return 'button';
      if (tag === 'select') return 'combobox';
      if (tag === 'textarea') return 'textbox';
      if (tag === 'input') {
        if (type === 'checkbox') return 'checkbox';
        if (type === 'radio') return 'radio';
        if (type === 'submit' || type === 'button' || type === 'reset') {
          return 'button';
        }
        if (type === 'range') return 'slider';
        return 'textbox';
      }
      if (tag === 'img') return 'image';
      if (/^h[1-6]$/.test(tag)) return 'heading';
      return 'generic';
    }

    function getName(el) {
      var name = getAttr(el, 'aria-label') || getAttr(el, 'alt') ||
          getAttr(el, 'title') || getAttr(el, 'placeholder');
      if (name) return name;
      if (el.labels && el.labels.length) {
        return cleanText(el.labels[0].textContent, 120);
      }
      if (el.id) {
        try {
          var label = document.querySelector('label[for="' + cssEscape(el.id) + '"]');
          if (label) return cleanText(label.textContent, 120);
        } catch (_e) {}
      }
      return cleanText(el.innerText || el.textContent, 120);
    }

    function collectAttributes(el) {
      var names = [
        'id', 'name', 'type', 'value', 'placeholder', 'aria-label',
        'aria-labelledby', 'title', 'alt', 'href', 'data-testid',
        'data-test', 'data-cy'
      ];
      var out = {};
      for (var i = 0; i < names.length; i++) {
        var value = getAttr(el, names[i]);
        if (value) out[names[i]] = value;
      }
      return out;
    }

    function pathPart(el) {
      var tag = el.tagName.toLowerCase();
      var id = getAttr(el, 'id');
      if (id && isUnique(tag + '#' + cssEscape(id))) {
        return tag + '#' + cssEscape(id);
      }
      var stableAttrs = ['data-testid', 'data-test', 'data-cy', 'name'];
      for (var i = 0; i < stableAttrs.length; i++) {
        var value = getAttr(el, stableAttrs[i]);
        if (value) return tag + cssAttr(stableAttrs[i], value);
      }
      var index = 1;
      var sibling = el;
      while ((sibling = sibling.previousElementSibling)) {
        if (sibling.tagName === el.tagName) index++;
      }
      return tag + ':nth-of-type(' + index + ')';
    }

    function fallbackPath(el) {
      var parts = [];
      var cur = el;
      while (cur && cur.nodeType === Node.ELEMENT_NODE &&
             cur !== document.documentElement) {
        parts.unshift(pathPart(cur));
        var candidate = parts.join(' > ');
        if (isUnique(candidate)) return candidate;
        cur = cur.parentElement;
      }
      return parts.join(' > ');
    }

    function stableSelector(el) {
      var tag = el.tagName.toLowerCase();
      var attrs = ['data-testid', 'data-test', 'data-cy', 'id', 'name',
                   'aria-label', 'placeholder', 'href'];
      for (var i = 0; i < attrs.length; i++) {
        var value = getAttr(el, attrs[i]);
        if (!value) continue;
        var selector = tag + cssAttr(attrs[i], value);
        if (isUnique(selector)) return selector;
        selector = cssAttr(attrs[i], value);
        if (isUnique(selector)) return selector;
      }
      return fallbackPath(el);
    }

    function nearText(el) {
      var out = [];
      function add(value) {
        var text = cleanText(value, 120);
        if (text && out.indexOf(text) < 0) out.push(text);
      }
      if (el.labels) {
        for (var i = 0; i < el.labels.length; i++) add(el.labels[i].textContent);
      }
      if (el.id) {
        try {
          var labels = document.querySelectorAll('label[for="' + cssEscape(el.id) + '"]');
          for (var j = 0; j < labels.length; j++) add(labels[j].textContent);
        } catch (_e) {}
      }
      var label = el.closest && el.closest('label');
      if (label) add(label.textContent);
      if (el.previousElementSibling) add(el.previousElementSibling.textContent);
      var parent = el.parentElement;
      if (parent) add(parent.textContent);
      var form = el.closest && el.closest('form,[role="form"]');
      if (form) add(form.textContent);
      return out.slice(0, 5);
    }

    function capture(el) {
      var rect = el.getBoundingClientRect();
      var role = getRole(el);
      var name = getName(el);
      var text = cleanText(el.innerText || el.textContent, 300);
      var locator = {
        role: role,
        name: name,
        tag: el.tagName.toLowerCase(),
        text: text,
        attributes: collectAttributes(el),
        css: stableSelector(el),
        fallbackPath: fallbackPath(el),
        nearText: nearText(el),
        bounds: {
          x: Math.round(rect.left),
          y: Math.round(rect.top),
          width: Math.round(rect.width),
          height: Math.round(rect.height)
        }
      };
      return {
        status: 'selected',
        url: location.href,
        title: document.title || '',
        label: name || text || role,
        text: text,
        locator: locator,
        viewport: {
          width: Math.round(window.innerWidth ||
              document.documentElement.clientWidth || 0),
          height: Math.round(window.innerHeight ||
              document.documentElement.clientHeight || 0)
        }
      };
    }

    function eventElement(event) {
      var path = event.composedPath ? event.composedPath() : [];
      for (var i = 0; i < path.length; i++) {
        var node = path[i];
        if (node && node.nodeType === Node.ELEMENT_NODE &&
            node !== overlay && node !== document.documentElement) {
          return node;
        }
      }
      return document.elementFromPoint(event.clientX, event.clientY);
    }

    function moveOverlay(el) {
      if (!el || !el.getBoundingClientRect) return;
      var rect = el.getBoundingClientRect();
      overlay.style.display = 'block';
      overlay.style.left = Math.round(rect.left) + 'px';
      overlay.style.top = Math.round(rect.top) + 'px';
      overlay.style.width = Math.round(rect.width) + 'px';
      overlay.style.height = Math.round(rect.height) + 'px';
    }

    function onMove(event) {
      moveOverlay(eventElement(event));
    }

    function cleanup(result) {
      document.removeEventListener('mousemove', onMove, true);
      document.removeEventListener('mouseover', onMove, true);
      document.removeEventListener('click', onClick, true);
      document.removeEventListener('keydown', onKeyDown, true);
      document.documentElement.style.cursor = previousCursor;
      if (cursorStyle && cursorStyle.parentNode) {
        cursorStyle.parentNode.removeChild(cursorStyle);
      }
      if (overlay.parentNode) overlay.parentNode.removeChild(overlay);
      state.active = false;
      if (result) state.result = result;
    }

    function onClick(event) {
      event.preventDefault();
      event.stopPropagation();
      if (event.stopImmediatePropagation) event.stopImmediatePropagation();
      var el = eventElement(event);
      cleanup(capture(el));
      return false;
    }

    function onKeyDown(event) {
      if (event.key === 'Escape') {
        event.preventDefault();
        event.stopPropagation();
        cleanup({status: 'cancelled'});
      }
    }

    document.addEventListener('mousemove', onMove, true);
    document.addEventListener('mouseover', onMove, true);
    document.addEventListener('click', onClick, true);
    document.addEventListener('keydown', onKeyDown, true);

    window.__dao_element_picker__ = {
      active: true,
      getResult: function() {
        return state.result || {status: state.active ? 'pending' : 'cancelled'};
      },
      cancel: function() {
        cleanup({status: 'cancelled'});
        return true;
      }
    };

    return JSON.stringify({started: true});
  } catch (e) {
    return JSON.stringify({error: (e && e.message) || String(e)});
  }
})()`;

const ELEMENT_PICKER_RESULT_SCRIPT = `(function() {
  try {
    var picker = window.__dao_element_picker__;
    if (!picker || !picker.getResult) {
      return JSON.stringify({status: 'missing'});
    }
    return JSON.stringify(picker.getResult());
  } catch (e) {
    return JSON.stringify({error: (e && e.message) || String(e)});
  }
})()`;

const ELEMENT_PICKER_CANCEL_SCRIPT = `(function() {
  try {
    var picker = window.__dao_element_picker__;
    if (picker && picker.cancel) picker.cancel();
    return JSON.stringify({ok: true});
  } catch (e) {
    return JSON.stringify({ok: false, error: (e && e.message) || String(e)});
  }
})()`;

interface ElementPickerOptions {
  pollIntervalMs?: number;
  timeoutMs?: number;
  cameraCursor?: boolean;
}

function delay(ms: number): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function parseNativeJson(raw: unknown): Record<string, unknown> | null {
  const result = raw as {result?: unknown; error?: unknown} | null;
  if (!result || result.error || typeof result.result !== 'string') return null;
  try {
    const parsed = JSON.parse(result.result);
    return parsed && typeof parsed === 'object' ?
        parsed as Record<string, unknown> :
        null;
  } catch (_) {
    return null;
  }
}

type SelectedElementContextPayload =
    Record<string, unknown> & ElementContextCapture & {status: 'selected'};

function isElementContextCapture(value: Record<string, unknown>):
    value is SelectedElementContextPayload {
  return value['status'] === 'selected' &&
      typeof value['url'] === 'string' &&
      typeof value['title'] === 'string' &&
      typeof value['label'] === 'string' &&
      typeof value['text'] === 'string' &&
      !!value['locator'] &&
      typeof value['locator'] === 'object';
}

export async function cancelElementPicker(): Promise<void> {
  try {
    await callNative('executeScript', {
      code: ELEMENT_PICKER_CANCEL_SCRIPT,
      lockTab: false,
    });
  } catch (_) { /* best-effort */ }
}

export async function startElementPicker(
    options: ElementPickerOptions = {}): Promise<ElementContextCapture | null> {
  const pollIntervalMs = options.pollIntervalMs ?? 150;
  const timeoutMs = options.timeoutMs ?? 30000;
  const startScript = ELEMENT_PICKER_START_SCRIPT.replace(
      '__DAO_ELEMENT_PICKER_USE_CAMERA_CURSOR__',
      options.cameraCursor ? 'true' : 'false');
  const started = parseNativeJson(await callNative('executeScript', {
    code: startScript,
    lockTab: false,
  }));
  if (!started || started['error']) return null;

  const deadline = Date.now() + timeoutMs;
  while (Date.now() <= deadline) {
    await delay(pollIntervalMs);
    const payload = parseNativeJson(await callNative('executeScript', {
      code: ELEMENT_PICKER_RESULT_SCRIPT,
      lockTab: false,
    }));
    if (!payload) continue;
    if (isElementContextCapture(payload)) {
      const capture: ElementContextCapture = {
        url: payload.url,
        title: payload.title,
        label: payload.label,
        text: payload.text,
        locator: payload.locator,
      };
      if (payload.viewport) capture.viewport = payload.viewport;
      return capture;
    }
    if (payload['status'] === 'cancelled' || payload['status'] === 'missing' ||
        payload['error']) {
      return null;
    }
  }

  await cancelElementPicker();
  return null;
}

export async function captureElementScreenshotFromPage():
    Promise<PiAttachment | null> {
  const capture = await startElementPicker({cameraCursor: true});
  if (!capture) return null;
  const viewport = capture.viewport;
  if (!viewport) return null;
  const clip = clampScreenshotClip(capture.locator.bounds, viewport);
  if (!clip) return null;
  const result = await callNative('captureScreenshot', {clip}) as
      {data?: string; format?: string; error?: string};
  if (result.error || !result.data) return null;
  return buildElementScreenshotAttachment(
      capture, result.data, result.format || 'jpeg');
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
