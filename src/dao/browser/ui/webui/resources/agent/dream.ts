// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

import {installDaoMarkdownPreprocess} from './dao_markdown.js';
import {marked} from './vendor/pi_runtime_bundle.js';

installDaoMarkdownPreprocess(marked);

import './dao_dream_app.js';
