// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Per-provider LLM configuration. Stores one {apiKey, baseUrl, model} entry
// per provider in localStorage so users can swap providers without retyping
// credentials, plus an `activeProvider` pointer.
//
// PR1 introduces this alongside the pi-ai adapter; the chat view and
// settings view read/write through these helpers instead of the three flat
// legacy keys.

export interface ProviderSpec {
  id: string;
  label: string;
  // If true, the user supplies the baseUrl (e.g. self-hosted vLLM). Otherwise
  // pi-ai resolves the upstream endpoint from its built-in model catalog.
  needsBaseUrl: boolean;
  defaultModel: string;
  defaultBaseUrl?: string;
  apiKeyPlaceholder: string;
}

export const LLM_PROVIDERS: ProviderSpec[] = [
  {
    id: 'openai-compatible',
    label: 'OpenAI-compatible',
    needsBaseUrl: true,
    defaultModel: 'gpt-5',
    defaultBaseUrl: 'https://api.openai.com/v1',
    apiKeyPlaceholder: 'sk-...',
  },
  {
    id: 'openai',
    label: 'OpenAI',
    needsBaseUrl: false,
    defaultModel: 'gpt-5',
    apiKeyPlaceholder: 'sk-...',
  },
  {
    id: 'anthropic',
    label: 'Anthropic',
    needsBaseUrl: false,
    defaultModel: 'claude-sonnet-4-5',
    apiKeyPlaceholder: 'sk-ant-...',
  },
  {
    id: 'google',
    label: 'Google',
    needsBaseUrl: false,
    defaultModel: 'gemini-2.5-flash',
    apiKeyPlaceholder: 'AIza...',
  },
  {
    id: 'groq',
    label: 'Groq',
    needsBaseUrl: false,
    defaultModel: 'llama-3.3-70b-versatile',
    apiKeyPlaceholder: 'gsk_...',
  },
  {
    id: 'xai',
    label: 'xAI',
    needsBaseUrl: false,
    defaultModel: 'grok-4',
    apiKeyPlaceholder: 'xai-...',
  },
  {
    id: 'openrouter',
    label: 'OpenRouter',
    needsBaseUrl: false,
    defaultModel: 'openrouter/auto',
    apiKeyPlaceholder: 'sk-or-...',
  },
];

export interface ProviderConfig {
  apiKey: string;
  baseUrl: string;
  model: string;
}

const PROVIDERS_KEY = 'dao_agent_providers';
const ACTIVE_PROVIDER_KEY = 'dao_agent_active_provider';
const DEFAULT_PROVIDER_ID = 'openai-compatible';

// Legacy flat-key names kept for one-time migration. Read-only after PR1.
const LEGACY_API_KEY = 'dao_agent_api_key';
const LEGACY_BASE_URL = 'dao_agent_base_url';
const LEGACY_MODEL = 'dao_agent_model';

function getSpec(id: string): ProviderSpec {
  return LLM_PROVIDERS.find((p) => p.id === id) ?? LLM_PROVIDERS[0]!;
}

function readProvidersMap(): Record<string, ProviderConfig> {
  try {
    const raw = localStorage.getItem(PROVIDERS_KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw) as unknown;
    if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
      return parsed as Record<string, ProviderConfig>;
    }
  } catch (_) { /* corrupt json — fall through */ }
  return {};
}

function writeProvidersMap(map: Record<string, ProviderConfig>): void {
  localStorage.setItem(PROVIDERS_KEY, JSON.stringify(map));
}

// Migrates the three legacy flat keys into the new per-provider shape the
// first time this module is consumed. Idempotent: if `dao_agent_providers`
// already exists, we leave it alone. The legacy keys are kept in place as a
// rollback safety net — they can be deleted in a later PR.
function migrateLegacyIfNeeded(): void {
  if (localStorage.getItem(PROVIDERS_KEY) !== null) return;
  const apiKey = localStorage.getItem(LEGACY_API_KEY);
  const baseUrl = localStorage.getItem(LEGACY_BASE_URL);
  const model = localStorage.getItem(LEGACY_MODEL);
  if (apiKey === null && baseUrl === null && model === null) return;
  const spec = getSpec(DEFAULT_PROVIDER_ID);
  writeProvidersMap({
    [DEFAULT_PROVIDER_ID]: {
      apiKey: apiKey ?? '',
      baseUrl: baseUrl ?? (spec.defaultBaseUrl ?? ''),
      model: model ?? spec.defaultModel,
    },
  });
  if (localStorage.getItem(ACTIVE_PROVIDER_KEY) === null) {
    localStorage.setItem(ACTIVE_PROVIDER_KEY, DEFAULT_PROVIDER_ID);
  }
}

migrateLegacyIfNeeded();

export function getActiveProvider(): string {
  const id = localStorage.getItem(ACTIVE_PROVIDER_KEY) ?? DEFAULT_PROVIDER_ID;
  return LLM_PROVIDERS.some((p) => p.id === id) ? id : DEFAULT_PROVIDER_ID;
}

export function setActiveProvider(id: string): void {
  if (!LLM_PROVIDERS.some((p) => p.id === id)) return;
  localStorage.setItem(ACTIVE_PROVIDER_KEY, id);
}

export function getProviderConfig(id: string): ProviderConfig {
  const spec = getSpec(id);
  const map = readProvidersMap();
  const saved = map[id];
  return {
    apiKey: saved?.apiKey ?? '',
    baseUrl: saved?.baseUrl ?? (spec.defaultBaseUrl ?? ''),
    model: saved?.model || spec.defaultModel,
  };
}

export function setProviderConfig(
    id: string, config: Partial<ProviderConfig>): void {
  const map = readProvidersMap();
  const prev = map[id] ?? getProviderConfig(id);
  map[id] = {
    apiKey: config.apiKey ?? prev.apiKey,
    baseUrl: config.baseUrl ?? prev.baseUrl,
    model: config.model ?? prev.model,
  };
  writeProvidersMap(map);
}

export interface ActiveLLMConfig {
  provider: string;
  apiKey: string;
  baseUrl: string;
  model: string;
}

export function getActiveLLMConfig(): ActiveLLMConfig {
  const provider = getActiveProvider();
  const config = getProviderConfig(provider);
  return {provider, ...config};
}
