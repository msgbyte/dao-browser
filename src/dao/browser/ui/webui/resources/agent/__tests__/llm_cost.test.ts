// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const piMocks = vi.hoisted(() => ({
  getModel: vi.fn(),
  getModels: vi.fn(),
  getProviders: vi.fn(),
}));

vi.mock('../vendor/pi_runtime_bundle.js', () => ({
  getModel: (...args: unknown[]) => piMocks.getModel(...args),
  getModels: (...args: unknown[]) => piMocks.getModels(...args),
  getProviders: (...args: unknown[]) => piMocks.getProviders(...args),
}));

import {getCostRatesForConfig, lookupCostByModelId} from '../llm_cost.js';

describe('llm_cost', () => {
  beforeEach(() => {
    piMocks.getModel.mockReset();
    piMocks.getModels.mockReset();
    piMocks.getProviders.mockReset();
  });

  it('uses native provider model cost when available', () => {
    piMocks.getModel.mockReturnValue({
      cost: {input: 3, output: 9, cacheRead: 0.3, cacheWrite: 1.5},
    });

    expect(getCostRatesForConfig({provider: 'openai', model: 'gpt-test'}))
        .toEqual({input: 3, output: 9, cacheRead: 0.3, cacheWrite: 1.5});
  });

  it('looks up gateway model cost by model id across providers', () => {
    piMocks.getModel.mockImplementation(() => {
      throw new Error('unknown provider');
    });
    piMocks.getProviders.mockReturnValue(['openai', 'anthropic']);
    piMocks.getModels.mockImplementation((provider: string) =>
      provider === 'openai' ?
        [{id: 'gpt-test', cost: {input: 2, output: 6}}] :
        []);

    expect(getCostRatesForConfig({
      provider: 'openai-compatible',
      model: 'gpt-test',
    })).toEqual({input: 2, output: 6, cacheRead: 0, cacheWrite: 0});
    expect(lookupCostByModelId('gpt-test'))
        .toEqual({input: 2, output: 6, cacheRead: 0, cacheWrite: 0});
  });
});
