import { readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, it } from 'vitest';

const root = path.resolve(__dirname, '..', '..');

function read(relative: string): string {
  return readFileSync(path.join(root, relative), 'utf8');
}

describe('agent plugins', () => {
  it('ships the same entry skill Dao installs for local agents', () => {
    const packages = readdirSync(path.join(root, 'agent-plugins'), { withFileTypes: true })
      .filter(entry => entry.isDirectory() && !entry.name.startsWith('.'))
      .map(entry => entry.name)
      .sort();
    expect(packages).toEqual(['agents', 'claude-code']);

    for (const skill of ['dao-browser', 'dao-lite']) {
      const entry = read(`src/dao/browser/cli/${skill.replace('-', '_')}_entry_skill.md`);
      for (const host of packages) {
        expect(read(`agent-plugins/${host}/skills/${skill}/SKILL.md`), `${host}/${skill}`)
          .toBe(entry);
      }
    }
  });

  it('shares a portable manifest with OpenAI metadata under its extension', () => {
    const plugin = JSON.parse(read('agent-plugins/agents/plugin.json'));
    expect(plugin.$schema).toBe('https://agent-plugins.org/schemas/1.0.0/plugin.schema.json');
    expect(plugin.name).toBe('dao-browser');
    expect(plugin.skills).toBeUndefined();
    expect(plugin.interface).toBeUndefined();
    expect(plugin.extensions['com.openai'].interface).toMatchObject({
      displayName: 'Dao Browser',
      capabilities: ['Interactive', 'Read', 'Write'],
    });
  });

  it('lists the Claude Code plugin in the marketplace manifest', () => {
    const marketplace = JSON.parse(
      read('agent-plugins/.claude-plugin/marketplace.json'));
    const plugin = JSON.parse(
      read('agent-plugins/claude-code/.claude-plugin/plugin.json'));
    expect(marketplace.plugins).toEqual([
      expect.objectContaining({ name: plugin.name, source: './claude-code' }),
    ]);
  });
});
