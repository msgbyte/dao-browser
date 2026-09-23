// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Resolves the active Dao provider config into the pi-ai model object that
// both the chat Agent and the legacy streaming adapter hand to pi-ai.

import {lookupCostByModelId} from './llm_cost.js';
import {lookupModelCapabilities} from './model_capabilities.js';
// eslint-disable-next-line @typescript-eslint/no-explicit-any
import * as pi from './vendor/pi_runtime_bundle.js';

export interface PiModel {
  id: string;
  name: string;
  api: string;
  provider: string;
  baseUrl: string;
  reasoning: boolean;
  input: Array<'text'|'image'>;
  cost: {input: number; output: number; cacheRead: number; cacheWrite: number};
  contextWindow: number;
  maxTokens: number;
}

function buildOpenAICompatModel(modelId: string, baseUrl: string): PiModel {
  // pi-ai appends `/chat/completions` itself when the API is
  // `openai-completions`; strip trailing slashes and ensure `/v1`.
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

// Built-in providers use pi-ai's catalog entry. A user base URL (proxy or
// gateway) replaces the catalog endpoint verbatim, matching pi-ai's own
// convention (e.g. Anthropic has no `/v1`, OpenAI does). Model ids outside
// the catalog (gateway aliases such as "vendor/claude-x") clone a sibling
// entry so requests keep the provider's native API; pi-ai's getModel
// returns undefined for them rather than throwing.
export function resolvePiModel(
    provider: string, modelId: string, baseUrl: string): PiModel {
  if (provider === 'openai-compatible') {
    return buildOpenAICompatModel(
        modelId, baseUrl || 'https://api.openai.com');
  }
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const mod = pi as any;
  let model = mod.getModel(provider, modelId) as PiModel | undefined;
  if (!model) {
    const caps = lookupModelCapabilities(modelId);
    model = {
      ...(mod.getModels(provider) as PiModel[])[0]!,
      id: modelId,
      name: modelId,
      reasoning: false,
      cost: lookupCostByModelId(modelId),
      contextWindow: caps.contextWindow,
      maxTokens: caps.maxTokens,
    };
  }
  const base = baseUrl.replace(/\/+$/, '');
  return base ? {...model, baseUrl: base} : model;
}
