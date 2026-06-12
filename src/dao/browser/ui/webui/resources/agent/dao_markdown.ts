// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {marked as defaultMarked} from './vendor/pi_runtime_bundle.js';

const CJK_PUNCT_CLASS = '[　-〿＀-￯‘-‟…—–]';
const CLOSE_FIX = new RegExp(
    `(${CJK_PUNCT_CLASS})(\\*\\*|__|\\*|_)(?=[\\p{L}\\p{N}])`, 'gu');
const OPEN_FIX = new RegExp(
    `(?<=[\\p{L}\\p{N}])(\\*\\*|__|\\*|_)(${CJK_PUNCT_CLASS})`, 'gu');
const CODE_PLACEHOLDER_PREFIX = '\uE000DAO_CODE_';
const CODE_PLACEHOLDER_SUFFIX = '\uE001';
const CODE_SPAN_OR_BLOCK = /```[\s\S]*?```|`[^`\n]+`/g;
const CODE_PLACEHOLDER = new RegExp(
    `${CODE_PLACEHOLDER_PREFIX}(\\d+)${CODE_PLACEHOLDER_SUFFIX}`, 'g');

interface MarkedLike {
  use?: (ext: object) => void;
  parse?: (markdown: string, options?: {async?: boolean}) => unknown;
}

function fixCjkEmphasisFlanking(input: string): string {
  if (!input || (input.indexOf('*') < 0 && input.indexOf('_') < 0)) {
    return input;
  }
  return input.replace(CLOSE_FIX, '$1$2 ').replace(OPEN_FIX, ' $1$2');
}

function transformMarkdownOutsideCode(
    input: string, transform: (markdown: string) => string): string {
  const codeSegments: string[] = [];
  const masked = input.replace(CODE_SPAN_OR_BLOCK, match => {
    const index = codeSegments.length;
    codeSegments.push(match);
    return `${CODE_PLACEHOLDER_PREFIX}${index}${CODE_PLACEHOLDER_SUFFIX}`;
  });
  return transform(masked).replace(CODE_PLACEHOLDER, (_match, index) => {
    return codeSegments[Number(index)] ?? '';
  });
}

function fixEmphasisTrailingSpaces(input: string): string {
  if (!input || (input.indexOf('*') < 0 && input.indexOf('_') < 0)) {
    return input;
  }
  return input
      .replace(/(\*\*)[ \t]+([^\n]*?\S)[ \t]*(\*\*)/g, '$1$2$3')
      .replace(/(\*\*)(?=\S)([^\n]*?\S)[ \t]+(\*\*)/g, '$1$2$3')
      .replace(/(__)[ \t]+([^\n]*?\S)[ \t]*(__)/g, '$1$2$3')
      .replace(/(__)(?=\S)([^\n]*?\S)[ \t]+(__)/g, '$1$2$3')
      .replace(/(?<!\*)\*(?!\*)[ \t]+([^\n*]*?\S)[ \t]+\*(?!\*)/g, '*$1*')
      .replace(/(?<!\*)\*(?!\*)([^\n*]*?\S)[ \t]+\*(?!\*)/g, '*$1*')
      .replace(/(?<!_)_(?!_)[ \t]+([^\n_]*?\S)[ \t]+_(?!_)/g, '_$1_')
      .replace(/(?<!_)_(?!_)([^\n_]*?\S)[ \t]+_(?!_)/g, '_$1_');
}

export function preprocessDaoMarkdown(input: string): string {
  if (!input || (input.indexOf('*') < 0 && input.indexOf('_') < 0)) {
    return input;
  }
  return transformMarkdownOutsideCode(
      input, md => fixEmphasisTrailingSpaces(fixCjkEmphasisFlanking(md)));
}

export function installDaoMarkdownPreprocess(marked: MarkedLike): void {
  marked.use?.({
    hooks: {
      preprocess(md: string): string {
        return preprocessDaoMarkdown(md);
      },
    },
  });
}

function escapeHtml(s: string): string {
  return s.replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
}

export function renderDaoMarkdown(markdown: string): string {
  const m = defaultMarked as MarkedLike;
  const markedFn =
      defaultMarked as unknown as (markdown: string) => unknown;
  try {
    const out = m.parse?.(markdown, {async: false}) ??
        (typeof defaultMarked === 'function' ? markedFn(markdown) :
                                             undefined);
    if (typeof out === 'string') {
      return out;
    }
  } catch (e) {
    console.warn('[dao] markdown render failed; falling back to <pre>', e);
  }
  return '<pre>' + escapeHtml(markdown) + '</pre>';
}
