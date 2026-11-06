// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Windows integration check with Node's inherited stdio pipes:
// node node_stdio_smoke_test.mjs path/to/dao.exe path/to/dao-mcp.exe path/to/dao-browser.exe
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {createInterface} from 'node:readline';
import {setTimeout as delay} from 'node:timers/promises';
import {test} from 'node:test';

assert.equal(process.platform, 'win32', 'This check exercises Windows pipes');
assert.equal(process.argv.length, 5, 'Pass dao.exe, dao-mcp.exe and dao-browser.exe');
const [browserPath, helperPath, cliPath] = process.argv.slice(2).map(p => path.resolve(p));

function launch(executable, args, stdio = 'pipe') {
  const process = spawn(executable, args, {windowsHide: true, stdio});
  const closed = new Promise(resolve => {
    process.once('error', error => resolve({error}));
    process.once('close', (code, signal) => resolve({code, signal}));
  });
  return {process, closed};
}

async function stop(child) {
  if (child.process.exitCode === null && child.process.signalCode === null) {
    child.process.kill();
  }
  await child.closed;
}

async function waitForExit(child) {
  let timer;
  try {
    return await Promise.race([
      child.closed,
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error('MCP helper did not exit after EOF')), 3000);
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}

function client(executable, args) {
  const child = launch(executable, args);
  const pending = new Map();
  let stderr = '';
  let nextId = 0;
  child.process.stderr.on('data', data => stderr += data);
  child.process.stdin.on('error', () => {});
  createInterface({input: child.process.stdout}).on('line', line => {
    const message = JSON.parse(line);
    const waiter = pending.get(message.id);
    if (!waiter) {
      return;
    }
    pending.delete(message.id);
    clearTimeout(waiter.timer);
    waiter.resolve({message, bytes: Buffer.byteLength(line)});
  });
  child.closed.then(result => {
    for (const waiter of pending.values()) {
      clearTimeout(waiter.timer);
      waiter.reject(new Error(`MCP helper exited: ${JSON.stringify(result)} ${stderr}`));
    }
    pending.clear();
  });
  return {
    ...child,
    notify(method) {
      child.process.stdin.write(JSON.stringify({jsonrpc: '2.0', method}) + '\n');
    },
    request(method, params = {}) {
      const id = ++nextId;
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          pending.delete(id);
          reject(new Error(`Timed out waiting for ${method}; stderr: ${stderr}`));
        }, 8000);
        pending.set(id, {resolve, reject, timer});
        child.process.stdin.write(JSON.stringify({jsonrpc: '2.0', id, method, params}) + '\n');
      });
    },
  };
}

await test('Windows MCP streams large responses through Node pipes', async t => {
  const profile = await mkdtemp(path.join(tmpdir(), 'dao-mcp-node-'));
  await writeFile(path.join(profile, 'Local State'), JSON.stringify({dao: {mcp_server_enabled: true}}));
  const browser = launch(browserPath, [
    '--headless', `--user-data-dir=${profile}`, '--no-first-run',
    '--no-default-browser-check', 'about:blank',
  ], 'ignore');
  t.after(async () => {
    await stop(browser);
    assert.equal(path.dirname(path.resolve(profile)), path.resolve(tmpdir()));
    assert.ok(path.basename(profile).startsWith('dao-mcp-node-'));
    await rm(profile, {recursive: true, force: true, maxRetries: 20, retryDelay: 100});
  });
  const deadline = Date.now() + 20000;
  for (;;) {
    try {
      const metadata = JSON.parse(await readFile(path.join(profile, 'MCP', 'runtime.json'), 'utf8'));
      assert.equal(metadata.browser_pid, browser.process.pid);
      break;
    } catch (error) {
      if (Date.now() >= deadline || browser.process.exitCode !== null) {
        throw error;
      }
      await delay(50);
    }
  }

  for (const [executable, args] of [[helperPath, []], [cliPath, ['mcp']]]) {
    await t.test(path.basename(executable), async t => {
      const peer = client(executable, [...args, `--user-data-dir=${profile}`]);
      t.after(() => stop(peer));
      const initialized = await peer.request('initialize', {
        protocolVersion: '2025-11-25', capabilities: {},
        clientInfo: {name: 'node-stdio-regression', version: '1'},
      });
      assert.ok(initialized.message.result, JSON.stringify(initialized.message));
      peer.notify('notifications/initialized');
      const catalog = await peer.request('tools/list');
      assert.ok(catalog.bytes > 64 * 1024, 'The catalog must exceed the Node pipe buffer');
      assert.ok(catalog.message.result.tools.length > 0);

      // The reader may temporarily stop while the helper has output queued.
      peer.process.stdout.pause();
      const resumedCatalogs = Array.from({length: 4}, () => peer.request('tools/list'));
      const ping = peer.request('ping');
      await delay(100);
      peer.process.stdout.resume();
      const [resumed, pong] = await Promise.all([Promise.all(resumedCatalogs), ping]);
      for (const response of resumed) {
        assert.deepEqual(response.message.result, catalog.message.result);
      }
      assert.deepEqual(pong.message.result, {});
      peer.process.stdin.end();
      assert.deepEqual(await waitForExit(peer), {code: 0, signal: null});
    });
  }
});
