import {existsSync, mkdirSync, readFileSync, statSync, writeFileSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {ROOT_DIR, runStreaming} from './utils.js';

interface WindowsToolchainConfiguration {
  baseUrl: string;
  hash: string;
}

export interface WindowsCrossToolchain {
  toolchainRoot: string;
  sdkDir: string;
  includeDirs: string[];
  libDirs: string[];
  clangCl: string;
  lldLink: string;
  rcScript: string;
  env: NodeJS.ProcessEnv;
}

function configurationPath(rootDir: string): string {
  return path.join(rootDir, '.dao', 'windows-toolchain.json');
}

function normalizeBaseUrl(value: string): string {
  if (/^https?:\/\//i.test(value)) {
    const url = new URL(value);
    if (url.search || url.hash) throw new Error('The toolchain base URL must name a directory without query or fragment.');
    if (!url.pathname.endsWith('/')) url.pathname += '/';
    return url.href;
  }
  if (!value.trim()) throw new Error('The Windows toolchain archive directory is required.');
  return path.resolve(value.startsWith('file:') ? fileURLToPath(value) : value);
}

export function configureWindowsToolchain(
  configuration: WindowsToolchainConfiguration,
  rootDir: string = ROOT_DIR,
): void {
  if (!/^[0-9a-f]{10}$/i.test(configuration.hash)) {
    throw new Error('The Windows toolchain hash must be the 10 hexadecimal characters in <hash>.zip.');
  }
  const baseUrl = normalizeBaseUrl(configuration.baseUrl);
  if (!/^https?:\/\//i.test(baseUrl) &&
      !existsSync(path.join(baseUrl, `${configuration.hash.toLowerCase()}.zip`))) {
    throw new Error(`Windows toolchain archive not found: ${path.join(baseUrl, `${configuration.hash.toLowerCase()}.zip`)}`);
  }
  const file = configurationPath(rootDir);
  mkdirSync(path.dirname(file), {recursive: true});
  writeFileSync(file, JSON.stringify({version: 1, baseUrl, hash: configuration.hash.toLowerCase()}, null, 2) + '\n');
}

export function readWindowsToolchainRequirements(rootDir: string = ROOT_DIR): {
  hash: string;
  sdkVersion: string;
  visualStudioVersion: string;
} {
  const file = path.join(rootDir, 'engine', 'src', 'build', 'vs_toolchain.py');
  if (!existsSync(file)) {
    throw new Error('Chromium source is missing. Run npm run download first, then prepare the Windows toolchain.');
  }
  const source = readFileSync(file, 'utf8');
  const hash = source.match(/^TOOLCHAIN_HASH\s*=\s*['"]([0-9a-f]+)['"]/m)?.[1];
  const sdkVersion = source.match(/^SDK_VERSION\s*=\s*['"]([\d.]+)['"]/m)?.[1];
  const visualStudioVersion = source.match(/MSVS_VERSIONS\s*=\s*collections\.OrderedDict\(\[\s*\(['"](\d+)['"]/m)?.[1];
  if (!hash || !sdkVersion || !visualStudioVersion) {
    throw new Error('Cannot read the Windows toolchain requirements from Chromium build/vs_toolchain.py.');
  }
  return {hash, sdkVersion, visualStudioVersion};
}

export function getWindowsToolchainEnvironment(
  baseEnv: NodeJS.ProcessEnv = process.env,
  rootDir: string = ROOT_DIR,
): NodeJS.ProcessEnv {
  const env: NodeJS.ProcessEnv = {...baseEnv, DEPOT_TOOLS_WIN_TOOLCHAIN: '1'};
  const file = configurationPath(rootDir);
  let configuration: WindowsToolchainConfiguration | undefined;
  if (existsSync(file)) {
    const value = JSON.parse(readFileSync(file, 'utf8'));
    if (value.version !== 1 || typeof value.baseUrl !== 'string' ||
        typeof value.hash !== 'string' || !/^[0-9a-f]{10}$/i.test(value.hash)) {
      throw new Error(`Invalid Windows toolchain configuration: ${file}`);
    }
    configuration = value;
    if (!env.DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL) {
      env.DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL = normalizeBaseUrl(value.baseUrl);
    }
  }
  const srcDir = path.join(rootDir, 'engine', 'src');
  if (!configuration && !env.DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL &&
      !existsSync(path.join(srcDir, 'build', 'win_toolchain.json'))) {
    throw new Error('Windows cross compilation requires a packaged MSVC/Windows SDK. ' +
      'On macOS, run npx tsx scripts/cli.ts windows-toolchain setup --accept-license. ' +
      'To use an existing archive, run windows-toolchain configure --base-url <archive-directory-or-url> --hash <hash>.');
  }
  // A fresh checkout obtains the pinned hash during sync --nohooks. The caller
  // must read this environment again before running Chromium hooks.
  if (configuration && existsSync(path.join(srcDir, 'build', 'vs_toolchain.py'))) {
    const {hash} = readWindowsToolchainRequirements(rootDir);
    const key = `GYP_MSVS_HASH_${hash}`;
    if (!env[key]) env[key] = configuration.hash;
  }
  return env;
}

export async function prepareWindowsCrossToolchain(
  signal?: AbortSignal,
  rootDir: string = ROOT_DIR,
  baseEnv: NodeJS.ProcessEnv = process.env,
  checkHostTools = true,
): Promise<WindowsCrossToolchain> {
  const srcDir = path.join(rootDir, 'engine', 'src');
  const requirements = readWindowsToolchainRequirements(rootDir);
  const env = getWindowsToolchainEnvironment(baseEnv, rootDir);
  // Chromium's updater validates the archive hash and reuses an existing SDK.
  // It also creates the metadata consumed by both GN and the installer host.
  const code = await runStreaming('python3', [
    path.join(srcDir, 'build', 'vs_toolchain.py'), 'update', '--force',
  ], {cwd: srcDir, env, signal});
  if (code !== 0) throw new Error(`Windows toolchain preparation failed with exit code ${code}.`);

  const metadataFile = path.join(srcDir, 'build', 'win_toolchain.json');
  if (!existsSync(metadataFile)) throw new Error('Chromium did not generate build/win_toolchain.json. Check the Windows SDK archive configuration.');
  const metadata = JSON.parse(readFileSync(metadataFile, 'utf8'));
  const toolchainRoot = metadata.path;
  const sdkDir = metadata.win_sdk;
  if (typeof toolchainRoot !== 'string' || !path.isAbsolute(toolchainRoot) ||
      typeof sdkDir !== 'string' || !path.isAbsolute(sdkDir)) {
    throw new Error('Invalid paths in Chromium build/win_toolchain.json.');
  }
  if (metadata.version !== requirements.visualStudioVersion) {
    throw new Error(`This Chromium checkout requires a Visual Studio ${requirements.visualStudioVersion} SDK archive; ` +
      `the configured archive contains Visual Studio ${metadata.version}.`);
  }
  const setEnvFile = path.join(sdkDir, 'bin', 'SetEnv.x64.json');
  if (!existsSync(setEnvFile)) throw new Error(`Packaged Windows SDK environment is missing: ${setEnvFile}`);
  const entries = JSON.parse(readFileSync(setEnvFile, 'utf8')).env;
  if (!entries || typeof entries !== 'object') throw new Error(`Invalid packaged Windows SDK environment: ${setEnvFile}`);
  const legacy = JSON.stringify(entries.VSINSTALLDIR) === JSON.stringify([['..', '..\\']]);
  const relativeDir = legacy ? path.join(sdkDir, 'bin') : toolchainRoot;
  const directories = (key: string): string[] => {
    const values = entries[key];
    if (!Array.isArray(values) || !values.length || values.some(parts =>
      !Array.isArray(parts) || !parts.length || parts.some(part => typeof part !== 'string'))) {
      throw new Error(`Invalid ${key} paths in ${setEnvFile}`);
    }
    return values.map((parts: string[]) => path.resolve(relativeDir, ...parts.map(part => part.replaceAll('\\', path.sep))));
  };
  const includeDirs = directories('INCLUDE');
  const libDirs = directories('LIB');
  for (const directory of [...includeDirs, ...libDirs]) {
    if (!existsSync(directory) || !statSync(directory).isDirectory()) {
      throw new Error(`The packaged Windows SDK is incomplete: ${directory}`);
    }
  }
  if (!includeDirs.some(directory => directory.includes(requirements.sdkVersion))) {
    throw new Error(`The packaged Windows SDK must include SDK ${requirements.sdkVersion}.`);
  }
  // Windows headers rely on case-insensitive lookup, including on macOS.
  if (process.platform === 'darwin') {
    const headers = path.join(sdkDir, 'Include', requirements.sdkVersion, 'um');
    if (!existsSync(path.join(headers, 'WINDOWS.H'))) {
      throw new Error('Place depot_tools and its Windows SDK cache on a case-insensitive macOS volume, then run download --platform windows again.');
    }
  }
  const clangDir = path.join(srcDir, 'third_party', 'llvm-build', 'Release+Asserts', 'bin');
  const clangCl = path.join(clangDir, 'clang-cl');
  const lldLink = path.join(clangDir, 'lld-link');
  const rcScript = path.join(srcDir, 'build', 'toolchain', 'win', 'rc', 'rc.py');
  const rcBinary = path.join(path.dirname(rcScript), 'mac', 'rc');
  for (const file of checkHostTools ? [clangCl, lldLink, rcScript, rcBinary] : []) {
    if (!existsSync(file)) {
      throw new Error(`Windows cross compilation tool is missing: ${file}. Run npm run download -- --platform windows to fetch Chromium host tools.`);
    }
  }
  env.INCLUDE = includeDirs.join(';');
  env.LIB = libDirs.join(';');
  env.PATH = [...directories('PATH'), env.PATH ?? ''].join(path.delimiter);
  env.GYP_MSVS_OVERRIDE_PATH = toolchainRoot;
  env.WINDOWSSDKDIR = sdkDir;
  if (typeof metadata.wdk === 'string') env.WDK_DIR = metadata.wdk;
  return {toolchainRoot, sdkDir, includeDirs, libDirs, clangCl, lldLink, rcScript, env};
}

/** Provision SDK inputs before gclient fetches the Windows host tools. */
export async function setupWindowsToolchain(
  options: {acceptLicense?: boolean},
  signal?: AbortSignal,
  rootDir: string = ROOT_DIR,
  baseEnv: NodeJS.ProcessEnv = process.env,
): Promise<WindowsCrossToolchain> {
  const configured = existsSync(configurationPath(rootDir)) ||
    !!baseEnv.DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL ||
    existsSync(path.join(rootDir, 'engine/src/build/win_toolchain.json'));
  if (!configured) {
    if (!options.acceptLicense) {
      throw new Error('Run windows-toolchain setup --accept-license to download Microsoft build tools on macOS. ' +
        'This accepts the Microsoft Visual Studio Build Tools and Windows SDK licenses.');
    }
    const requirements = readWindowsToolchainRequirements(rootDir);
    const code = await runStreaming('python3', [
      path.join(rootDir, 'scripts/bootstrap-windows-toolchain.py'),
      '--root', rootDir, '--vs-version', requirements.visualStudioVersion,
      '--sdk-version', requirements.sdkVersion, '--accept-license',
    ], {cwd: rootDir, env: baseEnv, signal});
    if (code !== 0) throw new Error(`Windows toolchain bootstrap failed with exit code ${code}.`);
    signal?.throwIfAborted();
    const result = JSON.parse(readFileSync(path.join(rootDir, '.dao/windows-sdk/bootstrap.json'), 'utf8'));
    configureWindowsToolchain(result, rootDir);
  }
  return prepareWindowsCrossToolchain(signal, rootDir, baseEnv, false);
}
