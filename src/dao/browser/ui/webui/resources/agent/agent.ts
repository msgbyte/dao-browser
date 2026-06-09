// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Chromium WebUI enforces Trusted Types. Create a default policy that
// passes HTML through — our markdown renderer already escapes user input.
{
  const w = window as unknown as
      {trustedTypes?: {createPolicy: (name: string, rules: object) => void}};
  if (w.trustedTypes && w.trustedTypes.createPolicy) {
    w.trustedTypes.createPolicy('default', {
      createHTML: (s: string) => s,
      createScript: (s: string) => s,
      createScriptURL: (s: string) => s,
    });
  }
}

// CommonMark left/right-flanking rules treat fullwidth CJK punctuation
// as Unicode punctuation but don't grant "punctuation neighbor" status
// to ASCII or CJK letters. So a **/*/__/_ wedged between e.g. `：` and
// `Claude` (or between `：` and `说`) never closes — the asterisks
// render literally. This bites both turndown-produced markdown (page
// captures) and any markdown the LLM emits inline. Install a preprocess
// hook on the same `marked` singleton that mini-lit's <markdown-block>
// renders with, so the fix applies uniformly to chat bubbles, thinking
// blocks, and Markdown artifacts. Registered before <dao-agent-app>'s
// import so it's in place by the first render.
import {marked} from './vendor/pi_runtime_bundle.js';

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

function preprocessDaoMarkdown(input: string): string {
  if (!input || (input.indexOf('*') < 0 && input.indexOf('_') < 0)) {
    return input;
  }
  return transformMarkdownOutsideCode(
      input, md => fixEmphasisTrailingSpaces(fixCjkEmphasisFlanking(md)));
}

(marked as {use: (ext: object) => void}).use({
  hooks: {
    preprocess(md: string): string {
      return preprocessDaoMarkdown(md);
    },
  },
});

import './dao_agent_app.js';
