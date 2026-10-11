import {createHash, createPublicKey, verify} from 'node:crypto';
import {execFile} from 'node:child_process';
import {createReadStream, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {promisify} from 'node:util';
import {ROOT_DIR} from './utils.js';
import {windowsInstallerName} from './commands/package-windows.js';

export const WINDOWS_UPDATE_ORIGIN = 'https://dao-release.msgbyte.com';
export const WINDOWS_UPDATE_PUBLIC_KEY = 'dz3KV6hBHuxJGYXru/dOYip1oSBH5o02nrdGZdWF8x4=';
export const WINDOWS_UPDATE_FEED = 'windows-update.json';
export const MAX_UPDATE_SIZE = 1024 ** 3;
export interface WindowsUpdatePayload {
  schema: 1;
  platform: 'windows';
  arch: 'x64';
  version: string;
  chromiumVersion: string;
  url: string;
  size: number;
  sha256: string;
}

export function windowsUpdateManifestName(version: string): string {
  return windowsInstallerName(version).replace(/\.exe$/, '.update.json');
}

export async function hashUpdateFile(file: string): Promise<string> {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(file)) hash.update(chunk);
  return hash.digest('hex');
}

function validatePayload(value: WindowsUpdatePayload): void {
  const version = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/;
  if (!value || value.schema !== 1 || value.platform !== 'windows' || value.arch !== 'x64' ||
      typeof value.version !== 'string' || !version.test(value.version) ||
      typeof value.chromiumVersion !== 'string' || !/^(0|[1-9]\d*)(\.(0|[1-9]\d*)){3}$/.test(value.chromiumVersion) ||
      [...value.version.split('.'), ...value.chromiumVersion.split('.')].some(v => Number(v) > 0xffffffff) ||
      value.url !== `${WINDOWS_UPDATE_ORIGIN}/${windowsInstallerName(value.version)}` ||
      !Number.isSafeInteger(value.size) || value.size <= 0 || value.size > MAX_UPDATE_SIZE ||
      typeof value.sha256 !== 'string' || !/^[0-9a-f]{64}$/.test(value.sha256) ||
      Object.keys(value).sort().join(',') !== 'arch,chromiumVersion,platform,schema,sha256,size,url,version') {
    throw new Error('Invalid Windows update metadata.');
  }
}

export async function createWindowsUpdatePayload(artifact: string, version: string, chromiumVersion: string): Promise<string> {
  if (path.basename(artifact) !== windowsInstallerName(version)) throw new Error('Unexpected installer filename.');
  const value: WindowsUpdatePayload = {schema: 1, platform: 'windows', arch: 'x64', version, chromiumVersion,
    url: `${WINDOWS_UPDATE_ORIGIN}/${path.basename(artifact)}`, size: statSync(artifact).size, sha256: await hashUpdateFile(artifact)};
  validatePayload(value);
  return JSON.stringify(value);
}

export function verifyWindowsUpdateEnvelope(envelope: string, publicKey = WINDOWS_UPDATE_PUBLIC_KEY): WindowsUpdatePayload {
  if (Buffer.byteLength(envelope) > 65536) throw new Error('Update manifest is too large.');
  const value = JSON.parse(envelope);
  if (!value || typeof value.payload !== 'string' || typeof value.signature !== 'string' ||
      Object.keys(value).sort().join(',') !== 'payload,signature') throw new Error('Invalid signed update envelope.');
  const key = Buffer.from(publicKey, 'base64');
  const signature = Buffer.from(value.signature, 'base64');
  if (key.length !== 32 || signature.length !== 64 || signature.toString('base64') !== value.signature ||
      !verify(null, Buffer.from(value.payload), createPublicKey({format: 'der', type: 'spki',
        key: Buffer.concat([Buffer.from('302a300506032b6570032100', 'hex'), key])}), signature)) {
    throw new Error('Windows update signature verification failed.');
  }
  const payload = JSON.parse(value.payload);
  validatePayload(payload);
  return payload;
}

export async function signWindowsUpdatePayload(payload: string, signal?: AbortSignal): Promise<string> {
  const directory = mkdtempSync(path.join(os.tmpdir(), 'dao-update-sign-'));
  try {
    const file = path.join(directory, 'payload.json');
    writeFileSync(file, payload);
    const {stdout} = await promisify(execFile)(path.join(ROOT_DIR, 'third_party/sparkle/bin/sign_update'), [file], {signal});
    const signature = stdout.match(/sparkle:edSignature="([A-Za-z0-9+/=]+)"/)?.[1];
    if (!signature) throw new Error('Sparkle did not produce an update signature.');
    const envelope = JSON.stringify({payload, signature}) + '\n';
    verifyWindowsUpdateEnvelope(envelope);
    return envelope;
  } finally {
    rmSync(directory, {recursive: true, force: true});
  }
}

export async function validateWindowsUpdateArtifact(manifestPath: string, artifact: string, version: string,
    chromiumVersion: string): Promise<WindowsUpdatePayload> {
  const value = verifyWindowsUpdateEnvelope(readFileSync(manifestPath, 'utf8'));
  const expected = JSON.parse(await createWindowsUpdatePayload(artifact, version, chromiumVersion));
  if (Object.keys(expected).some(key => expected[key] !== value[key as keyof WindowsUpdatePayload])) {
    throw new Error('Signed update metadata does not match the installer or release version.');
  }
  if (readFileSync(artifact + '.sha256', 'utf8').trim() !== `${value.sha256}  ${path.basename(artifact)}`) {
    throw new Error('Windows installer checksum sidecar does not match the signed update.');
  }
  return value;
}

async function publicObject(name: string, limit: number, signal?: AbortSignal): Promise<Response | null> {
  const response = await fetch(`${WINDOWS_UPDATE_ORIGIN}/${name}`, {redirect: 'error', cache: 'no-store',
    signal: signal ? AbortSignal.any([signal, AbortSignal.timeout(60_000)]) : AbortSignal.timeout(60_000)});
  if (response.status === 404) return null;
  if (!response.ok) throw new Error(`Cannot verify published ${name}: HTTP ${response.status}`);
  if (Number(response.headers.get('content-length')) > limit) throw new Error(`Published ${name} exceeds the size limit.`);
  return response;
}

async function readPublicFeed(signal?: AbortSignal): Promise<string | null> {
  const response = await publicObject(WINDOWS_UPDATE_FEED, 65536, signal);
  if (!response) return null;
  const chunks: Uint8Array[] = [];
  let size = 0;
  for await (const chunk of response.body!) {
    size += chunk.length;
    if (size > 65536) throw new Error('Published update feed exceeds the size limit.');
    chunks.push(chunk);
  }
  const text = Buffer.concat(chunks).toString('utf8');
  verifyWindowsUpdateEnvelope(text);
  return text;
}

function compareVersions(a: string, b: string): number {
  const right = b.split('.').map(Number);
  for (const [i, part] of a.split('.').map(Number).entries()) {
    if (part !== right[i]) return part < right[i] ? -1 : 1;
  }
  return 0;
}

/** Immutable objects first; the small signed feed is the publication boundary. */
export async function publishWindowsUpdate(files: string[], bucket: string, onPublication: (stage: 'artifacts' | 'feed') => void,
    signal: AbortSignal | undefined, upload: (args: string[]) => Promise<void>): Promise<void> {
  const manifest = files.at(-1)!;
  const candidateText = readFileSync(manifest, 'utf8');
  const candidate = verifyWindowsUpdateEnvelope(candidateText);
  const installer = path.join(path.dirname(manifest), windowsInstallerName(candidate.version));
  if (!files.includes(installer) || !files.includes(installer + '.sha256'))
    throw new Error('Windows publication requires the installer and checksum sidecar.');
  const previous = await readPublicFeed(signal);
  if (previous) {
    const current = verifyWindowsUpdateEnvelope(previous);
    if (current.version === candidate.version) onPublication('feed');
    if (compareVersions(current.version, candidate.version) > 0 ||
        (current.version === candidate.version && previous !== candidateText))
      throw new Error('A newer or different update is already published; refusing to overwrite it.');
  }
  await validateWindowsUpdateArtifact(manifest, installer, candidate.version, candidate.chromiumVersion);
  const missing: string[] = [];
  for (const file of files) {
    signal?.throwIfAborted();
    const size = statSync(file).size;
    const remote = await publicObject(path.basename(file), size, signal);
    if (!remote) { missing.push(file); continue; }
    await verifyPublishedFile(file, remote);
    onPublication('artifacts');
  }
  if (missing.length) {
    onPublication('artifacts');
    await upload([...missing, '--bucket', bucket]);
    for (const file of missing) {
      const remote = await publicObject(path.basename(file), statSync(file).size, signal);
      if (!remote) throw new Error(`Uploaded artifact is not publicly available: ${path.basename(file)}`);
      await verifyPublishedFile(file, remote);
    }
  }
  if (await readPublicFeed(signal) !== previous) throw new Error('The update feed changed during publication; retry after checking the other publisher.');
  signal?.throwIfAborted();
  onPublication('feed');
  await upload([manifest, '--bucket', bucket, '--key', WINDOWS_UPDATE_FEED, '--content-type', 'application/json']);
  if (await readPublicFeed(signal) !== candidateText) throw new Error('The public update feed did not confirm the candidate; verify publication before retrying.');
}

async function verifyPublishedFile(file: string, response: Response): Promise<void> {
  const size = statSync(file).size;
  const hash = createHash('sha256');
  let received = 0;
  for await (const chunk of response.body!) {
    received += chunk.length;
    if (received > size) throw new Error(`Different artifact already published: ${path.basename(file)}`);
    hash.update(chunk);
  }
  if (received !== size || hash.digest('hex') !== await hashUpdateFile(file))
    throw new Error(`Different artifact already published: ${path.basename(file)}`);
}

/** Check before rebuilding so an interrupted upload cannot lose its original bytes. */
export async function assertUnpublishedReleaseArtifacts(names: string[], signal?: AbortSignal): Promise<void> {
  for (const name of names) {
    if (name !== path.basename(name)) throw new Error('Expected an artifact basename.');
    const response = await fetch(`${WINDOWS_UPDATE_ORIGIN}/${name}`, {method: 'HEAD', redirect: 'error',
      cache: 'no-store', signal: signal ? AbortSignal.any([signal, AbortSignal.timeout(30_000)]) : AbortSignal.timeout(30_000)});
    if (response.status === 404) continue;
    if (!response.ok) throw new Error(`Cannot check release artifact ${name}: HTTP ${response.status}`);
    throw new Error(`${name} is already public. Preserve dist/ and retry with --skip-build; do not rebuild this version.`);
  }
}
