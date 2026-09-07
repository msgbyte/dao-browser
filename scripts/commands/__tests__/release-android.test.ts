// @vitest-environment node
import {spawnSync} from 'node:child_process';
import {mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {afterEach, beforeEach, describe, expect, it} from 'vitest';
import {resolveAndroidVersion, runAndroidRelease, validateAndroidVersion} from '../release-android.js';

function runCli(...args: string[]) {
  return spawnSync(process.execPath,
    ['--import', 'tsx', 'scripts/cli.ts', 'release-android', ...args],
    {cwd: process.cwd(), encoding: 'utf8', env: {...process.env, CI: 'true'}});
}

describe('Android release CLI', () => {
  it('offers patch and code increments when input is empty', async () => {
    const prompts: string[] = [];
    expect(await resolveAndroidVersion({version: '0.1.9', versionCode: 4}, {}, async (message) => {
      prompts.push(message);
      return '';
    })).toEqual({version: '0.1.10', versionCode: 5});
    expect(prompts).toEqual(['Release version (0.1.10): ', 'Version code (5): ']);
  });

  it('rejects invalid or non-increasing versions', () => {
    for (const next of [
      {version: '../0.1.1', versionCode: 2},
      {version: '0.1.1', versionCode: 1},
      {version: '0.0.9', versionCode: 2},
      {version: '0.1.1', versionCode: 2100000001},
    ]) expect(() => validateAndroidVersion(next, {version: '0.1.0', versionCode: 1})).toThrow();
  });

  it('previews an explicit version without credentials or publication', () => {
    const result = runCli('--version', '0.1.1', '--version-code', '2', '--dry-run');
    expect(result.status, result.stderr).toBe(0);
    expect(result.stdout).toContain('android-v0.1.1');
    expect(result.stdout).toContain('versionCode: 1 -> 2');
    expect(result.stdout).toContain('chore(android): release 0.1.1');
    expect(result.stdout).toContain('git push --atomic');
  });

  it('requires both version arguments without a terminal', () => {
    const result = runCli('--dry-run');
    expect(result.status).not.toBe(0);
    expect(result.stderr).toContain('--version and --version-code');
  });

});

describe('Android release Git transaction', () => {
  let directory: string;
  let checkout: string;
  let remote: string;
  const gradlePath = 'android/app/build.gradle.kts';
  const original = 'android {\n  defaultConfig {\n    versionCode = 1\n    versionName = "0.1.0"\n  }\n}\n';
  const options = {version: '0.1.1', versionCode: '2'};
  const git = (cwd: string, ...args: string[]) => {
    const result = spawnSync('git', args, {cwd, encoding: 'utf8'});
    if (result.status !== 0) throw new Error(result.stderr);
    return result.stdout.trim();
  };
  const gradle = () => readFileSync(path.join(checkout, gradlePath), 'utf8');

  beforeEach(() => {
    directory = mkdtempSync(path.join(tmpdir(), 'dao-android-git-'));
    checkout = path.join(directory, 'checkout');
    remote = path.join(directory, 'origin.git');
    mkdirSync(checkout);
    git(directory, 'init', '--bare', remote);
    git(checkout, 'init', '-b', 'main');
    git(checkout, 'config', 'user.name', 'Release Test');
    git(checkout, 'config', 'user.email', 'release-test@example.invalid');
    git(checkout, 'config', 'commit.gpgsign', 'false');
    git(checkout, 'config', 'tag.gpgsign', 'false');
    git(checkout, 'config', 'core.hooksPath', path.join(checkout, '.git/hooks'));
    mkdirSync(path.join(checkout, 'android/app'), {recursive: true});
    writeFileSync(path.join(checkout, gradlePath), original);
    writeFileSync(path.join(checkout, 'other.txt'), 'existing source\n');
    git(checkout, 'add', '.');
    git(checkout, 'commit', '-m', 'chore: initial source');
    git(checkout, 'remote', 'add', 'origin', remote);
    git(checkout, 'push', 'origin', 'main');
  });
  afterEach(() => rmSync(directory, {recursive: true, force: true}));

  it('commits only the version file, then pushes main and its release tag at the new commit', async () => {
    // A release can include already committed local work without a separate push.
    writeFileSync(path.join(checkout, 'other.txt'), 'committed feature\n');
    git(checkout, 'commit', '-am', 'feat(android): add feature');
    const before = git(checkout, 'rev-parse', 'HEAD');
    await runAndroidRelease(options, checkout);
    const head = git(checkout, 'rev-parse', 'HEAD');
    expect(head).not.toBe(before);
    expect(git(checkout, 'log', '-1', '--format=%s')).toBe('chore(android): release 0.1.1');
    expect(git(checkout, 'diff-tree', '--no-commit-id', '--name-only', '-r', 'HEAD')).toBe(gradlePath);
    expect(gradle()).toBe(original.replace('= 1', '= 2').replace('0.1.0', '0.1.1'));
    expect(git(checkout, 'status', '--porcelain')).toBe('');
    expect(git(checkout, 'rev-parse', 'android-v0.1.1^{commit}')).toBe(head);
    expect(git(remote, 'rev-parse', 'main')).toBe(head);
    expect(git(remote, 'rev-parse', 'android-v0.1.1^{commit}')).toBe(head);
  });

  it('keeps dry-run offline and leaves source and history unchanged', async () => {
    const before = git(checkout, 'rev-parse', 'HEAD');
    git(checkout, 'remote', 'remove', 'origin');
    await runAndroidRelease({...options, dryRun: true}, checkout);
    expect(gradle()).toBe(original);
    expect(git(checkout, 'rev-parse', 'HEAD')).toBe(before);
    expect(git(checkout, 'tag', '--list')).toBe('');
  });

  it('does not push unrelated tags when push.followTags is enabled', async () => {
    git(checkout, 'config', 'push.followTags', 'true');
    git(checkout, 'tag', '-a', 'v9.9.9', '-m', 'Unpublished desktop tag');
    await runAndroidRelease(options, checkout);
    expect(git(remote, 'tag', '--list')).toBe('android-v0.1.1');
    expect(git(checkout, 'tag', '--list', 'v9.9.9')).toBe('v9.9.9');
  });

  it.each(['staged', 'unstaged', 'untracked', 'branch', 'local tag', 'remote tag', 'remote ahead'])
    ('rejects %s state before changing the version', async (state) => {
      if (state === 'staged' || state === 'unstaged') {
        writeFileSync(path.join(checkout, 'other.txt'), 'unfinished change\n');
        if (state === 'staged') git(checkout, 'add', 'other.txt');
      }
      if (state === 'untracked') writeFileSync(path.join(checkout, 'new.txt'), 'unfinished\n');
      if (state === 'branch') git(checkout, 'switch', '-c', 'feature');
      if (state.endsWith('tag')) {
        git(checkout, 'tag', 'android-v0.1.1');
        if (state === 'remote tag') {
          git(checkout, 'push', 'origin', 'android-v0.1.1');
          git(checkout, 'tag', '-d', 'android-v0.1.1');
        }
      }
      if (state === 'remote ahead') {
        git(checkout, 'commit', '--allow-empty', '-m', 'chore: remote change');
        git(checkout, 'push', 'origin', 'main');
        git(checkout, 'reset', '--hard', 'HEAD~1');
      }
      const before = git(checkout, 'rev-parse', 'HEAD');
      await expect(runAndroidRelease(options, checkout)).rejects.toThrow();
      expect(gradle()).toBe(original);
      expect(git(checkout, 'rev-parse', 'HEAD')).toBe(before);
    });

  it('preserves the local commit and tag on push rejection while remote main stays unchanged', async () => {
    const before = git(remote, 'rev-parse', 'main');
    writeFileSync(path.join(remote, 'hooks/update'), '#!/bin/sh\ncase "$1" in refs/tags/*) exit 1;; esac\n', {mode: 0o755});
    await expect(runAndroidRelease(options, checkout)).rejects.toThrow(/git push --atomic/);
    expect(git(checkout, 'rev-parse', 'HEAD')).not.toBe(before);
    expect(git(checkout, 'rev-parse', 'android-v0.1.1^{commit}')).toBe(git(checkout, 'rev-parse', 'HEAD'));
    expect(git(remote, 'rev-parse', 'main')).toBe(before);
    expect(git(remote, 'tag', '--list')).toBe('');
  });
});
