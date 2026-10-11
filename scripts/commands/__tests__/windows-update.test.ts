// @vitest-environment node
import {createHash, generateKeyPairSync, sign} from 'node:crypto';
import {mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {afterEach, describe, expect, it} from 'vitest';
import {createWindowsUpdatePayload, verifyWindowsUpdateEnvelope, windowsUpdateManifestName} from '../../windows-update.js';

const keys = generateKeyPairSync('ed25519');
const publicKey = keys.publicKey.export({format: 'der', type: 'spki'}).subarray(-32).toString('base64');
const roots: string[] = [];
afterEach(() => roots.splice(0).forEach(root => rmSync(root, {recursive: true, force: true})));
function envelope(payload: string) {
  return JSON.stringify({payload, signature: sign(null, Buffer.from(payload), keys.privateKey).toString('base64')});
}
async function candidate() {
  const root = mkdtempSync(path.join(os.tmpdir(), 'dao-update-'));
  roots.push(root);
  const file = path.join(root, 'dao-browser-1.2.3-windows-x64.exe');
  writeFileSync(file, 'test installer');
  return {file, payload: await createWindowsUpdatePayload(file, '1.2.3', '149.0.7827.201')};
}
describe('signed Windows updates', () => {
  it('accepts the same signed fixture as the C++ updater and embeds the Sparkle key', () => {
    const fixture = readFileSync('src/dao/browser/updater/dao_update_manifest_test_data.h', 'utf8');
    const key = fixture.match(/kTestUpdateKey\[\] = "([^"]+)"/)![1];
    const envelope = fixture.match(/R"fixture\((.*)\)fixture"/)![1];
    expect(verifyWindowsUpdateEnvelope(envelope, key).version).toBe('1.2.3');
    const native = readFileSync('src/dao/browser/updater/dao_update_manifest.h', 'utf8');
    const plist = readFileSync('src/patches/chrome/app/app-Info.plist.patch', 'utf8');
    const production = 'dz3KV6hBHuxJGYXru/dOYip1oSBH5o02nrdGZdWF8x4=';
    expect(native).toContain(production);
    expect(plist).toContain(production);
  });
  it('binds version, origin, size and artifact digest in one signed payload', async () => {
    const {file, payload} = await candidate();
    const value = verifyWindowsUpdateEnvelope(envelope(payload), publicKey);
    expect(value).toEqual({schema: 1, platform: 'windows', arch: 'x64', version: '1.2.3',
      chromiumVersion: '149.0.7827.201', url: 'https://dao-release.msgbyte.com/dao-browser-1.2.3-windows-x64.exe',
      size: 14, sha256: createHash('sha256').update(readFileSync(file)).digest('hex')});
    expect(windowsUpdateManifestName('1.2.3')).toBe('dao-browser-1.2.3-windows-x64.update.json');
  });
  it('rejects tampered payloads, signatures and keys', async () => {
    const {payload} = await candidate();
    const signed = envelope(payload);
    expect(() => verifyWindowsUpdateEnvelope(signed.replace('1.2.3', '9.2.3'), publicKey)).toThrow();
    expect(() => verifyWindowsUpdateEnvelope(JSON.stringify({...JSON.parse(signed), signature: 'AAAA'}), publicKey)).toThrow();
    expect(() => verifyWindowsUpdateEnvelope(signed, Buffer.alloc(32).toString('base64'))).toThrow();
  });
  it.each([
    {schema: 2}, {platform: 'mac'}, {arch: 'arm64'}, {version: '01.2.3'},
    {chromiumVersion: '149.0'}, {size: 0}, {size: 1024 ** 3 + 1}, {size: 1.5},
    {sha256: 'not-a-hash'}, {url: 'https://evil.example/payload.exe'},
    {url: 'https://dao-release.msgbyte.com/dao-browser-1.2.3-windows-x64.exe#x'},
    {url: 'https://user@dao-release.msgbyte.com/dao-browser-1.2.3-windows-x64.exe'},
    {url: 'https://dao-release.msgbyte.com/dao-browser-1.2.4-windows-x64.exe'},
  ])('rejects signed but invalid metadata: %j', async mutation => {
    const {payload} = await candidate();
    expect(() => verifyWindowsUpdateEnvelope(envelope(JSON.stringify({...JSON.parse(payload), ...mutation})), publicKey)).toThrow();
  });
  it('rejects oversized input and unexpected envelope fields', () => {
    expect(() => verifyWindowsUpdateEnvelope(' '.repeat(65537), publicKey)).toThrow();
    expect(() => verifyWindowsUpdateEnvelope('{"payload":3,"signature":null}', publicKey)).toThrow();
  });
  it('rejects a mismatched artifact name or empty installer', async () => {
    const {file} = await candidate();
    await expect(createWindowsUpdatePayload(file, '1.2.4', '149.0.7827.201')).rejects.toThrow();
    writeFileSync(file, '');
    await expect(createWindowsUpdatePayload(file, '1.2.3', '149.0.7827.201')).rejects.toThrow();
  });
});
