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
import {buildAgentTools} from './pi_tool_adapter.js';
import {ensurePiAppStorage} from './pi_app_storage.js';
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
    maxTokens: 4096,
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
  protected messageCount_ = 0;
  protected tokenEstimate_ = 0;
  protected compacting_ = false;
  protected isStreaming_ = false;

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.unsubscribeAgent_?.();
    this.unsubscribeAgent_ = null;
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
      <pi-chat-panel></pi-chat-panel>`;
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
      iface.enableAttachments = false;
      iface.enableThinkingSelector = false;
      iface.requestUpdate?.();
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
      if (ev?.type === 'agent_end') {
        const agent = this.agent_;
        const panel = this.panel_;
        if (!agent || !panel) return;
        agent.waitForIdle().then(() => {
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          const iface = panel.querySelector('agent-interface') as any;
          iface?.requestUpdate?.();
          this.isStreaming_ = !!this.agent_?.state.isStreaming;
        });
      }
    });
    this.syncMeta_();
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
    this.syncMeta_();
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    iface?.requestUpdate?.();
    setTimeout(() => this.focusInput(), 50);
  }
}

customElements.define('dao-chat-view', DaoChatView);
