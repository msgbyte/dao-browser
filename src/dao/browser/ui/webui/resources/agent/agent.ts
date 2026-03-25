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

import './dao_agent_app.js';
