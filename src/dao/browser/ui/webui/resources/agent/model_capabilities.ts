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

// Order matters: more specific keywords first.
const CAPABILITY_RULES: CapabilityRule[] = [
  // ---- OpenAI ----
  // gpt-5 family: 400k context, 128k output (per 2026-04 announcement).
  {keywords: ['gpt-5'], caps: {contextWindow: 400_000, maxTokens: 128_000}},
  // gpt-4.1: 1M context, 32k output.
  {keywords: ['gpt-4.1'], caps: {contextWindow: 1_000_000, maxTokens: 32_768}},
  // o3 / o4 reasoning models: 200k context, 100k reasoning output.
  {keywords: ['o3-', 'o4-'],
   caps: {contextWindow: 200_000, maxTokens: 100_000}},
  // gpt-4o family: 128k / 16k.
  {keywords: ['gpt-4o'], caps: {contextWindow: 128_000, maxTokens: 16_384}},
  // legacy gpt-4 (non-turbo): 8k context.
  {keywords: ['gpt-4-32k'],
   caps: {contextWindow: 32_768, maxTokens: 4_096}},
  {keywords: ['gpt-4'], caps: {contextWindow: 8_192, maxTokens: 4_096}},
  // gpt-3.5-turbo-16k.
  {keywords: ['gpt-3.5-turbo-16k'],
   caps: {contextWindow: 16_384, maxTokens: 4_096}},
  {keywords: ['gpt-3.5'], caps: {contextWindow: 16_384, maxTokens: 4_096}},

  // ---- Anthropic Claude ----
  // Claude 4 family + Mythos Preview: 200k context, 64k output.
  // (Sonnet 4 supports a 1M-context tier in beta but the default
  //  general-availability tier is 200k; we keep the conservative
  //  number.)
  {keywords: ['claude-sonnet-4', 'claude-opus-4', 'claude-haiku-4',
              'claude-mythos'],
   caps: {contextWindow: 200_000, maxTokens: 64_000}},
  // Claude 3.7 / 3.5: 200k / 8k.
  {keywords: ['claude-3-7', 'claude-3.7', 'claude-3-5', 'claude-3.5'],
   caps: {contextWindow: 200_000, maxTokens: 8_192}},
  // Claude 3: 200k / 4k.
  {keywords: ['claude-3'],
   caps: {contextWindow: 200_000, maxTokens: 4_096}},

  // ---- Google Gemini ----
  // Gemini 3 family: 1M context, 64k output.
  {keywords: ['gemini-3'],
   caps: {contextWindow: 1_000_000, maxTokens: 64_000}},
  // Gemini 2.5: 1M / 65k.
  {keywords: ['gemini-2.5'],
   caps: {contextWindow: 1_000_000, maxTokens: 65_536}},
  // Gemini 2.0: 1M / 8k.
  {keywords: ['gemini-2.0'],
   caps: {contextWindow: 1_000_000, maxTokens: 8_192}},
  // Gemini 1.5 Pro / Flash: 2M / 1M context, 8k output.
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
  // Llama 3.3 / 3.1 70B+ commonly served at 128k.
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
