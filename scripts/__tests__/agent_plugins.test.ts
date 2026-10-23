import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, it } from 'vitest';

const root = path.resolve(__dirname, '..', '..');

function read(relative: string): string {
  return readFileSync(path.join(root, relative), 'utf8');
}

describe('agent plugins', () => {
  it('ships the same entry skill Dao installs for local agents', () => {
    for (const skill of ['dao-browser', 'dao-lite']) {
      const entry = read(`src/dao/browser/cli/${skill.replace('-', '_')}_entry_skill.md`);
      for (const host of ['claude-code', 'codex', 'cursor']) {
        expect(read(`agent-plugins/${host}/skills/${skill}/SKILL.md`), `${host}/${skill}`)
          .toBe(entry);
      }
    }
  });

  it('points every host manifest at the packaged skill', () => {
    for (const manifest of [
      'agent-plugins/codex/.codex-plugin/plugin.json',
      'agent-plugins/cursor/.cursor-plugin/plugin.json',
    ]) {
      const plugin = JSON.parse(read(manifest));
      expect(plugin.name, manifest).toBe('dao-browser');
      expect(plugin.skills, manifest).toBe('./skills/');
    }
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
