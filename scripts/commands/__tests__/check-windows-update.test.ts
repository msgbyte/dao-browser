// @vitest-environment node
import {mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';
import {checkWindowsUpdate} from '../check-windows-update.js';

const envelope = readFileSync('website/public/windows-update.json', 'utf8');
const payload = JSON.parse(JSON.parse(envelope).payload);
const archiveUrl = payload.url.replace(/\.exe$/, '.update.json');
let root: string;
let responses: Map<string, string>;
const check = () => checkWindowsUpdate(root);

describe('website Windows feed validation', () => {
  beforeEach(() => {
    root = mkdtempSync(path.join(os.tmpdir(), 'dao-feed-check-'));
    mkdirSync(path.join(root, 'website/public'), {recursive: true});
    writeFileSync(path.join(root, 'website/public/windows-update.json'), envelope);
    responses = new Map([[archiveUrl, envelope]]);
    vi.stubGlobal('fetch', vi.fn(async (url: string, options?: RequestInit) => {
      expect(options?.redirect).toBe('error');
      if (url === payload.url) return new Response(null, {status: 200, headers: {'content-length': String(payload.size)}});
      const contents = responses.get(url);
      return new Response(contents ?? null, {status: contents === undefined ? 404 : 200});
    }));
  });
  afterEach(() => {vi.unstubAllGlobals(); rmSync(root, {recursive: true, force: true});});

  it('validates a staged feed using only its published artifacts', async () => {
    await check();
    expect(vi.mocked(fetch).mock.calls.map(([url]) => url).sort()).toEqual(
      [archiveUrl, payload.url].sort());
  });
  it.each(['invalid signature', 'missing archive', 'different archive'])
    ('rejects %s', async problem => {
      if (problem === 'invalid signature') writeFileSync(path.join(root, 'website/public/windows-update.json'), envelope.replace(payload.version, '9.9.9'));
      if (problem === 'missing archive') responses.delete(archiveUrl);
      if (problem === 'different archive') responses.set(archiveUrl, envelope + '\n');
      await expect(check()).rejects.toThrow();
    });
  it.each([404, 403, 500])('rejects an installer returning HTTP %s', async status => {
    const original = vi.mocked(fetch).getMockImplementation()!;
    vi.mocked(fetch).mockImplementation((url, options) => url === payload.url
      ? Promise.resolve(new Response(null, {status})) : original(url, options));
    await expect(check()).rejects.toThrow();
  });
  it('rejects an installer whose size differs from the signed payload', async () => {
    const original = vi.mocked(fetch).getMockImplementation()!;
    vi.mocked(fetch).mockImplementation((url, options) => url === payload.url
      ? Promise.resolve(new Response(null, {headers: {'content-length': '1'}})) : original(url, options));
    await expect(check()).rejects.toThrow();
  });
});
