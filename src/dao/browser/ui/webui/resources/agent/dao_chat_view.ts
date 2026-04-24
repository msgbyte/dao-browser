// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PR3 Phase 1: thin wrapper around pi-web-ui's <pi-chat-panel>.
//
// Replaces the previous 1400-line hand-rolled chat view. The ChatPanel
// (from pi-web-ui) is driven by a pi-agent-core `Agent` that runs the
// turn loop, executes tools, and emits streaming events. We construct the
// Agent here with:
//   - systemPrompt: Dao's BASE_SYSTEM_PROMPT (+ soul, when we hook it back
//     in a later phase)
//   - model: resolved from Dao's llm_config via pi-ai's getModel() (or
//     built by hand for the `openai-compatible` path)
//   - tools: every existing Dao tool, wrapped by pi_tool_adapter
//
// Phase 1 deliberately drops: slash command menu, scenario / memory chips,
// soul channel live sync, API/tool call stats UI, and IndexedDB chat
// history persistence. These are overlays / external hooks that will be
// re-attached on top of ChatPanel in Phase 2.

import {CrLitElement, html} from '//resources/lit/v3_0/lit.rollup.js';

import {BASE_SYSTEM_PROMPT} from './agent_bridge.js';
import {compactAgentMessages, estimateMessagesTokens} from './dao_compact.js';
import {getActiveLLMConfig} from './llm_config.js';
import {buildPageAttachment, captureCurrentPageMarkdown, fetchCurrentPageInfo, isCapturablePageUrl, type PageInfo} from './dao_page_capture.js';
import {buildAgentTools} from './pi_tool_adapter.js';
import {ensurePiAppStorage, syncActiveKeyToPiStorage} from './pi_app_storage.js';
// eslint-disable-next-line @typescript-eslint/no-explicit-any
import * as pi from './vendor/pi_runtime_bundle.js';

// Structural types — the bundle ships `@ts-nocheck`, so imports above are
// all typed `any`. We name the small surface we actually touch.
interface PiAgent {
  state: {
    systemPrompt: string;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    model: any;
    thinkingLevel: 'off' | 'minimal' | 'low' | 'medium' | 'high' | 'xhigh';
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    tools: any[];
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    messages: any[];
    isStreaming: boolean;
  };
  getApiKey?: (provider: string) =>
      Promise<string | undefined> | string | undefined;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  subscribe(listener: (ev: any, signal: AbortSignal) => unknown):
      () => void;
  abort(): void;
  reset(): void;
  waitForIdle(): Promise<void>;
  // Re-runs the LLM given the current message list (last message must
  // not be `assistant`). Used by Dao's retry button after it pops the
  // trailing assistant/toolResult messages.
  continue(): Promise<void>;
}

interface PiChatPanel extends HTMLElement {
  setAgent(agent: PiAgent, config?: {
    onApiKeyRequired?: (provider: string) => Promise<boolean>;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    toolsFactory?: (...args: any[]) => any[];
  }): Promise<void>;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  agentInterface?: any;
}

function buildOpenAICompatModel(modelId: string, baseUrl: string) {
  let base = baseUrl.replace(/\/+$/, '');
  if (!base.endsWith('/v1')) base += '/v1';
  return {
    id: modelId,
    name: modelId,
    api: 'openai-completions',
    provider: 'openai',
    baseUrl: base,
    reasoning: false,
    input: ['text', 'image'],
    cost: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
    contextWindow: 128000,
    maxTokens: 16384,
  };
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function resolveModel(config: ReturnType<typeof getActiveLLMConfig>): any {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const mod = pi as any;
  if (config.provider === 'openai-compatible') {
    return buildOpenAICompatModel(
        config.model, config.baseUrl || 'https://api.openai.com');
  }
  try {
    return mod.getModel(config.provider, config.model);
  } catch (_) {
    // Unknown model id for a known provider — fall back to a minimal
    // openai-completions shim pointing at the configured baseUrl. The user
    // still gets a working stream as long as the model name is valid on
    // their endpoint.
    return buildOpenAICompatModel(
        config.model, config.baseUrl || 'https://api.openai.com');
  }
}

export class DaoChatView extends CrLitElement {
  static override get properties() {
    return {
      messageCount_: {type: Number, state: true},
      tokenEstimate_: {type: Number, state: true},
      compacting_: {type: Boolean, state: true},
      isStreaming_: {type: Boolean, state: true},
      pendingPageAttachment_: {state: true},
    };
  }

  // Light DOM so <pi-chat-panel> and its descendants pick up the global
  // Tailwind stylesheet linked from agent.html. Without this, pi-web-ui's
  // Tailwind utility classes render unstyled inside a shadow root.
  override createRenderRoot(): HTMLElement | DocumentFragment {
    return this;
  }

  private agent_: PiAgent | null = null;
  private panel_: PiChatPanel | null = null;
  private mounted_ = false;
  private unsubscribeAgent_: (() => void) | null = null;
  private compactAbort_: AbortController | null = null;
  private boundOnConfigChanged_: (() => void) | null = null;
  protected messageCount_ = 0;
  protected tokenEstimate_ = 0;
  protected compacting_ = false;
  protected isStreaming_ = false;
  // Current-page chip: renders as a small pill above the composer with the
  // active tab's title. Cleared when the URL is already in one of the
  // session sets (sent / dismissed) or the tab is a non-capturable surface
  // (chrome://, about:blank, ...).
  protected pendingPageAttachment_: PageInfo | null = null;

  // URLs we've already injected as a <current-webpage> block in this
  // session — the chip hides for them so the same page isn't re-attached
  // on every subsequent message.
  private sentPageUrls_ = new Set<string>();
  // URLs the user dismissed via the chip close button. Suppressed for the
  // rest of the session.
  private dismissedUrls_ = new Set<string>();
  // 2s poll of the active tab's URL+title. The sidebar has no event feed
  // for tab changes, so the chip follows the user via polling instead.
  private tabWatchTimer_: number | null = null;
  // Monkey-patched `agent-interface.sendMessage` original, kept so our
  // wrapper can call through after splicing the page block into the user
  // text. See mount_() for the patch site.
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private origSendMessage_: ((text: string, attachments: any[]) =>
                             Promise<void>) | null = null;
  private pendingDecorateTimer_ = 0;

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.unsubscribeAgent_?.();
    this.unsubscribeAgent_ = null;
    if (this.boundOnConfigChanged_) {
      window.removeEventListener(
          'llm-config-changed', this.boundOnConfigChanged_);
      this.boundOnConfigChanged_ = null;
    }
    if (this.tabWatchTimer_ !== null) {
      clearInterval(this.tabWatchTimer_);
      this.tabWatchTimer_ = null;
    }
    clearTimeout(this.pendingDecorateTimer_);
    try {
      this.agent_?.abort();
    } catch (_) { /* ignore */ }
    try {
      this.compactAbort_?.abort();
    } catch (_) { /* ignore */ }
  }

  // Show the compact action bar only once context utilization crosses this
  // ratio. Below that the user has plenty of headroom and the bar is just
  // visual noise. While a compaction is in-flight we keep the bar mounted so
  // the spinner / cancel affordance stays visible regardless of ratio.
  private static readonly COMPACT_BAR_VISIBLE_RATIO = 0.4;

  override render() {
    const ctx = this.agent_?.state.model?.contextWindow ?? 0;
    const ratio = ctx > 0 ? this.tokenEstimate_ / ctx : 0;
    const showBar = this.compacting_ ||
        (this.messageCount_ > 0 && ctx > 0 &&
         ratio >= DaoChatView.COMPACT_BAR_VISIBLE_RATIO);
    const canCompact =
        this.messageCount_ >= 2 && !this.compacting_ && !this.isStreaming_;
    const ratioPct = Math.min(100, Math.round(ratio * 100));
    let stateClass = 'idle';
    if (ratio >= 0.75) stateClass = 'hot';
    else if (ratio >= 0.5) stateClass = 'warm';

    return html`
      <style>
        .dao-compact-bar {
          display: flex;
          align-items: center;
          gap: 8px;
          padding: 4px 10px 0;
          font-size: 11px;
          color: var(--text-tertiary);
          flex-shrink: 0;
        }
        .dao-compact-bar .meta {
          display: flex;
          align-items: center;
          gap: 6px;
          flex: 1 1 auto;
          min-width: 0;
        }
        .dao-compact-bar .gauge {
          flex: 0 0 60px;
          height: 4px;
          border-radius: 2px;
          background: rgba(255,255,255,0.16);
          overflow: hidden;
        }
        .dao-compact-bar .gauge > span {
          display: block;
          height: 100%;
          background: rgba(70,120,190,0.55);
          transition: width 0.25s ease;
        }
        .dao-compact-bar.warm .gauge > span { background: rgba(220,160,80,0.7); }
        .dao-compact-bar.hot .gauge > span { background: rgba(220,80,80,0.8); }
        .dao-compact-bar .meta-text {
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }
        .dao-compact-bar .actions {
          display: flex;
          gap: 4px;
          flex-shrink: 0;
        }
        .dao-compact-bar button {
          font: inherit;
          font-size: 11px;
          color: var(--text-secondary);
          background: rgba(255,255,255,0.14);
          border: 1px solid rgba(255,255,255,0.18);
          border-radius: 8px;
          padding: 2px 8px;
          cursor: pointer;
          display: inline-flex;
          align-items: center;
          gap: 4px;
          transition: background 0.12s, border-color 0.12s, color 0.12s;
        }
        .dao-compact-bar button:hover:not(:disabled) {
          background: rgba(70,120,190,0.18);
          border-color: rgba(70,120,190,0.35);
          color: rgba(30,20,40,0.92);
        }
        .dao-compact-bar button:disabled {
          opacity: 0.45;
          cursor: not-allowed;
        }
        .dao-compact-bar button.compacting {
          color: rgb(70,120,190);
          border-color: rgba(70,120,190,0.35);
        }
        .dao-compact-bar .spinner {
          width: 10px;
          height: 10px;
          border: 1.5px solid rgba(70,120,190,0.25);
          border-top-color: rgb(70,120,190);
          border-radius: 50%;
          animation: dao-compact-spin 0.7s linear infinite;
        }
        @keyframes dao-compact-spin {
          to { transform: rotate(360deg); }
        }
      </style>
      ${showBar ? html`
        <div class="dao-compact-bar ${stateClass}">
          <div class="meta">
            <span class="gauge"
                title="Estimated context: ${this.tokenEstimate_} tokens${
                ctx > 0 ? ` / ${ctx} (${ratioPct}%)` : ''}">
              <span style="width: ${ratioPct}%"></span>
            </span>
            <span class="meta-text">
              ${this.messageCount_} msgs · ~${this.tokenEstimate_} tok${
                ctx > 0 ? ` (${ratioPct}%)` : ''}
            </span>
          </div>
          <div class="actions">
            <button class=${this.compacting_ ? 'compacting' : ''}
                ?disabled=${!canCompact && !this.compacting_}
                @click=${this.onCompactClick_}
                title=${this.compacting_
                    ? 'Cancel summarization'
                    : 'Summarize history into a single context message'}>
              ${this.compacting_
                  ? html`<span class="spinner"></span>Compacting…`
                  : html`Compact`}
            </button>
          </div>
        </div>` : ''}
      <pi-chat-panel></pi-chat-panel>
      ${this.pendingPageAttachment_ ? html`
        <div class="dao-page-chip-row">
          <div class="dao-page-chip" title=${this.pendingPageAttachment_.url}>
            <span class="dao-page-chip-favicon">
              ${this.renderChipFavicon_(this.pendingPageAttachment_.url)}
            </span>
            <span class="dao-page-chip-text">
              <span class="dao-page-chip-title">
                ${this.pendingPageAttachment_.title ||
                  this.chipHostname_(this.pendingPageAttachment_.url)}
              </span>
              <span class="dao-page-chip-domain">
                ${this.chipHostname_(this.pendingPageAttachment_.url)}
              </span>
            </span>
            <button class="dao-page-chip-close"
                @click=${this.onPageChipDismiss_}
                title="Don't attach this page">
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                  stroke-width="2" stroke-linecap="round"
                  stroke-linejoin="round" aria-hidden="true">
                <path d="M18 6 6 18"></path>
                <path d="m6 6 12 12"></path>
              </svg>
            </button>
          </div>
        </div>` : ''}`;
  }

  override firstUpdated() {
    this.panel_ = this.querySelector('pi-chat-panel') as PiChatPanel;
    void this.mount_();
  }

  private async mount_() {
    if (this.mounted_ || !this.panel_) return;
    this.mounted_ = true;

    // Boot the pi-web-ui AppStorage singleton before the ChatPanel tries
    // to read from it. Also mirrors the Dao-configured API key for the
    // active provider so AgentInterface's pre-send check passes.
    await ensurePiAppStorage();

    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const mod = pi as any;
    const Agent = mod.Agent as new (opts?: object) => PiAgent;

    const config = getActiveLLMConfig();
    const model = resolveModel(config);
    const tools = buildAgentTools();
    const thinkingLevel: PiAgent['state']['thinkingLevel'] =
        model.reasoning ? 'medium' : 'off';

    this.agent_ = new Agent({
      initialState: {
        systemPrompt: BASE_SYSTEM_PROMPT,
        model,
        tools,
        thinkingLevel,
        messages: [],
      },
      // pi-agent-core's default convertToLlm filters messages to only
      // the exact roles {user, assistant, toolResult}, silently dropping
      // user-with-attachments — which is the role pi's own
      // AgentInterface.sendMessage uses whenever there's at least one
      // attachment. Two jobs here:
      //   1. Map user-with-attachments to a plain `user` role so the
      //      LLM sees the turn at all.
      //   2. Splice each attachment's `extractedText` into the user
      //      content. pi only surfaces extractedText inside the artifact
      //      sandbox runtime (window.readTextAttachment), not in the
      //      chat completion path — without this the model gets only
      //      the user's typed text, never the captured <current-webpage>
      //      block. We do this at convert time (not at send time) so
      //      state.messages keeps the user's bubble clean: the UI only
      //      renders their typed text + a pretty attachment tile, while
      //      the LLM gets the full payload.
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      convertToLlm: (msgs: any[]) => {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        const out: any[] = [];
        for (const m of msgs) {
          if (!m) continue;
          if (m.role === 'user' || m.role === 'assistant' ||
              m.role === 'toolResult') {
            out.push(m);
          } else if (m.role === 'user-with-attachments') {
            const pieces: string[] = [];
            const atts = Array.isArray(m.attachments) ? m.attachments : [];
            for (const a of atts) {
              if (a && typeof a.extractedText === 'string' &&
                  a.extractedText.length > 0) {
                pieces.push(a.extractedText);
              }
            }
            const orig = typeof m.content === 'string' ? m.content : '';
            const trimmed = orig.trim();
            const merged = pieces.length > 0 ?
                (trimmed ? `${pieces.join('\n\n')}\n\n${orig}` :
                           pieces.join('\n\n')) :
                orig;
            out.push({
              role: 'user',
              content: merged,
              timestamp: m.timestamp,
            });
          }
        }
        return out;
      },
      getApiKey: (provider: string) => {
        // Always read the latest active config so rotating the key in the
        // settings view takes effect on the next prompt without a reload.
        const c = getActiveLLMConfig();
        const wanted = c.provider === 'openai-compatible' ? 'openai' : c.provider;
        return provider === wanted ? c.apiKey : undefined;
      },
    });

    const panel = this.panel_!;

    // The ChatPanel overwrites agent.state.tools during setAgent unless we
    // provide them via toolsFactory. Wrap our tools so they survive the
    // internal "tools = [artifactsPanel.tool, ...additionalTools]" merge.
    await panel.setAgent(this.agent_, {
      onApiKeyRequired: this.onApiKeyRequired_.bind(this),
      toolsFactory: () => tools,
    });

    // ChatPanel.setAgent() force-enables the model selector, attachments,
    // and thinking-level picker on its internal <agent-interface>. Dao
    // configures the provider/model from the settings view and doesn't
    // surface per-message thinking or attachments, so hide all three.
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = panel.querySelector('agent-interface') as any;
    if (iface) {
      iface.enableModelSelector = false;
      iface.enableAttachments = true;
      iface.enableThinkingSelector = false;
      iface.requestUpdate?.();

      // Intercept sendMessage so we can splice the current-page capture
      // into the outgoing `attachments` array. The actual merging of
      // attachment contents into the LLM request is done in the custom
      // convertToLlm above — this layer only runs the readability +
      // turndown capture and appends the pi-web-ui attachment object so
      // pi renders a native tile in the user bubble. We keep the user's
      // typed text untouched so their message bubble stays clean.
      if (typeof iface.sendMessage === 'function' && !this.origSendMessage_) {
        this.origSendMessage_ = iface.sendMessage.bind(iface);
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        iface.sendMessage = async (text: string, attachments: any[]) => {
          this.refreshModel_();
          const merged = await this.maybeAttachPage_(attachments || []);
          return this.origSendMessage_!(text, merged);
        };
      }
    }

    // Disable spellcheck / autocorrect on the composer textarea. Debug Blink
    // DCHECKs (`FragmentItem::LineLeftAndRightForOffsets`) in
    // HighlightPainter::PaintNonCssMarkers can crash the renderer when
    // spellcheck marker offsets disagree with the current inline text
    // fragment — a known issue triggered by re-layout of textareas that
    // carry spellcheck markers. Release builds don't DCHECK, but turning
    // the checker off is the right move for a chat composer anyway.
    this.hardenComposerTextarea_(panel);
    const editor = panel.querySelector('message-editor');
    if (editor) {
      const mo = new MutationObserver(() => this.hardenComposerTextarea_(panel));
      mo.observe(editor, {subtree: true, childList: true});
    }

    // pi-agent-core's Agent mutates `state.messages` in place via `push()`
    // on `message_end` / failure (Agent.js:366, 338). pi-web-ui's
    // <message-list> receives those messages through a Lit
    // `@property({type: Array})` binding, which uses the default
    // reference-equality `hasChanged`. In-place mutation therefore leaves
    // the stable list untouched — the streaming container clears on
    // message_end, but MessageList never rebuilds, and the finalized reply
    // appears to vanish. Re-assign through the state setter (which
    // slice-copies in Agent.js:42) so Lit detects a fresh reference on the
    // next render cycle.
    //
    // A second race also needs fixing here: on `agent_end`, AgentInterface
    // calls `requestUpdate()` inside its listener, but the Agent's
    // `finishRun()` (which sets `state.isStreaming = false`) only runs in
    // the `finally` block of `runWithLifecycle` *after* every emit's
    // awaited listener chain resolves. Lit's scheduled microtask therefore
    // fires before `isStreaming` flips, and MessageEditor re-renders with
    // `isStreaming=true` and stays on the Abort button forever — the send
    // button appears permanently stuck "in progress" after a reply. To
    // unstick it we wait for `agent.waitForIdle()` (which resolves after
    // finishRun via `activeRun.resolve()` in Agent.js:346) and force one
    // more `requestUpdate()` on the AgentInterface once `isStreaming` has
    // been cleared.
    this.unsubscribeAgent_ = this.agent_.subscribe((ev) => {
      if (ev?.type === 'message_end' || ev?.type === 'agent_end') {
        const agent = this.agent_;
        if (!agent) return;
        agent.state.messages = agent.state.messages.slice();
        this.syncMeta_();
      }
      if (ev?.type === 'agent_start' || ev?.type === 'agent_end') {
        this.isStreaming_ = !!this.agent_?.state.isStreaming;
      }
      if (ev?.type === 'message_end') {
        this.scheduleDecorate_();
      }
      if (ev?.type === 'agent_end') {
        const agent = this.agent_;
        const panel = this.panel_;
        if (!agent || !panel) return;
        agent.waitForIdle().then(() => {
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          const iface = panel.querySelector('agent-interface') as any;
          iface?.requestUpdate?.();
          this.isStreaming_ = !!this.agent_?.state.isStreaming;
          this.refreshRetryButton_();
          this.decoratePageAttachments_();
        });
      }
    });
    this.syncMeta_();

    // Seed the page chip with the current tab and start the 2s poller.
    // Poll unconditionally (not only when a chip is visible) because the
    // user may switch to a new URL we haven't seen yet, which requires
    // showing a chip that currently isn't rendered.
    void this.refreshPageChip_();
    this.tabWatchTimer_ = window.setInterval(() => {
      void this.refreshPageChip_();
    }, 2000);

    // Listen for provider/model changes from the settings view so the
    // agent picks up the new model on the next turn without a full reload.
    this.boundOnConfigChanged_ = () => this.refreshModel_();
    window.addEventListener('llm-config-changed', this.boundOnConfigChanged_);
  }

  // Debounced decoration pass — coalesces rapid-fire message_end events
  // (e.g. tool-result bursts) into a single DOM sweep after rendering
  // settles. Only decorates attachments; the retry button is injected in
  // the agent_end handler where we know the turn is truly finished.
  private scheduleDecorate_(): void {
    clearTimeout(this.pendingDecorateTimer_);
    this.pendingDecorateTimer_ = window.setTimeout(() => {
      this.decoratePageAttachments_();
    }, 80);
  }

  // pi-web-ui's <attachment-tile> renders our markdown page captures as
  // a generic 64x64 document square. For dao-page-* attachments we swap
  // that out for a horizontal card (favicon + title + domain) so the
  // "current webpage" reads as a link preview rather than a file.
  // Idempotent: we stamp a `data-dao-decorated` marker on the tile so
  // subsequent MutationObserver ticks don't re-run unless the tile
  // re-rendered (Lit would wipe our marker in that case).
  private decoratePageAttachments_(): void {
    const panel = this.panel_;
    if (!panel) return;
    const tiles = panel.querySelectorAll('attachment-tile');
    tiles.forEach(el => {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const tile = el as HTMLElement & {attachment?: any};
      const att = tile.attachment;
      if (!att || typeof att.id !== 'string' ||
          !att.id.startsWith('dao-page-')) return;
      if (tile.getAttribute('data-dao-decorated') === '1') return;
      const url = (att.daoPageUrl as string) || '';
      const rawTitle = (att.daoPageTitle as string) ||
          (att.fileName || '').replace(/\.md$/, '');
      const domain = this.chipHostname_(url);
      tile.setAttribute('data-dao-decorated', '1');
      tile.innerHTML = this.buildPageTileHtml_(url, rawTitle, domain);
      // CSP (chrome WebUI) forbids inline event handlers, so wire up
      // the favicon img's load/error fallbacks in JS instead of via
      // `onload=` / `onerror=` attributes.
      const favImg = tile.querySelector(
          '.dao-page-tile-favicon img') as HTMLImageElement | null;
      if (favImg) {
        const svg = favImg.parentElement?.querySelector('svg') as
            SVGElement | null;
        favImg.addEventListener('load', () => {
          if (svg) svg.style.display = 'none';
        });
        favImg.addEventListener('error', () => {
          favImg.style.display = 'none';
        });
        // Already cached / data-complete before the listener attached?
        if (favImg.complete && favImg.naturalWidth > 0 && svg) {
          svg.style.display = 'none';
        } else if (favImg.complete && favImg.naturalWidth === 0) {
          favImg.style.display = 'none';
        }
      }
    });
  }

  private buildPageTileHtml_(
      url: string, title: string, domain: string): string {
    const safeUrl = this.escapeAttr_(url);
    const safeTitle = this.escapeHtml_(title);
    const safeDomain = this.escapeHtml_(domain);
    let origin = '';
    try {
      origin = new URL(url).origin;
    } catch (_) { /* keep empty */ }
    const globeSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<circle cx="12" cy="12" r="10"></circle>' +
        '<path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"></path>' +
        '<path d="M2 12h20"></path>' +
        '</svg>';
    const favImg = origin ?
        `<img src="${this.escapeAttr_(origin + '/favicon.ico')}" alt=""` +
            ' referrerpolicy="no-referrer">' :
        '';
    return '<div class="dao-page-tile" title="' + safeUrl + '">' +
        '<span class="dao-page-tile-favicon">' + globeSvg + favImg +
        '</span>' +
        '<span class="dao-page-tile-text">' +
        '<span class="dao-page-tile-title">' + safeTitle + '</span>' +
        '<span class="dao-page-tile-domain">' + safeDomain + '</span>' +
        '</span>' +
        '</div>';
  }

  private escapeHtml_(s: string): string {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
  }

  private escapeAttr_(s: string): string {
    return this.escapeHtml_(s).replace(/"/g, '&quot;');
  }

  private refreshRetryButton_(): void {
    const panel = this.panel_;
    if (!panel) return;
    // Drop any previously-injected action rows so only one remains.
    panel.querySelectorAll('.dao-assistant-actions')
        .forEach(el => el.remove());
    if (this.agent_?.state.isStreaming) return;
    const list = panel.querySelectorAll('assistant-message');
    const last = list[list.length - 1] as HTMLElement | undefined;
    if (!last) return;
    const row = document.createElement('div');
    row.className = 'dao-assistant-actions';
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'dao-retry-btn';
    btn.title = 'Regenerate response';
    btn.setAttribute('aria-label', 'Regenerate response');
    btn.innerHTML =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<path d="M3 12a9 9 0 1 0 3-6.7"></path>' +
        '<path d="M3 4v5h5"></path>' +
        '</svg><span>Retry</span>';
    btn.addEventListener('click', () => void this.retryLastAssistant_());
    row.appendChild(btn);
    last.insertAdjacentElement('afterend', row);
  }

  private async retryLastAssistant_(): Promise<void> {
    const agent = this.agent_;
    if (!agent || agent.state.isStreaming) return;
    const messages = agent.state.messages;
    let lastUserIdx = -1;
    for (let i = messages.length - 1; i >= 0; i--) {
      const role = messages[i].role;
      if (role === 'user' || role === 'user-with-attachments') {
        lastUserIdx = i;
        break;
      }
    }
    if (lastUserIdx < 0) return;
    // Keep the user message, drop all assistant / toolResult messages
    // that came after it. Replace the array (not mutate in place) so
    // pi-web-ui's reference-equality change detector picks it up.
    agent.state.messages = messages.slice(0, lastUserIdx + 1);
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    iface?.requestUpdate?.();
    this.syncMeta_();
    try {
      await agent.continue();
    } catch (e) {
      console.warn('[dao] retry failed', e);
    }
  }

  // Probe the active tab's URL+title and update the chip state. Cheap —
  // just a native getPageInfo, no script injection. Skips non-capturable
  // schemes (chrome://, about:blank, data:) and URLs that are already in
  // the sent / dismissed sets.
  private async refreshPageChip_() {
    const info = await fetchCurrentPageInfo();
    if (!info || !isCapturablePageUrl(info.url) ||
        this.sentPageUrls_.has(info.url) ||
        this.dismissedUrls_.has(info.url)) {
      if (this.pendingPageAttachment_ !== null) {
        this.pendingPageAttachment_ = null;
      }
      return;
    }
    const current = this.pendingPageAttachment_;
    if (!current || current.url !== info.url || current.title !== info.title) {
      this.pendingPageAttachment_ = info;
    }
  }

  private onPageChipDismiss_() {
    const pending = this.pendingPageAttachment_;
    if (!pending) return;
    this.dismissedUrls_.add(pending.url);
    this.pendingPageAttachment_ = null;
  }

  private chipHostname_(url: string): string {
    try {
      return new URL(url).hostname;
    } catch (_) {
      return url;
    }
  }

  private renderChipFavicon_(url: string) {
    let origin = '';
    try {
      origin = new URL(url).origin;
    } catch (_) {
      origin = '';
    }
    const fallbackSvg = html`<svg viewBox="0 0 24 24" fill="none"
        stroke="currentColor" stroke-width="2" stroke-linecap="round"
        stroke-linejoin="round" aria-hidden="true">
      <circle cx="12" cy="12" r="10"></circle>
      <path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"></path>
      <path d="M2 12h20"></path>
    </svg>`;
    if (!origin) return fallbackSvg;
    const onErr = (e: Event) => {
      const img = e.target as HTMLImageElement | null;
      if (img) img.style.display = 'none';
    };
    const onLoad = (e: Event) => {
      const img = e.target as HTMLImageElement | null;
      const svg =
          img && img.parentElement
              ? img.parentElement.querySelector('svg')
              : null;
      if (svg) (svg as SVGElement).style.display = 'none';
    };
    return html`${fallbackSvg}<img src=${origin + '/favicon.ico'} alt=""
        @error=${onErr} @load=${onLoad} referrerpolicy="no-referrer">`;
  }

  // Called by the monkey-patched sendMessage. Runs the full page-capture
  // pipeline (readability -> turndown) against the ACTIVE TAB AT SEND
  // TIME (not at chip-render time) — the chip URL is used only as a
  // presence signal, the capture's own url/title is authoritative. The
  // returned attachment carries the full <current-webpage> block in its
  // `extractedText` field; convertToLlm splices that into the user
  // content when building the LLM request, leaving the visible bubble
  // untouched.
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private async maybeAttachPage_(attachments: any[]): Promise<any[]> {
    if (!this.pendingPageAttachment_) return attachments;
    const chipUrl = this.pendingPageAttachment_.url;
    let capture;
    try {
      capture = await captureCurrentPageMarkdown();
    } catch (_) {
      capture = null;
    }
    if (!capture || !capture.markdown) {
      // Still mark the chip URL as "handled" so we don't re-flash the
      // chip on the next poll tick while the user waits for a response.
      this.sentPageUrls_.add(chipUrl);
      this.pendingPageAttachment_ = null;
      void this.refreshPageChip_();
      return attachments;
    }
    const pageAtt = buildPageAttachment(capture);
    this.sentPageUrls_.add(capture.url);
    // Clear any prior dismiss for this URL — once the page has been sent
    // the dismiss state for that URL is moot.
    this.dismissedUrls_.delete(capture.url);
    this.pendingPageAttachment_ = null;
    void this.refreshPageChip_();
    return [...attachments, pageAtt];
  }

  private refreshModel_() {
    if (!this.agent_) return;
    const config = getActiveLLMConfig();
    const model = resolveModel(config);
    this.agent_.state.model = model;
    this.agent_.state.thinkingLevel = model.reasoning ? 'medium' : 'off';
    void syncActiveKeyToPiStorage();
  }

  private syncMeta_() {
    const msgs = this.agent_?.state.messages ?? [];
    this.messageCount_ = msgs.length;
    this.tokenEstimate_ = estimateMessagesTokens(msgs);
  }

  private async onCompactClick_() {
    if (this.compacting_) {
      this.compactAbort_?.abort();
      return;
    }
    if (!this.agent_) return;
    if (this.agent_.state.isStreaming) {
      this.dispatchEvent(new CustomEvent('show-toast', {
        bubbles: true,
        composed: true,
        detail: {text: 'Wait for the current turn to finish'},
      }));
      return;
    }
    this.compacting_ = true;
    this.compactAbort_ = new AbortController();
    try {
      const result = await compactAgentMessages(this.agent_, {
        signal: this.compactAbort_.signal,
        keepTailUserTurns: 1,
      });
      this.syncMeta_();
      // pi-web-ui's <message-list> binds to agent.state.messages with
      // reference-equality; the setter inside compactAgentMessages already
      // assigns a fresh array, but we still need to nudge the panel's
      // internal AgentInterface to re-render now that its message list
      // shrank.
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const iface = this.panel_?.querySelector('agent-interface') as any;
      iface?.requestUpdate?.();
      this.dispatchEvent(new CustomEvent('show-toast', {
        bubbles: true,
        composed: true,
        detail: {
          text: `Compacted ${result.collapsedCount} messages → 1 summary`,
        },
      }));
    } catch (e) {
      const err = e as Error;
      const text = err.name === 'AbortError'
          ? 'Compaction cancelled'
          : `Compact failed: ${err.message ?? err}`;
      this.dispatchEvent(new CustomEvent('show-toast', {
        bubbles: true,
        composed: true,
        detail: {text},
      }));
    } finally {
      this.compacting_ = false;
      this.compactAbort_ = null;
    }
  }

  private hardenComposerTextarea_(panel: PiChatPanel) {
    const ta = panel.querySelector('message-editor textarea') as
        HTMLTextAreaElement | null;
    if (!ta) return;
    ta.spellcheck = false;
    ta.setAttribute('spellcheck', 'false');
    ta.setAttribute('autocorrect', 'off');
    ta.setAttribute('autocapitalize', 'off');
    ta.setAttribute('autocomplete', 'off');
  }

  private async onApiKeyRequired_(_provider: string): Promise<boolean> {
    // Phase 1: punt to the Dao settings tab. A later phase can render an
    // inline Dao-styled prompt dialog.
    this.dispatchEvent(new CustomEvent('switch-tab', {
      bubbles: true,
      composed: true,
      detail: {tab: 'settings', subTab: 'connection'},
    }));
    return false;
  }

  // ---- Public API kept for dao-agent-app compatibility ----

  focusInput() {
    const editor = this.panel_?.querySelector(
        'message-editor textarea, message-editor input') as HTMLElement | null;
    editor?.focus();
  }

  endCurrentSession() {
    try {
      this.agent_?.abort();
    } catch (_) { /* ignore */ }
  }

  // Reset the agent conversation to an empty history. Aborts any in-flight
  // stream first, then reassigns through the state setter so pi-agent-core's
  // slice-copy + Lit reference-equality binding both fire (same pattern used
  // on `message_end` and in `compactAgentMessages`).
  startNewSession() {
    if (!this.agent_) return;
    try {
      this.agent_.abort();
    } catch (_) { /* ignore */ }
    try {
      this.compactAbort_?.abort();
    } catch (_) { /* ignore */ }
    this.compacting_ = false;
    this.compactAbort_ = null;
    this.agent_.state.messages = [];
    this.isStreaming_ = false;
    // Fresh conversation — clear the page-attachment bookkeeping so the
    // current active tab gets a new chip offered to the user.
    this.sentPageUrls_.clear();
    this.dismissedUrls_.clear();
    this.pendingPageAttachment_ = null;
    this.syncMeta_();
    void this.refreshPageChip_();
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    iface?.requestUpdate?.();
    setTimeout(() => this.focusInput(), 50);
  }
}

customElements.define('dao-chat-view', DaoChatView);
