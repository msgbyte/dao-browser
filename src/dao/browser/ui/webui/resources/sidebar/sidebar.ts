// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Chromium WebUI enforces Trusted Types. Create a default policy that
// passes HTML through — Lit already escapes user input.
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

import './dao_sidebar_app.js';

// Suppress the default web page context menu in the sidebar WebView.
document.addEventListener('contextmenu', (e: MouseEvent) => {
  e.preventDefault();
});

// Global drag cleanup: if a drag operation ends without a proper drop event
// (e.g. cancelled, or a native drag took over), clear any residual drag state
// from all components to prevent the sidebar from becoming non-interactive.
document.addEventListener('dragend', () => {
  document.querySelectorAll('.drag-over').forEach(
      el => el.classList.remove('drag-over'));
  const tabLists = document.querySelectorAll('dao-tab-list');
  tabLists.forEach(el => {
    el.classList.remove('drag-over');
    if (el.shadowRoot) {
      el.shadowRoot.querySelectorAll('.drag-over').forEach(
          inner => inner.classList.remove('drag-over'));
    }
  });
});
