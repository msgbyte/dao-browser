// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Bootstraps pi-web-ui's AppStorage (IndexedDB-backed). The ChatPanel's
// AgentInterface reads proxy/provider-key state from this singleton, so it
// must be initialized before the first `<pi-chat-panel>` render.
//
// The Dao agent keeps its own localStorage-backed config in `llm_config.ts`
// as the source of truth for API keys. On init we mirror the active
// provider's key into AppStorage.providerKeys so AgentInterface's
// pre-send "is the key present?" check passes. The Agent itself is still
// wired with `getApiKey` pointing at llm_config, so the actual value used
// for the request comes from Dao's config (not from IndexedDB).

// eslint-disable-next-line @typescript-eslint/no-explicit-any
import * as pi from './vendor/pi_runtime_bundle.js';
import {getActiveLLMConfig, LLM_PROVIDERS} from './llm_config.js';

interface StorageBackend {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  [key: string]: any;
}

interface AppStorageLike {
  providerKeys: {
    get: (provider: string) => Promise<string | null>;
    set: (provider: string, key: string) => Promise<void>;
  };
  settings: {
    get: (key: string) => Promise<unknown>;
    set: (key: string, value: unknown) => Promise<void>;
  };
}

let initPromise: Promise<AppStorageLike> | null = null;

async function initOnce(): Promise<AppStorageLike> {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const mod = pi as any;
  const SettingsStore = mod.SettingsStore;
  const ProviderKeysStore = mod.ProviderKeysStore;
  const SessionsStore = mod.SessionsStore;
  const CustomProvidersStore = mod.CustomProvidersStore;
  const IndexedDBStorageBackend = mod.IndexedDBStorageBackend;
  const AppStorage = mod.AppStorage;
  const setAppStorage = mod.setAppStorage;

  const settings = new SettingsStore();
  const providerKeys = new ProviderKeysStore();
  const sessions = new SessionsStore();
  const customProviders = new CustomProvidersStore();

  const backend: StorageBackend = new IndexedDBStorageBackend({
    dbName: 'dao-agent-pi-ui',
    version: 1,
    stores: [
      settings.getConfig(),
      SessionsStore.getMetadataConfig(),
      providerKeys.getConfig(),
      customProviders.getConfig(),
      sessions.getConfig(),
    ],
  });

  settings.setBackend(backend);
  providerKeys.setBackend(backend);
  customProviders.setBackend(backend);
  sessions.setBackend(backend);

  const storage = new AppStorage(
      settings, providerKeys, sessions, customProviders, backend);
  setAppStorage(storage);

  // Mirror every Dao-configured API key into pi-web-ui's providerKeys store
  // so AgentInterface's pre-send check succeeds. The Agent's getApiKey is
  // still wired to llm_config, so the actual value sent to the provider is
  // whatever the user last set in Dao Settings.
  try {
    for (const spec of LLM_PROVIDERS) {
      // Map Dao's provider id to what pi-ai internally calls `provider`.
      // For 'openai-compatible' we stash under 'openai' because the model
      // we construct reports provider='openai'.
      const mirrorId = spec.id === 'openai-compatible' ? 'openai' : spec.id;
      const active = getActiveLLMConfig();
      if (active.provider === spec.id && active.apiKey) {
        await providerKeys.set(mirrorId, active.apiKey);
      }
    }
  } catch (_) {
    // Non-fatal — if mirroring fails the user can still type the key into
    // the prompt dialog.
  }

  return storage as AppStorageLike;
}

export function ensurePiAppStorage(): Promise<AppStorageLike> {
  if (!initPromise) {
    initPromise = initOnce();
  }
  return initPromise;
}

// Helper called whenever Dao settings change the active provider's key.
export async function syncActiveKeyToPiStorage(): Promise<void> {
  const storage = await ensurePiAppStorage();
  const active = getActiveLLMConfig();
  if (!active.apiKey) return;
  const mirrorId =
      active.provider === 'openai-compatible' ? 'openai' : active.provider;
  try {
    await storage.providerKeys.set(mirrorId, active.apiKey);
  } catch (_) { /* non-fatal */ }
}
