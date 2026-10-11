// @vitest-environment node
import {createHash, generateKeyPairSync, sign} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {createWindowsUpdatePayload, publishWindowsUpdate, validateWindowsUpdateArtifact,
  assertUnpublishedReleaseArtifacts, WINDOWS_UPDATE_FEED, WINDOWS_UPDATE_ORIGIN, windowsUpdateManifestName} from '../../windows-update.js';

const testKey = vi.hoisted(() => ({publicKey: undefined as unknown}));
vi.mock('node:crypto', async original => {
  const crypto = await original<typeof import('node:crypto')>();
  return {...crypto, createPublicKey: (...args: Parameters<typeof crypto.createPublicKey>) =>
    testKey.publicKey ?? crypto.createPublicKey(...args)};
});
const keys = generateKeyPairSync('ed25519');
testKey.publicKey = keys.publicKey;
let directory: string;
let remote: Map<string, string | Buffer>;
let uploads: string[][];
let promoted: boolean;
let fetchError: number | undefined;
let feedError: boolean;
function signed(payload: object) {
  const text = JSON.stringify(payload);
  return JSON.stringify({payload: text, signature: sign(null, Buffer.from(text), keys.privateKey).toString('base64')});
}
async function files() {
  const installer = path.join(directory, 'dao-browser-1.2.3-windows-x64.exe');
  const dmg = path.join(directory, 'dao-browser-1.2.3-mac-arm64.dmg');
  const manifest = path.join(directory, windowsUpdateManifestName('1.2.3'));
  writeFileSync(installer, 'installer');
  writeFileSync(dmg, 'disk image');
  const sha = createHash('sha256').update('installer').digest('hex');
  writeFileSync(installer + '.sha256', `${sha}  ${path.basename(installer)}\n`);
  writeFileSync(manifest, signed(JSON.parse(await createWindowsUpdatePayload(installer, '1.2.3', '149.0.7827.201'))));
  return [dmg, installer, installer + '.sha256', manifest];
}
async function upload(args: string[]) {
  uploads.push(args);
  const key = args.includes('--key') ? args[args.indexOf('--key') + 1] : undefined;
  for (const file of args.slice(0, args.indexOf('--bucket'))) remote.set(key ?? path.basename(file), readFileSync(file));
  if (key && feedError) throw new Error('Lost the response after publishing');
}
function publish(input: string[]) {
  return publishWindowsUpdate(input, 'releases', stage => {if (stage === 'feed') promoted = true;}, undefined, upload);
}
beforeEach(() => {
  directory = mkdtempSync(path.join(os.tmpdir(), 'dao-publication-'));
  remote = new Map(); uploads = []; promoted = false; fetchError = undefined; feedError = false;
  vi.stubGlobal('fetch', vi.fn(async (url: string) => {
    const key = url.slice(WINDOWS_UPDATE_ORIGIN.length + 1);
    return new Response(fetchError ? '' : remote.get(key)?.toString() ?? '', {status: fetchError ?? (remote.has(key) ? 200 : 404)});
  }));
});
afterEach(() => {vi.unstubAllGlobals(); rmSync(directory, {recursive: true, force: true});});

describe('Windows update publication', () => {
  it('blocks rebuilding a partially published version before overwriting local artifacts', async () => {
    await expect(assertUnpublishedReleaseArtifacts(['installer.exe'])).resolves.toBeUndefined();
    remote.set('installer.exe', 'original bytes');
    await expect(assertUnpublishedReleaseArtifacts(['installer.exe'])).rejects.toThrow(/--skip-build/);
    expect(fetch).toHaveBeenLastCalledWith(expect.any(String), expect.objectContaining({method: 'HEAD'}));
  });
  it('publishes immutable files before the latest feed and allows an identical retry', async () => {
    const input = await files();
    await publish(input);
    expect(uploads).toHaveLength(2);
    expect(uploads[0].slice(0, 4)).toEqual(input);
    expect(uploads[1]).toContain(WINDOWS_UPDATE_FEED);
    expect(promoted).toBe(true);
    uploads = [];
    await publish(input);
    expect(uploads).toHaveLength(1);
  });
  it('refuses to replace an immutable artifact with different bytes', async () => {
    const input = await files();
    remote.set(path.basename(input[1]), 'different');
    await expect(publish(input)).rejects.toThrow(/Different artifact/);
    expect(uploads).toHaveLength(0);
    expect(promoted).toBe(false);
  });
  it.each([403, 500])('does not treat HTTP %s as a missing feed', async status => {
    fetchError = status;
    await expect(publish(await files())).rejects.toThrow(`HTTP ${status}`);
    expect(uploads).toHaveLength(0);
  });
  it('retains the publication boundary when the upload succeeds remotely but throws locally', async () => {
    feedError = true;
    await expect(publish(await files())).rejects.toThrow(/Lost the response/);
    expect(promoted).toBe(true);
    expect(remote.has(WINDOWS_UPDATE_FEED)).toBe(true);
  });
  it('refuses older and conflicting versions', async () => {
    const input = await files();
    const candidate = JSON.parse(JSON.parse(readFileSync(input[3], 'utf8')).payload);
    remote.set(WINDOWS_UPDATE_FEED, signed({...candidate, version: '1.2.4',
      url: candidate.url.replace('1.2.3', '1.2.4')}));
    await expect(publish(input)).rejects.toThrow(/newer or different/);
    remote.set(WINDOWS_UPDATE_FEED, signed({...candidate, size: 123}));
    await expect(publish(input)).rejects.toThrow(/newer or different/);
    expect(uploads).toHaveLength(0);
  });
  it('requires the matching checksum sidecar before publication', async () => {
    const input = await files();
    writeFileSync(input[2], 'invalid checksum');
    await expect(validateWindowsUpdateArtifact(input[3], input[1], '1.2.3', '149.0.7827.201'))
      .rejects.toThrow(/checksum/);
  });
  it('recognizes a previously public candidate before a later failure', async () => {
    const input = await files();
    remote.set(WINDOWS_UPDATE_FEED, readFileSync(input[3]));
    remote.set(path.basename(input[0]), 'conflict');
    await expect(publish(input)).rejects.toThrow();
    expect(promoted).toBe(true);
  });
});
