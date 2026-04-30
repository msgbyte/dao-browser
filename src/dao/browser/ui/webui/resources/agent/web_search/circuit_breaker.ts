// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Time-limited breaker that flags Jina (s.jina.ai / r.jina.ai) as
// temporarily unavailable after a 429. We rely on localStorage so the
// breaker state is shared across all chrome://dao-agent tabs in this
// profile — if the user has two agent tabs open, neither hammers Jina
// while the breaker is tripped.
//
// The breaker is intentionally short (10 min). Jina restores per-IP
// quota on a rolling window; we want to start trying again well before
// the user gives up and reaches for a different tool.

const KEY = 'dao_jina_unavailable_until';
const TTL_MS = 10 * 60 * 1000;

export function isJinaAvailable(): boolean {
  let raw: string | null = null;
  try {
    raw = localStorage.getItem(KEY);
  } catch (_) {
    return true;  // storage disabled — assume available
  }
  if (!raw) return true;
  const until = Number(raw);
  if (!Number.isFinite(until)) return true;
  if (Date.now() >= until) {
    try { localStorage.removeItem(KEY); } catch (_) { /* ignore */ }
    return true;
  }
  return false;
}

export function markJinaUnavailable(): void {
  try {
    localStorage.setItem(KEY, String(Date.now() + TTL_MS));
  } catch (_) { /* ignore */ }
}

export function clearJinaBreaker(): void {
  try { localStorage.removeItem(KEY); } catch (_) { /* ignore */ }
}
