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

import {addWebUIListener, BASE_SYSTEM_PROMPT, callNative, callNativeArgs, currentSoulContent, recordApiCall, refreshSoulContent, removeWebUIListener, soulChannel, type ProactiveSuggestionData} from './agent_bridge.js';
import {compactAgentMessages, estimateMessagesTokens} from './dao_compact.js';
import {addReusableElementContext, buildElementContextAttachment, consumeReusableElementContexts, getReusableElementContexts, removeReusableElementContext, type ElementContextCapture} from './dao_element_context.js';
import {buildMemoryContextText, hasMemoryContextPayload, type NativeMemoryContext} from './dao_memory_context.js';
import {getActiveLLMConfig} from './llm_config.js';
import {lookupModelCapabilities} from './model_capabilities.js';
import {buildPageAttachment, buildSelectionAttachment, cancelElementPicker, captureCurrentPageMarkdown, clearCurrentSelection, fetchCurrentPageInfo, fetchCurrentSelection, fetchPageProbeState, insertTextIntoFocusedInput, isCapturablePageUrl, startElementPicker, type PageInfo, type PiAttachment, type SelectionCapture} from './dao_page_capture.js';
import {
  copyPngBlobToClipboard,
  renderShareImage,
} from './dao_share_image.js';
import {reportTelemetryEvent} from './dao_telemetry.js';
import {lookupCostByModelId} from './llm_cost.js';
import {buildAgentTools} from './pi_tool_adapter.js';
import {toolConfigChannel} from './tool_catalog.js';
import './dao_chat_history_panel.js';
import {ensurePiAppStorage, syncActiveKeyToPiStorage} from './pi_app_storage.js';
import {buildAvailableSkillsPrompt, getAllSkills, initSkillRegistry, loadSkillInstructions, refreshSkillRegistry, refreshSkillRegistryIfStale, type SkillRegistryEntry} from './skill_registry.js';
import {t} from './i18n/i18n.js';
// eslint-disable-next-line @typescript-eslint/no-explicit-any
import * as pi from './vendor/pi_runtime_bundle.js';

// Structural types — the bundle ships `@ts-nocheck`, so imports above are
// all typed `any`. We name the small surface we actually touch.

// Latest unviewed dream report, loaded via getUnviewedDreamReport and
// rendered as a card at the top of the chat view.
interface DreamReportData {
  id: number;
  dreamDate: string;
  reportMarkdown: string;
  habits: Array<{
    key: string; value: string; confidence: number; evidence: string;
    relation: 'new'|'reinforce'|'contradict';
  }>;
  debugMaterialJson: string;
}

interface DaoMessageMetadata {
  id: string;
  editedAt?: string;
}

type DaoChatMessage = {
  role?: string;
  content?: unknown;
  attachments?: unknown[];
  timestamp?: unknown;
  dao?: DaoMessageMetadata;
};

interface DaoAssistantPair {
  question: string;
  source?: {title: string; domain: string};
  answer: string;
}

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
  const caps = lookupModelCapabilities(modelId);
  return {
    id: modelId,
    name: modelId,
    api: 'openai-completions',
    provider: 'openai',
    baseUrl: base,
    reasoning: false,
    input: ['text', 'image'],
    cost: lookupCostByModelId(modelId),
    contextWindow: caps.contextWindow,
    maxTokens: caps.maxTokens,
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

// Renders the assistant's Markdown answer to a self-contained HTML
// fragment suitable for the system clipboard's text/html slot. Uses the
// same `marked` singleton mini-lit's <markdown-block> renders with so
// the pasted output mirrors the chat bubble's structure (paragraphs,
// lists, code, emphasis, links). Unlike <markdown-block>, we go through
// the default renderer so the output is plain HTML — no <code-block>
// custom elements that other apps cannot interpret.
function renderAssistantMarkdown(markdown: string): string {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const m = (pi as any).marked;
  try {
    const out = m?.parse?.(markdown, {async: false}) ?? m?.(markdown);
    if (typeof out === 'string') return out;
  } catch (e) {
    console.warn('[dao] markdown render failed; falling back to <pre>', e);
  }
  // Fallback: wrap the raw text in <pre> so the clipboard still has
  // something rich-text targets can paste, with the line breaks intact.
  return '<pre>' + escapeHtml(markdown) + '</pre>';
}

function escapeHtml(s: string): string {
  return s.replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
}

function escapeAttr(s: string): string {
  return s.replace(/&/g, '&amp;')
      .replace(/"/g, '&quot;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
}

function utf8ToBase64(s: string): string {
  const bytes = new TextEncoder().encode(s);
  let binary = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode.apply(
        null, Array.from(bytes.subarray(i, i + chunk)) as number[]);
  }
  return btoa(binary);
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

const MAX_PROACTIVE_PAGE_CONTENT_CHARS = 12000;
const DISMISSED_DREAM_REPORT_IDS_KEY = 'dao_dismissed_dream_report_ids';
const DAO_AGENT_DEBUG_MODE_KEY = 'dao_agent_debug_mode';
const DAO_AGENT_DEBUG_MODE_CHANGED_EVENT = 'dao-agent-debug-mode-changed';

function yesterdayLocalYmd(now = new Date()): string {
  const yesterday = new Date(now);
  yesterday.setDate(yesterday.getDate() - 1);
  const year = yesterday.getFullYear();
  const month = String(yesterday.getMonth() + 1).padStart(2, '0');
  const day = String(yesterday.getDate()).padStart(2, '0');
  return `${year}-${month}-${day}`;
}

function readDismissedDreamReportIds(): Set<number> {
  try {
    const parsed = JSON.parse(
        localStorage.getItem(DISMISSED_DREAM_REPORT_IDS_KEY) || '[]');
    if (!Array.isArray(parsed)) {
      return new Set();
    }
    return new Set(parsed.filter((id): id is number =>
      typeof id === 'number' && Number.isFinite(id)));
  } catch {
    return new Set();
  }
}

function writeDismissedDreamReportIds(ids: Set<number>) {
  localStorage.setItem(
      DISMISSED_DREAM_REPORT_IDS_KEY,
      JSON.stringify(Array.from(ids).slice(-100)));
}

export class DaoChatView extends CrLitElement {
  static override get properties() {
    return {
      messageCount_: {type: Number, state: true},
      tokenEstimate_: {type: Number, state: true},
      compacting_: {type: Boolean, state: true},
      isStreaming_: {type: Boolean, state: true},
      editingMessageId_: {type: String, state: true},
      editingDraft_: {type: String, state: true},
      editingError_: {type: String, state: true},
      userActionMenuMessageId_: {type: String, state: true},
      debugMode_: {type: Boolean, state: true},
      debugContextMessageId_: {type: String, state: true},
      pendingPageAttachment_: {state: true},
      pendingSelection_: {state: true},
      pendingElementContexts_: {state: true},
      elementPicking_: {type: Boolean, state: true},
      skillPickerVisible_: {state: true},
      skillPickerSkills_: {state: true},
      skillPickerIndex_: {state: true},
      skillPickerAnchor_: {state: true},
      skillCatalogPrompt_: {state: true},
      historyOpen_: {state: true},
      currentSessionId_: {state: true},
      dreamReport_: {state: true},
      dreamExpanded_: {type: Boolean, state: true},
      proactiveSuggestion_: {state: true},
      proactiveRunning_: {type: Boolean, state: true},
    };
  }

  // Light DOM so <pi-chat-panel> and its descendants pick up the global
  // Tailwind stylesheet linked from agent.html. Without this, pi-web-ui's
  // Tailwind utility classes render unstyled inside a shadow root.

  constructor() {
    super();
    this.mountReady_ = new Promise<void>((resolve) => {
      this.mountReadyResolve_ = resolve;
    });
    this.tokenEstimate_ = 0;
    this.compacting_ = false;
    this.isStreaming_ = false;
    this.editingMessageId_ = '';
    this.editingDraft_ = '';
    this.editingError_ = '';
    this.userActionMenuMessageId_ = '';
    this.userActionMenuOpenAbove_ = false;
    this.debugMode_ = this.readDebugMode_();
    this.debugContextMessageId_ = '';
    this.pendingPageAttachment_ = null;
    this.pendingSelection_ = null;
    this.pendingElementContexts_ = getReusableElementContexts();
    this.elementPicking_ = false;
    this.skillPickerVisible_ = false;
    this.skillPickerSkills_ = [];
    this.skillPickerIndex_ = 0;
    this.skillCatalogPrompt_ = '';
    this.historyOpen_ = false;
    this.currentSessionId_ = '';
    this.messageCount_ = 0;
    this.dreamReport_ = null;
    this.dreamExpanded_ = false;
    this.proactiveSuggestion_ = null;
    this.proactiveRunning_ = false;
  }
  override createRenderRoot(): HTMLElement | DocumentFragment {
    return this;
  }

  override connectedCallback() {
    super.connectedCallback();
    void this.loadDreamReport_();
    this.onDreamReportUpdated_ = () => void this.loadDreamReport_();
    window.addEventListener(
        'dao-dream-report-updated', this.onDreamReportUpdated_);
    this.boundOnProactiveSuggestion_ =
        (raw: unknown) => this.onProactiveSuggestion_(raw);
    addWebUIListener(
        'proactiveSuggestion', this.boundOnProactiveSuggestion_);
    this.boundOnDebugModeChanged_ =
        (event: Event) => this.onDebugModeChanged_(event);
    window.addEventListener(
        DAO_AGENT_DEBUG_MODE_CHANGED_EVENT, this.boundOnDebugModeChanged_);
    this.boundOnStorageChanged_ =
        (event: StorageEvent) => this.onStorageChanged_(event);
    window.addEventListener('storage', this.boundOnStorageChanged_);
    if (localStorage.getItem('dao_proactive_enabled') === 'false') {
      callNativeArgs('setProactiveEnabled', false).catch(() => {});
    }
  }

  private agent_: PiAgent | null = null;
  private panel_: PiChatPanel | null = null;
  private mounted_ = false;
  // Resolves once mount_() has finished (including maybeResumeLastSession_).
  // Awaited by submitExternalPrompt so the resume probe can't land after a
  // Cmd+L/Cmd+T-driven startNewSession() and re-hydrate the old conversation.
  private mountReady_: Promise<void>;
  // Definite-assignment: assigned in the constructor's Promise executor.
  // A no-op default would silently swallow any pre-construction resolve.
  private mountReadyResolve_!: () => void;
  // Guards maybeResumeLastSession_ from racing an in-flight external submit
  // across its `await getAllMetadata()`.
  private externalSubmitInFlight_ = false;
  // One-shot flag consumed by the monkey-patched sendMessage to skip
  // maybeAttachPage_/maybeAttachSelection_ on the first turn of a Cmd+T
  // session — the user asked a standalone question, not one about the page
  // they happen to be on.
  private suppressChipAttachOnce_ = false;
  private unsubscribeAgent_: (() => void) | null = null;
  private compactAbort_: AbortController | null = null;
  private boundOnConfigChanged_: (() => void) | null = null;
  private boundOnSoulChanged_: (() => void) | null = null;
  private boundOnToolConfigChanged_: (() => void) | null = null;
  private boundOnChipHint_: (() => void) | null = null;
  private boundOnVisibilityHint_: (() => void) | null = null;
  private boundOnProactiveSuggestion_:
      ((raw: unknown) => void) | null = null;
  private boundOnDebugModeChanged_: ((event: Event) => void) | null = null;
  private boundOnStorageChanged_: ((event: StorageEvent) => void) | null =
      null;
  private boundOnUserActionMenuOutsidePointerDown_:
      ((event: Event) => void) | null = null;
  declare protected messageCount_: number;
  declare protected tokenEstimate_: number;
  declare protected compacting_: boolean;
  declare protected isStreaming_: boolean;
  declare protected editingMessageId_: string;
  declare protected editingDraft_: string;
  declare protected editingError_: string;
  declare protected userActionMenuMessageId_: string;
  declare protected debugMode_: boolean;
  declare protected debugContextMessageId_: string;
  // Current-page chip: renders as a small pill above the composer with the
  // active tab's title. Cleared when the URL is already in one of the
  // session sets (sent / dismissed) or the tab is a non-capturable surface
  // (chrome://, about:blank, ...).
  declare protected pendingPageAttachment_: PageInfo | null;
  private userActionMenuOpenAbove_ = false;
  private userActionMenuAlignStart_ = false;

  // Latest text selection from the active tab. Refreshed by the same 2s
  // poll that drives the page chip; displayed as its own chip below the
  // composer. Independent of pendingPageAttachment_ — both chips can be
  // active at once and the LLM will receive both blocks. Not de-duped:
  // every send picks up whatever the selection currently is, and the
  // selection is cleared in the tab after a successful send.
  declare protected pendingSelection_: SelectionCapture | null;

  // User-picked element contexts from the active WebContents. These follow
  // text selection semantics: once attached to a send, the chips are cleared.
  declare protected pendingElementContexts_: ElementContextCapture[];
  declare protected elementPicking_: boolean;

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
  private pendingMemoryContextText_: string | null = null;

  // Slash-command skill picker state. When the composer textarea starts with
  // `/query` (no space), a floating list of matching skills is shown.
  // Selection rewrites the input to `/skillId ` and dispatches an input
  // event so pi-web-ui picks up the new value. At send time, a leading
  // `/skillId ...` is expanded to the skill's SKILL.md instructions plus the
  // user's remaining text.
  declare protected skillPickerVisible_: boolean;
  declare protected skillPickerSkills_: SkillRegistryEntry[];
  declare protected skillPickerIndex_: number;
  // viewport coords of the composer textarea, recomputed whenever the
  // picker is visible; drives the picker's `position: fixed` anchor so it
  // sits exactly above the editor regardless of composer height changes
  // (multi-line autogrow, attachment chips, etc.).
  protected skillPickerAnchor_:
      {left: number, top: number, width: number} | null = null;
  private skillPickerQuery_ = '';
  private skillInstructionsCache_ = new Map<string, string>();
  declare private skillCatalogPrompt_: string;
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

  // History / persistence state. `currentSessionId_` is blank for a fresh
  // session that has not produced any message yet — the first save after
  // `message_end` lazily mints a UUID and records a row in pi-web-ui's
  // SessionsStore. Kept non-empty across `message_end` events so subsequent
  // saves update the same row.
  declare protected historyOpen_: boolean;
  declare protected currentSessionId_: string;
  private saveSessionScheduled_ = false;

  // Dream Analysis morning-report card state. `dreamReport_` holds the
  // latest unviewed report (null = no card); habit rows track per-index
  // confirm/reject so the buttons collapse into a status label.
  declare protected dreamReport_: DreamReportData|null;
  declare protected dreamExpanded_: boolean;
  private onDreamReportUpdated_: (() => void)|null = null;
  declare protected proactiveSuggestion_: ProactiveSuggestionData|null;
  declare protected proactiveRunning_: boolean;

  override disconnectedCallback() {
    super.disconnectedCallback();
    if (this.elementPicking_) void cancelElementPicker();
    if (this.onDreamReportUpdated_) {
      window.removeEventListener(
          'dao-dream-report-updated', this.onDreamReportUpdated_);
      this.onDreamReportUpdated_ = null;
    }
    if (this.boundOnProactiveSuggestion_) {
      removeWebUIListener(
          'proactiveSuggestion', this.boundOnProactiveSuggestion_);
      this.boundOnProactiveSuggestion_ = null;
    }
    if (this.boundOnDebugModeChanged_) {
      window.removeEventListener(
          DAO_AGENT_DEBUG_MODE_CHANGED_EVENT,
          this.boundOnDebugModeChanged_);
      this.boundOnDebugModeChanged_ = null;
    }
    if (this.boundOnStorageChanged_) {
      window.removeEventListener('storage', this.boundOnStorageChanged_);
      this.boundOnStorageChanged_ = null;
    }
    this.detachUserActionMenuOutsideListener_();
    // Unblock anything awaiting mount; safe to call when already resolved.
    this.mountReadyResolve_();
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
    if (this.boundOnToolConfigChanged_) {
      toolConfigChannel.removeEventListener(
          'message', this.boundOnToolConfigChanged_);
      window.removeEventListener(
          'dao-tool-config-changed', this.boundOnToolConfigChanged_);
      this.boundOnToolConfigChanged_ = null;
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
    const showEmptyGuide =
        this.messageCount_ === 0 && !this.isStreaming_ && !this.compacting_;

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
        .dao-proactive-card {
          --dao-proactive-card-bg: rgba(255, 255, 255, 0.62);
          --dao-proactive-card-border: rgba(70, 120, 190, 0.24);
          --dao-proactive-card-shadow: 0 1px 4px rgba(30, 20, 40, 0.08);
          --dao-proactive-icon-color: rgba(70, 120, 190, 0.95);
          --dao-proactive-btn-bg: rgba(127, 127, 127, 0.10);
          --dao-proactive-btn-border: rgba(127, 127, 127, 0.20);
          --dao-proactive-btn-color:
              var(--text-secondary, rgba(30, 20, 40, 0.70));
          --dao-proactive-btn-hover-bg: rgba(127, 127, 127, 0.18);
          --dao-proactive-primary-bg: rgba(70, 120, 190, 0.92);
          --dao-proactive-primary-border: rgba(70, 120, 190, 0.92);
          --dao-proactive-primary-hover-bg: rgba(58, 102, 166, 0.96);
          margin: 8px 12px 0;
          padding: 12px;
          border: 1px solid var(--dao-proactive-card-border);
          border-radius: 12px;
          background: var(--dao-proactive-card-bg);
          color: var(--text, rgba(30, 20, 40, 0.92));
          box-shadow: var(--dao-proactive-card-shadow);
          flex-shrink: 0;
        }
        .dao-proactive-head {
          display: flex;
          align-items: center;
          gap: 8px;
          min-width: 0;
        }
        .dao-proactive-icon {
          flex: 0 0 auto;
          width: 18px;
          height: 18px;
          color: var(--dao-proactive-icon-color);
        }
        .dao-proactive-copy {
          display: flex;
          flex-direction: column;
          gap: 2px;
          min-width: 0;
          flex: 1 1 auto;
        }
        .dao-proactive-title {
          font-size: 13px;
          font-weight: 600;
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }
        .dao-proactive-hint {
          font-size: 11px;
          color: var(--text-secondary, rgba(30, 20, 40, 0.58));
          white-space: nowrap;
          overflow: hidden;
          text-overflow: ellipsis;
        }
        .dao-proactive-actions {
          display: flex;
          gap: 6px;
          margin-top: 10px;
        }
        .dao-proactive-btn {
          font: inherit;
          font-size: 11px;
          border-radius: 8px;
          padding: 4px 10px;
          cursor: pointer;
          border: 1px solid var(--dao-proactive-btn-border);
          background: var(--dao-proactive-btn-bg);
          color: var(--dao-proactive-btn-color);
        }
        .dao-proactive-btn.primary {
          color: white;
          border-color: var(--dao-proactive-primary-border);
          background: var(--dao-proactive-primary-bg);
        }
        .dao-proactive-btn:hover:not(:disabled) {
          background: var(--dao-proactive-btn-hover-bg);
        }
        .dao-proactive-btn.primary:hover:not(:disabled) {
          background: var(--dao-proactive-primary-hover-bg);
        }
        .dao-proactive-btn:disabled {
          opacity: 0.55;
          cursor: not-allowed;
        }
        @media (prefers-color-scheme: dark) {
          .dao-proactive-card {
            --dao-proactive-card-bg: rgba(255, 255, 255, 0.06);
            --dao-proactive-card-border: rgba(70, 120, 190, 0.35);
            --dao-proactive-card-shadow: 0 1px 4px rgba(0, 0, 0, 0.25);
            --dao-proactive-icon-color: rgba(170, 200, 240, 0.95);
            --dao-proactive-btn-bg: rgba(255, 255, 255, 0.04);
            --dao-proactive-btn-border: rgba(255, 255, 255, 0.12);
            --dao-proactive-btn-color: rgba(255, 255, 255, 0.65);
            --dao-proactive-btn-hover-bg: rgba(70, 120, 190, 0.24);
            --dao-proactive-primary-bg: rgba(70, 120, 190, 0.72);
            --dao-proactive-primary-border: rgba(70, 120, 190, 0.72);
            --dao-proactive-primary-hover-bg: rgba(58, 102, 166, 0.82);
          }
        }
      </style>
      ${showBar ? html`
        <div class="dao-compact-bar ${stateClass}">
          <div class="meta">
            <span class="gauge"
                title=${ctx > 0
                    ? t('chat.gauge.tooltip_with_capacity', {
                        tokens: this.tokenEstimate_,
                        capacity: ctx,
                        percent: ratioPct,
                      })
                    : t('chat.gauge.tooltip_no_capacity', {
                        tokens: this.tokenEstimate_,
                      })}>
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
                    ? t('chat.compact.cancel_tooltip')
                    : t('chat.compact.start_tooltip')}>
              ${this.compacting_
                  ? html`<span class="spinner"></span>${t('chat.compact.compacting')}`
                  : html`${t('chat.compact.label')}`}
            </button>
          </div>
        </div>` : ''}
      ${showEmptyGuide ? html`
        <div class="dao-empty-guide" aria-hidden="true">
          <svg class="dao-empty-guide-icon" viewBox="0 0 24 24"
              fill="none" stroke="currentColor" stroke-width="2"
              stroke-linecap="round" stroke-linejoin="round">
            <path d="M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 0 1 0 1.966l-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558-1.051a2 2 0 0 0 1.594-1.594z"></path>
            <path d="M20 2v4"></path>
            <path d="M22 4h-4"></path>
            <circle cx="4" cy="20" r="2"></circle>
          </svg>
          <div class="dao-empty-guide-title">${t('chat.empty_guide.title')}</div>
          <div class="dao-empty-guide-hint">
            ${t('chat.empty_guide.hint')}
          </div>
        </div>` : ''}
      ${this.renderDreamCard_()}
      ${this.renderProactiveCard_()}
      <pi-chat-panel></pi-chat-panel>
      <dao-chat-history-panel
          ?open=${this.historyOpen_}
          .currentSessionId=${this.currentSessionId_}
          @history-close=${this.onHistoryClose_}
          @history-select=${this.onHistorySelect_}
          @history-deleted=${this.onHistoryDeleted_}>
      </dao-chat-history-panel>
      ${this.renderSkillPicker_()}
      ${this.renderUserContextModal_()}
      <div class="dao-page-chip-row">
        <button class=${'dao-element-pick-button' +
            (this.elementPicking_ ? ' active' : '')}
            @click=${this.onElementPickClick_}
            title=${this.elementPicking_ ?
              t('chat.attach.element.pick_active_tooltip') :
              t('chat.attach.element.pick_tooltip')}
            aria-label=${this.elementPicking_ ?
              t('chat.attach.element.pick_active_tooltip') :
              t('chat.attach.element.pick_tooltip')}>
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
              stroke-width="2" stroke-linecap="round"
              stroke-linejoin="round" aria-hidden="true">
            <circle cx="12" cy="12" r="3"></circle>
            <path d="M12 2v4"></path>
            <path d="M12 18v4"></path>
            <path d="M2 12h4"></path>
            <path d="M18 12h4"></path>
          </svg>
        </button>
        ${(this.pendingPageAttachment_ || this.pendingSelection_ ||
           this.pendingElementContexts_.length > 0) ? html`
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
                  title=${t('chat.attach.page.dismiss_title')}>
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
                <span class="dao-page-chip-domain">${t('chat.attach.selection.label')}</span>
              </span>
              <button class="dao-page-chip-close"
                  @click=${this.onSelectionChipDismiss_}
                  title=${t('chat.attach.selection.dismiss_title')}>
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                    stroke-width="2" stroke-linecap="round"
                    stroke-linejoin="round" aria-hidden="true">
                  <path d="M18 6 6 18"></path>
                  <path d="m6 6 12 12"></path>
                </svg>
              </button>
            </div>` : ''}
          ${this.pendingElementContexts_.map(context => html`
            <div class="dao-page-chip dao-element-chip"
                title=${context.text || context.label}>
              <span class="dao-page-chip-favicon">
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                    stroke-width="2" stroke-linecap="round"
                    stroke-linejoin="round" aria-hidden="true">
                  <path d="M4 4h16v16H4z"></path>
                  <path d="M9 9h6v6H9z"></path>
                </svg>
              </span>
              <span class="dao-page-chip-text">
                <span class="dao-page-chip-title">
                  ${this.selectionPreview_(context.label)}
                </span>
                <span class="dao-page-chip-domain">
                  ${t('chat.attach.element.label')}
                </span>
              </span>
              <button class="dao-page-chip-close"
                  @click=${() => this.onElementContextDismiss_(
                    context.contextId || '')}
                  title=${t('chat.attach.element.dismiss_title')}>
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                    stroke-width="2" stroke-linecap="round"
                    stroke-linejoin="round" aria-hidden="true">
                  <path d="M18 6 6 18"></path>
                  <path d="m6 6 12 12"></path>
                </svg>
              </button>
            </div>`)}
        ` : ''}
      </div>`;
  }

  override firstUpdated() {
    this.panel_ = this.querySelector('pi-chat-panel') as PiChatPanel;
    void this.mount_();
  }

  private readDebugMode_(): boolean {
    return localStorage.getItem(DAO_AGENT_DEBUG_MODE_KEY) === 'true';
  }

  private setDebugModeState_(enabled: boolean) {
    this.debugMode_ = enabled;
    if (!enabled) {
      this.debugContextMessageId_ = '';
    }
    this.refreshMessageActions_();
  }

  private onDebugModeChanged_(event: Event) {
    const enabled = event instanceof CustomEvent &&
        typeof event.detail?.enabled === 'boolean' ?
        event.detail.enabled :
        this.readDebugMode_();
    this.setDebugModeState_(enabled);
  }

  private onStorageChanged_(event: StorageEvent) {
    if (event.key !== DAO_AGENT_DEBUG_MODE_KEY) return;
    this.setDebugModeState_(event.newValue === 'true');
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
        const pendingMemory = this.pendingMemoryContextText_;
        let pendingMemoryUserIndex = -1;
        if (pendingMemory) {
          for (let i = msgs.length - 1; i >= 0; i--) {
            const role = msgs[i]?.role;
            if (role === 'user' || role === 'user-with-attachments') {
              pendingMemoryUserIndex = i;
              break;
            }
          }
        }
        for (let i = 0; i < msgs.length; i++) {
          const m = msgs[i];
          if (!m) continue;
          if (m.role === 'assistant' || m.role === 'toolResult') {
            out.push(m);
          } else if (m.role === 'user') {
            const hasPendingMemory =
                !!pendingMemory && i === pendingMemoryUserIndex;
            const orig = typeof m.content === 'string' ?
                m.content :
                (hasPendingMemory ? this.extractAssistantText_(m) : '');
            const expanded = this.expandSkillPrefix_(orig);
            if (hasPendingMemory) {
              const trimmed = expanded.trim();
              out.push({
                role: 'user',
                content: trimmed ? `${pendingMemory}\n\n${expanded}` :
                                   pendingMemory,
                timestamp: m.timestamp,
              });
            } else if (expanded === orig) {
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
            if (pendingMemory && i === pendingMemoryUserIndex) {
              pieces.push(pendingMemory);
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
      iface.enableAttachments = false;
      iface.enableThinkingSelector = false;
      iface.requestUpdate?.();

      // Intercept sendMessage so Dao can gather automatic context before
      // the turn starts. Visible page / selection / element context still
      // travels as pi-web-ui attachments; memory context is kept in
      // pendingMemoryContextText_ and spliced only inside convertToLlm so
      // it never renders as a user-visible attachment.
      if (typeof iface.sendMessage === 'function' && !this.origSendMessage_) {
        this.origSendMessage_ = iface.sendMessage.bind(iface);
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        iface.sendMessage = async (text: string, attachments: any[]) => {
          let turnTargetStarted = false;
          this.pendingMemoryContextText_ = null;
          try {
            try {
              await callNative('beginAgentTurn');
              turnTargetStarted = true;
            } catch (e) {
              console.warn('[dao-agent] beginAgentTurn failed', e);
            }

            reportTelemetryEvent('agent_message_send', {
              textLength: text?.length ?? 0,
              attachmentCount: attachments?.length ?? 0,
            });
            this.refreshModel_();
            // Pull latest soul into the live systemPrompt before the turn is
            // packed into the LLM request. Handles the same-tab update_soul
            // path (BroadcastChannel doesn't echo to its own document).
            await this.refreshSkillCatalogPrompt_();
            this.refreshSystemPrompt_();
            // Keep the user's typed `/skill rest` visible in the bubble;
            // load the SKILL.md body into the session cache so convertToLlm
            // can splice it synchronously at LLM-conversion time.
            await this.ensureSkillLoadedFromText_(text);
            // Cmd+T external submits suppress page/selection on their first
            // turn (set by submitExternalPrompt); subsequent turns fall through
            // to the normal chip-attach path.
            let merged = attachments || [];
            if (this.suppressChipAttachOnce_) {
              this.suppressChipAttachOnce_ = false;
            } else {
              const withPage = await this.maybeAttachPage_(merged);
              const withSelection = await this.maybeAttachSelection_(withPage);
              const withElement =
                  await this.maybeAttachElementContext_(withSelection);
              merged = withElement;
              this.pendingMemoryContextText_ =
                  await this.maybeBuildMemoryContextText_();
            }
            return await this.origSendMessage_!(text, merged);
          } finally {
            this.pendingMemoryContextText_ = null;
            if (turnTargetStarted) {
              try {
                await callNative('endAgentTurn');
              } catch (e) {
                console.warn('[dao-agent] endAgentTurn failed', e);
              }
            }
          }
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
        this.ensureMessageIds_();
        this.syncMeta_();
        // If the just-ended message contained an `update_soul` tool call,
        // the on-disk soul is now stale relative to state.systemPrompt.
        // Refresh so the next round of this turn (or the next turn) sees
        // the updated personality without waiting for a user send.
        this.refreshSystemPrompt_();
        this.scheduleSaveSession_();
      }
      if (ev?.type === 'message_end') {
        // Each message_end corresponds to one assistant completion returned
        // from the provider. Pi-agent attaches the provider-reported usage
        // on ev.message.usage as {input, output, cacheRead, cacheWrite};
        // pi-ai's model.cost is in USD per 1M tokens.
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        const msg = (ev as any).message;
        const usage = msg?.usage;
        if (usage && (usage.input || usage.output)) {
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          const cost = (this.agent_?.state.model as any)?.cost;
          recordApiCall(
              Number(usage.input) || 0,
              Number(usage.output) || 0,
              Number(cost?.input) || 0,
              Number(cost?.output) || 0);
        }
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
          this.refreshMessageActions_();
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
        void this.maybeResumeLastSession_();
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

    // Same-window (dispatchEvent) + cross-tab (BroadcastChannel) notifications
    // when the user toggles a tool or group in Settings → Tools. Rebuild
    // the adapted tool array and push it to the live agent so the change
    // takes effect on the next turn without restarting the chat.
    this.boundOnToolConfigChanged_ = () => this.refreshTools_();
    toolConfigChannel.addEventListener(
        'message', this.boundOnToolConfigChanged_);
    window.addEventListener(
        'dao-tool-config-changed', this.boundOnToolConfigChanged_);

    // Auto-resume the most recent non-empty session on open. Gated by the
    // `Resume Last Session` setting (default on). We look at session
    // metadata rather than loading every session's messages, skip rows with
    // messageCount === 0 so a freshly-minted empty shell from a previous
    // open doesn't win, and fall back to the empty-session state on any
    // error. Runs after the agent + listeners are wired so loadSession_'s
    // state mutations are observed normally.
    //
    // Awaited (not fired-and-forgotten) so mountReady_ resolves only after
    // resume has settled, fencing it against submitExternalPrompt's reset.
    try {
      await this.maybeResumeLastSession_();
    } finally {
      this.mountReadyResolve_();
    }
  }

  private async maybeResumeLastSession_() {
    if (localStorage.getItem('dao_resume_last_session') === 'false') return;
    if (!this.agent_) return;
    // Always preserve in-flight work. Streaming would drop tokens mid-turn;
    // an external submit is about to overwrite session state with a fresh
    // prompt and racing it would re-hydrate the previous conversation.
    if (this.isStreaming_) return;
    if (this.externalSubmitInFlight_) return;

    const staleMs = this.getStaleThresholdMs_();
    const isStale = (lastModified: string | undefined) => {
      if (!staleMs || !lastModified) return false;
      const ts = Date.parse(lastModified);
      return Number.isFinite(ts) && Date.now() - ts > staleMs;
    };

    // Already showing a session (from a prior resume or because the user
    // sent something earlier). On a Cmd+E reopen / visibilitychange we
    // want to drop into a fresh conversation when that session has gone
    // stale, otherwise keep the live context untouched.
    const hasLiveSession =
        !!this.currentSessionId_ ||
        (this.agent_.state.messages?.length ?? 0) > 0;
    if (hasLiveSession) {
      // Unsaved fresh session (no id yet) — keep it. lastModified isn't
      // available until pi-storage persists the first message, and a
      // brand-new turn can't be stale anyway.
      if (!this.currentSessionId_) return;
      if (!staleMs) return;
      try {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        const storage = await ensurePiAppStorage() as any;
        if (this.isStreaming_) return;
        if (this.externalSubmitInFlight_) return;
        const meta =
            await storage.sessions.getMetadata?.(this.currentSessionId_) ??
            null;
        if (meta && isStale(meta.lastModified)) {
          this.startNewSession();
        }
      } catch (_) { /* ignore — keep the current session on lookup error */ }
      return;
    }

    // Blank post-mount state — resume the most recent non-empty session
    // unless it has gone stale.
    try {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const storage = await ensurePiAppStorage() as any;
      const items: Array<{id: string; lastModified: string;
                          messageCount: number}> =
          await storage.sessions.getAllMetadata();
      // Re-check after the await: an external submit may have started while
      // we were waiting on storage. Same reasoning as the pre-await guard.
      if (this.externalSubmitInFlight_) return;
      if (this.currentSessionId_) return;
      if ((this.agent_?.state.messages?.length ?? 0) > 0) return;
      if (!Array.isArray(items) || items.length === 0) return;
      const candidates = items
          .filter(m => m && (m.messageCount ?? 0) > 0)
          .sort((a, b) => b.lastModified.localeCompare(a.lastModified));
      const latest = candidates[0];
      if (!latest) return;
      if (isStale(latest.lastModified)) return;
      await this.loadSession_(latest.id);
    } catch (_) { /* ignore — stay on the empty session */ }
  }

  // Reads the user-configurable `dao_resume_stale_hours` setting (default 3,
  // 0 disables) and returns it in milliseconds. Returns 0 when stale-gating
  // is off so callers can skip the check entirely.
  private getStaleThresholdMs_(): number {
    const raw = localStorage.getItem('dao_resume_stale_hours');
    const hours = raw === null ? 3 : Number(raw);
    if (!Number.isFinite(hours) || hours <= 0) return 0;
    return hours * 3600_000;
  }

  // Debounced decoration pass — coalesces rapid-fire message_end events
  // (e.g. tool-result bursts) into a single DOM sweep after rendering
  // settles. Decorates both page attachments and code-block insert
  // buttons so finalized code blocks get our "Insert" affordance mid-
  // stream (the retry button is injected in the agent_end handler where
  // we know the turn is truly finished).
  private scheduleDecorate_(): void {
    clearTimeout(this.pendingDecorateTimer_);
    this.pendingDecorateTimer_ = window.setTimeout(() => {
      this.decoratePageAttachments_();
      this.decorateCodeBlocks_();
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

  // Pulls the visible text out of an assistant or user message. pi-agent-core
  // stores content either as a plain string (most LLM streams) or as an
  // array of Claude-style blocks (text / toolCall / toolResult). Only
  // 'text' parts are user-visible, so tool plumbing never leaks into
  // the share image or the clipboard.
  //
  // Also handles user messages: when sendMessage is called without
  // attachments, pi-mono's normalizePromptInput wraps the typed string into
  // [{type:'text', text:'...'}] before pushing into state.messages. Without
  // this array path, the share image would render an empty user bubble.
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

  private buildActionButton_(
      className: string, titleKey: string, svg: string,
      onClick: (btn: HTMLButtonElement) => void): HTMLButtonElement {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = className;
    btn.title = t(titleKey);
    btn.setAttribute('aria-label', t(titleKey));
    btn.innerHTML = svg;
    btn.addEventListener('click', () => onClick(btn));
    return btn;
  }

  private buildAssistantActionRow_(
      msg: DaoChatMessage, disabled: boolean, canRewind: boolean): HTMLElement {
    const row = document.createElement('div');
    row.className = 'dao-message-actions dao-assistant-actions';
    row.dataset['daoMessageId'] = msg.dao?.id || '';
    const copySvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>' +
        '<path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1">' +
        '</path></svg>';
    const imageSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect>' +
        '<circle cx="9" cy="9" r="2"></circle>' +
        '<path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"></path>' +
        '</svg>';
    const regenSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<path d="M3 12a9 9 0 1 0 3-6.7"></path>' +
        '<path d="M3 4v5h5"></path>' +
        '</svg>';
    const rewindSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<path d="m15 14 5-5-5-5"></path>' +
        '<path d="M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13"></path>' +
        '</svg>';
    const id = msg.dao?.id || '';
    const copy = this.buildActionButton_(
        'dao-copy-btn', 'chat.message_actions.copy_tooltip', copySvg,
        btn => void this.copyAssistantTextById_(id, btn));
    const image = this.buildActionButton_(
        'dao-share-btn', 'chat.message_actions.share_tooltip', imageSvg,
        btn => void this.shareAssistantAsImageById_(id, btn));
    const regen = this.buildActionButton_(
        'dao-retry-btn', 'chat.message_actions.regenerate_tooltip', regenSvg,
        () => void this.regenerateAssistantById_(id));
    const rewind = this.buildActionButton_(
        'dao-rewind-btn', 'chat.message_actions.rewind_tooltip', rewindSvg,
        () => void this.rewindToAssistantById_(id));
    copy.disabled = disabled;
    image.disabled = disabled;
    regen.disabled = disabled;
    rewind.disabled = disabled;
    row.append(copy, image, regen);
    if (canRewind) {
      row.appendChild(rewind);
    }
    return row;
  }

  private buildUserActionRow_(
      msg: DaoChatMessage, disabled: boolean): HTMLElement {
    const row = document.createElement('div');
    row.dataset['daoMessageId'] = msg.dao?.id || '';
    const id = msg.dao?.id || '';
    const isOpen = this.userActionMenuMessageId_ === id;
    row.className = 'dao-message-actions dao-user-actions' +
        (isOpen ? ' dao-user-actions-open' : '');
    // Lucide ellipsis, copied from lucide-icons/lucide main.
    const moreSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<circle cx="12" cy="12" r="1"></circle>' +
        '<circle cx="19" cy="12" r="1"></circle>' +
        '<circle cx="5" cy="12" r="1"></circle>' +
        '</svg>';
    const more = this.buildActionButton_(
        'dao-retry-btn dao-user-more-btn' + (isOpen ? ' is-selected' : ''),
        'chat.message_actions.more_tooltip',
        moreSvg,
        btn => this.toggleUserActionMenu_(id, btn));
    more.disabled = disabled;
    more.setAttribute('aria-haspopup', 'menu');
    more.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
    row.appendChild(more);
    if (isOpen) {
      row.appendChild(this.buildUserActionMenu_(id));
    }
    return row;
  }

  private buildUserActionMenu_(id: string): HTMLElement {
    const menu = document.createElement('div');
    menu.className = 'dao-user-action-menu' +
        (this.userActionMenuOpenAbove_ ? ' dao-user-action-menu-above' : '') +
        (this.userActionMenuAlignStart_ ?
             ' dao-user-action-menu-align-start' :
             '');
    menu.setAttribute('role', 'menu');
    // Lucide square-pen, copied from lucide-icons/lucide main.
    const editSvg =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<path d="M12 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14' +
        'a2 2 0 0 0 2-2v-7"></path>' +
        '<path d="M18.375 2.625a1 1 0 0 1 3 3l-9.013 9.014a2 2 ' +
        '0 0 1-.853.505l-2.873.84a.5.5 0 0 1-.62-.62l.84-2.873' +
        'a2 2 0 0 1 .506-.852z"></path>' +
        '</svg>';
    const edit = document.createElement('button');
    edit.type = 'button';
    edit.className = 'dao-user-menu-item dao-edit-menu-item';
    edit.setAttribute('role', 'menuitem');
    edit.innerHTML = editSvg + '<span>' + t('chat.message_actions.edit') +
        '</span>';
    edit.addEventListener('click', () => this.beginEditUserMessage_(id));
    menu.appendChild(edit);
    if (this.debugMode_) {
      // Lucide file-text, copied from lucide-icons/lucide main.
      const contextSvg =
          '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
          ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
          ' aria-hidden="true">' +
          '<path d="M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12' +
          'a2 2 0 0 0 2-2V7Z"></path>' +
          '<path d="M14 2v4a2 2 0 0 0 2 2h4"></path>' +
          '<path d="M10 9H8"></path>' +
          '<path d="M16 13H8"></path>' +
          '<path d="M16 17H8"></path>' +
          '</svg>';
      const context = document.createElement('button');
      context.type = 'button';
      context.className =
          'dao-user-menu-item dao-debug-context-menu-item';
      context.setAttribute('role', 'menuitem');
      context.innerHTML = contextSvg + '<span>' +
          t('chat.message_actions.view_context') + '</span>';
      context.addEventListener(
          'click', () => this.showUserContextDebug_(id));
      menu.appendChild(context);
    }
    return menu;
  }

  private insertUserActionRow_(host: HTMLElement, row: HTMLElement): void {
    const bubble = host.querySelector('.user-message-container');
    if (bubble instanceof HTMLElement) {
      const line = this.ensureUserMessageLine_(bubble);
      const markdown = line?.querySelector<HTMLElement>(
          ':scope > markdown-block');
      if (line && markdown) {
        line.insertBefore(row, markdown);
        return;
      }
    }
    const flex = host.querySelector('.flex.justify-start');
    if (flex instanceof HTMLElement && bubble instanceof HTMLElement &&
        bubble.parentElement === flex) {
      flex.insertBefore(row, bubble);
      return;
    }
    host.insertAdjacentElement('beforebegin', row);
  }

  private ensureUserMessageLine_(bubble: HTMLElement): HTMLElement | null {
    let line =
        bubble.querySelector<HTMLElement>(':scope > .dao-user-message-line');
    const markdownInLine =
        line?.querySelector<HTMLElement>(':scope > markdown-block') || null;
    const markdown =
        markdownInLine ||
        bubble.querySelector<HTMLElement>(':scope > markdown-block');
    if (!markdown) return line;
    if (!line) {
      line = document.createElement('div');
      line.className = 'dao-user-message-line';
      bubble.insertBefore(line, markdown);
    }
    if (markdown.parentElement !== line) {
      line.appendChild(markdown);
    }
    return line;
  }

  private insertUserAuxiliaryAfterMessage_(
      msg: DaoChatMessage, host: HTMLElement): void {
    const id = msg.dao?.id || '';
    if (this.editingMessageId_ === id) {
      host.insertAdjacentElement('afterend', this.buildInlineEditor_(id));
    }
  }

  private buildInlineEditor_(id: string): HTMLElement {
    const wrap = document.createElement('div');
    wrap.className = 'dao-user-edit-wrap';
    const textarea = document.createElement('textarea');
    textarea.className = 'dao-user-edit-input';
    textarea.value = this.editingDraft_;
    textarea.rows = Math.min(
        8, Math.max(2, this.editingDraft_.split('\n').length));
    textarea.addEventListener('input', e => this.onEditDraftInput_(e));
    textarea.addEventListener('keydown', e => this.onEditDraftKeydown_(e));
    const actions = document.createElement('div');
    actions.className = 'dao-user-edit-actions';
    const cancel = document.createElement('button');
    cancel.type = 'button';
    cancel.textContent = t('chat.message_actions.cancel_edit');
    cancel.addEventListener('click', () => this.cancelEditUserMessage_());
    const save = document.createElement('button');
    save.type = 'button';
    save.className = 'primary';
    save.textContent = t('chat.message_actions.save_edit');
    save.addEventListener(
        'click', () => void this.applyUserMessageEdit_(id, textarea.value));
    actions.append(cancel, save);
    wrap.append(textarea);
    if (this.editingError_) {
      const error = document.createElement('div');
      error.className = 'dao-user-edit-error';
      error.textContent = this.editingError_;
      wrap.appendChild(error);
    }
    wrap.append(actions);
    setTimeout(() => textarea.focus(), 0);
    return wrap;
  }

  private renderUserContextModal_() {
    if (!this.debugContextMessageId_) return nothing;
    const json = this.safeDebugJson_(
        this.buildUserContextDebugPayload_(this.debugContextMessageId_));
    return html`
      <div class="dao-user-context-scrim"
          role="presentation"
          @click=${(e: Event) => {
            if (e.target === e.currentTarget) {
              this.hideUserContextDebug_();
            }
          }}>
        <section class="dao-user-context-modal"
            role="dialog"
            aria-modal="true"
            aria-label=${t('chat.message_actions.context_title')}>
          <div class="dao-user-context-head">
            <div class="dao-user-context-title">
              ${t('chat.message_actions.context_title')}
            </div>
            <button class="dao-user-context-close"
                type="button"
                title=${t('chat.message_actions.context_close')}
                aria-label=${t('chat.message_actions.context_close')}
                @click=${() => this.hideUserContextDebug_()}>
              <svg viewBox="0 0 24 24" fill="none" stroke="currentColor"
                  stroke-width="2" stroke-linecap="round"
                  stroke-linejoin="round" aria-hidden="true">
                <path d="M18 6 6 18"></path>
                <path d="m6 6 12 12"></path>
              </svg>
            </button>
          </div>
          <pre class="dao-user-context-pre">${json}</pre>
        </section>
      </div>`;
  }

  private showUserContextDebug_(id: string): void {
    const idx = this.findMessageIndexByDaoId_(id);
    const msg = this.currentMessages_()[idx];
    if (!this.debugMode_ || !this.isUserMessage_(msg)) return;
    this.debugContextMessageId_ = id;
    this.editingMessageId_ = '';
    this.editingDraft_ = '';
    this.editingError_ = '';
    this.closeUserActionMenu_(false);
    this.refreshMessageActions_();
    window.setTimeout(() => {
      (this.querySelector(
          '.dao-user-context-close') as HTMLButtonElement|null)?.focus();
    }, 0);
  }

  private hideUserContextDebug_(): void {
    this.debugContextMessageId_ = '';
    this.refreshMessageActions_();
  }

  private buildUserContextDebugPayload_(id: string): Record<string, unknown> {
    const messages = this.currentMessages_();
    const messageIndex = this.findMessageIndexByDaoId_(id);
    const rawMessage = messageIndex >= 0 ? messages[messageIndex] : null;
    const messagesUpToMessage =
        messageIndex >= 0 ? messages.slice(0, messageIndex + 1) : [];
    return {
      messageId: id,
      messageIndex,
      systemPrompt: this.agent_?.state.systemPrompt ?? '',
      model: this.debugModelSnapshot_(this.agent_?.state.model),
      rawMessage,
      attachments:
          rawMessage && Array.isArray(rawMessage.attachments) ?
          rawMessage.attachments :
          [],
      rawMessagesUpToMessage: messagesUpToMessage,
      llmMessagesUpToMessage:
          this.buildDebugLlmMessages_(messagesUpToMessage),
    };
  }

  private debugModelSnapshot_(model: unknown): Record<string, unknown>|null {
    if (!isRecord(model)) return null;
    const keys = [
      'id',
      'name',
      'api',
      'provider',
      'baseUrl',
      'reasoning',
      'input',
      'contextWindow',
      'maxTokens',
    ];
    const out: Record<string, unknown> = {};
    for (const key of keys) {
      if (key in model) {
        out[key] = model[key];
      }
    }
    return out;
  }

  private buildDebugLlmMessages_(
      messages: DaoChatMessage[]): Array<Record<string, unknown>> {
    const out: Array<Record<string, unknown>> = [];
    for (const msg of messages) {
      if (!msg) continue;
      if (msg.role === 'assistant' || msg.role === 'toolResult') {
        out.push({
          role: msg.role,
          content: msg.content,
          timestamp: msg.timestamp,
        });
        continue;
      }
      if (msg.role === 'user') {
        const orig = typeof msg.content === 'string' ?
            msg.content :
            this.extractAssistantText_(msg);
        const expanded = this.expandSkillPrefix_(orig);
        out.push({
          role: 'user',
          content: typeof msg.content === 'string' || expanded !== orig ?
              expanded :
              msg.content,
          timestamp: msg.timestamp,
        });
        continue;
      }
      if (msg.role === 'user-with-attachments') {
        const pieces: string[] = [];
        const attachments =
            Array.isArray(msg.attachments) ? msg.attachments : [];
        for (const attachment of attachments) {
          if (isRecord(attachment) &&
              typeof attachment['extractedText'] === 'string' &&
              attachment['extractedText'].length > 0) {
            pieces.push(attachment['extractedText']);
          }
        }
        const orig = typeof msg.content === 'string' ?
            msg.content :
            this.extractAssistantText_(msg);
        const expanded = this.expandSkillPrefix_(orig);
        const trimmed = expanded.trim();
        out.push({
          role: 'user',
          content: pieces.length > 0 ?
              (trimmed ? `${pieces.join('\n\n')}\n\n${expanded}` :
                         pieces.join('\n\n')) :
              expanded,
          timestamp: msg.timestamp,
        });
      }
    }
    return out;
  }

  private safeDebugJson_(value: unknown): string {
    return JSON.stringify(value, (_key, item) => {
      if (typeof item === 'function') {
        return `[Function ${item.name || 'anonymous'}]`;
      }
      if (item instanceof Error) {
        return {
          name: item.name,
          message: item.message,
          stack: item.stack,
        };
      }
      if (item === undefined) {
        return '[undefined]';
      }
      return item;
    }, 2) || '';
  }

  private refreshMessageActions_(): void {
    const panel = this.panel_;
    if (!panel) return;
    panel.querySelectorAll(
        '.dao-message-actions, .dao-user-edit-wrap')
        .forEach(el => el.remove());
    this.ensureMessageIds_();
    const disabled = !!this.agent_?.state.isStreaming;
    const msgs = this.currentMessages_();
    const latestAssistantIdx = this.findLatestAssistantIndex_();
    const userEls = Array.from(panel.querySelectorAll('user-message'));
    const assistantEls = Array.from(panel.querySelectorAll('assistant-message'));
    let userCursor = 0;
    let assistantCursor = 0;
    for (const msg of msgs) {
      const role = msg.role;
      if (role === 'user' || role === 'user-with-attachments') {
        const el = userEls[userCursor++] as HTMLElement | undefined;
        if (el && msg.dao?.id) {
          const row = this.buildUserActionRow_(msg, disabled);
          this.insertUserActionRow_(el, row);
          this.insertUserAuxiliaryAfterMessage_(msg, el);
        }
      } else if (role === 'assistant') {
        const el = assistantEls[assistantCursor++] as HTMLElement | undefined;
        if (el && msg.dao?.id && this.isAssistantMessage_(msg)) {
          const idx = msgs.indexOf(msg);
          const canRewind = idx >= 0 && idx !== latestAssistantIdx;
          el.insertAdjacentElement(
              'afterend',
              this.buildAssistantActionRow_(msg, disabled, canRewind));
        }
      }
    }
    this.decorateCodeBlocks_();
  }

  private refreshAssistantActions_(): void {
    this.refreshMessageActions_();
  }

  // Decorates every <code-block> in the message list with a Dao-owned
  // "Insert into focused input" button placed immediately before the
  // vendor <copy-button>. Idempotent via the `data-dao-insert-decorated`
  // attribute. Skips blocks whose internal <copy-button> hasn't rendered
  // yet (streaming) — the next call (driven by refreshMessageActions_
  // or the loadSession_ retry chain) picks them up.
  //
  // Decorates BOTH assistant-message code blocks AND tool-result code
  // blocks; the .dao-has-focused-input gate (CSS-only) handles when the
  // button is actually visible, so over-decoration is harmless.
  private decorateCodeBlocks_(): void {
    const panel = this.panel_;
    if (!panel) return;
    const blocks = panel.querySelectorAll('code-block');
    for (const block of blocks) {
      // NOTE: if vendor ever rebuilds <code-block> children in place
      // (keeping the host element), this skip will leave the host
      // stamped but unbuttoned. Today vendor swaps the whole host on
      // re-render, so this is safe.
      if (block.hasAttribute('data-dao-insert-decorated')) continue;
      const copyBtn = block.querySelector('copy-button');
      if (!copyBtn) continue;  // Vendor lit element hasn't rendered yet.
      const btn = this.buildCodeInsertButton_(block);
      copyBtn.insertAdjacentElement('beforebegin', btn);
      block.setAttribute('data-dao-insert-decorated', '');
    }
  }

  // Builds the insert-button DOM (lucide pencil-line icon, i18n'd
  // tooltip + aria-label). Click handler captures the owning code-block
  // by closure so each button knows which snippet to insert.
  private buildCodeInsertButton_(block: Element): HTMLButtonElement {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'dao-code-insert-btn';
    btn.title = t('chat.code_block.insert_tooltip');
    btn.setAttribute('aria-label', t('chat.code_block.insert_tooltip'));
    btn.innerHTML =
        '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor"' +
        ' stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' +
        ' aria-hidden="true">' +
        '<path d="M13 21h8"></path>' +
        '<path d="m15 5 4 4"></path>' +
        '<path d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0' +
        ' 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0' +
        ' 0 .83-.497z"></path>' +
        '</svg>';
    btn.addEventListener(
        'click', () => void this.insertCodeBlock_(btn, block));
    return btn;
  }

  // Handles a click on a code-block insert button. Reads the rendered
  // code via textContent (vendor stores the raw source on the <code-block>
  // .code property in base64-encoded form when fed from markdown-block,
  // so we read the post-highlight DOM text instead — always the decoded
  // source). Round-trips through executeScript; flashes a transient
  // label on success/failure using the same visual treatment the
  // copy/share buttons use.
  private async insertCodeBlock_(
      btn: HTMLButtonElement, block: Element): Promise<void> {
    const codeEl = block.querySelector('code');
    const text = codeEl?.textContent ?? '';
    if (!text) {
      this.flashButtonLabel_(btn, t('chat.code_block.empty'), false);
      return;
    }
    const ok = await insertTextIntoFocusedInput(text);
    if (ok) {
      this.flashButtonLabel_(btn, t('chat.code_block.inserted'), true);
    } else {
      this.flashButtonLabel_(btn, t('chat.code_block.no_input'), false);
    }
  }

  private flashButtonLabel_(
      btn: HTMLButtonElement, label: string, ok: boolean,
      durationMs = 2000): void {
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

  private async copyAssistantTextById_(
      assistantId: string, btn?: HTMLButtonElement): Promise<void> {
    await this.copyAssistantPair_(
        this.findPromptForAssistantId_(assistantId), btn);
  }

  private async copyAssistantPair_(
      pair: DaoAssistantPair|null, btn?: HTMLButtonElement): Promise<void> {
    if (!pair || !pair.answer) {
      if (btn) {
        this.flashButtonLabel_(btn, t('chat.message_actions.empty'), false);
      }
      return;
    }
    try {
      // Render the assistant's Markdown answer to HTML so rich-text
      // targets (Notion, Word, Slack, mail composers) preserve headings,
      // lists, code, and emphasis. text/plain still carries the raw
      // Markdown for terminals and code editors. We use the same `marked`
      // singleton mini-lit's <markdown-block> renders with so the
      // clipboard payload visually matches the chat bubble.
      const html = renderAssistantMarkdown(pair.answer);
      const ClipboardItemCtor =
          // eslint-disable-next-line @typescript-eslint/no-explicit-any
          (window as any).ClipboardItem as (new(items: Record<string, Blob>) =>
                                                ClipboardItem) |
          undefined;
      if (ClipboardItemCtor && navigator.clipboard?.write) {
        const item = new ClipboardItemCtor({
          'text/html': new Blob([html], {type: 'text/html'}),
          'text/plain': new Blob([pair.answer], {type: 'text/plain'}),
        });
        await navigator.clipboard.write([item]);
      } else {
        await navigator.clipboard.writeText(pair.answer);
      }
      if (btn) {
        this.flashButtonLabel_(btn, t('chat.message_actions.copied'), true);
      }
    } catch (e) {
      console.warn('[dao] copy text failed', e);
      try {
        await navigator.clipboard.writeText(pair.answer);
        if (btn) {
          this.flashButtonLabel_(btn, t('chat.message_actions.copied'), true);
        }
      } catch (e2) {
        console.warn('[dao] copy text fallback failed', e2);
        if (btn) {
          this.flashButtonLabel_(
              btn, t('chat.message_actions.failed'), false);
        }
      }
    }
  }

  private async shareAssistantAsImageById_(
      assistantId: string, btn?: HTMLButtonElement): Promise<void> {
    await this.shareAssistantPairAsImage_(
        this.findPromptForAssistantId_(assistantId), btn);
  }

  private async shareAssistantPairAsImage_(
      pair: DaoAssistantPair|null, btn?: HTMLButtonElement): Promise<void> {
    if (!pair || !pair.answer) {
      if (btn) {
        this.flashButtonLabel_(btn, t('chat.message_actions.empty'), false);
      }
      return;
    }
    try {
      const blob = await renderShareImage({
        question: pair.question,
        source: pair.source,
        answer: pair.answer,
      });
      await copyPngBlobToClipboard(blob);
      if (btn) {
        this.flashButtonLabel_(btn, t('chat.message_actions.shared'), true);
      }
    } catch (e) {
      console.warn('[dao] share image failed', e);
      if (btn) {
        this.flashButtonLabel_(btn, t('chat.message_actions.failed'), false);
      }
    }
  }

  private async regenerateAssistantById_(assistantId: string): Promise<void> {
    const assistantIdx = this.findMessageIndexByDaoId_(assistantId);
    const userIdx = this.findUserIndexForAssistantIndex_(assistantIdx);
    await this.retryFromUserIndex_(userIdx);
  }

  private async rewindToAssistantById_(assistantId: string): Promise<void> {
    const agent = this.agent_;
    if (!agent || agent.state.isStreaming || this.isStreaming_) return;

    const assistantIdx = this.findMessageIndexByDaoId_(assistantId);
    const msg = agent.state.messages[assistantIdx] as
        DaoChatMessage | undefined;
    if (assistantIdx < 0 || !this.isAssistantMessage_(msg)) return;
    if (assistantIdx === this.findLatestAssistantIndex_()) return;

    agent.state.messages = agent.state.messages.slice(0, assistantIdx + 1);
    this.editingMessageId_ = '';
    this.editingDraft_ = '';
    this.editingError_ = '';
    this.debugContextMessageId_ = '';
    this.closeUserActionMenu_(false);

    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    iface?.requestUpdate?.();
    this.syncMeta_();
    this.refreshMessageActions_();
    await this.saveCurrentSession_();
    this.focusInput();
  }

  private toggleUserActionMenu_(
      userId: string, anchor?: HTMLElement): void {
    const idx = this.findMessageIndexByDaoId_(userId);
    const msg = this.currentMessages_()[idx];
    if (!this.isUserMessage_(msg)) return;
    if (this.userActionMenuMessageId_ === userId) {
      this.closeUserActionMenu_();
      return;
    }
    this.userActionMenuMessageId_ = userId;
    this.userActionMenuOpenAbove_ = anchor ?
        this.shouldOpenUserActionMenuAbove_(anchor) :
        false;
    this.userActionMenuAlignStart_ = anchor ?
        this.shouldAlignUserActionMenuStart_(anchor) :
        false;
    this.attachUserActionMenuOutsideListener_();
    this.refreshMessageActions_();
  }

  private closeUserActionMenu_(refresh = true): void {
    this.userActionMenuMessageId_ = '';
    this.userActionMenuOpenAbove_ = false;
    this.userActionMenuAlignStart_ = false;
    this.detachUserActionMenuOutsideListener_();
    if (refresh) {
      this.refreshMessageActions_();
    }
  }

  private attachUserActionMenuOutsideListener_(): void {
    if (this.boundOnUserActionMenuOutsidePointerDown_) return;
    this.boundOnUserActionMenuOutsidePointerDown_ =
        (event: Event) => this.onUserActionMenuOutsidePointerDown_(event);
    document.addEventListener(
        'pointerdown',
        this.boundOnUserActionMenuOutsidePointerDown_,
        true);
  }

  private detachUserActionMenuOutsideListener_(): void {
    if (!this.boundOnUserActionMenuOutsidePointerDown_) return;
    document.removeEventListener(
        'pointerdown',
        this.boundOnUserActionMenuOutsidePointerDown_,
        true);
    this.boundOnUserActionMenuOutsidePointerDown_ = null;
  }

  private onUserActionMenuOutsidePointerDown_(event: Event): void {
    if (!this.userActionMenuMessageId_) return;
    const target = event.target;
    if (target instanceof Node && this.panel_) {
      const rows = Array.from(
          this.panel_.querySelectorAll<HTMLElement>('.dao-user-actions'));
      if (rows.some(row => row.contains(target))) {
        return;
      }
    }
    this.closeUserActionMenu_();
  }

  private shouldOpenUserActionMenuAbove_(anchor: HTMLElement): boolean {
    const rect = anchor.getBoundingClientRect();
    if (rect.width <= 0 && rect.height <= 0) return false;

    const viewportHeight =
        window.innerHeight || document.documentElement.clientHeight || 0;
    const edge = 12;
    const estimatedHeight = (this.debugMode_ ? 2 : 1) * 32 + 10;
    const spaceBelow = viewportHeight - rect.bottom - edge;
    const spaceAbove = rect.top - edge;
    return spaceBelow < estimatedHeight && spaceAbove > spaceBelow;
  }

  private shouldAlignUserActionMenuStart_(anchor: HTMLElement): boolean {
    const rect = anchor.getBoundingClientRect();
    if (rect.width <= 0 && rect.height <= 0) return false;

    const edge = 12;
    const estimatedWidth = this.debugMode_ ? 188 : 132;
    return rect.left < estimatedWidth + edge;
  }

  private beginEditUserMessage_(userId: string): void {
    const idx = this.findMessageIndexByDaoId_(userId);
    const msg = this.currentMessages_()[idx];
    if (!this.isUserMessage_(msg)) return;
    this.editingMessageId_ = userId;
    this.editingDraft_ = this.extractVisibleText_(msg);
    this.editingError_ = '';
    this.closeUserActionMenu_(false);
    this.debugContextMessageId_ = '';
    this.refreshMessageActions_();
  }

  private cancelEditUserMessage_(): void {
    this.editingMessageId_ = '';
    this.editingDraft_ = '';
    this.editingError_ = '';
    this.closeUserActionMenu_(false);
    this.refreshMessageActions_();
  }

  private onEditDraftInput_(e: Event): void {
    const target = e.target as HTMLInputElement | HTMLTextAreaElement | null;
    this.editingDraft_ = target?.value ?? '';
    if (this.editingDraft_.trim()) {
      this.editingError_ = '';
    }
  }

  private onEditDraftKeydown_(e: KeyboardEvent): void {
    if (e.key === 'Escape') {
      e.preventDefault();
      this.cancelEditUserMessage_();
      return;
    }
    if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      if (this.editingMessageId_) {
        void this.applyUserMessageEdit_(
            this.editingMessageId_, this.editingDraft_);
      }
    }
  }

  private async applyUserMessageEdit_(
      userId: string, nextText: string): Promise<void> {
    const agent = this.agent_;
    if (!agent) return;
    const trimmed = nextText.trim();
    if (!trimmed) {
      this.editingError_ = t('chat.message_actions.empty_edit');
      this.refreshMessageActions_();
      return;
    }
    let userIdx = this.findMessageIndexByDaoId_(userId);
    let msg = agent.state.messages[userIdx] as DaoChatMessage | undefined;
    if (userIdx < 0 || !this.isUserMessage_(msg)) return;

    if (agent.state.isStreaming || this.isStreaming_) {
      try {
        agent.abort();
      } catch (_) {
        // Keep applying the explicit edit even if the in-flight run is
        // already unwinding and abort reports an error.
      }
      try {
        await agent.waitForIdle();
      } catch (_) {
        // The run may already be idle or tearing down; re-check messages below.
      }
      agent.state.isStreaming = false;
      this.isStreaming_ = false;
    }

    const messages = agent.state.messages;
    userIdx = this.findMessageIndexByDaoId_(userId);
    msg = messages[userIdx] as DaoChatMessage | undefined;
    if (userIdx < 0 || !this.isUserMessage_(msg)) return;

    const now = new Date().toISOString();
    const daoMeta = {
      ...(msg.dao ?? {}),
    } as DaoMessageMetadata & {editHistory?: unknown};
    delete daoMeta.editHistory;
    const editedMessage: DaoChatMessage = {
      ...msg,
      content: trimmed,
      dao: {
        ...daoMeta,
        id: userId,
        editedAt: now,
      },
    };

    const nextMessages = messages.slice(0, userIdx + 1);
    nextMessages[userIdx] = editedMessage;
    agent.state.messages = nextMessages;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    iface?.requestUpdate?.();
    this.syncMeta_();
    this.cancelEditUserMessage_();
    await this.saveCurrentSession_();
    try {
      await agent.continue();
    } catch (e) {
      console.warn('[dao] edit retry failed', e);
      this.scheduleSaveSession_();
    }
  }

  private async retryFromUserIndex_(userIdx: number): Promise<void> {
    const agent = this.agent_;
    if (!agent || agent.state.isStreaming) return;
    const messages = agent.state.messages;
    if (userIdx < 0 || userIdx >= messages.length ||
        !this.isUserMessage_(messages[userIdx])) {
      return;
    }
    // Keep the user message, drop all assistant / toolResult messages
    // that came after it. Replace the array (not mutate in place) so
    // pi-web-ui's reference-equality change detector picks it up.
    agent.state.messages = messages.slice(0, userIdx + 1);
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    iface?.requestUpdate?.();
    this.syncMeta_();
    try {
      await agent.continue();
    } catch (e) {
      console.warn('[dao] retry failed', e);
      this.scheduleSaveSession_();
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
        // Non-capturable page → no focused-input gate either.
        this.panel_?.classList.remove('dao-has-focused-input');
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
      // Selection chip + focused-input gate: one unified probe returns
      // both the active tab's text selection and whether its active
      // element is a writable text input. Halves the executeScript count
      // versus two separate probes.
      const probe = await fetchPageProbeState();
      const sel = probe.selection;
      const currentSel = this.pendingSelection_;
      if (!sel) {
        if (currentSel !== null) this.pendingSelection_ = null;
      } else if (!currentSel || currentSel.text !== sel.text ||
                 currentSel.url !== sel.url) {
        this.pendingSelection_ = sel;
      }
      // Code-block insert button visibility. Class lives on the chat
      // panel root because it is the closest ancestor of every
      // <code-block> rendered in the message list; scoping the CSS rule
      // there keeps the blast radius minimal.
      this.panel_?.classList.toggle(
          'dao-has-focused-input', probe.hasFocusedInput);
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

  private onElementContextDismiss_(contextId: string) {
    if (!contextId) return;
    removeReusableElementContext(contextId);
    this.pendingElementContexts_ = getReusableElementContexts();
  }

  private showToast_(text: string) {
    this.dispatchEvent(new CustomEvent('show-toast', {
      bubbles: true,
      composed: true,
      detail: {text},
    }));
  }

  private async onElementPickClick_() {
    if (this.elementPicking_) {
      await cancelElementPicker();
      this.elementPicking_ = false;
      this.showToast_(t('chat.attach.element.pick_cancelled'));
      return;
    }

    this.elementPicking_ = true;
    try {
      const capture = await startElementPicker();
      if (capture) {
        const context = addReusableElementContext(capture);
        this.pendingElementContexts_ = getReusableElementContexts();
        this.showToast_(t('chat.attach.element.pick_selected', {
          label: context.label,
        }));
        this.focusInput();
      } else {
        this.showToast_(t('chat.attach.element.pick_cancelled'));
      }
    } catch (e) {
      console.warn('[dao] element picker failed', e);
      this.showToast_(t('chat.attach.element.pick_failed'));
    } finally {
      this.elementPicking_ = false;
    }
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

  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  private async maybeAttachElementContext_(attachments: any[]): Promise<any[]> {
    if (this.pendingElementContexts_.length === 0) return attachments;
    const contexts = consumeReusableElementContexts();
    this.pendingElementContexts_ = [];
    if (contexts.length === 0) return attachments;
    return [
      ...attachments,
      ...contexts.map(buildElementContextAttachment),
    ];
  }

  private async maybeBuildMemoryContextText_(): Promise<string|null> {
    try {
      const info = await fetchCurrentPageInfo();
      if (!info?.url || !isCapturablePageUrl(info.url)) return null;

      const domain = new URL(info.url).hostname;
      if (!domain) return null;

      const payload = await callNativeArgs(
          'getMemoryContext', info.url, domain, this.currentSessionId_ || '') as
          NativeMemoryContext;
      if (!hasMemoryContextPayload(payload || {})) return null;
      const text = buildMemoryContextText({
        url: info.url,
        domain,
        payload: payload || {},
      });
      return text || null;
    } catch (e) {
      console.warn('[dao-agent] memory context unavailable', e);
      return null;
    }
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
    const fallbackSvg = html`<svg viewBox="0 0 24 24" fill="none"
        stroke="currentColor" stroke-width="2" stroke-linecap="round"
        stroke-linejoin="round" aria-hidden="true">
      <circle cx="12" cy="12" r="10"></circle>
      <path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"></path>
      <path d="M2 12h20"></path>
    </svg>`;
    if (!url) return fallbackSvg;
    // Use Chromium's favicon service instead of loading directly from the
    // site origin. Direct cross-origin `https://site/favicon.ico` loads are
    // blocked by the WebUI's img-src CSP, so we'd only ever see the fallback
    // globe. chrome://favicon2 is allowlisted in webui_util.cc, cached by
    // the browser, and handles redirects and size variants for us.
    const faviconUrl =
        `chrome://favicon2/?size=16&scaleFactor=2x&pageUrl=${
            encodeURIComponent(url)}`;
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
    return html`${fallbackSvg}<img src=${faviconUrl} alt=""
        @error=${onErr} @load=${onLoad}>`;
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
      console.warn('[dao] page chip dropped: capture failed for', chipUrl);
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

  // BASE_SYSTEM_PROMPT + current skill catalog + current SOUL.md, with the
  // soul wrapped in <soul> tags so the LLM can clearly delineate personality
  // from base instructions (and so downstream tooling can find/replace the
  // soul block). Rebuilt fresh on each call so callers get the latest saved
  // soul.
  private buildSystemPrompt_(): string {
    const skills = this.skillCatalogPrompt_.trim();
    return BASE_SYSTEM_PROMPT + (skills ? '\n\n' + skills : '') +
        '\n\n<soul>\n' + currentSoulContent + '\n</soul>';
  }

  private async refreshSkillCatalogPrompt_(): Promise<void> {
    await refreshSkillRegistry();
    let host = '';
    try {
      const info = await fetchCurrentPageInfo();
      if (info?.url) {
        host = new URL(info.url).hostname;
      }
    } catch (_) {
      host = '';
    }
    this.skillCatalogPrompt_ =
        buildAvailableSkillsPrompt(getAllSkills(), host);
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

  // Rebuild the adapted tool array from the current enable/disable set and
  // push it into the live agent state. The agent consumes tools on each
  // turn so the new list applies to the next user message.
  private refreshTools_() {
    if (!this.agent_) return;
    this.agent_.state.tools = buildAgentTools();
  }

  private syncMeta_() {
    const msgs = this.agent_?.state.messages ?? [];
    this.messageCount_ = msgs.length;
    this.tokenEstimate_ = estimateMessagesTokens(msgs);
  }

  private currentMessages_(): DaoChatMessage[] {
    const msgs = this.agent_?.state.messages;
    return Array.isArray(msgs) ? msgs as DaoChatMessage[] : [];
  }

  private newDaoMessageId_(): string {
    const uuid = globalThis.crypto?.randomUUID?.();
    if (typeof uuid === 'string' && uuid.length > 0) {
      return `dao-msg-${uuid}`;
    }
    return `dao-msg-${Date.now()}-${Math.random().toString(36).slice(2, 12)}`;
  }

  private ensureMessageIds_(): boolean {
    if (!this.agent_) return false;
    const msgs = this.currentMessages_();
    let changed = false;
    for (const msg of msgs) {
      if (!isRecord(msg)) continue;
      if (!isRecord(msg.dao)) {
        msg.dao = {id: this.newDaoMessageId_()};
        changed = true;
        continue;
      }
      if (typeof msg.dao.id !== 'string' || !msg.dao.id.length) {
        msg.dao.id = this.newDaoMessageId_();
        changed = true;
      }
    }
    if (changed) {
      this.agent_.state.messages = msgs.slice();
    }
    return changed;
  }

  private findMessageIndexByDaoId_(id: string): number {
    const msgs = this.currentMessages_();
    for (let i = 0; i < msgs.length; i++) {
      if (msgs[i]?.dao?.id === id) return i;
    }
    return -1;
  }

  private findLatestAssistantIndex_(): number {
    const msgs = this.currentMessages_();
    for (let i = msgs.length - 1; i >= 0; i--) {
      if (this.isAssistantMessage_(msgs[i])) return i;
    }
    return -1;
  }

  private isUserMessage_(msg: unknown): msg is DaoChatMessage {
    if (!isRecord(msg)) return false;
    const role = msg['role'];
    return role === 'user' || role === 'user-with-attachments';
  }

  private isAssistantMessage_(msg: unknown): msg is DaoChatMessage {
    if (!isRecord(msg)) return false;
    return msg['role'] === 'assistant' &&
        !!this.extractVisibleText_(msg as DaoChatMessage);
  }

  private extractVisibleText_(msg: DaoChatMessage): string {
    return this.extractAssistantText_(msg);
  }

  private pageSourceForUserMessage_(msg: DaoChatMessage):
      {title: string; domain: string} | undefined {
    if (msg.role !== 'user-with-attachments') return;
    const attachments = Array.isArray(msg.attachments) ? msg.attachments : [];
    for (const raw of attachments) {
      if (!isRecord(raw)) continue;
      const pageUrl = raw['daoPageUrl'];
      if (typeof pageUrl !== 'string' || !pageUrl.length) continue;
      const rawTitle = raw['daoPageTitle'];
      const rawFileName = raw['fileName'];
      const pageTitle =
          typeof rawTitle === 'string' && rawTitle.length > 0 ?
          rawTitle :
          typeof rawFileName === 'string' && rawFileName.length > 0 ?
          rawFileName :
          '';
      let host = '';
      try {
        host = new URL(pageUrl).hostname.replace(/^www\./, '');
      } catch (_) {
        continue;
      }
      return {
        title: pageTitle || host,
        domain: host,
      };
    }
    return;
  }

  private findUserIndexForAssistantIndex_(assistantIdx: number): number {
    const msgs = this.currentMessages_();
    const assistant = msgs[assistantIdx];
    if (!isRecord(assistant) || assistant['role'] !== 'assistant') return -1;
    for (let i = assistantIdx - 1; i >= 0; i--) {
      if (this.isUserMessage_(msgs[i])) return i;
    }
    return -1;
  }

  private findPromptForAssistantIndex_(assistantIdx: number):
      DaoAssistantPair|null {
    const msgs = this.currentMessages_();
    const assistant = msgs[assistantIdx];
    if (!this.isAssistantMessage_(assistant)) return null;
    const answer = this.extractVisibleText_(assistant);
    const userIdx = this.findUserIndexForAssistantIndex_(assistantIdx);
    if (userIdx >= 0) {
      const user = msgs[userIdx];
      if (!this.isUserMessage_(user)) return {question: '', answer};
      return {
        question: this.extractVisibleText_(user),
        source: this.pageSourceForUserMessage_(user),
        answer,
      };
    }
    return {question: '', answer};
  }

  private findPromptForAssistantId_(assistantId: string): DaoAssistantPair|null {
    return this.findPromptForAssistantIndex_(
        this.findMessageIndexByDaoId_(assistantId));
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
        detail: {text: t('chat.toast.wait_for_turn')},
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
      this.ensureMessageIds_();
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
          text: t('chat.compact.success', {count: result.collapsedCount}),
        },
      }));
      this.scheduleSaveSession_();
    } catch (e) {
      const err = e as Error;
      const text = err.name === 'AbortError'
          ? t('chat.compact.cancelled')
          : t('chat.compact.failed', {error: err.message ?? String(err)});
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
    // Cross-WebUI sync: skills created/deleted in the dao://skills tab
    // don't update this page's module-local registry cache. Refresh on
    // demand (throttled to avoid spamming on every keystroke) and re-run
    // the picker once new data lands so newly-added skills show up
    // without a sidebar reload.
    void refreshSkillRegistryIfStale().then((refreshed) => {
      if (!refreshed) return;
      const ta2 = this.composerTextarea_;
      if (!ta2) return;
      if (/^\/([A-Za-z0-9_-]*)$/.test(ta2.value)) {
        this.updateSkillPicker_();
      }
    });
    this.skillPickerQuery_ = (m[1] || '').toLowerCase();
    const all = getAllSkills().filter((s) => !s.disabled);
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
  // not start with a known skill marker. Disabled skills are treated as
  // unknown so `/disabled-id` falls through to a plain message instead of
  // expanding into the LLM payload.
  private parseSkillPrefix_(text: string):
      {skillId: string, skill: SkillRegistryEntry, rest: string} | null {
    const m = /^\/([A-Za-z0-9_-]+)(?:\s+([\s\S]*))?$/.exec(text.trim());
    if (!m || !m[1]) return null;
    const skillId = m[1];
    const skill = getAllSkills().find((s) => s.id === skillId);
    if (!skill || skill.disabled) return null;
    return {skillId, skill, rest: (m[2] || '').trim()};
  }

  // Warm the instructions cache before the message hits state.messages so
  // convertToLlm can splice the body in synchronously on the next turn.
  private async ensureSkillLoadedFromText_(text: string): Promise<void> {
    let parsed = this.parseSkillPrefix_(text);
    if (!parsed) {
      // The text looks like `/<id> ...` but the id wasn't in our cache.
      // This is the "user created a skill in dao://skills and typed
      // /id fast enough to skip the picker" path. Bypass the staleness
      // throttle (a recent refresh from picker-open could still be from
      // before the skill was created in the other tab) and force one
      // fresh fetch before declaring the prefix unknown.
      if (/^\/([A-Za-z0-9_-]+)(?:\s|$)/.test(text.trim())) {
        await refreshSkillRegistry();
        parsed = this.parseSkillPrefix_(text);
      }
      if (!parsed) return;
    }
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

  // ---- Dream Analysis morning-report card ----

  private async loadDreamReport_() {
    try {
      const raw = await callNative('getUnviewedDreamReport') as {
        id?: number; dreamDate?: string; reportMarkdown?: string;
        habitCandidates?: string; debugMaterialJson?: string;
      } | null;
      if (!raw || typeof raw.id !== 'number') {
        this.dreamReport_ = null;
        return;
      }
      if (raw.dreamDate !== yesterdayLocalYmd() ||
          readDismissedDreamReportIds().has(raw.id)) {
        this.dreamReport_ = null;
        return;
      }
      let habits: DreamReportData['habits'] = [];
      try {
        const parsed = JSON.parse(raw.habitCandidates || '[]');
        if (Array.isArray(parsed)) habits = parsed;
      } catch {}
      this.dreamReport_ = {
        id: raw.id,
        dreamDate: raw.dreamDate || '',
        reportMarkdown: raw.reportMarkdown || '',
        habits,
        debugMaterialJson: raw.debugMaterialJson || '',
      };
      this.dreamExpanded_ = false;
    } catch {
      this.dreamReport_ = null;
    }
  }

  private toggleDreamExpanded_() {
    if (!this.dreamReport_) {
      return;
    }
    callNative('openTab', {url: 'dao://dream/'}).catch(() => {});
  }

  private dismissDreamReport_() {
    if (!this.dreamReport_) {
      return;
    }
    const dismissed = readDismissedDreamReportIds();
    dismissed.add(this.dreamReport_.id);
    writeDismissedDreamReportIds(dismissed);
    this.dreamReport_ = null;
  }

  private renderDreamCard_() {
    const r = this.dreamReport_;
    if (!r) return nothing;
    return html`
      <style>
        .dao-dream-card {
          border: 1px solid rgba(127,127,127,0.18);
          border-radius: 12px;
          padding: 12px 14px;
          margin: 8px 12px 0;
          flex-shrink: 0;
          font-size: 13px;
          color: var(--text, inherit);
        }
        .dao-dream-btn {
          font: inherit;
          font-size: 11px;
          color: var(--text-secondary, inherit);
          background: rgba(127,127,127,0.10);
          border: 1px solid rgba(127,127,127,0.20);
          border-radius: 8px;
          padding: 2px 10px;
          cursor: pointer;
          flex-shrink: 0;
        }
        .dao-dream-btn:hover { background: rgba(127,127,127,0.18); }
        .dao-dream-close {
          width: 24px;
          height: 24px;
          padding: 0;
          display: inline-flex;
          align-items: center;
          justify-content: center;
          border-radius: 8px;
        }
        .dao-dream-close svg {
          width: 14px;
          height: 14px;
        }
        .dao-dream-md { margin-top: 10px; font-size: 13px; }
        .dao-dream-debug-pre {
          font-size: 11px;
          max-height: 300px;
          overflow: auto;
          background: rgba(127,127,127,0.08);
          border-radius: 8px;
          padding: 8px;
          white-space: pre-wrap;
          word-break: break-all;
        }
      </style>
      <div class="dao-dream-card">
        <div style="display:flex;align-items:center;gap:8px;">
          <svg viewBox="0 0 24 24" width="16" height="16" fill="none"
              stroke="currentColor" stroke-width="2" stroke-linecap="round"
              stroke-linejoin="round" aria-hidden="true"
              style="flex-shrink:0;">
            <path d="M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401" />
          </svg>
          <span style="font-weight:600;">${t('chat.dream.card_title')}</span>
          <span style="font-size:11px;opacity:.6;white-space:nowrap;
              overflow:hidden;text-overflow:ellipsis;">
            ${t('chat.dream.card_date', {date: r.dreamDate})}</span>
          <span style="flex:1"></span>
          <button class="dao-dream-btn"
              @click=${this.toggleDreamExpanded_}>
            ${t('chat.dream.expand')}
          </button>
          <button class="dao-dream-btn dao-dream-close"
              title=${t('chat.dream.dismiss')}
              aria-label=${t('chat.dream.dismiss')}
              @click=${this.dismissDreamReport_}>
            <svg viewBox="0 0 24 24" fill="none"
                stroke="currentColor" stroke-width="2"
                stroke-linecap="round" stroke-linejoin="round"
                aria-hidden="true">
              <path d="M18 6 6 18"></path>
              <path d="m6 6 12 12"></path>
            </svg>
          </button>
        </div>
      </div>`;
  }

  private normalizeProactiveSuggestion_(
      raw: unknown): ProactiveSuggestionData|null {
    if (!isRecord(raw)) return null;
    const text = typeof raw['text'] === 'string' ? raw['text'] : '';
    const scenarioName =
        typeof raw['scenarioName'] === 'string' ? raw['scenarioName'] : '';
    const type = typeof raw['type'] === 'string' ? raw['type'] : '';
    const label = (scenarioName || text).trim();
    if (!label && type !== 'repeat_action') return null;
    return {
      episodeId: typeof raw['episodeId'] === 'number' ? raw['episodeId'] : 0,
      text: text || label,
      confidence:
          typeof raw['confidence'] === 'number' ? raw['confidence'] : 0,
      type,
      actionType:
          typeof raw['actionType'] === 'number' ? raw['actionType'] : 0,
      scenarioId:
          typeof raw['scenarioId'] === 'string' ? raw['scenarioId'] : '',
      scenarioName,
      actionLabel:
          typeof raw['actionLabel'] === 'string' ? raw['actionLabel'] : '',
      actionPrompt:
          typeof raw['actionPrompt'] === 'string' ? raw['actionPrompt'] : '',
      requiresPageContent: raw['requiresPageContent'] === true,
      tabId: typeof raw['tabId'] === 'number' ? raw['tabId'] : -1,
    };
  }

  private proactiveSuggestionTitle_(suggestion: ProactiveSuggestionData):
      string {
    if (suggestion.scenarioName) {
      return suggestion.scenarioName;
    }
    if (suggestion.type === 'repeat_action') {
      return t('chat.proactive.repeat_action_title');
    }
    if (suggestion.type === 'continue_conversation') {
      const intent = suggestion.text.trim();
      return intent ?
          t('chat.proactive.continue_conversation_title', {intent}) :
          t('chat.proactive.continue_conversation_title_fallback');
    }
    return suggestion.text;
  }

  private proactiveSuggestionPrompt_(suggestion: ProactiveSuggestionData):
      string {
    if (suggestion.type === 'repeat_action') {
      return t('chat.proactive.default_user_prompt');
    }
    if (suggestion.type === 'continue_conversation') {
      const intent = suggestion.text.trim();
      return intent ?
          t('chat.proactive.continue_conversation_prompt', {intent}) :
          t('chat.proactive.default_user_prompt');
    }
    return suggestion.text || t('chat.proactive.default_user_prompt');
  }

  private onProactiveSuggestion_(raw: unknown) {
    if (localStorage.getItem('dao_proactive_enabled') === 'false') {
      return;
    }
    const suggestion = this.normalizeProactiveSuggestion_(raw);
    if (!suggestion) return;
    this.proactiveSuggestion_ = suggestion;
    this.proactiveRunning_ = false;
  }

  private renderProactiveCard_() {
    const s = this.proactiveSuggestion_;
    if (!s) return nothing;
    const title = this.proactiveSuggestionTitle_(s);
    const running = this.proactiveRunning_ || this.isStreaming_;
    return html`
      <div class="dao-proactive-card">
        <div class="dao-proactive-head">
          <svg class="dao-proactive-icon" viewBox="0 0 24 24" fill="none"
              stroke="currentColor" stroke-width="2"
              stroke-linecap="round" stroke-linejoin="round"
              aria-hidden="true">
            <path d="M15 14c.2-1 .7-1.7 1.5-2.5A4.8 4.8 0 0 0 18 8a6 6 0 0 0-12 0c0 1.3.5 2.5 1.5 3.5.8.8 1.3 1.5 1.5 2.5"></path>
            <path d="M9 18h6"></path>
            <path d="M10 22h4"></path>
          </svg>
          <div class="dao-proactive-copy">
            <div class="dao-proactive-title">${title}</div>
            <div class="dao-proactive-hint">
              ${t('chat.proactive.cost_hint')}
            </div>
          </div>
        </div>
        <div class="dao-proactive-actions">
          <button class="dao-proactive-btn primary"
              ?disabled=${running}
              @click=${this.runProactiveSuggestion_}
              aria-label=${t('chat.proactive.run_aria', {title})}>
            ${this.proactiveRunning_
                ? t('chat.proactive.running')
                : t('chat.proactive.run')}
          </button>
          <button class="dao-proactive-btn"
              ?disabled=${this.proactiveRunning_}
              @click=${this.dismissProactiveSuggestion_}
              aria-label=${t('chat.proactive.dismiss_aria', {title})}>
            ${t('chat.proactive.dismiss')}
          </button>
        </div>
      </div>`;
  }

  private async buildProactiveFeedback_(
      suggestion: ProactiveSuggestionData): Promise<Record<string, unknown>> {
    let url = '';
    let domain = '';
    try {
      const info = await fetchCurrentPageInfo();
      url = info?.url || '';
      domain = url ? new URL(url).hostname : '';
    } catch (_) { /* best effort */ }
    return {
      scenarioId: suggestion.scenarioId,
      actionLabel: suggestion.actionLabel || suggestion.text,
      domain,
      url,
      confidence: suggestion.confidence,
    };
  }

  private truncateProactivePageContent_(text: string):
      {text: string, truncated: boolean} {
    const cleaned = text.replace(/\u0000/g, '').trim();
    if (cleaned.length <= MAX_PROACTIVE_PAGE_CONTENT_CHARS) {
      return {text: cleaned, truncated: false};
    }
    return {
      text: cleaned.slice(0, MAX_PROACTIVE_PAGE_CONTENT_CHARS) +
          `\n\n[Page content truncated to ${
              MAX_PROACTIVE_PAGE_CONTENT_CHARS} characters before sending.]`,
      truncated: true,
    };
  }

  private buildProactivePromptAttachment_(
      suggestion: ProactiveSuggestionData,
      prompt: string): PiAttachment {
    const title = this.proactiveSuggestionTitle_(suggestion) ||
        t('chat.proactive.attachment_title');
    const extractedText =
        `<proactive-suggestion name="${escapeAttr(title)}"` +
        ` scenario_id="${escapeAttr(suggestion.scenarioId)}">` +
        `\n${prompt}\n</proactive-suggestion>`;
    const safeName =
        title.replace(/[\\/\n\r\t\x00-\x1f]+/g, ' ')
            .replace(/\s+/g, ' ')
            .slice(0, 70) || t('chat.proactive.attachment_title');
    return {
      id: `dao-proactive-${Date.now()}-${
          Math.random().toString(36).slice(2)}`,
      type: 'document',
      fileName: `${safeName}.md`,
      mimeType: 'text/markdown',
      size: new TextEncoder().encode(prompt).length,
      content: utf8ToBase64(prompt),
      extractedText,
    };
  }

  private async buildProactivePayload_(
      suggestion: ProactiveSuggestionData):
      Promise<{text: string, attachments: PiAttachment[]}> {
    const title = this.proactiveSuggestionTitle_(suggestion);
    if (!suggestion.actionPrompt) {
      return {
        text: this.proactiveSuggestionPrompt_(suggestion),
        attachments: [],
      };
    }

    let prompt = suggestion.actionPrompt;
    if (suggestion.requiresPageContent) {
      const raw = await callNativeArgs(
          'getPageContentForScenario', suggestion.tabId) as
          {text?: string, error?: string};
      if (!raw || raw.error || !raw.text) {
        throw new Error(raw?.error || 'page content unavailable');
      }
      const page = this.truncateProactivePageContent_(raw.text);
      prompt = prompt.replaceAll('{page_content}', page.text);
    } else {
      prompt = prompt.replaceAll('{page_content}', '');
    }

    return {
      text: t('chat.proactive.user_prompt', {title}),
      attachments: [this.buildProactivePromptAttachment_(suggestion, prompt)],
    };
  }

  private async dismissProactiveSuggestion_() {
    const suggestion = this.proactiveSuggestion_;
    if (!suggestion) return;
    this.proactiveSuggestion_ = null;
    try {
      if (suggestion.scenarioId) {
        await callNativeArgs(
            'dismissSuggestion',
            await this.buildProactiveFeedback_(suggestion));
      } else if (suggestion.episodeId) {
        await callNativeArgs('dismissSuggestion', suggestion.episodeId);
      }
    } catch (e) {
      console.warn('[dao-agent] dismiss proactive suggestion failed', e);
    }
  }

  private async runProactiveSuggestion_() {
    const suggestion = this.proactiveSuggestion_;
    if (!suggestion || this.proactiveRunning_) return;
    if (this.isStreaming_) {
      this.showToast_(t('chat.toast.wait_for_turn'));
      return;
    }
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    if (!iface || typeof iface.sendMessage !== 'function') {
      return;
    }

    this.proactiveRunning_ = true;
    try {
      const payload = await this.buildProactivePayload_(suggestion);
      if (suggestion.scenarioId) {
        await callNativeArgs(
            'acceptSuggestion',
            await this.buildProactiveFeedback_(suggestion));
      } else if (suggestion.episodeId) {
        await callNativeArgs('acceptSuggestion', suggestion.episodeId);
      }
      this.proactiveSuggestion_ = null;
      // The proactive payload already carries its deliberate context. Skip
      // automatic page / selection / memory attachment for this turn so a
      // click never sends duplicate page text.
      this.suppressChipAttachOnce_ = true;
      await iface.sendMessage(payload.text, payload.attachments);
    } catch (e) {
      console.warn('[dao-agent] proactive suggestion failed', e);
      this.showToast_(t('chat.proactive.failed'));
    } finally {
      this.proactiveRunning_ = false;
    }
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
          aria-label=${t('chat.skill_picker.aria_label')}
          style=${pickerStyle}>
        <div class="dao-skill-picker-head">
          ${t('chat.skill_picker.title')}<span class="dao-skill-picker-hint">
            ${t('chat.skill_picker.hint')}
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

  // ---- History persistence ----

  // Coalesce multiple `message_end` events within the same turn into a
  // single IndexedDB write (a tool-heavy assistant turn can fire several).
  // The microtask flush preserves "save after current event loop" ordering
  // so agent.state.messages has already been slice-copied.
  private scheduleSaveSession_() {
    if (this.saveSessionScheduled_) return;
    this.saveSessionScheduled_ = true;
    queueMicrotask(() => {
      this.saveSessionScheduled_ = false;
      void this.saveCurrentSession_();
    });
  }

  private deriveTitleFromMessages_(): string {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const msgs = (this.agent_?.state.messages ?? []) as any[];
    for (const m of msgs) {
      if (m?.role !== 'user' && m?.role !== 'user-with-attachments') continue;
      const raw = typeof m.content === 'string' ?
          m.content :
          Array.isArray(m.content) ?
              m.content.filter((p: {type: string}) => p?.type === 'text')
                  .map((p: {text?: string}) => p.text || '')
                  .join(' ') :
              '';
      const cleaned = String(raw).replace(/\s+/g, ' ').trim();
      if (cleaned) return cleaned.length > 40 ? cleaned.slice(0, 40) : cleaned;
    }
    return '';
  }

  private async saveCurrentSession_(): Promise<void> {
    const agent = this.agent_;
    if (!agent) return;
    if (!agent.state.messages || agent.state.messages.length === 0) return;
    this.ensureMessageIds_();
    try {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const storage = await ensurePiAppStorage() as any;
      if (!this.currentSessionId_) {
        this.currentSessionId_ =
            (globalThis.crypto?.randomUUID?.() ?? `sess-${Date.now()}`);
      }
      const title = this.deriveTitleFromMessages_();
      // Only pass a non-empty title on the first write; subsequent writes
      // preserve whatever title is already in metadata (including user
      // renames via the history panel).
      const existingMeta =
          await storage.sessions.getMetadata?.(this.currentSessionId_) ?? null;
      await storage.sessions.saveSession(
          this.currentSessionId_,
          agent.state,
          existingMeta || undefined,
          existingMeta ? undefined : title);
    } catch (_) { /* non-fatal — storage may be full / unavailable */ }
  }

  // Public entry used by the app shell's history button.
  openHistory() {
    this.historyOpen_ = true;
  }

  private onHistoryClose_() {
    this.historyOpen_ = false;
  }

  private async onHistorySelect_(e: CustomEvent<{id: string}>) {
    const id = e.detail?.id;
    this.historyOpen_ = false;
    if (!id) return;
    await this.loadSession_(id);
  }

  private onHistoryDeleted_(e: CustomEvent<{id: string}>) {
    // The currently active session was removed — behave like
    // startNewSession so the UI doesn't keep a dangling sessionId that
    // would resurrect on the next save.
    if (e.detail?.id === this.currentSessionId_) {
      this.startNewSession();
    }
  }

  private async loadSession_(id: string) {
    if (!this.agent_) return;
    try {
      this.agent_.abort();
    } catch (_) { /* ignore */ }
    try {
      this.compactAbort_?.abort();
    } catch (_) { /* ignore */ }
    this.compacting_ = false;
    this.compactAbort_ = null;
    try {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const storage = await ensurePiAppStorage() as any;
      const sess = await storage.sessions.loadSession(id);
      if (!sess) return;
      this.currentSessionId_ = id;
      // Reassign through the state setter so pi-agent-core's
      // slice-copy + Lit reference-equality binding fire (same pattern
      // used in startNewSession and the `message_end` handler).
      this.agent_.state.messages = Array.isArray(sess.messages) ?
          sess.messages.slice() :
          [];
      const addedMessageIds = this.ensureMessageIds_();
      this.isStreaming_ = false;
      this.sentPageUrls_.clear();
      this.dismissedUrls_.clear();
      this.pendingPageAttachment_ = null;
      this.pendingSelection_ = null;
      this.pendingElementContexts_ = getReusableElementContexts();
      this.elementPicking_ = false;
      this.syncMeta_();
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const iface = this.panel_?.querySelector('agent-interface') as any;
      iface?.requestUpdate?.();
      if (addedMessageIds) {
        this.scheduleSaveSession_();
      }
      void this.refreshPageChip_();
      void this.refreshSelectionChip_();
      // After Lit re-renders the restored messages, re-inject the
      // message action rows below restored user / assistant bubbles and
      // re-decorate page attachments. Without this the buttons only
      // appear for replies generated in the current session — restored
      // history would have no actions until the user sent another
      // message.
      //
      // The exact tick when <assistant-message> elements appear in the
      // DOM depends on pi-web-ui's render schedule (Lit microtasks +
      // virtualization), so we retry on a few backoff steps until the
      // message hosts exist. 80/200/500ms covers cold starts where
      // IndexedDB hydration competes with the first paint; once rows are
      // injected refreshMessageActions_ is idempotent so
      // any later tick is harmless.
      const tryInject = (delay: number, remaining: number) => {
        setTimeout(() => {
          this.refreshMessageActions_();
          this.decoratePageAttachments_();
          this.decorateCodeBlocks_();
          const injected = !!this.panel_?.querySelector('.dao-message-actions');
          if (!injected && remaining > 0) {
            tryInject(delay * 2, remaining - 1);
          }
        }, delay);
      };
      tryInject(80, 3);
      setTimeout(() => this.focusInput(), 50);
    } catch (_) { /* ignore */ }
  }

  // ---- Public API kept for dao-agent-app compatibility ----

  _daoTestEnsureMessageIds(): void {
    this.ensureMessageIds_();
  }

  _daoTestFindMessageIndexByDaoId(id: string): number {
    return this.findMessageIndexByDaoId_(id);
  }

  _daoTestFindPromptForAssistant(assistantId: string): DaoAssistantPair|null {
    return this.findPromptForAssistantId_(assistantId);
  }

  _daoTestRegenerateAssistantById(assistantId: string): Promise<void> {
    return this.regenerateAssistantById_(assistantId);
  }

  _daoTestRewindAssistantById(assistantId: string): Promise<void> {
    return this.rewindToAssistantById_(assistantId);
  }

  _daoTestApplyUserMessageEdit(id: string, text: string): Promise<void> {
    return this.applyUserMessageEdit_(id, text);
  }

  _daoTestCopyAssistantById(assistantId: string): Promise<void> {
    return this.copyAssistantTextById_(assistantId);
  }

  _daoTestShareAssistantAsImageById(assistantId: string): Promise<void> {
    return this.shareAssistantAsImageById_(assistantId);
  }

  // Testing hook: lets browser_tests drive the action-row injector
  // directly without round-tripping through a real LLM stream. Mirrors
  // the path taken after agent_end / loadSession_. Not a public product
  // API; the underscore-prefixed name keeps it out of accidental use.
  _daoTestRefreshAssistantActions(): void {
    this.refreshAssistantActions_();
  }

  // Testing hook: exposes the same Markdown→HTML renderer that
  // copyAssistantText_ feeds into the text/html clipboard slot. Lets
  // browser_tests assert the output without re-importing the vendor
  // bundle (which would double-register lit-html's TrustedTypePolicy
  // and crash the test page).
  _daoTestRenderMarkdownToHtml(markdown: string): string {
    return renderAssistantMarkdown(markdown);
  }

  // Testing hook: shares the already-loaded dao_share_image module with
  // browser_tests. Importing the module again from EvalJs can re-run the
  // vendored runtime under a second WebUI URL and trip Trusted Types.
  _daoTestRenderShareImage(
      ctx: Parameters<typeof renderShareImage>[0]): Promise<Blob> {
    return renderShareImage(ctx);
  }

  private focusInputDom_() {
    const editor = this.panel_?.querySelector(
        'message-editor textarea, message-editor input') as HTMLElement | null;
    editor?.focus();
  }

  focusInput() {
    this.focusInputDom_();
    void Promise.resolve(callNative('focusAgentSidebar'))
        .catch(() => null)
        .then(() => this.focusInputDom_());
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
    // Fresh conversation — mint a new session id on first save rather than
    // pre-creating an empty row (keeps the history list free of stubs).
    this.currentSessionId_ = '';
    // Fresh conversation — clear the page-attachment bookkeeping so the
    // current active tab gets a new chip offered to the user.
    this.sentPageUrls_.clear();
    this.dismissedUrls_.clear();
    this.pendingPageAttachment_ = null;
    this.pendingSelection_ = null;
    this.pendingElementContexts_ = getReusableElementContexts();
    this.elementPicking_ = false;
    this.syncMeta_();
    void this.refreshPageChip_();
    void this.refreshSelectionChip_();
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const iface = this.panel_?.querySelector('agent-interface') as any;
    iface?.requestUpdate?.();
    setTimeout(() => this.focusInput(), 50);
  }

  // Start a fresh session and immediately submit `text`. Invoked from the
  // C++ command bar via window.__daoExternalSubmit — when the WebUI has not
  // finished mounting yet, the caller retries until `origSendMessage_` is
  // installed, so this path can assume the monkey-patched sendMessage is
  // live by the time it runs.
  async submitExternalPrompt(
      text: string,
      options?: {includePageContext?: boolean}) {
    if (!text) return;
    const includePageContext = options?.includePageContext !== false;
    this.externalSubmitInFlight_ = true;
    try {
      // Wait for mount_'s resume probe to finish so its loadSession_ can't
      // land after our reset and re-hydrate the previous conversation.
      await this.mountReady_;
      this.startNewSession();
      if (!includePageContext) {
        // suppressChipAttachOnce_ gates the monkey-patched sendMessage so a
        // chip-poll tick that fires between startNewSession and send can't
        // smuggle the active page into a Cmd+T standalone question.
        this.suppressChipAttachOnce_ = true;
      }
      // Let the new-session reset (messages=[], sessionId cleared) render
      // before pushing the first message — sendMessage reads post-render
      // state. updateComplete fires on the next Lit render rather than a
      // macrotask boundary, so we don't yield to unrelated event-loop work.
      await this.updateComplete;
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const iface = this.panel_?.querySelector('agent-interface') as any;
      if (iface && typeof iface.sendMessage === 'function') {
        try {
          await iface.sendMessage(text, []);
        } catch (_) { /* surfaced via agent error events */ }
      }
    } finally {
      this.externalSubmitInFlight_ = false;
      // Defensive: even if the send threw before sendMessage consumed it,
      // clear so a normal user-typed turn afterwards doesn't accidentally
      // skip its chip attach.
      this.suppressChipAttachOnce_ = false;
    }
  }
}

customElements.define('dao-chat-view', DaoChatView);
