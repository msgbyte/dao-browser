// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Heuristic lookup for context-window and max-output-token sizes
// based on a model id. Used by buildOpenAICompatModel() in both
// pi_llm_stream.ts (request shaping) and dao_chat_view.ts (UI usage
// bar).
//
// This complements pi-ai's built-in catalog: for native providers
// (anthropic / openai / google / etc.) pi-ai already returns proper
// numbers via `getModel(provider, model)`. The heuristics here apply
// to `openai-compatible` mode where the user has pointed us at any
// OpenAI-shaped backend (LiteLLM, self-hosted vLLM, OpenRouter API,
// etc.) and the model id is the only signal we have.
//
// Matching:
//   - case-insensitive substring on the model id
//   - rules ordered most-specific-first (gpt-4o before gpt-4)
//   - falls back to a conservative 128k / 8k pair if nothing matches
//
// Update this list when new model families ship. Numbers are upper-
// bound advertised limits; actual usable size on a given backend may
// be smaller (e.g. provider-specific tier limits).

export interface ModelCapability {
  contextWindow: number;
  maxTokens: number;  // max OUTPUT tokens per response
}

interface CapabilityRule {
  keywords: string[];
  caps: ModelCapability;
}

// Order matters: more specific keywords first (e.g. gpt-4o before gpt-4).
const CAPABILITY_RULES: CapabilityRule[] = [
  // ---- OpenAI ----
  {keywords: ['gpt-5'], caps: {contextWindow: 400_000, maxTokens: 128_000}},
  {keywords: ['gpt-4.1'], caps: {contextWindow: 1_000_000, maxTokens: 32_768}},
  {keywords: ['o3-', 'o4-'],
   caps: {contextWindow: 200_000, maxTokens: 100_000}},
  {keywords: ['gpt-4o'], caps: {contextWindow: 128_000, maxTokens: 16_384}},
  {keywords: ['gpt-4-32k'],
   caps: {contextWindow: 32_768, maxTokens: 4_096}},
  {keywords: ['gpt-4'], caps: {contextWindow: 8_192, maxTokens: 4_096}},
  {keywords: ['gpt-3.5-turbo-16k'],
   caps: {contextWindow: 16_384, maxTokens: 4_096}},
  {keywords: ['gpt-3.5'], caps: {contextWindow: 16_384, maxTokens: 4_096}},

  // ---- Anthropic Claude ----
  // Sonnet 4 has a 1M-context beta tier; we use the GA 200k.
  {keywords: ['claude-sonnet-4', 'claude-opus-4', 'claude-haiku-4',
              'claude-mythos'],
   caps: {contextWindow: 200_000, maxTokens: 64_000}},
  {keywords: ['claude-3-7', 'claude-3.7', 'claude-3-5', 'claude-3.5'],
   caps: {contextWindow: 200_000, maxTokens: 8_192}},
  {keywords: ['claude-3'],
   caps: {contextWindow: 200_000, maxTokens: 4_096}},

  // ---- Google Gemini ----
  {keywords: ['gemini-3'],
   caps: {contextWindow: 1_000_000, maxTokens: 64_000}},
  {keywords: ['gemini-2.5'],
   caps: {contextWindow: 1_000_000, maxTokens: 65_536}},
  {keywords: ['gemini-2.0'],
   caps: {contextWindow: 1_000_000, maxTokens: 8_192}},
  {keywords: ['gemini-1.5-pro'],
   caps: {contextWindow: 2_000_000, maxTokens: 8_192}},
  {keywords: ['gemini-1.5'],
   caps: {contextWindow: 1_000_000, maxTokens: 8_192}},

  // ---- xAI Grok ----
  {keywords: ['grok-4', 'grok-3'],
   caps: {contextWindow: 256_000, maxTokens: 16_384}},
  {keywords: ['grok-2'],
   caps: {contextWindow: 131_072, maxTokens: 4_096}},

  // ---- Meta Llama ----
  {keywords: ['llama-3.3', 'llama-3.1', 'llama3.3', 'llama3.1'],
   caps: {contextWindow: 128_000, maxTokens: 8_192}},
  {keywords: ['llama-3', 'llama3'],
   caps: {contextWindow: 8_192, maxTokens: 4_096}},

  // ---- Mistral / Mixtral ----
  {keywords: ['mistral-large', 'mistral-medium'],
   caps: {contextWindow: 128_000, maxTokens: 8_192}},
  {keywords: ['mixtral'],
   caps: {contextWindow: 32_768, maxTokens: 4_096}},
  {keywords: ['mistral'],
   caps: {contextWindow: 32_768, maxTokens: 8_192}},

  // ---- DeepSeek ----
  {keywords: ['deepseek-v3', 'deepseek-r1', 'deepseek-chat',
              'deepseek-reasoner'],
   caps: {contextWindow: 128_000, maxTokens: 8_192}},
  {keywords: ['deepseek'],
   caps: {contextWindow: 32_768, maxTokens: 4_096}},

  // ---- Qwen ----
  {keywords: ['qwen3', 'qwen2.5', 'qwen-2.5'],
   caps: {contextWindow: 128_000, maxTokens: 8_192}},
  {keywords: ['qwen'],
   caps: {contextWindow: 32_768, maxTokens: 4_096}},

  // ---- Moonshot Kimi ----
  {keywords: ['kimi-k2', 'kimi'],
   caps: {contextWindow: 200_000, maxTokens: 8_192}},

  // ---- Zhipu GLM ----
  {keywords: ['glm-4.5', 'glm-4-plus', 'glm-4'],
   caps: {contextWindow: 128_000, maxTokens: 8_192}},
];

// Conservative fallback: most modern OpenAI-compatible backends
// support at least this much.
const DEFAULT_CAPS: ModelCapability = {
  contextWindow: 128_000,
  maxTokens: 8_192,
};

export function lookupModelCapabilities(modelId: string): ModelCapability {
  const lc = modelId.toLowerCase();
  for (const rule of CAPABILITY_RULES) {
    for (const kw of rule.keywords) {
      if (lc.includes(kw)) return rule.caps;
    }
  }
  return DEFAULT_CAPS;
}
