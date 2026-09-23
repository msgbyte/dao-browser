// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const piMocks = vi.hoisted(() => ({
  getModel: vi.fn(),
  getModels: vi.fn(),
}));

vi.mock('../vendor/pi_runtime_bundle.js', () => ({
  getModel: (...args: unknown[]) => piMocks.getModel(...args),
  getModels: (...args: unknown[]) => piMocks.getModels(...args),
}));

import {piProviderId, resolvePiModel} from '../pi_model.js';

const ANTHROPIC_ENTRY = {
  id: 'claude-sonnet-4-5',
  name: 'Claude Sonnet 4.5',
  api: 'anthropic-messages',
  provider: 'anthropic',
  baseUrl: 'https://api.anthropic.com',
  reasoning: true,
  input: ['text', 'image'],
  cost: {input: 3, output: 15, cacheRead: 0, cacheWrite: 0},
  contextWindow: 200000,
  maxTokens: 64000,
};

describe('resolvePiModel', () => {
  beforeEach(() => {
    piMocks.getModel.mockReset();
    piMocks.getModels.mockReset();
    piMocks.getModels.mockReturnValue([ANTHROPIC_ENTRY]);
  });

  it('keeps named providers on their catalog endpoint', () => {
    piMocks.getModel.mockReturnValue(ANTHROPIC_ENTRY);

    const model = resolvePiModel(
        'anthropic', 'claude-sonnet-4-5', 'https://gateway.example');

    expect(piMocks.getModel).toHaveBeenCalledWith(
        'anthropic', 'claude-sonnet-4-5');
    expect(model.baseUrl).toBe('https://api.anthropic.com');
  });

  it('routes anthropic-compatible catalog models to the user base URL', () => {
    piMocks.getModel.mockReturnValue(ANTHROPIC_ENTRY);

    const model = resolvePiModel(
        'anthropic-compatible', 'claude-sonnet-4-5',
        'https://gateway.example/anthropic/v1/');

    expect(piMocks.getModel).toHaveBeenCalledWith(
        'anthropic', 'claude-sonnet-4-5');
    expect(model).toMatchObject({
      id: 'claude-sonnet-4-5',
      api: 'anthropic-messages',
      provider: 'anthropic',
      baseUrl: 'https://gateway.example/anthropic',
      reasoning: true,
    });
  });

  it('clones the Anthropic API for gateway model aliases', () => {
    piMocks.getModel.mockReturnValue(undefined);

    const model = resolvePiModel(
        'anthropic-compatible', 'vendor/claude-custom',
        'https://gateway.example');

    expect(piMocks.getModels).toHaveBeenCalledWith('anthropic');
    expect(model).toMatchObject({
      id: 'vendor/claude-custom',
      name: 'vendor/claude-custom',
      api: 'anthropic-messages',
      provider: 'anthropic',
      baseUrl: 'https://gateway.example',
      reasoning: false,
    });
  });

  it('falls back to the catalog endpoint when no base URL is set', () => {
    piMocks.getModel.mockReturnValue(ANTHROPIC_ENTRY);

    const model =
        resolvePiModel('anthropic-compatible', 'claude-sonnet-4-5', '');

    expect(model.baseUrl).toBe('https://api.anthropic.com');
  });

  it('builds an OpenAI chat completions model for openai-compatible', () => {
    const model = resolvePiModel(
        'openai-compatible', 'local-model', 'http://localhost:8000/');

    expect(piMocks.getModel).not.toHaveBeenCalled();
    expect(model).toMatchObject({
      id: 'local-model',
      api: 'openai-completions',
      provider: 'openai',
      baseUrl: 'http://localhost:8000/v1',
    });
  });
});

describe('piProviderId', () => {
  it('maps API formats to the pi-ai provider their models report', () => {
    expect(piProviderId('openai-compatible')).toBe('openai');
    expect(piProviderId('anthropic-compatible')).toBe('anthropic');
    expect(piProviderId('google')).toBe('google');
  });
});
