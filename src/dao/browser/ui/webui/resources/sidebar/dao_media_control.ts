// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative, addListener} from './sidebar_bridge.js';
import type {MediaPlaybackState} from './sidebar_bridge.js';

export class DaoMediaControl extends CrLitElement {
  static get is() {
    return 'dao-media-control';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
        flex-shrink: 0;
        padding: 0 4px;
      }

      .media-card {
        background: var(--surface-active);
        border-radius: 12px;
        padding: 0 8px;
        display: flex;
        flex-direction: column;
        box-shadow: 0 1px 4px rgba(0, 0, 0, 0.06),
                    0 0 0 0.5px rgba(0, 0, 0, 0.04);
        overflow: hidden;
      }

      .media-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 4px;
        max-height: 0;
        opacity: 0;
        overflow: hidden;
        transition: max-height 0.2s ease, opacity 0.15s ease,
                    padding 0.2s ease;
        padding: 0;
      }

      .media-card:hover .media-header {
        max-height: 28px;
        opacity: 1;
        padding: 6px 2px 0;
      }

      .source-title {
        font-size: 11px;
        font-weight: 500;
        color: var(--text-secondary);
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        min-width: 0;
        flex: 1;
        cursor: default;
      }

      .source-title:hover {
        color: var(--text-primary);
        text-decoration: underline;
      }

      .dismiss-btn {
        width: 18px;
        height: 18px;
        border: none;
        background: transparent;
        color: var(--text-muted);
        cursor: default;
        padding: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        border-radius: 4px;
        flex-shrink: 0;
        transition: color 0.1s ease;
      }

      .dismiss-btn:hover {
        color: var(--text-primary);
      }

      .media-body {
        display: flex;
        align-items: center;
        padding: 6px 0;
        align-items: center;
        flex: 1;
        justify-content: space-evenly;
      }

      .favicon {
        width: 16px;
        height: 16px;
        border-radius: 4px;
        flex-shrink: 0;
        object-fit: cover;
      }

      .favicon-placeholder {
        width: 16px;
        height: 16px;
        border-radius: 4px;
        flex-shrink: 0;
        background: var(--surface);
      }

      .ctrl-btn {
        width: 24px;
        height: 24px;
        border: none;
        background: transparent;
        color: var(--text-secondary);
        cursor: default;
        padding: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        border-radius: 6px;
        transition: background 0.12s ease, color 0.12s ease,
                    transform 0.1s ease;
      }

      .ctrl-btn:hover {
        background: rgba(0, 0, 0, 0.06);
        color: var(--text-primary);
      }

      .ctrl-btn:active {
        transform: scale(0.92);
      }

      .ctrl-btn:disabled {
        color: var(--text-muted);
        opacity: 0.35;
        pointer-events: none;
      }

      .ctrl-btn.play-pause {
        width: 24px;
        height: 24px;
        color: var(--text-primary);
      }
    `;
  }

  static override get properties() {
    return {
      mediaState_: {type: Object},
    };
  }

  declare protected mediaState_: MediaPlaybackState|null;

  constructor() {
    super();
    this.mediaState_ = null;
  }

  override connectedCallback() {
    super.connectedCallback();

    addListener('mediaPlaybackChanged', (...args: unknown[]) => {
      const state = args[0] as Record<string, unknown>;
      if (!state || (!state['tabIndex'] && state['tabIndex'] !== 0)) {
        this.mediaState_ = null;
      } else {
        this.mediaState_ = state as unknown as MediaPlaybackState;
      }
    });
  }

  override render() {
    if (!this.mediaState_) {
      return nothing;
    }

    const s = this.mediaState_;

    return html`
      <div class="media-card">
        <div class="media-header">
          <span class="source-title"
                @click=${this.onActivateTab_}>${s.title}</span>
          <button class="dismiss-btn" @click=${this.onDismiss_}>
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round">
              <path d="M18 6 6 18"></path>
              <path d="m6 6 12 12"></path>
            </svg>
          </button>
        </div>
        <div class="media-body">
          <button class="ctrl-btn" @click=${this.onActivateTab_}>
            ${s.faviconUrl
              ? html`<img class="favicon" src=${s.faviconUrl} alt="">`
              : html`<div class="favicon-placeholder"></div>`}
          </button>

          <button class="ctrl-btn" ?disabled=${!s.hasPrev}
                  @click=${this.onPrevious_}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                  stroke="currentColor" stroke-width="2"
                  stroke-linecap="round" stroke-linejoin="round">
              <path d="M17.971 4.285A2 2 0 0 1 21 6v12a2 2 0 0 1-3.029 1.715l-9.997-5.998a2 2 0 0 1-.003-3.432z"></path>
              <path d="M3 20V4"></path>
            </svg>
          </button>
          <button class="ctrl-btn play-pause" @click=${this.onPlayPause_}>
            ${s.isPlaying ? html`
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none"
                    stroke="currentColor" stroke-width="2"
                    stroke-linecap="round" stroke-linejoin="round">
                <rect x="14" y="3" width="5" height="18" rx="1"></rect>
                <rect x="5" y="3" width="5" height="18" rx="1"></rect>
              </svg>
            ` : html`
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none"
                    stroke="currentColor" stroke-width="2"
                    stroke-linecap="round" stroke-linejoin="round">
                <path d="M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z"></path>
              </svg>
            `}
          </button>
          <button class="ctrl-btn" ?disabled=${!s.hasNext}
                  @click=${this.onNext_}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                  stroke="currentColor" stroke-width="2"
                  stroke-linecap="round" stroke-linejoin="round">
              <path d="M21 4v16"></path>
              <path d="M6.029 4.285A2 2 0 0 0 3 6v12a2 2 0 0 0 3.029 1.715l9.997-5.998a2 2 0 0 0 .003-3.432z"></path>
            </svg>
          </button>
          <button class="ctrl-btn" @click=${this.onToggleMute_}>
            ${s.isMuted ? html`
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                    stroke="currentColor" stroke-width="2"
                    stroke-linecap="round" stroke-linejoin="round">
                <path d="M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z"></path>
                <line x1="22" x2="16" y1="9" y2="15"></line>
                <line x1="16" x2="22" y1="9" y2="15"></line>
              </svg>
            ` : html`
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                    stroke="currentColor" stroke-width="2"
                    stroke-linecap="round" stroke-linejoin="round">
                <path d="M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413l3.383 3.384A.705.705 0 0 0 11 19.298z"></path>
                <path d="M16 9a5 5 0 0 1 0 6"></path>
                <path d="M19.364 18.364a9 9 0 0 0 0-12.728"></path>
              </svg>
            `}
          </button>
        </div>
      </div>
    `;
  }

  private onPlayPause_() {
    sendNative('mediaPlayPause');
  }

  private onPrevious_() {
    sendNative('mediaPrevious');
  }

  private onNext_() {
    sendNative('mediaNext');
  }

  private onDismiss_(e: Event) {
    e.stopPropagation();
    sendNative('mediaDismiss');
  }

  private onActivateTab_() {
    sendNative('mediaActivateTab');
  }

  private onToggleMute_(e: Event) {
    e.stopPropagation();
    if (this.mediaState_) {
      sendNative('toggleMute', this.mediaState_.tabIndex);
    }
  }
}

customElements.define('dao-media-control', DaoMediaControl);
