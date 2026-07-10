// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {callNative, callNativeArgs} from './dream_bridge.js';
import {initI18n, t} from './i18n/i18n.js';
import {renderDaoMarkdown} from './dao_markdown.js';
import {
  copyPngBlobToClipboard,
  renderDreamReportShareImage,
} from './dao_share_image.js';

interface DreamHabit {
  key: string;
  value: string;
  confidence: number;
  evidence: string;
  relation: 'new'|'reinforce'|'contradict';
}

interface DreamReportData {
  id: number;
  dreamDate: string;
  reportMarkdown: string;
  habits: DreamHabit[];
  materialStats: string;
  debugMaterialJson: string;
  triggerKind: string;
  createdAt?: number;
}

type HabitState = 'confirmed'|'rejected';
type ShareStatus = 'idle'|'copying'|'copied'|'failed';

const DREAM_HABIT_FEEDBACK_STORAGE_KEY = 'dao.dream.habitFeedback.v1';
const DREAM_RUN_NATIVE_TIMEOUT_MS = 6 * 60 * 1000;

export class DaoDreamApp extends CrLitElement {
  static get is() {
    return 'dao-dream-app';
  }

  static override get properties() {
    return {
      loading_: {type: Boolean, state: true},
      report_: {type: Object, state: true},
      reports_: {type: Array, state: true},
      error_: {type: String, state: true},
      habitStates_: {type: Object, state: true},
      shareStatus_: {type: String, state: true},
      rerunRunning_: {type: Boolean, state: true},
      rerunError_: {type: String, state: true},
      dreamExcludedDomains_: {type: Array, state: true},
      dreamExclusionAdding_: {type: Boolean, state: true},
      dreamExclusionError_: {type: String, state: true},
    };
  }

  declare private loading_: boolean;
  declare private report_: DreamReportData|null;
  declare private reports_: DreamReportData[];
  declare private error_: string;
  declare private habitStates_: Record<number, HabitState>;
  declare private shareStatus_: ShareStatus;
  declare private rerunRunning_: boolean;
  declare private rerunError_: string;
  declare private dreamExcludedDomains_: string[];
  declare private dreamExclusionAdding_: boolean;
  declare private dreamExclusionError_: string;

  constructor() {
    super();
    this.loading_ = true;
    this.report_ = null;
    this.reports_ = [];
    this.error_ = '';
    this.habitStates_ = {};
    this.shareStatus_ = 'idle';
    this.rerunRunning_ = false;
    this.rerunError_ = '';
    this.dreamExcludedDomains_ = [];
    this.dreamExclusionAdding_ = false;
    this.dreamExclusionError_ = '';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
        min-height: 100vh;
        color: rgba(30, 20, 40, 0.88);
        background:
          linear-gradient(180deg, rgb(238, 243, 248) 0%, rgb(249, 251, 253) 46%),
          rgb(249, 251, 253);
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      }

      .page {
        width: min(880px, calc(100vw - 40px));
        margin: 0 auto;
        padding: 44px 0 64px;
      }

      header {
        display: flex;
        align-items: flex-end;
        justify-content: space-between;
        gap: 20px;
        padding-bottom: 24px;
        border-bottom: 1px solid rgba(70, 120, 190, 0.18);
      }

      .eyebrow {
        color: rgb(70, 120, 190);
        font-size: 12px;
        font-weight: 600;
        margin-bottom: 8px;
      }

      h1 {
        margin: 0;
        font-size: 34px;
        line-height: 1.12;
        font-weight: 700;
        letter-spacing: 0;
      }

      .date {
        color: rgba(30, 20, 40, 0.56);
        font-size: 13px;
        white-space: nowrap;
      }

      .header-actions {
        display: flex;
        align-items: center;
        justify-content: flex-end;
        gap: 10px;
        flex-wrap: wrap;
      }

      .copy-image-button,
      .rerun-report-button {
        display: inline-flex;
        align-items: center;
        justify-content: center;
        width: 30px;
        height: 30px;
        padding: 0;
        border: 1px solid rgba(70, 120, 190, 0.24);
        border-radius: 8px;
        background: rgba(255, 255, 255, 0.68);
        color: rgba(30, 20, 40, 0.80);
        line-height: 1;
        cursor: pointer;
      }

      .copy-image-button:hover:not(:disabled),
      .rerun-report-button:hover:not(:disabled) {
        background: rgba(255, 255, 255, 0.92);
        border-color: rgba(70, 120, 190, 0.42);
      }

      .copy-image-button:disabled,
      .rerun-report-button:disabled {
        cursor: default;
        opacity: 0.68;
      }

      .copy-image-button svg,
      .rerun-report-button svg {
        width: 14px;
        height: 14px;
        flex: 0 0 14px;
      }

      main {
        padding-top: 28px;
      }

      .status {
        display: flex;
        align-items: center;
        justify-content: center;
        min-height: 240px;
        color: rgba(30, 20, 40, 0.58);
        font-size: 14px;
        text-align: center;
      }

      .empty-title {
        display: block;
        color: rgba(30, 20, 40, 0.82);
        font-size: 18px;
        font-weight: 650;
        margin-bottom: 8px;
      }

      article {
        display: grid;
        gap: 22px;
      }

      .history-layout {
        display: grid;
        grid-template-columns: minmax(180px, 230px) minmax(0, 1fr);
        gap: 36px;
        align-items: start;
      }

      .history-list {
        border-right: 1px solid rgba(70, 120, 190, 0.14);
        padding-right: 16px;
      }

      .history-list-title {
        margin: 0 0 10px;
        color: rgba(30, 20, 40, 0.56);
        font-size: 12px;
        font-weight: 650;
      }

      .history-rerun-error {
        margin: -4px 0 10px;
        color: rgb(160, 48, 48);
        font-size: 12px;
        line-height: 1.4;
      }

      .report-domain-picker {
        border-top: 1px solid rgba(70, 120, 190, 0.16);
        padding-top: 18px;
      }

      .report-domain-summary {
        color: rgba(30, 20, 40, 0.88);
        font-size: 16px;
        font-weight: 650;
        line-height: 1.3;
      }

      .report-domain-picker[open] > .report-domain-summary {
        margin-bottom: 10px;
      }

      .report-domain-list {
        display: grid;
        gap: 8px;
      }

      .report-domain-option {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 6px;
        min-width: 0;
        max-width: 100%;
        min-height: 34px;
        padding: 6px 10px;
        border: 1px solid rgba(70, 120, 190, 0.18);
        border-radius: 8px;
        background: rgba(255, 255, 255, 0.62);
        color: rgba(30, 20, 40, 0.72);
        font-size: 12px;
      }

      .report-domain-option.excluded {
        background: rgba(70, 120, 190, 0.08);
        color: rgba(30, 20, 40, 0.46);
      }

      .report-domain-option span {
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
      }

      .report-domain-add-button {
        display: inline-flex;
        align-items: center;
        justify-content: center;
        flex: 0 0 auto;
        width: 30px;
        padding: 0;
      }

      .report-domain-add-button svg {
        width: 14px;
        height: 14px;
      }

      .report-domain-status {
        flex: 0 0 auto;
        color: rgba(30, 20, 40, 0.46);
        font-size: 12px;
      }

      .report-domain-empty {
        margin: 12px 0 0;
        color: rgba(30, 20, 40, 0.52);
        font-size: 12px;
        line-height: 1.45;
      }

      .report-body {
        min-width: 0;
      }

      .history-item {
        display: block;
        width: 100%;
        height: auto;
        min-height: 36px;
        margin: 0 0 6px;
        padding: 8px 10px;
        border-color: transparent;
        background: transparent;
        text-align: left;
      }

      .history-item:hover,
      .history-item.selected {
        border-color: rgba(70, 120, 190, 0.22);
        background: rgba(70, 120, 190, 0.08);
      }

      .history-date {
        display: block;
        font-size: 13px;
        font-weight: 650;
        line-height: 1.3;
      }

      .history-kind {
        display: block;
        margin-top: 2px;
        color: rgba(30, 20, 40, 0.48);
        font-size: 11px;
        line-height: 1.3;
      }

      .markdown {
        font-size: 15px;
        line-height: 1.72;
      }

      .markdown h1,
      .markdown h2,
      .markdown h3 {
        margin: 1.2em 0 0.45em;
        line-height: 1.25;
        letter-spacing: 0;
      }

      .markdown h1:first-child,
      .markdown h2:first-child,
      .markdown h3:first-child {
        margin-top: 0;
      }

      .markdown p {
        margin: 0 0 0.9em;
      }

      .markdown ul,
      .markdown ol {
        padding-left: 1.4em;
      }

      section {
        border-top: 1px solid rgba(70, 120, 190, 0.16);
        padding-top: 18px;
      }

      h2 {
        margin: 0 0 12px;
        font-size: 16px;
        line-height: 1.3;
        font-weight: 650;
        letter-spacing: 0;
      }

      .habit {
        display: flex;
        align-items: flex-start;
        gap: 12px;
        padding: 12px 0;
        border-top: 1px solid rgba(30, 20, 40, 0.08);
      }

      .habit:first-of-type {
        border-top: 0;
      }

      .habit-body {
        flex: 1;
        min-width: 0;
      }

      .habit-value {
        font-size: 14px;
        line-height: 1.5;
      }

      .habit-evidence {
        margin-top: 4px;
        color: rgba(30, 20, 40, 0.52);
        font-size: 12px;
        line-height: 1.45;
      }

      .actions {
        display: flex;
        flex-wrap: wrap;
        gap: 8px;
        justify-content: flex-end;
      }

      button {
        height: 30px;
        padding: 0 12px;
        border: 1px solid rgba(70, 120, 190, 0.24);
        border-radius: 8px;
        background: rgba(255, 255, 255, 0.68);
        color: rgba(30, 20, 40, 0.80);
        font: inherit;
        font-size: 12px;
        cursor: pointer;
      }

      button:hover {
        background: rgba(255, 255, 255, 0.92);
        border-color: rgba(70, 120, 190, 0.42);
      }

      .state {
        color: rgba(30, 20, 40, 0.52);
        font-size: 12px;
        line-height: 30px;
      }

      details {
        border-top: 1px solid rgba(70, 120, 190, 0.16);
        padding-top: 18px;
      }

      summary {
        color: rgba(30, 20, 40, 0.62);
        cursor: pointer;
        font-size: 13px;
      }

      pre {
        max-height: 420px;
        overflow: auto;
        margin: 12px 0 0;
        padding: 12px;
        border-radius: 8px;
        background: rgba(70, 120, 190, 0.08);
        color: rgba(30, 20, 40, 0.72);
        font-size: 12px;
        line-height: 1.45;
        white-space: pre-wrap;
        word-break: break-word;
      }

      .debug-meta {
        margin: 12px 0 0;
        color: rgba(30, 20, 40, 0.58);
        font-size: 12px;
      }

      @media (max-width: 640px) {
        .page {
          width: min(100vw - 28px, 880px);
          padding-top: 28px;
        }

        header {
          display: block;
        }

        h1 {
          font-size: 28px;
        }

        .date {
          display: block;
          margin-top: 10px;
          white-space: normal;
        }

        .header-actions {
          justify-content: flex-start;
        }

        .habit {
          display: block;
        }

        .history-layout {
          display: block;
        }

        .history-list {
          border-right: 0;
          border-bottom: 1px solid rgba(70, 120, 190, 0.14);
          margin-bottom: 22px;
          padding: 0 0 16px;
        }

        .actions {
          justify-content: flex-start;
          margin-top: 10px;
        }
      }
    `;
  }

  override connectedCallback() {
    super.connectedCallback();
    void this.loadPage_();
  }

  private currentRoute_(): 'today'|'history' {
    const path = window.location.pathname.replace(/\/+$/, '');
    return path === '/today' ? 'today' : 'history';
  }

  private async loadPage_() {
    this.loading_ = true;
    this.error_ = '';
    await initI18n();
    void this.loadDreamExcludedDomains_();
    if (this.currentRoute_() === 'today') {
      await this.loadTodayReport_();
    } else {
      await this.loadHistory_();
    }
    this.loading_ = false;
  }

  private async loadTodayReport_() {
    try {
      const raw = await callNative('getTodayDreamReport');
      const report = this.normalizeReport_(raw);
      this.report_ = report;
      this.reports_ = [];
      this.habitStates_ = report ? this.loadPersistedHabitStates_(report) : {};
      if (report) {
        this.markReportViewed_(report);
      }
    } catch (e) {
      this.error_ = e instanceof Error ? e.message : String(e);
      this.report_ = null;
      this.reports_ = [];
    }
  }

  private async loadHistory_() {
    try {
      const raw = await callNative('getDreamReports', {limit: 30});
      const reports = this.normalizeReports_(raw);
      this.reports_ = reports;
      this.report_ = reports[0] || null;
      this.habitStates_ =
          this.report_ ? this.loadPersistedHabitStates_(this.report_) : {};
      if (this.report_) {
        this.markReportViewed_(this.report_);
      }
    } catch (e) {
      this.error_ = e instanceof Error ? e.message : String(e);
      this.report_ = null;
      this.reports_ = [];
    }
  }

  private normalizeReport_(raw: unknown): DreamReportData|null {
    const r = raw as {
      id?: number;
      dreamDate?: string;
      reportMarkdown?: string;
      habitCandidates?: string;
      debugMaterialJson?: string;
      materialStats?: string;
      triggerKind?: string;
      createdAt?: number;
    } | null;
    if (!r || typeof r.id !== 'number') {
      return null;
    }
    let habits: DreamHabit[] = [];
    try {
      const parsed = JSON.parse(r.habitCandidates || '[]');
      if (Array.isArray(parsed)) {
        habits = parsed.filter((h): h is DreamHabit => {
          return typeof h === 'object' && h !== null &&
              typeof h.key === 'string' && typeof h.value === 'string';
        });
      }
    } catch {
      habits = [];
    }
    return {
      id: r.id,
      dreamDate: r.dreamDate || '',
      reportMarkdown: r.reportMarkdown || '',
      habits,
      materialStats: r.materialStats || '',
      debugMaterialJson: r.debugMaterialJson || '',
      triggerKind: r.triggerKind || '',
      createdAt:
          typeof r.createdAt === 'number' && Number.isFinite(r.createdAt) ?
          r.createdAt : undefined,
    };
  }

  private normalizeReports_(raw: unknown): DreamReportData[] {
    if (!Array.isArray(raw)) {
      return [];
    }
    return raw
        .map(item => this.normalizeReport_(item))
        .filter((item): item is DreamReportData => item !== null);
  }

  private markReportViewed_(report: DreamReportData) {
    callNativeArgs('markDreamReportViewed', report.id).catch(() => {});
  }

  private selectHistoryReport_(report: DreamReportData) {
    this.report_ = report;
    this.habitStates_ = this.loadPersistedHabitStates_(report);
    this.dreamExclusionError_ = '';
    this.markReportViewed_(report);
  }

  private async rerunDreamDate_(date: string) {
    const dreamDate = date.trim();
    if (!dreamDate || this.rerunRunning_) {
      return;
    }
    this.rerunRunning_ = true;
    this.rerunError_ = '';
    const previousReport = this.report_;
    try {
      await callNative('startManualDream', {date: dreamDate}, {
        timeoutMs: DREAM_RUN_NATIVE_TIMEOUT_MS,
      });
      await this.loadHistory_();
      const match = this.reports_.find(report => report.dreamDate === dreamDate);
      if (match) {
        this.selectHistoryReport_(match);
      }
    } catch (e) {
      this.report_ = previousReport;
      this.rerunError_ = e instanceof Error ? e.message : String(e);
    } finally {
      this.rerunRunning_ = false;
    }
  }

  private async loadDreamExcludedDomains_() {
    try {
      const domains = await callNativeArgs('getDreamExcludedDomains');
      this.dreamExcludedDomains_ = Array.isArray(domains) ?
          domains.filter(
              (domain): domain is string => typeof domain === 'string') :
          [];
    } catch {
      this.dreamExcludedDomains_ = [];
    }
  }

  private sourceDomainsForReport_(report: DreamReportData): string[] {
    try {
      const stats = JSON.parse(report.materialStats || '{}') as {
        source_domains?: unknown;
      };
      if (!Array.isArray(stats.source_domains)) {
        return [];
      }
      const seen = new Set<string>();
      const domains: string[] = [];
      for (const item of stats.source_domains) {
        if (typeof item !== 'string') {
          continue;
        }
        const domain = item.trim();
        if (!domain || seen.has(domain)) {
          continue;
        }
        seen.add(domain);
        domains.push(domain);
      }
      return domains;
    } catch {
      return [];
    }
  }

  private isDreamExcludedDomain_(domain: string) {
    return this.dreamExcludedDomains_.includes(domain);
  }

  private async addDreamExcludedDomain_(domain: string) {
    if (this.isDreamExcludedDomain_(domain) || this.dreamExclusionAdding_) {
      return;
    }
    if (!confirm(t('dream.page.source_domains_confirm', {domain}))) {
      return;
    }
    this.dreamExclusionAdding_ = true;
    this.dreamExclusionError_ = '';
    try {
      const result = await callNativeArgs('addDreamExcludedDomain', domain) as
          {domain?: string};
      if (typeof result?.domain === 'string' && result.domain) {
        this.dreamExcludedDomains_ =
            [...new Set([...this.dreamExcludedDomains_, result.domain])].sort();
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      this.dreamExclusionError_ =
          t('dream.page.excluded_add_failed', {error: msg});
    } finally {
      this.dreamExclusionAdding_ = false;
    }
  }

  private confirmHabit_(habit: DreamHabit, index: number) {
    callNativeArgs('updatePreference', habit.key, habit.value, 0.95)
        .catch(() => {});
    this.persistHabitState_(habit, 'confirmed');
    this.habitStates_ = {...this.habitStates_, [index]: 'confirmed'};
  }

  private rejectHabit_(habit: DreamHabit, index: number) {
    callNative('getPreferences')
        .then((prefs) => {
          const list = prefs as Array<{id: number; key: string}>;
          const match = list.find((p) => p.key === habit.key);
          if (match) {
            return callNativeArgs('deleteMemory', 'preference', match.id);
          }
          return undefined;
        })
        .catch(() => {});
    this.persistHabitState_(habit, 'rejected');
    this.habitStates_ = {...this.habitStates_, [index]: 'rejected'};
  }

  private loadPersistedHabitStates_(report: DreamReportData):
      Record<number, HabitState> {
    const stored = this.readHabitFeedbackStore_();
    const states: Record<number, HabitState> = {};
    report.habits
        .filter((h) => h.relation !== 'reinforce')
        .forEach((habit, index) => {
          const state = stored[this.habitFeedbackKey_(report, habit)];
          if (state === 'confirmed' || state === 'rejected') {
            states[index] = state;
          }
        });
    return states;
  }

  private persistHabitState_(habit: DreamHabit, state: HabitState) {
    if (!this.report_) {
      return;
    }
    const stored = this.readHabitFeedbackStore_();
    stored[this.habitFeedbackKey_(this.report_, habit)] = state;
    try {
      localStorage.setItem(
          DREAM_HABIT_FEEDBACK_STORAGE_KEY, JSON.stringify(stored));
    } catch {}
  }

  private readHabitFeedbackStore_(): Record<string, HabitState> {
    try {
      const parsed = JSON.parse(
          localStorage.getItem(DREAM_HABIT_FEEDBACK_STORAGE_KEY) || '{}');
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
        return {};
      }
      const states: Record<string, HabitState> = {};
      for (const [key, value] of Object.entries(parsed)) {
        if (value === 'confirmed' || value === 'rejected') {
          states[key] = value;
        }
      }
      return states;
    } catch {
      return {};
    }
  }

  private habitFeedbackKey_(report: DreamReportData, habit: DreamHabit) {
    return `${report.dreamDate}\u0000${habit.key}\u0000${habit.value}`;
  }

  private renderMarkdown_(markdown: string) {
    const container = document.createElement('div');
    container.className = 'markdown';
    container.innerHTML = renderDaoMarkdown(markdown);
    return html`${container}`;
  }

  private renderHabits_(report: DreamReportData) {
    const rows = report.habits.filter((h) => h.relation !== 'reinforce');
    if (rows.length === 0) {
      return nothing;
    }
    return html`
      <section>
        <h2>${t('chat.dream.habits_title')}</h2>
        ${rows.map((habit, index) => html`
          <div class="habit">
            <div class="habit-body">
              <div class="habit-value">
                ${habit.relation === 'contradict' ?
                    t('chat.dream.contradict_prefix') + ' ' : ''}${habit.value}
              </div>
              ${habit.evidence ? html`
                <div class="habit-evidence">${habit.evidence}</div>` :
                nothing}
            </div>
            <div class="actions">
              ${this.habitStates_[index] === 'confirmed' ?
                html`<span class="state">
                  ${t('chat.dream.habit_confirmed')}</span>` :
                this.habitStates_[index] === 'rejected' ?
                html`<span class="state">
                  ${t('chat.dream.habit_rejected')}</span>` :
                html`
                  <button @click=${() => this.confirmHabit_(habit, index)}>
                    ${t('chat.dream.habit_confirm')}</button>
                  <button @click=${() => this.rejectHabit_(habit, index)}>
                    ${t('chat.dream.habit_reject')}</button>`}
            </div>
          </div>`)}
      </section>`;
  }

  private renderHistoryList_() {
    return html`
      <nav class="history-list" aria-label=${t('dream.page.history_title')}>
        <h2 class="history-list-title">${t('dream.page.history_title')}</h2>
        ${this.reports_.map(report => html`
          <button
              class=${'history-item ' +
                  (this.report_?.id === report.id ? 'selected' : '')}
              @click=${() => this.selectHistoryReport_(report)}>
            <span class="history-date">${report.dreamDate}</span>
            <span class="history-kind">
              ${this.triggerKindLabel_(report.triggerKind)}
            </span>
          </button>`)}
      </nav>`;
  }

  private renderDreamExclusionShortcut_(report: DreamReportData) {
    const domains = this.sourceDomainsForReport_(report);
    return html`
      <details class="report-domain-picker">
        <summary class="report-domain-summary">
          ${t('dream.page.source_domains_title')}
        </summary>
        ${domains.length === 0 ? html`
          <p class="report-domain-empty">
            ${t('dream.page.source_domains_empty')}
          </p>` : html`
          <div class="report-domain-list">
            ${domains.map(domain => this.renderSourceDomainRow_(domain))}
          </div>
          ${this.dreamExclusionError_ ? html`
            <div class="history-rerun-error">
              ${this.dreamExclusionError_}
            </div>` : nothing}`}
      </details>`;
  }

  private renderSourceDomainRow_(domain: string) {
    const excluded = this.isDreamExcludedDomain_(domain);
    const addLabel = t('dream.page.source_domains_add');
    const addingLabel = t('dream.page.excluded_domains_adding');
    return html`
      <div class="${'report-domain-option ' + (excluded ? 'excluded' : '')}">
        <span data-domain-label="${domain}">${domain}</span>
        ${excluded ? html`
          <span class="report-domain-status">
            ${t('dream.page.source_domains_excluded')}
          </span>` : this.dreamExclusionAdding_ ? html`
          <button class="report-domain-add-button"
              data-testid="dream-add-domain-button"
              data-domain="${domain}"
              title="${addingLabel}"
              aria-label="${addingLabel}"
              disabled>
            ${this.renderDomainExclusionIcon_()}
          </button>` : html`
          <button class="report-domain-add-button"
              data-testid="dream-add-domain-button"
              data-domain="${domain}"
              title="${addLabel}"
              aria-label="${addLabel}"
              @click=${() => void this.addDreamExcludedDomain_(domain)}>
            ${this.renderDomainExclusionIcon_()}
          </button>`}
      </div>`;
  }

  private renderDomainExclusionIcon_() {
    return html`
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
          stroke-width="2" stroke-linecap="round" stroke-linejoin="round"
          aria-hidden="true">
        <circle cx="12" cy="12" r="10"></circle>
        <path d="M4.929 4.929 19.07 19.071"></path>
      </svg>`;
  }

  private renderRerunReportButton_(report: DreamReportData) {
    const label = t('dream.page.rerun_report');
    const icon = html`
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
          stroke-width="2" stroke-linecap="round"
          stroke-linejoin="round" aria-hidden="true">
        <path d="M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16"></path>
        <path d="M3 21v-5h5"></path>
        <path d="M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8"></path>
        <path d="M21 3v5h-5"></path>
      </svg>`;
    return this.rerunRunning_ ? html`
      <button class="rerun-report-button"
          data-testid="dream-rerun-current-button"
          title="${label}"
          aria-label="${label}"
          disabled>
        ${icon}
      </button>` : html`
      <button class="rerun-report-button"
          data-testid="dream-rerun-current-button"
          title="${label}"
          aria-label="${label}"
          @click=${() => void this.rerunDreamDate_(report.dreamDate)}>
        ${icon}
      </button>`;
  }

  private triggerKindLabel_(triggerKind: string) {
    switch (triggerKind) {
      case 'nightly':
        return t('dream.trigger.nightly');
      case 'catchup':
        return t('dream.trigger.catchup');
      case 'manual':
        return t('dream.trigger.manual');
      default:
        return triggerKind;
    }
  }

  private shareButtonLabel_() {
    switch (this.shareStatus_) {
      case 'copying':
        return t('dream.page.copy_image');
      case 'copied':
        return t('dream.page.copy_image_copied');
      case 'failed':
        return t('dream.page.copy_image_failed');
      default:
        return t('dream.page.copy_image');
    }
  }

  private shareButtonIcon_() {
    switch (this.shareStatus_) {
      case 'copied':
        return html`
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
              stroke-width="2" stroke-linecap="round" stroke-linejoin="round"
              aria-hidden="true">
            <path d="M20 6 9 17l-5-5"></path>
          </svg>`;
      case 'failed':
        return html`
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
              stroke-width="2" stroke-linecap="round" stroke-linejoin="round"
              aria-hidden="true">
            <path d="M18 6 6 18"></path>
            <path d="m6 6 12 12"></path>
          </svg>`;
      default:
        return html`
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
              stroke-width="2" stroke-linecap="round" stroke-linejoin="round"
              aria-hidden="true">
            <rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect>
            <circle cx="9" cy="9" r="2"></circle>
            <path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"></path>
          </svg>`;
    }
  }

  private renderCopyImageButton_() {
    const label = this.shareButtonLabel_();
    return this.shareStatus_ === 'copying' ? html`
      <button class="copy-image-button" title="${label}"
          aria-label="${label}" disabled
          @click=${() => void this.copyReportImage_()}>
        ${this.shareButtonIcon_()}
      </button>
    ` : html`
      <button class="copy-image-button" title="${label}"
          aria-label="${label}"
          @click=${() => void this.copyReportImage_()}>
        ${this.shareButtonIcon_()}
      </button>
    `;
  }

  private formatGeneratedAt_(createdAt?: number) {
    if (typeof createdAt !== 'number' || !Number.isFinite(createdAt)) {
      return '';
    }
    const date = new Date(createdAt);
    if (!Number.isFinite(date.getTime())) {
      return '';
    }
    return new Intl.DateTimeFormat(undefined, {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit',
      hour12: false,
    }).format(date);
  }

  private resetShareStatusLater_(status: ShareStatus) {
    window.setTimeout(() => {
      if (this.shareStatus_ === status) {
        this.shareStatus_ = 'idle';
      }
    }, 2000);
  }

  private async copyReportImage_() {
    const report = this.report_;
    if (!report || this.shareStatus_ === 'copying') {
      return;
    }

    this.shareStatus_ = 'copying';
    try {
      const blob = await renderDreamReportShareImage({
        title: t('dream.page.title'),
        dateLabel: t('chat.dream.card_date', {date: report.dreamDate}),
        markdown: report.reportMarkdown,
        footer: t('dream.share.footer'),
      });
      await copyPngBlobToClipboard(blob);
      this.shareStatus_ = 'copied';
      this.resetShareStatusLater_('copied');
    } catch (e) {
      console.warn('[dao] dream report image copy failed', e);
      this.shareStatus_ = 'failed';
      this.resetShareStatusLater_('failed');
    }
  }

  private renderReportArticle_(report: DreamReportData) {
    const generatedAt = this.formatGeneratedAt_(report.createdAt);
    return html`
      <article>
        <div class="report-body">
          ${this.renderMarkdown_(report.reportMarkdown)}
        </div>
        ${this.renderHabits_(report)}
        ${this.renderDreamExclusionShortcut_(report)}
        ${report.debugMaterialJson ? html`
          <details>
            <summary>${t('chat.dream.debug_title')}</summary>
            ${generatedAt ? html`
              <div class="debug-meta">
                ${t('dream.debug.generated_at', {time: generatedAt})}
              </div>` : nothing}
            <pre>${report.debugMaterialJson}</pre>
          </details>` : nothing}
      </article>`;
  }

  override render() {
    const report = this.report_;
    const isHistory = this.currentRoute_() === 'history';
    return html`
      <div class="page">
        <header>
          <div>
            <div class="eyebrow">${t('dream.page.eyebrow')}</div>
            <h1>${t('dream.page.title')}</h1>
          </div>
          ${report ? html`
            <div class="header-actions">
              <div class="date">
                ${t('chat.dream.card_date', {date: report.dreamDate})}
              </div>
              ${this.renderRerunReportButton_(report)}
              ${this.renderCopyImageButton_()}
            </div>` : nothing}
        </header>
        <main>
          ${this.loading_ ? html`
            <div class="status">${t('dream.page.loading')}</div>` :
          this.error_ ? html`
            <div class="status">${t('dream.page.error', {error: this.error_})}</div>` :
          !report ? html`
            <div class="status">
              <span>
                <span class="empty-title">${t('dream.page.empty_title')}</span>
                ${t('dream.page.empty_desc')}
              </span>
            </div>` :
          html`
            ${this.rerunError_ ? html`
              <div class="history-rerun-error">
                ${t('dream.page.rerun_failed', {error: this.rerunError_})}
              </div>` : nothing}
            ${isHistory ? html`
              <div class="history-layout">
                ${this.renderHistoryList_()}
                ${this.renderReportArticle_(report)}
              </div>` :
              this.renderReportArticle_(report)}`
          }
        </main>
      </div>`;
  }
}

customElements.define(DaoDreamApp.is, DaoDreamApp);
