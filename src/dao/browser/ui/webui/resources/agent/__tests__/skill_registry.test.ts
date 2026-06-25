// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {describe, expect, it} from 'vitest';

import {
  buildAvailableSkillsPrompt,
  isSkillAvailableForHost,
  type SkillRegistryEntry,
} from '../skill_registry.js';

function skill(overrides: Partial<SkillRegistryEntry>): SkillRegistryEntry {
  return {
    id: 'summary',
    name: 'summary',
    description: 'Summarize the current page',
    source: 'builtin',
    hosts: ['*'],
    requiresPageContent: true,
    disabled: false,
    ...overrides,
  };
}

describe('skill_registry helpers', () => {
  it('treats empty and wildcard host lists as globally available', () => {
    expect(isSkillAvailableForHost(skill({hosts: []}), 'example.com'))
        .toBe(true);
    expect(isSkillAvailableForHost(skill({hosts: ['*']}), 'example.com'))
        .toBe(true);
  });

  it('matches exact and subdomain hosts', () => {
    const entry = skill({hosts: ['github.com']});

    expect(isSkillAvailableForHost(entry, 'github.com')).toBe(true);
    expect(isSkillAvailableForHost(entry, 'gist.github.com')).toBe(true);
    expect(isSkillAvailableForHost(entry, 'example.com')).toBe(false);
  });

  it('does not include disabled or host-unavailable skills in the prompt', () => {
    const prompt = buildAvailableSkillsPrompt([
      skill({id: 'summary', disabled: false, hosts: ['*']}),
      skill({id: 'disabled', disabled: true, hosts: ['*']}),
      skill({id: 'github-only', disabled: false, hosts: ['github.com']}),
    ], 'example.com');

    expect(prompt).toContain('<available_skills>');
    expect(prompt).toContain('id="summary"');
    expect(prompt).not.toContain('id="disabled"');
    expect(prompt).not.toContain('id="github-only"');
  });

  it('escapes XML-sensitive metadata in the prompt', () => {
    const prompt = buildAvailableSkillsPrompt([
      skill({
        id: 'quote-&-skill',
        name: 'Quote <Skill>',
        description: 'Use A & B "carefully"',
        source: 'user<local>',
      }),
    ], 'example.com');

    expect(prompt).toContain('id="quote-&amp;-skill"');
    expect(prompt).toContain('source="user&lt;local&gt;"');
    expect(prompt).toContain('Quote &lt;Skill&gt;');
    expect(prompt).toContain('Use A &amp; B &quot;carefully&quot;');
  });

  it('limits the prompt to 40 skills and reports omitted entries', () => {
    const entries = Array.from({length: 42}, (_, index) => skill({
      id: `skill-${index}`,
      name: `Skill ${index}`,
    }));

    const prompt = buildAvailableSkillsPrompt(entries, 'example.com');

    expect(prompt).toContain('id="skill-0"');
    expect(prompt).toContain('id="skill-39"');
    expect(prompt).not.toContain('id="skill-40"');
    expect(prompt).toContain(
        '<!-- 2 additional skills omitted from this turn. -->');
  });

  it('returns an empty string when no skills are available', () => {
    expect(buildAvailableSkillsPrompt([
      skill({disabled: true}),
    ], 'example.com')).toBe('');
  });
});
