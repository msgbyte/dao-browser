// @vitest-environment node
import {createHash} from 'node:crypto';
import {existsSync, mkdtempSync, mkdirSync, readFileSync, rmSync, utimesSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

vi.mock('../../utils.js', async importOriginal => ({
  ...await importOriginal<typeof import('../../utils.js')>(),
  which: () => null,
  runStreaming: vi.fn(),
}));
vi.mock('../build-windows-installer.js', () => ({
  getWindowsInstallerHost: vi.fn(),
  renderWindowsInstallerHtml: () => '<html>Bundled installer interface</html>',
}));
import {runStreaming} from '../../utils.js';
import {getWindowsInstallerHost} from '../build-windows-installer.js';
import {packageWindowsInstaller} from '../package-windows.js';

const roots: string[] = [];
beforeEach(() => vi.spyOn(process, 'platform', 'get').mockReturnValue('win32'));
afterEach(() => {
  vi.restoreAllMocks();
  vi.unstubAllEnvs();
  vi.resetAllMocks();
  for (const root of roots.splice(0)) rmSync(root, {recursive: true, force: true});
});

function fixture(compilerName = 'makensis.exe') {
  const root = mkdtempSync(path.join(os.tmpdir(), 'dao-installer-'));
  roots.push(root);
  const out = path.join(root, 'out');
  const dist = path.join(root, 'dist');
  mkdirSync(out);
  mkdirSync(dist);
  writeFileSync(path.join(out, 'setup.exe'), 'dao-install-dir');
  writeFileSync(path.join(out, 'mini_installer.exe'), 'native payload');
  writeFileSync(path.join(root, compilerName), 'compiler fixture');
  const host = path.join(root, 'dao-installer-ui.exe');
  writeFileSync(host, 'native WebView2 host');
  writeFileSync(path.join(root, 'WebView2-LICENSE.txt'), 'SDK license');
  vi.mocked(getWindowsInstallerHost).mockReturnValue(host);
  vi.stubEnv('DAO_NSIS_DIR', root);
  return {out, dist};
}

describe('Windows installation wizard packaging', () => {
  it('packages on macOS with its native NSIS executable and option syntax', async () => {
    vi.spyOn(process, 'platform', 'get').mockReturnValue('darwin');
    const {out, dist} = fixture('makensis');
    vi.mocked(runStreaming).mockImplementation(async (command, args) => {
      expect(command).toBe(path.join(process.env.DAO_NSIS_DIR!, 'makensis'));
      expect(args.slice(0, 3)).toEqual(['-V2', '-INPUTCHARSET', 'UTF8']);
      expect(args).toContain(`-DPAYLOAD=${path.join(out, 'mini_installer.exe')}`);
      expect(args).toContain('-DVERSION=1.2.3');
      expect(args.some(arg => arg.startsWith('/D'))).toBe(false);
      writeFileSync(args.find(arg => arg.startsWith('-DOUTPUT='))!.slice(9), 'cross packaged wizard');
      return 0;
    });
    const artifact = await packageWindowsInstaller(out, dist, '1.2.3');
    expect(readFileSync(artifact, 'utf8')).toBe('cross packaged wizard');
  });

  it('embeds the native installer and hashes the completed wizard', async () => {
    const {out, dist} = fixture();
    vi.mocked(runStreaming).mockImplementation(async (_command, args) => {
      expect(args).toContain(`/DPAYLOAD=${path.join(out, 'mini_installer.exe')}`);
      expect(args).toContain('/DVERSION=1.2.3');
      expect(args).toContain(`/DWEBVIEW_HOST=${getWindowsInstallerHost()}`);
      const html = args.find(arg => arg.startsWith('/DWEBVIEW_HTML='))!.slice(15);
      expect(readFileSync(html, 'utf8')).toBe('<html>Bundled installer interface</html>');
      const license = args.find(arg => arg.startsWith('/DWEBVIEW_LICENSE='))!.slice(18);
      expect(readFileSync(license, 'utf8')).toBe('SDK license');
      expect(args.at(-1)).toMatch(/windows-installer[\\/]installer.nsi$/);
      const output = args.find(arg => arg.startsWith('/DOUTPUT='))!.slice(9);
      writeFileSync(output, 'compiled wizard');
      return 0;
    });
    const artifact = await packageWindowsInstaller(out, dist, '1.2.3');
    expect(readFileSync(artifact, 'utf8')).toBe('compiled wizard');
    const hash = createHash('sha256').update('compiled wizard').digest('hex');
    expect(readFileSync(artifact + '.sha256', 'utf8'))
      .toBe(`${hash}  dao-browser-1.2.3-windows-x64.exe\n`);
  });

  it('preserves an existing artifact when the wizard compiler fails', async () => {
    const {out, dist} = fixture();
    const artifact = path.join(dist, 'dao-browser-1.2.3-windows-x64.exe');
    writeFileSync(artifact, 'previous installer');
    vi.mocked(runStreaming).mockResolvedValue(1);
    await expect(packageWindowsInstaller(out, dist, '1.2.3')).rejects.toThrow(/NSIS packaging failed/);
    expect(readFileSync(artifact, 'utf8')).toBe('previous installer');
    expect(existsSync(artifact + '.sha256')).toBe(false);
  });

  it('rejects a stale embedded payload before compiling a wizard', async () => {
    const {out, dist} = fixture();
    utimesSync(path.join(out, 'mini_installer.exe'), new Date(0), new Date(0));
    await expect(packageWindowsInstaller(out, dist, '1.2.3')).rejects.toThrow(/older than setup/);
    expect(runStreaming).not.toHaveBeenCalled();
  });

  it('preserves the previous artifact and checksum when the host is stale', async () => {
    const {out, dist} = fixture();
    const artifact = path.join(dist, 'dao-browser-1.2.3-windows-x64.exe');
    writeFileSync(artifact, 'previous installer');
    writeFileSync(artifact + '.sha256', 'previous checksum');
    vi.mocked(getWindowsInstallerHost).mockImplementation(() => { throw new Error('Host is stale; rebuild required.'); });
    await expect(packageWindowsInstaller(out, dist, '1.2.3')).rejects.toThrow(/stale/);
    expect(readFileSync(artifact, 'utf8')).toBe('previous installer');
    expect(readFileSync(artifact + '.sha256', 'utf8')).toBe('previous checksum');
    expect(runStreaming).not.toHaveBeenCalled();
  });

  it('does not replace an artifact if packaging is cancelled', async () => {
    const {out, dist} = fixture();
    const artifact = path.join(dist, 'dao-browser-1.2.3-windows-x64.exe');
    writeFileSync(artifact, 'previous installer');
    const abort = new AbortController();
    vi.mocked(runStreaming).mockImplementation(async (_command, args) => {
      writeFileSync(args.find(arg => arg.startsWith('/DOUTPUT='))!.slice(9), 'cancelled build');
      abort.abort();
      return 0;
    });
    await expect(packageWindowsInstaller(out, dist, '1.2.3', false, abort.signal)).rejects.toThrow(/aborted/);
    expect(readFileSync(artifact, 'utf8')).toBe('previous installer');
  });

  it('preserves the artifact when its checksum destination cannot be replaced', async () => {
    const {out, dist} = fixture();
    const artifact = path.join(dist, 'dao-browser-1.2.3-windows-x64.exe');
    writeFileSync(artifact, 'previous installer');
    mkdirSync(artifact + '.sha256');
    vi.mocked(runStreaming).mockImplementation(async (_command, args) => {
      writeFileSync(args.find(arg => arg.startsWith('/DOUTPUT='))!.slice(9), 'new build');
      return 0;
    });
    await expect(packageWindowsInstaller(out, dist, '1.2.3')).rejects.toThrow();
    expect(readFileSync(artifact, 'utf8')).toBe('previous installer');
  });
});
