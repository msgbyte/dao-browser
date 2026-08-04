import {mkdtempSync, mkdirSync, rmSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

import {afterEach, describe, expect, it} from 'vitest';

const temporaryRoots: string[] = [];
const checkerPath = path.join(process.cwd(), 'scripts/docs-check.ts');

function createFixture(files: Record<string, string>): string {
  const root = mkdtempSync(path.join(os.tmpdir(), 'dao-docs-check-'));
  temporaryRoots.push(root);
  for (const [relativePath, content] of Object.entries(files)) {
    const absolutePath = path.join(root, relativePath);
    mkdirSync(path.dirname(absolutePath), {recursive: true});
    writeFileSync(absolutePath, content);
  }
  return root;
}

function runChecker(root: string) {
  return spawnSync(
      'npx',
      ['tsx', checkerPath, '--root', root],
      {encoding: 'utf-8'},
  );
}

afterEach(() => {
  for (const root of temporaryRoots.splice(0)) {
    rmSync(root, {recursive: true, force: true});
  }
});

describe('documentation checker', () => {
  it('reports broken commands, links, private references, and stale facts', () => {
    const root = createFixture({
      'package.json': JSON.stringify({scripts: {test: 'vitest run'}}),
      'README.md': [
        'Run `npm run missing`.',
        'Open `chrome://dao-agent`.',
        'See `MEMORY.md` and [missing](docs/missing.md).',
      ].join('\n'),
      'docs/features.md': [
        '# Features',
        '## Chromium Core Integration Patches (184 total)',
        '- **Version**: 1.0.79',
      ].join('\n'),
    });

    const result = runChecker(root);
    const output = `${result.stdout}\n${result.stderr}`;

    expect(result.status).toBe(1);
    expect(output).toContain('unknown npm script "missing"');
    expect(output).toContain('use dao:// URLs');
    expect(output).toContain('must not reference local MEMORY.md');
    expect(output).toContain('broken local link "docs/missing.md"');
    expect(output).toContain('must not hardcode volatile project facts');
  });

  it('accepts documentation whose commands and local links resolve', () => {
    const root = createFixture({
      'package.json': JSON.stringify({scripts: {test: 'vitest run'}}),
      'README.md': 'Run `npm run test` and read [the guide](docs/guide.md).',
      'docs/guide.md': 'Open `dao://agent`.',
      'docs/features.md': '# Features\n\nVersion and patch inventory live in dao.json and src/patches/.',
      'docs/superpowers/archive.md': [
        'Historical plans are not part of the maintained documentation set.',
        'Example callback: [](bool ok) {}',
        'Old command: `npm run removed-command`.',
      ].join('\n'),
    });

    const result = runChecker(root);

    expect(result.status).toBe(0);
    expect(result.stdout).toContain('documentation checks passed');
  });

  it('rejects volatile Chromium baselines and patch totals', () => {
    const root = createFixture({
      'package.json': JSON.stringify({scripts: {test: 'vitest run'}}),
      'README.md': 'The export command rewrites all ~166 patches.',
      'docs/features.md': [
        '# Features',
        'Features added on top of Chromium 149.0.7827.201.',
      ].join('\n'),
    });

    const result = runChecker(root);
    const output = `${result.stdout}\n${result.stderr}`;

    expect(result.status).toBe(1);
    expect(output).toContain(
        'README.md: must not hardcode volatile patch totals');
    expect(output).toContain(
        'docs/features.md: must not hardcode the Chromium baseline');
  });
});
