import {
  existsSync,
  mkdtempSync,
  mkdirSync,
  readFileSync,
  writeFileSync,
} from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

import {
  assertRequiredEntitlementsPresent,
  preserveDsymsForLocalDebugging,
} from '../package.js';

describe('package scripts', () => {
  it('exposes the R2 cleanup command', () => {
    const packageJsonPath = path.join(process.cwd(), 'package.json');
    const packageJson = JSON.parse(readFileSync(packageJsonPath, 'utf-8')) as {
      scripts?: Record<string, string>;
    };

    expect(packageJson.scripts?.['upload:cleanup'])
        .toBe('tsx scripts/cli.ts upload cleanup');
  });

  it('exposes the agent worktree setup command', () => {
    const packageJsonPath = path.join(process.cwd(), 'package.json');
    const packageJson = JSON.parse(readFileSync(packageJsonPath, 'utf-8')) as {
      scripts?: Record<string, string>;
    };

    expect(packageJson.scripts?.['worktree:setup'])
        .toBe('tsx scripts/cli.ts worktree setup');
    expect(packageJson.scripts?.['setup:worktree'])
        .toBe('npm install && npm run worktree:setup && npm run import');
    expect(packageJson.scripts?.['setup-worktree']).toBeUndefined();
  });

  it('exposes the agent worktree archive command', () => {
    const packageJsonPath = path.join(process.cwd(), 'package.json');
    const packageJson = JSON.parse(readFileSync(packageJsonPath, 'utf-8')) as {
      scripts?: Record<string, string>;
    };

    expect(packageJson.scripts?.['worktree:archive'])
        .toBe('tsx scripts/cli.ts worktree archive');
    expect(packageJson.scripts?.['archive:worktree'])
        .toBe('npm run worktree:archive --');
  });

  it('ignores both engine directories and worktree engine symlinks', () => {
    const gitignore = readFileSync(path.join(process.cwd(), '.gitignore'), 'utf-8')
        .split(/\r?\n/);

    expect(gitignore).toContain('engine');
    expect(gitignore).toContain('engine/');
  });

  it('rejects a signed helper with no embedded entitlements', () => {
    expect(() => assertRequiredEntitlementsPresent(
        'Dao Helper (Renderer).app',
        '',
        ['com.apple.security.cs.allow-jit'],
    )).toThrow(/missing signed entitlements/i);
  });

  it('accepts a signed helper with every required entitlement', () => {
    const entitlements = `<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
  <key>com.apple.security.cs.allow-jit</key>
  <true/>
  <key>com.apple.security.cs.allow-unsigned-executable-memory</key>
  <true/>
</dict>
</plist>`;

    expect(() => assertRequiredEntitlementsPresent(
        'Dao Helper (Renderer).app',
        entitlements,
        [
          'com.apple.security.cs.allow-jit',
          'com.apple.security.cs.allow-unsigned-executable-memory',
        ],
    )).not.toThrow();
  });

  it('preserves release dSYMs in the requested temporary directory', () => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-package-test-'));
    const outDir = path.join(root, 'out', 'dao');
    const tmpDir = path.join(root, 'tmp');
    const bundleDsym = path.join(
        outDir,
        'Dao Framework.dSYM',
        'Contents',
        'Resources',
        'DWARF',
    );
    mkdirSync(bundleDsym, {recursive: true});
    mkdirSync(tmpDir, {recursive: true});
    writeFileSync(path.join(bundleDsym, 'Dao Framework'), 'symbols');
    writeFileSync(path.join(outDir, 'Dao.dSYM.tar.bz2'), 'archive');

    const copied = preserveDsymsForLocalDebugging(
        outDir,
        'dao-browser-1.0.53-mac-arm64',
        tmpDir,
    );

    const destDir = path.join(tmpDir, 'dao-browser-1.0.53-mac-arm64-dsyms');
    expect(copied).toEqual([
      path.join(destDir, 'Dao Framework.dSYM'),
      path.join(destDir, 'Dao.dSYM.tar.bz2'),
    ]);
    expect(existsSync(path.join(
        destDir,
        'Dao Framework.dSYM',
        'Contents',
        'Resources',
        'DWARF',
        'Dao Framework',
    ))).toBe(true);
    expect(readFileSync(path.join(destDir, 'Dao.dSYM.tar.bz2'), 'utf-8'))
        .toBe('archive');
  });
});
