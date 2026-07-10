// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, css, html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {initI18n, t} from './i18n/i18n.js';
import {
  applyMemoryTableView,
  displaySchemaType,
  memoryFilterCount,
  nextSortState,
  resizedColumnWidth,
  type MemoryTableSort,
} from './dao_memory_table.js';

interface PendingCallback {
  resolve: (value: unknown) => void;
  reject: (reason: unknown) => void;
}

interface CrNamespace {
  webUIResponse?: (id: string, isSuccess: boolean, response: unknown) => void;
}

interface SqlCell {
  type: 'integer'|'real'|'text'|'blob'|'null'|string;
  value: string;
}

interface SqlResult {
  ok: boolean;
  error: string;
  columns: string[];
  rows: SqlCell[][];
  truncated: boolean;
}

interface TableEntry {
  name: string;
  type: string;
}

interface ColumnResizeState {
  columnIndex: number;
  startX: number;
  startWidth: number;
}

const DEFAULT_SQL =
    "SELECT name, type FROM sqlite_schema WHERE type IN ('table', 'view') ORDER BY type, name";
const DEFAULT_MAX_ROWS = 100;
const DEFAULT_COLUMN_WIDTH = 220;
const pendingCallbacks: Record<string, PendingCallback> = {};
let callbackCounter = 0;

const cr = ((window as unknown as {cr?: CrNamespace}).cr) || {};
(window as unknown as {cr: CrNamespace}).cr = cr;

cr.webUIResponse =
    function(id: string, isSuccess: boolean, response: unknown): void {
  const entry = pendingCallbacks[id];
  if (!entry) return;
  delete pendingCallbacks[id];
  if (isSuccess) {
    entry.resolve(response);
  } else {
    entry.reject(response);
  }
};

function callNative<T>(
    method: string, params?: Record<string, unknown>): Promise<T> {
  return new Promise((resolve, reject) => {
    const id = method + '_' + (++callbackCounter);
    pendingCallbacks[id] = {
      resolve: value => resolve(value as T),
      reject,
    };
    chrome.send(method, [id, params || {}]);
    setTimeout(() => {
      if (!pendingCallbacks[id]) return;
      delete pendingCallbacks[id];
      reject(new Error('Timeout calling ' + method));
    }, 15000);
  });
}

function quoteIdentifier(name: string): string {
  return '"' + name.replaceAll('"', '""') + '"';
}

function resultTables(result: SqlResult|null): TableEntry[] {
  if (!result?.ok) return [];
  return result.rows
      .map(row => ({
             name: row[0]?.value ?? '',
             type: row[1]?.value ?? '',
           }))
      .filter(entry => entry.name.length > 0);
}

function countFromResult(result: SqlResult): string {
  if (!result.ok) return '';
  const value = result.rows[0]?.[0]?.value;
  if (value === undefined) return '';
  const count = Number(value);
  return Number.isFinite(count) ? new Intl.NumberFormat().format(count) : value;
}

export class DaoMemoryApp extends CrLitElement {
  static get is() {
    return 'dao-memory-app';
  }

  static override get properties() {
    return {
      ready_: {type: Boolean, state: true},
      sql_: {type: String, state: true},
      maxRows_: {type: Number, state: true},
      running_: {type: Boolean, state: true},
      copied_: {type: Boolean, state: true},
      tablesResult_: {type: Object, state: true},
      tableCounts_: {type: Object, state: true},
      result_: {type: Object, state: true},
      selectedTable_: {type: String, state: true},
      columnFilters_: {type: Object, state: true},
      columnWidths_: {type: Object, state: true},
      filtersOpen_: {type: Boolean, state: true},
      sort_: {type: Object, state: true},
    };
  }

  declare private ready_: boolean;
  declare private sql_: string;
  declare private maxRows_: number;
  declare private running_: boolean;
  declare private copied_: boolean;
  declare private tablesResult_: SqlResult|null;
  declare private tableCounts_: Record<string, string>;
  declare private result_: SqlResult|null;
  declare private selectedTable_: string;
  declare private columnFilters_: Record<number, string>;
  declare private columnWidths_: Record<number, number>;
  declare private filtersOpen_: boolean;
  declare private sort_: MemoryTableSort|null;
  private tableCountLoadId_: number;
  private columnResizeState_: ColumnResizeState|null;

  constructor() {
    super();
    this.ready_ = false;
    this.sql_ = DEFAULT_SQL;
    this.maxRows_ = DEFAULT_MAX_ROWS;
    this.running_ = false;
    this.copied_ = false;
    this.tablesResult_ = null;
    this.tableCounts_ = {};
    this.result_ = null;
    this.selectedTable_ = '';
    this.columnFilters_ = {};
    this.columnWidths_ = {};
    this.filtersOpen_ = false;
    this.sort_ = null;
    this.tableCountLoadId_ = 0;
    this.columnResizeState_ = null;
  }

  override connectedCallback() {
    super.connectedCallback();
    void this.init_();
  }

  override disconnectedCallback() {
    this.setFiltersOpen_(false);
    this.stopColumnResize_();
    super.disconnectedCallback();
  }

  static override get styles() {
    return css`
      :host {
        display: block;
        min-height: 100vh;
        color: rgb(29, 35, 42);
        background:
          linear-gradient(180deg, rgb(246, 248, 251) 0%, rgb(239, 244, 249) 100%);
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      }

      .shell {
        display: grid;
        grid-template-columns: 248px minmax(0, 1fr);
        height: 100vh;
      }

      aside {
        min-width: 0;
        border-right: 1px solid rgba(70, 120, 190, 0.18);
        background: rgba(231, 238, 245, 0.86);
        overflow: hidden;
      }

      .brand {
        padding: 22px 18px 16px;
        border-bottom: 1px solid rgba(70, 120, 190, 0.14);
      }

      h1 {
        margin: 0;
        font-size: 22px;
        line-height: 1.1;
        font-weight: 720;
        letter-spacing: 0;
      }

      .subtitle {
        margin-top: 6px;
        color: rgba(29, 35, 42, 0.58);
        font-size: 12px;
        font-weight: 500;
      }

      .tables-title {
        margin: 16px 18px 8px;
        color: rgba(29, 35, 42, 0.56);
        font-size: 11px;
        font-weight: 700;
        letter-spacing: 0.04em;
        text-transform: uppercase;
      }

      .table-list {
        height: calc(100vh - 110px);
        overflow: auto;
        padding: 0 10px 18px;
      }

      .table-button {
        display: grid;
        grid-template-columns: minmax(0, 1fr) auto auto;
        align-items: center;
        gap: 8px;
        width: 100%;
        min-height: 34px;
        margin: 2px 0;
        padding: 7px 8px;
        border: 0;
        border-radius: 8px;
        background: transparent;
        color: rgb(29, 35, 42);
        font: inherit;
        text-align: left;
        cursor: pointer;
      }

      .table-button:hover,
      .table-button.active {
        background: rgba(70, 120, 190, 0.12);
      }

      .table-name {
        overflow: hidden;
        font-size: 12px;
        font-weight: 620;
        text-overflow: ellipsis;
        white-space: nowrap;
      }

      .table-type {
        padding: 2px 5px;
        border-radius: 999px;
        background: rgba(70, 120, 190, 0.12);
        color: rgba(29, 35, 42, 0.56);
        font-size: 10px;
        font-weight: 760;
        text-transform: uppercase;
      }

      .table-count {
        min-width: 22px;
        padding: 2px 6px;
        border-radius: 999px;
        background: rgba(255, 255, 255, 0.54);
        color: rgba(29, 35, 42, 0.58);
        font: 10px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
        font-weight: 700;
        text-align: center;
      }

      .table-button:hover .table-count,
      .table-button.active .table-count {
        background: rgba(255, 255, 255, 0.78);
        color: rgba(29, 35, 42, 0.72);
      }

      main {
        display: grid;
        grid-template-rows: minmax(0, 1fr) auto;
        min-width: 0;
        height: 100vh;
        overflow: hidden;
      }

      .query {
        border-top: 1px solid rgba(70, 120, 190, 0.14);
        background: rgba(250, 251, 253, 0.92);
      }

      .query summary {
        display: grid;
        grid-template-columns: auto minmax(0, 1fr) auto auto;
        align-items: center;
        gap: 10px;
        min-height: 34px;
        padding: 0 20px;
        cursor: pointer;
        list-style: none;
      }

      .query summary::-webkit-details-marker {
        display: none;
      }

      .query-title {
        color: rgba(29, 35, 42, 0.7);
        font-size: 12px;
        font-weight: 740;
      }

      .query-preview {
        overflow: hidden;
        color: rgba(29, 35, 42, 0.48);
        font: 12px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
        text-overflow: ellipsis;
        white-space: nowrap;
      }

      .query-status {
        color: rgba(29, 35, 42, 0.58);
        font-size: 12px;
        font-weight: 620;
        white-space: nowrap;
      }

      .query-status.error {
        color: rgb(154, 60, 46);
      }

      .query-caret {
        width: 7px;
        height: 7px;
        border-right: 2px solid rgba(29, 35, 42, 0.42);
        border-bottom: 2px solid rgba(29, 35, 42, 0.42);
        transform: rotate(45deg);
        transition: transform 140ms ease;
      }

      .query[open] .query-caret {
        transform: rotate(225deg);
      }

      .query-body {
        display: grid;
        gap: 10px;
        padding: 0 20px 12px;
      }

      .query-top {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 12px;
      }

      label {
        display: grid;
        gap: 6px;
        color: rgba(29, 35, 42, 0.62);
        font-size: 12px;
        font-weight: 650;
      }

      .row-limit {
        width: 92px;
      }

      textarea,
      input {
        box-sizing: border-box;
        border: 1px solid rgba(70, 120, 190, 0.22);
        border-radius: 8px;
        background: rgb(255, 255, 255);
        color: rgb(24, 29, 35);
        font: 12px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
        outline: none;
      }

      textarea:focus,
      input:focus {
        border-color: rgba(70, 120, 190, 0.72);
        box-shadow: 0 0 0 3px rgba(70, 120, 190, 0.12);
      }

      textarea {
        width: 100%;
        min-height: 82px;
        max-height: 140px;
        padding: 9px 10px;
        line-height: 1.48;
        resize: vertical;
      }

      input {
        width: 100%;
        height: 30px;
        padding: 0 9px;
      }

      .actions {
        display: flex;
        align-items: end;
        gap: 8px;
      }

      button.command {
        height: 30px;
        padding: 0 11px;
        border: 1px solid rgba(70, 120, 190, 0.26);
        border-radius: 8px;
        background: rgb(70, 120, 190);
        color: white;
        font: inherit;
        font-size: 12px;
        font-weight: 700;
        cursor: pointer;
      }

      button.command.secondary {
        background: rgb(255, 255, 255);
        color: rgb(42, 53, 66);
      }

      button.command:disabled {
        cursor: default;
        opacity: 0.56;
      }

      .results {
        display: grid;
        grid-template-rows: auto minmax(0, 1fr);
        min-width: 0;
        min-height: 0;
        overflow: hidden;
        background: rgb(250, 251, 253);
      }

      .results-toolbar {
        display: flex;
        align-items: center;
        justify-content: flex-end;
        min-width: 0;
        min-height: 34px;
        padding: 0 12px;
        border-bottom: 1px solid rgba(30, 20, 40, 0.07);
        background: rgb(250, 251, 253);
      }

      .results-toolbar-actions {
        position: relative;
        display: flex;
        align-items: center;
      }

      .filter-button {
        display: grid;
        place-items: center;
        position: relative;
        width: 24px;
        height: 24px;
        padding: 0;
        border: 1px solid rgba(70, 120, 190, 0.24);
        border-radius: 7px;
        background: rgb(255, 255, 255);
        color: rgba(29, 35, 42, 0.68);
        font: inherit;
        cursor: pointer;
      }

      .filter-button svg {
        width: 13px;
        height: 13px;
        stroke-width: 2;
      }

      .filter-button:hover,
      .filter-button.active {
        border-color: rgba(70, 120, 190, 0.48);
        color: rgb(45, 74, 116);
      }

      .filter-button.active {
        background: rgb(235, 242, 250);
      }

      .filter-count-badge {
        position: absolute;
        top: -5px;
        right: -6px;
        min-width: 13px;
        height: 13px;
        padding: 0 4px;
        border: 1px solid rgb(250, 251, 253);
        border-radius: 999px;
        background: rgb(70, 120, 190);
        color: rgb(255, 255, 255);
        font: 8px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
        font-weight: 760;
        line-height: 11px;
        text-align: center;
      }

      .filter-panel {
        position: absolute;
        top: 31px;
        right: 0;
        z-index: 5;
        box-sizing: border-box;
        width: 300px;
        max-height: min(420px, calc(100vh - 136px));
        padding: 10px;
        border: 1px solid rgba(70, 120, 190, 0.22);
        border-radius: 8px;
        background: rgba(255, 255, 255, 0.98);
        box-shadow: 0 18px 36px rgba(26, 36, 48, 0.16);
        overflow: auto;
      }

      .filter-panel-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 10px;
        margin-bottom: 8px;
      }

      .filter-panel-title {
        color: rgba(29, 35, 42, 0.72);
        font-size: 12px;
        font-weight: 760;
      }

      .clear-filters {
        height: 24px;
        padding: 0 8px;
        border: 1px solid rgba(70, 120, 190, 0.2);
        border-radius: 7px;
        background: rgb(255, 255, 255);
        color: rgba(29, 35, 42, 0.62);
        font: inherit;
        font-size: 11px;
        font-weight: 700;
        cursor: pointer;
      }

      .clear-filters:disabled {
        cursor: default;
        opacity: 0.48;
      }

      .filter-fields {
        display: grid;
        gap: 8px;
      }

      .filter-field {
        gap: 4px;
      }

      .filter-field-name {
        overflow: hidden;
        color: rgba(29, 35, 42, 0.58);
        text-overflow: ellipsis;
        white-space: nowrap;
      }

      .filter-field input {
        height: 28px;
        font-size: 11px;
      }

      .table-scroll {
        min-width: 0;
        min-height: 0;
        overflow: auto;
      }

      .empty {
        display: grid;
        place-items: center;
        min-height: 260px;
        color: rgba(29, 35, 42, 0.48);
        font-size: 13px;
      }

      table {
        width: max-content;
        min-width: 100%;
        border-collapse: collapse;
        font-size: 12px;
        table-layout: fixed;
      }

      th,
      td {
        border-right: 1px solid rgba(30, 20, 40, 0.07);
        border-bottom: 1px solid rgba(30, 20, 40, 0.07);
        text-align: left;
        vertical-align: top;
      }

      th {
        position: sticky;
        top: 0;
        z-index: 1;
        padding: 0;
        background: rgb(239, 244, 249);
        color: rgba(29, 35, 42, 0.7);
        font-weight: 720;
        white-space: nowrap;
      }

      button.header-button {
        display: grid;
        grid-template-columns: minmax(0, 1fr) auto;
        align-items: center;
        gap: 8px;
        width: 100%;
        min-height: 30px;
        padding: 7px 24px 7px 10px;
        border: 0;
        background: transparent;
        color: inherit;
        font: inherit;
        text-align: left;
        cursor: pointer;
      }

      button.header-button span:first-child {
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
      }

      .sort-mark {
        width: 0;
        height: 0;
        opacity: 0;
      }

      .sort-mark.asc,
      .sort-mark.desc {
        opacity: 0.72;
      }

      .sort-mark.asc {
        border-right: 4px solid transparent;
        border-bottom: 6px solid rgba(29, 35, 42, 0.54);
        border-left: 4px solid transparent;
      }

      .sort-mark.desc {
        border-top: 6px solid rgba(29, 35, 42, 0.54);
        border-right: 4px solid transparent;
        border-left: 4px solid transparent;
      }

      .resize-handle {
        position: absolute;
        top: 0;
        right: -3px;
        z-index: 2;
        width: 7px;
        height: 100%;
        cursor: col-resize;
      }

      .resize-handle:hover {
        background: rgba(70, 120, 190, 0.22);
      }

      td {
        padding: 0;
      }

      .cell-content {
        box-sizing: border-box;
        width: 100%;
        max-height: 112px;
        overflow: auto;
        padding: 6px 10px;
        color: rgb(31, 36, 43);
        font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
        line-height: 1.4;
        overflow-wrap: anywhere;
        scrollbar-gutter: stable;
        white-space: pre-wrap;
      }

      .cell-content.null {
        color: rgba(29, 35, 42, 0.38);
        font-style: italic;
      }

      @media (max-width: 760px) {
        .shell {
          grid-template-columns: 1fr;
          grid-template-rows: auto minmax(0, 1fr);
        }

        aside {
          border-right: 0;
          border-bottom: 1px solid rgba(70, 120, 190, 0.18);
        }

        .brand {
          padding: 16px 16px 10px;
        }

        .tables-title {
          display: none;
        }

        .table-list {
          display: flex;
          gap: 6px;
          height: auto;
          max-width: 100vw;
          overflow-x: auto;
          padding: 8px 10px 12px;
        }

        .table-button {
          width: max-content;
          min-width: 128px;
        }

        .results-toolbar {
          min-height: 32px;
          padding: 0 8px;
        }

        .filter-panel {
          width: min(300px, calc(100vw - 20px));
          max-height: min(360px, calc(100vh - 166px));
        }

        main {
          height: auto;
          min-height: 0;
        }

        .query summary {
          padding: 0 16px;
        }

        .query-body {
          padding: 0 16px 12px;
        }

        .query-top {
          align-items: stretch;
          flex-direction: column;
        }

        .actions {
          align-items: center;
          flex-wrap: wrap;
        }
      }
    `;
  }

  override render() {
    if (!this.ready_) {
      return html`<div class="empty">${t('memory.tables_loading')}</div>`;
    }

    const tables = resultTables(this.tablesResult_);
    return html`
      <div class="shell">
        <aside>
          <div class="brand">
            <h1>${t('memory.title')}</h1>
            <div class="subtitle">${t('memory.subtitle')}</div>
          </div>
          <div class="tables-title">${t('memory.tables')}</div>
          <div class="table-list">
            ${tables.length === 0 ? html`
              <div class="empty">${this.tablesResult_ ? t('memory.tables_empty') : t('memory.tables_loading')}</div>
            ` : tables.map(table => this.renderTableButton_(table))}
          </div>
        </aside>
        <main>
          ${this.renderResults_()}
          <details class="query">
            <summary>
              <span class="query-title">${t('memory.query_label')}</span>
              <span class="query-preview">${this.sql_}</span>
              <span class=${this.result_ && !this.result_.ok ? 'query-status error' : 'query-status'}>
                ${this.resultSummary_()}
              </span>
              <span class="query-caret"></span>
            </summary>
            <div class="query-body">
              <div class="query-top">
                <label>
                  ${t('memory.query_label')}
                  <textarea .value=${this.sql_} @input=${this.onSqlInput_}></textarea>
                </label>
                <div class="actions">
                  <label class="row-limit">
                    ${t('memory.max_rows')}
                    <input
                        type="number"
                        min="1"
                        max="500"
                        .value=${String(this.maxRows_)}
                        @input=${this.onMaxRowsInput_}>
                  </label>
                  <button
                      class="command"
                      ?disabled=${this.running_}
                      @click=${this.runQuery_}>
                    ${this.running_ ? t('memory.running') : t('memory.run')}
                  </button>
                  <button class="command secondary" @click=${this.resetQuery_}>
                    ${t('memory.reset')}
                  </button>
                  <button class="command secondary" @click=${this.copySql_}>
                    ${this.copied_ ? t('memory.copied') : t('memory.copy_sql')}
                  </button>
                </div>
              </div>
            </div>
          </details>
        </main>
      </div>
    `;
  }

  private async init_(): Promise<void> {
    await initI18n();
    this.ready_ = true;
    await this.loadTables_();
    await this.runQuery_();
  }

  private renderTableButton_(table: TableEntry) {
    const active = this.selectedTable_ === table.name ? ' active' : '';
    const schemaType = displaySchemaType(table.type);
    return html`
      <button
          class=${'table-button' + active}
          title=${table.name}
          @click=${() => this.selectTable_(table.name)}>
        <span class="table-name">${table.name}</span>
        ${schemaType ? html`<span class="table-type">${schemaType}</span>` : nothing}
        <span class="table-count">${this.tableCounts_[table.name] || nothing}</span>
      </button>
    `;
  }

  private renderResults_() {
    if (!this.result_) {
      return html`
        <section class="results">
          <div class="table-scroll">
            <div class="empty">${t('memory.result_empty')}</div>
          </div>
        </section>
      `;
    }

    if (!this.result_.ok) {
      return html`
        <section class="results">
          <div class="table-scroll">
            <div class="empty">${t('memory.result_empty')}</div>
          </div>
        </section>
      `;
    }

    const rows = this.visibleRows_();
    return html`
      <section class="results">
        ${this.result_.columns.length > 0 ?
            this.renderResultsToolbar_() :
            nothing}
        <div class="table-scroll">
          ${this.result_.columns.length === 0 ? html`
            <div class="empty">${t('memory.result_empty')}</div>
          ` : html`
            <table>
              <colgroup>
                ${this.result_.columns.map((_, index) => html`
                  <col style=${this.columnWidthStyle_(index)}>
                `)}
              </colgroup>
              <thead>
                <tr>
                  ${this.result_.columns.map((column, index) =>
                      this.renderHeaderCell_(column, index))}
                </tr>
              </thead>
              <tbody>
                ${rows.map(row => html`
                  <tr>
                    ${row.map(cell => this.renderCell_(cell))}
                  </tr>
                `)}
              </tbody>
            </table>
          `}
        </div>
      </section>
    `;
  }

  private visibleRows_(): SqlCell[][] {
    if (!this.result_?.ok) return [];
    return applyMemoryTableView(this.result_.rows, {
      filters: this.columnFilters_,
      sort: this.sort_,
    });
  }

  private renderResultsToolbar_() {
    const count = this.activeFilterCount_();
    const filterLabel = count > 0 ?
        t('memory.filter_button_count', {count}) :
        t('memory.filter_button');
    return html`
      <div class="results-toolbar">
        <div class="results-toolbar-actions">
          <button
              class=${'filter-button' +
                  (this.filtersOpen_ || count > 0 ? ' active' : '')}
              aria-label=${filterLabel}
              aria-expanded=${String(this.filtersOpen_)}
              title=${filterLabel}
              @click=${this.toggleFilters_}>
            ${this.renderFilterIcon_()}
            ${count > 0 ? html`
              <span class="filter-count-badge">${count}</span>
            ` : nothing}
          </button>
          ${this.filtersOpen_ ? html`
            <div class="filter-panel">
              <div class="filter-panel-header">
                <div class="filter-panel-title">
                  ${t('memory.filter_panel_title')}
                </div>
                <button
                    class="clear-filters"
                    ?disabled=${count === 0}
                    @click=${this.clearFilters_}>
                  ${t('memory.clear_filters')}
                </button>
              </div>
              <div class="filter-fields">
                ${this.result_!.columns.map((column, index) =>
                    this.renderFilterField_(column, index))}
              </div>
            </div>
          ` : nothing}
        </div>
      </div>
    `;
  }

  private renderFilterIcon_() {
    // Lucide funnel, copied from vendor/node_modules/lucide v0.544.0.
    return html`
      <svg
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          stroke-linecap="round"
          stroke-linejoin="round"
          aria-hidden="true">
        <path d="M10 20a1 1 0 0 0 .553.895l2 1A1 1 0 0 0 14 21v-7a2 2 0 0 1 .517-1.341L21.74 4.67A1 1 0 0 0 21 3H3a1 1 0 0 0-.742 1.67l7.225 7.989A2 2 0 0 1 10 14z"></path>
      </svg>
    `;
  }

  private renderHeaderCell_(column: string, index: number) {
    const sortDirection = this.sort_?.columnIndex === index ?
        this.sort_.direction :
        '';
    return html`
      <th>
        <button
            class="header-button"
            title=${column}
            @click=${() => this.toggleSort_(index)}>
          <span>${column}</span>
          <span class=${'sort-mark ' + sortDirection}></span>
        </button>
        <span
            class="resize-handle"
            @pointerdown=${(event: PointerEvent) =>
                this.startColumnResize_(event, index)}>
        </span>
      </th>
    `;
  }

  private renderFilterField_(column: string, index: number) {
    return html`
      <label class="filter-field">
        <span class="filter-field-name">${column}</span>
        <input
            placeholder=${t('memory.filter_placeholder')}
            title=${column}
            .value=${this.columnFilters_[index] || ''}
            @input=${(event: Event) => this.onColumnFilterInput_(event, index)}>
      </label>
    `;
  }

  private resultSummary_(): string {
    if (this.running_) {
      return t('memory.running');
    }
    if (!this.result_) {
      return '';
    }
    if (!this.result_.ok) {
      return `${t('memory.error_prefix')}: ${this.result_.error}`;
    }
    if (this.hasActiveFilters_()) {
      const visible = this.visibleRows_().length;
      const count = this.result_.rows.length;
      return this.result_.truncated ?
          t('memory.result_count_filtered_truncated', {visible, count}) :
          t('memory.result_count_filtered', {visible, count});
    }
    return this.result_.truncated ?
        t('memory.result_count_truncated', {count: this.result_.rows.length}) :
        t('memory.result_count', {count: this.result_.rows.length});
  }

  private renderCell_(cell: SqlCell) {
    if (cell.type === 'null') {
      return html`
        <td>
          <div class="cell-content null">${t('memory.null')}</div>
        </td>
      `;
    }
    return html`
      <td>
        <div class="cell-content">
          ${cell.value || nothing}
        </div>
      </td>
    `;
  }

  private async loadTables_(): Promise<void> {
    try {
      this.tablesResult_ = await callNative<SqlResult>('memoryGetTables');
      void this.loadTableCounts_(resultTables(this.tablesResult_));
    } catch (error) {
      this.tablesResult_ = {
        ok: false,
        error: error instanceof Error ? error.message : String(error),
        columns: [],
        rows: [],
        truncated: false,
      };
    }
  }

  private async loadTableCounts_(tables: TableEntry[]): Promise<void> {
    const loadId = ++this.tableCountLoadId_;
    this.tableCounts_ = {};
    await Promise.all(tables.map(async table => {
      let count = '';
      try {
        const result = await callNative<SqlResult>('memoryExecuteSql', {
          sql: `SELECT COUNT(*) FROM ${quoteIdentifier(table.name)}`,
          maxRows: 1,
        });
        count = countFromResult(result);
      } catch {
        count = '';
      }
      if (loadId !== this.tableCountLoadId_) return;
      this.tableCounts_ = {
        ...this.tableCounts_,
        [table.name]: count,
      };
    }));
  }

  private columnWidthStyle_(columnIndex: number): string {
    return `width: ${this.columnWidths_[columnIndex] || DEFAULT_COLUMN_WIDTH}px`;
  }

  private hasActiveFilters_(): boolean {
    return this.activeFilterCount_() > 0;
  }

  private activeFilterCount_(): number {
    return memoryFilterCount(this.columnFilters_);
  }

  private readonly toggleFilters_ = (): void => {
    this.setFiltersOpen_(!this.filtersOpen_);
  };

  private setFiltersOpen_(open: boolean): void {
    if (this.filtersOpen_ === open) return;
    this.filtersOpen_ = open;
    if (open) {
      document.addEventListener('pointerdown', this.onDocumentPointerDown_,
                                true);
    } else {
      document.removeEventListener('pointerdown', this.onDocumentPointerDown_,
                                   true);
    }
  }

  private readonly onDocumentPointerDown_ = (event: Event): void => {
    const root = this.shadowRoot;
    const filterButton = root?.querySelector('.filter-button');
    const filterPanel = root?.querySelector('.filter-panel');
    const path = event.composedPath();
    if ((filterButton && path.includes(filterButton)) ||
        (filterPanel && path.includes(filterPanel))) {
      return;
    }
    this.setFiltersOpen_(false);
  };

  private clearFilters_(): void {
    this.columnFilters_ = {};
  }

  private onColumnFilterInput_(event: Event, columnIndex: number): void {
    const value = (event.target as HTMLInputElement).value;
    const nextFilters = {...this.columnFilters_};
    if (value.trim()) {
      nextFilters[columnIndex] = value;
    } else {
      delete nextFilters[columnIndex];
    }
    this.columnFilters_ = nextFilters;
  }

  private toggleSort_(columnIndex: number): void {
    this.sort_ = nextSortState(this.sort_, columnIndex);
  }

  private startColumnResize_(event: PointerEvent, columnIndex: number): void {
    event.preventDefault();
    event.stopPropagation();
    this.columnResizeState_ = {
      columnIndex,
      startX: event.clientX,
      startWidth: this.columnWidths_[columnIndex] || DEFAULT_COLUMN_WIDTH,
    };
    window.addEventListener('pointermove', this.onColumnResizePointerMove_);
    window.addEventListener('pointerup', this.onColumnResizePointerUp_, {
      once: true,
    });
  }

  private readonly onColumnResizePointerMove_ = (event: PointerEvent): void => {
    if (!this.columnResizeState_) return;
    const {columnIndex, startX, startWidth} = this.columnResizeState_;
    this.columnWidths_ = {
      ...this.columnWidths_,
      [columnIndex]: resizedColumnWidth(startWidth, event.clientX - startX),
    };
  };

  private readonly onColumnResizePointerUp_ = (): void => {
    this.stopColumnResize_();
  };

  private stopColumnResize_(): void {
    this.columnResizeState_ = null;
    window.removeEventListener('pointermove', this.onColumnResizePointerMove_);
    window.removeEventListener('pointerup', this.onColumnResizePointerUp_);
  }

  private async runQuery_(): Promise<void> {
    if (this.running_) return;
    this.running_ = true;
    try {
      this.result_ = await callNative<SqlResult>('memoryExecuteSql', {
        sql: this.sql_,
        maxRows: this.maxRows_,
      });
      this.columnFilters_ = {};
      this.columnWidths_ = {};
      this.setFiltersOpen_(false);
      this.sort_ = null;
    } catch (error) {
      this.result_ = {
        ok: false,
        error: error instanceof Error ? error.message : String(error),
        columns: [],
        rows: [],
        truncated: false,
      };
    } finally {
      this.running_ = false;
    }
  }

  private selectTable_(name: string): void {
    this.selectedTable_ = name;
    this.sql_ = `SELECT * FROM ${quoteIdentifier(name)} LIMIT 100`;
    this.maxRows_ = 100;
    void this.runQuery_();
  }

  private resetQuery_(): void {
    this.selectedTable_ = '';
    this.sql_ = DEFAULT_SQL;
    this.maxRows_ = DEFAULT_MAX_ROWS;
    void this.runQuery_();
  }

  private onSqlInput_(event: Event): void {
    this.sql_ = (event.target as HTMLTextAreaElement).value;
  }

  private onMaxRowsInput_(event: Event): void {
    const value = Number((event.target as HTMLInputElement).value);
    this.maxRows_ = Number.isFinite(value) ?
        Math.min(500, Math.max(1, Math.floor(value))) :
        DEFAULT_MAX_ROWS;
  }

  private async copySql_(): Promise<void> {
    try {
      await navigator.clipboard.writeText(this.sql_);
      this.copied_ = true;
      setTimeout(() => {
        this.copied_ = false;
      }, 1200);
    } catch {
      this.copied_ = false;
    }
  }
}

customElements.define(DaoMemoryApp.is, DaoMemoryApp);
