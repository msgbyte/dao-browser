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
  refreshSoulContent,
  saveSoul,
  soulChannel,
} from './agent_bridge.js';

export class DaoSettingsView extends CrLitElement {
  static override get properties() {
    return {
      activeSubTab_: {type: String, state: true},
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
    };
  }

  private activeSubTab_ = 'connection';
  private apiKey_ = '';
  private baseUrl_ = 'https://api.openai.com/v1';
  private model_ = 'gpt-5';
  private soulText_ = '';
  private saveStatusText_ = '';
  private saveStatusVisible_ = false;
  private memoryEnabled_ = false;
  private proactiveEnabled_ = true;
  private pageContextEnabled_ = true;
  private conversationEnabled_ = true;
  private threshold_ = 'balanced';
  private statConversations_ = 0;
  private statPreferences_ = 0;
  private statEpisodes_ = 0;
  private statTotal_ = 'Total: 0 KB';
  private showConfirmDialog_ = false;
  private saveStatusTimer_ = 0;

  static override get styles() {
    return css`
      :host {
        display: flex; flex-direction: column;
        flex: 1; overflow: hidden;
      }
      :host([hidden]) { display: none !important; }

      .settings-sub-tabs {
        display: flex; gap: 2px; padding: 6px 14px 0;
        border-bottom: 1px solid var(--border); flex-shrink: 0;
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
        flex: 1; overflow-y: auto; padding: 14px;
      }
      .panel::-webkit-scrollbar { width: 4px; }
      .panel::-webkit-scrollbar-track { background: transparent; }
      .panel::-webkit-scrollbar-thumb {
        background: rgba(0,0,0,0.15); border-radius: 2px;
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
        width: 100%; padding: 6px 8px;
        background: var(--surface); border: 1px solid var(--border);
        border-radius: 8px; color: var(--text);
        font-size: 12px; outline: none;
      }
      input:focus { border-color: var(--accent); }

      /* Soul editor */
      .soul-editor {
        width: 100%; min-height: 300px; padding: 10px 12px;
        background: var(--surface); border: 1px solid var(--border);
        border-radius: 8px; color: var(--text);
        font-family: ui-monospace, 'SF Mono', Menlo, Monaco, monospace;
        font-size: 12px; line-height: 1.5;
        resize: vertical; outline: none;
      }
      .soul-editor:focus { border-color: var(--accent); }
      .soul-editor::placeholder { color: var(--text-tertiary); }
      .soul-actions {
        display: flex; align-items: center; gap: 8px; margin-top: 10px;
      }
      .btn-primary {
        padding: 6px 16px; background: var(--accent); border: none;
        border-radius: 8px; color: white;
        font-size: 12px; font-family: inherit; cursor: pointer;
      }
      .btn-primary:hover { filter: brightness(1.15); }
      .btn-secondary {
        padding: 6px 16px; background: var(--surface);
        border: 1px solid var(--border); border-radius: 8px;
        color: var(--text-secondary);
        font-size: 12px; font-family: inherit; cursor: pointer;
      }
      .btn-secondary:hover {
        background: var(--surface-hover); color: var(--text);
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
        background: var(--surface); border: 1px solid var(--border);
        border-radius: 10px; transition: background 150ms, border-color 150ms;
      }
      .toggle-track::after {
        content: ''; position: absolute; top: 2px; left: 2px;
        width: 14px; height: 14px; background: white;
        border-radius: 50%; transition: transform 150ms;
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
        display: flex; gap: 0; margin-top: 8px;
        background: var(--surface); border-radius: 8px; overflow: hidden;
      }
      .segment {
        flex: 1; height: 28px; background: transparent;
        border: none; font-size: 12px; font-family: inherit;
        color: var(--text-secondary); cursor: pointer;
        transition: background 150ms, color 150ms;
      }
      .segment:hover { background: var(--surface-hover); }
      .segment.active { background: var(--accent); color: white; }

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
        background: rgba(239, 68, 68, 0.15); border: none;
        border-radius: 8px; color: var(--error);
        font-size: 13px; font-family: inherit; cursor: pointer;
        transition: background 150ms;
      }
      .btn-danger:hover { background: rgba(239, 68, 68, 0.25); }

      /* Confirm dialog */
      .confirm-scrim {
        position: fixed; inset: 0;
        background: rgba(0, 0, 0, 0.5);
        display: flex; align-items: center; justify-content: center;
        z-index: 100;
      }
      .confirm-card {
        background: var(--bg); border: 1px solid var(--border);
        border-radius: var(--radius); padding: 20px;
        max-width: 280px; width: 90%;
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
    `;
  }

  override connectedCallback() {
    super.connectedCallback();
    this.loadSettings_();
    this.loadMemorySettings_();

    soulChannel.addEventListener('message', () => {
      refreshSoulContent();
      this.soulText_ = currentSoulContent;
    });
  }

  switchSubTab(tab: string) {
    this.activeSubTab_ = tab;
    if (tab === 'soul') {
      refreshSoulContent();
      this.soulText_ = currentSoulContent;
    } else if (tab === 'memory') {
      this.loadStorageStats_();
    }
  }

  override render() {
    return html`
      <div class="settings-sub-tabs">
        ${['connection', 'soul', 'memory', 'skills'].map(tab => html`
          <button class="sub-tab ${this.activeSubTab_ === tab ? 'active' : ''}"
              @click=${() => this.switchSubTab(tab)}>
            ${tab.charAt(0).toUpperCase() + tab.slice(1)}
          </button>`)}
      </div>
      ${this.activeSubTab_ === 'connection' ? this.renderConnection_() :
        this.activeSubTab_ === 'soul' ? this.renderSoul_() :
        this.activeSubTab_ === 'skills' ? this.renderSkills_() :
        this.renderMemory_()}
      ${this.showConfirmDialog_ ? this.renderConfirmDialog_() : nothing}
    `;
  }

  private renderConnection_() {
    return html`
      <div class="panel">
        <div class="section-title">API Connection</div>
        <div class="section-desc">
          Configure the LLM API endpoint for the agent.</div>
        <label>API Key</label>
        <input type="password" .value=${this.apiKey_}
            placeholder="sk-..."
            @change=${(e: Event) => this.saveSetting_(
                'dao_agent_api_key',
                (e.target as HTMLInputElement).value,
                v => this.apiKey_ = v)}>
        <label>Base URL</label>
        <input type="text" .value=${this.baseUrl_}
            placeholder="https://api.openai.com/v1"
            @change=${(e: Event) => this.saveSetting_(
                'dao_agent_base_url',
                (e.target as HTMLInputElement).value,
                v => this.baseUrl_ = v)}>
        <label>Model</label>
        <input type="text" .value=${this.model_}
            placeholder="gpt-5"
            @change=${(e: Event) => this.saveSetting_(
                'dao_agent_model',
                (e.target as HTMLInputElement).value,
                v => this.model_ = v)}>
      </div>`;
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

  // ---- Settings Persistence ----

  private loadSettings_() {
    this.apiKey_ = localStorage.getItem('dao_agent_api_key') || '';
    this.baseUrl_ = localStorage.getItem('dao_agent_base_url') ||
        'https://api.openai.com/v1';
    this.model_ = localStorage.getItem('dao_agent_model') || 'gpt-5';
    this.soulText_ = currentSoulContent;
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

  private saveSetting_(
      key: string, value: string, setter: (v: string) => void) {
    setter(value);
    localStorage.setItem(key, value);
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
