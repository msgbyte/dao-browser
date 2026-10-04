// @vitest-environment node
import {spawn} from 'node:child_process';
import {existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {describe, expect, it} from 'vitest';
import {runStreaming} from '../../utils.js';

function alive(pid: number): boolean {
  try { process.kill(pid, 0); return true; } catch { return false; }
}

describe.runIf(process.platform === 'win32')('Windows process cancellation', () => {
  it('waits for batch descendants to stop without terminating unrelated jobs', async () => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-cancel-'));
    const marker = path.join(root, 'owned.json');
    const script = path.join(root, 'child.cjs');
    const batch = path.join(root, 'launch.cmd');
    writeFileSync(script, [
      'const {spawn} = require("node:child_process");',
      'const code = `require("node:fs").writeFileSync(process.argv[1], JSON.stringify([process.ppid, process.pid])); setTimeout(() => {}, 30000);`;',
      'spawn(process.execPath, ["-e", code, process.argv[2]], {stdio: "ignore"});',
      'setTimeout(() => {}, 30000);',
    ].join('\n'));
    writeFileSync(batch, `@"${process.execPath}" "${script}" "%~1"\r\n`);
    const unrelated = spawn(process.execPath, ['-e', 'setTimeout(() => {}, 30000)'], {
      stdio: 'ignore', windowsHide: true,
    });
    const controller = new AbortController();
    const task = runStreaming(batch, [marker], {signal: controller.signal})
      .catch((error: unknown) => error);
    let owned: number[] = [];
    try {
      const deadline = Date.now() + 5000;
      while (!existsSync(marker) && Date.now() < deadline) await delay(20);
      expect(existsSync(marker)).toBe(true);
      owned = JSON.parse(readFileSync(marker, 'utf8'));
      expect(owned.every(alive)).toBe(true);
      controller.abort();
      expect(await task).toMatchObject({name: 'AbortError'});
      expect(owned.map(alive)).toEqual([false, false]);
      expect(alive(unrelated.pid!)).toBe(true);
    } finally {
      controller.abort();
      await task;
      for (const pid of owned) {
        try { process.kill(pid); } catch { /* Already terminated. */ }
      }
      unrelated.kill();
      rmSync(root, {recursive: true, force: true});
    }
  }, 15_000);
});
