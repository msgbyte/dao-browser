// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {describe, expect, it} from 'vitest';

import {
  applyMemoryTableView,
  displaySchemaType,
  memoryFilterCount,
  nextSortState,
  resizedColumnWidth,
} from '../dao_memory_table.js';

function cell(value: string) {
  return {
    type: 'text',
    value,
  };
}

describe('dao memory table helpers', () => {
  const rows = [
    [cell('3'), cell('Beta'), cell('night')],
    [cell('12'), cell('Alpha'), cell('day')],
    [cell('2'), cell('alphabet'), cell('night')],
  ];

  it('filters rows by case-insensitive per-column text', () => {
    const view = applyMemoryTableView(rows, {
      filters: {1: 'ALPHA', 2: 'night'},
      sort: null,
    });

    expect(view.map(row => row[1]!.value)).toEqual(['alphabet']);
  });

  it('sorts numeric-looking values numerically', () => {
    const view = applyMemoryTableView(rows, {
      filters: {},
      sort: {columnIndex: 0, direction: 'asc'},
    });

    expect(view.map(row => row[0]!.value)).toEqual(['2', '3', '12']);
  });

  it('cycles sort state for header clicks', () => {
    expect(nextSortState(null, 1)).toEqual({
      columnIndex: 1,
      direction: 'asc',
    });
    expect(nextSortState({columnIndex: 1, direction: 'asc'}, 1)).toEqual({
      columnIndex: 1,
      direction: 'desc',
    });
    expect(nextSortState({columnIndex: 1, direction: 'desc'}, 1)).toBeNull();
    expect(nextSortState({columnIndex: 0, direction: 'desc'}, 1)).toEqual({
      columnIndex: 1,
      direction: 'asc',
    });
  });

  it('clamps resized column widths to a readable minimum', () => {
    expect(resizedColumnWidth(160, 42)).toBe(202);
    expect(resizedColumnWidth(160, -120)).toBe(72);
  });

  it('hides ordinary table schema type labels', () => {
    expect(displaySchemaType('table')).toBe('');
    expect(displaySchemaType('view')).toBe('view');
  });

  it('counts only non-empty column filters', () => {
    expect(memoryFilterCount({0: 'alpha', 1: ' ', 2: 'night'})).toBe(2);
  });
});
