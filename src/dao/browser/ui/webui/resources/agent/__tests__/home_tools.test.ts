// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, describe, expect, it} from 'vitest';

import {
  clearHomeToolContext,
  getHomeToolContext,
  getHomeToolDefinitions,
  getHomeSystemPrompt,
  setHomeToolContext,
} from '../home_tools.js';
import {TOOL_GROUPS} from '../tool_catalog.js';

describe('Home contextual Agent tools', () => {
  afterEach(() => clearHomeToolContext());

  it('does not expose Home tools without an active Home turn', () => {
    expect(getHomeToolContext()).toBeNull();
    expect(getHomeToolDefinitions()).toEqual([]);
  });

  it('exposes the atomic Home tool pack for the pinned revision', () => {
    setHomeToolContext({active: true, revision: 'revision-1'});

    expect(getHomeToolContext()).toEqual({
      active: true,
      revision: 'revision-1',
    });
    expect(getHomeToolDefinitions().map(tool => tool.function.name)).toEqual([
      'home_get_manifest',
      'home_list_files',
      'home_read_file',
      'home_get_diagnostics',
      'home_get_selected_element',
      'home_apply_patch',
      'home_replace_files',
      'home_add_asset',
      'home_preview',
      'home_publish',
      'home_rollback',
      'home_list_connectors',
      'home_collect_sample',
      'home_test_connector',
      'home_request_source_access',
      'home_list_versions',
      'home_export_project',
    ]);
    expect(getHomeSystemPrompt()).not.toContain(
        'action-first browser start surface');
    expect(getHomeSystemPrompt()).not.toContain('Do not show visit counts');
    expect(getHomeSystemPrompt()).toContain(
        'clears the default html and body margins');
    expect(getHomeSystemPrompt()).toContain(
        'Treat visual design as part of correctness');
    expect(getHomeSystemPrompt()).toContain(
        'Do not narrate this internal review');
    expect(getHomeSystemPrompt()).toContain(
        'call home_list_files and home_read_file for every');
    expect(getHomeSystemPrompt()).toContain(
        'Use home_replace_files when rewriting most or all');
    expect(getHomeSystemPrompt()).toContain(
        'Put every related replacement in the same call');
    expect(getHomeSystemPrompt()).toContain(
        'Never use Delete File plus Add File for the');
    expect(getHomeSystemPrompt()).toContain('*** End of File');
    const replaceTool = getHomeToolDefinitions().find(
        tool => tool.function.name === 'home_replace_files');
    expect(replaceTool?.function.parameters.properties.files).toEqual({
      type: 'array',
      minItems: 1,
      maxItems: 32,
      items: {
        type: 'object',
        properties: {
          path: {type: 'string'},
          contents: {type: 'string'},
        },
        required: ['path', 'contents'],
      },
    });
  });

  it('exposes the design-grounded feed bootstrap contract for a history turn',
     () => {
       setHomeToolContext({
         active: true,
         revision: '',
         bootstrapKind: 'history',
       });

       expect(getHomeSystemPrompt()).toContain(
           'comprehensive information Home');
       expect(getHomeSystemPrompt()).toContain(
           'Treat visual design as part of correctness');
       expect(getHomeSystemPrompt()).toMatch(
           /instead of forcing a\s+sidebar or a fixed column count/);
       expect(getHomeSystemPrompt()).toContain(
           'do not reserve empty rails');
       expect(getHomeSystemPrompt()).toMatch(
           /trusted browser rejects omissions on the first request/);
       expect(getHomeSystemPrompt()).toContain(
           'do not stretch an empty card across most of the viewport');
       expect(getHomeSystemPrompt()).toContain('never page content');
       expect(getHomeSystemPrompt()).toContain('Do not show visit counts');
       expect(getHomeSystemPrompt()).toContain('status `succeeded`');
       expect(getHomeSystemPrompt()).toContain(
           '`authentication_required`, `runtime_failed`, or `schema_failed`');
       expect(getHomeSystemPrompt()).toContain(
           'failed connector definition');
       expect(getHomeSystemPrompt()).toContain(
           'preserve its launch action');
       expect(getHomeSystemPrompt()).toMatch(
           /Keep every launch target/);
       expect(getHomeSystemPrompt()).toContain('data-dao-feed');
       expect(getHomeSystemPrompt()).toMatch(
           /Do not publish a page whose main\s+content is quick links/);
       expect(getHomeSystemPrompt()).toMatch(
           /opens those validated controls directly/);
       expect(getHomeSystemPrompt()).toContain('no fake sample content');
       expect(getHomeSystemPrompt()).toContain('sample_shape');
       expect(getHomeSystemPrompt()).toContain(
           'Never persist connector sample content');
       expect(getHomeSystemPrompt()).toMatch(
           /fixed Home\s+runtime opens those validated controls directly/);
       expect(getHomeSystemPrompt()).toContain(
           'do not add custom click handlers');
       expect(getHomeSystemPrompt()).toContain('<button type="button">');
       expect(getHomeSystemPrompt()).toMatch(
           /including trailing slashes/);
       expect(getHomeSystemPrompt()).toMatch(
           /do not call either source-access tool\s+again/);
       expect(getHomeSystemPrompt()).toContain('async collect(page, input)');
       expect(getHomeSystemPrompt()).toContain(
           'standalone, syntactically valid ECMAScript module');
       expect(getHomeSystemPrompt()).toMatch(
           /Do not use regular-expression literals or template\s+strings/);
       expect(getHomeSystemPrompt()).toContain(
           'Do not reference document, window, location, fetch');
       expect(getHomeSystemPrompt()).toMatch(
           /collection_url, content_intent, and content_kinds are generic\s+starting hints/);
       expect(getHomeSystemPrompt()).toMatch(
           /including a same-site\s+subdomain when appropriate/);
       expect(getHomeSystemPrompt()).toMatch(
           /aligned with its proposed content_intent and content_kinds/);
       expect(getHomeSystemPrompt()).toMatch(
           /source declaring video must not return text posts/);
       expect(getHomeSystemPrompt()).toMatch(
           /For following_feed or\s+subscription_feed/);
       expect(getHomeSystemPrompt()).toMatch(
           /never substitute home discovery,\s+popular\/trending/);
       expect(getHomeSystemPrompt()).toMatch(
           /every result must be\s+selected from inside a semantic feed-card/);
       expect(getHomeSystemPrompt()).toMatch(
           /Never\s+fall back to unscoped selectors/);
       expect(getHomeSystemPrompt()).toMatch(
           /literal selector "a\[href\]" must\s+not appear anywhere in a personalized collector/);
       expect(getHomeSystemPrompt()).toMatch(
           /finish an optionalFields\s+fallback with the runtime root shorthand "href"/);
       expect(getHomeSystemPrompt()).toContain(
           'Treat collector authoring as a one-shot task');
       expect(getHomeSystemPrompt()).toMatch(
           /first submitted module must\s+already contain its complete bounded fallback chain/);
       expect(getHomeSystemPrompt()).toMatch(
           /query semantic card roots with queryAll and an\s+optionalFields/);
       expect(getHomeSystemPrompt()).toContain(
           'survive internal class-name changes');
       expect(getHomeSystemPrompt()).toMatch(
           /union of plausible repeated semantic card roots/);
       expect(getHomeSystemPrompt()).toMatch(
           /Every\s+queryAll call in a personalized collector must select those card roots/);
       expect(getHomeSystemPrompt()).toMatch(
           /never query content links directly at document,\s+main, body, or html scope/);
       expect(getHomeSystemPrompt()).toMatch(
           /If none of the scoped roots match, return an empty\s+array/);
       expect(getHomeSystemPrompt()).toMatch(
           /rejects even content-path-constrained whole-page\s+link fallbacks/);
       expect(getHomeSystemPrompt()).toMatch(
           /complete comma-separated card-root selector\s+directly in queryAll as a quoted string literal/);
       expect(getHomeSystemPrompt()).toMatch(
           /never assign it to a variable,\s+build it dynamically, or pass an identifier/);
       expect(getHomeSystemPrompt()).toMatch(
           /absolute,\s+protocol-relative, and root-relative href values/);
       expect(getHomeSystemPrompt()).toMatch(
           /content-semantic URL predicate must prove an item URL, not merely prove the\s+site or origin/);
       expect(getHomeSystemPrompt()).toMatch(
           /Explicitly reject collection_url\s+itself, its origin root, navigation routes/);
       expect(getHomeSystemPrompt()).toMatch(
           /Every accepted branch must describe a complete\s+content-path family and validate the identifier/);
       expect(getHomeSystemPrompt()).toMatch(
           /is not content-semantic and must never be\s+used/);
       expect(getHomeSystemPrompt()).toMatch(
           /Pair every content-path family with an exact canonical content host allowlist/);
       expect(getHomeSystemPrompt()).toMatch(
           /Validate the normalized URL's complete host before inspecting its path/);
       expect(getHomeSystemPrompt()).toMatch(
           /require both the allowed host\s+and the content-path identifier/);
       expect(getHomeSystemPrompt()).toContain(
           'each result is then {text, tag, href, media}');
       expect(getHomeSystemPrompt()).toMatch(
           /root shorthands\s+"text", "href", and "media"/);
       expect(getHomeSystemPrompt()).toMatch(
           /wait for a\s+semantic feed\/card\/link selector/);
       expect(getHomeSystemPrompt()).toMatch(
           /Do not treat navigation,\s+pricing, product-promotion/);
       expect(getHomeSystemPrompt()).toContain('dao.sources.collect');
       expect(getHomeSystemPrompt()).toContain(
           'exactly 10 * 60 * 1000 milliseconds');
       expect(getHomeSystemPrompt()).toMatch(
           /connector-specific cache entry with dao\.session\.get/);
       expect(getHomeSystemPrompt()).toMatch(
           /successful returned array plus Date\.now\(\) with dao\.session\.set/);
       expect(getHomeSystemPrompt()).toContain(
           'never create a refresh timer');
       expect(getHomeSystemPrompt()).toMatch(
           /Cache an\s+empty successful array/);
       expect(getHomeSystemPrompt()).toContain('data-dao-source-slot');
       expect(getHomeSystemPrompt()).toContain('data-dao-connector');
       expect(getHomeSystemPrompt()).toContain('preserves the authored');
       expect(getHomeSystemPrompt()).toContain('schema_source');
       expect(getHomeSystemPrompt()).toMatch(
           /Do not author connector\s+manifest entries/);
       expect(getHomeToolDefinitions().map(tool => tool.function.name))
           .toContain('home_get_bootstrap_brief');
       const batchTool = getHomeToolDefinitions().find(
           tool => tool.function.name === 'home_request_bootstrap_sources');
       expect(batchTool).toBeDefined();
       const sourceSchema =
           batchTool?.function.parameters.properties.sources as
           Record<string, unknown>;
       expect(sourceSchema.type).toBe('array');
       expect(sourceSchema.minItems).toBe(0);
       expect(sourceSchema.maxItems).toBe(3);
       expect(getHomeSystemPrompt()).toContain('sources: []');
       expect(getHomeSystemPrompt()).toMatch(
           /has no ordinary\s+tool timeout/);
       expect(getHomeSystemPrompt()).toMatch(
           /Do not create\s+another provisional draft/);
       expect(getHomeSystemPrompt()).toMatch(
           /Do not preview or publish the\s+provisional draft/);
       expect(getHomeSystemPrompt()).toMatch(
           /native Home\s+host writes canonical experience\.json metadata/);
       expect(getHomeSystemPrompt()).toMatch(
           /never call home_request_source_access/);
       expect(batchTool?.function.parameters.required).toEqual([
         'base_revision',
         'draft_id',
         'sources',
       ]);
       expect(getHomeToolDefinitions().map(tool => tool.function.name))
           .not.toContain('home_get_history_material');
  });

  it('requires a base revision for every mutating tool', () => {
    setHomeToolContext({active: true, revision: 'revision-1'});
    const mutations = new Set([
      'home_apply_patch',
      'home_replace_files',
      'home_add_asset',
      'home_publish',
      'home_rollback',
      'home_request_source_access',
      'home_request_bootstrap_sources',
    ]);

    for (const tool of getHomeToolDefinitions()) {
      if (mutations.has(tool.function.name)) {
        expect(tool.function.parameters.required).toContain('base_revision');
      }
    }
  });

  it('keeps every Home tool controllable from settings', () => {
    setHomeToolContext({
      active: true,
      revision: 'revision-1',
      bootstrapKind: 'history',
    });
    const homeGroup = TOOL_GROUPS.find(group => group.id === 'home');

    expect(homeGroup?.toolNames).toEqual(
        getHomeToolDefinitions().map(tool => tool.function.name));
  });

  it('clears stale context instead of accepting an inactive replacement', () => {
    setHomeToolContext({active: true, revision: 'revision-1'});
    setHomeToolContext({active: false, revision: 'revision-2'});

    expect(getHomeToolContext()).toBeNull();
    expect(getHomeToolDefinitions()).toEqual([]);
  });
});
