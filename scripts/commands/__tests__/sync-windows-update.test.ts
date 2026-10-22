// @vitest-environment node
import {mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {syncWindowsUpdate} from '../sync-windows-update.js';

const envelope = readFileSync('website/public/windows-update.json', 'utf8');
const payload = JSON.parse(JSON.parse(envelope).payload);
const websiteUrl = 'https://dao.msgbyte.com/windows-update.json';
const legacyUrl = 'https://dao-release.msgbyte.com/windows-update.json';
const archiveUrl = payload.url.replace(/\.exe$/, '.update.json');
let root: string;
let responses: Map<string, string>;
let uploads: string[][];
const upload = async (args: string[]) => {
  uploads.push(args);
  expect(args.slice(1)).toEqual(['--bucket', 'releases', '--key', 'windows-update.json', '--content-type', 'application/json']);
  responses.set(legacyUrl, readFileSync(args[0], 'utf8'));
};
const sync = (check = false) => syncWindowsUpdate({check, bucket: 'releases'}, root, upload);

describe('website deployment Windows mirror', () => {
  beforeEach(() => {
    root = mkdtempSync(path.join(os.tmpdir(), 'dao-mirror-'));
    mkdirSync(path.join(root, 'website/public'), {recursive: true});
    writeFileSync(path.join(root, 'website/public/windows-update.json'), envelope);
    responses = new Map([[websiteUrl, envelope], [archiveUrl, envelope]]);
    uploads = [];
    vi.stubGlobal('fetch', vi.fn(async (url: string, options?: RequestInit) => {
      expect(options?.redirect).toBe('error');
      if (url === payload.url) return new Response(null, {status: 200, headers: {'content-length': String(payload.size)}});
      const contents = responses.get(url);
      return new Response(contents ?? null, {status: contents === undefined ? 404 : 200});
    }));
  });
  afterEach(() => {vi.unstubAllGlobals(); rmSync(root, {recursive: true, force: true});});

  it('copies the exact deployed signed bytes and safely repeats the upload', async () => {
    await sync();
    expect(responses.get(legacyUrl)).toBe(envelope);
    expect(uploads).toHaveLength(1);
    await sync(); expect(uploads).toHaveLength(2);
    expect(responses.get(legacyUrl)).toBe(envelope);
  });
  it('repairs a corrupted legacy feed using the verified website snapshot', async () => {
    responses.set(legacyUrl, '{broken');
    await sync();
    expect(uploads).toHaveLength(1);
    expect(responses.get(legacyUrl)).toBe(envelope);
  });
  it('validates a staged feed without requiring deployment or publishing', async () => {
    responses.delete(websiteUrl);
    await sync(true); expect(uploads).toHaveLength(0);
  });
  it.each(['not deployed', 'different deployment', 'invalid signature', 'missing archive', 'different archive'])
    ('does not mirror %s', async problem => {
      if (problem === 'not deployed') responses.delete(websiteUrl);
      if (problem === 'different deployment') responses.set(websiteUrl, envelope + '\n');
      if (problem === 'invalid signature') writeFileSync(path.join(root, 'website/public/windows-update.json'), envelope.replace(payload.version, '9.9.9'));
      if (problem === 'missing archive') responses.delete(archiveUrl);
      if (problem === 'different archive') responses.set(archiveUrl, envelope + '\n');
      await expect(sync()).rejects.toThrow(); expect(uploads).toHaveLength(0);
    });
  it.each([404, 403, 500])('rejects an installer returning HTTP %s', async status => {
    const original = vi.mocked(fetch).getMockImplementation()!;
    vi.mocked(fetch).mockImplementation((url, options) => url === payload.url
      ? Promise.resolve(new Response(null, {status})) : original(url, options));
    await expect(sync()).rejects.toThrow(); expect(uploads).toHaveLength(0);
  });
  it('rejects an installer whose size differs from the signed payload', async () => {
    const original = vi.mocked(fetch).getMockImplementation()!;
    vi.mocked(fetch).mockImplementation((url, options) => url === payload.url
      ? Promise.resolve(new Response(null, {headers: {'content-length': '1'}})) : original(url, options));
    await expect(sync()).rejects.toThrow(); expect(uploads).toHaveLength(0);
  });
  it('reports failure when the public legacy feed does not confirm the upload', async () => {
    await expect(syncWindowsUpdate({bucket: 'releases'}, root, async () => {})).rejects.toThrow(/confirm/);
  });
});
