// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {loadTimeData} from '//resources/js/load_time_data.js';
import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative, addListener} from './sidebar_bridge.js';
import type {
  RecentFileData, ActiveDownloadData, DownloadState
} from './sidebar_bridge.js';

interface CompletedDownloadData {
  id: number;
  name: string;
}

export class DaoDownloadButton extends CrLitElement {
  static get is() {
    return 'dao-download-button';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
        position: relative;
        flex-shrink: 0;
      }

      /* Active downloads sit in normal flow so they take real layout space
       * and push the tab list up rather than overlapping it. */
      .active-downloads {
        display: flex;
        flex-direction: column;
        gap: 2px;
        padding: 0 6px;
        max-height: 180px;
        overflow-y: auto;
      }

      .active-item {
        display: flex;
        align-items: center;
        height: 40px;
        padding: 0 8px;
        gap: 6px;
        overflow: hidden;
        min-width: 0;
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
        background: rgba(0, 0, 0, 0.10);
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
        max-width: 70px;
        overflow: hidden;
        text-overflow: ellipsis;
      }

      .cancel-btn {
        width: 20px;
        height: 20px;
        border: none;
        background: transparent;
        color: var(--text-muted);
        cursor: default;
        padding: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        flex-shrink: 0;
        border-radius: 6px;
        transition: background 0.12s ease, color 0.12s ease;
      }

      .cancel-btn:hover {
        background: var(--ink-drop);
        color: var(--text-primary);
      }

      /* Bottom toolbar: download trigger on the left, slotted plus button
       * on the right. */
      .toolbar-row {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 0 6px 6px;
      }

      /* Small hover zone that also contains the recent-files popup, so the
       * cursor can bridge from the trigger into the popup without collapsing
       * it, and so hovering the active downloads above does not open it. */
      .trigger-zone {
        position: relative;
        display: flex;
        min-width: 0;
      }

      .popup-stack {
        position: absolute;
        bottom: 100%;
        left: -6px;
        width: 100vw;
        padding: 0 0 6px;
        box-sizing: border-box;
        pointer-events: none;
      }

      :host(.expanded) .popup-stack {
        pointer-events: auto;
      }

      .file-list {
        max-height: 0;
        overflow: hidden;
        transition: max-height 0.16s ease-out;
        border-radius: 10px;
        /* Opaque, slightly darkened backing so the floating list reads as a
         * raised panel and stays legible over the tab list behind it. */
        background:
          linear-gradient(180deg, rgba(0, 0, 0, 0.12),
                          rgba(0, 0, 0, 0.07) 60px),
          var(--sidebar-bg);
        box-shadow: 0 6px 20px rgba(0, 0, 0, 0.16);
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

      .download-btn {
        display: flex;
        align-items: center;
        justify-content: center;
        width: 26px;
        height: 26px;
        border: none;
        border-radius: 8px;
        background: transparent;
        color: var(--text-secondary);
        cursor: default;
        transition: background 0.15s ease;
      }

      .download-btn:hover {
        background: rgba(0, 0, 0, 0.08);
      }

      .completed-download {
        display: flex;
        align-items: center;
        min-width: 0;
        max-width: calc(100vw - 50px);
        height: 26px;
        border-radius: 8px;
        background: var(--ink-drop);
        overflow: hidden;
      }

      .completed-open,
      .completed-close {
        display: flex;
        align-items: center;
        border: none;
        background: transparent;
        color: var(--text-secondary);
        cursor: default;
      }

      .completed-open {
        min-width: 0;
        height: 26px;
        padding: 0 6px;
        gap: 5px;
      }

      .completed-open svg,
      .completed-close svg {
        flex-shrink: 0;
      }

      .completed-name {
        min-width: 0;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
        font-size: 11px;
      }

      .completed-close {
        width: 24px;
        height: 26px;
        padding: 0;
        justify-content: center;
        flex-shrink: 0;
      }

      .completed-open:hover,
      .completed-close:hover {
        background: rgba(0, 0, 0, 0.08);
      }

      @media (prefers-color-scheme: dark) {
        .file-list {
          background:
            linear-gradient(180deg, rgba(0, 0, 0, 0.30),
                            rgba(0, 0, 0, 0.20) 60px),
            var(--sidebar-bg);
        }
        .progress-bar {
          background: rgba(255, 255, 255, 0.12);
        }
        .download-btn:hover {
          background: rgba(255, 255, 255, 0.08);
        }
        .completed-open:hover,
        .completed-close:hover {
          background: rgba(255, 255, 255, 0.08);
        }
      }
    `;
  }

  static override get properties() {
    return {
      recentFiles_: {type: Array},
      activeDownloads_: {type: Array},
      completedDownload_: {type: Object},
    };
  }

  declare protected recentFiles_: RecentFileData[];
  declare protected activeDownloads_: ActiveDownloadData[];
  declare protected completedDownload_: CompletedDownloadData|null;

  private dragFileIndex_: number = -1;
  private dragStartX_: number = 0;
  private dragStartY_: number = 0;
  private readonly DRAG_THRESHOLD_ = 5;
  private tooltipTimer_: number = 0;
  private tooltipScheduled_: boolean = false;
  private tooltipVisible_: boolean = false;
  private hoveredDownloadId_: number = -1;
  private lastMouseX_: number = 0;
  private lastMouseY_: number = 0;
  private completedListOpenedByHover_: boolean = false;

  constructor() {
    super();
    this.recentFiles_ = [];
    this.activeDownloads_ = [];
    this.completedDownload_ = null;
  }

  override connectedCallback() {
    super.connectedCallback();

    addListener('downloadStateChanged', (...args: unknown[]) => {
      const state = args[0] as DownloadState;
      this.recentFiles_ = state.recentFiles;
      this.setActiveDownloads_(state.activeDownloads);
    });

    addListener('activeDownloadsChanged', (...args: unknown[]) => {
      const downloads = args[0] as ActiveDownloadData[];
      this.setActiveDownloads_(downloads);
    });

    addListener('downloadCompleted', (...args: unknown[]) => {
      this.completedDownload_ = args[0] as CompletedDownloadData;
      this.completedListOpenedByHover_ = false;
    });
  }

  override disconnectedCallback() {
    this.clearDownloadTooltip_(true);
    super.disconnectedCallback?.();
  }

  override render() {
    return html`
      ${this.activeDownloads_.length > 0 ? html`
        <div class="active-downloads">
          ${this.activeDownloads_.map(dl => html`
            <div class="active-item"
                 @mousemove=${(e: MouseEvent) =>
                     this.onDownloadMouseMove_(e, dl.id)}
                 @mouseleave=${() => this.onDownloadMouseLeave_()}>
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

      <div class="toolbar-row">
        <div class="trigger-zone"
             @mouseenter=${() => this.onMouseEnter_()}
             @mouseleave=${() => this.onMouseLeave_()}>
          <div class="popup-stack">
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
          </div>

          ${this.completedDownload_ ? html`
            <div class="completed-download">
              <button class="completed-open"
                      title=${this.completedDownload_.name}
                      @click=${() => this.onCompletedOpen_()}>
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                     stroke="currentColor" stroke-width="2"
                     stroke-linecap="round" stroke-linejoin="round">
                  <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                  <polyline points="7 10 12 15 17 10"></polyline>
                  <line x1="12" y1="15" x2="12" y2="3"></line>
                </svg>
                <span class="completed-name">${this.completedDownload_.name}</span>
              </button>
              <button class="completed-close"
                      title=${loadTimeData.getString(
                          'daoDismissCompletedDownload')}
                      aria-label=${loadTimeData.getString(
                          'daoDismissCompletedDownload')}
                      @click=${() => this.onCompletedClose_()}>
                <svg width="12" height="12" viewBox="0 0 24 24" fill="none"
                     stroke="currentColor" stroke-width="2"
                     stroke-linecap="round" stroke-linejoin="round">
                  <line x1="18" y1="6" x2="6" y2="18"></line>
                  <line x1="6" y1="6" x2="18" y2="18"></line>
                </svg>
              </button>
            </div>
          ` : html`
            <button class="download-btn" title="Downloads"
                    @click=${this.onButtonClick_}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                   stroke="currentColor" stroke-width="2"
                   stroke-linecap="round" stroke-linejoin="round">
                <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"></path>
                <polyline points="7 10 12 15 17 10"></polyline>
                <line x1="12" y1="15" x2="12" y2="3"></line>
              </svg>
            </button>
          `}
        </div>

        <slot name="toolbar-end"></slot>
      </div>
    `;
  }

  private onMouseEnter_() {
    if (this.completedDownload_) {
      this.completedListOpenedByHover_ = true;
    }
    this.classList.add('expanded');
    sendNative('requestDownloadState');
  }

  private onMouseLeave_() {
    this.classList.remove('expanded');
    if (this.completedListOpenedByHover_) {
      this.completedDownload_ = null;
      this.completedListOpenedByHover_ = false;
    }
  }

  private onButtonClick_() {
    sendNative('openDownloadsFolder');
  }

  private onCompletedOpen_() {
    if (!this.completedDownload_) {
      return;
    }
    sendNative('openDownload', this.completedDownload_.id);
    this.completedDownload_ = null;
    this.completedListOpenedByHover_ = false;
  }

  private onCompletedClose_() {
    this.completedDownload_ = null;
    this.completedListOpenedByHover_ = false;
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
    if (this.hoveredDownloadId_ === id) {
      this.clearDownloadTooltip_(true);
    }
    sendNative('cancelDownload', id);
  }

  private setActiveDownloads_(downloads: ActiveDownloadData[]) {
    if (this.hoveredDownloadId_ >= 0 &&
        !downloads.some(download => download.id === this.hoveredDownloadId_)) {
      this.clearDownloadTooltip_(true);
    }
    this.activeDownloads_ = downloads;
  }

  private onDownloadMouseMove_(e: MouseEvent, id: number) {
    if (this.tooltipTimer_) {
      window.clearTimeout(this.tooltipTimer_);
    }
    this.hoveredDownloadId_ = id;
    this.lastMouseX_ = e.screenX;
    this.lastMouseY_ = e.screenY;
    this.tooltipScheduled_ = true;
    this.tooltipTimer_ = window.setTimeout(() => {
      this.tooltipTimer_ = 0;
      this.tooltipScheduled_ = false;
      const download = this.activeDownloads_.find(item => item.id === id);
      if (!download || this.hoveredDownloadId_ !== id) {
        return;
      }
      this.tooltipVisible_ = true;
      sendNative(
          'showDownloadTooltip', this.lastMouseX_ + 4, this.lastMouseY_ + 4,
          download.name, download.sizeDetail, download.statusDetail);
    }, 400);
  }

  private onDownloadMouseLeave_() {
    this.clearDownloadTooltip_(true);
  }

  private clearDownloadTooltip_(sendHide: boolean) {
    const shouldHide = sendHide &&
        (this.tooltipScheduled_ || this.tooltipVisible_);
    if (this.tooltipTimer_) {
      window.clearTimeout(this.tooltipTimer_);
      this.tooltipTimer_ = 0;
    }
    this.tooltipScheduled_ = false;
    this.tooltipVisible_ = false;
    this.hoveredDownloadId_ = -1;
    if (shouldHide) {
      sendNative('hideTabTooltip');
    }
  }
}

customElements.define('dao-download-button', DaoDownloadButton);
