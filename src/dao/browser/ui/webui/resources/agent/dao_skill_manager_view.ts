// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, css, html, nothing} from
    '//resources/lit/v3_0/lit.rollup.js';

import {
  deleteUserSkill,
  getAllSkills,
  initSkillRegistry,
  loadSkillInstructions,
  saveUserSkill,
} from './skill_registry.js';
import type {SkillRegistryEntry} from './skill_registry.js';

export class DaoSkillManagerView extends CrLitElement {
  static override get properties() {
    return {
      skillList_: {type: Array, state: true},
      selectedSkillId_: {type: String, state: true},
      editorContent_: {type: String, state: true},
      editorReadonly_: {type: Boolean, state: true},
      isNewSkill_: {type: Boolean, state: true},
      showDeleteConfirm_: {type: Boolean, state: true},
      saveStatusText_: {type: String, state: true},
      saveStatusVisible_: {type: Boolean, state: true},
    };
  }

  declare private skillList_: SkillRegistryEntry[];
  declare private selectedSkillId_: string;
  declare private editorContent_: string;
  declare private editorReadonly_: boolean;
  declare private isNewSkill_: boolean;
  declare private showDeleteConfirm_: boolean;
  declare private saveStatusText_: string;
  declare private saveStatusVisible_: boolean;
  private saveStatusTimer_ = 0;

  static override get styles() {
    return css`
      :host {
        display: flex;
        flex-direction: column;
        height: 100%;
        overflow: hidden;
        --list-width: 280px;
        color: var(--text);
      }

      /* ---- Toolbar ---- */
      .toolbar {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 10px 16px;
        border-bottom: 1px solid var(--border);
        flex-shrink: 0;
      }
      .toolbar-title {
        font-size: 14px;
        font-weight: 600;
        color: var(--text);
        flex: 1;
      }
      .btn-primary {
        padding: 5px 14px;
        background: var(--accent);
        border: none;
        border-radius: 8px;
        color: white;
        font-size: 12px;
        font-family: inherit;
        cursor: pointer;
      }
      .btn-primary:hover { filter: brightness(1.15); }
      .btn-secondary {
        padding: 5px 14px;
        background: var(--surface);
        border: 1px solid var(--border);
        border-radius: 8px;
        color: var(--text-secondary);
        font-size: 12px;
        font-family: inherit;
        cursor: pointer;
      }
      .btn-secondary:hover {
        background: var(--surface-hover);
        color: var(--text);
      }

      /* ---- Two‑panel body ---- */
      .body {
        display: flex;
        flex: 1;
        overflow: hidden;
      }

      /* ---- Left: skill list ---- */
      .list-panel {
        width: var(--list-width);
        flex-shrink: 0;
        border-right: 1px solid var(--border);
        overflow-y: auto;
        display: flex;
        flex-direction: column;
      }
      .list-panel::-webkit-scrollbar { width: 4px; }
      .list-panel::-webkit-scrollbar-track { background: transparent; }
      .list-panel::-webkit-scrollbar-thumb {
        background: rgba(0, 0, 0, 0.15);
        border-radius: 2px;
      }

      @media (prefers-color-scheme: dark) {
        .list-panel::-webkit-scrollbar-thumb {
          background: rgba(255, 255, 255, 0.18);
        }
      }

      .skill-item {
        display: flex;
        flex-direction: column;
        gap: 2px;
        padding: 10px 14px;
        cursor: pointer;
        transition: background 0.12s;
        border-bottom: 1px solid var(--border);
      }
      .skill-item:hover { background: var(--surface-hover); }
      .skill-item.selected {
        background: var(--accent-dim);
        border-left: 3px solid var(--accent);
        padding-left: 11px;
      }
      .skill-item-header {
        display: flex;
        align-items: center;
        gap: 6px;
      }
      .skill-name {
        font-size: 13px;
        color: var(--text);
        font-weight: 500;
      }
      .skill-badge {
        font-size: 10px;
        padding: 1px 5px;
        border-radius: 4px;
        color: var(--text-tertiary);
        background: var(--surface);
      }
      .skill-badge.builtin {
        color: var(--accent);
        background: var(--accent-dim);
      }
      .skill-host-badge {
        font-size: 10px;
        padding: 1px 5px;
        border-radius: 4px;
        color: var(--text-tertiary);
        background: var(--surface);
        margin-left: auto;
      }
      .skill-desc {
        font-size: 11px;
        color: var(--text-tertiary);
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
      }
      .list-empty {
        padding: 24px;
        text-align: center;
        font-size: 12px;
        color: var(--text-tertiary);
      }

      /* ---- Right: editor ---- */
      .editor-panel {
        flex: 1;
        display: flex;
        flex-direction: column;
        overflow: hidden;
      }
      .editor-placeholder {
        flex: 1;
        display: flex;
        align-items: center;
        justify-content: center;
        color: var(--text-tertiary);
        font-size: 13px;
      }
      .editor-toolbar {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 8px 16px;
        border-bottom: 1px solid var(--border);
        flex-shrink: 0;
      }
      .editor-skill-name {
        font-size: 13px;
        font-weight: 600;
        color: var(--text);
        flex: 1;
      }
      .save-status {
        font-size: 11px;
        color: var(--accent);
        opacity: 0;
        transition: opacity 0.3s;
      }
      .save-status.visible { opacity: 1; }
      .btn-danger-small {
        padding: 5px 14px;
        background: rgba(239, 68, 68, 0.15);
        border: none;
        border-radius: 8px;
        color: var(--error);
        font-size: 12px;
        font-family: inherit;
        cursor: pointer;
        transition: background 0.15s;
      }
      .btn-danger-small:hover {
        background: rgba(239, 68, 68, 0.25);
      }
      textarea.editor {
        flex: 1;
        width: 100%;
        padding: 16px 20px;
        background: transparent;
        border: none;
        color: var(--text);
        font-family: ui-monospace, 'SF Mono', Menlo, Monaco, monospace;
        font-size: 13px;
        line-height: 1.6;
        resize: none;
        outline: none;
      }
      textarea.editor:disabled {
        opacity: 0.6;
        cursor: not-allowed;
      }
      textarea.editor::placeholder {
        color: var(--text-tertiary);
      }

      /* ---- Confirm dialog ---- */
      .confirm-scrim {
        position: fixed;
        inset: 0;
        background: rgba(0, 0, 0, 0.5);
        display: flex;
        align-items: center;
        justify-content: center;
        z-index: 100;
      }
      .confirm-card {
        background: var(--bg);
        border: 1px solid var(--border);
        border-radius: var(--radius);
        padding: 20px;
        max-width: 320px;
        width: 90%;
      }
      .confirm-title {
        font-size: 14px;
        font-weight: 600;
        color: var(--text);
        margin-bottom: 8px;
      }
      .confirm-desc {
        font-size: 12px;
        color: var(--text-secondary);
        line-height: 1.5;
        margin-bottom: 16px;
      }
      .confirm-actions {
        display: flex;
        gap: 8px;
        justify-content: flex-end;
      }
      .btn-danger {
        padding: 6px 16px;
        background: rgba(239, 68, 68, 0.15);
        border: none;
        border-radius: 8px;
        color: var(--error);
        font-size: 12px;
        font-family: inherit;
        cursor: pointer;
      }
      .btn-danger:hover {
        background: rgba(239, 68, 68, 0.25);
      }
    `;
  }

  constructor() {
    super();
    this.selectedSkillId_ = '';
    this.editorContent_ = '';
    this.editorReadonly_ = false;
    this.isNewSkill_ = false;
    this.showDeleteConfirm_ = false;
    this.saveStatusText_ = '';
    this.saveStatusVisible_ = false;
    this.skillList_ = [];
  }

  override connectedCallback() {
    super.connectedCallback();
    this.loadSkillList_();
  }

  override render() {
    const selected = this.skillList_.find(
        s => s.id === this.selectedSkillId_);
    const showEditor = !!selected || this.isNewSkill_;

    return html`
      <div class="toolbar">
        <span class="toolbar-title">Skill Manager</span>
        <button class="btn-secondary"
            @click=${this.openSkillsDirectory_}>Open Directory</button>
        <button class="btn-primary"
            @click=${this.createNewSkill_}>+ New Skill</button>
      </div>
      <div class="body">
        <div class="list-panel">
          ${this.skillList_.length === 0 ? html`
            <div class="list-empty">No skills found</div>` :
            this.skillList_.map(s => this.renderSkillItem_(s))}
        </div>
        <div class="editor-panel">
          ${showEditor ? this.renderEditor_(selected) :
              html`<div class="editor-placeholder">
                Select a skill or create a new one</div>`}
        </div>
      </div>
      ${this.showDeleteConfirm_ ? this.renderDeleteConfirm_() : nothing}
    `;
  }

  private renderSkillItem_(s: SkillRegistryEntry) {
    return html`
      <div class="skill-item ${
          this.selectedSkillId_ === s.id ? 'selected' : ''}"
          @click=${() => this.selectSkill_(s.id)}>
        <div class="skill-item-header">
          <span class="skill-name">/${s.name}</span>
          <span class="skill-badge ${
              s.source === 'builtin' ? 'builtin' : ''}">${s.source}</span>
          ${s.hosts && s.hosts.length > 0 && !s.hosts.includes('*') ? html`
            <span class="skill-host-badge">${s.hosts[0]}</span>` : nothing}
        </div>
        <span class="skill-desc">${s.description}</span>
      </div>`;
  }

  private renderEditor_(selected: SkillRegistryEntry|undefined) {
    const isUser = selected?.source === 'user';
    const editorTitle = this.isNewSkill_ ? 'New Skill' :
        (selected ? '/' + selected.name : '');

    return html`
      <div class="editor-toolbar">
        <span class="editor-skill-name">${editorTitle}</span>
        <span class="save-status ${this.saveStatusVisible_ ?
            'visible' : ''}">${this.saveStatusText_}</span>
        ${!this.editorReadonly_ ? html`
          <button class="btn-primary"
              @click=${this.saveSkill_}>Save</button>` : nothing}
        ${isUser ? html`
          <button class="btn-danger-small"
              @click=${() => this.showDeleteConfirm_ = true}>Delete</button>`
            : nothing}
        ${this.editorReadonly_ ? html`
          <span style="font-size: 11px; color: var(--text-tertiary)">
            Read-only</span>` : nothing}
      </div>
      <textarea class="editor"
          ?disabled=${this.editorReadonly_}
          placeholder="Enter SKILL.md content..."
          .value=${this.editorContent_}
          @input=${(e: Event) =>
            this.editorContent_ =
                (e.target as HTMLTextAreaElement).value}
      ></textarea>
    `;
  }

  private renderDeleteConfirm_() {
    return html`
      <div class="confirm-scrim" role="alertdialog"
          @click=${(e: Event) => {
            if (e.target === e.currentTarget) {
              this.showDeleteConfirm_ = false;
            }
          }}>
        <div class="confirm-card">
          <div class="confirm-title">Delete Skill?</div>
          <div class="confirm-desc">
            This will permanently delete the skill
            "${this.selectedSkillId_}". This action cannot be undone.</div>
          <div class="confirm-actions">
            <button class="btn-secondary"
                @click=${() => this.showDeleteConfirm_ = false}>
              Cancel</button>
            <button class="btn-danger"
                @click=${this.deleteSkill_}>Delete</button>
          </div>
        </div>
      </div>`;
  }

  // ---- Data ----

  private async loadSkillList_() {
    await initSkillRegistry();
    this.skillList_ = getAllSkills();
  }

  private async selectSkill_(id: string) {
    this.selectedSkillId_ = id;
    this.isNewSkill_ = false;
    const content = await loadSkillInstructions(id);
    if (content) {
      this.editorReadonly_ = content.metadata.source === 'builtin';
      const meta = content.metadata;
      let frontmatter = '---\n';
      frontmatter += 'name: ' + meta.name + '\n';
      frontmatter += 'description: ' + meta.description + '\n';
      if (meta.hosts && meta.hosts.length > 0) {
        frontmatter += 'hosts:\n';
        for (const h of meta.hosts) {
          frontmatter += '  - "' + h + '"\n';
        }
      }
      if (meta.requiresPageContent) {
        frontmatter += 'requiresPageContent: true\n';
      }
      frontmatter += '---\n';
      this.editorContent_ = frontmatter + '\n' + content.instructions;
    } else {
      this.editorContent_ = '';
      this.editorReadonly_ = true;
    }
  }

  private createNewSkill_() {
    this.selectedSkillId_ = '';
    this.isNewSkill_ = true;
    this.editorReadonly_ = false;
    this.editorContent_ = `---
name: my-new-skill
description: Describe what this skill does
hosts:
  - "*"
requiresPageContent: false
---

# My New Skill

## Instructions
1. First step...
2. Second step...
`;
  }

  private async saveSkill_() {
    const content = this.editorContent_;

    // For existing skills, use the selected ID; for new skills, parse from
    // frontmatter name field.
    let skillId = this.selectedSkillId_;
    if (this.isNewSkill_ || !skillId) {
      const nameMatch = content.match(/^name:\s*(.+)$/m);
      if (!nameMatch) {
        this.fireToast_('Missing "name" in frontmatter');
        return;
      }
      skillId = nameMatch[1]!.trim();
    }

    const hostMatch = content.match(/^\s*-\s*"?([^"*\n]+)"?\s*$/m);
    const host = hostMatch ? hostMatch[1]!.trim() : '';

    const ok = await saveUserSkill(skillId, content, host);
    if (ok) {
      this.isNewSkill_ = false;
      this.selectedSkillId_ = skillId;
      this.showSaveStatus_('Saved');
      await this.loadSkillList_();
    } else {
      this.fireToast_('Failed to save skill');
    }
  }

  private async deleteSkill_() {
    this.showDeleteConfirm_ = false;
    const ok = await deleteUserSkill(this.selectedSkillId_);
    if (ok) {
      this.selectedSkillId_ = '';
      this.editorContent_ = '';
      this.fireToast_('Skill deleted');
      await this.loadSkillList_();
    } else {
      this.fireToast_('Failed to delete skill');
    }
  }

  private openSkillsDirectory_() {
    chrome.send('openSkillsDirectory', []);
  }

  private showSaveStatus_(text: string) {
    this.saveStatusText_ = text;
    this.saveStatusVisible_ = true;
    clearTimeout(this.saveStatusTimer_);
    this.saveStatusTimer_ = window.setTimeout(() => {
      this.saveStatusVisible_ = false;
    }, 2000);
  }

  private fireToast_(text: string) {
    this.dispatchEvent(new CustomEvent('show-toast', {
      bubbles: true, composed: true, detail: {text},
    }));
  }
}

customElements.define('dao-skill-manager-view', DaoSkillManagerView);
