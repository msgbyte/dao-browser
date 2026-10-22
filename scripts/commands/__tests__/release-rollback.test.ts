// @vitest-environment node
import {spawnSync} from 'node:child_process';
import {generateKeyPairSync, sign} from 'node:crypto';
import {chmodSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {PassThrough} from 'node:stream';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {listWebsiteReleases, resolveRollbackVersion, runReleaseRollback} from '../release-rollback.js';

const INFO = 'website/public/info.json';
const FEED = 'website/public/windows-update.json';
const testKey = vi.hoisted(() => ({publicKey: undefined as unknown}));
vi.mock('node:crypto', async original => {
  const crypto = await original<typeof import('node:crypto')>();
  return {...crypto, createPublicKey: (...args: Parameters<typeof crypto.createPublicKey>) =>
    testKey.publicKey ?? crypto.createPublicKey(...args)};
});
const keys = generateKeyPairSync('ed25519');
testKey.publicKey = keys.publicKey;
function signedFeed(version: string) {
  const payload = JSON.stringify({schema: 1, platform: 'windows', arch: 'x64', version,
    chromiumVersion: '153.0.1.0', url: `https://dao-release.msgbyte.com/dao-browser-${version}-windows-x64.exe`,
    size: 123, sha256: 'a'.repeat(64)});
  return JSON.stringify({payload, signature: sign(null, Buffer.from(payload), keys.privateKey).toString('base64')}) + '\n';
}
const oldFeed = signedFeed('1.0.8');
const newFeed = signedFeed('1.0.10');
const oldInfo = JSON.stringify({version: '1.0.9', chromiumVersion: '153.0.1.0', releasedAt: '2026-10-01',
  platforms: {macArm64: {label: 'macOS', url: 'https://dao-release.msgbyte.com/dao-browser-1.0.9-mac-arm64.dmg'},
    win: {label: 'Windows', version: '1.0.8', url: 'https://dao-release.msgbyte.com/dao-browser-1.0.8-windows-x64.exe'}},
  default: 'macArm64'}, null, 2) + '\n';
const newInfo = oldInfo.replaceAll('1.0.9', '1.0.10').replaceAll('1.0.8', '1.0.10').replace('2026-10-01', '2026-10-02');
const git = (cwd: string, ...args: string[]) => {
  const result = spawnSync('git', args, {cwd, encoding: 'utf8'});
  if (result.status !== 0) throw new Error(result.stderr);
  return result.stdout.trim();
};

describe('website release rollback', () => {
  let directory: string;
  let root: string;
  let remote: string;
  const info = () => readFileSync(path.join(root, INFO), 'utf8');
  const head = () => git(root, 'rev-parse', 'HEAD');
  const hook = (repo: string, name: string) => {
    const file = path.join(repo, name);
    writeFileSync(file, '#!/bin/sh\nexit 1\n');
    chmodSync(file, 0o755);
  };

  beforeEach(() => {
    directory = mkdtempSync(path.join(tmpdir(), 'dao-rollback-'));
    root = path.join(directory, 'checkout');
    remote = path.join(directory, 'origin.git');
    mkdirSync(root);
    git(directory, 'init', '--bare', remote);
    git(root, 'init', '-b', 'main');
    git(root, 'config', 'user.name', 'Rollback Test');
    git(root, 'config', 'user.email', 'rollback@example.invalid');
    git(root, 'config', 'commit.gpgsign', 'false');
    git(root, 'config', 'core.hooksPath', path.join(root, '.git/hooks'));
    git(remote, 'config', 'core.hooksPath', path.join(remote, 'hooks'));
    mkdirSync(path.join(root, 'website/public'), {recursive: true});
    writeFileSync(path.join(root, INFO), oldInfo);
    writeFileSync(path.join(root, 'website/public/appcast.xml'), '<rss/>\n');
    writeFileSync(path.join(root, 'dao.json'), '{"version":"1.0.10"}\n');
    git(root, 'add', '.');
    git(root, 'commit', '-m', 'chore: publish first website release');
    git(root, 'tag', 'v1.0.10');
    writeFileSync(path.join(root, INFO), newInfo);
    writeFileSync(path.join(root, FEED), newFeed);
    git(root, 'add', FEED);
    git(root, 'commit', '-am', 'chore: publish second website release');
    git(root, 'remote', 'add', 'origin', remote);
    git(root, 'push', 'origin', 'main');
    vi.stubGlobal('fetch', vi.fn(async (url: string, options?: RequestInit) => options?.method === 'HEAD'
      ? new Response(null, {status: 200, headers: {'content-length': '123'}})
      : new Response(url.includes('1.0.8') ? oldFeed : newFeed)));
  });
  afterEach(() => {vi.unstubAllGlobals(); rmSync(directory, {recursive: true, force: true});});

  it('lists metadata versions instead of the preceding version in release tags', async () => {
    const releases = await listWebsiteReleases(root);
    expect(releases.map(release => release.version)).toEqual(['1.0.10', '1.0.9']);
    expect(await resolveRollbackVersion(releases, '1.0.10')).toEqual(releases[0]);
    await expect(resolveRollbackVersion(releases, '9.9.9')).rejects.toThrow();
    await expect(resolveRollbackVersion(releases)).rejects.toThrow('--version');
  });

  it('commits only the selected website snapshot locally without changing appcast, app versions or remote refs', async () => {
    const before = head();
    git(root, 'config', 'push.followTags', 'true');
    git(root, '-c', 'tag.gpgsign=false', 'tag', '-a', 'v9.9.9', '-m', 'Unpublished');
    await runReleaseRollback({version: '1.0.9'}, root);
    expect(info()).toBe(oldInfo);
    expect(git(root, 'diff-tree', '--no-commit-id', '--name-only', '-r', 'HEAD')).toBe(`${INFO}\n${FEED}`);
    expect(readFileSync(path.join(root, FEED), 'utf8')).toBe(oldFeed);
    expect(readFileSync(path.join(root, 'website/public/appcast.xml'), 'utf8')).toBe('<rss/>\n');
    expect(readFileSync(path.join(root, 'dao.json'), 'utf8')).toBe('{"version":"1.0.10"}\n');
    expect(head()).not.toBe(before);
    expect(git(remote, 'rev-parse', 'main')).toBe(before);
    expect(git(remote, 'tag', '--list')).toBe('');
    expect(git(root, 'status', '--porcelain')).toBe('');
    expect((await listWebsiteReleases(root)).map(release => release.version)).toEqual(['1.0.9', '1.0.10']);
  });

  it('previews offline without editing, committing or pushing', async () => {
    const before = head();
    git(root, 'remote', 'remove', 'origin');
    vi.stubGlobal('fetch', () => {throw new Error('Unexpected network request');});
    await runReleaseRollback({version: '1.0.9', dryRun: true}, root);
    expect(info()).toBe(newInfo);
    expect(head()).toBe(before);
    expect(git(root, 'status', '--porcelain')).toBe('');
  });

  it('does nothing when the selected snapshot is already current', async () => {
    const before = head();
    await runReleaseRollback({version: '1.0.10'}, root);
    expect(head()).toBe(before);
    expect(info()).toBe(newInfo);
  });

  it('restores the feed even when the website download version is unchanged', async () => {
    writeFileSync(path.join(root, FEED), oldFeed);
    git(root, 'commit', '-am', 'Temporarily withdraw Windows update');
    git(root, 'push', 'origin', 'main');
    await runReleaseRollback({version: '1.0.10'}, root);
    expect(info()).toBe(newInfo);
    expect(readFileSync(path.join(root, FEED), 'utf8')).toBe(newFeed);
    expect(git(root, 'diff-tree', '--no-commit-id', '--name-only', '-r', 'HEAD')).toBe(FEED);
  });

  it.each(['missing', 'tampered', 'wrong version'])('rejects a %s archived Windows manifest before changing files', async problem => {
    const original = vi.mocked(fetch).getMockImplementation()!;
    vi.mocked(fetch).mockImplementation((url, options) => String(url).endsWith('.update.json')
      ? Promise.resolve(problem === 'missing' ? new Response(null, {status: 404})
        : new Response(problem === 'tampered' ? oldFeed.replace('1.0.8', '9.9.9') : newFeed))
      : original(url, options));
    const before = head();
    await expect(runReleaseRollback({version: '1.0.9'}, root)).rejects.toThrow();
    expect(info()).toBe(newInfo);
    expect(readFileSync(path.join(root, FEED), 'utf8')).toBe(newFeed);
    expect(head()).toBe(before);
  });

  it.each(['dirty', 'branch'])('rejects a %s checkout before changing download metadata', async state => {
    if (state === 'dirty') writeFileSync(path.join(root, 'pending.txt'), 'unrelated work');
    if (state === 'branch') git(root, 'switch', '-c', 'other');
    const before = head();
    await expect(runReleaseRollback({version: '1.0.9'}, root)).rejects.toThrow();
    expect(info()).toBe(newInfo);
    expect(head()).toBe(before);
  });

  it.each(['ahead', 'behind', 'diverged', 'no remote', 'unreachable remote'])
    ('creates a local rollback commit with %s', async state => {
    if (state === 'ahead' || state === 'diverged') git(root, 'commit', '--allow-empty', '-m', 'Unpublished work');
    if (state === 'behind' || state === 'diverged') {
      const next = git(root, 'commit-tree', 'HEAD^{tree}', '-p', 'origin/main', '-m', 'Remote work');
      git(root, 'push', 'origin', `${next}:refs/heads/main`);
    }
    if (state === 'no remote') git(root, 'remote', 'remove', 'origin');
    if (state === 'unreachable remote') git(root, 'remote', 'set-url', 'origin', path.join(directory, 'missing.git'));
    const remoteHead = git(remote, 'rev-parse', 'main');
    const before = head();
    await runReleaseRollback({version: '1.0.9'}, root);
    expect(git(root, 'rev-parse', 'HEAD^')).toBe(before);
    expect(info()).toBe(oldInfo);
    expect(readFileSync(path.join(root, FEED), 'utf8')).toBe(oldFeed);
    expect(git(remote, 'rev-parse', 'main')).toBe(remoteHead);
  });

  it.each(['missing', 'network'])('rejects a %s Windows installer even when the Mac installer exists', async failure => {
    vi.stubGlobal('fetch', async (url: string, options: RequestInit) => {
      expect(options.method).toBe('HEAD');
      if (url.endsWith('.exe')) {
        if (failure === 'network') throw new Error('Network unavailable');
        return new Response(null, {status: 404});
      }
      return new Response(null, {status: 200});
    });
    const before = head();
    await expect(runReleaseRollback({version: '1.0.9'}, root)).rejects.toThrow();
    expect(info()).toBe(newInfo);
    expect(head()).toBe(before);
  });

  it('preserves an edit made while checking installer availability', async () => {
    vi.stubGlobal('fetch', async () => {
      writeFileSync(path.join(root, INFO), newInfo + '\n');
      return new Response(null, {status: 200});
    });
    const before = head();
    await expect(runReleaseRollback({version: '1.0.9'}, root)).rejects.toThrow();
    expect(info()).toBe(newInfo + '\n');
    expect(head()).toBe(before);
  });

  it('restores metadata when the commit hook rejects publication', async () => {
    hook(path.join(root, '.git/hooks'), 'pre-commit');
    const before = head();
    await expect(runReleaseRollback({version: '1.0.9'}, root)).rejects.toThrow();
    expect(info()).toBe(newInfo);
    expect(readFileSync(path.join(root, FEED), 'utf8')).toBe(newFeed);
    expect(head()).toBe(before);
    expect(git(root, 'status', '--porcelain')).toBe('');
  });

  it('does not invoke push hooks or claim that the rollback was published', async () => {
    hook(path.join(root, '.git/hooks'), 'pre-push');
    const output = vi.spyOn(console, 'log').mockImplementation(() => {});
    const before = head();
    await runReleaseRollback({version: '1.0.9'}, root);
    expect(info()).toBe(oldInfo);
    expect(head()).not.toBe(before);
    expect(git(remote, 'rev-parse', 'main')).toBe(before);
    expect(git(root, 'status', '--porcelain')).toBe('');
    expect(output.mock.calls.flat().join('\n')).toContain('locally');
    expect(output.mock.calls.flat().join('\n')).not.toContain('Pushed');
  });

  it('does not publish extra files added by a commit hook', async () => {
    const file = path.join(root, '.git/hooks/pre-commit');
    writeFileSync(file, '#!/bin/sh\nprintf "\\n" >> dao.json\ngit add dao.json\n');
    chmodSync(file, 0o755);
    const before = head();
    await expect(runReleaseRollback({version: '1.0.9'}, root)).rejects.toThrow('Inspect the local commit');
    expect(git(remote, 'rev-parse', 'main')).toBe(before);
  });
});

describe('interactive rollback selection', () => {
  const releases = [
    {version: '1.0.10', releasedAt: '2026-10-02', contents: newInfo, urls: []},
    {version: '1.0.9', releasedAt: '2026-10-01', contents: oldInfo, urls: []},
  ];
  function terminal() {
    const input = Object.assign(new PassThrough(), {
      isTTY: true, isRaw: false,
      setRawMode(mode: boolean) {this.isRaw = mode; return this;},
    });
    const output = Object.assign(new PassThrough(), {isTTY: true});
    const select = () => resolveRollbackVersion(releases, undefined,
      input as unknown as NodeJS.ReadStream, output as unknown as NodeJS.WriteStream);
    return {input, output, select};
  }

  it.each([
    {keys: '\x1b[B\r', version: '1.0.9', action: 'Down'},
    {keys: '\x1b[B\x1b[A\r', version: '1.0.10', action: 'Down then Up'},
    {keys: '\x1b[A\r', version: '1.0.9', action: 'Up wrapping to the last version'},
  ])('selects $version using $action and Enter', async ({keys, version}) => {
    const {input, output, select} = terminal();
    const selected = select();
    const result = expect(selected).resolves.toMatchObject({version});
    input.write(keys);
    await result;
    expect(output.read().toString()).toContain('1.0.9');
    expect(input.isRaw).toBe(false);
    expect(input.isPaused()).toBe(true);
    expect(input.listenerCount('keypress')).toBe(0);
  });

  it.each(['\x03', '\x1b', 'EOF'])('cancels on %j and restores terminal input', async key => {
    const {input, select} = terminal();
    const result = expect(select()).rejects.toThrow(/cancel/i);
    if (key === 'EOF') input.end(); else input.write(key);
    await result;
    expect(input.isRaw).toBe(false);
    expect(input.isPaused()).toBe(true);
    expect(input.listenerCount('keypress')).toBe(0);
  });
});
