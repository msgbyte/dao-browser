# Agent Auto Skill Activation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let Dao Agent expose a compact list of available skills to the model and let the model load full `SKILL.md` instructions through a controlled `activate_skill` tool.

**Architecture:** Keep the existing slash-command path intact. Add shared skill availability/catalog helpers in `skill_registry.ts`, inject a dynamic `<available_skills>` block into the chat system prompt, and expose `activate_skill` as a normal Dao tool that validates the requested skill before returning full instructions.

**Tech Stack:** Lit WebUI, TypeScript, pi-agent-core tool adapter, Vitest WebUI tests.

---

## Project Rules

- Do not edit `engine/`.
- Do not run Chromium build tools.
- This is a WebUI-only change; verification is focused Vitest plus Lit lint.
- Do not run `git add`, `git commit`, or other state-changing git commands unless the user explicitly authorizes that exact action.
- Plan commit steps are intentionally omitted because project AGENTS forbids state-changing git commands without explicit user authorization.

## File Structure

- Modify `src/dao/browser/ui/webui/resources/agent/skill_registry.ts`
  - Export host availability and prompt-catalog formatting helpers shared by chat prompt injection and `activate_skill`.
- Modify `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
  - Refresh the skill registry before each send, compute the current host, and include a compact `<available_skills>` block in `agent_.state.systemPrompt`.
- Modify `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
  - Add the `activate_skill` tool definition, base prompt guidance, and executeTool handling.
- Modify `src/dao/browser/ui/webui/resources/agent/tool_catalog.ts`
  - Add `activate_skill` to the Memory & Skills group.
- Modify `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`
  - Cover dynamic catalog injection and filtering.
- Create `src/dao/browser/ui/webui/resources/agent/__tests__/skill_registry.test.ts`
  - Cover pure helper behavior.
- Create `src/dao/browser/ui/webui/resources/agent/__tests__/agent_bridge_skill_activation.test.ts`
  - Cover `activate_skill` success and validation failures.
- Modify `src/dao/browser/ui/webui/resources/agent/__tests__/tool_catalog.test.ts`
  - Cover that `activate_skill` is enabled by default through the catalog.

## Task 1: Add Shared Skill Availability And Catalog Helpers

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/skill_registry.ts`
- Create: `src/dao/browser/ui/webui/resources/agent/__tests__/skill_registry.test.ts`

- [ ] **Step 1: Write failing helper tests**

Create `src/dao/browser/ui/webui/resources/agent/__tests__/skill_registry.test.ts`:

```ts
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
        id: 'quote-skill',
        name: 'Quote <Skill>',
        description: 'Use A & B "carefully"',
      }),
    ], 'example.com');

    expect(prompt).toContain('Quote &lt;Skill&gt;');
    expect(prompt).toContain('Use A &amp; B &quot;carefully&quot;');
  });

  it('returns an empty string when no skills are available', () => {
    expect(buildAvailableSkillsPrompt([
      skill({disabled: true}),
    ], 'example.com')).toBe('');
  });
});
```

- [ ] **Step 2: Run the helper test and verify it fails**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/skill_registry.test.ts
```

Expected: FAIL because `buildAvailableSkillsPrompt` and exported `isSkillAvailableForHost` do not exist.

- [ ] **Step 3: Implement shared helpers**

Modify `src/dao/browser/ui/webui/resources/agent/skill_registry.ts`:

```ts
const MAX_SKILL_CATALOG_ENTRIES = 40;

export function isSkillAvailableForHost(
    skill: SkillRegistryEntry, host: string): boolean {
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
}

function escapeSkillXml(value: string): string {
  return value
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
}

export function buildAvailableSkillsPrompt(
    skills: SkillRegistryEntry[], currentHost: string): string {
  const available = skills
      .filter(skill => isSkillAvailableForHost(skill, currentHost))
      .slice(0, MAX_SKILL_CATALOG_ENTRIES);
  if (available.length === 0) return '';

  const rows = available.map((skill) => {
    const requiresPageContent =
        skill.requiresPageContent ? 'true' : 'false';
    return [
      `<skill id="${escapeSkillXml(skill.id)}" source="${
          escapeSkillXml(skill.source)}" requires_page_content="${
          requiresPageContent}">`,
      `  <name>${escapeSkillXml(skill.name)}</name>`,
      `  <description>${escapeSkillXml(skill.description)}</description>`,
      `</skill>`,
    ].join('\n');
  });

  const omittedCount = skills
      .filter(skill => isSkillAvailableForHost(skill, currentHost)).length -
      available.length;
  const omittedLine = omittedCount > 0 ?
      `\n<!-- ${omittedCount} additional skills omitted from this turn. -->` :
      '';

  return [
    '## Available Skills',
    '',
    'You may activate one of these Dao Agent skills when the user request clearly matches its description. Call `activate_skill` before answering or acting. Do not invent skill ids, and do not activate a skill when none is relevant.',
    '',
    '<available_skills>',
    rows.join('\n'),
    `${omittedLine}\n</available_skills>`,
  ].join('\n');
}
```

Remove the old private `isSkillAvailableForHost` implementation from `skill_registry.ts` and update `getAvailableSkills()` to call the exported helper.

- [ ] **Step 4: Run the helper test and verify it passes**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/skill_registry.test.ts
```

Expected: PASS.

## Task 2: Inject Available Skills Into The Chat System Prompt

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/dao_chat_view.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`

- [ ] **Step 1: Write failing chat prompt tests**

In `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`, change the `skill_registry.js` mock to use mutable test data:

```ts
const skillMocks = vi.hoisted(() => ({
  skills: [] as Array<{
    id: string;
    name: string;
    description: string;
    source: string;
    hosts: string[];
    requiresPageContent: boolean;
    disabled: boolean;
  }>,
  initSkillRegistry: vi.fn(),
  loadSkillInstructions: vi.fn(async () => null),
  refreshSkillRegistry: vi.fn(async () => undefined),
  refreshSkillRegistryIfStale: vi.fn(async () => false),
}));

vi.mock('../skill_registry.js', async () => {
  const actual =
      await vi.importActual<typeof import('../skill_registry.js')>(
          '../skill_registry.js');
  return {
    ...actual,
    getAllSkills: () => skillMocks.skills,
    initSkillRegistry: (...args: unknown[]) =>
        skillMocks.initSkillRegistry(...args),
    loadSkillInstructions: (...args: unknown[]) =>
        skillMocks.loadSkillInstructions(...args),
    refreshSkillRegistry: (...args: unknown[]) =>
        skillMocks.refreshSkillRegistry(...args),
    refreshSkillRegistryIfStale: (...args: unknown[]) =>
        skillMocks.refreshSkillRegistryIfStale(...args),
  };
});
```

Add to the shared `beforeEach` in the test file:

```ts
skillMocks.skills = [];
skillMocks.initSkillRegistry.mockClear();
skillMocks.loadSkillInstructions.mockClear();
skillMocks.refreshSkillRegistry.mockClear();
skillMocks.refreshSkillRegistryIfStale.mockClear();
```

Add these tests near other `mountChatViewWithSend` tests:

```ts
it('injects enabled available skills into the next system prompt', async () => {
  skillMocks.skills = [{
    id: 'summary',
    name: 'summary',
    description: 'Summarize the current page',
    source: 'builtin',
    hosts: ['*'],
    requiresPageContent: true,
    disabled: false,
  }];
  pickerMocks.callNative.mockImplementation(async (method: string) => {
    if (method === 'getPageInfo') {
      return {url: 'https://example.com/article', title: 'Article'};
    }
    return {success: true};
  });
  const originalSend = vi.fn(async () => 'sent');
  const {view, iface} = await mountChatViewWithSend(originalSend);

  try {
    await iface.sendMessage('summarize this', []);
    const systemPrompt = (view as unknown as {
      agent_: {state: {systemPrompt: string}};
    }).agent_.state.systemPrompt;

    expect(systemPrompt).toContain('<available_skills>');
    expect(systemPrompt).toContain('id="summary"');
    expect(systemPrompt).toContain('Summarize the current page');
  } finally {
    clearTabWatchTimer(view);
  }
});

it('filters disabled and host-unavailable skills from the system prompt',
   async () => {
     skillMocks.skills = [
       {
         id: 'global-skill',
         name: 'global-skill',
         description: 'Works anywhere',
         source: 'user',
         hosts: ['*'],
         requiresPageContent: false,
         disabled: false,
       },
       {
         id: 'disabled-skill',
         name: 'disabled-skill',
         description: 'Disabled',
         source: 'user',
         hosts: ['*'],
         requiresPageContent: false,
         disabled: true,
       },
       {
         id: 'github-skill',
         name: 'github-skill',
         description: 'GitHub only',
         source: 'user',
         hosts: ['github.com'],
         requiresPageContent: false,
         disabled: false,
       },
     ];
     pickerMocks.callNative.mockImplementation(async (method: string) => {
       if (method === 'getPageInfo') {
         return {url: 'https://example.com/app', title: 'Example'};
       }
       return {success: true};
     });
     const originalSend = vi.fn(async () => 'sent');
     const {view, iface} = await mountChatViewWithSend(originalSend);

     try {
       await iface.sendMessage('help me', []);
       const systemPrompt = (view as unknown as {
         agent_: {state: {systemPrompt: string}};
       }).agent_.state.systemPrompt;

       expect(systemPrompt).toContain('id="global-skill"');
       expect(systemPrompt).not.toContain('id="disabled-skill"');
       expect(systemPrompt).not.toContain('id="github-skill"');
     } finally {
       clearTabWatchTimer(view);
     }
   });
```

- [ ] **Step 2: Run the chat prompt tests and verify they fail**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: FAIL because the prompt does not include a skill catalog yet.

- [ ] **Step 3: Implement prompt catalog state**

Modify the import in `dao_chat_view.ts`:

```ts
import {buildAvailableSkillsPrompt, getAllSkills, initSkillRegistry, loadSkillInstructions, refreshSkillRegistry, refreshSkillRegistryIfStale, type SkillRegistryEntry} from './skill_registry.js';
```

Add a private field near the other skill picker fields:

```ts
private skillCatalogPrompt_ = '';
```

Add this method near `buildSystemPrompt_()`:

```ts
private async refreshSkillCatalogPrompt_(): Promise<void> {
  await refreshSkillRegistryIfStale();
  let host = '';
  try {
    const info = await fetchCurrentPageInfo();
    if (info.url) {
      host = new URL(info.url).hostname;
    }
  } catch (_) {
    host = '';
  }
  this.skillCatalogPrompt_ =
      buildAvailableSkillsPrompt(getAllSkills(), host);
}
```

Update `buildSystemPrompt_()`:

```ts
private buildSystemPrompt_(): string {
  const skills = this.skillCatalogPrompt_.trim();
  return BASE_SYSTEM_PROMPT + (skills ? '\n\n' + skills : '') +
      '\n\n<soul>\n' + currentSoulContent + '\n</soul>';
}
```

Update the intercepted send path before `this.refreshSystemPrompt_()`:

```ts
await this.refreshSkillCatalogPrompt_();
this.refreshSystemPrompt_();
```

- [ ] **Step 4: Run the chat prompt tests and verify they pass**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: PASS.

## Task 3: Add The `activate_skill` Tool

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/tool_catalog.ts`
- Create: `src/dao/browser/ui/webui/resources/agent/__tests__/agent_bridge_skill_activation.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/tool_catalog.test.ts`

- [ ] **Step 1: Write failing activation tool tests**

Create `src/dao/browser/ui/webui/resources/agent/__tests__/agent_bridge_skill_activation.test.ts`:

```ts
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

const skillMocks = vi.hoisted(() => ({
  skills: [] as Array<{
    id: string;
    name: string;
    description: string;
    source: string;
    hosts: string[];
    requiresPageContent: boolean;
    disabled: boolean;
  }>,
  content: new Map<string, {
    metadata: {
      id: string;
      name: string;
      description: string;
      source: string;
      hosts: string[];
      requiresPageContent: boolean;
      disabled: boolean;
    };
    instructions: string;
  }>(),
  refreshSkillRegistry: vi.fn(async () => undefined),
}));

vi.mock('../skill_registry.js', async () => {
  return {
    getAllSkills: () => skillMocks.skills,
    isSkillAvailableForHost: (
        skill: {disabled: boolean; hosts: string[]}, host: string) => {
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
    saveUserSkill: vi.fn(async () => true),
  };
});

import {executeTool, tools} from '../agent_bridge.js';

function addSkill(overrides = {}) {
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
    vi.unstubAllGlobals();
  });

  it('is exposed as a model tool', () => {
    expect(tools.some(t => t.function.name === 'activate_skill')).toBe(true);
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
      instructions: expect.stringContaining('<activated_skill'),
    });
  });

  it('refreshes the registry once before reporting an unknown skill', async () => {
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
             cr: {webUIResponse: (id: string, ok: boolean, value: unknown) => void};
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
});
```

Add this assertion to `src/dao/browser/ui/webui/resources/agent/__tests__/tool_catalog.test.ts`:

```ts
it('includes activate_skill in the memory-skills group', () => {
  const group = TOOL_GROUPS.find(g => g.id === 'memory-skills');
  expect(group?.toolNames).toContain('activate_skill');
  expect(isToolEnabled('activate_skill')).toBe(true);
});
```

- [ ] **Step 2: Run activation tests and verify they fail**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/agent_bridge_skill_activation.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/tool_catalog.test.ts
```

Expected: FAIL because `activate_skill` does not exist.

- [ ] **Step 3: Add tool imports and definition**

Modify `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts` import:

```ts
import {getAllSkills, isSkillAvailableForHost, loadSkillInstructions, refreshSkillRegistry, saveUserSkill, type SkillContent, type SkillRegistryEntry} from './skill_registry.js';
```

Add this tool definition immediately after `save_skill`:

```ts
{
  type: 'function',
  function: {
    name: 'activate_skill',
    description:
        'Load full instructions for an available Dao Agent skill when the user request clearly matches the skill description. Call this before answering or acting with that skill.',
    parameters: {
      type: 'object',
      properties: {
        skill_id: {
          type: 'string',
          description: 'The exact id of a skill from <available_skills>',
        },
        reason: {
          type: 'string',
          description:
              'Brief reason this skill matches the current user request',
        },
      },
      required: ['skill_id'],
    },
  },
},
```

- [ ] **Step 4: Add activation helper implementation**

Add helper functions above `executeTool()`:

```ts
async function getCurrentHostForSkill(skill: SkillRegistryEntry):
    Promise<string> {
  if (!skill.hosts || skill.hosts.length === 0 ||
      skill.hosts.includes('*')) {
    return '';
  }
  try {
    const pageInfo = await callNative('getPageInfo') as
        {url?: string; title?: string};
    if (!pageInfo.url) return '';
    return new URL(pageInfo.url).hostname;
  } catch (_) {
    return '';
  }
}

function escapeSkillAttr(value: string): string {
  return value
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
}

function formatActivatedSkill(content: SkillContent): string {
  const id = escapeSkillAttr(content.metadata.id);
  const name = escapeSkillAttr(content.metadata.name);
  return `<activated_skill id="${id}" name="${name}">\n` +
      content.instructions + '\n</activated_skill>';
}

async function executeActivateSkill(
    args: Record<string, unknown>): Promise<Record<string, unknown>> {
  const skillId = getStringArg(args, 'skill_id').trim();
  if (!skillId) {
    return {success: false, error: 'Missing skill_id for activate_skill'};
  }

  let skill = getAllSkills().find(s => s.id === skillId);
  if (!skill) {
    await refreshSkillRegistry();
    skill = getAllSkills().find(s => s.id === skillId);
  }
  if (!skill) {
    return {success: false, error: 'Unknown skill: ' + skillId};
  }
  if (skill.disabled) {
    return {success: false, error: 'Skill is disabled: ' + skillId};
  }

  const host = await getCurrentHostForSkill(skill);
  if (!isSkillAvailableForHost(skill, host)) {
    return {
      success: false,
      error: 'Skill is not available for this site: ' + skillId,
    };
  }

  const content = await loadSkillInstructions(skillId);
  if (!content || !content.instructions) {
    return {
      success: false,
      error: 'Skill content unavailable: ' + skillId,
    };
  }

  return {
    success: true,
    skill_id: skillId,
    name: content.metadata.name,
    requires_page_content: content.metadata.requiresPageContent,
    reason: getStringArg(args, 'reason'),
    instructions: formatActivatedSkill(content),
  };
}
```

Add to `executeTool()`:

```ts
case 'activate_skill':
  return await executeActivateSkill(args);
```

- [ ] **Step 5: Add tool catalog entry**

Modify `src/dao/browser/ui/webui/resources/agent/tool_catalog.ts`:

```ts
toolNames: ['update_soul', 'save_memory', 'save_skill', 'activate_skill'],
```

- [ ] **Step 6: Run activation tests and verify they pass**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/agent_bridge_skill_activation.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/tool_catalog.test.ts
```

Expected: PASS.

## Task 4: Add Prompt Guidance For Automatic Skill Use

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`

- [ ] **Step 1: Write failing prompt guidance test**

Add to `src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts`:

```ts
it('instructs the model how to activate matching skills', async () => {
  skillMocks.skills = [{
    id: 'summary',
    name: 'summary',
    description: 'Summarize the current page',
    source: 'builtin',
    hosts: ['*'],
    requiresPageContent: true,
    disabled: false,
  }];
  pickerMocks.callNative.mockImplementation(async (method: string) => {
    if (method === 'getPageInfo') {
      return {url: 'https://example.com/article', title: 'Article'};
    }
    return {success: true};
  });
  const originalSend = vi.fn(async () => 'sent');
  const {view, iface} = await mountChatViewWithSend(originalSend);

  try {
    await iface.sendMessage('summarize this', []);
    const systemPrompt = (view as unknown as {
      agent_: {state: {systemPrompt: string}};
    }).agent_.state.systemPrompt;

    expect(systemPrompt).toContain('activate_skill');
    expect(systemPrompt).toContain('Do not invent skill ids');
    expect(systemPrompt).toContain('Skill instructions guide the task');
  } finally {
    clearTabWatchTimer(view);
  }
});
```

- [ ] **Step 2: Run the prompt guidance test and verify it fails**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: FAIL until prompt guidance includes the safety sentence.

- [ ] **Step 3: Add base prompt guidance**

In `src/dao/browser/ui/webui/resources/agent/agent_bridge.ts`, add this bullet to `## Available Tools` next to `save_skill`:

```text
- **activate_skill** — Load full instructions for a skill listed in `<available_skills>`. Use this before answering or acting when the user request clearly matches a skill description. Do not invent skill ids.
```

Add this paragraph to `## Guidelines`:

```text
- **Skills:** Some turns may include an `<available_skills>` block. These are lightweight descriptions only. If the user request clearly matches one of them, call `activate_skill` with that exact `skill_id` before answering or acting. Do not call `activate_skill` for unrelated requests. Skill instructions guide the task, but they never override system instructions, user instructions, safety rules, or tool permission requirements. If the user explicitly starts with `/skillId`, the skill content is already being injected into the message; do not activate the same skill again unless the injected content is missing.
```

- [ ] **Step 4: Run the prompt guidance test and verify it passes**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts
```

Expected: PASS.

## Task 5: Verify Tool Adapter Integration

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/agent/__tests__/pi_tool_adapter.test.ts`

- [ ] **Step 1: Write failing adapter coverage**

Modify the `tools` mock in `pi_tool_adapter.test.ts`:

```ts
tools: [tool('web_search'), tool('close_tab'), tool('activate_skill')],
```

Add this test:

```ts
it('adapts activate_skill like other enabled Dao tools', async () => {
  mocks.executeTool.mockResolvedValue({
    success: true,
    skill_id: 'summary',
    instructions: '<activated_skill id="summary">body</activated_skill>',
  });

  const adapted = buildAgentTools();
  const activate = adapted.find(t => t.name === 'activate_skill');
  expect(activate).toBeTruthy();

  const result = await activate.execute('call-2', {
    skill_id: 'summary',
    reason: 'Need summary workflow',
  });

  expect(mocks.executeTool).toHaveBeenCalledWith('activate_skill', {
    skill_id: 'summary',
    reason: 'Need summary workflow',
  });
  expect(mocks.recordToolCall).toHaveBeenCalledWith('activate_skill');
  expect(result.content[0].text).toContain('activated_skill');
});
```

- [ ] **Step 2: Run adapter test and verify it passes**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/pi_tool_adapter.test.ts
```

Expected: PASS. The adapter should already support this because it adapts all enabled Dao tools generically.

## Task 6: Focused Verification

**Files:**
- All modified files from Tasks 1-5.

- [ ] **Step 1: Run focused WebUI tests**

Run:

```bash
npm run test:webui -- src/dao/browser/ui/webui/resources/agent/__tests__/skill_registry.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/dao_chat_view.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/agent_bridge_skill_activation.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/tool_catalog.test.ts src/dao/browser/ui/webui/resources/agent/__tests__/pi_tool_adapter.test.ts
```

Expected: PASS.

- [ ] **Step 2: Run Lit reactive field lint**

Run:

```bash
npm run lint:lit
```

Expected: PASS.

- [ ] **Step 3: Run full WebUI test suite if focused tests pass**

Run:

```bash
npm run test:webui
```

Expected: PASS.

## Self-Review

- Spec coverage: The plan covers catalog exposure, model-triggered activation, slash compatibility, host/disabled validation, tool catalog registration, and focused tests.
- Placeholder scan: The plan has no `TBD`, no unresolved implementation references, and no generic “add tests” steps without test content.
- Type consistency: `SkillRegistryEntry`, `SkillContent`, `buildAvailableSkillsPrompt`, `isSkillAvailableForHost`, and `activate_skill` names are consistent across tasks.
- Scope check: This remains one WebUI feature. Vector search, auto-generated skills, and advanced learning are intentionally left out of first implementation.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-25-agent-auto-skill-activation.md`.

Two execution options:

1. **Subagent-Driven (recommended)** - Dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints.

This repository forbids creating worktrees and forbids state-changing git commands without explicit user authorization, so either execution path must stay in the primary checkout and skip git staging/commits unless the user explicitly authorizes those exact actions.
