// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {readFileSync} from 'node:fs';
import {describe, expect, it} from 'vitest';
import '../vendor/pi_runtime_bundle.js';

async function renderMarkdown(content: string): Promise<string> {
  const block = document.createElement('markdown-block') as HTMLElement & {
    content: string;
    updateComplete: Promise<boolean>;
  };
  block.content = content;
  document.body.append(block);
  try {
    await block.updateComplete;
    return block.innerHTML;
  } finally {
    block.remove();
  }
}

describe('bundled agent math rendering', () => {
  it.each(['$x^2$', '$$x^2$$', String.raw`\(x^2\)`, String.raw`\[x^2\]`])(
      'renders supported math delimiters: %s', async markdown => {
        expect(await renderMarkdown(markdown)).toContain('class="katex"');
      });

  it('applies the bundled stylesheet to rendered math layout', async () => {
    const style = document.createElement('style');
    style.textContent = readFileSync(
        'src/dao/browser/ui/webui/resources/agent/vendor/pi_web_ui.css', 'utf8');
    const container = document.createElement('div');
    container.innerHTML = await renderMarkdown('$x^2$');
    document.head.append(style);
    document.body.append(container);
    try {
      const base = container.querySelector('.katex-html > span');
      expect(base).not.toBeNull();
      expect(getComputedStyle(base!).display).toBe('inline-block');
    } finally {
      container.remove();
      style.remove();
    }
  });

  it('does not trust math links through inherited renderer options', async () => {
    const original = Object.getOwnPropertyDescriptor(Object.prototype, 'trust');
    let rendered: string;
    try {
      Object.defineProperty(Object.prototype, 'trust', {
        value: true, writable: true, configurable: true,
      });
      rendered = await renderMarkdown(
          String.raw`$\href{javascript:alert(1)}{unsafe}$`);
    } finally {
      if (original) {
        Object.defineProperty(Object.prototype, 'trust', original);
      } else {
        Reflect.deleteProperty(Object.prototype, 'trust');
      }
    }
    expect(rendered).toContain('class="katex"');
    expect(rendered).not.toContain('href="javascript:');
  });
});
