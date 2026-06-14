// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export interface MemoryTableCell {
  type: string;
  value: string;
}

export interface MemoryTableSort {
  columnIndex: number;
  direction: 'asc'|'desc';
}

export interface MemoryTableViewOptions {
  filters: Record<number, string>;
  sort: MemoryTableSort|null;
}

export function applyMemoryTableView<T extends MemoryTableCell>(
    rows: T[][], options: MemoryTableViewOptions): T[][] {
  let view = rows.filter(row => rowMatchesFilters(row, options.filters));
  if (options.sort) {
    view = [...view].sort((left, right) =>
      compareCells(left[options.sort!.columnIndex],
                   right[options.sort!.columnIndex], options.sort!.direction));
  }
  return view;
}

export function nextSortState(
    current: MemoryTableSort|null, columnIndex: number): MemoryTableSort|null {
  if (!current || current.columnIndex !== columnIndex) {
    return {columnIndex, direction: 'asc'};
  }
  if (current.direction === 'asc') {
    return {columnIndex, direction: 'desc'};
  }
  return null;
}

export function resizedColumnWidth(
    startWidth: number, delta: number, minWidth = 72, maxWidth = 720): number {
  return Math.min(maxWidth, Math.max(minWidth, Math.round(startWidth + delta)));
}

export function displaySchemaType(type: string): string {
  return type === 'table' ? '' : type;
}

export function memoryFilterCount(filters: Record<number, string>): number {
  return Object.values(filters).filter(filter => filter.trim()).length;
}

function rowMatchesFilters<T extends MemoryTableCell>(
    row: T[], filters: Record<number, string>): boolean {
  return Object.entries(filters).every(([columnIndex, rawFilter]) => {
    const filter = rawFilter.trim().toLowerCase();
    if (!filter) return true;
    const value = row[Number(columnIndex)]?.value ?? '';
    return value.toLowerCase().includes(filter);
  });
}

function compareCells(
    left: MemoryTableCell|undefined, right: MemoryTableCell|undefined,
    direction: 'asc'|'desc'): number {
  const leftValue = left?.value ?? '';
  const rightValue = right?.value ?? '';
  const leftNumber = Number(leftValue);
  const rightNumber = Number(rightValue);
  const bothNumeric = leftValue.trim() !== '' && rightValue.trim() !== '' &&
      Number.isFinite(leftNumber) && Number.isFinite(rightNumber);
  const comparison = bothNumeric ?
      leftNumber - rightNumber :
      leftValue.localeCompare(rightValue, undefined, {
        numeric: true,
        sensitivity: 'base',
      });
  return direction === 'asc' ? comparison : -comparison;
}
