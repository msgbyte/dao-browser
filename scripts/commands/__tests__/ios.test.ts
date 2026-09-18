// @vitest-environment node
import {mkdtempSync, mkdirSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {afterEach, expect, it, vi} from 'vitest';

const mocks = vi.hoisted(() => ({spawnSync: vi.fn(), root: ''}));
vi.mock('node:child_process', () => ({spawnSync: mocks.spawnSync}));
vi.mock('../../utils.js', () => ({get ROOT_DIR() { return mocks.root; }}));

afterEach(() => {
  vi.unstubAllEnvs();
  if (mocks.root) rmSync(mocks.root, {recursive: true, force: true});
});

it('validates signing inputs, exports a store-ready archive, and stops on build failure', async () => {
  mocks.root = mkdtempSync(path.join(tmpdir(), 'dao ios '));
  mkdirSync(path.join(mocks.root, 'ios'));
  const config = {
    IOS_TEAM_ID: 'AB12345678',
    IOS_PROFILE_UUID: '12345678-1234-1234-1234-123456789012',
    IOS_VERSION: '0.1.0',
    IOS_BUILD_NUMBER: '12.2',
  };
  for (const [key, value] of Object.entries(config)) vi.stubEnv(key, value);

  const run = async (...args: string[]) => {
    vi.resetModules();
    const {iosCommand} = await import('../ios.js');
    iosCommand.commands[0].exitOverride();
    return iosCommand.parseAsync(['rebuild', ...args], {from: 'user'});
  };
  mocks.spawnSync.mockReturnValue({status: 0});

  for (const [key, value] of Object.entries(config)) {
    vi.stubEnv(key, '');
    await expect(run('--archive')).rejects.toThrow(key);
    vi.stubEnv(key, value);
  }
  for (const option of ['--simulator', '--core-only', '--sources-only']) {
    await expect(run('--archive', option)).rejects.toThrow(/cannot combine/i);
  }
  vi.stubEnv('IOS_VERSION', '1.0.0$(touch bad)');
  await expect(run('--archive')).rejects.toThrow('IOS_VERSION');
  vi.stubEnv('IOS_VERSION', config.IOS_VERSION);
  expect(mocks.spawnSync).not.toHaveBeenCalled();

  await run('--archive');
  const commands = mocks.spawnSync.mock.calls;
  expect(commands.map(call => call[0])).toEqual(['swift', 'xcodegen', 'xcodebuild', 'xcodebuild']);
  expect(commands[2][1]).toEqual(expect.arrayContaining([
    '-scheme', 'DaoBrowser', '-configuration', 'Release', 'generic/platform=iOS',
    `DEVELOPMENT_TEAM=${config.IOS_TEAM_ID}`, 'CODE_SIGN_STYLE=Manual',
    'CODE_SIGN_IDENTITY=Apple Distribution', `PROVISIONING_PROFILE_SPECIFIER=${config.IOS_PROFILE_UUID}`,
    'MARKETING_VERSION=0.1.0', 'CURRENT_PROJECT_VERSION=12.2', 'archive',
  ]));
  expect(commands[2][1]).not.toContain('CODE_SIGNING_ALLOWED=NO');
  expect(commands[2][2].shell).not.toBe(true);
  expect(commands[2][2].cwd).toBe(path.join(mocks.root, 'ios'));
  expect(commands[3][1][0]).toBe('-exportArchive');
  const plist = readFileSync(path.join(mocks.root, 'ios/build/Release/ExportOptions.plist'), 'utf8');
  expect(plist).toContain('<string>app-store-connect</string>');
  expect(plist).toContain('<key>manageAppVersionAndBuildNumber</key><false/>');
  expect(plist).toContain('<key>testFlightInternalTestingOnly</key><false/>');
  expect(plist).toContain(`<key>com.msgbyte.dao</key><string>${config.IOS_PROFILE_UUID}</string>`);

  mocks.spawnSync.mockClear();
  mocks.spawnSync.mockReturnValueOnce({status: 0}).mockReturnValueOnce({status: 0})
    .mockReturnValueOnce({status: 65});
  await expect(run('--archive')).rejects.toThrow(/xcodebuild.*65/);
  expect(mocks.spawnSync).toHaveBeenCalledTimes(3);

  mocks.spawnSync.mockClear();
  await run('--simulator');
  expect(mocks.spawnSync.mock.calls[2][1]).toEqual(expect.arrayContaining([
    '-configuration', 'Debug', '-sdk', 'iphonesimulator', 'CODE_SIGNING_ALLOWED=NO', 'build',
  ]));
});

it('installs only unexpired App Store profiles for this bundle', async () => {
  const {spawnSync} = await vi.importActual<typeof import('node:child_process')>('node:child_process');
  const workflow = readFileSync('.github/workflows/publish-ios-testflight.yml', 'utf8');
  const script = workflow.match(/python3 - <<'PY'\n([\s\S]*?)\n          PY/)![1]
    .replace(/^          /gm, '');
  const result = spawnSync('python3', ['-c', `
import copy, datetime, os, pathlib, plistlib, tempfile
script = ${JSON.stringify(script)}
valid = {
    'TeamIdentifier': ['AB12345678'],
    'UUID': '12345678-1234-1234-1234-123456789012',
    'ApplicationIdentifierPrefix': ['AB12345678'],
    'ExpirationDate': datetime.datetime.now() + datetime.timedelta(days=1),
    'Entitlements': {'application-identifier': 'AB12345678.com.msgbyte.dao', 'beta-reports-active': True},
}
for invalid in [None, 'bundle', 'expired', 'adhoc', 'enterprise', 'development', 'uuid']:
    with tempfile.TemporaryDirectory(prefix='dao-ios-profile-') as temporary:
        root = pathlib.Path(temporary)
        pathlib.Path.home = classmethod(lambda cls: root / 'home')
        directory = root / 'dao-ios-signing'
        directory.mkdir()
        os.environ['RUNNER_TEMP'] = temporary
        os.environ['GITHUB_ENV'] = str(root / 'env')
        profile = copy.deepcopy(valid)
        if invalid == 'bundle': profile['Entitlements']['application-identifier'] = 'AB12345678.com.msgbyte.dao.ios'
        if invalid == 'expired': profile['ExpirationDate'] = datetime.datetime(2000, 1, 1)
        if invalid == 'adhoc': profile['ProvisionedDevices'] = ['device']
        if invalid == 'enterprise': profile['ProvisionsAllDevices'] = True
        if invalid == 'development': profile['Entitlements']['get-task-allow'] = True
        if invalid == 'uuid': profile['UUID'] = '../other-profile'
        with (directory / 'profile.plist').open('wb') as output: plistlib.dump(profile, output)
        (directory / 'profile.mobileprovision').write_bytes(b'signed-profile')
        try:
            exec(script)
        except AssertionError:
            assert invalid is not None
            assert not (root / 'env').exists()
            assert not (root / 'home').exists()
        else:
            assert invalid is None, f'Accepted invalid profile: {invalid}'
            assert (root / 'env').read_text() == 'IOS_TEAM_ID=AB12345678\\nIOS_PROFILE_UUID=12345678-1234-1234-1234-123456789012\\n'
            installed = list((root / 'home').rglob('*.mobileprovision'))
            assert len(installed) == 1 and installed[0].read_bytes() == b'signed-profile'
`], {encoding: 'utf8'});
  expect(result.stderr).toBe('');
  expect(result.status).toBe(0);
});
