// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {describe, expect, it} from 'vitest';

import * as runtime from '../../src/dao/browser/ui/webui/resources/agent/vendor/pi_runtime_bundle.js';

const legacyRuntimeExports = [
  'getModel',
  'getModels',
  'getProviders',
  'stream',
  'streamSimple',
  'complete',
  'completeSimple',
  'getEnvApiKey',
] as const;

describe('Pi runtime vendor bundle', () => {
  it('preserves the legacy Pi AI API used by Dao', () => {
    for (const name of legacyRuntimeExports) {
      expect(runtime[name], `${name} export`).toBeTypeOf('function');
    }
  });

  it('constructs an Agent with the default stream function', () => {
    expect(() => new runtime.Agent()).not.toThrow();
  });

  it('provides the streamFn property expected by pi-web-ui', () => {
    const agent = new runtime.Agent() as unknown as {
      streamFn: (...args: unknown[]) => unknown;
    };
    const replacement = () => undefined;

    expect(agent.streamFn).toBeTypeOf('function');
    agent.streamFn = replacement;
    expect(agent.streamFn).toBe(replacement);
  });

  it('keeps proxy handling when callers pass the public streamSimple', () => {
    const agent = new runtime.Agent({streamFn: runtime.streamSimple});
    const customStream = () => undefined;
    const customAgent = new runtime.Agent({streamFn: customStream as never});

    expect(agent.streamFn).not.toBe(runtime.streamSimple);
    expect(customAgent.streamFn).toBe(customStream);
  });

  it('propagates proxy setting read failures after AppStorage is initialized',
      async () => {
        const storageError = new Error('settings unavailable');
        runtime.setAppStorage({
          settings: {
            get: async () => {
              throw storageError;
            },
          },
        } as never);
        const agent = new runtime.Agent();

        try {
          await expect(agent.streamFn({
            id: 'test-model',
            name: 'Test model',
            api: 'unsupported-api',
            provider: 'zai',
            baseUrl: 'https://example.com',
            reasoning: false,
            input: ['text'],
            cost: {input: 0, output: 0, cacheRead: 0, cacheWrite: 0},
            contextWindow: 1024,
            maxTokens: 128,
          } as never, {systemPrompt: '', messages: [], tools: []}, {
            apiKey: 'test-key',
          })).rejects.toBe(storageError);
        } finally {
          runtime.setAppStorage(null as never);
        }
      });
});
