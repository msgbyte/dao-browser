// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {
  SIDEBAR_POINTER_EXITED_EVENT,
  sendNative,
  TAB_DRAG_MIME_TYPE,
  TAB_DRAG_PREFIX,
} from './sidebar_bridge.js';
import type {TabData} from './sidebar_bridge.js';

export class DaoTabItem extends CrLitElement {
  static get is() {
    return 'dao-tab-item';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
      }

      .tab-row {
        display: flex;
        align-items: center;
        height: 36px;
        padding: 0 10px;
        border-radius: var(--radius-tab, 12px);
        cursor: default;
        gap: 8px;
        position: relative;
        transition: background 0.12s ease, box-shadow 0.12s ease;
      }

      .tab-row:hover {
        background: var(--ink-drop-strong, rgba(0, 0, 0, 0.06));
      }

      /* Active state — explicitly defined for both hover and non-hover to
       * defend against CSS specificity edge cases and ensure the active
       * row never reverts to the hover background. The accent-tinted
       * hairline ring gives the active tab a clear boundary so users can
       * distinguish it from the surrounding sidebar even when the
       * translucent white surface blends into a light background. */
      :host([active]) .tab-row,
      :host([active]) .tab-row:hover {
        background: var(--surface-active);
        box-shadow: 0 0 0 0.5px rgba(70, 120, 190, 0.24),
                    0 1px 3px rgba(0, 0, 0, 0.10),
                    0 1px 2px rgba(0, 0, 0, 0.06);
      }

      @media (prefers-color-scheme: dark) {
        .tab-row:hover {
          background: var(--ink-drop-strong, rgba(255, 255, 255, 0.10));
        }
        :host([active]) .tab-row,
        :host([active]) .tab-row:hover {
          box-shadow: 0 0 0 0.5px rgba(255, 255, 255, 0.14),
                      0 1px 3px rgba(0, 0, 0, 0.30),
                      0 1px 2px rgba(0, 0, 0, 0.18);
        }
      }

      .favicon {
        width: 16px;
        height: 16px;
        flex-shrink: 0;
        border-radius: 2px;
        position: relative;
      }

      .favicon img {
        width: 16px;
        height: 16px;
        border-radius: 2px;
      }

      .favicon img.light-icon {
        filter: invert(1);
      }

      .favicon .placeholder {
        width: 16px;
        height: 16px;
        border-radius: 2px;
        background: var(--surface);
      }

      .agent-lock-dot {
        position: absolute;
        bottom: -2px;
        right: -2px;
        width: 6px;
        height: 6px;
        border-radius: 50%;
        background: var(--accent);
        animation: agent-lock-pulse 1.2s ease-in-out infinite;
      }

      @keyframes agent-lock-pulse {
        0%, 100% { opacity: 0.5; }
        50% { opacity: 1.0; }
      }

      .title {
        flex: 1;
        min-width: 0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        font-size: 13px;
        color: var(--text-primary);
      }

      /* Reaffirm title color under active state. Defensive: keeps text
       * legible even if a stale CSS variable from a media-query transition
       * briefly leaks (e.g. system theme auto-switch) and prevents
       * "white-on-white" symptoms on the near-white active surface. */
      :host([active]) .title {
        color: var(--text-primary);
        font-weight: 500;
      }

      .audio-btn {
        width: 16px;
        height: 16px;
        border: none;
        background: transparent;
        color: var(--text-secondary);
        cursor: default;
        padding: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        flex-shrink: 0;
      }

      .close-btn {
        width: 20px;
        height: 20px;
        border: none;
        background: transparent;
        color: var(--text-muted);
        cursor: default;
        padding: 0;
        opacity: 0;
        flex-shrink: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        border-radius: 6px;
        transition: opacity 0.1s ease, background 0.12s ease, color 0.12s ease;
      }

      .tab-row:hover .close-btn {
        opacity: 1;
      }

      .close-btn:hover {
        background: var(--ink-drop);
        color: var(--text-primary);
      }

      :host([hover-suppressed]) .tab-row:hover {
        background: transparent;
      }

      :host([active][hover-suppressed]) .tab-row:hover {
        background: var(--surface-active);
      }

      :host([hover-suppressed]) .tab-row:hover .close-btn {
        opacity: 0;
      }

      :host([hover-suppressed]) .close-btn:hover {
        background: transparent;
        color: var(--text-muted);
      }

    `;
  }

  static override get properties() {
    return {
      tabData: {type: Object},
      active: {type: Boolean, reflect: true},
      sessionId: {type: Number},
      autoScrollToken: {type: Number},
      suppressActiveAutoScroll: {type: Boolean},
      hoverSuppressed_: {
        type: Boolean,
        reflect: true,
        attribute: 'hover-suppressed',
      },
    };
  }

  declare tabData: TabData;
  declare active: boolean;
  declare sessionId: number;
  declare autoScrollToken: number;
  declare suppressActiveAutoScroll: boolean;
  declare protected hoverSuppressed_: boolean;

  constructor() {
    super();
    this.tabData = {
      tabId: '',
      index: -1,
      title: '',
      url: '',
      faviconUrl: '',
      isActive: false,
      isPinned: false,
      isAudible: false,
      isMuted: false,
    };
    this.active = false;
    this.sessionId = 0;
    this.autoScrollToken = 0;
    this.suppressActiveAutoScroll = false;
    this.hoverSuppressed_ = false;
  }

  private tooltipTimer_: number = 0;
  private tooltipScheduled_: boolean = false;
  private tooltipVisible_: boolean = false;
  private lastMouseX_: number = 0;
  private lastMouseY_: number = 0;
  private boundSidebarPointerExited_ = () => this.onSidebarPointerExited_();

  override connectedCallback() {
    super.connectedCallback();
    window.addEventListener(
        SIDEBAR_POINTER_EXITED_EVENT, this.boundSidebarPointerExited_);
  }

  override disconnectedCallback() {
    window.removeEventListener(
        SIDEBAR_POINTER_EXITED_EVENT, this.boundSidebarPointerExited_);
    super.disconnectedCallback?.();
  }

  override render() {
    const tab = this.tabData;
    const showAudio = tab.isAudible || tab.isMuted;
    const displayTitle = tab.title || tab.url || 'New Tab';

    return html`
      <div class="tab-row"
           draggable="true"
           @click=${this.onActivate_}
           @dragstart=${this.onDragStart_}
           @contextmenu=${this.onContextMenu_}
           @mouseenter=${this.onShowTooltip_}
           @mousemove=${this.onTrackMouse_}
           @mouseleave=${this.onHideTooltip_}>
        <div class="favicon">
          ${tab.faviconUrl
              ? html`<img src=${tab.faviconUrl} alt=""
                          class=${tab.isFaviconLight ? 'light-icon' : ''}>`
              : html`<div class="placeholder"></div>`}
          ${tab.isAgentLocked
              ? html`<div class="agent-lock-dot"></div>`
              : ''}
        </div>
        ${showAudio && tab.isMuted ? html`
          <button class="audio-btn"
                  title="Unmute"
                  @click=${this.onToggleMute_}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round">
              <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"></polygon>
              <line x1="23" y1="9" x2="17" y2="15"></line>
              <line x1="17" y1="9" x2="23" y2="15"></line>
            </svg>
          </button>
        ` : ''}
        ${showAudio && !tab.isMuted ? html`
          <button class="audio-btn"
                  title="Mute"
                  @click=${this.onToggleMute_}>
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
                 stroke="currentColor" stroke-width="2"
                 stroke-linecap="round" stroke-linejoin="round">
              <polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"></polygon>
              <path d="M19.07 4.93a10 10 0 0 1 0 14.14"></path>
              <path d="M15.54 8.46a5 5 0 0 1 0 7.07"></path>
            </svg>
          </button>
        ` : ''}
        <span class="title">${displayTitle}</span>
        <button class="close-btn"
                @click=${this.onClose_}>
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none"
               stroke="currentColor" stroke-width="2"
               stroke-linecap="round" stroke-linejoin="round">
            <line x1="18" y1="6" x2="6" y2="18"></line>
            <line x1="6" y1="6" x2="18" y2="18"></line>
          </svg>
        </button>
      </div>
    `;
  }

  override updated(changedProperties: Map<PropertyKey, unknown>) {
    const activeScrollRequested =
        changedProperties.get('active') === false && this.active &&
        !this.suppressActiveAutoScroll;
    const tokenScrollRequested =
        changedProperties.has('autoScrollToken') && this.autoScrollToken > 0;
    if (activeScrollRequested || tokenScrollRequested) {
      this.scrollIntoView({block: 'nearest', behavior: 'smooth'});
    }
  }

  private onActivate_() {
    sendNative('activateTab', this.tabData.index);
  }

  private onClose_(e: Event) {
    e.stopPropagation();
    sendNative('closeTab', this.tabData.index);
  }

  private onToggleMute_(e: Event) {
    e.stopPropagation();
    sendNative('toggleMute', this.tabData.index);
  }

  private onDragStart_(e: DragEvent) {
    if (!e.dataTransfer) return;
    this.clearTooltip_(this.tooltipVisible_);
    const payload =
        `${TAB_DRAG_PREFIX}${this.sessionId}:${this.tabData.index}`;
    console.error('[Dao-Xwin-JS] dragstart: payload=' +
        JSON.stringify(payload) + ' sessionId=' + this.sessionId);
    e.dataTransfer.setData(TAB_DRAG_MIME_TYPE, payload);
    e.dataTransfer.setData('text/plain', payload);
    e.dataTransfer.effectAllowed = 'move';
  }

  private onTrackMouse_(e: MouseEvent) {
    this.setHoverSuppressed_(false);
    this.lastMouseX_ = e.screenX;
    this.lastMouseY_ = e.screenY;
    // Reset timer on every move: only show after the pointer settles.
    this.clearTooltip_(false);
    this.tooltipScheduled_ = true;
    this.tooltipTimer_ = window.setTimeout(() => {
      this.tooltipTimer_ = 0;
      this.tooltipScheduled_ = false;
      this.tooltipVisible_ = true;
      const title = this.tabData.title || this.tabData.url || 'New Tab';
      sendNative('showTabTooltip',
          this.lastMouseX_ + 4,
          this.lastMouseY_ + 4,
          title);
    }, 1500);
  }

  private onShowTooltip_(e: MouseEvent) {
    this.setHoverSuppressed_(false);
    this.lastMouseX_ = e.screenX;
    this.lastMouseY_ = e.screenY;
    this.clearTooltip_(false);
    this.tooltipScheduled_ = true;
    this.tooltipTimer_ = window.setTimeout(() => {
      this.tooltipTimer_ = 0;
      this.tooltipScheduled_ = false;
      this.tooltipVisible_ = true;
      const title = this.tabData.title || this.tabData.url || 'New Tab';
      sendNative('showTabTooltip',
          this.lastMouseX_ + 4,
          this.lastMouseY_ + 4,
          title);
    }, 1500);
  }

  private onHideTooltip_() {
    this.clearTooltip_(true);
  }

  private onSidebarPointerExited_() {
    this.setHoverSuppressed_(true);
    this.clearTooltip_(true);
  }

  private setHoverSuppressed_(suppressed: boolean) {
    this.hoverSuppressed_ = suppressed;
    this.toggleAttribute('hover-suppressed', suppressed);
  }

  private clearTooltip_(sendHide: boolean) {
    const shouldHide = sendHide &&
        (this.tooltipScheduled_ || this.tooltipVisible_);
    if (this.tooltipTimer_) {
      window.clearTimeout(this.tooltipTimer_);
      this.tooltipTimer_ = 0;
    }
    this.tooltipScheduled_ = false;
    this.tooltipVisible_ = false;
    if (shouldHide) {
      sendNative('hideTabTooltip');
    }
  }

  private onContextMenu_(e: MouseEvent) {
    e.preventDefault();
    e.stopPropagation();
    this.dispatchEvent(new CustomEvent('tab-context-menu', {
      bubbles: true,
      composed: true,
      detail: {
        index: this.tabData.index,
        screenX: e.screenX,
        screenY: e.screenY,
      },
    }));
  }

}

customElements.define('dao-tab-item', DaoTabItem);
