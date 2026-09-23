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

// Dao's API-format providers reuse a pi-ai provider's wire protocol, and
// the models built for them report that provider. API keys are mirrored
// and looked up under this id.
export function piProviderId(provider: string): string {
  if (provider === 'openai-compatible') return 'openai';
  if (provider === 'anthropic-compatible') return 'anthropic';
  return provider;
}

// Named providers use pi-ai's catalog entry and endpoint. Model ids outside
// the catalog (new releases, gateway aliases such as "vendor/claude-x")
// clone a sibling entry so requests keep the provider's native API; pi-ai's
// getModel returns undefined for them rather than throwing.
// `anthropic-compatible` is the Anthropic Messages API on a user base URL.
// The Anthropic SDK appends `/v1/messages` itself, so a trailing `/v1` the
// user copied from gateway docs is dropped.
export function resolvePiModel(
    provider: string, modelId: string, baseUrl: string): PiModel {
  if (provider === 'openai-compatible') {
    return buildOpenAICompatModel(
        modelId, baseUrl || 'https://api.openai.com');
  }
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const mod = pi as any;
  const piProvider = piProviderId(provider);
  let model = mod.getModel(piProvider, modelId) as PiModel | undefined;
  if (!model) {
    const caps = lookupModelCapabilities(modelId);
    model = {
      ...(mod.getModels(piProvider) as PiModel[])[0]!,
      id: modelId,
      name: modelId,
      reasoning: false,
      cost: lookupCostByModelId(modelId),
      contextWindow: caps.contextWindow,
      maxTokens: caps.maxTokens,
    };
  }
  if (provider !== 'anthropic-compatible') return model;
  const base = baseUrl.replace(/\/+$/, '').replace(/\/v1$/, '');
  return base ? {...model, baseUrl: base} : model;
}
