// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {describe, expect, it} from 'vitest';

import {
  buildMemoryContextAttachment,
  buildMemoryContextText,
  hasMemoryContextPayload,
  type NativeMemoryContext,
} from '../dao_memory_context.js';

describe('dao memory context helper', () => {
  it('detects empty memory payloads', () => {
    expect(hasMemoryContextPayload({})).toBe(false);
    expect(hasMemoryContextPayload({
      preferences: [],
      episodes: [],
      recentMessages: [],
      relevantSummary: null,
    })).toBe(false);
  });

  it('renders preferences, summary, and episodes with escaped XML', () => {
    const payload: NativeMemoryContext = {
      preferences: [{
        key: 'response.style',
        value: 'Use <short> answers & cite "files".',
        confidence: 0.91,
      }],
      relevantSummary: {
        summary: 'User reviews PRs on "github" <dao> & expects stable "quotes".',
        primaryDomain: 'github.com',
      },
      episodes: [{
        title: 'Pull request "review" <stage>',
        intent: 'Review auth changes with "strict" <checks>.',
        outcome: 'Pointed out missing tests because A&B<>" are ambiguous.',
        confidence: 0.77,
      }],
    };

    const text = buildMemoryContextText({
      url: 'https://github.com/example/repo/pull/1',
      domain: 'github.com',
      payload,
    });

    expect(text).toContain('<memory-context source="dao-agent-memory" domain="github.com"');
    expect(text).toContain('Use &lt;short&gt; answers &amp; cite &quot;files&quot;.');
    expect(text).toContain(
        'User reviews PRs on &quot;github&quot; &lt;dao&gt; &amp; expects stable &quot;quotes&quot;.');
    expect(text).toContain('<summary domain="github.com">');
    expect(text).toContain(
        '<episode confidence="0.77" title="Pull request &quot;review&quot; &lt;stage&gt;">');
    expect(text).toContain('Review auth changes with &quot;strict&quot; &lt;checks&gt;.');
    expect(text).toContain('Pointed out missing tests because A&amp;B&lt;&gt;&quot; are ambiguous.');
  });

  it('builds a hidden pi attachment with extracted text', () => {
    const attachment = buildMemoryContextAttachment({
      url: 'https://example.com/a',
      domain: 'example.com',
      payload: {
        preferences: [{
          key: 'interest.dao',
          value: 'Likes Dao Browser implementation details',
          confidence: 0.8,
        }],
      },
    });

    expect(attachment).not.toBeNull();
    if (!attachment) return;
    expect(attachment.type).toBe('document');
    expect(attachment.fileName).toBe('dao-memory-context.md');
    expect(attachment.mimeType).toBe('text/markdown');
    expect(atob(attachment.content)).toBe(attachment.extractedText);
    expect(attachment.extractedText).toContain('<memory-context');
    expect(attachment.preview).toBe('');
  });

  it('trims oversized memory values while preserving closing tags', () => {
    const longValue = 'A'.repeat(12000);
    const text = buildMemoryContextText({
      url: 'https://example.com/a',
      domain: 'example.com',
      charBudget: 1600,
      payload: {
        preferences: [{
          key: 'long.pref',
          value: longValue,
          confidence: 0.9,
        }],
        episodes: [{
          intent: longValue,
          outcome: longValue,
          confidence: 0.7,
        }],
      },
    });

    expect(text.length).toBeLessThanOrEqual(1900);
    expect(text).toContain('truncated');
    expect(text).toContain('</memory-context>');
  });

  it('keeps tiny budgets safe around escaped XML entities', () => {
    const payload: NativeMemoryContext = {
      preferences: [{
        key: 'xml.sensitive',
        value: ('& < > " \' ').repeat(30),
        confidence: 0.75,
      }],
    };
    const text = buildMemoryContextText({
      url: 'https://example.com/edge-case',
      domain: 'example.com',
      charBudget: 180,
      payload,
    });

    expect(text).toContain('</memory-context>');
    expect(text).toContain('<truncated>true</truncated>');

    for (const match of text.matchAll(/&/g)) {
      const fragment = text.slice(match.index, match.index + 6);
      expect(fragment).toMatch(/^&(amp;|lt;|gt;|quot;|#39;)/);
    }
  });
});
