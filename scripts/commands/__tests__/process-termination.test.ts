// @vitest-environment node
import * as childProcess from 'node:child_process';
import {EventEmitter} from 'node:events';
import {afterEach, describe, expect, it, vi} from 'vitest';
import {waitForSpawnedProcess} from '../../utils.js';

vi.mock('node:child_process', async (importOriginal) => ({
  ...await importOriginal<typeof import('node:child_process')>(),
  spawn: vi.fn(),
}));

afterEach(() => vi.useRealTimers());

describe('Windows termination completion', () => {
  it.each(['success', 'failure', 'timeout'])('waits for tree cleanup after leader close: %s', async (outcome) => {
    vi.useFakeTimers();
    const child = Object.assign(new EventEmitter(), {
      pid: 123, kill: vi.fn(), unref: vi.fn(),
    }) as unknown as childProcess.ChildProcess;
    const killer = Object.assign(new EventEmitter(), {
      exitCode: null as number | null, signalCode: null, kill: vi.fn(),
    });
    vi.mocked(childProcess.spawn).mockReturnValue(killer as unknown as childProcess.ChildProcess);
    const controller = new AbortController();
    let settled = false;
    const result = waitForSpawnedProcess(child, controller.signal, {platform: 'win32', timeoutMs: 20})
      .catch((cause: unknown) => { settled = true; return cause; });
    controller.abort();
    expect(childProcess.spawn).toHaveBeenCalledWith(
      'taskkill.exe', ['/PID', '123', '/T', '/F'],
      {detached: true, windowsHide: true, stdio: 'ignore'});
    child.emit('close', 1);
    await Promise.resolve();
    expect(settled).toBe(false);
    if (outcome === 'timeout') {
      await vi.advanceTimersByTimeAsync(20);
    } else {
      killer.exitCode = outcome === 'success' ? 0 : 1;
      killer.emit('close', killer.exitCode);
    }
    expect(await result).toMatchObject({
      name: outcome === 'success' ? 'AbortError' : 'ProcessTerminationError',
    });
  });
});
