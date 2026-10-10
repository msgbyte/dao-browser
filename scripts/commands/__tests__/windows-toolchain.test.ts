// @vitest-environment node
import {mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

vi.mock('../../utils.js', async importOriginal => ({
  ...await importOriginal<typeof import('../../utils.js')>(),
  runStreaming: vi.fn().mockResolvedValue(0),
}));
import {runStreaming} from '../../utils.js';
import {
  configureWindowsToolchain,
  getWindowsToolchainEnvironment,
  prepareWindowsCrossToolchain,
  setupWindowsToolchain,
} from '../../windows-toolchain.js';

let root: string;
function write(relative: string, content: string) {
  const file = path.join(root, relative);
  mkdirSync(path.dirname(file), {recursive: true});
  writeFileSync(file, content);
}
function checkout() {
  write('engine/src/build/vs_toolchain.py', [
    "TOOLCHAIN_HASH = 'e66617bc68'",
    "SDK_VERSION = '10.0.26100.0'",
    "MSVS_VERSIONS = collections.OrderedDict([('2026', '18.0')])",
  ].join('\n'));
}
function preparedToolchain() {
  checkout();
  const toolchainRoot = path.join(root, 'sdk');
  const sdkDir = path.join(toolchainRoot, 'Windows Kits', '10');
  write('engine/src/build/win_toolchain.json', JSON.stringify({
    path: toolchainRoot, win_sdk: sdkDir, version: '2026', runtime_dirs: [],
  }));
  write('sdk/Windows Kits/10/bin/SetEnv.x64.json', JSON.stringify({env: {
    VSINSTALLDIR: [['.\\']],
    INCLUDE: [['VC', 'include'], ['Windows Kits', '10', 'Include', '10.0.26100.0', 'um']],
    LIB: [['VC', 'lib', 'x64']],
    PATH: [['VC', 'bin']],
  }}));
  for (const dir of ['sdk/VC/include', 'sdk/VC/lib/x64', 'sdk/VC/bin']) {
    mkdirSync(path.join(root, dir), {recursive: true});
  }
  write('sdk/Windows Kits/10/Include/10.0.26100.0/um/Windows.h', '');
  for (const file of ['third_party/llvm-build/Release+Asserts/bin/clang-cl',
    'third_party/llvm-build/Release+Asserts/bin/lld-link',
    'build/toolchain/win/rc/rc.py', 'build/toolchain/win/rc/mac/rc']) {
    write(`engine/src/${file}`, 'host tool');
  }
  return {toolchainRoot, sdkDir};
}

beforeEach(() => {
  root = mkdtempSync(path.join(os.tmpdir(), 'dao-toolchain-test-'));
  vi.mocked(runStreaming).mockClear();
  vi.mocked(runStreaming).mockResolvedValue(0);
});
afterEach(() => rmSync(root, {recursive: true, force: true}));

describe('Windows cross toolchain provisioning', () => {
  it('bootstraps the SDK on Mac without requiring downloaded Chromium resource tools', async () => {
    checkout();
    vi.mocked(runStreaming).mockImplementation(async (_command, args) => {
      if (String(args?.[0]).endsWith('bootstrap-windows-toolchain.py')) {
        write('.dao/windows-sdk/archives/0123456789.zip', 'archive');
        write('.dao/windows-sdk/bootstrap.json', JSON.stringify({
          baseUrl: path.join(root, '.dao/windows-sdk/archives'), hash: '0123456789',
        }));
      } else {
        preparedToolchain();
        rmSync(path.join(root, 'engine/src/build/toolchain/win/rc/mac/rc'));
      }
      return 0;
    });
    await setupWindowsToolchain({acceptLicense: true}, undefined, root, {});
    expect(getWindowsToolchainEnvironment({}, root).GYP_MSVS_HASH_e66617bc68).toBe('0123456789');
    expect(runStreaming).toHaveBeenCalledTimes(2);
  });

  it('does not replace a configured private SDK or require license acceptance again', async () => {
    preparedToolchain();
    configureWindowsToolchain({baseUrl: 'https://private.test/sdk', hash: '0123456789'}, root);
    await setupWindowsToolchain({}, undefined, root, {});
    expect(runStreaming).toHaveBeenCalledTimes(1);
    expect(getWindowsToolchainEnvironment({}, root).DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL).toBe('https://private.test/sdk/');
  });

  it('requires explicit license acceptance before bootstrapping and preserves failure', async () => {
    checkout();
    await expect(setupWindowsToolchain({}, undefined, root, {})).rejects.toThrow(/--accept-license/);
    expect(runStreaming).not.toHaveBeenCalled();
    vi.mocked(runStreaming).mockResolvedValueOnce(1);
    await expect(setupWindowsToolchain({acceptLicense: true}, undefined, root, {})).rejects.toThrow(/bootstrap.*1/i);
    expect(() => getWindowsToolchainEnvironment({}, root)).toThrow();
  });

  it('configures an archive before checkout and maps the pinned hash after checkout', () => {
    configureWindowsToolchain({baseUrl: 'https://example.test/sdk', hash: '0123456789'}, root);
    expect(getWindowsToolchainEnvironment({}, root)).toMatchObject({
      DEPOT_TOOLS_WIN_TOOLCHAIN: '1',
      DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL: 'https://example.test/sdk/',
    });
    checkout();
    expect(getWindowsToolchainEnvironment({}, root).GYP_MSVS_HASH_e66617bc68).toBe('0123456789');
    const configuration = JSON.parse(readFileSync(path.join(root, '.dao/windows-toolchain.json'), 'utf8'));
    expect(configuration.hash).toBe('0123456789');
  });

  it('preserves explicit upstream environment overrides and forces the hermetic toolchain', () => {
    checkout();
    configureWindowsToolchain({baseUrl: 'https://example.test/sdk/', hash: '0123456789'}, root);
    const env = getWindowsToolchainEnvironment({
      DEPOT_TOOLS_WIN_TOOLCHAIN: '0',
      DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL: 'https://mirror.test/tools/',
      GYP_MSVS_HASH_e66617bc68: 'abcdefghij',
    }, root);
    expect(env.DEPOT_TOOLS_WIN_TOOLCHAIN).toBe('1');
    expect(env.DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL).toBe('https://mirror.test/tools/');
    expect(env.GYP_MSVS_HASH_e66617bc68).toBe('abcdefghij');
  });

  it('rejects missing configuration and malformed archive hashes before running any tools', () => {
    checkout();
    expect(() => getWindowsToolchainEnvironment({}, root)).toThrow(/windows-toolchain configure/);
    expect(() => configureWindowsToolchain({baseUrl: 'https://example.test/', hash: '../bad'}, root)).toThrow(/hash/i);
    expect(runStreaming).not.toHaveBeenCalled();
  });

  it('reuses official preparation and exposes SDK paths and host tools without compiling', async () => {
    const expected = preparedToolchain();
    const result = await prepareWindowsCrossToolchain(undefined, root, {PATH: '/host/bin'});
    expect(runStreaming).toHaveBeenCalledExactlyOnceWith('python3', [
      path.join(root, 'engine/src/build/vs_toolchain.py'), 'update', '--force',
    ], expect.objectContaining({cwd: path.join(root, 'engine/src')}));
    expect(result).toMatchObject(expected);
    expect(result.includeDirs).toContain(path.join(root, 'sdk/VC/include'));
    expect(result.env.INCLUDE).toBe(result.includeDirs.join(';'));
    expect(result.env.LIB).toBe(result.libDirs.join(';'));
    expect(result.env.PATH).toContain('/host/bin');
    expect(result.clangCl).toBe(path.join(root, 'engine/src/third_party/llvm-build/Release+Asserts/bin/clang-cl'));
  });

  it('fails preparation on a failed downloader or missing cross resource compiler', async () => {
    preparedToolchain();
    vi.mocked(runStreaming).mockResolvedValueOnce(7);
    await expect(prepareWindowsCrossToolchain(undefined, root, {})).rejects.toThrow(/exit code 7/);
    rmSync(path.join(root, 'engine/src/build/toolchain/win/rc/mac/rc'));
    await expect(prepareWindowsCrossToolchain(undefined, root, {})).rejects.toThrow(/download -- --platform windows/);
  });

  it('rejects SDK archives for a different Visual Studio generation', async () => {
    preparedToolchain();
    const metadataPath = path.join(root, 'engine/src/build/win_toolchain.json');
    const metadata = JSON.parse(readFileSync(metadataPath, 'utf8'));
    writeFileSync(metadataPath, JSON.stringify({...metadata, version: '2022'}));
    await expect(prepareWindowsCrossToolchain(undefined, root, {})).rejects.toThrow(/Visual Studio 2026/);
  });
});
