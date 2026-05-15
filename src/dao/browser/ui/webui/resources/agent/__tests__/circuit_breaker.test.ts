// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import {
  clearJinaBreaker,
  isJinaAvailable,
  markJinaUnavailable,
} from '../web_search/circuit_breaker.js';

describe('web_search/circuit_breaker', () => {
  beforeEach(() => {
    localStorage.clear();
    vi.useRealTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('reports available when nothing is stored', () => {
    expect(isJinaAvailable()).toBe(true);
  });

  it('marks jina unavailable for the TTL window then auto-clears', () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-05-15T00:00:00Z'));

    markJinaUnavailable();
    expect(isJinaAvailable()).toBe(false);

    vi.advanceTimersByTime(9 * 60 * 1000);
    expect(isJinaAvailable()).toBe(false);

    vi.advanceTimersByTime(2 * 60 * 1000);
    expect(isJinaAvailable()).toBe(true);
    expect(localStorage.getItem('dao_jina_unavailable_until')).toBeNull();
  });

  it('clearJinaBreaker forces availability before TTL', () => {
    markJinaUnavailable();
    expect(isJinaAvailable()).toBe(false);

    clearJinaBreaker();
    expect(isJinaAvailable()).toBe(true);
  });

  it('treats non-numeric stored values as available (and tolerates them)', () => {
    localStorage.setItem('dao_jina_unavailable_until', 'not-a-number');
    expect(isJinaAvailable()).toBe(true);
  });

  it('survives localStorage throwing on read', () => {
    const spy = vi
      .spyOn(Storage.prototype, 'getItem')
      .mockImplementation(() => {
        throw new Error('storage disabled');
      });
    expect(isJinaAvailable()).toBe(true);
    spy.mockRestore();
  });
});
