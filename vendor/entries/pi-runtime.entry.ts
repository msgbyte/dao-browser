// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unified pi-mono runtime bundle. Merges what were previously separate
// `pi-agent` (LLM streaming + Agent loop) and `pi-web-ui` (Lit-based
// ChatPanel / ArtifactsPanel) bundles. Having one entry gives Dao a single
// import surface. pi-web-ui currently trails the core SDK release line and
// therefore keeps a compatible nested pi-ai version until upstream publishes
// a matching Web UI release.
//
// The Dao agent WebUI imports this bundle once; downstream modules see
// all pi-mono APIs through the same JavaScript module instance.
//
// Browser-only. Node-only providers inside pi-ai are guarded at runtime
// by `typeof process !== "undefined"` checks, so we do not need to ship
// node builtins. Heavy optional pi-web-ui preview deps (pdfjs-dist,
// xlsx, docx-preview, jszip, @lmstudio/sdk, ollama) are marked external
// in vendor.config.ts so they are never pulled into the output.

// ---------- pi-ai: models, streaming, provider registry ----------
// @ts-expect-error — resolved from vendor/node_modules at bundle time.
export {
  getModel,
  getModels,
  getProviders,
  stream,
  streamSimple,
  complete,
  completeSimple,
  getEnvApiKey,
} from "@earendil-works/pi-ai/compat";

// Stable root exports that remain outside the temporary compatibility API.
// @ts-expect-error — resolved from vendor/node_modules at bundle time.
export {
  calculateCost,
  modelsAreEqual,
  Type,
} from "@earendil-works/pi-ai";

// @ts-expect-error — resolved from vendor/node_modules at bundle time.
export type {
  Model,
  Api,
  Context,
  Message,
  AssistantMessage,
  AssistantMessageEvent,
  AssistantMessageEventStream,
  ContentBlock,
  ToolCall,
  ToolResult,
  StreamOptions,
  SimpleStreamOptions,
  ProviderStreamOptions,
  KnownProvider,
  Transport,
  Usage,
  ThinkingBudgets,
} from "@earendil-works/pi-ai";

// ---------- pi-agent-core: Agent loop ----------
// pi-agent-core 0.81 requires an explicit streamFn and renamed the mutable
// property from `streamFn` to `streamFunction`. pi-web-ui 0.75 still expects
// the old property, so keep that small compatibility surface here until the
// Web UI package catches up with the core release line.
// @ts-expect-error — resolved from vendor/node_modules at bundle time.
import {
  Agent as CoreAgent,
} from "@earendil-works/pi-agent-core";
// @ts-expect-error — resolved from vendor/node_modules at bundle time.
import type {
  AgentOptions as CoreAgentOptions,
  StreamFn,
} from "@earendil-works/pi-agent-core";
// @ts-expect-error — resolved from vendor/node_modules at bundle time.
import { streamSimple as compatStreamSimple } from "@earendil-works/pi-ai/compat";
// @ts-expect-error — resolved from vendor/node_modules at bundle time.
import {
  applyProxyIfNeeded,
  getAppStorage as getPiAppStorage,
} from "@earendil-works/pi-web-ui";

type CompatibleAgentOptions = Omit<CoreAgentOptions, "streamFn"> & {
  streamFn?: StreamFn;
};

const defaultStreamFn: StreamFn = async (model, context, options) => {
  let effectiveModel = model;
  if (options?.apiKey) {
    let storage: ReturnType<typeof getPiAppStorage> | undefined;
    try {
      storage = getPiAppStorage();
    } catch (error) {
      if (!(error instanceof Error) ||
          error.message !== "AppStorage not initialized. Call setAppStorage() first.") {
        throw error;
      }
    }
    if (storage) {
      const enabled = await storage.settings.get("proxy.enabled");
      const proxyUrl = enabled ? await storage.settings.get("proxy.url") : undefined;
      if (typeof proxyUrl === "string" && proxyUrl.length > 0) {
        effectiveModel = applyProxyIfNeeded(model, options.apiKey, proxyUrl);
      }
    }
  }
  return compatStreamSimple(effectiveModel, context, options);
};

export class Agent extends CoreAgent {
  constructor(options: CompatibleAgentOptions = {}) {
    super({
      ...options,
      streamFn: !options.streamFn || options.streamFn === compatStreamSimple ?
        defaultStreamFn : options.streamFn,
    });
  }

  get streamFn(): StreamFn {
    return this.streamFunction;
  }

  set streamFn(value: StreamFn) {
    this.streamFunction = value;
  }
}

// @ts-expect-error — resolved from vendor/node_modules at bundle time.
export type {
  AgentOptions,
  AgentState,
  AgentEvent,
  AgentMessage,
  AgentTool,
  ToolExecutionMode,
  BeforeToolCallContext,
  BeforeToolCallResult,
  AfterToolCallContext,
  AfterToolCallResult,
  StreamFn,
} from "@earendil-works/pi-agent-core";

// ---------- pi-web-ui: ChatPanel + ArtifactsPanel + dialogs ----------
// Importing these modules triggers the `customElement(...)` side-effect
// registration (e.g. `pi-chat-panel`, `artifacts-panel`). Consumers can
// then render via the tag directly or via the class export.
// @ts-expect-error — resolved from vendor/node_modules at bundle time.
export {
  ChatPanel,
  ArtifactsPanel,
  ApiKeyPromptDialog,
  AppStorage,
  getAppStorage,
  setAppStorage,
  // Storage stores + backend — needed by pi_app_storage.ts to bootstrap
  // the AppStorage singleton before the ChatPanel first renders.
  SettingsStore,
  ProviderKeysStore,
  SessionsStore,
  CustomProvidersStore,
  IndexedDBStorageBackend,
} from "@earendil-works/pi-web-ui";

// @ts-expect-error — resolved from vendor/node_modules at bundle time.
export type {
  Artifact,
  ArtifactsParams,
} from "@earendil-works/pi-web-ui";

// ---------- pi-web-ui: tool renderer registry ----------
// Dao wraps every agent tool with a collapsed-by-default renderer that can
// be toggled via the settings UI. Needs the registry hook + lit's `html`
// (from the same lit instance pi-web-ui renders with) so our custom
// TemplateResult is compatible with pi-web-ui's <message-list>.
// @ts-expect-error — resolved from vendor/node_modules at bundle time.
export { registerToolRenderer } from "@earendil-works/pi-web-ui";

// @ts-expect-error — resolved from the bundled lit (same instance used by
// pi-web-ui's ChatPanel). Consumers outside the bundle MUST import `html`
// from here, not from Chromium's //resources/lit, or their TemplateResult
// type tags will not match.
export { html } from "lit";

// Same singleton instance @mariozechner/mini-lit's <markdown-block> uses
// to render all chat content. Exposing it lets Dao install a preprocess
// hook that fixes CJK + emphasis flanking before marked tokenizes (see
// dao_agent_app.ts), without forking pi-web-ui or mini-lit.
// @ts-expect-error — resolved from vendor/node_modules at bundle time.
export { marked } from "marked";
