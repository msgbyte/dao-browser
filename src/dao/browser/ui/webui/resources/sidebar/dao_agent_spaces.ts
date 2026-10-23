// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// AI tasks running in background Agent Spaces. In user windows this lists
// each task with its live status: hovering shows a live native preview and
// clicking opens an inline panel that streams the task's page, the agent's
// pointer and its recent steps. In a task window it becomes the control bar
// for taking over and handing back that task.

import {CrLitElement, css, html, nothing} from '//resources/lit/v3_0/lit.rollup.js';
import {loadTimeData} from '//resources/js/load_time_data.js';

import {addListener, removeListener, sendNative} from './sidebar_bridge.js';

export type AgentSpaceState =
    'running'|'user_in_control'|'finished'|'stopped';

export interface AgentSpaceEvent {
  time: number;
  tool: string;
  label: string;
  target?: string;
  url: string;
  pending: boolean;
  ok: boolean;
  error_code?: string;
}

export interface AgentSpaceData {
  space_id: string;
  name: string;
  state: AgentSpaceState;
  owner: string;
  status: string;
  created_at: number;
  tab_count: number;
  window_visible: boolean;
  handoff_reason?: string;
  handoff_message?: string;
  summary?: string;
  stop_reason?: string;
  dialog?: {tab_id: string, type: string, message: string};
  tab_id?: string;
  url?: string;
  title?: string;
  cursor?: {x: number, y: number};
  events?: AgentSpaceEvent[];
}

export interface AgentSpacesState {
  spaces: AgentSpaceData[];
  currentSpaceId: string;
}

type IconName = 'app-window'|'square'|'hand'|'undo-2'|'eye-off'|'x'|'bot'|
                'loader-circle'|'circle-check'|'chevron-down';

const PREVIEW_DELAY_MS = 300;
const MAX_TIMELINE_STEPS = 8;
const COLLAPSED_TIMELINE_STEPS = 3;

const READ_TOOLS = new Set([
  'get_page_info', 'get_page_html', 'get_accessibility_tree',
  'query_elements', 'wait_for_element', 'capture_screenshot', 'list_tabs',
  'get_network_requests', 'wait_for_network_response', 'get_console_messages',
  'list_page_resources', 'get_resource_content', 'get_network_body',
  'search_in_resources',
]);
const NAVIGATE_TOOLS = new Set(['open_tab', 'switch_tab', 'close_tab']);

function str(id: string): string {
  return loadTimeData.getString(id);
}

function describeStep(event: AgentSpaceEvent): string {
  if (event.label) {
    return event.label;
  }
  if (event.tool === 'create_space') {
    return str('daoAgentSpaceStepStart');
  }
  if (READ_TOOLS.has(event.tool)) {
    return str('daoAgentSpaceStepRead');
  }
  if (NAVIGATE_TOOLS.has(event.tool)) {
    return str('daoAgentSpaceStepNavigate');
  }
  return str('daoAgentSpaceStepAct');
}

function hostOf(url: string|undefined): string {
  if (!url) {
    return '';
  }
  try {
    return new URL(url).host;
  } catch {
    return '';
  }
}

function formatTime(ms: number): string {
  return new Date(ms).toLocaleTimeString(undefined, {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });
}

export class DaoAgentSpacesElement extends CrLitElement {
  static get is() {
    return 'dao-agent-spaces';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
        font-family: var(--font-family, system-ui);
        --space-accent-text: var(--accent);
        --space-attention: rgb(176, 104, 16);
        --space-attention-subtle: rgba(214, 145, 40, 0.16);
        --space-done: rgb(46, 140, 88);
        --space-failed: rgb(200, 70, 60);
      }

      @media (prefers-color-scheme: dark) {
        :host {
          --space-accent-text: rgb(130, 172, 232);
          --space-attention: rgb(240, 172, 80);
          --space-attention-subtle: rgba(240, 172, 80, 0.18);
          --space-done: rgb(98, 200, 140);
          --space-failed: rgb(240, 110, 100);
        }
      }

      .section-header {
        display: flex;
        align-items: center;
        gap: 6px;
        font-size: 10px;
        font-weight: 600;
        color: var(--text-muted);
        text-transform: uppercase;
        letter-spacing: 0.5px;
        padding: 4px 14px;
      }

      .count {
        padding: 0 5px;
        border-radius: 5px;
        background: var(--accent-subtle);
        color: var(--space-accent-text);
        letter-spacing: 0;
        line-height: 15px;
      }

      .space {
        margin: 0 8px 2px;
        border-radius: var(--radius-tab, 12px);
      }

      .space.expanded {
        margin-bottom: 6px;
        background: var(--surface-active);
        box-shadow: 0 0 0 1px var(--separator);
      }

      .row {
        display: flex;
        align-items: center;
        gap: 8px;
        min-height: 36px;
        padding: 4px 8px 4px 10px;
        border-radius: var(--radius-tab, 12px);
        cursor: default;
      }

      .row:hover,
      .row:focus-visible {
        background: var(--ink-drop);
        outline: none;
      }

      .row.tall {
        align-items: flex-start;
        padding-top: 7px;
        padding-bottom: 8px;
      }

      .state-icon {
        flex: none;
        display: flex;
        align-items: center;
        justify-content: center;
        width: 16px;
        height: 16px;
        border-radius: 50%;
      }

      .row.tall .state-icon {
        margin-top: 1px;
      }

      .state-icon.running {
        color: var(--space-accent-text);
        animation: daoSpaceSpin 1.2s linear infinite;
      }

      .state-icon.attention {
        background: var(--space-attention-subtle);
        color: var(--space-attention);
      }

      .state-icon.control {
        background: var(--accent-subtle);
        color: var(--space-accent-text);
      }

      .state-icon.finished {
        color: var(--space-done);
      }

      .state-icon.stopped {
        background: var(--surface);
        color: var(--text-muted);
      }

      @keyframes daoSpaceSpin {
        to { transform: rotate(360deg); }
      }

      @media (prefers-reduced-motion: reduce) {
        .state-icon.running {
          animation: none;
        }
      }

      .text {
        flex: 1;
        min-width: 0;
        display: flex;
        flex-direction: column;
      }

      .name {
        font-size: 13px;
        font-weight: 600;
        color: var(--text-primary);
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
      }

      .detail {
        font-size: 11px;
        color: var(--text-secondary);
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
      }

      .detail.attention {
        color: var(--space-attention);
      }

      .detail.result {
        white-space: normal;
        display: -webkit-box;
        -webkit-box-orient: vertical;
        -webkit-line-clamp: 2;
      }

      .resolve {
        display: flex;
        margin-top: 6px;
      }

      .row-actions {
        display: none;
        gap: 2px;
      }

      .row:hover .row-actions,
      .row-actions.always {
        display: flex;
      }

      .chevron {
        display: flex;
        color: var(--text-muted);
        transform: rotate(180deg);
      }

      .icon-btn {
        display: flex;
        align-items: center;
        justify-content: center;
        width: 24px;
        height: 24px;
        padding: 0;
        border: none;
        border-radius: 8px;
        background: transparent;
        color: var(--text-secondary);
      }

      .icon-btn:hover {
        background: var(--ink-drop-strong);
        color: var(--text-primary);
      }

      .panel {
        padding: 0 6px 6px;
      }

      .frame {
        position: relative;
        width: 100%;
        aspect-ratio: 16 / 10;
        border-radius: 10px;
        overflow: hidden;
        background: var(--separator);
      }

      .frame img {
        display: block;
        width: 100%;
        height: 100%;
        object-fit: cover;
      }

      .live {
        position: absolute;
        bottom: 6px;
        left: 6px;
        padding: 1px 6px;
        border-radius: 6px;
        background: rgba(0, 0, 0, 0.55);
        color: white;
        font-size: 10px;
        font-weight: 600;
      }

      .pointer {
        position: absolute;
        width: 10px;
        height: 10px;
        margin: -5px 0 0 -5px;
        border-radius: 50%;
        background: var(--accent);
        box-shadow: 0 0 0 2px white;
        transition: left 220ms ease-out, top 220ms ease-out;
        pointer-events: none;
      }

      .now {
        display: flex;
        align-items: flex-start;
        gap: 6px;
        margin: 8px 2px 0;
      }

      .now-tag {
        flex: none;
        padding: 0 5px;
        border-radius: 5px;
        background: var(--accent-subtle);
        color: var(--space-accent-text);
        font-size: 10px;
        font-weight: 600;
        line-height: 17px;
      }

      .now-text {
        flex: 1;
        min-width: 0;
        font-size: 12px;
        font-weight: 600;
        line-height: 17px;
        color: var(--text-primary);
        display: -webkit-box;
        -webkit-box-orient: vertical;
        -webkit-line-clamp: 2;
        overflow: hidden;
      }

      .now-text.attention {
        color: var(--space-attention);
      }

      .timeline {
        list-style: none;
        margin: 4px 0 0;
        padding: 0 2px;
      }

      .step {
        position: relative;
        padding: 2px 0 2px 14px;
        font-size: 11px;
        line-height: 15px;
        color: var(--text-secondary);
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
      }

      .step::before {
        content: '';
        position: absolute;
        z-index: 1;
        top: 7px;
        left: 3px;
        width: 5px;
        height: 5px;
        border-radius: 50%;
        background: var(--text-muted);
      }

      .step::after {
        content: '';
        position: absolute;
        top: 0;
        bottom: 0;
        left: 5px;
        width: 1px;
        background: var(--separator);
      }

      .step:first-child::after {
        top: 9px;
      }

      .step:last-child::after {
        bottom: calc(100% - 9px);
      }

      .step .target {
        color: var(--text-muted);
      }

      .step.pending {
        color: var(--text-primary);
      }

      .step.pending::before {
        background: var(--accent);
        box-shadow: 0 0 0 2px var(--accent-subtle);
      }

      .step.failed {
        color: var(--space-failed);
      }

      .step.failed::before {
        background: var(--space-failed);
      }

      .link-btn {
        margin: 2px 0 0 16px;
        padding: 0;
        border: none;
        background: none;
        color: var(--space-accent-text);
        font: inherit;
        font-size: 11px;
        cursor: pointer;
      }

      .link-btn:hover {
        text-decoration: underline;
      }

      .card-buttons {
        display: flex;
        align-items: center;
        gap: 4px;
        margin-top: 8px;
      }

      .card-buttons .btn {
        flex: 1;
        min-width: 0;
      }

      .card-buttons .icon-btn {
        width: 28px;
        height: 28px;
        border-radius: 10px;
        background: var(--surface);
      }

      .buttons {
        display: flex;
        flex-wrap: wrap;
        gap: 6px;
        margin-top: 8px;
      }

      .btn {
        flex: 1 1 auto;
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 6px;
        height: 28px;
        padding: 0 10px;
        border: none;
        border-radius: 12px;
        background: var(--surface);
        color: var(--text-primary);
        font: inherit;
        font-size: 12px;
        white-space: nowrap;
      }

      .btn:hover {
        background: var(--ink-drop-strong);
      }

      .btn.primary {
        background: var(--accent);
        color: white;
      }

      .btn.primary:hover {
        filter: brightness(1.08);
      }

      .btn.attention {
        flex: none;
        height: 24px;
        border-radius: 9px;
        background: var(--space-attention-subtle);
        color: var(--space-attention);
        font-size: 11px;
        font-weight: 600;
      }

      .btn.attention:hover {
        filter: brightness(0.96);
      }

      .bar {
        margin: 4px 8px 6px;
        padding: 10px;
        border-radius: var(--radius-tab, 12px);
        background: var(--accent-subtle);
      }

      .bar .title {
        display: flex;
        align-items: center;
        gap: 6px;
        font-size: 11px;
        font-weight: 600;
        color: var(--accent);
        text-transform: uppercase;
        letter-spacing: 0.4px;
      }

      .bar .name {
        margin-top: 4px;
      }

      .bar .hint {
        margin-top: 4px;
        font-size: 11px;
        color: var(--text-secondary);
      }

      svg {
        flex: none;
      }
    `;
  }

  static override get properties() {
    return {
      spaces_: {type: Array},
      currentSpaceId_: {type: String},
      expandedId_: {type: String},
      frameUrl_: {type: String},
      showAllSteps_: {type: Boolean},
    };
  }

  declare protected spaces_: AgentSpaceData[];
  declare protected currentSpaceId_: string;
  declare protected expandedId_: string;
  declare protected frameUrl_: string;
  declare protected showAllSteps_: boolean;

  private listeners_: Array<ReturnType<typeof addListener>> = [];
  private previewTimer_ = 0;
  private previewId_ = '';

  constructor() {
    super();
    this.spaces_ = [];
    this.currentSpaceId_ = '';
    this.expandedId_ = '';
    this.frameUrl_ = '';
    this.showAllSteps_ = false;
  }

  override connectedCallback() {
    super.connectedCallback();
    this.listeners_.push(
        addListener('agentSpacesChanged', (...args: unknown[]) => {
          this.onStateChanged_(args[0] as AgentSpacesState);
        }),
        addListener('agentSpaceFrame', (...args: unknown[]) => {
          const frame = args[0] as {spaceId: string, dataUrl: string};
          if (frame && frame.spaceId === this.expandedId_) {
            this.frameUrl_ = frame.dataUrl;
          }
        }));
    sendNative('getAgentSpaces');
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    for (const listener of this.listeners_) {
      removeListener(listener);
    }
    this.listeners_ = [];
    this.hidePreview_();
    if (this.expandedId_) {
      sendNative('stopAgentSpaceStream');
    }
  }

  private onStateChanged_(state: AgentSpacesState) {
    this.spaces_ = state?.spaces || [];
    this.currentSpaceId_ = state?.currentSpaceId || '';
    const expanded = this.spaces_.find(s => s.space_id === this.expandedId_);
    if (this.expandedId_ && (!expanded || this.isClosed_(expanded))) {
      this.collapse_();
    }
  }

  private isClosed_(space: AgentSpaceData): boolean {
    return space.state === 'finished' || space.state === 'stopped';
  }

  // True when the task is waiting on a step only the user can do, as opposed
  // to the user having taken control on their own.
  private needsAttention_(space: AgentSpaceData): boolean {
    return space.state === 'user_in_control' && !!space.handoff_reason &&
        space.handoff_reason !== 'manual_takeover';
  }

  private action_(action: string, space: AgentSpaceData, e?: Event) {
    e?.stopPropagation();
    this.hidePreview_();
    sendNative('agentSpaceAction', action, space.space_id);
  }

  private toggle_(space: AgentSpaceData) {
    this.hidePreview_();
    if (this.expandedId_ === space.space_id) {
      this.collapse_();
      return;
    }
    if (this.isClosed_(space)) {
      return;
    }
    this.expandedId_ = space.space_id;
    this.frameUrl_ = '';
    this.showAllSteps_ = false;
    sendNative('startAgentSpaceStream', space.space_id);
  }

  private collapse_() {
    if (this.expandedId_) {
      sendNative('stopAgentSpaceStream');
    }
    this.expandedId_ = '';
    this.frameUrl_ = '';
    this.showAllSteps_ = false;
  }

  private onRowKeyDown_(space: AgentSpaceData, e: KeyboardEvent) {
    if (e.target !== e.currentTarget ||
        (e.key !== 'Enter' && e.key !== ' ')) {
      return;
    }
    e.preventDefault();
    this.toggle_(space);
  }

  private onRowEnter_(space: AgentSpaceData, e: PointerEvent) {
    if (this.isClosed_(space) || this.expandedId_ === space.space_id) {
      return;
    }
    const row = e.currentTarget as HTMLElement;
    window.clearTimeout(this.previewTimer_);
    this.previewTimer_ = window.setTimeout(() => {
      this.previewTimer_ = 0;
      this.previewId_ = space.space_id;
      sendNative('showAgentSpacePreview',
          Math.round(e.screenX - e.clientX + window.innerWidth + 8),
          Math.round(e.screenY - e.clientY + row.getBoundingClientRect().top),
          space.space_id);
    }, PREVIEW_DELAY_MS);
  }

  private hidePreview_() {
    window.clearTimeout(this.previewTimer_);
    this.previewTimer_ = 0;
    if (this.previewId_) {
      this.previewId_ = '';
      sendNative('hideAgentSpacePreview');
    }
  }

  private stateLabel_(space: AgentSpaceData): string {
    switch (space.state) {
      case 'running':
        if (space.dialog) {
          return loadTimeData.getStringF(
              'daoAgentSpaceDialogPending', space.dialog.message);
        }
        return space.status || this.lastStep_(space) ||
            str('daoAgentSpaceStateRunning');
      case 'user_in_control':
        if (space.handoff_reason === 'help_requested') {
          return loadTimeData.getStringF(
              'daoAgentSpaceNeedsYou', space.handoff_message || '');
        }
        switch (space.handoff_reason) {
          case 'permission_prompt':
            return str('daoAgentSpaceNeedsPermission');
          case 'file_chooser':
            return str('daoAgentSpaceNeedsFile');
          case 'device_chooser':
            return str('daoAgentSpaceNeedsDevice');
          case 'browser_dialog':
            return str('daoAgentSpaceNeedsBrowserDialog');
        }
        return str('daoAgentSpaceStateYouHaveControl');
      case 'finished':
        return space.summary || str('daoAgentSpaceStateFinished');
      case 'stopped':
        return space.summary ||
            str(space.stop_reason === 'inactive' ?
                    'daoAgentSpaceStateStoppedInactive' :
                    'daoAgentSpaceStateStopped');
    }
  }

  private lastStep_(space: AgentSpaceData): string {
    const events = space.events || [];
    const last = events[events.length - 1];
    return last ? describeStep(last) : '';
  }

  private startedLabel_(space: AgentSpaceData): string {
    const time = new Date(space.created_at).toLocaleTimeString(undefined, {
      hour: '2-digit',
      minute: '2-digit',
    });
    return space.owner ?
        loadTimeData.getStringF('daoAgentSpaceStartedBy', space.owner, time) :
        loadTimeData.getStringF('daoAgentSpaceStartedAt', time);
  }

  // Lucide icons, copied from lucide-icons/lucide main. Each icon is a whole
  // template so its shapes are parsed as SVG; shapes from a separate html``
  // template would land in the HTML namespace and never paint.
  private renderIcon_(name: IconName, size = 14) {
    switch (name) {
      case 'app-window':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <rect x="2" y="4" width="20" height="16" rx="2" />
          <path d="M10 4v4" />
          <path d="M2 8h20" />
          <path d="M6 4v4" />
        </svg>`;
      case 'square':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <rect width="18" height="18" x="3" y="3" rx="2" />
        </svg>`;
      case 'hand':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <path d="M18 11V6a2 2 0 0 0-2-2a2 2 0 0 0-2 2" />
          <path d="M14 10V4a2 2 0 0 0-2-2a2 2 0 0 0-2 2v2" />
          <path d="M10 10.5V6a2 2 0 0 0-2-2a2 2 0 0 0-2 2v8" />
          <path d="M18 8a2 2 0 1 1 4 0v6a8 8 0 0 1-8 8h-2c-2.8 0-4.5-.86-5.99-2.34l-3.6-3.6a2 2 0 0 1 2.83-2.82L7 15" />
        </svg>`;
      case 'undo-2':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <path d="M9 14 4 9l5-5" />
          <path d="M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11" />
        </svg>`;
      case 'eye-off':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <path d="M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-1.444 2.49" />
          <path d="M14.084 14.158a3 3 0 0 1-4.242-4.242" />
          <path d="M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 10.75 0 0 1 4.446-5.143" />
          <path d="m2 2 20 20" />
        </svg>`;
      case 'x':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <path d="M18 6 6 18" />
          <path d="m6 6 12 12" />
        </svg>`;
      case 'bot':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <path d="M12 8V4H8" />
          <rect width="16" height="12" x="4" y="8" rx="2" />
          <path d="M2 14h2" />
          <path d="M20 14h2" />
          <path d="M15 13v2" />
          <path d="M9 13v2" />
        </svg>`;
      case 'loader-circle':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <path d="M21 12a9 9 0 1 1-6.219-8.56" />
        </svg>`;
      case 'circle-check':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <circle cx="12" cy="12" r="10" />
          <path d="m16 9-5.5 5.5L8 12" />
        </svg>`;
      case 'chevron-down':
        return html`<svg width=${size} height=${size} viewBox="0 0 24 24"
            fill="none" stroke="currentColor" stroke-width="2"
            stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
          <path d="m6 9 6 6 6-6" />
        </svg>`;
    }
  }

  private renderControlButtons_(space: AgentSpaceData, inTaskWindow: boolean) {
    const controlling = space.state === 'user_in_control';
    return html`
      <div class="buttons">
        ${controlling ? html`
          <button class="btn primary"
                  @click=${(e: Event) => this.action_('hand_back', space, e)}>
            ${this.renderIcon_('undo-2')}${str('daoAgentSpaceHandBack')}
          </button>
        ` : html`
          <button class="btn ${inTaskWindow ? 'primary' : ''}"
                  @click=${(e: Event) => this.action_('take_over', space, e)}>
            ${this.renderIcon_('hand')}${str('daoAgentSpaceTakeOver')}
          </button>
        `}
        ${inTaskWindow ? html`
          <button class="btn"
                  @click=${(e: Event) => this.action_('hide', space, e)}>
            ${this.renderIcon_('eye-off')}${str('daoAgentSpaceHideWindow')}
          </button>
        ` : html`
          <button class="btn"
                  @click=${(e: Event) => this.action_('show', space, e)}>
            ${this.renderIcon_('app-window')}${str('daoAgentSpaceOpenWindow')}
          </button>
        `}
        <button class="btn"
                @click=${(e: Event) => this.action_('stop', space, e)}>
          ${this.renderIcon_('square')}${str('daoAgentSpaceStop')}
        </button>
      </div>
    `;
  }

  private renderStateIcon_(space: AgentSpaceData) {
    switch (space.state) {
      case 'running':
        return html`<span class="state-icon running">
          ${this.renderIcon_('loader-circle', 16)}</span>`;
      case 'user_in_control':
        return html`<span class="state-icon ${
            this.needsAttention_(space) ? 'attention' : 'control'}">
          ${this.renderIcon_('hand', 10)}</span>`;
      case 'finished':
        return html`<span class="state-icon finished">
          ${this.renderIcon_('circle-check', 16)}</span>`;
      case 'stopped':
        return html`<span class="state-icon stopped">
          ${this.renderIcon_('square', 8)}</span>`;
    }
  }

  private renderPanel_(space: AgentSpaceData) {
    const now = this.stateLabel_(space);
    const steps = (space.events || []).slice(-MAX_TIMELINE_STEPS).reverse();
    // Without a status the "now" line already shows the newest step.
    const newest = steps[0];
    if (newest && describeStep(newest) === now) {
      steps.shift();
    }
    const shown =
        this.showAllSteps_ ? steps : steps.slice(0, COLLAPSED_TIMELINE_STEPS);
    const controlling = space.state === 'user_in_control';
    return html`
      <div class="panel">
        <div class="frame">
          ${this.frameUrl_ ? html`<img src=${this.frameUrl_} alt="">` :
                             nothing}
          <span class="live">${str('daoAgentSpaceLive')}</span>
          ${space.cursor && space.state === 'running' ? html`
            <span class="pointer"
                  style="left:${space.cursor.x * 100}%;top:${
                      space.cursor.y * 100}%"></span>
          ` : nothing}
        </div>
        <div class="now">
          <span class="now-tag">${str('daoAgentSpaceNow')}</span>
          <span class="now-text ${
              this.needsAttention_(space) ? 'attention' : ''}">${now}</span>
        </div>
        ${shown.length ? html`
          <ol class="timeline">
            ${shown.map(event => html`
              <li class="step ${event.pending ? 'pending' : ''} ${
                  !event.pending && !event.ok ? 'failed' : ''}"
                  title=${[formatTime(event.time), hostOf(event.url)]
                              .filter(Boolean).join(' · ')}>
                ${describeStep(event)}${event.target ?
                    html` <span class="target">${event.target}</span>` :
                    nothing}
              </li>
            `)}
          </ol>
        ` : nothing}
        ${steps.length > COLLAPSED_TIMELINE_STEPS ? html`
          <button class="link-btn"
                  @click=${() => {
                    this.showAllSteps_ = !this.showAllSteps_;
                  }}>
            ${str(this.showAllSteps_ ? 'daoAgentSpaceShowFewerSteps' :
                                       'daoAgentSpaceShowAllSteps')}
          </button>
        ` : nothing}
        <div class="card-buttons">
          ${controlling ? html`
            <button class="btn"
                    @click=${(e: Event) => this.action_('hand_back', space, e)}>
              ${this.renderIcon_('undo-2')}${str('daoAgentSpaceHandBack')}
            </button>
          ` : html`
            <button class="btn"
                    @click=${(e: Event) => this.action_('take_over', space, e)}>
              ${this.renderIcon_('hand')}${str('daoAgentSpaceTakeOver')}
            </button>
          `}
          <button class="icon-btn" title=${str('daoAgentSpaceOpenWindow')}
                  aria-label=${str('daoAgentSpaceOpenWindow')}
                  @click=${(e: Event) => this.action_('show', space, e)}>
            ${this.renderIcon_('app-window')}
          </button>
          <button class="icon-btn" title=${str('daoAgentSpaceStop')}
                  aria-label=${str('daoAgentSpaceStop')}
                  @click=${(e: Event) => this.action_('stop', space, e)}>
            ${this.renderIcon_('square')}
          </button>
        </div>
      </div>
    `;
  }

  private renderSpace_(space: AgentSpaceData) {
    const expanded = this.expandedId_ === space.space_id;
    const closed = this.isClosed_(space);
    const attention = this.needsAttention_(space);
    const detail =
        expanded ? this.startedLabel_(space) : this.stateLabel_(space);
    return html`
      <div class="space ${expanded ? 'expanded' : ''}">
        <div class="row ${attention || closed ? 'tall' : ''}" role="button"
             tabindex="0"
             aria-expanded=${closed ? nothing : String(expanded)}
             @click=${() => this.toggle_(space)}
             @keydown=${(e: KeyboardEvent) => this.onRowKeyDown_(space, e)}
             @pointerenter=${(e: PointerEvent) => this.onRowEnter_(space, e)}
             @pointerleave=${() => this.hidePreview_()}>
          ${this.renderStateIcon_(space)}
          <span class="text">
            <span class="name">${space.name}</span>
            <span class="detail ${attention && !expanded ? 'attention' : ''} ${
                closed ? 'result' : ''}"
                  title=${closed ? detail : nothing}>
              ${detail}
            </span>
            ${attention ? html`
              <span class="resolve">
                <button class="btn attention"
                        @click=${(e: Event) => this.action_('show', space, e)}>
                  ${str('daoAgentSpaceResolve')}
                </button>
              </span>
            ` : nothing}
          </span>
          ${expanded ? html`
            <span class="chevron">${this.renderIcon_('chevron-down')}</span>
          ` : html`
            <span class="row-actions ${closed ? 'always' : ''}">
              ${closed ? html`
                <button class="icon-btn" title=${str('daoAgentSpaceDismiss')}
                        aria-label=${str('daoAgentSpaceDismiss')}
                        @click=${(e: Event) =>
                                     this.action_('dismiss', space, e)}>
                  ${this.renderIcon_('x')}
                </button>
              ` : html`
                <button class="icon-btn"
                        title=${str('daoAgentSpaceOpenWindow')}
                        aria-label=${str('daoAgentSpaceOpenWindow')}
                        @click=${(e: Event) => this.action_('show', space, e)}>
                  ${this.renderIcon_('app-window')}
                </button>
                <button class="icon-btn" title=${str('daoAgentSpaceStop')}
                        aria-label=${str('daoAgentSpaceStop')}
                        @click=${(e: Event) => this.action_('stop', space, e)}>
                  ${this.renderIcon_('square')}
                </button>
              `}
            </span>
          `}
        </div>
        ${expanded ? this.renderPanel_(space) : nothing}
      </div>
    `;
  }

  private renderTaskWindowBar_(space: AgentSpaceData) {
    const controlling = space.state === 'user_in_control';
    return html`
      <div class="bar">
        <div class="title">
          ${this.renderIcon_('bot')}${str('daoAgentSpaceTaskWindowLabel')}
        </div>
        <div class="name">${space.name}</div>
        <div class="detail ${controlling ? 'attention' : ''}">
          ${this.stateLabel_(space)}
        </div>
        <div class="hint">
          ${str(controlling ? 'daoAgentSpaceTaskWindowUserHint' :
                              'daoAgentSpaceTaskWindowAgentHint')}
        </div>
        ${this.isClosed_(space) ? nothing :
                                  this.renderControlButtons_(space, true)}
      </div>
    `;
  }

  override render() {
    if (this.currentSpaceId_) {
      const space =
          this.spaces_.find(s => s.space_id === this.currentSpaceId_);
      return space ? this.renderTaskWindowBar_(space) : html``;
    }
    if (this.spaces_.length === 0) {
      return html``;
    }
    const active = this.spaces_.filter(s => !this.isClosed_(s)).length;
    return html`
      <div class="section-header">
        ${str('daoAgentSpacesTitle')}
        ${active ? html`<span class="count">${active}</span>` : nothing}
      </div>
      ${this.spaces_.map(space => this.renderSpace_(space))}
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'dao-agent-spaces': DaoAgentSpacesElement;
  }
}

customElements.define(DaoAgentSpacesElement.is, DaoAgentSpacesElement);
