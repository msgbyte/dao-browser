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

function fixCjkEmphasisFlanking(input: string): string {
  if (!input || (input.indexOf('*') < 0 && input.indexOf('_') < 0)) {
    return input;
  }
  return input.replace(CLOSE_FIX, '$1$2 ').replace(OPEN_FIX, ' $1$2');
}

(marked as {use: (ext: object) => void}).use({
  hooks: {
    preprocess(md: string): string {
      return fixCjkEmphasisFlanking(md);
    },
  },
});

import './dao_agent_app.js';
