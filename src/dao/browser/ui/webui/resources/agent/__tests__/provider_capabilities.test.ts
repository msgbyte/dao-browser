// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { describe, expect, it } from 'vitest';

import {
  getProviderToolSpecKind,
  isProviderSearchAvailable,
} from '../web_search/provider_capabilities.js';

describe('web_search/provider_capabilities', () => {
  describe('Layer 1: exact provider allowlist', () => {
    it('matches the Claude 4 family on Anthropic', () => {
      expect(isProviderSearchAvailable('anthropic', 'claude-sonnet-4-5'))
        .toBe(true);
      expect(getProviderToolSpecKind('anthropic', 'claude-opus-4-7-20260124'))
        .toBe('anthropic');
      expect(isProviderSearchAvailable('anthropic', 'claude-haiku-4-5'))
        .toBe(true);
    });

    it('rejects Claude 3 family on Anthropic', () => {
      expect(isProviderSearchAvailable('anthropic', 'claude-3-sonnet'))
        .toBe(false);
      expect(isProviderSearchAvailable('anthropic', 'claude-3-opus'))
        .toBe(false);
    });

    it('matches GPT-5 / 4o / 4.1 / o3 / o4 on OpenAI', () => {
      expect(getProviderToolSpecKind('openai', 'gpt-5-pro'))
        .toBe('openai-responses');
      expect(getProviderToolSpecKind('openai', 'gpt-4o'))
        .toBe('openai-responses');
      expect(getProviderToolSpecKind('openai', 'gpt-4.1-mini'))
        .toBe('openai-responses');
      expect(getProviderToolSpecKind('openai', 'o3-mini'))
        .toBe('openai-responses');
      expect(getProviderToolSpecKind('openai', 'o4-mini'))
        .toBe('openai-responses');
    });

    it('rejects pre-grounding GPT models', () => {
      expect(isProviderSearchAvailable('openai', 'gpt-3.5-turbo')).toBe(false);
    });

    it('matches Gemini 2.x and 3.x on Google', () => {
      expect(getProviderToolSpecKind('google', 'gemini-2.0-flash'))
        .toBe('gemini-grounding');
      expect(getProviderToolSpecKind('google', 'gemini-2.5-pro'))
        .toBe('gemini-grounding');
      expect(getProviderToolSpecKind('google', 'gemini-3-flash'))
        .toBe('gemini-grounding');
    });

    it('rejects legacy Gemini 1.x', () => {
      expect(isProviderSearchAvailable('google', 'gemini-1.5-flash'))
        .toBe(false);
    });
  });

  describe('Layer 2: openai-compatible keyword inference', () => {
    it('infers anthropic spec from LiteLLM-style namespaced model', () => {
      expect(getProviderToolSpecKind(
        'openai-compatible', 'anthropic/claude-sonnet-4-5'))
        .toBe('anthropic');
    });

    it('infers openai-responses spec from azure-namespaced gpt-4o', () => {
      expect(getProviderToolSpecKind('openai-compatible', 'azure/gpt-4o'))
        .toBe('openai-responses');
    });

    it('infers gemini-grounding from gemini-2.5 in proxy model name', () => {
      expect(getProviderToolSpecKind(
        'openai-compatible', 'vertex/gemini-2.5-pro'))
        .toBe('gemini-grounding');
    });

    it('returns null when no keyword matches', () => {
      expect(getProviderToolSpecKind('openai-compatible', 'mistral-large'))
        .toBeNull();
    });

    it('is case-insensitive on the inference path', () => {
      expect(getProviderToolSpecKind(
        'openai-compatible', 'Anthropic/Claude-Sonnet-4-5'))
        .toBe('anthropic');
    });
  });

  describe('non-allowlist providers do NOT infer', () => {
    // Groq/XAI/OpenRouter are intentionally excluded from inference even
    // when their model names contain matching keywords, per the doc
    // comment in provider_capabilities.ts.
    it('does not infer for groq even with claude-shaped model id', () => {
      expect(isProviderSearchAvailable('groq', 'claude-sonnet-4-5'))
        .toBe(false);
    });

    it('does not infer for openrouter', () => {
      expect(isProviderSearchAvailable('openrouter', 'gpt-4o')).toBe(false);
    });

    it('does not infer for xai', () => {
      expect(isProviderSearchAvailable('xai', 'gpt-5')).toBe(false);
    });
  });
});
