// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PR3 Phase 1: thin wrapper around pi-web-ui's <pi-chat-panel>.
//
// Replaces the previous 1400-line hand-rolled chat view. The ChatPanel
// (from pi-web-ui) is driven by a pi-agent-core `Agent` that runs the
// turn loop, executes tools, and emits streaming events. We construct the
// Agent here with:
//   - systemPrompt: Dao's BASE_SYSTEM_PROMPT concatenated with the current
//     SOUL.md (personality layer). Refreshed before every send and on
//     cross-tab soulChannel broadcasts so `update_soul` takes effect on
//     the next turn without a reload.
//   - model: resolved from Dao's llm_config via pi-ai's getModel() (or
//     built by hand for the `openai-compatible` path)
//   - tools: every existing Dao tool, wrapped by pi_tool_adapter
//
// Phase 1 deliberately drops: scenario / memory chips, API/tool call stats
// UI, and IndexedDB chat history persistence. These are overlays / external
// hooks that will be re-attached on top of ChatPanel in Phase 2.

import {CrLitElement, html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

import {BASE_SYSTEM_PROMPT, currentSoulContent, refreshSoulContent, soulChannel} from './agent_bridge.js';
import {compactAgentMessages, estimateMessagesTokens} from './dao_compact.js';
import {getActiveLLMConfig} from './llm_config.js';
import {buildPageAttachment, buildSelectionAttachment, captureCurrentPageMarkdown, clearCurrentSelection, fetchCurrentPageInfo, fetchCurrentSelection, isCapturablePageUrl, type PageInfo, type SelectionCapture} from './dao_page_capture.js';
import {renderShareImage} from './dao_share_image.js';
import {buildAgentTools} from './pi_tool_adapter.js';
import {ensurePiAppStorage, syncActiveKeyToPiStorage} from './pi_app_storage.js';
import {getAllSkills, initSkillRegistry, loadSkillInstructions, type SkillRegistryEntry} from './skill_registry.js';
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
      pendingSelection_: {state: true},
      skillPickerVisible_: {state: true},
      skillPickerSkills_: {state: true},
      skillPickerIndex_: {state: true},
      skillPickerAnchor_: {state: true},
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
  private boundOnSoulChanged_: (() => void) | null = null;
  private boundOnChipHint_: (() => void) | null = null;
  private boundOnVisibilityHint_: (() => void) | null = null;
  protected messageCount_ = 0;
  protected tokenEstimate_ = 0;
  protected compacting_ = false;
  protected isStreaming_ = false;
  // Current-page chip: renders as a small pill above the composer with the
  // active tab's title. Cleared when the URL is already in one of the
  // session sets (sent / dismissed) or the tab is a non-capturable surface
  // (chrome://, about:blank, ...).
  protected pendingPageAttachment_: PageInfo | null = null;

  // Latest text selection from the active tab. Refreshed by the same 2s
  // poll that drives the page chip; displayed as its own chip below the
  // composer. Independent of pendingPageAttachment_ — both chips can be
  // active at once and the LLM will receive both blocks. Not de-duped:
  // every send picks up whatever the selection currently is, and the
  // selection is cleared in the tab after a successful send.
  protected pendingSelection_: SelectionCapture | null = null;

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

  // Slash-command skill picker state. When the composer textarea starts with
  // `/query` (no space), a floating list of matching skills is shown.
  // Selection rewrites the input to `/skillId ` and dispatches an input
  // event so pi-web-ui picks up the new value. At send time, a leading
  // `/skillId ...` is expanded to the skill's SKILL.md instructions plus the
  // user's remaining text.
  protected skillPickerVisible_ = false;
  protected skillPickerSkills_: SkillRegistryEntry[] = [];
  protected skillPickerIndex_ = 0;
  // viewport coords of the composer textarea, recomputed whenever the
  // picker is visible; drives the picker's `position: fixed` anchor so it
  // sits exactly above the editor regardless of composer height changes
  // (multi-line autogrow, attachment chips, etc.).
  protected skillPickerAnchor_:
      {left: number, top: number, width: number} | null = null;
  private skillPickerQuery_ = '';
  private skillInstructionsCache_ = new Map<string, string>();
  private composerTextarea_: HTMLTextAreaElement | null = null;
  private onComposerInput_: ((e: Event) => void) | null = null;
  private onComposerKeyDown_: ((e: KeyboardEvent) => void) | null = null;
  private composerResizeObserver_: ResizeObserver | null = null;
  // Measures the whole <message-editor> (textarea + buttons + attachment
  // row), not just the textarea. Drives the `--dao-composer-h` custom
  // property so the floating chip row can sit a fixed gap above the
  // composer regardless of how the composer has grown.
  private composerHeightObserver_: ResizeObserver | null = null;
  private onWindowResizeForPicker_: (() => void) | null = null;

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.unsubscribeAgent_?.();
    this.unsubscribeAgent_ = null;
    if (this.boundOnConfigChanged_) {
      window.removeEventListener(
          'llm-config-changed', this.boundOnConfigChanged_);
      this.boundOnConfigChanged_ = null;
    }
    if (this.boundOnSoulChanged_) {
      soulChannel.removeEventListener('message', this.boundOnSoulChanged_);
      this.boundOnSoulChanged_ = null;
    }
    if (this.boundOnChipHint_) {
      this.removeEventListener('pointerenter', this.boundOnChipHint_);
      this.removeEventListener('focusin', this.boundOnChipHint_);
      this.boundOnChipHint_ = null;
    }
    if (this.boundOnVisibilityHint_) {
      document.removeEventListener(
          'visibilitychange', this.boundOnVisibilityHint_);
      this.boundOnVisibilityHint_ = null;
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
    if (this.composerTextarea_) {
      if (this.onComposerInput_) {
        this.composerTextarea_.removeEventListener(
            'input', this.onComposerInput_);
      }
      if (this.onComposerKeyDown_) {
        this.composerTextarea_.removeEventListener(
            'keydown', this.onComposerKeyDown_, true);
      }
      this.composerTextarea_ = null;
      this.onComposerInput_ = null;
      this.onComposerKeyDown_ = null;
    }
    this.composerResizeObserver_?.disconnect();
    this.composerResizeObserver_ = null;
    this.composerHeightObserver_?.disconnect();
    this.composerHeightObserver_ = null;
    if (this.onWindowResizeForPicker_) {
      window.removeEventListener('resize', this.onWindowResizeForPicker_);
      window.removeEventListener(
          'scroll', this.onWindowResizeForPicker_, true);
      this.onWindowResizeForPicker_ = null;
    }
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

        /* Slash-command skill picker — positioned via inline style in
         * renderSkillPicker_ (position: fixed, anchored to the composer
         * textarea's viewport rect). Colors mirror dao_skill_manager_view
         * (var(--surface) background, var(--accent-dim) selected,
         * var(--text*) typography). */
        .dao-skill-picker {
          display: flex;
          flex-direction: column;
          background: var(--surface);
          border: 1px solid rgba(0, 0, 0, 0.08);
          border-radius: 12px;
          box-shadow: 0 8px 24px rgba(0, 0, 0, 0.18);
          overflow: hidden;
          z-index: 20;
          font-size: 12px;
          color: var(--text);
          backdrop-filter: blur(20px);
          -webkit-backdrop-filter: blur(20px);
        }
        .dao-skill-picker-head {
          display: flex;
          align-items: center;
          gap: 8px;
          padding: 6px 10px;
          font-size: 11px;
          font-weight: 600;
          color: var(--text-secondary);
          border-bottom: 1px solid rgba(0, 0, 0, 0.06);
          flex-shrink: 0;
        }
        .dao-skill-picker-hint {
          margin-left: auto;
          font-weight: 400;
          font-size: 10px;
          color: var(--text-tertiary);
        }
        .dao-skill-picker-list {
          overflow-y: auto;
          padding: 4px;
        }
        .dao-skill-picker-item {
          display: flex;
          flex-direction: column;
          gap: 2px;
          padding: 6px 8px;
          border-radius: 8px;
          cursor: pointer;
        }
        .dao-skill-picker-item:hover {
          background: var(--surface-hover);
        }
        .dao-skill-picker-item.selected {
          background: var(--accent-dim);
        }
        .dao-skill-picker-name {
          font-weight: 600;
          color: var(--text);
          font-family: ui-monospace, Menlo, monospace;
          font-size: 12px;
        }
        .dao-skill-picker-desc {
          color: var(--text-secondary);
          font-size: 11px;
          overflow: hidden;
          text-overflow: ellipsis;
          white-space: nowrap;
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
      ${this.renderSkillPicker_()}
      ${(this.pendingPageAttachment_ || this.pendingSelection_) ? html`
        <div class="dao-page-chip-row">
          ${this.pendingPageAttachment_ ? html`
            <div class="dao-page-chip"
                title=${this.pendingPageAttachment_.url}>
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
            </div>` : ''}
          ${this.pendingSelection_ ? html`
            <div class="dao-page-chip dao-selection-chip"
                title=${this.pendingSelection_.text}>
              <span class="dao-page-chip-favicon">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                    stroke-width="2" stroke-linecap="round"
                    stroke-linejoin="round" aria-hidden="true">
                  <path d="M4 7V4h16v3"></path>
                  <path d="M9 20h6"></path>
                  <path d="M12 4v16"></path>
                </svg>
              </span>
              <span class="dao-page-chip-text">
                <span class="dao-page-chip-title">
                  ${this.selectionPreview_(this.pendingSelection_.text)}
                </span>
                <span class="dao-page-chip-domain">selection</span>
              </span>
              <button class="dao-page-chip-close"
                  @click=${this.onSelectionChipDismiss_}
                  title="Don't attach this selection">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                    stroke-width="2" stroke-linecap="round"
                    stroke-linejoin="round" aria-hidden="true">
                  <path d="M18 6 6 18"></path>
                  <path d="m6 6 12 12"></path>
                </svg>
              </button>
            </div>` : ''}
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
        systemPrompt: this.buildSystemPrompt_(),
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
          if (m.role === 'assistant' || m.role === 'toolResult') {
            out.push(m);
          } else if (m.role === 'user') {
            const orig = typeof m.content === 'string' ? m.content : '';
            const expanded = this.expandSkillPrefix_(orig);
            if (expanded === orig) {
              out.push(m);
            } else {
              out.push({
                role: 'user',
                content: expanded,
                timestamp: m.timestamp,
              });
            }
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
            const expandedOrig = this.expandSkillPrefix_(orig);
            const trimmed = expandedOrig.trim();
            const merged = pieces.length > 0 ?
                (trimmed ? `${pieces.join('\n\n')}\n\n${expandedOrig}` :
                           pieces.join('\n\n')) :
                expandedOrig;
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
          // Pull latest soul into the live systemPrompt before the turn is
          // packed into the LLM request. Handles the same-tab update_soul
          // path (BroadcastChannel doesn't echo to its own document).
          this.refreshSystemPrompt_();
          // Keep the user's typed `/skill rest` visible in the bubble;
          // load the SKILL.md body into the session cache so convertToLlm
          // can splice it synchronously at LLM-conversion time.
          await this.ensureSkillLoadedFromText_(text);
          const withPage = await this.maybeAttachPage_(attachments || []);
          const merged = await this.maybeAttachSelection_(withPage);
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
    this.attachSkillPicker_(panel);
    this.attachComposerHeightObserver_(panel);
    const editor = panel.querySelector('message-editor');
    if (editor) {
      const mo = new MutationObserver(() => {
        this.hardenComposerTextarea_(panel);
        this.attachSkillPicker_(panel);
        this.attachComposerHeightObserver_(panel);
      });
      mo.observe(editor, {subtree: true, childList: true});
    }

    // Warm the skill registry so the picker lists entries on first `/`.
    void initSkillRegistry();

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
        // If the just-ended message contained an `update_soul` tool call,
        // the on-disk soul is now stale relative to state.systemPrompt.
        // Refresh so the next round of this turn (or the next turn) sees
        // the updated personality without waiting for a user send.
        this.refreshSystemPrompt_();
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
          this.refreshAssistantActions_();
          this.decoratePageAttachments_();
        });
      }
    });
    this.syncMeta_();

    // Seed the chips with the current tab and start the poller. A 2s tick
    // was noticeably sluggish — typical flow is "select text on page →
    // mouse into sidebar → ask" and the chip could lag behind the pointer
    // by most of that second. We compensate on two axes:
    //   1. Shorter poll (800ms) as an always-on safety net for keyboard-
    //      only tab switches / selection edits where no pointer event ever
    //      reaches the sidebar.
    //   2. Event-driven instant refresh (see boundOnChipHint_ below) when
    //      the pointer or focus enters the sidebar — this is what makes
    //      the chip feel "already there" by the time the user looks at it.
    void this.refreshChips_();
    this.tabWatchTimer_ = window.setInterval(() => {
      void this.refreshChips_();
    }, 800);

    // Instant-refresh hints. pointerenter / focusin fire at the moment the
    // user moves their attention to the sidebar — exactly when we need the
    // chips to be up to date. Trailing-edge throttled by refreshChipsRunning_
    // so burst events don't stack concurrent executeScripts.
    this.boundOnChipHint_ = () => { void this.refreshChips_(); };
    this.addEventListener('pointerenter', this.boundOnChipHint_);
    this.addEventListener('focusin', this.boundOnChipHint_);
    // document.visibilitychange already flips when the sidebar toggles
    // open (see dao_agent_app.ts). Refresh there too so the first chip
    // paint lines up with the first frame after the animation.
    this.boundOnVisibilityHint_ = () => {
      if (document.visibilityState === 'visible') {
        void this.refreshChips_();
      }
    };
    document.addEventListener(
        'visibilitychange', this.boundOnVisibilityHint_);

    // Listen for provider/model changes from the settings view so the
    // agent picks up the new model on the next turn without a full reload.
    this.boundOnConfigChanged_ = () => this.refreshModel_();
    window.addEventListener('llm-config-changed', this.boundOnConfigChanged_);

    // Cross-tab soul sync: another window's settings panel (or update_soul
    // tool) saves the soul → BroadcastChannel fires here → refresh the
    // live systemPrompt so the next turn uses the latest personality.
    this.boundOnSoulChanged_ = () => this.refreshSystemPrompt_();
    soulChannel.addEventListener('message', this.boundOnSoulChanged_);
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

  // Pulls the visible text out of an assistant message. pi-agent-core
  // stores content either as a plain string (most LLM streams) or as an
  // array of Claude-style blocks (text / toolCall / toolResult). Only
  // 'text' parts are user-visible, so tool plumbing never leaks into
  // the share image or the clipboard.
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private extractAssistantText_(msg: any): string {
    if (!msg) return '';
    const c = msg.content;
    if (typeof c === 'string') return c;
    if (!Array.isArray(c)) return '';
    const pieces: string[] = [];
    for (const part of c) {
      if (part && part.type === 'text' && typeof part.text === 'string') {
        pieces.push(part.text);
      }
    }
    return pieces.join('\n\n');
  }

  // Walks the message list backward and returns the last assistant reply
  // paired with the user message that prompted it. `source` is populated
  // from the first Dao-authored page/selection attachment on that user
  // message. Returns null if there is no assistant reply yet.
  private getLastQaPair_():
      {question: string; source?: {title: string; domain: string};
       answer: string}|null {
    const agent = this.agent_;
    if (!agent) return null;
    const msgs = agent.state.messages;
    let assistantIdx = -1;
    for (let i = msgs.length - 1; i >= 0; i--) {
      if (msgs[i]?.role === 'assistant') {
        assistantIdx = i;
        break;
      }
    }
    if (assistantIdx < 0) return null;
    const answer = this.extractAssistantText_(msgs[assistantIdx]);

    let question = '—';
    let source: {title: string; domain: string}|undefined;
    for (let i = assistantIdx - 1; i >= 0; i--) {
      const m = msgs[i];
      const role = m?.role;
      if (role !== 'user' && role !== 'user-with-attachments') continue;
      if (typeof m.content === 'string' && m.content.length > 0) {
        question = m.content;
      }
      if (role === 'user-with-attachments' && Array.isArray(m.attachments)) {
        for (const att of m.attachments) {
          if (att && typeof att.daoPageUrl === 'string' && att.daoPageUrl) {
            try {
              const host = new URL(att.daoPageUrl).hostname.replace(
                  /^www\./, '');
              source = {
                title: att.daoPageTitle || att.fileName || host,
                domain: host,
              };
            } catch (_) {
              // Bad URL — skip this attachment, keep looking.
            }
            if (source) break;
          }
        }
      }
      break;
    }
    return {question, source, answer};
  }

  private refreshAssistantActions_(): void {
    const panel = this.panel_;
    if (!panel) return;
    panel.querySelectorAll('.dao-assistant-actions')
        .forEach(el => el.remove());
    if (this.agent_?.state.isStreaming) return;
    const list = panel.querySelectorAll('assistant-message');
    const last = list[list.length - 1] as HTMLElement | undefined;
    if (!last) return;

    const row = document.createElement('div');
    row.className = 'dao-assistant-actions';

    const copyBtn = document.createElement('button');
    copyBtn.type = 'button';
    copyBtn.className = 'dao-copy-btn';
    copyBtn.title = 'Copy answer text';
    copyBtn.setAttribute('aria-label', 'Copy answer text');
    copyBtn.innerHTML =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>' +
        '<path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1">' +
        '</path></svg>';
    copyBtn.addEventListener(
        'click', () => void this.copyAssistantText_(copyBtn));
    row.appendChild(copyBtn);

    const shareBtn = document.createElement('button');
    shareBtn.type = 'button';
    shareBtn.className = 'dao-share-btn';
    shareBtn.title = 'Copy as image';
    shareBtn.setAttribute('aria-label', 'Copy as image');
    shareBtn.innerHTML =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect>' +
        '<circle cx="9" cy="9" r="2"></circle>' +
        '<path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"></path>' +
        '</svg>';
    shareBtn.addEventListener(
        'click', () => void this.shareAssistantAsImage_(shareBtn));
    row.appendChild(shareBtn);

    const retryBtn = document.createElement('button');
    retryBtn.type = 'button';
    retryBtn.className = 'dao-retry-btn';
    retryBtn.title = 'Regenerate response';
    retryBtn.setAttribute('aria-label', 'Regenerate response');
    retryBtn.innerHTML =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<path d="M3 12a9 9 0 1 0 3-6.7"></path>' +
        '<path d="M3 4v5h5"></path>' +
        '</svg>';
    retryBtn.addEventListener('click', () => void this.retryLastAssistant_());
    row.appendChild(retryBtn);

    last.insertAdjacentElement('afterend', row);
  }

  private flashButtonLabel_(
      btn: HTMLButtonElement, label: string, durationMs = 2000): void {
    const ok = label === 'Copied' || label === 'Shared';
    const checkSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true"><path d="M20 6 9 17l-5-5"></path></svg>';
    const xSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true"><path d="M18 6 6 18"></path>' +
        '<path d="m6 6 12 12"></path></svg>';
    const origHtml = btn.innerHTML;
    const origAria = btn.getAttribute('aria-label') || '';
    btn.innerHTML = ok ? checkSvg : xSvg;
    btn.setAttribute('aria-label', label);
    btn.classList.add('is-flashing');
    window.setTimeout(() => {
      btn.innerHTML = origHtml;
      btn.setAttribute('aria-label', origAria);
      btn.classList.remove('is-flashing');
    }, durationMs);
  }

  private async copyAssistantText_(btn: HTMLButtonElement): Promise<void> {
    const pair = this.getLastQaPair_();
    if (!pair || !pair.answer) {
      this.flashButtonLabel_(btn, 'Empty');
      return;
    }
    try {
      await navigator.clipboard.writeText(pair.answer);
      this.flashButtonLabel_(btn, 'Copied');
    } catch (e) {
      console.warn('[dao] copy text failed', e);
      this.flashButtonLabel_(btn, 'Failed');
    }
  }

  private async shareAssistantAsImage_(btn: HTMLButtonElement):
      Promise<void> {
    const pair = this.getLastQaPair_();
    if (!pair) {
      this.flashButtonLabel_(btn, 'Empty');
      return;
    }
    try {
      const blob = await renderShareImage({
        question: pair.question,
        source: pair.source,
        answer: pair.answer,
      });
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const ClipboardItemCtor = (window as any).ClipboardItem;
      if (!ClipboardItemCtor || !navigator.clipboard?.write) {
        throw new Error('ClipboardItem API unavailable');
      }
      await navigator.clipboard.write(
          [new ClipboardItemCtor({'image/png': blob})]);
      this.flashButtonLabel_(btn, 'Shared');
    } catch (e) {
      console.warn('[dao] share image failed', e);
      this.flashButtonLabel_(btn, 'Failed');
    }
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

  // Unified page + selection probe. One native getPageInfo and (only when
  // the URL is capturable) one executeScript for the selection — both chips
  // update from the same tick, halving the round trips the previous split
  // refresh incurred.
  //
  // `refreshChipsRunning_` collapses bursts: pointerenter + focusin often
  // arrive back-to-back, and we don't want two concurrent executeScripts
  // racing to clobber each other's state. A leading-edge guard is enough —
  // if a second request arrives mid-flight we just drop it; the poll tick
  // or the next event will pick up any change.
  private refreshChipsRunning_ = false;
  private async refreshChips_() {
    if (this.refreshChipsRunning_) return;
    this.refreshChipsRunning_ = true;
    try {
      const info = await fetchCurrentPageInfo();
      if (!info || !isCapturablePageUrl(info.url)) {
        if (this.pendingPageAttachment_ !== null) {
          this.pendingPageAttachment_ = null;
        }
        if (this.pendingSelection_ !== null) {
          this.pendingSelection_ = null;
        }
        return;
      }
      // Page chip: suppress when URL is already in sent / dismissed sets.
      if (this.sentPageUrls_.has(info.url) ||
          this.dismissedUrls_.has(info.url)) {
        if (this.pendingPageAttachment_ !== null) {
          this.pendingPageAttachment_ = null;
        }
      } else {
        const current = this.pendingPageAttachment_;
        if (!current || current.url !== info.url ||
            current.title !== info.title) {
          this.pendingPageAttachment_ = info;
        }
      }
      // Selection chip: script injection, only when the page itself is
      // capturable (guarded above).
      const sel = await fetchCurrentSelection();
      const currentSel = this.pendingSelection_;
      if (!sel) {
        if (currentSel !== null) this.pendingSelection_ = null;
      } else if (!currentSel || currentSel.text !== sel.text ||
                 currentSel.url !== sel.url) {
        this.pendingSelection_ = sel;
      }
    } finally {
      this.refreshChipsRunning_ = false;
    }
  }

  // Back-compat wrappers used by startNewSession / the page-chip send flow.
  // Both chips share one probe now, so these just delegate.
  private refreshPageChip_() { return this.refreshChips_(); }
  private refreshSelectionChip_() { return this.refreshChips_(); }

  private onPageChipDismiss_() {
    const pending = this.pendingPageAttachment_;
    if (!pending) return;
    this.dismissedUrls_.add(pending.url);
    this.pendingPageAttachment_ = null;
  }

  private onSelectionChipDismiss_() {
    // Dismiss clears the chip immediately; next poll tick may re-show it
    // if the user still has a selection. This matches the "no memory" rule
    // — we don't track dismissed selections across ticks.
    this.pendingSelection_ = null;
    void clearCurrentSelection();
  }

  // Splice the current selection into the attachments list as a
  // <selected-text> block. Runs alongside maybeAttachPage_ — both can
  // attach on the same turn. After a successful attach we clear the tab's
  // selection so the same highlight isn't silently re-sent next turn.
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private async maybeAttachSelection_(attachments: any[]): Promise<any[]> {
    // Re-probe at send time so a selection made between the last 2s tick
    // and the send button still gets picked up.
    const fresh = await fetchCurrentSelection();
    const sel = fresh || this.pendingSelection_;
    if (!sel || !sel.text) return attachments;
    const att = buildSelectionAttachment(sel);
    this.pendingSelection_ = null;
    // Fire-and-forget: clearing in the tab is UX, not correctness. If it
    // fails the next poll will pick up whatever selection still exists.
    void clearCurrentSelection();
    return [...attachments, att];
  }

  private selectionPreview_(text: string): string {
    const collapsed = text.replace(/\s+/g, ' ').trim();
    const cap = 60;
    if (collapsed.length <= cap) return collapsed;
    return collapsed.slice(0, cap) + '…';
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

  // BASE_SYSTEM_PROMPT + current SOUL.md, with the soul wrapped in <soul>
  // tags so the LLM can clearly delineate personality from base instructions
  // (and so downstream tooling can find/replace the soul block). Rebuilt
  // fresh on each call so callers get the latest saved soul.
  private buildSystemPrompt_(): string {
    return BASE_SYSTEM_PROMPT + '\n\n<soul>\n' + currentSoulContent +
        '\n</soul>';
  }

  // Rebuild the concatenated prompt from the latest soul and push it into
  // the live agent state. Called before each send (to cover same-tab edits
  // from the settings panel or the `update_soul` tool, which BroadcastChannel
  // does not echo to its own poster) and on cross-tab soulChannel messages.
  private refreshSystemPrompt_() {
    if (!this.agent_) return;
    refreshSoulContent();
    this.agent_.state.systemPrompt = this.buildSystemPrompt_();
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

  // ---- Slash-command skill picker ----

  // Tracks the `<message-editor>`'s outer height so the floating chip row
  // can hover a fixed gap above the composer. Without this the chip row
  // uses a magic-number `bottom` value and slides under the composer when
  // the user types multi-line input or attaches files.
  private attachComposerHeightObserver_(panel: PiChatPanel) {
    const editor = panel.querySelector('message-editor') as HTMLElement | null;
    if (!editor) return;
    this.composerHeightObserver_?.disconnect();
    const apply = (h: number) => {
      this.style.setProperty('--dao-composer-h', `${Math.round(h)}px`);
    };
    apply(editor.getBoundingClientRect().height);
    this.composerHeightObserver_ = new ResizeObserver((entries) => {
      for (const entry of entries) {
        apply(entry.contentRect.height);
      }
    });
    this.composerHeightObserver_.observe(editor);
  }

  private attachSkillPicker_(panel: PiChatPanel) {
    const ta = panel.querySelector('message-editor textarea') as
        HTMLTextAreaElement | null;
    if (!ta || ta === this.composerTextarea_) {
      return;
    }
    // pi-web-ui re-renders the editor: detach from any prior node first.
    if (this.composerTextarea_ && this.onComposerInput_) {
      this.composerTextarea_.removeEventListener('input', this.onComposerInput_);
    }
    if (this.composerTextarea_ && this.onComposerKeyDown_) {
      this.composerTextarea_.removeEventListener(
          'keydown', this.onComposerKeyDown_);
    }
    this.composerResizeObserver_?.disconnect();
    this.composerTextarea_ = ta;
    this.onComposerInput_ = () => this.updateSkillPicker_();
    this.onComposerKeyDown_ = (e) => this.onSkillPickerKeyDown_(e);
    ta.addEventListener('input', this.onComposerInput_);
    // Capture phase so we beat pi-web-ui's own keydown handling (Enter=send).
    ta.addEventListener('keydown', this.onComposerKeyDown_, true);
    // Re-anchor the picker when the composer autogrows or the sidebar is
    // resized. Observing the textarea catches multi-line growth; the
    // ancestor chain (message-editor, pi-chat-panel) is covered by window
    // resize + capture-phase scroll listeners set up when the picker opens.
    this.composerResizeObserver_ = new ResizeObserver(() => {
      if (this.skillPickerVisible_) this.recomputeSkillPickerAnchor_();
    });
    this.composerResizeObserver_.observe(ta);
  }

  private updateSkillPicker_() {
    const ta = this.composerTextarea_;
    if (!ta) {
      this.hideSkillPicker_();
      return;
    }
    const value = ta.value;
    // Show only when the buffer is a single `/token` (no whitespace yet).
    const m = /^\/([A-Za-z0-9_-]*)$/.exec(value);
    if (!m) {
      this.hideSkillPicker_();
      return;
    }
    this.skillPickerQuery_ = (m[1] || '').toLowerCase();
    const all = getAllSkills();
    const q = this.skillPickerQuery_;
    const filtered = q === '' ? all : all.filter((s) => {
      return s.id.toLowerCase().includes(q) ||
             s.name.toLowerCase().includes(q);
    });
    if (filtered.length === 0) {
      this.hideSkillPicker_();
      return;
    }
    this.skillPickerSkills_ = filtered;
    this.skillPickerIndex_ =
        Math.min(this.skillPickerIndex_, filtered.length - 1);
    if (this.skillPickerIndex_ < 0) this.skillPickerIndex_ = 0;
    const wasVisible = this.skillPickerVisible_;
    this.skillPickerVisible_ = true;
    this.recomputeSkillPickerAnchor_();
    if (!wasVisible && !this.onWindowResizeForPicker_) {
      this.onWindowResizeForPicker_ = () => {
        if (this.skillPickerVisible_) this.recomputeSkillPickerAnchor_();
      };
      window.addEventListener('resize', this.onWindowResizeForPicker_);
      // Capture-phase scroll catches any ancestor scroll container so the
      // picker doesn't detach from the editor when the chat log scrolls.
      window.addEventListener(
          'scroll', this.onWindowResizeForPicker_, true);
    }
  }

  private recomputeSkillPickerAnchor_() {
    const ta = this.composerTextarea_;
    if (!ta) {
      this.skillPickerAnchor_ = null;
      return;
    }
    const r = ta.getBoundingClientRect();
    this.skillPickerAnchor_ = {
      left: r.left,
      top: r.top,
      width: r.width,
    };
  }

  private hideSkillPicker_() {
    if (!this.skillPickerVisible_ && this.skillPickerSkills_.length === 0) {
      return;
    }
    this.skillPickerVisible_ = false;
    this.skillPickerSkills_ = [];
    this.skillPickerIndex_ = 0;
    this.skillPickerQuery_ = '';
    this.skillPickerAnchor_ = null;
    if (this.onWindowResizeForPicker_) {
      window.removeEventListener('resize', this.onWindowResizeForPicker_);
      window.removeEventListener(
          'scroll', this.onWindowResizeForPicker_, true);
      this.onWindowResizeForPicker_ = null;
    }
  }

  private onSkillPickerKeyDown_(e: KeyboardEvent) {
    if (!this.skillPickerVisible_ || this.skillPickerSkills_.length === 0) {
      return;
    }
    const n = this.skillPickerSkills_.length;
    switch (e.key) {
      case 'ArrowDown':
        e.preventDefault();
        e.stopPropagation();
        this.skillPickerIndex_ = (this.skillPickerIndex_ + 1) % n;
        return;
      case 'ArrowUp':
        e.preventDefault();
        e.stopPropagation();
        this.skillPickerIndex_ =
            (this.skillPickerIndex_ - 1 + n) % n;
        return;
      case 'Enter':
      case 'Tab': {
        e.preventDefault();
        e.stopPropagation();
        const pick = this.skillPickerSkills_[this.skillPickerIndex_];
        if (pick) this.commitSkillPicker_(pick);
        return;
      }
      case 'Escape':
        e.preventDefault();
        e.stopPropagation();
        this.hideSkillPicker_();
        return;
    }
  }

  private commitSkillPicker_(skill: SkillRegistryEntry | undefined) {
    if (!skill) return;
    const ta = this.composerTextarea_;
    if (!ta) return;
    const next = '/' + skill.id + ' ';
    ta.value = next;
    // Dispatching a synthetic input event lets pi-web-ui's own binding
    // (<message-editor>) pick up the new value instead of dropping it on
    // the next re-render.
    ta.dispatchEvent(new Event('input', {bubbles: true}));
    ta.setSelectionRange(next.length, next.length);
    ta.focus();
    this.hideSkillPicker_();
    // Prefetch the SKILL.md body so send-time has zero extra latency.
    if (!this.skillInstructionsCache_.has(skill.id)) {
      void loadSkillInstructions(skill.id).then((c) => {
        if (c && c.instructions) {
          this.skillInstructionsCache_.set(skill.id, c.instructions);
        }
      });
    }
  }

  // Parse a leading `/skillId` from `text`. Returns null if the text does
  // not start with a known skill marker.
  private parseSkillPrefix_(text: string):
      {skillId: string, skill: SkillRegistryEntry, rest: string} | null {
    const m = /^\/([A-Za-z0-9_-]+)(?:\s+([\s\S]*))?$/.exec(text.trim());
    if (!m || !m[1]) return null;
    const skillId = m[1];
    const skill = getAllSkills().find((s) => s.id === skillId);
    if (!skill) return null;
    return {skillId, skill, rest: (m[2] || '').trim()};
  }

  // Warm the instructions cache before the message hits state.messages so
  // convertToLlm can splice the body in synchronously on the next turn.
  private async ensureSkillLoadedFromText_(text: string): Promise<void> {
    const parsed = this.parseSkillPrefix_(text);
    if (!parsed) return;
    if (this.skillInstructionsCache_.has(parsed.skillId)) return;
    const content = await loadSkillInstructions(parsed.skillId);
    if (content && content.instructions) {
      this.skillInstructionsCache_.set(parsed.skillId, content.instructions);
    }
  }

  // Synchronous splice used inside convertToLlm. Keeps the visible bubble
  // content (`/skillId rest`) untouched; expands into the LLM payload only.
  private expandSkillPrefix_(text: string): string {
    const parsed = this.parseSkillPrefix_(text);
    if (!parsed) return text;
    const instructions = this.skillInstructionsCache_.get(parsed.skillId);
    if (!instructions) return text;
    const head = '<skill name="' + parsed.skill.name + '">\n' + instructions +
        '\n</skill>';
    return parsed.rest ? head + '\n\n' + parsed.rest : head;
  }

  private renderSkillPicker_() {
    if (!this.skillPickerVisible_ || this.skillPickerSkills_.length === 0 ||
        !this.skillPickerAnchor_) {
      return nothing;
    }
    // Anchor above the composer textarea using viewport coords, capped at
    // ~45% of the viewport so the picker never overflows the top edge on
    // short windows. `bottom` positions the picker so its bottom edge sits
    // 8px above the textarea's top.
    const a = this.skillPickerAnchor_;
    const maxH = Math.max(160, Math.floor(window.innerHeight * 0.45));
    const bottom = Math.max(8, window.innerHeight - a.top + 8);
    const pickerStyle = `
      position: fixed;
      left: ${a.left}px;
      width: ${a.width}px;
      bottom: ${bottom}px;
      max-height: ${maxH}px;
    `;
    return html`
      <div class="dao-skill-picker" role="listbox"
          aria-label="Skill picker"
          style=${pickerStyle}>
        <div class="dao-skill-picker-head">
          Skills<span class="dao-skill-picker-hint">
            ↑↓ to navigate · Enter to select · Esc to dismiss
          </span>
        </div>
        <div class="dao-skill-picker-list">
          ${this.skillPickerSkills_.map((s, i) => html`
            <div class="dao-skill-picker-item ${
                i === this.skillPickerIndex_ ? 'selected' : ''}"
                role="option"
                aria-selected=${i === this.skillPickerIndex_}
                @mouseenter=${() => { this.skillPickerIndex_ = i; }}
                @mousedown=${(ev: Event) => {
                  // mousedown rather than click so the textarea keeps focus.
                  ev.preventDefault();
                  this.commitSkillPicker_(s);
                }}>
              <span class="dao-skill-picker-name">/${s.name}</span>
              <span class="dao-skill-picker-desc">${s.description}</span>
            </div>`)}
        </div>
      </div>`;
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
    this.pendingSelection_ = null;
    this.syncMeta_();
    void this.refreshPageChip_();
    void this.refreshSelectionChip_();
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    iface?.requestUpdate?.();
    setTimeout(() => this.focusInput(), 50);
  }
}

customElements.define('dao-chat-view', DaoChatView);
