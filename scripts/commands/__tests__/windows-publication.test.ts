// @vitest-environment node
import {createHash, generateKeyPairSync, sign} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {createWindowsUpdatePayload, uploadWindowsReleaseArtifacts, validateWindowsUpdateArtifact,
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
}
function publish(input: string[]) {
  return uploadWindowsReleaseArtifacts(input, 'releases', () => {promoted = true;}, undefined, upload);
}
beforeEach(() => {
  directory = mkdtempSync(path.join(os.tmpdir(), 'dao-publication-'));
  remote = new Map(); uploads = []; promoted = false; fetchError = undefined;
  vi.stubGlobal('fetch', vi.fn(async (url: string, options?: RequestInit) => {
    const key = url.slice(WINDOWS_UPDATE_ORIGIN.length + 1);
    const body = fetchError ? '' : remote.get(key)?.toString() ?? '';
    return new Response(options?.method === 'HEAD' ? null : body, {
      status: fetchError ?? (remote.has(key) ? 200 : 404),
      headers: {'content-length': String(Buffer.byteLength(body))},
    });
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
  it('uploads immutable files without changing the latest feed and allows an identical retry', async () => {
    const input = await files();
    await publish(input);
    expect(uploads).toHaveLength(1);
    expect(uploads[0].slice(0, 4)).toEqual(input);
    expect(remote.has(WINDOWS_UPDATE_FEED)).toBe(false);
    expect(promoted).toBe(true);
    uploads = [];
    await publish(input);
    expect(uploads).toHaveLength(0);
    const artifactRequests = vi.mocked(fetch).mock.calls.filter(([url]) => !String(url).endsWith(WINDOWS_UPDATE_FEED));
    expect(artifactRequests).toHaveLength(input.length * 2);
    for (const [, options] of artifactRequests) expect(options?.method).toBe('HEAD');
  });
  it('publishes after successful uploads even while artifact URLs still return 404', async () => {
    const input = await files();
    const publicFetch = vi.mocked(fetch).getMockImplementation()!;
    vi.mocked(fetch).mockImplementation((url, options) => String(url).endsWith(WINDOWS_UPDATE_FEED)
      ? publicFetch(url, options) : Promise.resolve(new Response(null, {status: 404})));
    await publish(input);
    expect(promoted).toBe(true);
    expect(remote.has(WINDOWS_UPDATE_FEED)).toBe(false);
    expect(uploads).toHaveLength(1);
  });
  it.each(['', 'bad', 'oversized installer'])('rejects an existing artifact with a different size (%j)', async content => {
    const input = await files();
    remote.set(path.basename(input[1]), content);
    await expect(publish(input)).rejects.toThrow(/Different artifact|size limit/);
    expect(uploads).toHaveLength(0);
    expect(promoted).toBe(false);
  });
  it('requests uncompressed artifact sizes and accepts HEAD responses without a content length', async () => {
    const input = await files();
    for (const file of input) remote.set(path.basename(file), readFileSync(file));
    const publicFetch = vi.mocked(fetch).getMockImplementation()!;
    vi.mocked(fetch).mockImplementation(async (url, options) => {
      const response = await publicFetch(url, options);
      if (options?.method === 'HEAD') response.headers.delete('content-length');
      return response;
    });
    await publish(input);
    expect(uploads).toHaveLength(0);
    for (const [, options] of vi.mocked(fetch).mock.calls) {
      expect(new Headers(options?.headers).get('accept-encoding')).toBe('identity');
    }
  });
  it.each(['HEAD'])('reports the failing %s URL and underlying network error', async method => {
    const input = await files();
    const cause = new TypeError('fetch failed', {cause: new Error('connect ETIMEDOUT')});
    const publicFetch = vi.mocked(fetch).getMockImplementation()!;
    vi.mocked(fetch).mockImplementation((url, options) => {
      if ((options?.method ?? 'GET') === method) return Promise.reject(cause);
      return publicFetch(url, options);
    });
    const name = method === 'GET' ? WINDOWS_UPDATE_FEED : path.basename(input[0]);
    await expect(publish(input)).rejects.toThrow(`${method} ${WINDOWS_UPDATE_ORIGIN}/${name}: Error: connect ETIMEDOUT`);
    expect(uploads).toHaveLength(0);
    expect(promoted).toBe(false);
  });
  it.each([403, 500])('does not treat HTTP %s as a missing artifact', async status => {
    fetchError = status;
    await expect(publish(await files())).rejects.toThrow(`HTTP ${status}`);
    expect(uploads).toHaveLength(0);
  });
  it('requires the matching checksum sidecar before publication', async () => {
    const input = await files();
    writeFileSync(input[2], 'invalid checksum');
    await expect(validateWindowsUpdateArtifact(input[3], input[1], '1.2.3', '149.0.7827.201'))
      .rejects.toThrow(/checksum/);
  });
});
