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

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.unsubscribeAgent_?.();
    this.unsubscribeAgent_ = null;
    try {
      this.agent_?.abort();
    } catch (_) { /* ignore */ }
  }

  override render() {
    return html`<pi-chat-panel></pi-chat-panel>`;
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

    // ChatPanel.setAgent() force-enables the model selector on its internal
    // <agent-interface>. Dao configures the provider/model from the settings
    // view, so we don't want the in-composer model picker — hide it.
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = panel.querySelector('agent-interface') as any;
    if (iface) {
      iface.enableModelSelector = false;
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
      }
      if (ev?.type === 'agent_end') {
        const agent = this.agent_;
        const panel = this.panel_;
        if (!agent || !panel) return;
        agent.waitForIdle().then(() => {
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          const iface = panel.querySelector('agent-interface') as any;
          iface?.requestUpdate?.();
        });
      }
    });
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
}

customElements.define('dao-chat-view', DaoChatView);
