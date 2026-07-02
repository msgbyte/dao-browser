// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { describe, expect, it } from 'vitest';

import { currentLocale, t } from '../i18n/i18n.js';
import zhCN from '../i18n/locales/zh-CN.js';

describe('i18n.t()', () => {
  it('returns the English string for a known key without init', () => {
    expect(t('chat.compact.label')).toBe('Compact');
  });

  it('substitutes single {var} placeholders', () => {
    const out = t('chat.compact.success', { count: 4 });
    expect(out).toBe('Compacted 4 messages → 1 summary');
  });

  it('substitutes multiple placeholders', () => {
    const out = t('chat.gauge.tooltip_with_capacity', {
      tokens: 1234, capacity: '128k', percent: 50,
    });
    expect(out).toContain('1234');
    expect(out).toContain('128k');
    expect(out).toContain('50');
  });

  it('falls back to the literal key when missing in en', () => {
    expect(t('this.key.does.not.exist')).toBe('this.key.does.not.exist');
  });

  it('coerces numeric vars to strings', () => {
    expect(t('chat.compact.success', { count: 0 }))
      .toBe('Compacted 0 messages → 1 summary');
  });

  it('exposes the active locale (default "en")', () => {
    // Without initI18n() being awaited in the test environment,
    // the active locale should be the synchronous default.
    expect(currentLocale()).toBe('en');
  });

  it('describes proactive never-here as page-scoped', () => {
    expect(t('chat.proactive.never_here'))
        .toBe("Don't suggest on this page");
    expect(t('chat.proactive.never_here_aria', {title: 'Review this PR'}))
        .toBe("Don't suggest on this page again: Review this PR");
    expect(zhCN['chat.proactive.never_here']).toBe('不要在此页建议');
    expect(zhCN['chat.proactive.never_here_aria'])
        .toBe('不要再在此页建议:{title}');
  });
});
