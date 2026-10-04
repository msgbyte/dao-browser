// @vitest-environment node
import {mkdtempSync, mkdirSync, readFileSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {describe, expect, it} from 'vitest';
import {packageWindowsInstaller} from '../package-windows.js';
import {
  buildReleaseApplication, packageReleaseArtifact, plannedReleasePhases,
  runRelease, desktopReleaseSourcesMatch, hasUnreleasedDesktopChanges,
  type ReleaseDependencies, type ReleasePhaseContext,
} from '../release.js';
import {buildGithubReleasePlan} from '../github-release.js';
import {validateLocalInstaller} from '../github-release.js';

describe('Windows desktop release', () => {
  it('shares the desktop tag while selecting a Windows installer', () => {
    expect(buildGithubReleasePlan('v1.2.3', 'windows')).toEqual({
      tag: 'v1.2.3', assetName: 'dao-browser-1.2.3-windows-x64.exe',
      sourceUrl: 'https://dao-release.msgbyte.com/dao-browser-1.2.3-windows-x64.exe',
    });
    expect(buildGithubReleasePlan('v1.2.3').assetName)
      .toBe('dao-browser-1.2.3-mac-arm64.dmg');
  });

  it('builds mini_installer and packages without Apple signing or Sparkle', async () => {
    expect(plannedReleasePhases({platform: 'windows'})).toEqual([
      'import', 'build', 'package', 'upload', 'metadata', 'tag',
    ]);
    const calls: string[][] = [];
    const runner = async (cmd: string, args: string[]) => {
      calls.push([cmd, ...args]);
      return 0;
    };
    const context = {options: {platform: 'windows'}} as ReleasePhaseContext;
    await buildReleaseApplication(context, runner);
    await packageReleaseArtifact(context, runner);
    expect(calls).toEqual([
      ['npx', 'tsx', 'scripts/cli.ts', 'build', '--target', 'mini_installer'],
      ['npx', 'tsx', 'scripts/cli.ts', 'package'],
    ]);
  });

  it('copies the complete installer and writes a checksum, rejecting missing builds', () => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-win-package-'));
    const out = path.join(root, 'out');
    const dist = path.join(root, 'dist');
    mkdirSync(out);
    expect(() => packageWindowsInstaller(out, dist, '1.2.3')).toThrow(/mini_installer/);
    writeFileSync(path.join(out, 'mini_installer.exe'), 'installer fixture');
    const artifact = packageWindowsInstaller(out, dist, '1.2.3');
    expect(path.basename(artifact)).toBe('dao-browser-1.2.3-windows-x64.exe');
    expect(readFileSync(artifact, 'utf8')).toBe('installer fixture');
    expect(readFileSync(artifact + '.sha256', 'utf8'))
      .toMatch(/^[a-f0-9]{64}  dao-browser-1\.2\.3-windows-x64\.exe\n$/);
    const debug = packageWindowsInstaller(out, dist, '1.2.3', true);
    expect(() => validateLocalInstaller(buildGithubReleasePlan('v1.2.3', 'windows'), debug)).toThrow(/named/);
  });

  it('reuses the desktop version and tag without rewriting macOS metadata', async () => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-win-release-'));
    mkdirSync(path.join(root, 'website/public'), {recursive: true});
    const config = {version: {display: '1.2.3', version: '149.0.1.2'},
      build: {target_os: 'mac', target_cpu: 'arm64'}};
    writeFileSync(path.join(root, 'dao.json'), JSON.stringify(config));
    const info = {version: '1.2.3', platforms: {macArm64: {url: 'keep.dmg'}}};
    writeFileSync(path.join(root, 'website/public/info.json'), JSON.stringify(info));
    const phases: string[] = [];
    const deps: ReleaseDependencies = {
      rootDir: root, env: {}, now: () => new Date(), head: () => 'a'.repeat(40),
      tagState: () => ({exists: true, commit: 'a'.repeat(40), objectId: 'b'.repeat(40)}),
      createTag: () => {throw new Error('must reuse tag');},
      deleteTag: () => {throw new Error('must preserve tag');},
      runPhase: async phase => {phases.push(phase);},
    };
    const result = await runRelease({platform: 'windows', skipUpload: true}, deps);
    expect(result.newVersion).toBe('1.2.3');
    expect(JSON.parse(readFileSync(path.join(root, 'dao.json'), 'utf8'))).toEqual(config);
    expect(JSON.parse(readFileSync(path.join(root, 'website/public/info.json'), 'utf8'))).toEqual(info);
    expect(phases).not.toContain('appcast');
    expect(phases).not.toContain('notarize');

    await runRelease({platform: 'windows'}, deps);
    const published = JSON.parse(readFileSync(path.join(root, 'website/public/info.json'), 'utf8'));
    expect(published.platforms.macArm64).toEqual(info.platforms.macArm64);
    expect(published.platforms.win.version).toBe('1.2.3');
    expect(published.platforms.win.url).toBe('https://github.com/msgbyte/dao-browser/releases/download/v1.2.3/dao-browser-1.2.3-windows-x64.exe');
    deps.tagState = () => ({exists: false});
    await expect(runRelease({platform: 'windows'}, deps)).rejects.toThrow(/tag v1.2.3 is missing/);
  });

  it('allows the release metadata commit but rejects different source or engine versions', () => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-desktop-sources-'));
    const git = (...args: string[]) => execFileSync('git', args, {cwd: root, encoding: 'utf8'}).trim();
    git('init', '--quiet');
    git('config', 'user.name', 'Dao Test');
    git('config', 'user.email', 'test@example.invalid');
    const config = {version: {display: '1.2.2', version: '149.0.1.2'}};
    const writeConfig = () => writeFileSync(path.join(root, 'dao.json'), JSON.stringify(config));
    const commit = () => {git('add', '.'); git('commit', '--quiet', '-m', 'test(release): fixture'); return git('rev-parse', 'HEAD');};
    writeConfig();
    const source = commit();
    expect(hasUnreleasedDesktopChanges(root)).toBe(false);
    config.version.display = '1.2.3';
    writeConfig();
    expect(hasUnreleasedDesktopChanges(root)).toBe(false);
    expect(desktopReleaseSourcesMatch(root, source, commit())).toBe(true);
    config.version.version = '150.0.1.2';
    writeConfig();
    expect(hasUnreleasedDesktopChanges(root)).toBe(true);
    expect(desktopReleaseSourcesMatch(root, source, commit())).toBe(false);
    config.version.version = '149.0.1.2';
    writeConfig();
    writeFileSync(path.join(root, 'code.ts'), 'changed');
    expect(hasUnreleasedDesktopChanges(root)).toBe(true);
    expect(desktopReleaseSourcesMatch(root, source, commit())).toBe(false);
    writeFileSync(path.join(root, 'code.ts'), 'dirty');
    expect(hasUnreleasedDesktopChanges(root)).toBe(true);
    git('add', 'code.ts');
    expect(hasUnreleasedDesktopChanges(root)).toBe(true);
  }, 30_000);
});
