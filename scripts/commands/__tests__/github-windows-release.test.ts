// @vitest-environment node
import {mkdtempSync, readFileSync, writeFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import path from 'node:path';
import os from 'node:os';
import {beforeEach, describe, expect, it, vi} from 'vitest';
import {windowsInstallerName} from '../package-windows.js';
import {buildGithubReleasePlan, publishGithubRelease, validateLocalInstaller} from '../github-release.js';

const fake = vi.hoisted(() => ({
  assets: [] as string[], calls: [] as string[][],
  status: 0, stderr: '', remote: '',
  isDraft: false,
}));
vi.mock('node:child_process', async importOriginal => ({
  ...await importOriginal<typeof import('node:child_process')>(),
  spawnSync: () => ({status: fake.status, stderr: fake.stderr,
    stdout: JSON.stringify({assets: fake.assets.map(name => ({name})), isDraft: fake.isDraft})}),
}));
vi.mock('../../utils.js', async importOriginal => ({
  ...await importOriginal<typeof import('../../utils.js')>(),
  which: () => 'gh',
  runStreaming: async (command: string, args: string[]) => {
    fake.calls.push([command, ...args]);
    if (args[1] === 'download') writeFileSync(args.at(-1)!, fake.remote);
    return 0;
  },
}));

function artifact() {
  const root = mkdtempSync(path.join(os.tmpdir(), 'dao-github-windows-'));
  const artifact = path.join(root, windowsInstallerName('1.2.3'));
  writeFileSync(artifact, 'complete installer');
  const digest = createHash('sha256').update('complete installer').digest('hex');
  writeFileSync(artifact + '.sha256', `${digest}  ${path.basename(artifact)}\n`);
  return artifact;
}

describe('shared desktop GitHub release', () => {
  const plan = buildGithubReleasePlan('v1.2.3', 'windows');
  beforeEach(() => {
    fake.assets = ['dao-browser-1.2.3-mac-arm64.dmg'];
    fake.calls = [];
    fake.status = 0;
    fake.stderr = '';
    fake.remote = '';
    fake.isDraft = false;
  });

  it('only appends the Windows installer/checksum without replacing macOS or release metadata', async () => {
    const asset = artifact();
    await publishGithubRelease(plan, {asset});
    expect(fake.calls).toEqual([
      ['gh', 'release', 'upload', 'v1.2.3', asset],
      ['gh', 'release', 'upload', 'v1.2.3', asset + '.sha256'],
    ]);
  });

  it('repairs an interrupted checksum upload after verifying the published binary', async () => {
    const asset = artifact();
    fake.assets.push(plan.assetName);
    fake.remote = 'complete installer';
    await publishGithubRelease(plan, {asset});
    expect(fake.calls[0].slice(0, 6)).toEqual(['gh', 'release', 'download', 'v1.2.3', '--pattern', plan.assetName]);
    expect(fake.calls[1]).toEqual(['gh', 'release', 'upload', 'v1.2.3', asset + '.sha256']);
  });

  it('rejects replacing an existing installer with different contents', async () => {
    fake.assets.push(plan.assetName, plan.assetName + '.sha256');
    fake.remote = 'different checksum';
    await expect(publishGithubRelease(plan, {asset: artifact()})).rejects.toThrow(/different installer/);
    expect(fake.calls.every(call => call[2] === 'download')).toBe(true);
  });

  it('checks an orphaned checksum before repairing a missing installer', async () => {
    const asset = artifact();
    fake.assets.push(plan.assetName + '.sha256');
    fake.remote = readFileSync(asset + '.sha256', 'utf8');
    await publishGithubRelease(plan, {asset});
    expect(fake.calls).toHaveLength(2);
    expect(fake.calls[0][2]).toBe('download');
    expect(fake.calls[1]).toEqual(['gh', 'release', 'upload', 'v1.2.3', asset]);
  });

  it('validates local content and version before any GitHub operation', () => {
    const asset = artifact();
    expect(validateLocalInstaller(plan, asset)).toBe(asset);
    writeFileSync(asset, 'modified');
    expect(() => validateLocalInstaller(plan, asset)).toThrow(/checksum mismatch/);
    expect(() => validateLocalInstaller(buildGithubReleasePlan('v1.2.4', 'windows'), asset)).toThrow(/named/);
    expect(fake.calls).toEqual([]);
  });

  it('does not interpret authentication/network failures as a missing release', async () => {
    fake.status = 1;
    fake.stderr = 'authentication failed';
    await expect(publishGithubRelease(plan, {asset: artifact()})).rejects.toThrow(/Cannot inspect/);
    expect(fake.calls).toEqual([]);
  });

  it('preserves a shared draft without publishing inaccessible download URLs', async () => {
    fake.isDraft = true;
    await expect(publishGithubRelease(plan, {asset: artifact()})).rejects.toThrow(/still a draft/);
    expect(fake.calls).toEqual([]);
  });

  it('reuses an identical published asset without uploading it again', async () => {
    const asset = artifact();
    fake.assets.push(plan.assetName, plan.assetName + '.sha256');
    fake.remote = readFileSync(asset + '.sha256', 'utf8');
    await publishGithubRelease(plan, {asset});
    expect(fake.calls).toHaveLength(1);
    expect(fake.calls[0][2]).toBe('download');
  });
});
