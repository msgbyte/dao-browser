// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {callNative, callNativeArgs} from './dream_bridge.js';
import {currentLocale, initI18n, t} from './i18n/i18n.js';
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

interface DreamTheme {
  name: string;
  summary: string;
  intensity: 'light'|'medium'|'deep';
  timeLabel: string;
  attentionShare: number;
}

interface DreamRecap {
  summary: string;
  timeBuckets: {
    morning: number;
    afternoon: number;
    evening: number;
    night: number;
  };
  themes: DreamTheme[];
}

interface DreamMaterialStats {
  historyDomains: number;
  searchQueries: number;
  conversationSessions: number;
  sourceDomains: string[];
  hasStructuredRecap: boolean;
  hasDurationData: boolean;
  recap: DreamRecap;
}

interface ActivityTooltipState {
  dateKey: string;
  text: string;
  left: number;
  top: number;
}

interface DailyDreamReportData {
  reportKind: 'daily';
  id: number;
  dreamDate: string;
  reportMarkdown: string;
  habits: DreamHabit[];
  materialStats: string;
  stats: DreamMaterialStats;
  debugMaterialJson: string;
  triggerKind: string;
  createdAt?: number;
}

interface WeeklyDreamThread {
  title: string;
  statusSummary: string;
  nextStep: string;
  confidence: number;
  sourceRefs: string[];
}

interface WeeklyDreamOutcome {
  text: string;
  confidence: number;
  sourceRefs: string[];
}

interface WeeklyDreamReportData {
  reportKind: 'weekly';
  id: number;
  weekStart: string;
  weekEnd: string;
  headline: string;
  primaryThread: WeeklyDreamThread;
  secondaryThreads: WeeklyDreamThread[];
  retainedOutcomes: WeeklyDreamOutcome[];
  footprintThemes: string[];
  timePattern: string;
  materialStats: string;
  triggerKind: string;
  sourceCount: number;
  createdAt?: number;
}

type DreamReportData = DailyDreamReportData|WeeklyDreamReportData;

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
      weeklyReports_: {type: Array, state: true},
      error_: {type: String, state: true},
      habitStates_: {type: Object, state: true},
      shareStatus_: {type: String, state: true},
      rerunRunning_: {type: Boolean, state: true},
      rerunError_: {type: String, state: true},
      dreamExcludedDomains_: {type: Array, state: true},
      dreamExclusionAdding_: {type: Boolean, state: true},
      dreamExclusionError_: {type: String, state: true},
      activityTooltip_: {type: Object, state: true},
    };
  }

  declare private loading_: boolean;
  declare private report_: DreamReportData|null;
  declare private reports_: DailyDreamReportData[];
  declare private weeklyReports_: WeeklyDreamReportData[];
  declare private error_: string;
  declare private habitStates_: Record<number, HabitState>;
  declare private shareStatus_: ShareStatus;
  declare private rerunRunning_: boolean;
  declare private rerunError_: string;
  declare private dreamExcludedDomains_: string[];
  declare private dreamExclusionAdding_: boolean;
  declare private dreamExclusionError_: string;
  declare private activityTooltip_: ActivityTooltipState|null;

  constructor() {
    super();
    this.loading_ = true;
    this.report_ = null;
    this.reports_ = [];
    this.weeklyReports_ = [];
    this.error_ = '';
    this.habitStates_ = {};
    this.shareStatus_ = 'idle';
    this.rerunRunning_ = false;
    this.rerunError_ = '';
    this.dreamExcludedDomains_ = [];
    this.dreamExclusionAdding_ = false;
    this.dreamExclusionError_ = '';
    this.activityTooltip_ = null;
  }

  static override get styles() {
    return css`
      :host {
        display: block;
        min-height: 100vh;
        color: rgba(var(--dream-ink), 0.88);
        background: var(--dream-bg-soft);
        color-scheme: light;
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
        border-bottom: 1px solid rgba(var(--dream-accent), 0.18);
      }

      .eyebrow {
        color: rgb(var(--dream-accent));
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
        color: rgba(var(--dream-ink), 0.56);
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
        border: 1px solid rgba(var(--dream-accent), 0.24);
        border-radius: 8px;
        background: var(--dream-surface);
        color: rgba(var(--dream-ink), 0.80);
        line-height: 1;
        cursor: pointer;
      }

      .copy-image-button:hover:not(:disabled),
      .rerun-report-button:hover:not(:disabled) {
        background: var(--dream-surface-strong);
        border-color: rgba(var(--dream-accent), 0.42);
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
        color: rgba(var(--dream-ink), 0.58);
        font-size: 14px;
        text-align: center;
      }

      .empty-title {
        display: block;
        color: rgba(var(--dream-ink), 0.82);
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
        border-right: 1px solid rgba(var(--dream-accent), 0.14);
        padding-right: 16px;
      }

      .history-list-title {
        margin: 0 0 10px;
        color: rgba(var(--dream-ink), 0.56);
        font-size: 12px;
        font-weight: 650;
      }

      .history-rerun-error {
        margin: -4px 0 10px;
        color: var(--dream-danger);
        font-size: 12px;
        line-height: 1.4;
      }

      .report-domain-picker {
        border-top: 1px solid var(--dream-border);
        padding-top: 18px;
      }

      .report-domain-summary {
        color: rgba(var(--dream-ink), 0.88);
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
        border: 1px solid rgba(var(--dream-accent), 0.18);
        border-radius: 8px;
        background: var(--dream-surface);
        color: rgba(var(--dream-ink), 0.72);
        font-size: 12px;
      }

      .report-domain-option.excluded {
        background: rgba(var(--dream-accent), 0.08);
        color: rgba(var(--dream-ink), 0.46);
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
        color: rgba(var(--dream-ink), 0.46);
        font-size: 12px;
      }

      .report-domain-empty {
        margin: 12px 0 0;
        color: rgba(var(--dream-ink), 0.52);
        font-size: 12px;
        line-height: 1.45;
      }

      .report-body {
        min-width: 0;
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
        border-top: 1px solid var(--dream-border);
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
        border-top: 1px solid var(--dream-border-soft);
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
        color: rgba(var(--dream-ink), 0.52);
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
        border: 1px solid rgba(var(--dream-accent), 0.24);
        border-radius: 8px;
        background: var(--dream-surface);
        color: rgba(var(--dream-ink), 0.80);
        font: inherit;
        font-size: 12px;
        cursor: pointer;
      }

      button:hover {
        background: var(--dream-surface-strong);
        border-color: rgba(var(--dream-accent), 0.42);
      }

      .state {
        color: rgba(var(--dream-ink), 0.52);
        font-size: 12px;
        line-height: 30px;
      }

      details {
        border-top: 1px solid var(--dream-border);
        padding-top: 18px;
      }

      summary {
        color: rgba(var(--dream-ink), 0.62);
        cursor: pointer;
        font-size: 13px;
      }

      pre {
        max-height: 420px;
        overflow: auto;
        margin: 12px 0 0;
        padding: 12px;
        border-radius: 8px;
        background: rgba(var(--dream-accent), 0.08);
        color: rgba(var(--dream-ink), 0.72);
        font-size: 12px;
        line-height: 1.45;
        white-space: pre-wrap;
        word-break: break-word;
      }

      .debug-meta {
        margin: 12px 0 0;
        color: rgba(var(--dream-ink), 0.58);
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
          border-bottom: 1px solid rgba(var(--dream-accent), 0.14);
          margin-bottom: 22px;
          padding: 0 0 16px;
        }

        .actions {
          justify-content: flex-start;
          margin-top: 10px;
        }
      }

      :host {
        --dream-ink: 30, 20, 40;
        --dream-accent: 70, 120, 190;
        --dream-bg: rgb(238, 243, 248);
        --dream-bg-soft: rgb(249, 251, 253);
        --dream-surface: rgba(255, 255, 255, 0.66);
        --dream-surface-strong: rgba(255, 255, 255, 0.9);
        --dream-border: rgba(var(--dream-accent), 0.16);
        --dream-border-soft: rgba(var(--dream-ink), 0.08);
        --dream-danger: rgb(160, 48, 48);
        --dream-fg: rgba(var(--dream-ink), 0.9);
        --dream-fg-2: rgba(var(--dream-ink), 0.66);
        --dream-fg-3: rgba(var(--dream-ink), 0.48);
        color: var(--dream-fg);
        background:
          radial-gradient(1200px 520px at 78% -10%,
            rgba(var(--dream-accent), 0.1), transparent 60%),
          linear-gradient(180deg, var(--dream-bg) 0%,
            var(--dream-bg-soft) 44%),
          var(--dream-bg-soft);
        background-attachment: fixed;
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI",
          "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", sans-serif;
        -webkit-font-smoothing: antialiased;
      }

      .page {
        width: min(860px, calc(100vw - 56px));
        padding: 30px 0 72px;
      }

      main {
        padding: 0;
      }

      section,
      details {
        border-top: 0;
        padding-top: 0;
      }

      .history-layout {
        grid-template-columns: minmax(196px, 234px) minmax(0, 1fr);
        gap: 34px;
      }

      .history-rail {
        position: sticky;
        top: 24px;
        display: flex;
        min-width: 0;
        flex-direction: column;
        gap: 20px;
        padding-right: 18px;
        border-right: 1px solid rgba(var(--dream-accent), 0.14);
      }

      .activity-heatmap {
        margin: 0;
        padding: 15px 15px 13px;
        overflow: hidden;
        border: 1px solid var(--dream-border-soft);
        border-radius: 14px;
        background: var(--dream-surface);
        backdrop-filter: blur(14px);
      }

      .heatmap-header {
        display: flex;
        flex-direction: column;
        align-items: flex-start;
        gap: 3px;
        margin-bottom: 12px;
      }

      .heatmap-header h2,
      .history-heading h2,
      .block-title h2 {
        margin: 0;
        color: var(--dream-fg-2);
        font-size: 13px;
        font-weight: 620;
        letter-spacing: 0.02em;
      }

      .heatmap-header span,
      .history-heading span,
      .block-title span {
        color: var(--dream-fg-3);
        font-size: 11px;
        font-variant-numeric: tabular-nums;
      }

      .heatmap-scroll {
        overflow-x: auto;
        scrollbar-width: none;
      }

      .heatmap-scroll::-webkit-scrollbar {
        display: none;
      }

      .heatmap-months {
        display: grid;
        grid-template-columns: repeat(53, 11px);
        gap: 3px;
        width: max-content;
        height: 12px;
        color: var(--dream-fg-3);
        font-size: 9px;
      }

      .heatmap-months span {
        white-space: nowrap;
      }

      .heatmap-grid {
        display: grid;
        grid-template-columns: repeat(53, 11px);
        grid-template-rows: repeat(7, 11px);
        grid-auto-flow: column;
        gap: 3px;
        width: max-content;
      }

      .heat-cell {
        width: 11px;
        min-height: 11px;
        height: 11px;
        padding: 0;
        border: 0;
        border-radius: 2.5px;
        background: rgba(var(--dream-ink), 0.07);
      }

      .heat-cell:disabled {
        opacity: 1;
        cursor: default;
      }

      .heat-cell:not(:disabled):hover,
      .heat-cell:not(:disabled):focus-visible {
        outline: 2px solid rgb(var(--dream-accent));
        outline-offset: 1px;
      }

      .activity-tooltip {
        position: fixed;
        z-index: 10;
        max-width: min(240px, calc(100vw - 24px));
        padding: 6px 9px;
        border: 1px solid rgba(var(--dream-accent), 0.22);
        border-radius: 7px;
        background: rgba(30, 20, 40, 0.92);
        box-shadow: 0 6px 18px rgba(var(--dream-ink), 0.16);
        color: white;
        font-size: 11px;
        line-height: 1.35;
        pointer-events: none;
        transform: translate(-50%, calc(-100% - 7px));
        white-space: nowrap;
      }

      .heatmap-legend {
        display: flex;
        align-items: center;
        gap: 4px;
        margin-top: 12px;
        color: var(--dream-fg-3);
        font-size: 10px;
      }

      .heatmap-legend i {
        width: 11px;
        height: 11px;
        border-radius: 2.5px;
        background: rgba(var(--dream-ink), 0.07);
      }

      [data-level="1"] { background: rgba(var(--dream-accent), 0.28); }
      [data-level="2"] { background: rgba(var(--dream-accent), 0.5); }
      [data-level="3"] { background: rgba(var(--dream-accent), 0.74); }
      [data-level="4"] { background: rgb(var(--dream-accent)); }

      .history-list {
        min-width: 0;
        margin: 0;
        padding: 0;
        border: 0;
      }

      .history-heading {
        display: flex;
        align-items: baseline;
        justify-content: space-between;
        margin: 0 0 12px;
        padding: 0 2px;
      }

      .history-heading h2 {
        color: var(--dream-fg-3);
        font-size: 11px;
        font-weight: 650;
        letter-spacing: 0.06em;
        text-transform: uppercase;
      }

      .history-item {
        display: flex;
        align-items: center;
        gap: 9px;
        box-sizing: border-box;
        width: 100%;
        max-width: 100%;
        min-width: 0;
        height: auto;
        min-height: 0;
        margin: 0 0 1px;
        padding: 7px 10px;
        overflow: hidden;
        border: 1px solid transparent;
        border-radius: 10px;
        background: transparent;
        color: inherit;
        text-align: left;
      }

      .history-item:hover {
        border-color: rgba(var(--dream-accent), 0.2);
        background: rgba(var(--dream-accent), 0.1);
      }

      .history-item[data-selected="true"] {
        border-color: rgba(var(--dream-accent), 0.28);
        background: rgba(var(--dream-accent), 0.14);
      }

      .history-item:focus-visible {
        outline: 2px solid rgb(var(--dream-accent));
        outline-offset: -2px;
      }

      .history-dot {
        flex: 0 0 9px;
        width: 9px;
        height: 9px;
        border-radius: 50%;
      }

      .history-copy {
        flex: 1 1 auto;
        min-width: 0;
      }

      .history-kind-badge {
        flex: 0 0 auto;
        padding: 2px 6px;
        border-radius: 999px;
        background: rgba(var(--dream-accent), 0.12);
        color: rgb(var(--dream-accent));
        font-size: 9px;
        font-weight: 650;
        letter-spacing: 0.04em;
        text-transform: uppercase;
      }

      .history-date,
      .history-kind {
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
      }

      .history-date {
        font-size: 13px;
        font-weight: 550;
      }

      .history-item[data-selected="true"] .history-date {
        color: rgb(var(--dream-accent));
        font-weight: 620;
      }

      .history-kind {
        margin: 0;
        color: var(--dream-fg-3);
        font-size: 11px;
      }

      .recap-article {
        display: block;
        min-width: 0;
      }

      .recap-header {
        align-items: baseline;
        margin: 0 0 22px;
        padding: 0;
        border: 0;
      }

      .recap-header .eyebrow {
        display: inline-flex;
        align-items: center;
        gap: 7px;
        margin: 0;
        color: rgb(var(--dream-accent));
        font-size: 12px;
        font-weight: 600;
        letter-spacing: 0.06em;
        text-transform: uppercase;
      }

      .recap-header .eyebrow svg {
        width: 15px;
        height: 15px;
      }

      .recap-header h1 {
        margin: 8px 0 0;
        font-size: 26px;
        line-height: 1.15;
        font-weight: 640;
        letter-spacing: -0.01em;
      }

      .header-actions {
        flex-wrap: nowrap;
      }

      .date {
        color: var(--dream-fg-3);
        font-size: 13px;
      }

      .copy-image-button,
      .rerun-report-button,
      .habit-action {
        background: var(--dream-surface-strong);
        color: var(--dream-fg-2);
      }

      .recap-summary {
        margin: 0 0 26px;
        padding: 22px 26px;
        border: 1px solid var(--dream-border);
        border-radius: 14px;
        background: var(--dream-surface);
        box-shadow: 0 10px 30px rgba(var(--dream-ink), 0.05);
        backdrop-filter: blur(14px);
      }

      .recap-summary > div {
        margin-bottom: 10px;
        color: var(--dream-fg-3);
        font-size: 11px;
        font-weight: 600;
        letter-spacing: 0.08em;
        text-transform: uppercase;
      }

      .recap-summary p {
        margin: 0;
        font-size: 21px;
        line-height: 1.45;
        font-weight: 500;
        letter-spacing: -0.005em;
      }

      .recap-block,
      .memory-section {
        margin: 0 0 26px;
      }

      .block-title {
        display: flex;
        align-items: baseline;
        justify-content: space-between;
        margin: 0 0 12px;
      }

      .rhythm-grid {
        display: grid;
        grid-template-columns: repeat(4, 1fr);
        gap: 10px;
      }

      .rhythm-slot {
        position: relative;
        display: flex;
        min-height: 96px;
        flex-direction: column;
        justify-content: flex-end;
        padding: 12px 12px 14px;
        overflow: hidden;
        border: 1px solid var(--dream-border-soft);
        border-radius: 10px;
        background: var(--dream-surface);
      }

      .rhythm-slot[data-peak="true"] {
        border-color: var(--dream-border);
      }

      .rhythm-fill {
        position: absolute;
        right: 0;
        bottom: 0;
        left: 0;
        background: linear-gradient(180deg,
          rgba(var(--dream-accent), 0.16),
          rgba(var(--dream-accent), 0.28));
      }

      .rhythm-meta {
        position: relative;
        display: flex;
        flex-direction: column;
      }

      .rhythm-meta strong {
        font-size: 12px;
        font-weight: 600;
      }

      .rhythm-slot[data-peak="true"] strong {
        color: rgb(var(--dream-accent));
      }

      .rhythm-meta span {
        margin-top: 2px;
        color: var(--dream-fg-3);
        font-size: 12px;
        font-variant-numeric: tabular-nums;
      }

      .theme-grid {
        display: grid;
        grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
        gap: 12px;
      }

      .theme-card {
        display: flex;
        flex-direction: column;
        gap: 10px;
        padding: 16px 16px 14px;
        border: 1px solid var(--dream-border-soft);
        border-radius: 14px;
        background: var(--dream-surface);
        transition: border-color 140ms ease, transform 140ms ease,
          box-shadow 140ms ease;
      }

      .theme-card:hover {
        border-color: var(--dream-border);
        box-shadow: 0 12px 26px rgba(var(--dream-ink), 0.06);
        transform: translateY(-2px);
      }

      .theme-header {
        display: flex;
        align-items: center;
        gap: 10px;
      }

      .theme-icon {
        display: grid;
        width: 34px;
        height: 34px;
        flex: 0 0 34px;
        place-items: center;
        border-radius: 9px;
        background: rgba(var(--dream-accent), 0.1);
        color: rgb(var(--dream-accent));
      }

      .theme-icon svg {
        width: 18px;
        height: 18px;
      }

      .theme-header strong {
        font-size: 15px;
        font-weight: 600;
      }

      .theme-card p {
        margin: 0;
        color: var(--dream-fg-2);
        font-size: 13px;
        line-height: 1.5;
      }

      .weekly-next-step {
        padding-top: 10px;
        border-top: 1px solid var(--dream-border-soft);
      }

      .weekly-next-step span {
        color: rgb(var(--dream-accent));
        font-size: 10px;
        font-weight: 650;
        letter-spacing: 0.06em;
        text-transform: uppercase;
      }

      .weekly-next-step p {
        margin-top: 5px;
        color: var(--dream-fg);
      }

      .weekly-footprint p {
        margin: 0;
        color: var(--dream-fg-2);
        font-size: 14px;
        line-height: 1.6;
      }

      .theme-footer {
        display: flex;
        align-items: center;
        gap: 8px;
        flex-wrap: wrap;
      }

      .theme-chip {
        padding: 3px 9px;
        border-radius: 999px;
        background: rgba(var(--dream-ink), 0.05);
        color: var(--dream-fg-2);
        font-size: 11px;
        font-weight: 550;
        white-space: nowrap;
      }

      .theme-chip.intensity {
        background: rgba(var(--dream-accent), 0.1);
        color: rgb(var(--dream-accent));
      }

      .theme-bar {
        height: 6px;
        flex: 1 1 70px;
        overflow: hidden;
        border-radius: 999px;
        background: rgba(var(--dream-ink), 0.08);
      }

      .theme-bar i {
        display: block;
        height: 100%;
        border-radius: inherit;
        background: rgb(var(--dream-accent));
      }

      .recap-stats {
        display: grid;
        grid-template-columns: repeat(3, 1fr);
        gap: 12px;
        margin: 0 0 26px;
      }

      .stat-card {
        padding: 16px 18px;
        border: 1px solid var(--dream-border-soft);
        border-radius: 14px;
        background: var(--dream-surface);
      }

      .stat-card strong {
        display: block;
        font-size: 30px;
        line-height: 1;
        font-weight: 640;
        font-variant-numeric: tabular-nums;
        letter-spacing: -0.02em;
      }

      .stat-card span {
        display: block;
        margin-top: 7px;
        color: var(--dream-fg-3);
        font-size: 12px;
      }

      .memory-candidates {
        overflow: hidden;
        border: 1px solid var(--dream-border-soft);
        border-radius: 14px;
        background: var(--dream-surface);
      }

      .habit {
        padding: 14px 16px;
        border: 0;
        border-top: 1px solid var(--dream-border-soft);
      }

      .habit:first-child {
        border-top: 0;
      }

      .habit-relation {
        display: inline-block;
        margin-bottom: 6px;
        padding: 2px 7px;
        border-radius: 5px;
        font-size: 10px;
        font-weight: 600;
        letter-spacing: 0.06em;
        text-transform: uppercase;
      }

      .habit-relation[data-relation="new"] {
        background: rgba(var(--dream-accent), 0.1);
        color: rgb(var(--dream-accent));
      }

      .habit-relation[data-relation="reinforce"] {
        background: rgba(var(--dream-ink), 0.06);
        color: var(--dream-fg-2);
      }

      .habit-relation[data-relation="contradict"] {
        background: rgba(160, 82, 45, 0.12);
        color: rgb(160, 82, 45);
      }

      .habit-action {
        display: grid;
        width: 30px;
        height: 30px;
        padding: 0;
        place-items: center;
      }

      .habit-action svg {
        width: 15px;
        height: 15px;
      }

      .full-report {
        margin-top: 26px;
        padding-top: 16px;
        border-top: 1px solid var(--dream-border-soft);
      }

      .full-report summary {
        display: inline-flex;
        align-items: center;
        gap: 8px;
        color: var(--dream-fg-2);
        font-size: 13px;
        font-weight: 550;
        list-style: none;
      }

      .full-report summary::-webkit-details-marker {
        display: none;
      }

      .full-report summary svg {
        width: 14px;
        height: 14px;
        transition: transform 180ms ease;
      }

      .full-report[open] summary svg {
        transform: rotate(90deg);
      }

      .report-body {
        max-width: 66ch;
        margin-top: 14px;
        color: var(--dream-fg-2);
      }

      .markdown {
        font-size: 14px;
        line-height: 1.72;
      }

      .report-utilities {
        margin-top: 24px;
      }

      .report-domain-picker,
      .debug-details {
        margin-top: 14px;
        padding-top: 16px;
        border-top: 1px solid var(--dream-border-soft);
      }

      @media (prefers-color-scheme: dark) {
        :host {
          --dream-ink: 245, 245, 245;
          --dream-bg: rgb(40, 44, 49);
          --dream-bg-soft: rgb(46, 50, 56);
          --dream-surface: rgba(255, 255, 255, 0.06);
          --dream-surface-strong: rgba(255, 255, 255, 0.1);
          --dream-border: rgba(var(--dream-accent), 0.34);
          --dream-border-soft: rgba(255, 255, 255, 0.09);
          --dream-danger: rgb(255, 150, 150);
          --dream-fg: rgba(var(--dream-ink), 0.92);
          --dream-fg-2: rgba(var(--dream-ink), 0.66);
          --dream-fg-3: rgba(var(--dream-ink), 0.46);
          color-scheme: dark;
        }

        .habit-relation[data-relation="contradict"] {
          background: rgba(224, 152, 108, 0.16);
          color: rgb(224, 152, 108);
        }
      }

      @media (max-width: 760px) {
        .page {
          width: calc(100vw - 40px);
        }

        .history-layout {
          grid-template-columns: 1fr;
          gap: 20px;
        }

        .history-rail {
          position: static;
          gap: 16px;
          padding: 0;
          border-right: 0;
        }

        .history-list {
          padding-bottom: 16px;
          border-bottom: 1px solid rgba(var(--dream-accent), 0.14);
        }

        .history-items {
          display: flex;
          gap: 8px;
          overflow-x: auto;
          padding-bottom: 2px;
          scrollbar-width: none;
        }

        .history-items::-webkit-scrollbar {
          display: none;
        }

        .history-items > button {
          width: 132px;
          flex: 0 0 132px;
          margin: 0;
          border-color: var(--dream-border-soft);
        }
      }

      @media (max-width: 640px) {
        .page {
          width: calc(100vw - 28px);
          padding-top: 22px;
        }

        .recap-header {
          display: flex;
          align-items: flex-start;
          flex-direction: column;
          gap: 10px;
        }

        .recap-header h1 {
          font-size: 24px;
        }

        .header-actions {
          justify-content: flex-start;
        }

        .date {
          margin: 0;
        }

        .recap-summary {
          padding: 19px 20px;
        }

        .recap-summary p {
          font-size: 18px;
        }

        .rhythm-grid {
          gap: 7px;
        }

        .rhythm-slot {
          min-height: 88px;
          padding: 10px 8px 12px;
        }

        .recap-stats {
          gap: 8px;
        }

        .stat-card {
          padding: 14px 12px;
        }

        .stat-card strong {
          font-size: 26px;
        }
      }

      @media (prefers-reduced-motion: reduce) {
        .theme-card,
        .full-report summary svg {
          transition: none;
        }
      }
    `;
  }

  override connectedCallback() {
    super.connectedCallback();
    void this.loadPage_();
  }

  override updated(changedProperties: Map<PropertyKey, unknown>) {
    if (changedProperties.has('loading_') && !this.loading_) {
      this.scrollHeatmapToLatest_();
    }
  }

  private scrollHeatmapToLatest_() {
    const heatmap =
        this.shadowRoot?.querySelector<HTMLElement>('.heatmap-scroll');
    if (!heatmap) {
      return;
    }
    heatmap.scrollLeft = Math.max(0, heatmap.scrollWidth - heatmap.clientWidth);
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
      this.weeklyReports_ = [];
      this.habitStates_ = report ? this.loadPersistedHabitStates_(report) : {};
      if (report) {
        this.markReportViewed_(report);
      }
    } catch (e) {
      this.error_ = e instanceof Error ? e.message : String(e);
      this.report_ = null;
      this.reports_ = [];
      this.weeklyReports_ = [];
    }
  }

  private async loadHistory_() {
    try {
      const [raw, rawWeekly] = await Promise.all([
        callNative('getDreamReports', {limit: 371}),
        callNative('getWeeklyDreamReports', {limit: 53}),
      ]);
      const reports = this.normalizeReports_(raw);
      const weeklyReports = this.normalizeWeeklyReports_(rawWeekly);
      this.reports_ = reports;
      this.weeklyReports_ = weeklyReports;
      this.report_ = this.historyReports_()[0] || null;
      this.habitStates_ = this.report_?.reportKind === 'daily' ?
          this.loadPersistedHabitStates_(this.report_) : {};
      if (this.report_) {
        this.markReportViewed_(this.report_);
      }
    } catch (e) {
      this.error_ = e instanceof Error ? e.message : String(e);
      this.report_ = null;
      this.reports_ = [];
      this.weeklyReports_ = [];
    }
  }

  private normalizeReport_(raw: unknown): DailyDreamReportData|null {
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
      reportKind: 'daily',
      id: r.id,
      dreamDate: r.dreamDate || '',
      reportMarkdown: r.reportMarkdown || '',
      habits,
      materialStats: r.materialStats || '',
      stats: this.normalizeMaterialStats_(
          r.materialStats || '', r.reportMarkdown || ''),
      debugMaterialJson: r.debugMaterialJson || '',
      triggerKind: r.triggerKind || '',
      createdAt:
          typeof r.createdAt === 'number' && Number.isFinite(r.createdAt) ?
          r.createdAt : undefined,
    };
  }

  private boundedNumber_(value: unknown, max: number): number {
    return typeof value === 'number' && Number.isFinite(value) ?
        Math.min(Math.max(Math.round(value), 0), max) : 0;
  }

  private markdownSections_(markdown: string):
      Array<{title: string; body: string}> {
    const sections: Array<{title: string; body: string}> = [];
    let title = '';
    let body: string[] = [];
    const flush = () => {
      const text = body.join(' ').replace(/\s+/g, ' ').trim();
      if (text) {
        sections.push({title, body: text});
      }
      body = [];
    };
    for (const line of markdown.split(/\r?\n/)) {
      const heading = line.match(/^#{1,6}\s+(.+)$/);
      if (heading) {
        flush();
        title = heading[1]!.trim();
        continue;
      }
      const clean = line
                        .replace(/^\s*[-*+]\s+/, '')
                        .replace(/\[([^\]]+)\]\([^\)]+\)/g, '$1')
                        .replace(/[*_`>#]/g, '')
                        .trim();
      if (clean) {
        body.push(clean);
      }
    }
    flush();
    return sections;
  }

  private normalizeMaterialStats_(
      raw: string, reportMarkdown: string): DreamMaterialStats {
    let parsed: Record<string, unknown> = {};
    try {
      const candidate = JSON.parse(raw || '{}');
      if (candidate && typeof candidate === 'object' &&
          !Array.isArray(candidate)) {
        parsed = candidate as Record<string, unknown>;
      }
    } catch {}
    const recap = parsed['recap'] && typeof parsed['recap'] === 'object' ?
        parsed['recap'] as Record<string, unknown> : {};
    const rawBuckets =
        recap['time_buckets'] && typeof recap['time_buckets'] === 'object' ?
        recap['time_buckets'] as Record<string, unknown> : {};
    const measuredBuckets =
        parsed['foreground_seconds_by_bucket'] &&
            typeof parsed['foreground_seconds_by_bucket'] === 'object' ?
        parsed['foreground_seconds_by_bucket'] as Record<string, unknown> :
        null;
    const hasMeasuredBuckets = measuredBuckets !== null &&
        ['morning', 'afternoon', 'evening', 'night'].some(
            key => typeof measuredBuckets[key] === 'number');
    const hasRecapBuckets = [
      'morning_minutes',
      'afternoon_minutes',
      'evening_minutes',
      'night_minutes',
    ].some(key => typeof rawBuckets[key] === 'number');
    const bucketMinutes = (name: string, recapName: string) => {
      if (hasMeasuredBuckets) {
        return this.boundedNumber_(
            (measuredBuckets![name] as number) / 60, 1440);
      }
      return this.boundedNumber_(rawBuckets[recapName], 1440);
    };
    const sections = this.markdownSections_(reportMarkdown);
    const themes: DreamTheme[] = [];
    if (Array.isArray(recap['themes'])) {
      for (const rawTheme of recap['themes']) {
        if (themes.length >= 3 || !rawTheme ||
            typeof rawTheme !== 'object') {
          continue;
        }
        const candidate = rawTheme as Record<string, unknown>;
        if (typeof candidate['name'] !== 'string' ||
            typeof candidate['summary'] !== 'string' ||
            !candidate['name'].trim() || !candidate['summary'].trim()) {
          continue;
        }
        const intensity = candidate['intensity'];
        themes.push({
          name: candidate['name'].trim(),
          summary: candidate['summary'].trim(),
          intensity: intensity === 'light' || intensity === 'deep' ?
              intensity : 'medium',
          timeLabel: typeof candidate['time_label'] === 'string' ?
              candidate['time_label'].trim() : '',
          attentionShare:
              this.boundedNumber_(candidate['attention_share'], 100),
        });
      }
    }
    const recapSummary = typeof recap['summary'] === 'string' ?
        recap['summary'].trim() : '';
    const hasStructuredRecap = Boolean(recapSummary || themes.length > 0);
    if (themes.length === 0) {
      sections.slice(0, 3).forEach((section, index) => themes.push({
        name: section.title || t('dream.page.theme_fallback'),
        summary: section.body,
        intensity: index === 0 ? 'deep' : 'medium',
        timeLabel: '',
        attentionShare: Math.max(36, 100 - index * 28),
      }));
    }
    const sourceDomains = Array.isArray(parsed['source_domains']) ?
        parsed['source_domains'].filter(
            (item): item is string => typeof item === 'string') : [];
    return {
      historyDomains: this.boundedNumber_(parsed['history_domains'], 10000),
      searchQueries: this.boundedNumber_(parsed['search_queries'], 10000),
      conversationSessions:
          this.boundedNumber_(parsed['conversation_sessions'], 10000),
      sourceDomains,
      hasStructuredRecap,
      hasDurationData: hasMeasuredBuckets || hasRecapBuckets,
      recap: {
        summary: recapSummary || sections[0]?.body || reportMarkdown.trim(),
        timeBuckets: {
          morning: bucketMinutes('morning', 'morning_minutes'),
          afternoon: bucketMinutes('afternoon', 'afternoon_minutes'),
          evening: bucketMinutes('evening', 'evening_minutes'),
          night: bucketMinutes('night', 'night_minutes'),
        },
        themes,
      },
    };
  }

  private normalizeReports_(raw: unknown): DailyDreamReportData[] {
    if (!Array.isArray(raw)) {
      return [];
    }
    return raw
        .map(item => this.normalizeReport_(item))
        .filter((item): item is DailyDreamReportData => item !== null);
  }

  private normalizeWeeklyThread_(raw: unknown): WeeklyDreamThread|null {
    if (!raw || typeof raw !== 'object') {
      return null;
    }
    const thread = raw as Record<string, unknown>;
    if (typeof thread['title'] !== 'string' ||
        typeof thread['status_summary'] !== 'string' ||
        typeof thread['next_step'] !== 'string') {
      return null;
    }
    return {
      title: thread['title'].trim(),
      statusSummary: thread['status_summary'].trim(),
      nextStep: thread['next_step'].trim(),
      confidence: typeof thread['confidence'] === 'number' &&
              Number.isFinite(thread['confidence']) ?
          Math.min(Math.max(thread['confidence'], 0), 1) : 0,
      sourceRefs: Array.isArray(thread['source_refs']) ?
          thread['source_refs'].filter(
              (ref): ref is string => typeof ref === 'string') : [],
    };
  }

  private normalizeWeeklyReport_(raw: unknown): WeeklyDreamReportData|null {
    if (!raw || typeof raw !== 'object') {
      return null;
    }
    const report = raw as Record<string, unknown>;
    const content = report['content'];
    if (report['reportKind'] !== 'weekly' || typeof report['id'] !== 'number' ||
        typeof report['weekStart'] !== 'string' ||
        typeof report['weekEnd'] !== 'string' || !content ||
        typeof content !== 'object') {
      return null;
    }
    const value = content as Record<string, unknown>;
    const primaryThread = this.normalizeWeeklyThread_(value['primary_thread']);
    if (typeof value['headline'] !== 'string' || !primaryThread) {
      return null;
    }
    const secondaryThreads = Array.isArray(value['secondary_threads']) ?
        value['secondary_threads']
            .map(thread => this.normalizeWeeklyThread_(thread))
            .filter((thread): thread is WeeklyDreamThread => thread !== null) :
        [];
    const retainedOutcomes: WeeklyDreamOutcome[] = [];
    if (Array.isArray(value['retained_outcomes'])) {
      for (const rawOutcome of value['retained_outcomes']) {
        if (!rawOutcome || typeof rawOutcome !== 'object') {
          continue;
        }
        const outcome = rawOutcome as Record<string, unknown>;
        if (typeof outcome['text'] !== 'string') {
          continue;
        }
        retainedOutcomes.push({
          text: outcome['text'].trim(),
          confidence: typeof outcome['confidence'] === 'number' &&
                  Number.isFinite(outcome['confidence']) ?
              Math.min(Math.max(outcome['confidence'], 0), 1) : 0,
          sourceRefs: Array.isArray(outcome['source_refs']) ?
              outcome['source_refs'].filter(
                  (ref): ref is string => typeof ref === 'string') : [],
        });
      }
    }
    const footprint = value['footprint_summary'] &&
            typeof value['footprint_summary'] === 'object' ?
        value['footprint_summary'] as Record<string, unknown> : {};
    return {
      reportKind: 'weekly',
      id: report['id'],
      weekStart: report['weekStart'],
      weekEnd: report['weekEnd'],
      headline: value['headline'].trim(),
      primaryThread,
      secondaryThreads,
      retainedOutcomes,
      footprintThemes: Array.isArray(footprint['themes']) ?
          footprint['themes'].filter(
              (theme): theme is string => typeof theme === 'string') : [],
      timePattern: typeof footprint['time_pattern'] === 'string' ?
          footprint['time_pattern'].trim() : '',
      materialStats: typeof report['materialStats'] === 'string' ?
          report['materialStats'] : '',
      triggerKind: typeof report['triggerKind'] === 'string' ?
          report['triggerKind'] : '',
      sourceCount: this.boundedNumber_(report['sourceCount'], 10000),
      createdAt: typeof report['createdAt'] === 'number' &&
              Number.isFinite(report['createdAt']) ?
          report['createdAt'] : undefined,
    };
  }

  private normalizeWeeklyReports_(raw: unknown): WeeklyDreamReportData[] {
    if (!Array.isArray(raw)) {
      return [];
    }
    return raw
        .map(item => this.normalizeWeeklyReport_(item))
        .filter((item): item is WeeklyDreamReportData => item !== null);
  }

  private reportDate_(report: DreamReportData): string {
    return report.reportKind === 'daily' ? report.dreamDate : report.weekStart;
  }

  private historyReports_(): DreamReportData[] {
    return [...this.reports_, ...this.weeklyReports_].sort(
        (a, b) => this.reportDate_(b).localeCompare(this.reportDate_(a)) ||
            (a.reportKind === 'daily' ? -1 : 1));
  }

  private markReportViewed_(report: DreamReportData) {
    if (report.reportKind === 'weekly') {
      callNative('markWeeklyDreamReportViewed', {reportId: report.id})
          .catch(() => {});
      return;
    }
    callNativeArgs('markDreamReportViewed', report.id).catch(() => {});
  }

  private selectHistoryReport_(report: DreamReportData) {
    this.report_ = report;
    this.habitStates_ = report.reportKind === 'daily' ?
        this.loadPersistedHabitStates_(report) : {};
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

  private sourceDomainsForReport_(report: DailyDreamReportData): string[] {
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
    this.persistHabitState_(habit, 'rejected');
    this.habitStates_ = {...this.habitStates_, [index]: 'rejected'};
  }

  private loadPersistedHabitStates_(report: DailyDreamReportData):
      Record<number, HabitState> {
    const stored = this.readHabitFeedbackStore_();
    const states: Record<number, HabitState> = {};
    report.habits.forEach((habit, index) => {
      if (habit.relation === 'reinforce') {
        return;
      }
          const state = stored[this.habitFeedbackKey_(report, habit)];
          if (state === 'confirmed' || state === 'rejected') {
            states[index] = state;
          }
    });
    return states;
  }

  private persistHabitState_(habit: DreamHabit, state: HabitState) {
    if (!this.report_ || this.report_.reportKind !== 'daily') {
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

  private habitFeedbackKey_(report: DailyDreamReportData, habit: DreamHabit) {
    return `${report.dreamDate}\u0000${habit.key}\u0000${habit.value}`;
  }

  private renderMarkdown_(markdown: string) {
    const container = document.createElement('div');
    container.className = 'markdown';
    container.innerHTML = renderDaoMarkdown(markdown);
    return html`${container}`;
  }

  private renderHabits_(report: DailyDreamReportData) {
    const rows = report.habits;
    if (rows.length === 0) {
      return nothing;
    }
    return html`
      <section class="memory-section">
        <div class="block-title">
          <h2>${t('dream.page.memory_title')}</h2>
          <span>${t('dream.page.memory_hint')}</span>
        </div>
        <div class="memory-candidates">
        ${rows.map((habit, index) => html`
          <div class="habit">
            <div class="habit-body">
              <span class="habit-relation" data-relation=${habit.relation}>
                ${habit.relation === 'new' ? t('dream.page.memory_new') :
                  habit.relation === 'reinforce' ?
                    t('dream.page.memory_reinforce') :
                    t('dream.page.memory_contradict')}
              </span>
              <div class="habit-value">
                ${habit.relation === 'contradict' ?
                    t('chat.dream.contradict_prefix') + ' ' : ''}${habit.value}
              </div>
              ${habit.evidence ? html`
                <div class="habit-evidence">${habit.evidence}</div>` :
                nothing}
            </div>
            <div class="actions">
              ${habit.relation === 'reinforce' ? html`
                <span class="state">
                  ${t('dream.page.memory_existing')}</span>` :
                this.habitStates_[index] === 'confirmed' ?
                html`<span class="state">
                  ${t('chat.dream.habit_confirmed')}</span>` :
                this.habitStates_[index] === 'rejected' ?
                html`<span class="state">
                  ${t('chat.dream.habit_rejected')}</span>` :
                html`
                  <button class="habit-action confirm"
                      title=${t('chat.dream.habit_confirm')}
                      aria-label=${t('chat.dream.habit_confirm')}
                      @click=${() => this.confirmHabit_(habit, index)}>
                    <svg viewBox="0 0 24 24" fill="none"
                        stroke="currentColor" stroke-width="2"
                        stroke-linecap="round" stroke-linejoin="round"
                        aria-hidden="true">
                      <path d="M20 6 9 17l-5-5"></path>
                    </svg>
                  </button>
                  <button class="habit-action reject"
                      title=${t('chat.dream.habit_reject')}
                      aria-label=${t('chat.dream.habit_reject')}
                      @click=${() => this.rejectHabit_(habit, index)}>
                    <svg viewBox="0 0 24 24" fill="none"
                        stroke="currentColor" stroke-width="2"
                        stroke-linecap="round" stroke-linejoin="round"
                        aria-hidden="true">
                      <path d="M18 6 6 18"></path>
                      <path d="m6 6 12 12"></path>
                    </svg>
                  </button>`}
            </div>
          </div>`)}
        </div>
      </section>`;
  }

  private parseDreamDate_(value: string): Date|null {
    const match = value.match(/^(\d{4})-(\d{2})-(\d{2})$/);
    if (!match) {
      return null;
    }
    const date = new Date(
        Number(match[1]), Number(match[2]) - 1, Number(match[3]));
    return Number.isFinite(date.getTime()) ? date : null;
  }

  private formatDreamDate_(value: string): string {
    const date = this.parseDreamDate_(value);
    return date ? new Intl.DateTimeFormat(currentLocale(), {
      month: 'short',
      day: 'numeric',
      weekday: 'short',
    }).format(date) : value;
  }

  private reportActivityLevel_(report: DailyDreamReportData): number {
    const buckets = report.stats.recap.timeBuckets;
    const focusedMinutes =
        buckets.morning + buckets.afternoon + buckets.evening + buckets.night;
    if (focusedMinutes > 0) {
      return focusedMinutes >= 180 ? 4 : focusedMinutes >= 90 ? 3 :
          focusedMinutes >= 30 ? 2 : 1;
    }
    const signals = report.stats.historyDomains + report.stats.searchQueries +
        report.stats.conversationSessions;
    return signals >= 16 ? 4 : signals >= 9 ? 3 : signals >= 3 ? 2 : 1;
  }

  private showActivityTooltip_(event: Event, report: DailyDreamReportData) {
    const cell = event.currentTarget as HTMLElement;
    const rect = cell.getBoundingClientRect();
    const buckets = report.stats.recap.timeBuckets;
    const duration = report.stats.hasDurationData ?
        this.formatDuration_(
            buckets.morning + buckets.afternoon + buckets.evening +
            buckets.night) :
        t('dream.page.activity_duration_unavailable');
    this.activityTooltip_ = {
      dateKey: report.dreamDate,
      text: t('dream.page.activity_tooltip', {
        date: this.formatDreamDate_(report.dreamDate),
        duration,
      }),
      left: rect.left + rect.width / 2,
      top: rect.top,
    };
  }

  private hideActivityTooltip_() {
    this.activityTooltip_ = null;
  }

  private renderActivityHeatmapCell_(
      dateKey: string, label: string, report: DailyDreamReportData|null,
      level: number, column: number, row: number) {
    return html`
      <button class="heat-cell" data-level=${String(level)}
          style=${`grid-column:${column};grid-row:${row}`}
          role="gridcell"
          aria-label="${label}"
          aria-describedby="${
            this.activityTooltip_?.dateKey === dateKey ?
              'dream-activity-tooltip' : nothing}"
          @pointerenter=${report ?
            (event: Event) => this.showActivityTooltip_(event, report) :
            undefined}
          @pointerleave=${report ? () => this.hideActivityTooltip_() :
            undefined}
          @focus=${report ?
            (event: Event) => this.showActivityTooltip_(event, report) :
            undefined}
          @blur=${report ? () => this.hideActivityTooltip_() : undefined}
          @click=${report ? () => this.selectHistoryReport_(report) :
            undefined}
          ?disabled=${!report}>
      </button>`;
  }

  private renderActivityTooltip_() {
    return this.activityTooltip_ ? html`
      <div id="dream-activity-tooltip" class="activity-tooltip"
          role="tooltip"
          style=${`left:${this.activityTooltip_.left}px;` +
            `top:${this.activityTooltip_.top}px`}>
        ${this.activityTooltip_.text}
      </div>` : nothing;
  }

  private renderActivityHeatmap_() {
    const reportsByDate = new Map(
        this.reports_.map(report => [report.dreamDate, report]));
    const latestDate = this.parseDreamDate_(this.reports_[0]?.dreamDate || '') ||
        new Date();
    const endDay = new Date(
        latestDate.getFullYear(), latestDate.getMonth(), latestDate.getDate());
    const mondayOffset = (endDay.getDay() + 6) % 7;
    const startDay = new Date(endDay);
    startDay.setDate(endDay.getDate() - mondayOffset - 52 * 7);
    const cells = [];
    const monthLabels: Array<{label: string; column: number}> = [];
    let previousMonth = -1;
    for (let index = 0; index < 53 * 7; index++) {
      const date = new Date(startDay);
      date.setDate(startDay.getDate() + index);
      if (date > endDay) {
        break;
      }
      const dateKey = [
        date.getFullYear(),
        String(date.getMonth() + 1).padStart(2, '0'),
        String(date.getDate()).padStart(2, '0'),
      ].join('-');
      const report = reportsByDate.get(dateKey);
      const level = report ? this.reportActivityLevel_(report) : 0;
      const column = Math.floor(index / 7) + 1;
      if (date.getMonth() !== previousMonth && date.getDate() <= 7) {
        monthLabels.push({
          label: new Intl.DateTimeFormat(currentLocale(), {month: 'short'})
                     .format(date),
          column,
        });
        previousMonth = date.getMonth();
      }
      const label = this.formatDreamDate_(dateKey);
      cells.push(this.renderActivityHeatmapCell_(
          dateKey, label, report || null, level, column, index % 7 + 1));
    }
    return html`
      <section class="activity-heatmap">
        <div class="heatmap-header">
          <h2>${t('dream.page.activity_title')}</h2>
          <span>${t('dream.page.activity_reports', {
            count: this.reports_.length,
          })}</span>
        </div>
        <div class="heatmap-scroll"
            @scroll=${() => this.hideActivityTooltip_()}>
          <div class="heatmap-months">
            ${monthLabels.map(month => html`
              <span style=${`grid-column:${month.column}`}>${month.label}</span>`)}
          </div>
          <div class="heatmap-grid" role="grid"
              aria-label=${t('dream.page.activity_label')}>
            ${cells}
          </div>
        </div>
        <div class="heatmap-legend">
          <span>${t('dream.page.activity_less')}</span>
          ${[0, 1, 2, 3, 4].map(level => html`
            <i data-level=${String(level)}></i>`)}
          <span>${t('dream.page.activity_more')}</span>
        </div>
      </section>
      ${this.renderActivityTooltip_()}`;
  }

  private renderThemeIcon_(index: number) {
    if (index % 3 === 1) {
      return html`
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
            stroke-width="1.7" stroke-linecap="round"
            stroke-linejoin="round" aria-hidden="true">
          <rect x="3" y="3" width="18" height="18" rx="2"></rect>
          <path d="M3 9h18M9 21V9"></path>
        </svg>`;
    }
    if (index % 3 === 2) {
      return html`
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
            stroke-width="1.7" stroke-linecap="round"
            stroke-linejoin="round" aria-hidden="true">
          <circle cx="11" cy="11" r="7"></circle>
          <path d="m21 21-4.3-4.3"></path>
        </svg>`;
    }
    return html`
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
          stroke-width="1.7" stroke-linecap="round"
          stroke-linejoin="round" aria-hidden="true">
        <path d="m18 16 4-4-4-4"></path>
        <path d="m6 8-4 4 4 4"></path>
        <path d="m14.5 4-5 16"></path>
      </svg>`;
  }

  private formatDuration_(minutes: number) {
    const roundedMinutes = Math.max(0, Math.round(minutes));
    if (roundedMinutes < 60) {
      return t('dream.page.minutes', {count: roundedMinutes});
    }
    const hours = Math.floor(roundedMinutes / 60);
    const remainingMinutes = roundedMinutes % 60;
    const formatUnit = (value: number, unit: 'hour'|'minute') =>
      new Intl.NumberFormat(currentLocale(), {
        style: 'unit',
        unit,
        unitDisplay: 'short',
      }).format(value);
    const formattedHours = formatUnit(hours, 'hour');
    if (remainingMinutes === 0) {
      return formattedHours;
    }
    return `${formattedHours} ${formatUnit(remainingMinutes, 'minute')}`;
  }

  private renderRhythm_(report: DailyDreamReportData) {
    const buckets = report.stats.recap.timeBuckets;
    const slots = [
      [t('dream.page.rhythm_morning'), buckets.morning],
      [t('dream.page.rhythm_afternoon'), buckets.afternoon],
      [t('dream.page.rhythm_evening'), buckets.evening],
      [t('dream.page.rhythm_night'), buckets.night],
    ] as Array<[string, number]>;
    const peak = Math.max(...slots.map(([, value]) => value), 1);
    const peakIndex = slots.reduce(
        (best, slot, index) => slot[1] > slots[best]![1] ? index : best, 0);
    if (slots.every(([, value]) => value === 0)) {
      return nothing;
    }
    return html`
      <section class="recap-block rhythm-block">
        <div class="block-title">
          <h2>${t('dream.page.rhythm_title')}</h2>
          <span>${t('dream.page.rhythm_hint')}</span>
        </div>
        <div class="rhythm-grid">
          ${slots.map(([label, value], index) => html`
            <div class="rhythm-slot"
                data-peak=${index === peakIndex ? 'true' : 'false'}>
              <div class="rhythm-fill"
                  style=${`height:${Math.round(value / peak * 100)}%`}></div>
              <div class="rhythm-meta">
                <strong>${label}</strong>
                <span>${this.formatDuration_(value)}</span>
              </div>
            </div>`)}
        </div>
      </section>`;
  }

  private renderThemes_(report: DailyDreamReportData) {
    const themes = report.stats.recap.themes;
    if (themes.length === 0) {
      return nothing;
    }
    return html`
      <section class="recap-block themes-block">
        <div class="block-title">
          <h2>${t('dream.page.themes_title')}</h2>
          <span>${t('dream.page.themes_count', {count: themes.length})}</span>
        </div>
        <div class="theme-grid">
          ${themes.map((theme, index) => html`
            <article class="theme-card">
              <div class="theme-header">
                <span class="theme-icon">${this.renderThemeIcon_(index)}</span>
                <strong>${theme.name}</strong>
              </div>
              <p>${theme.summary}</p>
              <div class="theme-footer">
                <span class="theme-chip intensity">
                  ${t(`dream.page.theme_${theme.intensity}`)}
                </span>
                ${theme.timeLabel ? html`
                  <span class="theme-chip">${theme.timeLabel}</span>` : nothing}
                <span class="theme-bar">
                  <i style=${`width:${theme.attentionShare}%`}></i>
                </span>
              </div>
            </article>`)}
        </div>
      </section>`;
  }

  private renderStats_(report: DailyDreamReportData) {
    const stats = report.stats;
    return html`
      <section class="recap-stats" aria-label=${t('dream.page.stats_label')}>
        ${[
          [stats.historyDomains, t('dream.page.stats_domains')],
          [stats.searchQueries, t('dream.page.stats_searches')],
          [stats.conversationSessions, t('dream.page.stats_conversations')],
        ].map(([value, label]) => html`
          <div class="stat-card">
            <strong>${value}</strong>
            <span>${label}</span>
          </div>`)}
      </section>`;
  }

  private historySummary_(report: DailyDreamReportData) {
    if (!report.stats.hasStructuredRecap) {
      return report.stats.recap.summary ||
          this.triggerKindLabel_(report.triggerKind);
    }
    return report.stats.recap.themes[0]?.name ||
        report.stats.recap.summary ||
        this.triggerKindLabel_(report.triggerKind);
  }

  private renderHistoryList_() {
    const reports = this.historyReports_();
    const selectedKey = this.report_ ?
        `${this.report_.reportKind}:${this.report_.id}` : '';
    return html`
      <div class="history-rail">
        ${this.renderActivityHeatmap_()}
        <nav class="history-list" aria-label=${t('dream.page.history_title')}>
          <div class="history-heading">
            <h2 class="history-list-title">${t('dream.page.history_title')}</h2>
            <span>${t('dream.page.history_recent', {
              count: Math.min(reports.length, 14),
            })}</span>
          </div>
        <div class="history-items">
        ${reports.slice(0, 14).map(report => {
          const weekly = report.reportKind === 'weekly';
          const reportKey = `${report.reportKind}:${report.id}`;
          return html`
          <button
              class="history-item"
              data-report-kind=${report.reportKind}
              data-selected=${selectedKey === reportKey ? 'true' : 'false'}
              aria-current=${selectedKey === reportKey ? 'true' : 'false'}
              @click=${() => this.selectHistoryReport_(report)}>
            <i class="history-dot"
                data-level=${String(
                  weekly ? Math.min(Math.max(report.sourceCount, 1), 4) :
                    this.reportActivityLevel_(report))}
                aria-hidden="true"></i>
            <span class="history-copy">
              <span class="history-date">
                ${this.formatDreamDate_(this.reportDate_(report))}
              </span>
              <span class="history-kind">
                ${weekly ? report.headline :
                  this.historySummary_(report)}
              </span>
            </span>
            ${weekly ? html`<span class="history-kind-badge">
              ${t('dream.page.weekly_badge')}</span>` : nothing}
          </button>`;
        })}
        </div>
        </nav>
      </div>`;
  }

  private renderDreamExclusionShortcut_(report: DailyDreamReportData) {
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

  private renderRerunReportButton_(report: DailyDreamReportData) {
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
        dateLabel: report.reportKind === 'daily' ?
            t('chat.dream.card_date', {date: report.dreamDate}) :
            t('dream.page.weekly_period', {
              start: report.weekStart,
              end: report.weekEnd,
            }),
        markdown: report.reportKind === 'daily' ? report.reportMarkdown :
            this.weeklyReportMarkdown_(report),
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

  private weeklyReportMarkdown_(report: WeeklyDreamReportData): string {
    const lines = [
      `# ${report.headline}`,
      '',
      `## ${t('dream.page.weekly_primary_thread')}`,
      `### ${report.primaryThread.title}`,
      report.primaryThread.statusSummary,
      '',
      `**${t('dream.page.weekly_next_step')}** ` +
          report.primaryThread.nextStep,
    ];
    if (report.secondaryThreads.length > 0) {
      lines.push('', `## ${t('dream.page.weekly_secondary_threads')}`);
      for (const thread of report.secondaryThreads) {
        lines.push('', `### ${thread.title}`, thread.statusSummary,
            `**${t('dream.page.weekly_next_step')}** ${thread.nextStep}`);
      }
    }
    if (report.retainedOutcomes.length > 0) {
      lines.push('', `## ${t('dream.page.weekly_outcomes')}`,
          ...report.retainedOutcomes.map(outcome => `- ${outcome.text}`));
    }
    if (report.timePattern) {
      lines.push('', `## ${t('dream.page.weekly_time_pattern')}`,
          report.timePattern);
    }
    return lines.join('\n');
  }

  private renderWeeklyThread_(thread: WeeklyDreamThread, primary: boolean) {
    return html`
      <article class="theme-card weekly-thread" data-primary=${String(primary)}>
        <div class="theme-header">
          <span class="theme-icon">${this.renderThemeIcon_(primary ? 0 : 1)}</span>
          <strong>${thread.title}</strong>
        </div>
        <p>${thread.statusSummary}</p>
        <div class="weekly-next-step">
          <span>${t('dream.page.weekly_next_step')}</span>
          <p>${thread.nextStep}</p>
        </div>
      </article>`;
  }

  private renderWeeklyReportArticle_(report: WeeklyDreamReportData) {
    const generatedAt = this.formatGeneratedAt_(report.createdAt);
    return html`
      <article class="recap-article weekly-recap">
        <header class="recap-header">
          <div>
            <div class="eyebrow">${t('dream.page.weekly_eyebrow')}</div>
            <h1>${report.headline}</h1>
          </div>
          <div class="header-actions">
            <div class="date">${t('dream.page.weekly_period', {
              start: report.weekStart,
              end: report.weekEnd,
            })}</div>
            ${this.renderCopyImageButton_()}
          </div>
        </header>
        <section class="recap-summary">
          <div>${t('dream.page.weekly_primary_thread')}</div>
          <p>${report.primaryThread.statusSummary}</p>
        </section>
        <section class="recap-block themes-block">
          <div class="block-title">
            <h2>${t('dream.page.weekly_threads')}</h2>
            <span>${t('dream.page.themes_count', {
              count: 1 + report.secondaryThreads.length,
            })}</span>
          </div>
          <div class="theme-grid">
            ${this.renderWeeklyThread_(report.primaryThread, true)}
            ${report.secondaryThreads.map(
              thread => this.renderWeeklyThread_(thread, false))}
          </div>
        </section>
        ${report.retainedOutcomes.length > 0 ? html`
          <section class="memory-section weekly-outcomes">
            <div class="block-title">
              <h2>${t('dream.page.weekly_outcomes')}</h2>
              <span>${t('dream.page.weekly_sources', {
                count: report.sourceCount,
              })}</span>
            </div>
            <div class="memory-candidates">
              ${report.retainedOutcomes.map(outcome => html`
                <div class="habit"><div class="habit-body">
                  <div class="habit-value">${outcome.text}</div>
                </div></div>`)}
            </div>
          </section>` : nothing}
        ${report.timePattern || report.footprintThemes.length > 0 ? html`
          <section class="recap-block weekly-footprint">
            <div class="block-title">
              <h2>${t('dream.page.weekly_time_pattern')}</h2>
              <span>${report.footprintThemes.join(' · ')}</span>
            </div>
            ${report.timePattern ? html`<p>${report.timePattern}</p>` : nothing}
          </section>` : nothing}
        <details class="full-report">
          <summary>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                stroke-width="2" stroke-linecap="round"
                stroke-linejoin="round" aria-hidden="true">
              <path d="m9 18 6-6-6-6"></path>
            </svg>
            ${t('dream.page.full_report')}
          </summary>
          <div class="report-body">
            ${this.renderMarkdown_(this.weeklyReportMarkdown_(report))}
          </div>
        </details>
        ${generatedAt ? html`<div class="debug-meta">
          ${t('dream.debug.generated_at', {time: generatedAt})}
        </div>` : nothing}
      </article>`;
  }

  private renderReportArticle_(report: DailyDreamReportData) {
    const generatedAt = this.formatGeneratedAt_(report.createdAt);
    return html`
      <article class="recap-article">
        <header class="recap-header">
          <div>
            <div class="eyebrow">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                  stroke-width="1.7" stroke-linecap="round"
                  stroke-linejoin="round" aria-hidden="true">
                <path d="M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z"></path>
              </svg>
              ${t('dream.page.recap_eyebrow')}
            </div>
            <h1>${t('dream.page.recap_title')}</h1>
          </div>
          <div class="header-actions">
            <div class="date">${this.formatDreamDate_(report.dreamDate)}</div>
            ${this.renderRerunReportButton_(report)}
            ${this.renderCopyImageButton_()}
          </div>
        </header>
        <section class="recap-summary">
          <div>${t('dream.page.summary_label')}</div>
          <p>${report.stats.recap.summary}</p>
        </section>
        ${this.renderRhythm_(report)}
        ${this.renderThemes_(report)}
        ${this.renderStats_(report)}
        ${this.renderHabits_(report)}
        <details class="full-report">
          <summary>
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                stroke-width="2" stroke-linecap="round"
                stroke-linejoin="round" aria-hidden="true">
              <path d="m9 18 6-6-6-6"></path>
            </svg>
            ${t('dream.page.full_report')}
          </summary>
          <div class="report-body">
            ${this.renderMarkdown_(report.reportMarkdown)}
          </div>
        </details>
        <div class="report-utilities">
          ${this.renderDreamExclusionShortcut_(report)}
          ${report.debugMaterialJson ? html`
            <details class="debug-details">
              <summary>${t('chat.dream.debug_title')}</summary>
              ${generatedAt ? html`
                <div class="debug-meta">
                  ${t('dream.debug.generated_at', {time: generatedAt})}
                </div>` : nothing}
              <pre>${report.debugMaterialJson}</pre>
            </details>` : nothing}
        </div>
      </article>`;
  }

  override render() {
    const report = this.report_;
    const isHistory = this.currentRoute_() === 'history';
    return html`
      <div class="page">
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
                ${report.reportKind === 'daily' ?
                  this.renderReportArticle_(report) :
                  this.renderWeeklyReportArticle_(report)}
              </div>` :
              report.reportKind === 'daily' ?
                this.renderReportArticle_(report) :
                this.renderWeeklyReportArticle_(report)}`
          }
        </main>
      </div>`;
  }
}

customElements.define(DaoDreamApp.is, DaoDreamApp);
