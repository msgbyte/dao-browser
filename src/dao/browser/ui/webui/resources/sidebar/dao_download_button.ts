// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative, addListener} from './sidebar_bridge.js';
import type {
  RecentFileData, ActiveDownloadData, DownloadState
} from './sidebar_bridge.js';

export class DaoDownloadButton extends CrLitElement {
  static get is() {
    return 'dao-download-button';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
        padding: 0 6px 6px;
      }

      .container {
        display: flex;
        flex-direction: column;
        gap: 4px;
      }

      .file-list {
        max-height: 0;
        overflow: hidden;
        transition: max-height 0.15s ease-out;
      }

      :host(.expanded) .file-list {
        max-height: 300px;
      }

      .file-item {
        display: flex;
        align-items: center;
        height: 48px;
        padding: 0 10px;
        gap: 8px;
        border-radius: 8px;
        cursor: default;
        transition: background 0.1s ease;
      }

      .file-item:hover {
        background: var(--ink-drop);
      }

      .file-icon {
        width: 40px;
        height: 40px;
        flex-shrink: 0;
        border-radius: 4px;
        object-fit: cover;
      }

      .file-icon.placeholder {
        background: var(--surface);
        display: flex;
        align-items: center;
        justify-content: center;
        color: var(--text-muted);
      }

      .file-name {
        flex: 1;
        min-width: 0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        font-size: 11px;
        color: var(--text-secondary);
      }

      .active-downloads {
        display: flex;
        flex-direction: column;
      }

      .active-item {
        display: flex;
        align-items: center;
        height: 40px;
        padding: 0 10px 0 6px;
        gap: 6px;
      }

      .active-icon {
        width: 16px;
        height: 16px;
        flex-shrink: 0;
        color: rgb(100, 180, 255);
      }

      .active-mid {
        flex: 1;
        min-width: 0;
        display: flex;
        flex-direction: column;
        gap: 2px;
      }

      .active-name {
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        font-size: 11px;
        color: var(--text-secondary);
      }

      .progress-bar {
        height: 3px;
        background: rgba(0,0,0,0.10);
        border-radius: 1.5px;
        overflow: hidden;
      }

      .progress-fill {
        height: 100%;
        background: rgb(100, 180, 255);
        border-radius: 1.5px;
        transition: width 0.3s ease;
      }

      .active-speed {
        font-size: 10px;
        color: var(--text-muted);
        white-space: nowrap;
        flex-shrink: 0;
      }

      .cancel-btn {
        width: 16px;
        height: 16px;
        border: none;
        background: transparent;
        color: var(--text-muted);
        cursor: default;
        padding: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        flex-shrink: 0;
      }

      .cancel-btn:hover {
        color: var(--text-primary);
      }

      .download-btn {
        display: flex;
        align-items: center;
        justify-content: center;
        width: 32px;
        height: 32px;
        border: none;
        border-radius: 8px;
        background: transparent;
        color: var(--text-secondary);
        cursor: default;
        transition: background 0.15s ease;
      }

      .download-btn:hover {
        background: rgba(0,0,0,0.08);
      }
    `;
  }

  static override get properties() {
    return {
      recentFiles_: {type: Array},
      activeDownloads_: {type: Array},
    };
  }

  protected recentFiles_: RecentFileData[] = [];
  protected activeDownloads_: ActiveDownloadData[] = [];

  private dragFileIndex_: number = -1;
  private dragStartX_: number = 0;
  private dragStartY_: number = 0;
  private readonly DRAG_THRESHOLD_ = 5;

  override connectedCallback() {
    super.connectedCallback();

    addListener('downloadStateChanged', (...args: unknown[]) => {
      const state = args[0] as DownloadState;
      this.recentFiles_ = state.recentFiles;
      this.activeDownloads_ = state.activeDownloads;
    });

    addListener('activeDownloadsChanged', (...args: unknown[]) => {
      const downloads = args[0] as ActiveDownloadData[];
      this.activeDownloads_ = downloads;
    });

    this.addEventListener('mouseenter', this.onMouseEnter_.bind(this));
    this.addEventListener('mouseleave', this.onMouseLeave_.bind(this));
  }

  override render() {
    return html`
      <div class="container">
        <div class="file-list">
          ${this.recentFiles_.map(file => html`
            <div class="file-item"
                 @mousedown=${(e: MouseEvent) =>
                     this.onFileMouseDown_(e, file.index)}>
              ${file.iconUrl
                ? html`<img class="file-icon" src=${file.iconUrl} alt="">`
                : html`<div class="file-icon placeholder">
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none"
                         stroke="currentColor" stroke-width="2"
                         stroke-linecap="round" stroke-linejoin="round">
                      <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path>
                      <polyline points="14 2 14 8 20 8"></polyline>
                    </svg>
                  </div>`}
              <span class="file-name">${file.name}</span>
            </div>
          `)}
        </div>

        ${this.activeDownloads_.length > 0 ? html`
          <div class="active-downloads">
            ${this.activeDownloads_.map(dl => html`
              <div class="active-item">
                <svg class="active-icon" viewBox="0 0 24 24" fill="none"
                     stroke="currentColor" stroke-width="2"
                     stroke-linecap="round" stroke-linejoin="round">
                  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                  <polyline points="7 10 12 15 17 10"></polyline>
                  <line x1="12" y1="15" x2="12" y2="3"></line>
                </svg>
                <div class="active-mid">
                  <span class="active-name">${dl.name}</span>
                  <div class="progress-bar">
                    <div class="progress-fill"
                         style="width: ${Math.max(0, dl.percent)}%"></div>
                  </div>
                </div>
                <span class="active-speed">${dl.speed}</span>
                <button class="cancel-btn" title="Cancel"
                        @click=${() => this.onCancelDownload_(dl.id)}>
                  <svg width="12" height="12" viewBox="0 0 24 24" fill="none"
                       stroke="currentColor" stroke-width="2"
                       stroke-linecap="round" stroke-linejoin="round">
                    <line x1="18" y1="6" x2="6" y2="18"></line>
                    <line x1="6" y1="6" x2="18" y2="18"></line>
                  </svg>
                </button>
              </div>
            `)}
          </div>
        ` : ''}

        <button class="download-btn" title="Downloads"
                @click=${this.onButtonClick_}>
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none"
               stroke="currentColor" stroke-width="2"
               stroke-linecap="round" stroke-linejoin="round">
            <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
            <polyline points="7 10 12 15 17 10"></polyline>
            <line x1="12" y1="15" x2="12" y2="3"></line>
          </svg>
        </button>
      </div>
    `;
  }

  private onMouseEnter_() {
    this.classList.add('expanded');
    sendNative('requestDownloadState');
  }

  private onMouseLeave_() {
    this.classList.remove('expanded');
  }

  private onButtonClick_() {
    sendNative('openDownloadsFolder');
  }

  private onFileMouseDown_(e: MouseEvent, index: number) {
    // Only handle left button.
    if (e.button !== 0) return;
    e.preventDefault();

    this.dragFileIndex_ = index;
    this.dragStartX_ = e.clientX;
    this.dragStartY_ = e.clientY;

    const onMouseMove = (me: MouseEvent) => {
      const dx = me.clientX - this.dragStartX_;
      const dy = me.clientY - this.dragStartY_;
      if (Math.sqrt(dx * dx + dy * dy) > this.DRAG_THRESHOLD_) {
        document.removeEventListener('mousemove', onMouseMove);
        document.removeEventListener('mouseup', onMouseUp);
        // Tell C++ to initiate a native file drag.
        sendNative('startFileDrag', this.dragFileIndex_);
        this.dragFileIndex_ = -1;
      }
    };

    const onMouseUp = () => {
      document.removeEventListener('mousemove', onMouseMove);
      document.removeEventListener('mouseup', onMouseUp);
      if (this.dragFileIndex_ >= 0) {
        // No drag happened — treat as click.
        sendNative('openRecentFile', this.dragFileIndex_);
        this.dragFileIndex_ = -1;
      }
    };

    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup', onMouseUp);
  }

  private onCancelDownload_(id: number) {
    sendNative('cancelDownload', id);
  }
}

customElements.define('dao-download-button', DaoDownloadButton);
