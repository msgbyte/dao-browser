// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {PageInfo, PiAttachment} from './dao_page_capture.js';

export interface ElementLocator {
  role: string;
  name: string;
  tag: string;
  text: string;
  attributes: Record<string, string>;
  css: string;
  fallbackPath: string;
  nearText: string[];
  bounds: {x: number; y: number; width: number; height: number};
}

export interface ElementViewport {
  width: number;
  height: number;
}

export interface ElementContextCapture extends PageInfo {
  contextId?: string;
  label: string;
  text: string;
  locator: ElementLocator;
  viewport?: ElementViewport;
}

export const REUSABLE_ELEMENT_CONTEXT_STORAGE_KEY =
    'dao_reusable_element_context';

function isRecord(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

function isStringRecord(value: unknown): value is Record<string, string> {
  return isRecord(value) &&
      Object.values(value).every(item => typeof item === 'string');
}

function isBounds(value: unknown):
    value is {x: number; y: number; width: number; height: number} {
  return isRecord(value) &&
      typeof value['x'] === 'number' &&
      typeof value['y'] === 'number' &&
      typeof value['width'] === 'number' &&
      typeof value['height'] === 'number';
}

function isElementViewport(value: unknown): value is ElementViewport {
  return isRecord(value) &&
      typeof value['width'] === 'number' &&
      typeof value['height'] === 'number';
}

function isElementLocator(value: unknown): value is ElementLocator {
  return isRecord(value) &&
      typeof value['role'] === 'string' &&
      typeof value['name'] === 'string' &&
      typeof value['tag'] === 'string' &&
      typeof value['text'] === 'string' &&
      isStringRecord(value['attributes']) &&
      typeof value['css'] === 'string' &&
      typeof value['fallbackPath'] === 'string' &&
      Array.isArray(value['nearText']) &&
      value['nearText'].every(item => typeof item === 'string') &&
      isBounds(value['bounds']);
}

function isElementContextCapture(value: unknown):
    value is ElementContextCapture {
  return isRecord(value) &&
      (value['contextId'] === undefined ||
       typeof value['contextId'] === 'string') &&
      typeof value['url'] === 'string' &&
      typeof value['title'] === 'string' &&
      typeof value['label'] === 'string' &&
      typeof value['text'] === 'string' &&
      isElementLocator(value['locator']) &&
      (value['viewport'] === undefined ||
       isElementViewport(value['viewport']));
}

function getStorage(): Storage | null {
  try {
    return globalThis.localStorage ?? null;
  } catch (_) {
    return null;
  }
}

function makeElementContextId(context: ElementContextCapture): string {
  const basis = [
    context.url,
    context.label,
    context.locator.role,
    context.locator.name,
    context.locator.tag,
    context.locator.css,
    context.locator.fallbackPath,
  ].join('\n');
  let hash = 0;
  for (let i = 0; i < basis.length; i++) {
    hash = ((hash << 5) - hash + basis.charCodeAt(i)) | 0;
  }
  return `ctx_${(hash >>> 0).toString(36)}`;
}

function normalizeElementContext(
    context: ElementContextCapture): ElementContextCapture {
  return {
    ...context,
    contextId: context.contextId || makeElementContextId(context),
  };
}

function readStoredElementContexts(): ElementContextCapture[] {
  try {
    const raw = getStorage()?.getItem(REUSABLE_ELEMENT_CONTEXT_STORAGE_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw);
    if (Array.isArray(parsed)) {
      return parsed.filter(isElementContextCapture)
          .map(normalizeElementContext);
    }
    return isElementContextCapture(parsed) ?
        [normalizeElementContext(parsed)] :
        [];
  } catch (_) {
    return [];
  }
}

function writeStoredElementContexts(contexts: ElementContextCapture[]): void {
  try {
    getStorage()?.setItem(
        REUSABLE_ELEMENT_CONTEXT_STORAGE_KEY, JSON.stringify(contexts));
  } catch (_) { /* storage unavailable/full — keep in memory only */ }
}

let reusableElementContexts: ElementContextCapture[] =
    readStoredElementContexts();

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

function compactLine(label: string, value: string | string[]): string {
  const text = Array.isArray(value) ? value.filter(Boolean).join(', ') : value;
  return text ? `${label}: ${text}` : '';
}

export function setReusableElementContext(context: ElementContextCapture): void {
  reusableElementContexts = [normalizeElementContext(context)];
  writeStoredElementContexts(reusableElementContexts);
}

export function addReusableElementContext(
    context: ElementContextCapture): ElementContextCapture {
  const normalized = normalizeElementContext(context);
  const next = reusableElementContexts.slice();
  const existingIndex =
      next.findIndex(item => item.contextId === normalized.contextId);
  if (existingIndex >= 0) {
    next[existingIndex] = normalized;
  } else {
    next.push(normalized);
  }
  reusableElementContexts = next;
  writeStoredElementContexts(reusableElementContexts);
  return normalized;
}

export function getReusableElementContexts(): ElementContextCapture[] {
  if (reusableElementContexts.length === 0) {
    reusableElementContexts = readStoredElementContexts();
  }
  return reusableElementContexts.slice();
}

export function getReusableElementContext(): ElementContextCapture | null {
  return getReusableElementContexts()[0] || null;
}

export function removeReusableElementContext(contextId: string): void {
  reusableElementContexts =
      reusableElementContexts.filter(item => item.contextId !== contextId);
  writeStoredElementContexts(reusableElementContexts);
}

export function clearReusableElementContext(): void {
  reusableElementContexts = [];
  try {
    getStorage()?.removeItem(REUSABLE_ELEMENT_CONTEXT_STORAGE_KEY);
  } catch (_) { /* ignore */ }
}

export function consumeReusableElementContexts(): ElementContextCapture[] {
  const contexts = getReusableElementContexts();
  clearReusableElementContext();
  return contexts;
}

export function buildElementContextAttachment(capture: ElementContextCapture):
    PiAttachment {
  const safeTitle = capture.title.replace(/"/g, '&quot;');
  const safeUrl = capture.url.replace(/"/g, '&quot;');
  const safeLabel = capture.label.replace(/"/g, '&quot;');
  const contextId = capture.contextId || makeElementContextId(capture);
  const safeContextId = contextId.replace(/"/g, '&quot;');
  const payload = {
    contextId,
    label: capture.label,
    url: capture.url,
    title: capture.title,
    text: capture.text,
    locator: capture.locator,
  };
  const json = JSON.stringify(payload, null, 2);
  const lines = [
    compactLine('role', capture.locator.role),
    compactLine('name', capture.locator.name),
    compactLine('text', capture.text),
    compactLine('near', capture.locator.nearText),
  ].filter(Boolean);
  const extractedText =
      `<element-context context_id="${safeContextId}" url="${safeUrl}" ` +
      `title="${safeTitle}" label="${safeLabel}">\n${lines.join('\n')}` +
      `\n</element-context>`;
  const safeFileName =
      (capture.label || capture.locator.name || capture.locator.role || 'element')
          .replace(/[\\/\n\r\t\x00-\x1f]+/g, ' ')
          .replace(/\s+/g, ' ')
          .slice(0, 60) + '.element.json';
  const id =
      `dao-element-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  return {
    id,
    type: 'document',
    fileName: safeFileName,
    mimeType: 'application/json',
    size: new TextEncoder().encode(json).length,
    content: utf8ToBase64(json),
    extractedText,
    daoPageUrl: capture.url,
    daoPageTitle: capture.title,
  };
}

export function makeResolveElementContextScript(locator: ElementLocator): string {
  const locatorJson = JSON.stringify(locator);
  return `(function() {
    try {
      var locator = ${locatorJson};
      var selectedAttr = 'data-dao-element-context';
      var old = document.querySelectorAll('[' + selectedAttr + ']');
      for (var oi = 0; oi < old.length; oi++) old[oi].removeAttribute(selectedAttr);

      function clean(value) {
        return String(value || '').replace(/\\s+/g, ' ').trim();
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

      function getAttr(el, name) {
        try {
          return clean(el.getAttribute(name));
        } catch (_e) {
          return '';
        }
      }

      function roleOf(el) {
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
        return 'generic';
      }

      function nameOf(el) {
        var name = getAttr(el, 'aria-label') || getAttr(el, 'alt') ||
            getAttr(el, 'title') || getAttr(el, 'placeholder');
        if (name) return name;
        if (el.labels && el.labels.length) return clean(el.labels[0].textContent);
        if (el.id) {
          try {
            var label = document.querySelector('label[for="' + cssEscape(el.id) + '"]');
            if (label) return clean(label.textContent);
          } catch (_e) {}
        }
        return clean(el.innerText || el.textContent);
      }

      function addCandidate(list, el, matchedBy) {
        if (!el || !el.getBoundingClientRect || list.some(function(c) { return c.el === el; })) {
          return;
        }
        list.push({el: el, matchedBy: matchedBy.slice()});
      }

      function queryOne(selector) {
        try {
          var matches = document.querySelectorAll(selector);
          return matches.length === 1 ? matches[0] : null;
        } catch (_e) {
          return null;
        }
      }

      var candidates = [];
      var attrs = locator.attributes || {};
      ['data-testid', 'data-test', 'data-cy', 'id', 'name', 'aria-label', 'placeholder', 'href'].forEach(function(name) {
        var value = attrs[name];
        if (!value) return;
        var selector = cssAttr(name, value);
        addCandidate(candidates, queryOne(selector), [name]);
        if (locator.tag) {
          addCandidate(candidates, queryOne(locator.tag + selector), [name]);
        }
      });
      if (locator.css) addCandidate(candidates, queryOne(locator.css), ['css']);
      if (locator.fallbackPath) {
        addCandidate(candidates, queryOne(locator.fallbackPath), ['fallbackPath']);
      }

      var scanSelector = locator.tag && locator.tag !== 'generic' ?
          locator.tag : 'a,button,input,select,textarea,[role],[tabindex]';
      try {
        var all = document.querySelectorAll(scanSelector);
        for (var i = 0; i < all.length; i++) addCandidate(candidates, all[i], ['scan']);
      } catch (_e) {}

      function score(el, matchedBy) {
        var points = 0;
        var why = matchedBy.slice();
        var role = roleOf(el);
        var name = nameOf(el);
        var text = clean(el.innerText || el.textContent);
        if (locator.tag && el.tagName.toLowerCase() === locator.tag) {
          points += 15;
          why.push('tag');
        }
        if (locator.role && role === locator.role) {
          points += 20;
          why.push('role');
        }
        if (locator.name && name === locator.name) {
          points += 25;
          why.push('name');
        }
        if (locator.text && text === locator.text) {
          points += 20;
          why.push('text');
        }
        Object.keys(locator.attributes || {}).forEach(function(attrName) {
          if (getAttr(el, attrName) === locator.attributes[attrName]) {
            points += attrName.indexOf('data-') === 0 || attrName === 'id' ? 40 : 25;
            why.push(attrName);
          }
        });
        var containerText = clean(el.parentElement ? el.parentElement.textContent : '');
        (locator.nearText || []).forEach(function(near) {
          if (near && containerText.indexOf(near) >= 0) {
            points += 5;
            why.push('nearText');
          }
        });
        return {score: Math.min(points, 100), matchedBy: Array.from(new Set(why))};
      }

      var best = null;
      for (var ci = 0; ci < candidates.length; ci++) {
        var scored = score(candidates[ci].el, candidates[ci].matchedBy);
        if (!best || scored.score > best.score) {
          best = {el: candidates[ci].el, score: scored.score, matchedBy: scored.matchedBy};
        }
      }
      if (!best || best.score < 60) {
        return JSON.stringify({resolved: false, score: best ? best.score : 0});
      }
      best.el.setAttribute(selectedAttr, 'current');
      var rect = best.el.getBoundingClientRect();
      return JSON.stringify({
        resolved: true,
        selector: '[' + selectedAttr + '="current"]',
        score: best.score,
        matchedBy: best.matchedBy,
        label: nameOf(best.el) || clean(best.el.textContent),
        text: clean(best.el.innerText || best.el.textContent),
        bounds: {
          x: Math.round(rect.left),
          y: Math.round(rect.top),
          width: Math.round(rect.width),
          height: Math.round(rect.height)
        }
      });
    } catch (e) {
      return JSON.stringify({resolved: false, error: (e && e.message) || String(e)});
    }
  })()`;
}
