// @vitest-environment node
import {afterEach, describe, expect, it, vi} from 'vitest';

// Never access engine/ or launch build tools in these CLI tests.
vi.mock('node:fs', () => ({
  existsSync: () => true,
  readFileSync: () => '',
  mkdirSync: vi.fn(),
  writeFileSync: vi.fn(),
}));
vi.mock('../../utils.js', async (importOriginal) => ({
  ...await importOriginal<typeof import('../../utils.js')>(),
  loadConfig: () => ({version: {display: '1.2.3'}, build: {target_os: 'win', target_cpu: 'x64'}}),
  resolveBuildTarget: () => ({os: 'win', cpu: 'x64'}),
  which: (command: string) => command,
  runStreaming: vi.fn(),
  log: vi.fn(), success: vi.fn(), error: vi.fn(),
}));

import {buildCommand} from '../build.js';
import {createAbortError, runStreaming} from '../../utils.js';

const originalExitCode = process.exitCode;
afterEach(() => { process.exitCode = originalExitCode; });

describe.runIf(process.platform === 'win32')('build cancellation', () => {
  it('can verify the release installer using the existing release cache', async () => {
    vi.mocked(runStreaming).mockReset().mockResolvedValue(0);
    await buildCommand.parseAsync(['node', 'cli', '--debug', '--release', '--target', 'mini_installer', '-j', '2']);
    expect(runStreaming).toHaveBeenCalledWith('gn', ['gen', 'out/dao'], expect.anything());
    expect(runStreaming).toHaveBeenCalledWith('autoninja', ['-j2', '-C', 'out/dao', 'mini_installer'], expect.anything());
  });
  it.each([
    ['SIGINT', 1, 130], ['SIGINT', 2, 130], ['SIGTERM', 2, 143],
  ] as const)('cleans up %s handlers when cancelled in command %i', async (name, phase, code) => {
    const before = process.listeners(name);
    let commands = 0;
    vi.mocked(runStreaming).mockImplementation(async (_cmd, _args, options) => {
      commands++;
      if (commands !== phase) return 0;
      const handler = process.listeners(name).find(listener => !before.includes(listener));
      expect(handler, 'Build must install a scoped cancellation handler').toBeDefined();
      expect(options?.signal).toBeDefined();
      handler!(name);
      // A second Ctrl+C during cleanup must not bypass that handler.
      expect(process.listeners(name)).toContain(handler);
      handler!(name);
      expect(options!.signal!.aborted).toBe(true);
      throw createAbortError(options!.signal!.reason);
    });
    await buildCommand.parseAsync(['node', 'cli', '--target', 'mini_installer']);
    expect(commands).toBe(phase);
    expect(process.exitCode).toBe(code);
    expect(process.listeners(name)).toEqual(before);
  });
});
