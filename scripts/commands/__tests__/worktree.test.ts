import {
  existsSync,
  readlinkSync,
  mkdirSync,
  readFileSync,
  symlinkSync,
  writeFileSync,
} from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

import {
  archiveDaoWorktreeEngines,
  attachEngineToWorktree,
  archiveStaleDaoWorktreeEngines,
  computeWarmCacheKey,
  createDaoWorktree,
  getEngineStorePaths,
  setupDaoWorktree,
  sanitizeWorktreeId,
  type CommandRunner,
  type CommandOutputRunner,
  type EngineCloner,
} from '../worktree.js';

function makeProjectRoot(): string {
  const root = path.join(
      os.tmpdir(),
      `dao-worktree-test-${process.pid}-${Math.random().toString(16).slice(2)}`);
  mkdirSync(path.join(root, 'configs'), {recursive: true});
  mkdirSync(path.join(root, 'src/patches/chrome'), {recursive: true});
  mkdirSync(path.join(root, 'src/dao/browser'), {recursive: true});
  mkdirSync(path.join(root, 'branding'), {recursive: true});
  writeFileSync(path.join(root, 'dao.json'), JSON.stringify({
    version: {version: '147.0.7727.135', display: '1.0.61'},
  }));
  writeFileSync(path.join(root, 'configs/common.gn'), 'is_debug = false\n');
  writeFileSync(path.join(root, 'src/patches/chrome/example.patch'), 'patch\n');
  writeFileSync(path.join(root, 'src/dao/browser/example.cc'), 'dao\n');
  writeFileSync(path.join(root, 'branding/BRANDING'), 'PRODUCT_FULLNAME=Dao\n');
  return root;
}

describe('worktree engine helpers', () => {
  it('sanitizes branch-like names into stable engine ids', () => {
    expect(sanitizeWorktreeId('feature/little-dao')).toBe('feature-little-dao');
    expect(sanitizeWorktreeId(' fix: fullscreen ')).toBe('fix-fullscreen');
    expect(sanitizeWorktreeId('../main')).toBe('main');
  });

  it('computes warm cache keys from Dao-owned build inputs', () => {
    const root = makeProjectRoot();

    const before = computeWarmCacheKey(root, 'dao-debug');
    writeFileSync(path.join(root, 'src/dao/browser/example.cc'), 'dao change\n');
    const after = computeWarmCacheKey(root, 'dao-debug');

    expect(before).toMatch(/^147\.0\.7727\.135-dao-debug-[a-f0-9]{16}$/);
    expect(after).toMatch(/^147\.0\.7727\.135-dao-debug-[a-f0-9]{16}$/);
    expect(after).not.toBe(before);
  });

  it('attaches a private cached engine to a git worktree through an engine symlink', () => {
    const root = makeProjectRoot();
    const paths = getEngineStorePaths(root);
    const worktreePath = path.join(root, 'dao-browser-feature-a');
    const enginePath = path.join(paths.worktreeEnginesDir, 'feature-a', 'engine');
    mkdirSync(enginePath, {recursive: true});
    mkdirSync(worktreePath, {recursive: true});

    const linkPath = attachEngineToWorktree({
      worktreePath,
      enginePath,
    });

    expect(linkPath).toBe(path.join(worktreePath, 'engine'));
    expect(path.resolve(worktreePath, readlinkSync(linkPath))).toBe(enginePath);
  });

  it('creates a git worktree and clones a warm engine cache into its private engine slot', () => {
    const root = makeProjectRoot();
    const paths = getEngineStorePaths(root);
    const cacheKey = computeWarmCacheKey(root, 'dao-debug');
    const warmEngine = path.join(paths.warmCacheDir, cacheKey, 'engine');
    mkdirSync(warmEngine, {recursive: true});
    writeFileSync(path.join(warmEngine, 'sentinel.txt'), 'cache\n');

    const commands: Array<{cmd: string; args: string[]; cwd: string}> = [];
    const runner: CommandRunner = (cmd, args, opts) => {
      commands.push({cmd, args, cwd: opts.cwd});
      mkdirSync(args[4], {recursive: true});
    };
    const clones: Array<{source: string; dest: string}> = [];
    const cloner: EngineCloner = (source, dest) => {
      clones.push({source, dest});
      mkdirSync(dest, {recursive: true});
      symlinkSync(path.join(source, 'sentinel.txt'), path.join(dest, 'sentinel.txt'));
    };

    const result = createDaoWorktree({
      rootDir: root,
      name: 'feature/a',
      branch: 'feature/a',
      baseRef: 'main',
      worktreePath: path.join(root, 'dao-browser-feature-a'),
      runner,
      cloneEngine: cloner,
    });

    expect(commands).toEqual([{
      cmd: 'git',
      args: [
        'worktree',
        'add',
        '-b',
        'feature/a',
        path.join(root, 'dao-browser-feature-a'),
        'main',
      ],
      cwd: root,
    }]);
    expect(clones).toEqual([{
      source: warmEngine,
      dest: path.join(paths.worktreeEnginesDir, 'feature-a', 'engine'),
    }]);
    expect(result.engineId).toBe('feature-a');
    expect(result.cacheKey).toBe(cacheKey);
    expect(existsSync(path.join(result.worktreePath, 'engine'))).toBe(true);

    const manifest = JSON.parse(readFileSync(
        path.join(paths.worktreeEnginesDir, 'feature-a', 'manifest.json'),
        'utf-8'));
    expect(manifest).toMatchObject({
      id: 'feature-a',
      branch: 'feature/a',
      baseRef: 'main',
      cacheKey,
    });
  });

  it('sets up an externally-created git worktree from the primary checkout engine cache', () => {
    const primaryRoot = makeProjectRoot();
    const workerRoot = makeProjectRoot();
    const paths = getEngineStorePaths(primaryRoot);
    const cacheKey = computeWarmCacheKey(primaryRoot, 'dao-debug');
    const warmEngine = path.join(paths.warmCacheDir, cacheKey, 'engine');
    mkdirSync(warmEngine, {recursive: true});
    writeFileSync(path.join(warmEngine, 'sentinel.txt'), 'cache\n');

    const clones: Array<{source: string; dest: string}> = [];
    const cloner: EngineCloner = (source, dest) => {
      clones.push({source, dest});
      mkdirSync(dest, {recursive: true});
      symlinkSync(path.join(source, 'sentinel.txt'), path.join(dest, 'sentinel.txt'));
    };
    const outputRunner: CommandOutputRunner = (cmd, args, opts) => {
      if (cmd === 'git' && args.join(' ') === 'worktree list --porcelain') {
        expect(opts.cwd).toBe(workerRoot);
        return [
          `worktree ${primaryRoot}`,
          'HEAD abc123',
          'branch refs/heads/main',
          '',
          `worktree ${workerRoot}`,
          'HEAD def456',
          'branch refs/heads/feature/external-worker',
          '',
        ].join('\n');
      }
      if (cmd === 'git' && args.join(' ') === 'rev-parse --abbrev-ref HEAD') {
        expect(opts.cwd).toBe(workerRoot);
        return 'feature/external-worker\n';
      }
      throw new Error(`unexpected command: ${cmd} ${args.join(' ')}`);
    };

    const result = setupDaoWorktree({
      rootDir: workerRoot,
      outputRunner,
      cloneEngine: cloner,
    });

    expect(result.engineId).toBe('feature-external-worker');
    expect(result.primaryRootDir).toBe(primaryRoot);
    expect(result.cacheKey).toBe(cacheKey);
    expect(clones).toEqual([{
      source: warmEngine,
      dest: path.join(paths.worktreeEnginesDir, 'feature-external-worker', 'engine'),
    }]);
    expect(path.resolve(workerRoot, readlinkSync(path.join(workerRoot, 'engine'))))
        .toBe(result.enginePath);
  });

  it('finds stale private worktree engines without deleting them by default', () => {
    const root = makeProjectRoot();
    const activeRoot = path.join(root, 'dao-browser-active');
    const staleRoot = path.join(root, 'dao-browser-stale');
    const paths = getEngineStorePaths(root);
    const activeEngineRoot = path.join(paths.worktreeEnginesDir, 'feature-active');
    const staleEngineRoot = path.join(paths.worktreeEnginesDir, 'feature-stale');

    mkdirSync(path.join(activeEngineRoot, 'engine'), {recursive: true});
    mkdirSync(path.join(staleEngineRoot, 'engine'), {recursive: true});
    writeFileSync(path.join(activeEngineRoot, 'manifest.json'), JSON.stringify({
      id: 'feature-active',
      worktreePath: activeRoot,
      enginePath: path.join(activeEngineRoot, 'engine'),
    }));
    writeFileSync(path.join(staleEngineRoot, 'manifest.json'), JSON.stringify({
      id: 'feature-stale',
      worktreePath: staleRoot,
      enginePath: path.join(staleEngineRoot, 'engine'),
    }));

    const outputRunner: CommandOutputRunner = (cmd, args, opts) => {
      expect(cmd).toBe('git');
      expect(args).toEqual(['worktree', 'list', '--porcelain']);
      expect(opts.cwd).toBe(root);
      return [
        `worktree ${root}`,
        'HEAD abc123',
        'branch refs/heads/main',
        '',
        `worktree ${activeRoot}`,
        'HEAD def456',
        'branch refs/heads/feature/active',
        '',
      ].join('\n');
    };

    const result = archiveStaleDaoWorktreeEngines({
      rootDir: root,
      outputRunner,
    });

    expect(result.stale.map((entry) => entry.id)).toEqual(['feature-stale']);
    expect(result.deleted).toEqual([]);
    expect(existsSync(activeEngineRoot)).toBe(true);
    expect(existsSync(staleEngineRoot)).toBe(true);
  });

  it('deletes only stale private worktree engines when requested', () => {
    const root = makeProjectRoot();
    const activeRoot = path.join(root, 'dao-browser-active');
    const staleRoot = path.join(root, 'dao-browser-stale');
    const paths = getEngineStorePaths(root);
    const activeEngineRoot = path.join(paths.worktreeEnginesDir, 'feature-active');
    const staleEngineRoot = path.join(paths.worktreeEnginesDir, 'feature-stale');

    mkdirSync(path.join(activeEngineRoot, 'engine'), {recursive: true});
    mkdirSync(path.join(staleEngineRoot, 'engine'), {recursive: true});
    writeFileSync(path.join(activeEngineRoot, 'manifest.json'), JSON.stringify({
      id: 'feature-active',
      worktreePath: activeRoot,
      enginePath: path.join(activeEngineRoot, 'engine'),
    }));
    writeFileSync(path.join(staleEngineRoot, 'manifest.json'), JSON.stringify({
      id: 'feature-stale',
      worktreePath: staleRoot,
      enginePath: path.join(staleEngineRoot, 'engine'),
    }));

    const outputRunner: CommandOutputRunner = () => [
      `worktree ${root}`,
      'HEAD abc123',
      'branch refs/heads/main',
      '',
      `worktree ${activeRoot}`,
      'HEAD def456',
      'branch refs/heads/feature/active',
      '',
    ].join('\n');

    const result = archiveStaleDaoWorktreeEngines({
      rootDir: root,
      deleteStale: true,
      outputRunner,
    });

    expect(result.deleted.map((entry) => entry.id)).toEqual(['feature-stale']);
    expect(existsSync(activeEngineRoot)).toBe(true);
    expect(existsSync(staleEngineRoot)).toBe(false);
  });

  it('deletes the current private engine when run from a linked worktree', () => {
    const primaryRoot = makeProjectRoot();
    const workerRoot = makeProjectRoot();
    const paths = getEngineStorePaths(primaryRoot);
    const engineRoot = path.join(paths.worktreeEnginesDir, 'feature-current');
    const enginePath = path.join(engineRoot, 'engine');

    mkdirSync(enginePath, {recursive: true});
    writeFileSync(path.join(engineRoot, 'manifest.json'), JSON.stringify({
      id: 'feature-current',
      worktreePath: workerRoot,
      enginePath,
    }));
    symlinkSync(
        path.relative(workerRoot, enginePath),
        path.join(workerRoot, 'engine'),
        'dir',
    );

    const outputRunner: CommandOutputRunner = (cmd, args, opts) => {
      expect(cmd).toBe('git');
      expect(args).toEqual(['worktree', 'list', '--porcelain']);
      expect(opts.cwd).toBe(workerRoot);
      return [
        `worktree ${primaryRoot}`,
        'HEAD abc123',
        'branch refs/heads/main',
        '',
        `worktree ${workerRoot}`,
        'HEAD def456',
        'branch refs/heads/feature/current',
        '',
      ].join('\n');
    };

    const result = archiveDaoWorktreeEngines({
      rootDir: workerRoot,
      outputRunner,
    });

    expect(result.mode).toBe('current-worktree');
    expect(result.deleted.map((entry) => entry.id)).toEqual(['feature-current']);
    expect(existsSync(engineRoot)).toBe(false);
    expect(existsSync(path.join(workerRoot, 'engine'))).toBe(false);
  });

  it('keeps primary checkout archive runs as dry runs by default', () => {
    const root = makeProjectRoot();
    const staleRoot = path.join(root, 'dao-browser-stale');
    const paths = getEngineStorePaths(root);
    const staleEngineRoot = path.join(paths.worktreeEnginesDir, 'feature-stale');

    mkdirSync(path.join(staleEngineRoot, 'engine'), {recursive: true});
    writeFileSync(path.join(staleEngineRoot, 'manifest.json'), JSON.stringify({
      id: 'feature-stale',
      worktreePath: staleRoot,
      enginePath: path.join(staleEngineRoot, 'engine'),
    }));

    const outputRunner: CommandOutputRunner = () => [
      `worktree ${root}`,
      'HEAD abc123',
      'branch refs/heads/main',
      '',
    ].join('\n');

    const result = archiveDaoWorktreeEngines({
      rootDir: root,
      outputRunner,
    });

    expect(result.mode).toBe('primary-dry-run');
    expect(result.stale.map((entry) => entry.id)).toEqual(['feature-stale']);
    expect(result.deleted).toEqual([]);
    expect(existsSync(staleEngineRoot)).toBe(true);
  });
});
