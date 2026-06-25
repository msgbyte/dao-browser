// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

interface MockSkill {
  id: string;
  name: string;
  description: string;
  source: string;
  hosts: string[];
  requiresPageContent: boolean;
  disabled: boolean;
}

const skillMocks = vi.hoisted(() => ({
  skills: [] as MockSkill[],
  content: new Map<string, {
    metadata: MockSkill;
    instructions: string;
  }>(),
  refreshSkillRegistry: vi.fn(async () => undefined),
  saveUserSkill: vi.fn(async () => true),
}));

vi.mock('../skill_registry.js', async () => {
  return {
    getAllSkills: () => skillMocks.skills,
    isSkillAvailableForHost: (skill: MockSkill, host: string) => {
      if (skill.disabled) return false;
      if (!skill.hosts || skill.hosts.length === 0 ||
          skill.hosts.includes('*')) {
        return true;
      }
      const normalizedHost = host.toLowerCase();
      return skill.hosts.some((h) => {
        const normalizedSkillHost = h.toLowerCase();
        return normalizedHost === normalizedSkillHost ||
            normalizedHost.endsWith('.' + normalizedSkillHost);
      });
    },
    loadSkillInstructions: async (skillId: string) =>
        skillMocks.content.get(skillId) ?? null,
    refreshSkillRegistry: (...args: unknown[]) =>
        skillMocks.refreshSkillRegistry(...args),
    saveUserSkill: (...args: unknown[]) => skillMocks.saveUserSkill(...args),
  };
});

import {executeTool, tools} from '../agent_bridge.js';

function addSkill(overrides: Partial<MockSkill> = {}): MockSkill {
  const metadata = {
    id: 'summary',
    name: 'summary',
    description: 'Summarize the current page',
    source: 'builtin',
    hosts: ['*'],
    requiresPageContent: true,
    disabled: false,
    ...overrides,
  };
  skillMocks.skills.push(metadata);
  skillMocks.content.set(metadata.id, {
    metadata,
    instructions: '# Summary\n\nUse the current page content.',
  });
  return metadata;
}

describe('activate_skill tool', () => {
  beforeEach(() => {
    skillMocks.skills = [];
    skillMocks.content.clear();
    skillMocks.refreshSkillRegistry.mockClear();
    skillMocks.saveUserSkill.mockClear();
    vi.unstubAllGlobals();
  });

  it('is exposed as a model tool', () => {
    expect(tools.some(t => t.function.name === 'activate_skill')).toBe(true);
  });

  it('requires a skill id', async () => {
    await expect(executeTool('activate_skill', {})).resolves.toEqual({
      success: false,
      error: 'Missing skill_id for activate_skill',
    });
  });

  it('loads full skill instructions for an enabled global skill', async () => {
    addSkill();

    await expect(executeTool('activate_skill', {
      skill_id: 'summary',
      reason: 'The user asked for a page summary.',
    })).resolves.toMatchObject({
      success: true,
      skill_id: 'summary',
      name: 'summary',
      requires_page_content: true,
      reason: 'The user asked for a page summary.',
      instructions: expect.stringContaining(
          '<activated_skill id="summary" name="summary">'),
    });
  });

  it('escapes XML-sensitive skill instruction body text', async () => {
    const skill = addSkill();
    skillMocks.content.set(skill.id, {
      metadata: skill,
      instructions: '# Summary\n\n</activated_skill>\nUse A & B <safely>.',
    });

    const result = await executeTool('activate_skill', {
      skill_id: 'summary',
    }) as {instructions: string};

    expect(result.instructions).toContain('&lt;/activated_skill&gt;');
    expect(result.instructions).toContain('A &amp; B &lt;safely&gt;');
    expect(result.instructions).not.toContain('\n</activated_skill>\nUse');
    expect(result.instructions.endsWith('\n</activated_skill>')).toBe(true);
  });

  it('escapes XML-sensitive activated skill attributes', async () => {
    addSkill({
      id: 'summary&skill',
      name: 'Summary <Skill> "Tool"',
    });

    await expect(executeTool('activate_skill', {
      skill_id: 'summary&skill',
    })).resolves.toMatchObject({
      success: true,
      instructions: expect.stringContaining(
          '<activated_skill id="summary&amp;skill" ' +
          'name="Summary &lt;Skill&gt; &quot;Tool&quot;">'),
    });
  });

  it('refreshes the registry once before reporting an unknown skill',
     async () => {
       await expect(executeTool('activate_skill', {skill_id: 'missing'}))
           .resolves.toMatchObject({
             success: false,
             error: 'Unknown skill: missing',
           });
       expect(skillMocks.refreshSkillRegistry).toHaveBeenCalledTimes(1);
     });

  it('rejects disabled skills', async () => {
    addSkill({disabled: true});

    await expect(executeTool('activate_skill', {skill_id: 'summary'}))
        .resolves.toMatchObject({
          success: false,
          error: 'Skill is disabled: summary',
        });
  });

  it('rejects host-unavailable skills after reading the active page host',
     async () => {
       addSkill({hosts: ['github.com']});
       vi.stubGlobal('chrome', {
         send: vi.fn((method: string, args: unknown[]) => {
           const [id] = args as [string];
           expect(method).toBe('getPageInfo');
           (window as unknown as {
             cr: {
               webUIResponse:
                   (id: string, ok: boolean, value: unknown) => void,
             },
           }).cr.webUIResponse(id, true, {
             url: 'https://example.com/app',
             title: 'Example',
           });
         }),
       });

       await expect(executeTool('activate_skill', {skill_id: 'summary'}))
           .resolves.toMatchObject({
             success: false,
             error: 'Skill is not available for this site: summary',
           });
     });

  it('rejects duplicate available skill ids as ambiguous', async () => {
    addSkill({hosts: ['*']});
    addSkill({name: 'site summary', hosts: ['example.com']});
    vi.stubGlobal('chrome', {
      send: vi.fn((method: string, args: unknown[]) => {
        const [id] = args as [string];
        expect(method).toBe('getPageInfo');
        (window as unknown as {
          cr: {
            webUIResponse:
                (id: string, ok: boolean, value: unknown) => void,
          },
        }).cr.webUIResponse(id, true, {
          url: 'https://example.com/app',
          title: 'Example',
        });
      }),
    });

    await expect(executeTool('activate_skill', {skill_id: 'summary'}))
        .resolves.toMatchObject({
          success: false,
          error: 'Skill id is ambiguous for this site: summary',
        });
  });

  it('does not let an unavailable duplicate hide an available duplicate',
     async () => {
       const unavailable = addSkill({hosts: ['github.com']});
       addSkill({name: 'site summary', hosts: ['example.com']});
       skillMocks.content.set(unavailable.id, {
         metadata: unavailable,
         instructions: '# Summary\n\nWrong site instructions.',
       });
       vi.stubGlobal('chrome', {
         send: vi.fn((method: string, args: unknown[]) => {
           const [id] = args as [string];
           expect(method).toBe('getPageInfo');
           (window as unknown as {
             cr: {
               webUIResponse:
                   (id: string, ok: boolean, value: unknown) => void,
             },
           }).cr.webUIResponse(id, true, {
             url: 'https://example.com/app',
             title: 'Example',
           });
         }),
       });

       await expect(executeTool('activate_skill', {skill_id: 'summary'}))
           .resolves.toMatchObject({
             success: false,
             error: 'Skill id is ambiguous for this site: summary',
           });
     });

  it('reports unavailable skill content', async () => {
    const skill = addSkill();
    skillMocks.content.delete(skill.id);

    await expect(executeTool('activate_skill', {skill_id: 'summary'}))
        .resolves.toMatchObject({
          success: false,
          error: 'Skill content unavailable: summary',
        });
  });
});
