// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import {
  TOOL_GROUPS,
  countEnabled,
  getDisabledTools,
  getGroupState,
  isGroupExpanded,
  isToolEnabled,
  setGroupEnabled,
  setGroupExpanded,
  setToolEnabled,
} from '../tool_catalog.js';

beforeEach(() => {
  localStorage.clear();
});

afterEach(() => {
  localStorage.clear();
});

describe('tool_catalog: enable/disable semantics', () => {
  it('treats unknown tools as enabled (no migration needed for new tools)', () => {
    expect(isToolEnabled('a_tool_we_will_add_in_2027')).toBe(true);
  });

  it('all built-in tools are enabled by default', () => {
    for (const group of TOOL_GROUPS) {
      for (const name of group.toolNames) {
        expect(isToolEnabled(name)).toBe(true);
      }
    }
    expect(getDisabledTools().size).toBe(0);
  });

  it('disabling a single tool persists to localStorage', () => {
    setToolEnabled('web_search', false);
    expect(isToolEnabled('web_search')).toBe(false);

    const raw = localStorage.getItem('dao_disabled_tools');
    expect(raw).not.toBeNull();
    expect(JSON.parse(raw!)).toContain('web_search');
  });

  it('re-enabling a tool removes it from the disabled set', () => {
    setToolEnabled('web_search', false);
    setToolEnabled('web_search', true);
    expect(isToolEnabled('web_search')).toBe(true);
    expect(JSON.parse(localStorage.getItem('dao_disabled_tools')!))
      .not.toContain('web_search');
  });
});

describe('tool_catalog: group state', () => {
  it('reports "all" when nothing is disabled', () => {
    expect(getGroupState('web')).toBe('all');
  });

  it('reports "none" when every tool in a group is disabled', () => {
    setGroupEnabled('web', false);
    expect(getGroupState('web')).toBe('none');
  });

  it('reports "some" when only part of a group is disabled', () => {
    setToolEnabled('web_search', false);
    expect(getGroupState('web')).toBe('some');
  });

  it('returns "none" for an unknown group id', () => {
    expect(getGroupState('not_a_real_group')).toBe('none');
  });

  it('countEnabled returns enabled/total fractions', () => {
    const before = countEnabled('web');
    expect(before.enabled).toBe(before.total);

    setToolEnabled('web_search', false);
    const after = countEnabled('web');
    expect(after.total).toBe(before.total);
    expect(after.enabled).toBe(before.total - 1);
  });

  it('setGroupEnabled toggles every tool in the group', () => {
    setGroupEnabled('tabs', false);
    for (const name of TOOL_GROUPS.find(g => g.id === 'tabs')!.toolNames) {
      expect(isToolEnabled(name)).toBe(false);
    }
    setGroupEnabled('tabs', true);
    for (const name of TOOL_GROUPS.find(g => g.id === 'tabs')!.toolNames) {
      expect(isToolEnabled(name)).toBe(true);
    }
  });
});

describe('tool_catalog: corrupted localStorage', () => {
  it('treats non-array JSON as no-disabled', () => {
    localStorage.setItem('dao_disabled_tools', '{"foo":1}');
    expect(isToolEnabled('web_search')).toBe(true);
    expect(getDisabledTools().size).toBe(0);
  });

  it('treats non-JSON as no-disabled', () => {
    localStorage.setItem('dao_disabled_tools', 'not json');
    expect(isToolEnabled('web_search')).toBe(true);
  });

  it('strips non-string entries', () => {
    localStorage.setItem('dao_disabled_tools', '["web_search", 42, null]');
    expect(getDisabledTools()).toEqual(new Set(['web_search']));
  });
});

describe('tool_catalog: group expanded persistence', () => {
  it('groups default to collapsed on first open', () => {
    for (const group of TOOL_GROUPS) {
      expect(isGroupExpanded(group.id)).toBe(false);
    }
  });

  it('expanding a group persists', () => {
    setGroupExpanded('web', true);
    expect(isGroupExpanded('web')).toBe(true);
    setGroupExpanded('web', false);
    expect(isGroupExpanded('web')).toBe(false);
  });
});
