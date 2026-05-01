// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Jina availability flag, shared across agent tabs via localStorage.
// Tripped on 429; auto-clears after 10 min so we resume probing well
// before Jina's per-IP quota fully resets.

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
