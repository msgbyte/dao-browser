import {Command} from 'commander';
import {existsSync, mkdirSync, readFileSync, readdirSync} from 'node:fs';
import path from 'node:path';
import {ROOT_DIR, log, runStreaming, success, which} from '../utils.js';
import {
  configureWindowsToolchain,
  prepareWindowsCrossToolchain,
} from '../windows-toolchain.js';

export const windowsToolchainCommand = new Command('windows-toolchain')
  .description('Provision the Windows SDK for source cross compilation on macOS');

windowsToolchainCommand.addCommand(new Command('configure')
  .description('Remember a private MSVC/Windows SDK archive location for this checkout')
  .requiredOption('--base-url <directory-or-url>', 'Directory containing <hash>.zip, or its HTTP(S) base URL')
  .requiredOption('--hash <hash>', 'The 10-character hash in the archive filename')
  .action((options: {baseUrl: string; hash: string}) => {
    configureWindowsToolchain(options);
    success('Windows toolchain configuration saved to .dao/windows-toolchain.json');
  }));

windowsToolchainCommand.addCommand(new Command('setup')
  .description('Download and verify the configured Windows SDK using Chromium tooling')
  .action(async () => {
    if (process.platform !== 'darwin') throw new Error('windows-toolchain setup is intended for the macOS cross compilation host.');
    const toolchain = await prepareWindowsCrossToolchain();
    success(`Windows toolchain ready: ${toolchain.toolchainRoot}`);
  }));

windowsToolchainCommand.addCommand(new Command('export')
  .description('Package an installed Windows SDK once for transfer to the Mac')
  .option('--output <directory>', 'Empty destination directory', path.join(ROOT_DIR, '.dao', 'windows-sdk-export'))
  .option('--sdk-version <version>', 'SDK version to package (defaults to the depot_tools requirement)')
  .action(async (options: {output: string; sdkVersion?: string}) => {
    if (process.platform !== 'win32') throw new Error('SDK export requires Windows with Visual Studio and Windows SDK installed. Compilation runs on the Mac after this one-time export.');
    const gclient = which('gclient');
    if (!gclient) throw new Error('depot_tools must be available in PATH to export the Windows SDK.');
    const packager = path.join(path.dirname(gclient), 'win_toolchain', 'package_from_installed.py');
    if (!existsSync(packager)) throw new Error(`The depot_tools SDK packager is missing: ${packager}`);
    const source = readFileSync(packager, 'utf8');
    const visualStudioVersion = source.match(/^SUPPORTED_VS_VERSION\s*=\s*['"](\d+)['"]/m)?.[1];
    const sdkVersion = options.sdkVersion ?? source.match(/^EXPECTED_SDK_VERSION\s*=\s*['"]([\d.]+)['"]/m)?.[1];
    if (!visualStudioVersion || !sdkVersion || !/^\d+(?:\.\d+){3}$/.test(sdkVersion)) {
      throw new Error('Cannot determine supported Visual Studio/SDK versions from the depot_tools packager.');
    }
    const output = path.resolve(options.output);
    if (existsSync(output) && readdirSync(output).length) {
      throw new Error(`SDK export requires an empty destination directory: ${output}`);
    }
    mkdirSync(output, {recursive: true});
    log(`Packaging Visual Studio ${visualStudioVersion} and Windows SDK ${sdkVersion}; the matching C++ workload, ATL/MFC, ARM64 tools, and SDK debuggers must be installed.`);
    const code = await runStreaming('python3', [packager, visualStudioVersion, '-w', sdkVersion], {cwd: output});
    if (code !== 0) throw new Error(`Windows SDK export failed with exit code ${code}. Install the matching Visual Studio/SDK components listed by the packager and retry with an empty output directory.`);
    const archives = readdirSync(output).filter(file => /^[0-9a-f]{10}\.zip$/i.test(file));
    if (archives.length !== 1) throw new Error(`SDK export did not produce one <hash>.zip archive in ${output}.`);
    success(`Windows SDK exported: ${path.join(output, archives[0])}`);
    log(`Copy the archive to the Mac, then run windows-toolchain configure --base-url <archive-directory> --hash ${archives[0].slice(0, -4)}.`);
  }));
