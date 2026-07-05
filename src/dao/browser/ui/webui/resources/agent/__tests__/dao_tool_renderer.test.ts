// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

type TemplateValue =
    TemplateResult|string|number|boolean|null|undefined|TemplateValue[];

interface TemplateResult {
  strings: TemplateStringsArray;
  values: TemplateValue[];
}

interface ToolRenderer {
  render(params: string, result: unknown, isStreaming: boolean):
      {content: unknown; isCustom: boolean};
}

const mocks = vi.hoisted(() => ({
  renderers: new Map<string, ToolRenderer>(),
}));

function escapeHtml(value: string): string {
  return value.replaceAll('&', '&amp;')
      .replaceAll('<', '&lt;')
      .replaceAll('>', '&gt;')
      .replaceAll('"', '&quot;');
}

function isTemplateResult(value: TemplateValue): value is TemplateResult {
  return typeof value === 'object' && value !== null && 'strings' in value &&
      'values' in value;
}

function templateHtml(value: TemplateValue): string {
  if (value === null || value === undefined || value === false) {
    return '';
  }
  if (Array.isArray(value)) {
    return value.map(templateHtml).join('');
  }
  if (isTemplateResult(value)) {
    let out = '';
    for (let i = 0; i < value.values.length; i++) {
      out += value.strings[i] ?? '';
      out += templateHtml(value.values[i]);
    }
    out += value.strings[value.strings.length - 1] ?? '';
    return out;
  }
  return escapeHtml(String(value));
}

vi.mock('../vendor/pi_runtime_bundle.js', () => ({
  html(strings: TemplateStringsArray, ...values: TemplateValue[]):
      TemplateResult {
    return {strings, values};
  },
  registerToolRenderer(name: string, renderer: ToolRenderer) {
    mocks.renderers.set(name, renderer);
  },
}));

import {registerDaoToolRenderers} from '../dao_tool_renderer.js';

describe('dao_tool_renderer', () => {
  beforeEach(() => {
    mocks.renderers.clear();
    localStorage.clear();
  });

  it('adds native title text to collapsed tool-call labels', () => {
    const longToolName =
        'very_long_dao_tool_name_that_should_be_available_on_hover';
    const longFetchTitle =
        'Extremely long fetched document title that should remain discoverable';

    registerDaoToolRenderers([longToolName, 'fetch_url']);

    const regularRenderer = mocks.renderers.get(longToolName);
    const fetchRenderer = mocks.renderers.get('fetch_url');

    expect(regularRenderer).toBeTruthy();
    expect(fetchRenderer).toBeTruthy();

    const regular = regularRenderer!.render('{"example":true}', null, false);
    const regularHtml = templateHtml(regular.content as TemplateValue);
    expect(regularHtml).toContain(
        `class="dao-tool-call-summary" title="${longToolName}"`);
    expect(regularHtml).toContain(
        `class="dao-tool-call-name" title="${longToolName}"`);

    const fetchResult = {
      content: [{
        type: 'text',
        text: JSON.stringify({
          source: 'custom-source',
          url: 'https://example.com/article',
          title: longFetchTitle,
          content: '# Body',
        }),
      }],
    };

    const fetch = fetchRenderer!.render('', fetchResult, false);
    const fetchHtml = templateHtml(fetch.content as TemplateValue);
    expect(fetchHtml).toContain(
        `class="dao-tool-call-summary" title="custom-source  Read: ${
            longFetchTitle}"`);
    expect(fetchHtml).toContain(
        `class="dao-tool-call-name" title="custom-source  Read: ${
            longFetchTitle}"`);
  });
});
