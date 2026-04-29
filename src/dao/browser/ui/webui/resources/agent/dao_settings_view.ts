// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, css, html, nothing} from
    '//resources/lit/v3_0/lit.rollup.js';

import {
  callNativeArgs,
  CONFIDENCE_THRESHOLD_MAP,
  currentSoulContent,
  DEFAULT_SOUL,
  getAgentStats,
  refreshSoulContent,
  resetAgentStats,
  saveSoul,
  soulChannel,
} from './agent_bridge.js';
import type {AgentStats} from './agent_bridge.js';
import {
  getActiveProvider,
  getProviderConfig,
  LLM_PROVIDERS,
  setActiveProvider,
  setProviderConfig,
} from './llm_config.js';
import type {ProviderSpec} from './llm_config.js';
import {tools as allTools} from './agent_bridge.js';
import {
  countEnabled,
  getGroupState,
  isGroupExpanded,
  isToolEnabled,
  setGroupEnabled,
  setGroupExpanded,
  setToolEnabled,
  TOOL_GROUPS,
  toolConfigChannel,
} from './tool_catalog.js';

export class DaoSettingsView extends CrLitElement {
  static override get properties() {
    return {
      activeSubTab_: {type: String, state: true},
      provider_: {type: String, state: true},
      apiKey_: {type: String, state: true},
      baseUrl_: {type: String, state: true},
      model_: {type: String, state: true},
      soulText_: {type: String, state: true},
      saveStatusText_: {type: String, state: true},
      saveStatusVisible_: {type: Boolean, state: true},
      memoryEnabled_: {type: Boolean, state: true},
      proactiveEnabled_: {type: Boolean, state: true},
      pageContextEnabled_: {type: Boolean, state: true},
      conversationEnabled_: {type: Boolean, state: true},
      threshold_: {type: String, state: true},
      statConversations_: {type: Number, state: true},
      statPreferences_: {type: Number, state: true},
      statEpisodes_: {type: Number, state: true},
      statTotal_: {type: String, state: true},
      showConfirmDialog_: {type: Boolean, state: true},
      agentStats_: {type: Object, state: true},
      showResetStatsDialog_: {type: Boolean, state: true},
      toolCallShowDetails_: {type: Boolean, state: true},
      resumeLastSession_: {type: Boolean, state: true},
    };
  }

  declare private activeSubTab_: string;
  declare private provider_: string;
  declare private apiKey_: string;
  declare private baseUrl_: string;
  declare private model_: string;
  declare private soulText_: string;
  declare private saveStatusText_: string;
  declare private saveStatusVisible_: boolean;
  declare private memoryEnabled_: boolean;
  declare private proactiveEnabled_: boolean;
  declare private pageContextEnabled_: boolean;
  declare private conversationEnabled_: boolean;
  declare private threshold_: string;
  declare private statConversations_: number;
  declare private statPreferences_: number;
  declare private statEpisodes_: number;
  declare private statTotal_: string;
  declare private showConfirmDialog_: boolean;
  private saveStatusTimer_ = 0;
  declare private agentStats_: AgentStats|null;
  declare private showResetStatsDialog_: boolean;
  declare private toolCallShowDetails_: boolean;
  declare private resumeLastSession_: boolean;

  static override get styles() {
    return css`
      :host {
        display: flex; flex-direction: column;
        flex: 1; overflow: hidden;
      }
      :host([hidden]) { display: none !important; }

      .settings-sub-tabs {
        display: flex; gap: 2px; padding: 6px 14px 0;
        border-bottom: 1px solid rgba(255,255,255,0.15); flex-shrink: 0;
      }
      .sub-tab {
        background: none; border: none;
        border-bottom: 2px solid transparent;
        padding: 6px 10px; font-size: 12px; font-family: inherit;
        color: var(--text-tertiary); cursor: pointer;
        transition: color 0.15s, border-color 0.15s;
      }
      .sub-tab:hover { color: var(--text-secondary); }
      .sub-tab.active {
        color: var(--text); border-bottom-color: var(--accent);
      }

      .panel {
        flex: 1; overflow-y: auto; overflow-x: hidden; padding: 14px;
      }
      .panel::-webkit-scrollbar { width: 4px; }
      .panel::-webkit-scrollbar-track { background: transparent; }
      .panel::-webkit-scrollbar-thumb {
        background: rgba(0,0,0,0.12); border-radius: 4px;
      }
      .panel::-webkit-scrollbar-thumb:hover {
        background: rgba(0,0,0,0.2);
      }

      @media (prefers-color-scheme: dark) {
        .panel::-webkit-scrollbar-thumb {
          background: rgba(255,255,255,0.18);
        }
        .panel::-webkit-scrollbar-thumb:hover {
          background: rgba(255,255,255,0.30);
        }
      }

      .section-title {
        font-size: 14px; font-weight: 600;
        color: var(--text); margin-bottom: 4px;
      }
      .section-desc {
        font-size: 12px; color: var(--text-tertiary);
        margin-bottom: 12px; line-height: 1.4;
      }

      /* Connection inputs */
      label {
        display: block; font-size: 11px;
        color: var(--text-tertiary);
        margin-bottom: 3px; margin-top: 10px;
      }
      label:first-of-type { margin-top: 0; }
      input {
        width: 100%; padding: 7px 10px;
        box-sizing: border-box;
        background: var(--glass); border: 1px solid var(--glass-border);
        border-radius: 10px; color: var(--text);
        font-size: 12px; outline: none;
        box-shadow: var(--shadow-sm);
        transition: border-color 0.15s, box-shadow 0.15s;
      }
      input:focus {
        border-color: rgba(70, 120, 190, 0.4);
        box-shadow: 0 0 0 3px rgba(70, 120, 190, 0.08);
      }
      select {
        width: 100%; padding: 7px 10px;
        box-sizing: border-box;
        background: var(--glass); border: 1px solid var(--glass-border);
        border-radius: 10px; color: var(--text);
        font-size: 12px; font-family: inherit; outline: none;
        box-shadow: var(--shadow-sm);
        appearance: none;
        background-image: url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='white' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><polyline points='6 9 12 15 18 9'/></svg>");
        background-repeat: no-repeat;
        background-position: right 10px center;
        padding-right: 28px;
        transition: border-color 0.15s, box-shadow 0.15s;
      }
      select:focus {
        border-color: rgba(70, 120, 190, 0.4);
        box-shadow: 0 0 0 3px rgba(70, 120, 190, 0.08);
      }
      select option {
        background: var(--glass-strong, #2a2434); color: var(--text);
      }

      /* Soul editor */
      .soul-editor {
        width: 100%; min-height: 300px; padding: 10px 12px;
        box-sizing: border-box;
        background: var(--glass); border: 1px solid var(--glass-border);
        border-radius: 10px; color: var(--text);
        font-family: ui-monospace, 'SF Mono', Menlo, Monaco, monospace;
        font-size: 12px; line-height: 1.5;
        resize: vertical; outline: none;
        box-shadow: var(--shadow-sm);
        transition: border-color 0.15s, box-shadow 0.15s;
      }
      .soul-editor:focus {
        border-color: rgba(70, 120, 190, 0.4);
        box-shadow: 0 0 0 3px rgba(70, 120, 190, 0.08);
      }
      .soul-editor::placeholder { color: var(--text-tertiary); }
      .soul-actions {
        display: flex; align-items: center; gap: 8px; margin-top: 10px;
      }
      .btn-primary {
        padding: 7px 18px; background: var(--accent); border: none;
        border-radius: 10px; color: white;
        font-size: 12px; font-family: inherit; cursor: pointer;
        box-shadow: 0 2px 6px rgba(70, 120, 190, 0.25);
        transition: filter 0.15s, box-shadow 0.15s;
      }
      .btn-primary:hover {
        filter: brightness(1.12);
        box-shadow: 0 3px 10px rgba(70, 120, 190, 0.35);
      }
      .btn-secondary {
        padding: 7px 18px; background: var(--glass);
        border: 1px solid var(--glass-border); border-radius: 10px;
        color: var(--text-secondary);
        font-size: 12px; font-family: inherit; cursor: pointer;
        transition: background 0.15s, color 0.15s;
      }
      .btn-secondary:hover {
        background: var(--glass-strong); color: var(--text);
      }
      .save-status {
        font-size: 11px; color: var(--accent);
        opacity: 0; transition: opacity 0.3s;
      }
      .save-status.visible { opacity: 1; }

      /* Toggle row */
      .toggle-row {
        display: flex; align-items: center; justify-content: space-between;
        padding: 8px 0; border-bottom: 1px solid var(--border);
      }
      .toggle-label {
        display: flex; flex-direction: column; gap: 2px;
        flex: 1; min-width: 0;
      }
      .toggle-name { font-size: 13px; color: var(--text); }
      .toggle-desc { font-size: 11px; color: var(--text-tertiary); }
      .toggle {
        position: relative; display: inline-block;
        width: 36px; height: 20px; flex-shrink: 0; cursor: pointer;
      }
      .toggle input { display: none; }
      .toggle-track {
        position: absolute; inset: 0;
        background: var(--glass); border: 1px solid var(--glass-border);
        border-radius: 10px; transition: background 150ms, border-color 150ms;
      }
      .toggle-track::after {
        content: ''; position: absolute; top: 2px; left: 2px;
        width: 14px; height: 14px; background: white;
        border-radius: 50%; transition: transform 150ms;
        box-shadow: 0 1px 3px rgba(0,0,0,0.1);
      }
      .toggle input:checked + .toggle-track {
        background: var(--accent); border-color: var(--accent);
      }
      .toggle input:checked + .toggle-track::after {
        transform: translateX(16px);
      }

      /* Segment selector */
      .setting-group {
        padding: 10px 0; border-bottom: 1px solid var(--border);
      }
      .segment-selector {
        display: flex; gap: 2px; margin-top: 8px; padding: 2px;
        background: var(--glass); border: 1px solid var(--glass-border);
        border-radius: 10px; overflow: hidden;
      }
      .segment {
        flex: 1; height: 26px; background: transparent;
        border: none; font-size: 12px; font-family: inherit;
        color: var(--text-secondary); cursor: pointer;
        border-radius: 8px;
        transition: background 150ms, color 150ms;
      }
      .segment:hover { background: rgba(255,255,255,0.2); }
      .segment.active {
        background: var(--accent); color: white;
        box-shadow: 0 1px 4px rgba(70, 120, 190, 0.3);
      }

      /* Memory stats */
      .memory-stats { margin-top: 16px; padding-top: 12px; }
      .stats-list {
        display: flex; flex-direction: column; gap: 8px; margin-top: 8px;
      }
      .stat-row {
        display: flex; align-items: center; gap: 8px;
        font-size: 13px; color: var(--text);
      }
      .stat-dot {
        width: 8px; height: 8px; border-radius: 50%; flex-shrink: 0;
      }
      .dot-accent { background: var(--accent); }
      .dot-blue { background: #60a5fa; }
      .dot-green { background: #4ade80; }
      .stat-count {
        margin-left: auto; font-variant-numeric: tabular-nums;
        color: var(--text-secondary);
      }
      .stat-total {
        margin-top: 8px; font-size: 12px; color: var(--text-tertiary);
      }

      /* Danger button */
      .btn-danger {
        display: block; width: 100%; height: 36px; margin-top: 16px;
        background: rgba(239, 68, 68, 0.1); border: 1px solid rgba(239, 68, 68, 0.15);
        border-radius: 10px; color: var(--error);
        font-size: 13px; font-family: inherit; cursor: pointer;
        transition: background 150ms, border-color 150ms;
      }
      .btn-danger:hover {
        background: rgba(239, 68, 68, 0.18);
        border-color: rgba(239, 68, 68, 0.25);
      }

      /* Stats cards */
      .stats-cards {
        display: flex; flex-direction: column; gap: 10px;
        margin-bottom: 16px;
      }
      .stats-card {
        display: flex; align-items: center; gap: 12px;
        padding: 12px; background: var(--glass);
        border: 1px solid var(--glass-border);
        border-radius: 12px;
      }
      .stats-icon {
        width: 36px; height: 36px; border-radius: 10px;
        display: flex; align-items: center; justify-content: center;
        flex-shrink: 0;
      }
      .stats-icon.purple { background: rgba(70, 120, 190, 0.15); color: var(--accent); }
      .stats-icon.blue { background: rgba(96, 165, 250, 0.15); color: #60a5fa; }
      .stats-icon.green { background: rgba(74, 222, 128, 0.15); color: #4ade80; }
      .stats-icon.orange { background: rgba(251, 146, 60, 0.15); color: #fb923c; }
      .stats-value {
        font-size: 18px; font-weight: 600; color: var(--text);
        font-variant-numeric: tabular-nums;
      }
      .stats-label {
        font-size: 11px; color: var(--text-tertiary);
      }
      .tool-table {
        width: 100%; border-collapse: collapse; margin-top: 8px;
      }
      .tool-table th, .tool-table td {
        padding: 6px 8px; text-align: left;
        font-size: 12px; border-bottom: 1px solid var(--border);
      }
      .tool-table th {
        color: var(--text-tertiary); font-weight: 500;
      }
      .tool-table td {
        color: var(--text);
      }
      .tool-table td:last-child {
        text-align: right; font-variant-numeric: tabular-nums;
        color: var(--text-secondary);
      }
      .empty-state {
        text-align: center; padding: 24px 16px;
        color: var(--text-tertiary); font-size: 12px;
      }

      /* Confirm dialog */
      .confirm-scrim {
        position: fixed; inset: 0;
        background: rgba(0, 0, 0, 0.5);
        display: flex; align-items: center; justify-content: center;
        z-index: 100;
      }
      .confirm-card {
        background: rgba(210, 205, 222, 0.95); border: 1px solid var(--glass-border);
        border-radius: 16px; padding: 20px;
        max-width: 280px; width: 90%;
        box-shadow: 0 8px 32px rgba(0,0,0,0.15);
      }
      .confirm-title {
        font-size: 14px; font-weight: 600;
        color: var(--text); margin-bottom: 8px;
      }
      .confirm-desc {
        font-size: 12px; color: var(--text-secondary);
        line-height: 1.5; margin-bottom: 16px;
      }
      .confirm-actions {
        display: flex; gap: 8px; justify-content: flex-end;
      }

      /* Tool catalog */
      .tool-group {
        margin-bottom: 14px; border: 1px solid var(--glass-border);
        border-radius: 12px; overflow: hidden;
        background: var(--glass);
      }
      .tool-group-header {
        display: flex; align-items: center; gap: 10px;
        padding: 10px 12px; background: var(--glass-strong, var(--glass));
        cursor: pointer;
        user-select: none;
      }
      .tool-group.expanded .tool-group-header {
        border-bottom: 1px solid var(--glass-border);
      }
      .tool-group-chevron {
        width: 14px; height: 14px; flex-shrink: 0;
        color: var(--text-tertiary);
        transform: rotate(0deg);
        transition: transform 0.15s ease;
      }
      .tool-group.expanded .tool-group-chevron {
        transform: rotate(90deg);
      }
      .tool-group-label {
        font-size: 13px; font-weight: 600; color: var(--text);
        flex: 1; min-width: 0;
      }
      .tool-group-count {
        font-size: 11px; color: var(--text-tertiary);
        font-variant-numeric: tabular-nums;
      }
      .tool-group-checkbox {
        width: 16px; height: 16px; flex-shrink: 0;
        accent-color: var(--accent);
        cursor: pointer;
      }
      .tool-list {
        display: flex; flex-direction: column;
      }
      .tool-row {
        display: flex; align-items: flex-start; gap: 10px;
        padding: 8px 12px; border-top: 1px solid var(--border);
      }
      .tool-row:first-child { border-top: none; }
      .tool-checkbox {
        width: 14px; height: 14px; margin-top: 2px; flex-shrink: 0;
        accent-color: var(--accent); cursor: pointer;
      }
      .tool-meta {
        display: flex; flex-direction: column; gap: 2px;
        flex: 1; min-width: 0;
      }
      .tool-name {
        font-size: 12px; color: var(--text);
        font-family: ui-monospace, 'SF Mono', Menlo, Monaco, monospace;
      }
      .tool-desc {
        font-size: 11px; color: var(--text-tertiary);
        line-height: 1.4; word-break: break-word;
      }
    `;
  }

  private boundOnToolConfigChanged_: (() => void) | null = null;

  constructor() {
    super();
    this.activeSubTab_ = 'general';
    this.provider_ = 'openai-compatible';
    this.apiKey_ = '';
    this.baseUrl_ = 'https://api.openai.com/v1';
    this.model_ = 'gpt-5';
    this.soulText_ = '';
    this.saveStatusText_ = '';
    this.saveStatusVisible_ = false;
    this.memoryEnabled_ = false;
    this.proactiveEnabled_ = true;
    this.pageContextEnabled_ = true;
    this.conversationEnabled_ = true;
    this.threshold_ = 'balanced';
    this.statConversations_ = 0;
    this.statPreferences_ = 0;
    this.statEpisodes_ = 0;
    this.statTotal_ = 'Total: 0 KB';
    this.showConfirmDialog_ = false;
    this.agentStats_ = null;
    this.showResetStatsDialog_ = false;
    this.toolCallShowDetails_ = false;
    this.resumeLastSession_ = true;
  }


  override connectedCallback() {
    super.connectedCallback();
    this.loadSettings_();
    this.loadMemorySettings_();

    soulChannel.addEventListener('message', () => {
      refreshSoulContent();
      this.soulText_ = currentSoulContent;
    });

    // Another agent tab (or our own toggles) changed the tool config —
    // re-render so the checkboxes reflect the new state.
    this.boundOnToolConfigChanged_ = () => this.requestUpdate();
    toolConfigChannel.addEventListener(
        'message', this.boundOnToolConfigChanged_);
    window.addEventListener(
        'dao-tool-config-changed', this.boundOnToolConfigChanged_);
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    if (this.boundOnToolConfigChanged_) {
      toolConfigChannel.removeEventListener(
          'message', this.boundOnToolConfigChanged_);
      window.removeEventListener(
          'dao-tool-config-changed', this.boundOnToolConfigChanged_);
      this.boundOnToolConfigChanged_ = null;
    }
  }

  switchSubTab(tab: string) {
    this.activeSubTab_ = tab;
    if (tab === 'soul') {
      refreshSoulContent();
      this.soulText_ = currentSoulContent;
    } else if (tab === 'memory') {
      this.loadStorageStats_();
    } else if (tab === 'stats') {
      this.agentStats_ = getAgentStats();
    }
  }

  override render() {
    return html`
      <div class="settings-sub-tabs">
        ${['general', 'soul', 'tools', 'memory', 'skills', 'stats'].map(tab => html`
          <button class="sub-tab ${this.activeSubTab_ === tab ? 'active' : ''}"
              @click=${() => this.switchSubTab(tab)}>
            ${tab.charAt(0).toUpperCase() + tab.slice(1)}
          </button>`)}
      </div>
      ${this.activeSubTab_ === 'general' ? this.renderConnection_() :
        this.activeSubTab_ === 'soul' ? this.renderSoul_() :
        this.activeSubTab_ === 'tools' ? this.renderTools_() :
        this.activeSubTab_ === 'skills' ? this.renderSkills_() :
        this.activeSubTab_ === 'stats' ? this.renderStats_() :
        this.renderMemory_()}
      ${this.showConfirmDialog_ ? this.renderConfirmDialog_() : nothing}
      ${this.showResetStatsDialog_ ? this.renderResetStatsDialog_() : nothing}
    `;
  }

  private renderConnection_() {
    const spec = this.getProviderSpec_(this.provider_);
    return html`
      <div class="panel">
        <div class="section-title">API Connection</div>
        <div class="section-desc">
          Configure the LLM provider for the agent. Each provider keeps
          its own credentials.</div>
        <label>Provider</label>
        <select .value=${this.provider_}
            @change=${(e: Event) =>
                this.onProviderChange_(
                    (e.target as HTMLSelectElement).value)}>
          ${LLM_PROVIDERS.map(p => html`
            <option value=${p.id}
                ?selected=${p.id === this.provider_}>${p.label}</option>`)}
        </select>
        <label>API Key</label>
        <input type="password" .value=${this.apiKey_}
            placeholder=${spec.apiKeyPlaceholder}
            @change=${(e: Event) => this.onApiKeyChange_(
                (e.target as HTMLInputElement).value)}>
        ${spec.needsBaseUrl ? html`
          <label>Base URL</label>
          <input type="text" .value=${this.baseUrl_}
              placeholder=${spec.defaultBaseUrl ?? ''}
              @change=${(e: Event) => this.onBaseUrlChange_(
                  (e.target as HTMLInputElement).value)}>` : nothing}
        <label>Model</label>
        <input type="text" .value=${this.model_}
            placeholder=${spec.defaultModel}
            @change=${(e: Event) => this.onModelChange_(
                (e.target as HTMLInputElement).value)}>

        <div class="section-title" style="margin-top:18px">Display</div>
        ${this.renderToggle_(
            'Show Tool Call Details',
            'Expand every tool call input / output by default',
            this.toolCallShowDetails_, (v) => {
              this.toolCallShowDetails_ = v;
              localStorage.setItem(
                  'dao_tool_call_show_details', String(v));
            })}

        <div class="section-title" style="margin-top:18px">Session</div>
        ${this.renderToggle_(
            'Resume Last Session',
            'Reopen the most recent conversation when the agent panel opens',
            this.resumeLastSession_, (v) => {
              this.resumeLastSession_ = v;
              localStorage.setItem(
                  'dao_resume_last_session', String(v));
            })}
      </div>`;
  }

  private getProviderSpec_(id: string): ProviderSpec {
    return LLM_PROVIDERS.find(p => p.id === id) ?? LLM_PROVIDERS[0]!;
  }

  private onProviderChange_(id: string) {
    if (!LLM_PROVIDERS.some(p => p.id === id)) return;
    setActiveProvider(id);
    this.provider_ = id;
    const cfg = getProviderConfig(id);
    this.apiKey_ = cfg.apiKey;
    this.baseUrl_ = cfg.baseUrl;
    this.model_ = cfg.model;
    this.notifyConfigChanged_();
  }

  private onApiKeyChange_(value: string) {
    this.apiKey_ = value;
    setProviderConfig(this.provider_, {apiKey: value});
    this.notifyConfigChanged_();
  }

  private onBaseUrlChange_(value: string) {
    this.baseUrl_ = value;
    setProviderConfig(this.provider_, {baseUrl: value});
    this.notifyConfigChanged_();
  }

  private onModelChange_(value: string) {
    this.model_ = value;
    setProviderConfig(this.provider_, {model: value});
    this.notifyConfigChanged_();
  }

  private notifyConfigChanged_() {
    window.dispatchEvent(new Event('llm-config-changed'));
  }

  private renderSoul_() {
    return html`
      <div class="panel">
        <div class="section-title">Soul Prompt</div>
        <div class="section-desc">Define the AI agent's personality and
          behavior. This is used as the system prompt in every
          conversation.</div>
        <textarea class="soul-editor"
            placeholder="Enter your soul prompt here..."
            .value=${this.soulText_}
            @input=${(e: Event) =>
              this.soulText_ = (e.target as HTMLTextAreaElement).value}
        ></textarea>
        <div class="soul-actions">
          <button class="btn-primary" @click=${this.saveSoul_}>Save</button>
          <button class="btn-secondary"
              @click=${this.resetSoul_}>Reset to Default</button>
          <span class="save-status ${this.saveStatusVisible_ ?
              'visible' : ''}">${this.saveStatusText_}</span>
        </div>
      </div>`;
  }

  private renderMemory_() {
    const thresholds = ['quiet', 'balanced', 'active'] as const;
    return html`
      <div class="panel">
        <div class="section-title">Memory</div>
        <div class="section-desc">Control how the Agent remembers and
          learns from your interactions.</div>

        ${this.renderToggle_(
            'Enable Memory', 'Master switch for all memory features',
            this.memoryEnabled_, (v) => {
              this.memoryEnabled_ = v;
              callNativeArgs('setMemoryEnabled', v).catch(() => {});
            })}
        ${this.renderToggle_(
            'Proactive Suggestions',
            'Show tips based on your browsing context',
            this.proactiveEnabled_, (v) => {
              this.proactiveEnabled_ = v;
              localStorage.setItem(
                  'dao_proactive_enabled', String(v));
              callNativeArgs('setProactiveEnabled', v).catch(() => {});
            })}

        <div class="setting-group">
          <span class="toggle-name">Suggestion Threshold</span>
          <div class="segment-selector" role="radiogroup"
              aria-label="Suggestion threshold"
              @keydown=${this.onSegmentKeydown_}>
            ${thresholds.map(t => html`
              <button class="segment ${this.threshold_ === t ? 'active' : ''}"
                  role="radio"
                  aria-checked=${String(this.threshold_ === t)}
                  @click=${() => this.setThreshold_(t)}>
                ${t.charAt(0).toUpperCase() + t.slice(1)}
              </button>`)}
          </div>
        </div>

        ${this.renderToggle_(
            'Page Context Memory',
            'Remember interactions on specific pages',
            this.pageContextEnabled_, (v) => {
              this.pageContextEnabled_ = v;
              localStorage.setItem(
                  'dao_page_context_enabled', String(v));
            })}
        ${this.renderToggle_(
            'Conversation History',
            'Save chat history across sessions',
            this.conversationEnabled_, (v) => {
              this.conversationEnabled_ = v;
              localStorage.setItem(
                  'dao_conversation_enabled', String(v));
            })}

        <div class="memory-stats">
          <div class="section-title">Memory Usage</div>
          <div class="stats-list" role="list">
            <div class="stat-row" role="listitem">
              <span class="stat-dot dot-accent"></span>
              <span>Conversations</span>
              <span class="stat-count">${this.statConversations_}</span>
            </div>
            <div class="stat-row" role="listitem">
              <span class="stat-dot dot-blue"></span>
              <span>Preferences</span>
              <span class="stat-count">${this.statPreferences_}</span>
            </div>
            <div class="stat-row" role="listitem">
              <span class="stat-dot dot-green"></span>
              <span>Episodes</span>
              <span class="stat-count">${this.statEpisodes_}</span>
            </div>
          </div>
          <div class="stat-total">${this.statTotal_}</div>
        </div>

        <button class="btn-danger"
            @click=${() => this.showConfirmDialog_ = true}>
          Clear All Memory
        </button>
      </div>`;
  }

  private renderToggle_(
      name: string, desc: string, checked: boolean,
      onChange: (val: boolean) => void) {
    return html`
      <div class="toggle-row">
        <div class="toggle-label">
          <span class="toggle-name">${name}</span>
          <span class="toggle-desc">${desc}</span>
        </div>
        <label class="toggle" role="switch" aria-label="${name}">
          <input type="checkbox" .checked=${checked}
              @change=${(e: Event) =>
                onChange((e.target as HTMLInputElement).checked)}>
          <span class="toggle-track"></span>
        </label>
      </div>`;
  }

  private renderConfirmDialog_() {
    return html`
      <div class="confirm-scrim" role="alertdialog"
          @click=${(e: Event) => {
            if (e.target === e.currentTarget) {
              this.showConfirmDialog_ = false;
            }
          }}>
        <div class="confirm-card">
          <div class="confirm-title">Clear All Memory?</div>
          <div class="confirm-desc">This will permanently erase all
            conversations, preferences, and page episodes. This action
            cannot be undone.</div>
          <div class="confirm-actions">
            <button class="btn-secondary"
                @click=${() => this.showConfirmDialog_ = false}>
              Cancel</button>
            <button class="btn-danger"
                @click=${this.clearAllMemory_}>Clear All</button>
          </div>
        </div>
      </div>`;
  }

  // ---- Stats ----

  private renderStats_() {
    const s = this.agentStats_ || getAgentStats();
    const toolEntries = Object.entries(s.toolCalls)
        .sort((a, b) => b[1] - a[1]);
    const totalToolCalls = toolEntries.reduce((sum, [, c]) => sum + c, 0);
    const resetDate = new Date(s.lastReset);
    const sinceStr = resetDate.toLocaleDateString(undefined, {
      month: 'short', day: 'numeric', year: 'numeric',
    });

    // SVG icons for stats cards
    const apiIcon = html`<svg width="18" height="18" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round">
      <path d="M21 12a9 9 0 0 1-9 9m9-9a9 9 0 0 0-9-9m9 9H3m9 9a9 9 0 0 1-9-9m9 9c1.66 0 3-4.03 3-9s-1.34-9-3-9m0 18c-1.66 0-3-4.03-3-9s1.34-9 3-9m-9 9a9 9 0 0 1 9-9"/></svg>`;
    const toolIcon = html`<svg width="18" height="18" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round">
      <path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z"/></svg>`;
    const tokenIcon = html`<svg width="18" height="18" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round">
      <path d="M20 12V8H6a2 2 0 0 1-2-2c0-1.1.9-2 2-2h12v4"/>
      <path d="M4 6v12c0 1.1.9 2 2 2h14v-4"/>
      <path d="M18 12a2 2 0 0 0 0 4h4v-4z"/></svg>`;
    const costIcon = html`<svg width="18" height="18" viewBox="0 0 24 24"
        fill="none" stroke="currentColor" stroke-width="2"
        stroke-linecap="round" stroke-linejoin="round">
      <line x1="12" y1="1" x2="12" y2="23"/>
      <path d="M17 5H9.5a3.5 3.5 0 0 0 0 7h5a3.5 3.5 0 0 1 0 7H6"/></svg>`;

    return html`
      <div class="panel">
        <div class="section-title">Agent Statistics</div>
        <div class="section-desc">
          Usage since ${sinceStr}</div>

        <div class="stats-cards">
          <div class="stats-card">
            <div class="stats-icon purple">${apiIcon}</div>
            <div>
              <div class="stats-value">${s.apiCalls}</div>
              <div class="stats-label">API Calls</div>
            </div>
          </div>
          <div class="stats-card">
            <div class="stats-icon blue">${toolIcon}</div>
            <div>
              <div class="stats-value">${totalToolCalls}</div>
              <div class="stats-label">Tool Calls</div>
            </div>
          </div>
          <div class="stats-card">
            <div class="stats-icon green">${tokenIcon}</div>
            <div>
              <div class="stats-value">${this.formatNumber_(s.totalTokens)}</div>
              <div class="stats-label">Total Tokens (${this.formatNumber_(s.promptTokens)} in / ${this.formatNumber_(s.completionTokens)} out)</div>
            </div>
          </div>
          <div class="stats-card">
            <div class="stats-icon orange">${costIcon}</div>
            <div>
              <div class="stats-value">$${s.estimatedCost.toFixed(4)}</div>
              <div class="stats-label">Estimated Cost</div>
            </div>
          </div>
        </div>

        <div class="section-title">Tool Breakdown</div>
        ${toolEntries.length > 0 ? html`
          <table class="tool-table">
            <thead>
              <tr><th>Tool</th><th>Calls</th></tr>
            </thead>
            <tbody>
              ${toolEntries.map(([name, count]) => html`
                <tr><td>${name}</td><td>${count}</td></tr>`)}
            </tbody>
          </table>` :
          html`<div class="empty-state">No tool calls recorded yet.</div>`}

        <button class="btn-danger"
            @click=${() => this.showResetStatsDialog_ = true}>
          Reset Statistics
        </button>
      </div>`;
  }

  private formatNumber_(n: number): string {
    if (n >= 1_000_000) return (n / 1_000_000).toFixed(1) + 'M';
    if (n >= 1_000) return (n / 1_000).toFixed(1) + 'K';
    return String(n);
  }

  private renderResetStatsDialog_() {
    return html`
      <div class="confirm-scrim" role="alertdialog"
          @click=${(e: Event) => {
            if (e.target === e.currentTarget) {
              this.showResetStatsDialog_ = false;
            }
          }}>
        <div class="confirm-card">
          <div class="confirm-title">Reset Statistics?</div>
          <div class="confirm-desc">This will clear all recorded API calls,
            tool usage counts, token usage, and cost data. This action
            cannot be undone.</div>
          <div class="confirm-actions">
            <button class="btn-secondary"
                @click=${() => this.showResetStatsDialog_ = false}>
              Cancel</button>
            <button class="btn-danger"
                @click=${this.resetStats_}>Reset</button>
          </div>
        </div>
      </div>`;
  }

  private resetStats_() {
    this.showResetStatsDialog_ = false;
    resetAgentStats();
    this.agentStats_ = getAgentStats();
    this.fireToast_('Statistics reset');
  }

  // ---- Settings Persistence ----

  private loadSettings_() {
    this.provider_ = getActiveProvider();
    const cfg = getProviderConfig(this.provider_);
    this.apiKey_ = cfg.apiKey;
    this.baseUrl_ = cfg.baseUrl;
    this.model_ = cfg.model;
    this.soulText_ = currentSoulContent;
    this.toolCallShowDetails_ =
        localStorage.getItem('dao_tool_call_show_details') === 'true';
    this.resumeLastSession_ =
        localStorage.getItem('dao_resume_last_session') !== 'false';
  }

  private loadMemorySettings_() {
    this.memoryEnabled_ = false;
    this.proactiveEnabled_ =
        localStorage.getItem('dao_proactive_enabled') !== 'false';
    this.pageContextEnabled_ =
        localStorage.getItem('dao_page_context_enabled') !== 'false';
    this.conversationEnabled_ =
        localStorage.getItem('dao_conversation_enabled') !== 'false';
    this.threshold_ =
        localStorage.getItem('dao_proactive_threshold') || 'balanced';

    callNativeArgs('getMemoryEnabled').then(enabled => {
      this.memoryEnabled_ = !!enabled;
    }).catch(() => {});

    callNativeArgs(
        'setProactiveEnabled', this.proactiveEnabled_).catch(() => {});
    callNativeArgs(
        'setConfidenceThreshold',
        CONFIDENCE_THRESHOLD_MAP[this.threshold_] ?? 0.7).catch(() => {});
  }

  // ---- Soul ----

  private saveSoul_() {
    saveSoul(this.soulText_);
    this.showSaveStatus_('Saved');
  }

  private resetSoul_() {
    this.soulText_ = DEFAULT_SOUL;
    saveSoul(DEFAULT_SOUL);
    this.showSaveStatus_('Reset to default');
  }

  private showSaveStatus_(text: string) {
    this.saveStatusText_ = text;
    this.saveStatusVisible_ = true;
    clearTimeout(this.saveStatusTimer_);
    this.saveStatusTimer_ = window.setTimeout(() => {
      this.saveStatusVisible_ = false;
    }, 2000);
  }

  // ---- Threshold ----

  private setThreshold_(value: string) {
    this.threshold_ = value;
    localStorage.setItem('dao_proactive_threshold', value);
    callNativeArgs(
        'setConfidenceThreshold',
        CONFIDENCE_THRESHOLD_MAP[value] ?? 0.7).catch(() => {});
  }

  private onSegmentKeydown_(e: KeyboardEvent) {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    const values = ['quiet', 'balanced', 'active'];
    const idx = values.indexOf(this.threshold_);
    const next = e.key === 'ArrowRight'
        ? Math.min(idx + 1, 2) : Math.max(idx - 1, 0);
    this.setThreshold_(values[next]!);
  }

  // ---- Memory ----

  private async loadStorageStats_() {
    try {
      const stats = await callNativeArgs('getStorageStats') as {
        totalSize?: number; conversationCount?: number;
        episodeCount?: number; preferenceCount?: number;
      };
      this.statConversations_ = stats.conversationCount || 0;
      this.statPreferences_ = stats.preferenceCount || 0;
      this.statEpisodes_ = stats.episodeCount || 0;
      const kb = ((stats.totalSize || 0) / 1024).toFixed(1);
      this.statTotal_ = 'Total: ' + kb + ' KB';
    } catch (_) { /* non-critical */ }
  }

  private async clearAllMemory_() {
    this.showConfirmDialog_ = false;
    try {
      await callNativeArgs('clearAllMemory');
      this.fireToast_('All memory cleared');
      this.loadStorageStats_();
    } catch (_) {
      this.fireToast_('Failed to clear memory');
    }
  }

  // ---- Tools ----

  private renderTools_() {
    return html`
      <div class="panel">
        <div class="section-title">Tools</div>
        <div class="section-desc">
          Choose which tools the agent can call. Disabled tools are hidden
          from the model, so it will not attempt to use them. Changes take
          effect on the next turn of any open chat.</div>
        ${TOOL_GROUPS.map(group => this.renderToolGroup_(group.id))}
      </div>`;
  }

  private renderToolGroup_(groupId: string) {
    const group = TOOL_GROUPS.find(g => g.id === groupId);
    if (!group) return nothing;
    const state = getGroupState(groupId);
    const counts = countEnabled(groupId);
    const allChecked = state === 'all';
    const indeterminate = state === 'some';
    const expanded = isGroupExpanded(groupId);

    const onHeaderToggle = () => {
      setGroupExpanded(groupId, !expanded);
      this.requestUpdate();
    };

    return html`
      <div class="tool-group ${expanded ? 'expanded' : ''}">
        <div class="tool-group-header" @click=${onHeaderToggle}>
          <svg class="tool-group-chevron" viewBox="0 0 24 24" fill="none"
              stroke="currentColor" stroke-width="2" stroke-linecap="round"
              stroke-linejoin="round" aria-hidden="true">
            <path d="m9 18 6-6-6-6"/>
          </svg>
          <input type="checkbox" class="tool-group-checkbox"
              .checked=${allChecked}
              .indeterminate=${indeterminate}
              aria-label="Toggle all ${group.label} tools"
              @click=${(e: Event) => e.stopPropagation()}
              @change=${(e: Event) => setGroupEnabled(
                  groupId, (e.target as HTMLInputElement).checked)}>
          <span class="tool-group-label">${group.label}</span>
          <span class="tool-group-count">
            ${counts.enabled}/${counts.total}
          </span>
        </div>
        ${expanded ? html`
          <div class="tool-list">
            ${group.toolNames.map(name => this.renderToolRow_(name))}
          </div>` : nothing}
      </div>`;
  }

  private renderToolRow_(name: string) {
    const def = allTools.find(t => t.function.name === name);
    const desc = def?.function.description ?? '';
    const enabled = isToolEnabled(name);
    return html`
      <label class="tool-row">
        <input type="checkbox" class="tool-checkbox"
            .checked=${enabled}
            aria-label="Toggle ${name}"
            @change=${(e: Event) => setToolEnabled(
                name, (e.target as HTMLInputElement).checked)}>
        <div class="tool-meta">
          <span class="tool-name">${name}</span>
          ${desc ? html`<span class="tool-desc">${desc}</span>` : nothing}
        </div>
      </label>`;
  }

  // ---- Skills ----

  private renderSkills_() {
    return html`
      <div class="panel">
        <div class="section-title">Skills</div>
        <div class="section-desc">
          Manage slash-command skills for the agent. Open the Skill Manager
          to create, edit, and delete skills in a full-page editor.</div>
        <button class="btn-primary"
            style="width: 100%; height: 36px; margin-top: 8px"
            @click=${this.openSkillManager_}>
          Open Skill Manager
        </button>
        <button class="btn-secondary"
            style="width: 100%; margin-top: 8px"
            @click=${() => chrome.send('openSkillsDirectory', [])}>
          Open Skills Directory
        </button>
      </div>`;
  }

  private openSkillManager_() {
    chrome.send('openSkillManager', []);
  }

  private fireToast_(text: string) {
    this.dispatchEvent(new CustomEvent('show-toast', {
      bubbles: true, composed: true, detail: {text},
    }));
  }
}

customElements.define('dao-settings-view', DaoSettingsView);
