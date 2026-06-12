// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {callNative, callNativeArgs} from './dream_bridge.js';
import {initI18n, t} from './i18n/i18n.js';
import {renderDaoMarkdown} from './dao_markdown.js';

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
  debugMaterialJson: string;
  triggerKind: string;
}

type HabitState = 'confirmed'|'rejected';

const DREAM_HABIT_FEEDBACK_STORAGE_KEY = 'dao.dream.habitFeedback.v1';

export class DaoDreamApp extends CrLitElement {
  static get is() {
    return 'dao-dream-app';
  }

  static override get properties() {
    return {
      loading_: {type: Boolean, state: true},
      report_: {state: true},
      reports_: {state: true},
      error_: {type: String, state: true},
      habitStates_: {state: true},
    };
  }

  declare private loading_: boolean;
  declare private report_: DreamReportData|null;
  declare private reports_: DreamReportData[];
  declare private error_: string;
  declare private habitStates_: Record<number, HabitState>;

  constructor() {
    super();
    this.loading_ = true;
    this.report_ = null;
    this.reports_ = [];
    this.error_ = '';
    this.habitStates_ = {};
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
      triggerKind?: string;
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
      debugMaterialJson: r.debugMaterialJson || '',
      triggerKind: r.triggerKind || '',
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
    this.markReportViewed_(report);
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

  private renderReportArticle_(report: DreamReportData) {
    return html`
      <article>
        ${this.renderMarkdown_(report.reportMarkdown)}
        ${this.renderHabits_(report)}
        ${report.debugMaterialJson ? html`
          <details>
            <summary>${t('chat.dream.debug_title')}</summary>
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
            <div class="date">
              ${t('chat.dream.card_date', {date: report.dreamDate})}
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
