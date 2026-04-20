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
      }

      .favicon {
        width: 24px;
        height: 24px;
        border-radius: 4px;
        flex-shrink: 0;
        object-fit: cover;
      }

      .favicon-placeholder {
        width: 24px;
        height: 24px;
        border-radius: 4px;
        flex-shrink: 0;
        background: var(--surface);
      }

      .controls {
        display: flex;
        align-items: center;
        flex: 1;
        justify-content: space-evenly;
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

      .ctrl-btn.disabled {
        color: var(--text-muted);
        opacity: 0.4;
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

  protected mediaState_: MediaPlaybackState|null = null;

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
    const canSkip = s.hasMediaSession;

    return html`
      <div class="media-card">
        <div class="media-header">
          <span class="source-title"
                @click=${this.onActivateTab_}>${s.title}</span>
          <button class="dismiss-btn" @click=${this.onDismiss_}>
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round">
              <line x1="18" y1="6" x2="6" y2="18"></line>
              <line x1="6" y1="6" x2="18" y2="18"></line>
            </svg>
          </button>
        </div>
        <div class="media-body">
          ${s.faviconUrl
            ? html`<img class="favicon" src=${s.faviconUrl} alt="">`
            : html`<div class="favicon-placeholder"></div>`}
          <div class="controls">
            <button class="ctrl-btn ${canSkip ? '' : 'disabled'}"
                    @click=${this.onPrevious_}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                   stroke="currentColor" stroke-width="2"
                   stroke-linecap="round" stroke-linejoin="round">
                <polygon points="19 20 9 12 19 4 19 20"></polygon>
                <line x1="5" y1="19" x2="5" y2="5"></line>
              </svg>
            </button>
            <button class="ctrl-btn play-pause" @click=${this.onPlayPause_}>
              ${s.isPlaying ? html`
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none"
                     stroke="currentColor" stroke-width="2.5"
                     stroke-linecap="round" stroke-linejoin="round">
                  <rect x="6" y="4" width="4" height="16"></rect>
                  <rect x="14" y="4" width="4" height="16"></rect>
                </svg>
              ` : html`
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none"
                     stroke="currentColor" stroke-width="2.5"
                     stroke-linecap="round" stroke-linejoin="round">
                  <polygon points="5 3 19 12 5 21 5 3"></polygon>
                </svg>
              `}
            </button>
            <button class="ctrl-btn ${canSkip ? '' : 'disabled'}"
                    @click=${this.onNext_}>
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                   stroke="currentColor" stroke-width="2"
                   stroke-linecap="round" stroke-linejoin="round">
                <polygon points="5 4 15 12 5 20 5 4"></polygon>
                <line x1="19" y1="5" x2="19" y2="19"></line>
              </svg>
            </button>
            <button class="ctrl-btn" @click=${this.onToggleMute_}>
              ${s.isMuted ? html`
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                     stroke="currentColor" stroke-width="2"
                     stroke-linecap="round" stroke-linejoin="round">
                  <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"></polygon>
                  <line x1="23" y1="9" x2="17" y2="15"></line>
                  <line x1="17" y1="9" x2="23" y2="15"></line>
                </svg>
              ` : html`
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                     stroke="currentColor" stroke-width="2"
                     stroke-linecap="round" stroke-linejoin="round">
                  <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"></polygon>
                  <path d="M19.07 4.93a10 10 0 0 1 0 14.14"></path>
                  <path d="M15.54 8.46a5 5 0 0 1 0 7.07"></path>
                </svg>
              `}
            </button>
          </div>
        </div>
      </div>
    `;
  }

  private onPlayPause_() {
    sendNative('mediaPlayPause');
  }

  private onPrevious_() {
    if (this.mediaState_?.hasMediaSession) {
      sendNative('mediaPrevious');
    }
  }

  private onNext_() {
    if (this.mediaState_?.hasMediaSession) {
      sendNative('mediaNext');
    }
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
