// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, css, html, nothing} from '//resources/lit/v3_0/lit.rollup.js';
import {loadTimeData} from '//resources/js/load_time_data.js';

import {
  addListener,
  cancelBrowserMigration,
  detectImportSources,
  getBrowserMigrationState,
  getImportItemCount,
  removeListener,
  retryBrowserMigrationCategories,
  startBrowserMigration,
} from './import_bridge.js';
import type {
  CategoryState,
  MigrationState,
  SourceProfile,
  WebUiListener,
} from './import_bridge.js';

const CATEGORY_ORDER =
    ['bookmarks', 'history', 'passwords', 'tabs', 'extensions'];

const SOURCE_KINDS = [
  {kind: 'chrome', nameKey: 'daoImportBrowserChrome'},
  {kind: 'arc', nameKey: 'daoImportBrowserArc'},
  {kind: 'edge', nameKey: 'daoImportBrowserEdge'},
  {kind: 'safari', nameKey: 'daoImportBrowserSafari'},
  {kind: 'firefox', nameKey: 'daoImportBrowserFirefox'},
] as const;

const TERMINAL_PHASES = new Set(['succeeded', 'failed', 'cancelled']);
const DAO_LOGO_URL = 'assets/dao.png';

type SourceCard = {
  kind: string;
  browserName: string;
  source?: SourceProfile;
};

export class DaoImportApp extends CrLitElement {
  static get is() {
    return 'dao-import-app';
  }

  static override get properties() {
    return {
      step_: {type: Number},
      loading_: {type: Boolean},
      sources_: {type: Array},
      selectedSourceId_: {type: String},
      selectedCategories_: {type: Array},
      categoryCounts_: {type: Object},
      jobState_: {type: Object},
    };
  }

  static override get styles() {
    return css`
      :host {
        --bg: rgb(231, 238, 245);
        --panel: rgba(255, 255, 255, .88);
        --surface: rgba(0, 0, 0, .055);
        --surface-hover: rgba(0, 0, 0, .09);
        --border: rgba(0, 0, 0, .09);
        --text: rgba(30, 20, 40, .92);
        --secondary: rgba(30, 20, 40, .6);
        --tertiary: rgba(30, 20, 40, .42);
        --accent: rgb(70, 120, 190);
        --accent-strong: rgb(52, 96, 158);
        --accent-soft: rgba(70, 120, 190, .12);
        --success: rgb(46, 140, 92);
        --danger: rgb(196, 65, 65);
        --danger-soft: rgba(196, 65, 65, .1);
        align-items: center;
        background: var(--bg);
        color: var(--text);
        display: flex;
        justify-content: center;
        min-height: 100vh;
        padding: 24px;
      }

      @media (prefers-color-scheme: dark) {
        :host {
          --bg: rgb(54, 59, 64);
          --panel: rgba(70, 76, 82, .92);
          --surface: rgba(255, 255, 255, .065);
          --surface-hover: rgba(255, 255, 255, .11);
          --border: rgba(255, 255, 255, .12);
          --text: rgba(245, 245, 245, .92);
          --secondary: rgba(255, 255, 255, .62);
          --tertiary: rgba(255, 255, 255, .42);
          --accent-strong: rgb(112, 158, 216);
          --accent-soft: rgba(70, 120, 190, .22);
          --success: rgb(74, 176, 120);
          --danger: rgb(245, 118, 118);
          --danger-soft: rgba(245, 118, 118, .14);
        }
      }

      * { box-sizing: border-box; }
      button { font: inherit; }
      button:focus-visible {
        outline: 2px solid var(--accent);
        outline-offset: 2px;
      }

      .shell {
        background:
          radial-gradient(100% 70% at 50% 0%, var(--accent-soft), transparent 58%),
          var(--panel);
        border: 1px solid var(--border);
        border-radius: 12px;
        box-shadow: 0 20px 60px rgba(25, 35, 48, .16);
        display: grid;
        grid-template-rows: auto minmax(0, 1fr) auto;
        height: min(680px, calc(100vh - 48px));
        max-width: 820px;
        overflow: hidden;
        width: 100%;
      }

      header, footer {
        align-items: center;
        display: flex;
        justify-content: space-between;
        padding: 18px 24px;
      }
      header { border-bottom: 1px solid var(--border); }
      footer { border-top: 1px solid var(--border); gap: 16px; }
      .brand { align-items: center; display: flex; gap: 10px; }
      .brand-logo {
        display: block;
        height: 28px;
        object-fit: contain;
        width: 28px;
      }
      .brand-name { font-size: 13px; font-weight: 650; }
      .rail { align-items: center; display: flex; gap: 6px; }
      .rail span {
        background: var(--surface-hover);
        border-radius: 8px;
        height: 4px;
        transition: width 180ms ease, background 180ms ease;
        width: 24px;
      }
      .rail span.done { background: var(--accent); }
      .rail span.current { background: var(--accent); width: 36px; }

      main { min-height: 0; overflow: auto; padding: 28px 32px; }
      .eyebrow {
        color: var(--accent-strong);
        font-size: 12px;
        font-weight: 650;
        letter-spacing: .02em;
        margin-bottom: 8px;
      }
      h1 {
        font-size: 24px;
        letter-spacing: -.025em;
        line-height: 1.2;
        margin: 0;
      }
      .description {
        color: var(--secondary);
        line-height: 1.55;
        margin: 7px 0 22px;
        max-width: 62ch;
      }

      .source-grid { display: grid; gap: 10px; grid-template-columns: 1fr 1fr; }
      .source-card, .category-row {
        align-items: center;
        background: var(--surface);
        border: 1px solid transparent;
        border-radius: 12px;
        color: var(--text);
        cursor: pointer;
        display: flex;
        gap: 12px;
        min-width: 0;
        padding: 14px;
        text-align: start;
        transition: background 120ms ease, border-color 120ms ease,
                    transform 120ms ease;
        width: 100%;
      }
      .source-card:hover, .category-row:hover { background: var(--surface-hover); }
      .source-card.selected, .category-row.selected {
        background: var(--accent-soft);
        border-color: var(--accent);
      }
      .source-card:active, .category-row:active { transform: scale(.992); }
      .source-card:disabled {
        cursor: default;
        opacity: .48;
      }
      .source-card:disabled:hover { background: var(--surface); }
      .source-card:disabled:active { transform: none; }
      .source-mark {
        align-items: center;
        background: var(--mark-color, var(--accent));
        border-radius: 11px;
        color: white;
        display: flex;
        flex: 0 0 auto;
        font-size: 15px;
        font-weight: 750;
        height: 42px;
        justify-content: center;
        width: 42px;
      }
      .source-logo {
        height: 30px;
        object-fit: contain;
        width: 30px;
      }
      .source-mark.arc { --mark-color: rgb(255, 83, 106); }
      .source-mark.safari { --mark-color: rgb(55, 137, 205); }
      .source-mark.edge { --mark-color: rgb(24, 158, 180); }
      .source-mark.firefox { --mark-color: rgb(220, 67, 109); }
      .card-copy { min-width: 0; }
      .card-title { display: block; font-size: 14px; font-weight: 650; }
      .card-meta {
        color: var(--secondary);
        display: block;
        margin-top: 2px;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
      }
      .selection {
        border: 1px solid var(--border);
        border-radius: 50%;
        flex: 0 0 auto;
        height: 20px;
        margin-inline-start: auto;
        position: relative;
        width: 20px;
      }
      .selected .selection { background: var(--accent); border-color: var(--accent); }
      .selected .selection::after {
        border-bottom: 2px solid white;
        border-right: 2px solid white;
        content: '';
        height: 8px;
        left: 6px;
        position: absolute;
        top: 3px;
        transform: rotate(45deg);
        width: 4px;
      }

      .empty {
        align-items: center;
        border: 1px dashed var(--border);
        border-radius: 12px;
        color: var(--secondary);
        display: flex;
        flex-direction: column;
        gap: 12px;
        justify-content: center;
        min-height: 230px;
        padding: 32px;
        text-align: center;
      }
      .empty-dot {
        background: var(--accent-soft);
        border: 1px solid var(--accent);
        border-radius: 50%;
        height: 42px;
        width: 42px;
      }

      .category-list { display: flex; flex-direction: column; gap: 8px; }
      .category-row { padding: 12px 14px; }
      .category-row .card-title { flex: 1; }
      .category-count {
        color: var(--secondary);
        flex: 0 0 auto;
        font-size: 12px;
        font-variant-numeric: tabular-nums;
      }
      .category-glyph {
        background: var(--accent-soft);
        border-radius: 9px;
        height: 34px;
        position: relative;
        width: 34px;
      }
      .category-glyph::before, .category-glyph::after {
        background: var(--accent-strong);
        border-radius: 2px;
        content: '';
        left: 9px;
        position: absolute;
        top: 11px;
      }
      .category-glyph::before { height: 2px; width: 16px; }
      .category-glyph::after { height: 2px; top: 20px; width: 11px; }
      .switch {
        background: var(--surface-hover);
        border-radius: 16px;
        height: 24px;
        flex: 0 0 auto;
        position: relative;
        width: 40px;
      }
      .switch::after {
        background: white;
        border-radius: 50%;
        box-shadow: 0 1px 3px rgba(0, 0, 0, .25);
        content: '';
        height: 18px;
        left: 3px;
        position: absolute;
        top: 3px;
        transition: transform 150ms ease;
        width: 18px;
      }
      .selected .switch { background: var(--accent); }
      .selected .switch::after { transform: translateX(16px); }

      .tip {
        align-items: flex-start;
        background: var(--accent-soft);
        border: 1px solid rgba(70, 120, 190, .24);
        border-radius: 12px;
        display: flex;
        gap: 11px;
        line-height: 1.5;
        margin-top: 12px;
        padding: 13px 14px;
      }
      .tip-mark {
        align-items: center;
        border: 1px solid var(--accent);
        border-radius: 50%;
        color: var(--accent-strong);
        display: flex;
        flex: 0 0 auto;
        font-size: 11px;
        font-weight: 700;
        height: 20px;
        justify-content: center;
        margin-top: 1px;
        width: 20px;
      }
      .tip strong { display: block; font-weight: 650; margin-bottom: 2px; }
      .tip span { color: var(--secondary); }

      .migration-pipe {
        align-items: center;
        display: grid;
        grid-template-columns: auto 1fr auto;
        margin: 34px 18px 30px;
      }
      .pipe-node {
        align-items: center;
        display: flex;
        flex-direction: column;
        font-weight: 650;
        gap: 8px;
      }
      .pipe-disc {
        align-items: center;
        background: var(--surface-hover);
        border-radius: 18px;
        display: flex;
        height: 64px;
        justify-content: center;
        width: 64px;
      }
      .pipe-disc.dao { background: transparent; }
      .pipe-logo {
        display: block;
        height: 54px;
        object-fit: contain;
        width: 54px;
      }
      .conduit { height: 2px; background: var(--border); position: relative; }
      .conduit::after {
        animation: travel 1.6s ease-in-out infinite;
        background: var(--accent);
        border-radius: 8px;
        box-shadow: 0 0 12px rgba(70, 120, 190, .45);
        content: '';
        height: 6px;
        left: 0;
        position: absolute;
        top: -2px;
        width: 28px;
      }
      @keyframes travel { from { left: 0; } to { left: calc(100% - 28px); } }

      .overall { align-items: center; display: flex; gap: 12px; margin-bottom: 15px; }
      .progress-track {
        background: var(--surface-hover);
        border-radius: 8px;
        flex: 1;
        height: 8px;
        overflow: hidden;
      }
      .progress-fill {
        background: var(--accent);
        border-radius: inherit;
        height: 100%;
        transition: width 180ms ease;
      }
      .percent { font-size: 15px; font-variant-numeric: tabular-nums; font-weight: 650; }
      .task-list { display: flex; flex-direction: column; gap: 7px; }
      .task-row {
        align-items: center;
        background: var(--surface);
        border-radius: 10px;
        display: flex;
        gap: 10px;
        padding: 11px 13px;
      }
      .task-state {
        border: 2px solid var(--tertiary);
        border-radius: 50%;
        height: 17px;
        width: 17px;
      }
      .task-state.running {
        animation: spin .7s linear infinite;
        border-color: var(--accent-soft);
        border-top-color: var(--accent);
      }
      .task-state.succeeded { background: var(--success); border-color: var(--success); }
      .task-state.failed { border-color: var(--danger); }
      @keyframes spin { to { transform: rotate(360deg); } }
      .task-name { flex: 1; font-weight: 600; }
      .task-progress { color: var(--secondary); font-variant-numeric: tabular-nums; }
      .task-error { color: var(--danger); }

      .completion {
        align-items: center;
        display: flex;
        flex-direction: column;
        justify-content: center;
        min-height: 390px;
        text-align: center;
      }
      .done-ring {
        align-items: center;
        background: var(--accent-soft);
        border-radius: 50%;
        color: var(--accent-strong);
        display: flex;
        font-size: 30px;
        height: 92px;
        justify-content: center;
        margin-bottom: 20px;
        width: 92px;
      }
      .done-ring::before {
        border-bottom: 3px solid currentColor;
        border-right: 3px solid currentColor;
        content: '';
        height: 25px;
        transform: rotate(45deg) translate(-4px, -4px);
        width: 13px;
      }
      .failure-summary {
        align-items: flex-start;
        background: var(--danger-soft);
        border: 1px solid color-mix(in srgb, var(--danger) 45%, transparent);
        border-radius: 10px;
        color: var(--danger);
        display: flex;
        flex-direction: column;
        gap: 6px;
        margin-top: 18px;
        max-width: 100%;
        padding: 10px 14px;
        text-align: start;
      }
      .failure-summary strong { font-size: 13px; }
      .failure-categories { display: flex; flex-wrap: wrap; gap: 6px; }
      .failure-category {
        background: var(--panel);
        border-radius: 999px;
        font-size: 12px;
        font-weight: 650;
        padding: 3px 9px;
      }
      .stats { display: flex; flex-wrap: wrap; gap: 8px; justify-content: center; margin-top: 22px; }
      .failure-summary + .stats { margin-top: 12px; }
      .stat {
        background: var(--surface);
        border: 1px solid transparent;
        border-radius: 10px;
        min-width: 105px;
        padding: 10px 13px;
      }
      .stat.failed {
        background: var(--danger-soft);
        border-color: color-mix(in srgb, var(--danger) 55%, transparent);
      }
      .stat strong { display: block; font-size: 20px; }
      .stat span { color: var(--secondary); }
      .stat.failed strong, .stat .stat-status { color: var(--danger); }
      .stat .stat-status {
        display: block;
        font-size: 11px;
        font-weight: 700;
        margin-top: 4px;
      }

      .hint { color: var(--tertiary); line-height: 1.4; }
      .actions { display: flex; gap: 8px; margin-inline-start: auto; }
      .button {
        background: transparent;
        border: 1px solid transparent;
        border-radius: 12px;
        color: var(--secondary);
        cursor: pointer;
        font-weight: 650;
        min-height: 36px;
        padding: 0 15px;
      }
      .button:hover { background: var(--surface-hover); color: var(--text); }
      .button.primary { background: var(--accent); color: white; }
      .button.primary:hover { background: var(--accent-strong); }
      .button:disabled { cursor: default; opacity: .4; }

      @media (max-width: 620px) {
        :host { padding: 0; }
        .shell { border: 0; border-radius: 0; height: 100vh; }
        main { padding: 24px 20px; }
        header, footer { padding: 16px 20px; }
        .source-grid { grid-template-columns: 1fr; }
        .hint { display: none; }
      }

      @media (prefers-reduced-motion: reduce) {
        *, *::before, *::after { animation: none !important; transition: none !important; }
      }
    `;
  }

  declare protected step_: number;
  declare protected loading_: boolean;
  declare protected sources_: SourceProfile[];
  declare protected selectedSourceId_: string;
  declare protected selectedCategories_: string[];
  declare protected categoryCounts_: Record<string, number|null|undefined>;
  declare protected jobState_: MigrationState|null;
  private listeners_: WebUiListener[];
  private countGeneration_: number;

  constructor() {
    super();
    this.step_ = 1;
    this.loading_ = true;
    this.sources_ = [];
    this.selectedSourceId_ = '';
    this.selectedCategories_ = [];
    this.categoryCounts_ = {};
    this.jobState_ = null;
    this.listeners_ = [];
    this.countGeneration_ = 0;
  }

  override connectedCallback() {
    super.connectedCallback();
    this.listeners_.push(addListener(
        'browser-migration-sources-changed', (...args: unknown[]) => {
          this.sources_ = args[0] as SourceProfile[];
          this.loading_ = false;
        }));
    this.listeners_.push(addListener(
        'browser-migration-state-changed', (...args: unknown[]) => {
          this.applyJobState_(args[0] as MigrationState);
        }));
    void this.load_();
  }

  override disconnectedCallback() {
    for (const listener of this.listeners_) {
      removeListener(listener);
    }
    this.listeners_ = [];
    super.disconnectedCallback?.();
  }

  private async load_() {
    const [sources, state] = await Promise.all([
      detectImportSources(), getBrowserMigrationState(),
    ]);
    this.sources_ = sources;
    this.loading_ = false;
    if (state) {
      this.applyJobState_(state);
    }
  }

  private applyJobState_(state: MigrationState) {
    this.jobState_ = state;
    this.selectedSourceId_ = state.sourceId;
    this.selectedCategories_ = state.categories.map(item => item.category);
    this.step_ = state.terminal ? 4 : 3;
  }

  private string_(key: string): string {
    return loadTimeData.getString(key);
  }

  private selectedSource_(): SourceProfile|undefined {
    return this.sources_.find(source => source.id === this.selectedSourceId_);
  }

  private sourceCards_(): SourceCard[] {
    const cards: SourceCard[] = [];
    for (const definition of SOURCE_KINDS) {
      const detected = this.sources_.filter(
          source => source.kind === definition.kind);
      if (detected.length > 0) {
        cards.push(...detected.map(source => ({
          kind: source.kind,
          browserName: source.browserName,
          source,
        })));
      } else {
        cards.push({
          kind: definition.kind,
          browserName: this.string_(definition.nameKey),
        });
      }
    }
    return cards;
  }

  private categoryLabel_(category: string): string {
    const key = `daoImportCategory${category[0]!.toUpperCase()}${category.slice(1)}`;
    return this.string_(key);
  }

  private sourceInitial_(kind: string): string {
    return kind.slice(0, 1).toUpperCase();
  }

  private sourceLogo_(kind: string): string {
    return `assets/${kind}.svg`;
  }

  private selectSource_(source: SourceProfile) {
    this.selectedSourceId_ = source.id;
    this.scanCandidateCounts_(source);
  }

  private scanCandidateCounts_(source: SourceProfile) {
    const generation = ++this.countGeneration_;
    this.categoryCounts_ = Object.fromEntries(
        source.supportedCategories.map(category => [category, undefined]));
    for (const category of source.supportedCategories) {
      void getImportItemCount(source.id, category).then(count => {
        if (generation !== this.countGeneration_ ||
            source.id !== this.selectedSourceId_) {
          return;
        }
        this.categoryCounts_ = {...this.categoryCounts_, [category]: count};
      });
    }
  }

  private renderCandidateCount_(category: string) {
    const count = this.categoryCounts_[category];
    if (count === undefined) {
      return this.string_('daoImportCountScanning');
    }
    if (count === null) {
      return this.string_('daoImportCountUnavailable');
    }
    return loadTimeData.getStringF(
        'daoImportCountScanned', count.toLocaleString());
  }

  private continueToCategories_() {
    const source = this.selectedSource_();
    if (!source) {
      return;
    }
    this.selectedCategories_ = CATEGORY_ORDER.filter(
        category => source.supportedCategories.includes(category));
    this.step_ = 2;
  }

  private toggleCategory_(category: string) {
    this.selectedCategories_ = this.selectedCategories_.includes(category) ?
        this.selectedCategories_.filter(item => item !== category) :
        [...this.selectedCategories_, category];
  }

  private start_() {
    if (!this.selectedSourceId_ || this.selectedCategories_.length === 0) {
      return;
    }
    startBrowserMigration(
        this.selectedSourceId_, [...this.selectedCategories_]);
    this.step_ = 3;
  }

  private rescan_() {
    ++this.countGeneration_;
    this.categoryCounts_ = {};
    this.loading_ = true;
    void detectImportSources().then(sources => {
      this.sources_ = sources;
      this.loading_ = false;
    });
  }

  private retryFailed_() {
    const failed = this.jobState_?.categories
                       .filter(item => item.phase === 'failed')
                       .map(item => item.category) || [];
    if (failed.length) {
      retryBrowserMigrationCategories(failed);
      this.step_ = 3;
    }
  }

  private progress_(): number {
    const categories = this.jobState_?.categories || [];
    if (!categories.length) {
      return 0;
    }
    const sum = categories.reduce((total, item) => {
      if (TERMINAL_PHASES.has(item.phase)) {
        return total + 1;
      }
      return total + (item.totalItems > 0 ?
                          Math.min(1, item.completedItems / item.totalItems) :
                          0);
    }, 0);
    return Math.round(sum / categories.length * 100);
  }

  private renderHeader_() {
    return html`
      <header>
        <div class="brand">
          <img class="brand-logo" src=${DAO_LOGO_URL} alt="">
          <span class="brand-name">${this.string_('daoImportWizardName')}</span>
        </div>
        <div class="rail" aria-hidden="true">
          ${[1, 2, 3, 4].map(step => html`
            <span class=${step === this.step_ ? 'current' :
                           step < this.step_ ? 'done' : ''}></span>`)}
        </div>
      </header>`;
  }

  private renderSources_() {
    return html`
      <div class="eyebrow">${this.string_('daoImportStepSource')}</div>
      <h1>${this.string_('daoImportSourceTitle')}</h1>
      <p class="description">${this.string_('daoImportSourceDescription')}</p>
      ${this.loading_ ? html`
        <div class="empty">${this.string_('daoImportDetectingSources')}</div>` :
        html`
          ${this.sources_.length === 0 ? html`
            <div class="empty" data-test="empty">
              <span class="empty-dot" aria-hidden="true"></span>
              <span>${this.string_('daoImportNoSources')}</span>
              <button class="button" data-test="rescan"
                  @click=${() => this.rescan_()}>
                ${this.string_('daoImportScanAgain')}
              </button>
            </div>` : nothing}
          <div class="source-grid" role="listbox">
            ${this.sourceCards_().map(card => {
              const source = card.source;
              return html`
              <button class="source-card ${
                  source?.id === this.selectedSourceId_ ? 'selected' : ''}"
                  data-source-kind=${card.kind}
                  data-source-id=${source?.id || nothing}
                  aria-selected="${source?.id === this.selectedSourceId_}"
                  .disabled=${!source}
                  @click=${() => source && this.selectSource_(source)}>
                <span class="source-mark ${card.kind}" aria-hidden="true">
                  <img class="source-logo" src=${this.sourceLogo_(card.kind)}
                      alt="">
                </span>
                <span class="card-copy">
                  <span class="card-title">${card.browserName}</span>
                  <span class="card-meta">${source?.profileName ||
                      this.string_('daoImportSourceNotDetected')}</span>
                </span>
                <span class="selection" aria-hidden="true"></span>
              </button>`;
            })}
          </div>`}
    `;
  }

  private renderCategories_() {
    const source = this.selectedSource_();
    return html`
      <div class="eyebrow">${this.string_('daoImportStepData')}</div>
      <h1>${this.string_('daoImportDataTitle')}</h1>
      <p class="description">${this.string_('daoImportDataDescription')}</p>
      <div class="category-list">
        ${(source?.supportedCategories || []).map(category => html`
          <button class="category-row ${
              this.selectedCategories_.includes(category) ? 'selected' : ''}"
              data-category=${category}
              role="switch"
              aria-checked="${this.selectedCategories_.includes(category)}"
              @click=${() => this.toggleCategory_(category)}>
            <span class="category-glyph" aria-hidden="true"></span>
            <span class="card-title">${this.categoryLabel_(category)}</span>
            <span class="category-count" data-test="candidate-count">
              ${this.renderCandidateCount_(category)}
            </span>
            <span class="switch" aria-hidden="true"></span>
          </button>`)}
      </div>
      ${this.selectedCategories_.includes('passwords') ? html`
        <div class="tip" data-test="password-tip">
          <span class="tip-mark" aria-hidden="true">i</span>
          <div>
            <strong>${this.string_('daoImportPasswordTipTitle')}</strong>
            <span>${this.string_('daoImportPasswordTipDescription')}</span>
          </div>
        </div>` : nothing}
      ${this.selectedCategories_.includes('extensions') ? html`
        <div class="tip">
          <span class="tip-mark" aria-hidden="true">i</span>
          <div>
            <strong>${this.string_('daoImportExtensionTipTitle')}</strong>
            <span>${this.string_('daoImportExtensionTipDescription')}</span>
          </div>
        </div>` : nothing}
    `;
  }

  private renderMigration_() {
    const source = this.selectedSource_();
    const progress = this.progress_();
    return html`
      <div class="eyebrow">${this.string_('daoImportStepProgress')}</div>
      <h1>${this.string_('daoImportProgressTitle')}</h1>
      <p class="description">${this.string_('daoImportProgressDescription')}</p>
      <div class="migration-pipe" aria-hidden="true">
        <div class="pipe-node">
          <span class="pipe-disc source-mark ${source?.kind || ''}">
            ${source ? html`<img class="source-logo"
                src=${this.sourceLogo_(source.kind)} alt="">` :
                this.sourceInitial_('')}
          </span>
          <span>${source?.browserName || this.string_('daoImportSourceFallback')}</span>
        </div>
        <span class="conduit"></span>
        <div class="pipe-node">
          <span class="pipe-disc dao">
            <img class="pipe-logo" data-test="dao-logo"
                src=${DAO_LOGO_URL} alt="">
          </span>
          <span>${this.string_('daoImportDaoName')}</span>
        </div>
      </div>
      <div class="overall">
        <div class="progress-track"><div class="progress-fill"
            style=${`width: ${progress}%`}></div></div>
        <span class="percent">${progress}%</span>
      </div>
      <div class="task-list" role="status" aria-live="polite">
        ${(this.jobState_?.categories || this.selectedCategories_.map(
            category => ({category, phase: 'pending'} as CategoryState)))
            .map(item => this.renderTask_(item))}
      </div>
    `;
  }

  private renderTask_(item: CategoryState) {
    const running = ['snapshotting', 'reading', 'writing'].includes(item.phase);
    const count = item.totalItems > 0 ?
        `${item.completedItems} / ${item.totalItems}` :
        this.string_(`daoImportPhase${
            item.phase[0]!.toUpperCase()}${item.phase.slice(1)}`);
    return html`
      <div class="task-row">
        <span class="task-state ${running ? 'running' : item.phase}"
            aria-hidden="true"></span>
        <span class="task-name">${this.categoryLabel_(item.category)}</span>
        <span class="task-progress ${item.phase === 'failed' ? 'task-error' : ''}">
          ${count}
        </span>
      </div>`;
  }

  private renderDone_() {
    const failed = this.jobState_?.categories.filter(
        item => item.phase === 'failed') || [];
    const cancelled = this.jobState_?.categories.some(
        item => item.phase === 'cancelled') || false;
    const totals = this.jobState_?.categories || [];
    return html`
      <div class="completion">
        <div class="done-ring" aria-hidden="true"></div>
        <h1>${cancelled ? this.string_('daoImportCancelledTitle') :
                         failed.length ? this.string_('daoImportPartialTitle') :
                                         this.string_('daoImportDoneTitle')}</h1>
        <p class="description">${cancelled ?
            this.string_('daoImportCancelledDescription') : failed.length ?
            this.string_('daoImportPartialDescription') :
            this.string_('daoImportDoneDescription')}</p>
        ${failed.length ? html`
          <div class="failure-summary" data-test="failure-summary"
              role="status">
            <strong>${this.string_('daoImportFailedSummaryTitle')}</strong>
            <div class="failure-categories">
              ${failed.map(item => html`
                <span class="failure-category">
                  ${this.categoryLabel_(item.category)}
                </span>`)}
            </div>
          </div>` : nothing}
        <div class="stats">
          ${totals.map(item => html`
            <div class="stat ${item.phase === 'failed' ? 'failed' : ''}"
                data-summary-category=${item.category}>
              <strong>${item.imported || 0}</strong>
              <span>${this.categoryLabel_(item.category)}</span>
              ${item.phase === 'failed' ? html`
                <span class="stat-status">
                  ${this.string_('daoImportFailedStatus')}
                </span>` : nothing}
            </div>`)}
        </div>
      </div>`;
  }

  private renderFooter_() {
    const hasFailed = this.jobState_?.categories.some(
        item => item.phase === 'failed');
    return html`
      <footer>
        <span class="hint">${this.step_ === 2 ?
            this.string_('daoImportMergeHint') :
            this.string_('daoImportLocalHint')}</span>
        <div class="actions">
          ${this.step_ === 2 ? html`
            <button class="button" @click=${() => this.step_ = 1}>
              ${this.string_('daoImportBack')}
            </button>
            <button class="button primary" data-test="start"
                .disabled=${this.selectedCategories_.length === 0}
                @click=${() => this.start_()}>
              ${this.string_('daoImportStart')}
            </button>` :
            this.step_ === 3 ? html`
            <button class="button" @click=${cancelBrowserMigration}
                .disabled=${this.jobState_?.cancelRequested || false}>
              ${this.string_('daoImportCancel')}
            </button>` :
            this.step_ === 4 && hasFailed ? html`
            <button class="button primary" @click=${() => this.retryFailed_()}>
              ${this.string_('daoImportRetryFailed')}
            </button>` :
            this.step_ === 1 ? html`
            <button class="button primary" data-test="continue"
                .disabled=${!this.selectedSource_()}
                @click=${() => this.continueToCategories_()}>
              ${this.string_('daoImportContinue')}
            </button>` : nothing}
        </div>
      </footer>`;
  }

  override render() {
    return html`
      <section class="shell">
        ${this.renderHeader_()}
        <main>
          ${this.step_ === 1 ? this.renderSources_() :
            this.step_ === 2 ? this.renderCategories_() :
            this.step_ === 3 ? this.renderMigration_() : this.renderDone_()}
        </main>
        ${this.renderFooter_()}
      </section>`;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'dao-import-app': DaoImportApp;
  }
}

customElements.define(DaoImportApp.is, DaoImportApp);
