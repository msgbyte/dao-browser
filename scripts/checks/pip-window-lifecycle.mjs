// Run after npm run rebuild: node scripts/checks/pip-window-lifecycle.mjs [binary] [output-dir] [iterations]
// Exercises native Document PiP teardown with an offline page and a separate profile.
import {strict as assert} from 'node:assert';
import {spawn} from 'node:child_process';
import {mkdir, readFile, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {resolve, join} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const binary = resolve(process.argv[2] ?? (process.platform === 'win32'
  ? 'engine/src/out/dao-debug/dao.exe'
  : 'engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug'));
const output = resolve(process.argv[3] ?? `.dao/pip-lifecycle-${Date.now()}`);
const iterations = Number(process.argv[4] ?? 30);
assert.ok(Number.isInteger(iterations) && iterations > 0);
const profile = join(output, 'profile');
await mkdir(output, {recursive: true});
// Refuse to reuse a profile: the test must never attach to an existing browser.
await mkdir(profile);

const server = createServer((_request, response) => {
  response.writeHead(200, {'Content-Type': 'text/html', 'Cache-Control': 'no-store'});
  response.end('<!doctype html><title>Dao PiP lifecycle test</title><p>Offline PiP lifecycle test</p>');
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const url = `http://127.0.0.1:${server.address().port}/`;
const child = spawn(binary, [
  `--user-data-dir=${profile}`, '--remote-debugging-port=0',
  '--no-first-run', '--no-default-browser-check', '--enable-automation',
  '--disable-background-networking', '--enable-logging=stderr',
  '--disable-features=DocumentPipStandaloneWindow', 'about:blank',
], {stdio: ['ignore', 'ignore', 'pipe'], windowsHide: true});
let stderr = '';
let launchError;
child.stderr.on('data', data => { stderr += data; });
child.on('error', error => { launchError = error; });
const exited = new Promise(resolve => child.once('exit', (code, signal) => resolve({code, signal})));
const connections = new Set();
const results = [];
let sendBrowser;
let failure;

function checkAlive() {
  if (launchError) throw launchError;
  assert.equal(child.exitCode, null, `Browser exited with code ${child.exitCode}`);
  assert.equal(child.signalCode, null, `Browser exited with signal ${child.signalCode}`);
}

async function until(predicate, label) {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    checkAlive();
    const value = await predicate();
    if (value) return value;
    await delay(50);
  }
  throw new Error(`Timed out: ${label}`);
}

async function connect(url) {
  const socket = new WebSocket(url);
  connections.add(socket);
  await new Promise((resolve, reject) => {
    socket.onopen = resolve;
    socket.onerror = reject;
  });
  let sequence = 0;
  const pending = new Map();
  socket.onmessage = event => {
    const message = JSON.parse(event.data);
    const handler = pending.get(message.id);
    if (!handler) return;
    pending.delete(message.id);
    clearTimeout(handler.timer);
    if (message.error) handler.reject(new Error(JSON.stringify(message.error)));
    else handler.resolve(message.result);
  };
  socket.onclose = () => {
    connections.delete(socket);
    for (const handler of pending.values()) {
      clearTimeout(handler.timer);
      handler.reject(new Error('Browser or page disconnected'));
    }
    pending.clear();
  };
  return (method, params = {}) => new Promise((resolve, reject) => {
    if (socket.readyState !== WebSocket.OPEN) {
      reject(new Error(`CDP connection closed: ${method}`));
      return;
    }
    const id = ++sequence;
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`CDP timed out: ${method}`));
    }, 30000);
    pending.set(id, {resolve, reject, timer});
    socket.send(JSON.stringify({id, method, params}));
  });
}

async function evaluate(send, expression) {
  const result = await send('Runtime.evaluate', {
    expression, awaitPromise: true, userGesture: true, returnByValue: true,
  });
  assert.equal(result.exceptionDetails, undefined, JSON.stringify(result.exceptionDetails));
  return result.result.value;
}

try {
  const port = await until(async () => {
    try { return (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; }
    catch (error) {
      // Windows can briefly lock this file while the browser publishes its port.
      if (error.code !== 'ENOENT' && error.code !== 'EBUSY') throw error;
    }
  }, 'DevTools port');
  const endpoint = `http://127.0.0.1:${port}`;
  const version = await (await fetch(`${endpoint}/json/version`)).json();
  sendBrowser = await connect(version.webSocketDebuggerUrl);
  const targets = async () => (await sendBrowser('Target.getTargets')).targetInfos;
  for (let index = 0; index < iterations; index++) {
    const action = ['close-window', 'close-opener', 'navigate-opener'][index % 3];
    const {targetId} = await sendBrowser('Target.createTarget', {url: `${url}?case=${index}`});
    const page = await until(async () => {
      const pages = await (await fetch(`${endpoint}/json/list`)).json();
      return pages.find(page => page.id === targetId);
    }, 'opener page');
    const sendPage = await connect(page.webSocketDebuggerUrl);
    await until(async () => await evaluate(sendPage, 'document.readyState') === 'complete', 'page loaded');
    const before = new Set((await targets()).map(target => target.targetId));
    assert.equal(await evaluate(sendPage, `(async () => {
      window.testPip = await documentPictureInPicture.requestWindow({width: 480, height: 320});
      testPip.document.title = 'Dao PiP test';
      testPip.document.body.textContent = 'Close this test window safely';
      return !testPip.closed;
    })()`), true);
    const pip = await until(async () => (await targets()).find(target =>
      target.type === 'page' && !before.has(target.targetId) && target.openerId === targetId), 'PiP target');
    // Round-trip the browser after creation before initiating native destruction.
    await sendBrowser('Browser.getWindowForTarget', {targetId: pip.targetId});
    if (action === 'close-window') {
      await evaluate(sendPage, 'testPip.close(); true');
    } else if (action === 'close-opener') {
      assert.equal((await sendBrowser('Target.closeTarget', {targetId})).success, true);
    } else {
      await sendPage('Page.navigate', {url: `${url}after-close`});
    }
    await until(async () => !(await targets()).some(target => target.targetId === pip.targetId), 'PiP closed');
    if (action !== 'close-opener') {
      assert.equal(await evaluate(sendPage, '21 * 2'), 42);
      assert.equal((await sendBrowser('Target.closeTarget', {targetId})).success, true);
    }
    checkAlive();
    results.push({iteration: index + 1, action, passed: true});
    console.log(`PASS ${index + 1}/${iterations}: ${action}`);
  }
} catch (error) {
  failure = error;
} finally {
  if (sendBrowser && child.exitCode === null && child.signalCode === null) {
    await sendBrowser('Browser.close').catch(() => {});
  }
  const exit = await Promise.race([exited, delay(5000).then(() => null)]);
  if (exit && (exit.code !== 0 || exit.signal !== null)) {
    failure ??= new Error(`Test browser exited abnormally (code: ${exit.code}, signal: ${exit.signal})`);
  }
  if (!exit && child.pid) {
    // Terminate only this test's process tree if graceful shutdown failed.
    if (process.platform === 'win32') {
      const kill = spawn('taskkill', ['/PID', String(child.pid), '/T', '/F'], {windowsHide: true, stdio: 'ignore'});
      await new Promise(resolve => kill.once('exit', resolve));
    } else child.kill('SIGKILL');
    failure ??= new Error('Test browser did not exit gracefully');
  }
  for (const socket of connections) socket.close();
  child.stderr.destroy();
  server.closeAllConnections();
  server.close();
  await writeFile(join(output, 'browser.log'), stderr);
  await writeFile(join(output, 'results.json'), JSON.stringify({
    binary, iterations, results, error: failure?.message ?? null,
    exitCode: child.exitCode, signal: child.signalCode,
  }, null, 2) + '\n');
}
if (failure) {
  console.error(failure.stack);
  process.exitCode = 1;
}
