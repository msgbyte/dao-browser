// @vitest-environment node
import {describe, expect, it} from 'vitest';
import {Command} from 'commander';
import {loadConfig} from '../../utils.js';
import {createLaunchSpec, startCommand, type LaunchOptions} from '../start.js';

describe('native browser launch', () => {
  const config = loadConfig();

  it('forwards Chromium switches, their values and URLs through the launch parser', async () => {
    const browserArgs = [
      '--remote-debugging-port=9222', '--disable-features', 'FeatureA',
      '--user-data-dir', '/tmp/Dao profile', 'https://example.com/?x=1&y=2',
    ];
    let parsed: LaunchOptions | undefined;
    startCommand.exitOverride().action((urls: string[], options: Omit<LaunchOptions, 'urls'>) => {
      parsed = {...options, urls};
    });
    const program = new Command().enablePositionalOptions().addCommand(startCommand);
    await program.parseAsync(['start', '--debug', '--view', ...browserArgs], {from: 'user'});
    expect(parsed).toEqual({debug: true, view: true, urls: browserArgs});

    const urlFirst = ['https://example.com', ...browserArgs];
    await program.parseAsync(['start', '--debug', '--view', ...urlFirst], {from: 'user'});
    expect(parsed).toEqual({debug: true, view: true, urls: urlFirst});
  });

  it('isolates Windows profiles and preserves URLs as literal arguments', () => {
    const url = 'https://example.com/?q=a&value=%20';
    const debug = createLaunchSpec(config, {os: 'win', cpu: 'x64'},
        {debug: true, urls: [url]}, 'D:\\Dao Project\\engine', 'C:\\Local Data');
    const release = createLaunchSpec(config, {os: 'win', cpu: 'x64'},
        {urls: []}, 'D:\\Dao Project\\engine', 'C:\\Local Data');
    expect(debug.command).toBe('D:\\Dao Project\\engine\\src\\out\\dao-debug\\chrome.exe');
    expect(release.command).toBe('D:\\Dao Project\\engine\\src\\out\\dao\\chrome.exe');
    expect(debug.args).toContain('--user-data-dir=C:\\Local Data\\Dao Debug\\User Data');
    expect(release.args).toContain('--user-data-dir=C:\\Local Data\\Dao\\User Data');
    expect(debug.args).toContain('--enable-logging=stderr');
    expect(debug.args.at(-1)).toBe(url);
    expect(debug.args).not.toContain('--use-mock-keychain');
  });

  it('preserves macOS app names and launch mechanisms', () => {
    const target = {os: 'mac', cpu: 'arm64'} as const;
    const debug = createLaunchSpec(config, target, {debug: true, view: true, urls: []}, '/engine');
    expect(debug.command).toBe('/engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug');
    expect(debug.args).toContain('--use-mock-keychain');
    expect(debug.args).toContain('--enable-ui-devtools=9223');
    expect(debug.args).toContain('--v=1');
    expect(debug.args).not.toContain('--log-level=2');
    const normalDebug = createLaunchSpec(
        config, target, {debug: true, urls: []}, '/engine');
    expect(normalDebug.args).toContain('--log-level=2');
    expect(createLaunchSpec(config, target, {urls: []}, '/engine')).toEqual({
      command: 'open', args: ['/engine/src/out/dao/Dao.app', '--args', '--use-mock-keychain'],
    });
    expect(createLaunchSpec(config, target,
        {debug: true, little: true, urls: ['https://example.com']}, '/engine')).toEqual({
      command: 'open', args: ['-a', '/engine/src/out/dao-debug/Dao Debug.app',
        'https://example.com', '--args', '--use-mock-keychain'],
    });
  });

  it('rejects unsupported Windows launch modes and missing profile roots', () => {
    const target = {os: 'win', cpu: 'x64'} as const;
    expect(() => createLaunchSpec(config, target, {urls: []}, 'D:\\engine'))
        .toThrow(/LOCALAPPDATA/);
    expect(() => createLaunchSpec(config, target, {little: true, urls: []},
        'D:\\engine', 'C:\\Local')).toThrow(/Little Dao/);
  });

  it('keeps Little Dao browser arguments after the macOS open separator', () => {
    expect(createLaunchSpec(config, {os: 'mac', cpu: 'arm64'}, {
      debug: true, little: true,
      urls: ['https://bing.com', '--remote-debugging-port=9222',
        '--user-data-dir', '/tmp/Dao profile', 'https://example.com'],
    }, '/engine')).toEqual({
      command: 'open', args: ['-a', '/engine/src/out/dao-debug/Dao Debug.app',
        'https://bing.com', '--args', '--use-mock-keychain',
        '--remote-debugging-port=9222', '--user-data-dir', '/tmp/Dao profile',
        'https://example.com'],
    });
  });
});
