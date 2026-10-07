// @vitest-environment node
import {mkdtempSync, mkdirSync, readFileSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {describe, expect, it} from 'vitest';
import {packageWindowsInstaller} from '../package-windows.js';
import {
  buildReleaseApplication, importReleaseSources, packageReleaseArtifact, plannedReleasePhases,
  runRelease, desktopReleaseSourcesMatch, hasUnreleasedDesktopChanges,
  type ReleaseDependencies, type ReleasePhaseContext,
} from '../release.js';
import {buildGithubReleasePlan} from '../github-release.js';

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
      ['npx', 'tsx', 'scripts/cli.ts', 'build', '--platform', 'windows', '--target', 'mini_installer'],
      ['npx', 'tsx', 'scripts/cli.ts', 'build', '--platform', 'windows', '--target', 'dao_installer_ui'],
      ['npx', 'tsx', 'scripts/cli.ts', 'package', '--platform', 'windows'],
    ]);
  });

  it.each([
    ['darwin', 'arm64'], ['darwin', 'x64'], ['win32', 'x64'],
  ] as const)('routes a Windows release on %s/%s through every Windows build phase', async (hostPlatform, hostArch) => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-cross-release-'));
    mkdirSync(path.join(root, 'website/public'), {recursive: true});
    const config = {version: {display: '1.2.3', version: '149.0.1.2'},
      build: {target_os: 'mac', target_cpu: 'arm64'}};
    const info = {version: '1.2.3', platforms: {macArm64: {url: 'keep.dmg'}}};
    writeFileSync(path.join(root, 'dao.json'), JSON.stringify(config));
    writeFileSync(path.join(root, 'website/public/info.json'), JSON.stringify(info));
    writeFileSync(path.join(root, 'website/public/appcast.xml'), '<rss/>');
    const commands: string[][] = [];
    const runner = async (cmd: string, args: string[]) => {
      commands.push([cmd, ...args]);
      return 0;
    };
    const phases: string[] = [];
    const deps: ReleaseDependencies = {
      hostPlatform, hostArch, rootDir: root, env: {}, now: () => new Date(),
      head: () => 'a'.repeat(40),
      tagState: () => ({exists: true, commit: 'a'.repeat(40), objectId: 'b'.repeat(40)}),
      createTag: () => {throw new Error('must reuse tag');},
      deleteTag: () => {throw new Error('must preserve tag');},
      runPhase: async (phase, context) => {
        phases.push(phase);
        if (phase === 'import') await importReleaseSources(context, runner);
        if (phase === 'build') await buildReleaseApplication(context, runner);
        if (phase === 'package') {
          expect(path.basename(context.dmgPath)).toBe('dao-browser-1.2.3-windows-x64.exe');
          await packageReleaseArtifact(context, runner);
        }
      },
    };

    expect(await runRelease({platform: 'windows', skipUpload: true}, deps))
      .toEqual({oldVersion: '1.2.3', newVersion: '1.2.3'});
    expect(phases).toEqual(['import', 'build', 'package', 'tag']);
    expect(commands).toEqual([
      ['npx', 'tsx', 'scripts/cli.ts', 'import', '--platform', 'windows'],
      ['npx', 'tsx', 'scripts/cli.ts', 'build', '--platform', 'windows', '--target', 'mini_installer'],
      ['npx', 'tsx', 'scripts/cli.ts', 'build', '--platform', 'windows', '--target', 'dao_installer_ui'],
      ['npx', 'tsx', 'scripts/cli.ts', 'package', '--platform', 'windows'],
    ]);
    expect(JSON.parse(readFileSync(path.join(root, 'dao.json'), 'utf8'))).toEqual(config);
    expect(JSON.parse(readFileSync(path.join(root, 'website/public/info.json'), 'utf8'))).toEqual(info);
    expect(readFileSync(path.join(root, 'website/public/appcast.xml'), 'utf8')).toBe('<rss/>');

    for (const [unsupportedPlatform, unsupportedArch] of [['linux', 'x64'], ['win32', 'arm64'], ['darwin', 'ia32']] as const) {
      await expect(runRelease({platform: 'windows', skipUpload: true}, {
        ...deps, hostPlatform: unsupportedPlatform, hostArch: unsupportedArch,
      })).rejects.toMatchObject({phase: 'preflight', message: expect.stringContaining('Windows x64 or macOS')});
    }
    expect(phases).toEqual(['import', 'build', 'package', 'tag']);
  });

  it('rejects a native installer built without custom directory support', async () => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-win-package-'));
    const out = path.join(root, 'out');
    const dist = path.join(root, 'dist');
    mkdirSync(out);
    await expect(async () => packageWindowsInstaller(out, dist, '1.2.3')).rejects.toThrow(/mini_installer/);
    writeFileSync(path.join(out, 'mini_installer.exe'), 'installer fixture');
    writeFileSync(path.join(out, 'setup.exe'), 'old native setup');
    await expect(async () => packageWindowsInstaller(out, dist, '1.2.3'))
      .rejects.toThrow(/custom installation directory/);
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
    mkdirSync(path.join(root, 'docs/changelog'), {recursive: true});
    const changelog = path.join(root, 'docs/changelog/en.md');
    writeFileSync(changelog, '### [Unreleased]\n\n- Pending change.\n');
    const source = commit();
    expect(hasUnreleasedDesktopChanges(root)).toBe(false);
    config.version.display = '1.2.3';
    writeConfig();
    writeFileSync(changelog, '### [Unreleased]\n\n### [1.2.3] - 2026-10-08\n\n- Pending change.\n');
    expect(hasUnreleasedDesktopChanges(root)).toBe(false);
    git('add', 'docs/changelog/en.md');
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
