// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {ActiveLLMConfig} from './llm_config.js';
// eslint-disable-next-line @typescript-eslint/no-explicit-any
import * as pi from './vendor/pi_runtime_bundle.js';

export interface LlmCostRates {
  input: number;
  output: number;
  cacheRead: number;
  cacheWrite: number;
}

const ZERO_COST: LlmCostRates = {
  input: 0,
  output: 0,
  cacheRead: 0,
  cacheWrite: 0,
};

function normalizeCost(cost: unknown): LlmCostRates|null {
  if (!cost || typeof cost !== 'object') {
    return null;
  }
  const c = cost as Partial<Record<keyof LlmCostRates, unknown>>;
  return {
    input: Number(c.input) || 0,
    output: Number(c.output) || 0,
    cacheRead: Number(c.cacheRead) || 0,
    cacheWrite: Number(c.cacheWrite) || 0,
  };
}

// When the user routes a well-known model through an OpenAI-compatible gateway
// (e.g. OpenRouter, LiteLLM, a corporate proxy), pi-ai's built-in catalog can
// still have the real pricing for that model id. Do a name-based lookup across
// every provider so Estimated Cost reflects real rates instead of $0 when
// possible.
export function lookupCostByModelId(modelId: string): LlmCostRates {
  try {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const mod = pi as any;
    const providers: string[] = mod.getProviders?.() ?? [];
    for (const provider of providers) {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      const models: any[] = mod.getModels?.(provider) ?? [];
      const match = models.find((model) => model?.id === modelId);
      const cost = normalizeCost(match?.cost);
      if (cost) {
        return cost;
      }
    }
  } catch (_) {
    // Catalog access failed; fall through to zeros.
  }
  return ZERO_COST;
}

export function getCostRatesForConfig(
    config: Pick<ActiveLLMConfig, 'provider'|'model'>): LlmCostRates {
  try {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const mod = pi as any;
    const model = mod.getModel?.(config.provider, config.model);
    const cost = normalizeCost(model?.cost);
    if (cost) {
      return cost;
    }
  } catch (_) {
    // Unknown provider/model pairs can still be priced by model id below.
  }
  return lookupCostByModelId(config.model);
}
