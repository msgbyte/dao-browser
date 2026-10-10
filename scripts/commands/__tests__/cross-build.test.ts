// @vitest-environment node
import path from 'node:path';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

const mocks = vi.hoisted(() => ({
  run: vi.fn(), capture: vi.fn(), prepare: vi.fn(), package: vi.fn(), write: vi.fn(),
  environment: vi.fn(),
  gclient: 'solutions = []\ntarget_os = ["mac"]\ncustom_setting = True\n',
  env: {DEPOT_TOOLS_WIN_TOOLCHAIN: '1', CROSS_TOOLCHAIN: 'fixture'},
}));
vi.mock('node:fs', async original => ({
  ...await original<typeof import('node:fs')>(),
  existsSync: () => true,
  readFileSync: (file: string) => file.endsWith('.gclient') ? mocks.gclient : '',
  mkdirSync: vi.fn(), writeFileSync: mocks.write,
}));
vi.mock('../../utils.js', async original => ({
  ...await original<typeof import('../../utils.js')>(),
  loadConfig: () => ({display_name: 'Dao', version: {display: '1.2.3', version: '149.0.0.0'},
    build: {target_os: 'mac', target_cpu: 'arm64'}}),
  which: (command: string) => command,
  runStreaming: mocks.run, run: mocks.capture, log: vi.fn(), success: vi.fn(), warn: vi.fn(),
}));
vi.mock('../../windows-toolchain.js', () => ({
  prepareWindowsCrossToolchain: mocks.prepare,
  getWindowsToolchainEnvironment: mocks.environment,
}));
vi.mock('../package-windows.js', () => ({packageWindowsInstaller: mocks.package}));

beforeEach(() => {
  vi.resetModules();
  vi.clearAllMocks();
  vi.spyOn(process, 'platform', 'get').mockReturnValue('darwin');
  vi.spyOn(process, 'arch', 'get').mockReturnValue('arm64');
  vi.stubEnv('DAO_BUILD_PLATFORM', undefined);
  mocks.run.mockResolvedValue(0);
  mocks.capture.mockReturnValue('0123abcd\n0123abcd');
  mocks.environment.mockReturnValue(mocks.env);
  mocks.gclient = 'solutions = []\ntarget_os = ["mac"]\ncustom_setting = True\n';
  mocks.prepare.mockResolvedValue({env: mocks.env});
  mocks.package.mockResolvedValue('dist/dao-browser-1.2.3-windows-x64.exe');
});
afterEach(() => {vi.restoreAllMocks(); vi.unstubAllEnvs();});

describe('Windows cross-build CLI on Mac', () => {
  it('uses the isolated output and hermetic environment for both GN and compilation', async () => {
    const {buildCommand} = await import('../build.js');
    await buildCommand.parseAsync(['node', 'cli', '--platform', 'windows', '--target', 'mini_installer']);
    expect(mocks.prepare).toHaveBeenCalledOnce();
    expect(mocks.run).toHaveBeenNthCalledWith(1, 'gn', ['gen', 'out/dao-win-x64'],
      expect.objectContaining({env: mocks.env}));
    expect(mocks.run).toHaveBeenNthCalledWith(2, 'autoninja',
      ['-C', 'out/dao-win-x64', 'mini_installer'], expect.objectContaining({env: mocks.env}));
    expect(mocks.write).toHaveBeenCalledWith(expect.stringContaining('dao-win-x64'),
      expect.stringContaining('target_os = "win"'));
  });

  it('fails before writing build arguments when the SDK cannot be prepared', async () => {
    mocks.prepare.mockRejectedValueOnce(new Error('Configure the Windows SDK'));
    const {buildCommand} = await import('../build.js');
    await expect(buildCommand.parseAsync(['node', 'cli', '--platform', 'windows']))
      .rejects.toThrow('Configure the Windows SDK');
    expect(mocks.run).not.toHaveBeenCalled();
    expect(mocks.write).not.toHaveBeenCalled();
  });

  it('inherits the target and isolates its debug output', async () => {
    vi.stubEnv('DAO_BUILD_PLATFORM', 'windows');
    const {buildCommand} = await import('../build.js');
    await buildCommand.parseAsync(['node', 'cli', '--debug']);
    expect(mocks.run).toHaveBeenCalledWith('gn', ['gen', 'out/dao-win-x64-debug'], expect.anything());
  });

  it('preserves imported patches at the requested revision and runs SDK hooks after dependency sync', async () => {
    const {downloadCommand} = await import('../download.js');
    await downloadCommand.parseAsync(['node', 'cli', '--platform', 'windows']);
    expect(mocks.run).toHaveBeenNthCalledWith(1, 'python3', [
      expect.stringContaining('configure-windows-checkout.py'), expect.stringContaining('.gclient'),
    ]);
    expect(mocks.run).toHaveBeenNthCalledWith(2, 'gclient',
      ['sync', '--revision', 'src@unmanaged', '--no-history', '--shallow', '--nohooks'],
      expect.anything());
    expect(mocks.run).toHaveBeenNthCalledWith(3, 'gclient', ['runhooks'],
      expect.objectContaining({env: mocks.env}));
    expect(mocks.write).not.toHaveBeenCalled();
  });

  it('still selects the requested Chromium revision when the checkout needs an update', async () => {
    mocks.capture.mockReturnValueOnce('0123abcd\nolder-commit').mockReturnValueOnce('0123abcd');
    const {downloadCommand} = await import('../download.js');
    await downloadCommand.parseAsync(['node', 'cli', '--platform', 'windows']);
    expect(mocks.run).toHaveBeenNthCalledWith(1, 'git', [
      '-C', expect.stringContaining('src'), 'fetch', '--depth=1', '--no-tags', 'origin',
      '+refs/tags/149.0.0.0:refs/tags/149.0.0.0',
    ]);
    expect(mocks.run).toHaveBeenCalledWith('gclient',
      ['sync', '--revision', 'src@0123abcd', '--no-history', '--shallow', '--nohooks'],
      expect.anything());
  });

  it('fetches the requested tag when it is not present locally', async () => {
    mocks.capture.mockImplementationOnce(() => {throw new Error('Unknown revision');})
      .mockReturnValueOnce('0123abcd');
    const {downloadCommand} = await import('../download.js');
    await downloadCommand.parseAsync(['node', 'cli', '--platform', 'windows']);
    expect(mocks.run).toHaveBeenCalledWith('gclient',
      ['sync', '--revision', 'src@0123abcd', '--no-history', '--shallow', '--nohooks'],
      expect.anything());
  });

  it('retains the requested full-history synchronization', async () => {
    const {downloadCommand} = await import('../download.js');
    await downloadCommand.parseAsync(['node', 'cli', '--platform', 'windows', '--full-history']);
    expect(mocks.run).toHaveBeenCalledWith('gclient',
      ['sync', '--revision', 'src@refs/tags/149.0.0.0', '--with_branch_heads', '--with_tags', '--nohooks'],
      expect.anything());
  });

  it('does not run hooks after a failed source sync', async () => {
    mocks.run.mockResolvedValueOnce(0).mockResolvedValueOnce(1);
    vi.spyOn(process, 'exit').mockImplementation(() => {throw new Error('exit');});
    const {downloadCommand} = await import('../download.js');
    await expect(downloadCommand.parseAsync(['node', 'cli', '--platform', 'windows']))
      .rejects.toThrow('exit');
    expect(mocks.run).toHaveBeenCalledTimes(2);
  });

  it('keeps the SDK environment when a later native Mac sync retains Windows dependencies', async () => {
    const {downloadCommand} = await import('../download.js');
    mocks.gclient += '# Dao: retain Windows dependencies for cross compilation.\n';
    await downloadCommand.parseAsync(['node', 'cli', '--platform', 'mac']);
    expect(mocks.write).not.toHaveBeenCalled();
    expect(mocks.run).toHaveBeenNthCalledWith(3, 'gclient', ['runhooks'],
      expect.objectContaining({env: mocks.env}));
  });

  it('rejects a missing SDK configuration before downloading source', async () => {
    mocks.environment.mockImplementationOnce(() => {throw new Error('Configure SDK');});
    const {downloadCommand} = await import('../download.js');
    await expect(downloadCommand.parseAsync(['node', 'cli', '--platform', 'windows']))
      .rejects.toThrow('Configure SDK');
    expect(mocks.run).not.toHaveBeenCalled();
  });

  it('packages the cross output using the explicit platform over the environment', async () => {
    vi.stubEnv('DAO_BUILD_PLATFORM', 'mac');
    const {packageCommand} = await import('../package.js');
    await packageCommand.parseAsync(['node', 'cli', '--platform', 'windows']);
    expect(mocks.package).toHaveBeenCalledWith(
      expect.stringContaining(path.join('out', 'dao-win-x64')),
      expect.any(String), '1.2.3', false);
  });
});
