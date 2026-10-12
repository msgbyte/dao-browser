// @vitest-environment node
import {createHash, generateKeyPairSync, sign} from 'node:crypto';
import {readFileSync, writeFileSync} from 'node:fs';
import path from 'node:path';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {archiveWindowsUpdate} from '../github-release.js';

const fake = vi.hoisted(() => ({publicKey: undefined as unknown,
  assets: new Map<string, string>(), calls: [] as string[][], installer: 'test installer'}));
vi.mock('node:crypto', async original => {
  const crypto = await original<typeof import('node:crypto')>();
  return {...crypto, createPublicKey: (...args: Parameters<typeof crypto.createPublicKey>) =>
    fake.publicKey ?? crypto.createPublicKey(...args)};
});
vi.mock('node:child_process', async original => ({
  ...await original<typeof import('node:child_process')>(),
  spawnSync: () => ({status: 0, stderr: '', stdout: JSON.stringify({isDraft: false,
    assets: [...fake.assets.keys()].map(name => ({name}))})}),
}));
vi.mock('../../utils.js', async original => ({
  ...await original<typeof import('../../utils.js')>(),
  which: () => 'gh',
  runStreaming: async (command: string, args: string[]) => {
    fake.calls.push([command, ...args]);
    if (command === 'curl') writeFileSync(args[args.indexOf('--output') + 1], fake.installer);
    else if (args[1] === 'download') writeFileSync(args.at(-1)!, fake.assets.get(args[args.indexOf('--pattern') + 1])!);
    else if (args[1] === 'upload') fake.assets.set(path.basename(args.at(-1)!), readFileSync(args.at(-1)!, 'utf8'));
    else throw new Error(`Unexpected command: ${command} ${args}`);
    return 0;
  },
}));
const keys = generateKeyPairSync('ed25519');
fake.publicKey = keys.publicKey;
const payload = JSON.stringify({schema: 1, platform: 'windows', arch: 'x64', version: '1.2.3',
  chromiumVersion: '149.0.7827.201', url: 'https://dao-release.msgbyte.com/dao-browser-1.2.3-windows-x64.exe',
  size: 14, sha256: createHash('sha256').update('test installer').digest('hex')});
const envelope = JSON.stringify({payload, signature: sign(null, Buffer.from(payload), keys.privateKey).toString('base64')});
let status: number;
beforeEach(() => {
  fake.assets = new Map([['dao-browser-1.2.3-mac-arm64.dmg', 'original dmg']]);
  fake.calls = []; fake.installer = 'test installer'; status = 200;
  vi.stubGlobal('fetch', vi.fn(async () => new Response(envelope, {status})));
});
afterEach(() => vi.unstubAllGlobals());

describe('signed Windows GitHub archival', () => {
  it('archives only the verified installer and preserves Mac on retry', async () => {
    await archiveWindowsUpdate('v1.2.3');
    expect([...fake.assets.keys()]).toEqual(['dao-browser-1.2.3-mac-arm64.dmg',
      'dao-browser-1.2.3-windows-x64.exe']);
    fake.calls = [];
    await archiveWindowsUpdate('v1.2.3');
    expect(fake.calls.some(call => call[2] === 'upload')).toBe(false);
    expect(fake.assets.get('dao-browser-1.2.3-mac-arm64.dmg')).toBe('original dmg');
  });
  it('permits a missing Windows version only on a real 404', async () => {
    status = 404;
    await archiveWindowsUpdate('v1.2.3');
    expect(fake.calls).toEqual([]);
    status = 503;
    await expect(archiveWindowsUpdate('v1.2.3')).rejects.toThrow('HTTP 503');
  });
  it('rejects a damaged executable before any GitHub upload', async () => {
    fake.installer = 'evil installer';
    await expect(archiveWindowsUpdate('v1.2.3')).rejects.toThrow(/does not match/);
    expect(fake.calls.some(call => call[2] === 'upload')).toBe(false);
  });
  it('leaves legacy metadata attachments untouched on retry', async () => {
    await archiveWindowsUpdate('v1.2.3');
    fake.assets.set('dao-browser-1.2.3-windows-x64.update.json', 'different');
    fake.calls = [];
    await archiveWindowsUpdate('v1.2.3');
    expect(fake.assets.get('dao-browser-1.2.3-windows-x64.update.json')).toBe('different');
    expect(fake.calls.filter(call => call[0] === 'gh')).toEqual([
      ['gh', 'release', 'download', 'v1.2.3', '--pattern',
        'dao-browser-1.2.3-windows-x64.exe', '--output', expect.any(String)],
    ]);
  });
});
