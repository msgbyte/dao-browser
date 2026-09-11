// Run after npm run rebuild: node scripts/checks/parallel-downloading.mjs
// Uses an isolated profile and a local HTTP server; never touches user downloads.
import {strict as assert} from 'node:assert';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdir, mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const binary = resolve(process.argv[2] ??
    'engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug');
const profile = await mkdtemp(join(tmpdir(), 'dao-parallel-check-'));
// Keep Chrome's download delegate: DevTools download interception replaces it
// and bypasses the browser's normal download preferences and metadata.
await mkdir(join(profile, 'Default'));
await writeFile(join(profile, 'Default', 'Preferences'), JSON.stringify({
  download: {default_directory: profile, prompt_for_download: false},
}));
const payload = Buffer.alloc(12 * 1024 * 1024);
for (let i = 0; i < payload.length; i++) payload[i] = i % 251;
const digest = data => createHash('sha256').update(data).digest('hex');
const requests = new Map();
const sockets = [];
let child;
let stderr = '';

const server = createServer((req, res) => {
  const name = req.url.slice(1);
  const range = req.headers.range;
  requests.set(name, [...(requests.get(name) ?? []), range ?? 'full']);
  const match = range?.match(/^bytes=(\d+)-(\d*)$/);
  let offset = match ? Number(match[1]) : 0;
  const end = match?.[2] ? Number(match[2]) : payload.length - 1;
  const headers = {
    'Content-Type': 'application/octet-stream',
    'Content-Disposition': `attachment; filename="${name}"`,
    'Content-Length': end - offset + 1,
    'Accept-Ranges': name === 'unsupported.bin' ? 'none' : 'bytes',
    'ETag': '"dao-parallel-check"',
  };
  if (match) headers['Content-Range'] = `bytes ${offset}-${end}/${payload.length}`;
  res.writeHead(match ? 206 : 200, headers);
  const timer = setInterval(() => {
    const next = Math.min(offset + 64 * 1024, end + 1);
    res.write(payload.subarray(offset, next));
    offset = next;
    if (offset > end) res.end();
  }, 25);
  res.on('close', () => clearInterval(timer));
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;

async function until(fn, label) {
  for (let i = 0; i < 300; i++) {
    const result = await fn();
    if (result) return result;
    if (child?.exitCode != null) throw new Error(`Browser exited: ${stderr.slice(-2000)}`);
    await delay(100);
  }
  throw new Error(`Timed out: ${label}\n${stderr.slice(-2000)}`);
}

async function connect(url) {
  const socket = new WebSocket(url);
  sockets.push(socket);
  await new Promise((resolve, reject) => {
    socket.onopen = resolve;
    socket.onerror = reject;
  });
  let sequence = 0;
  const pending = new Map();
  socket.onmessage = event => {
    const msg = JSON.parse(event.data);
    const handler = pending.get(msg.id);
    if (!handler) return;
    pending.delete(msg.id);
    msg.error ? handler.reject(new Error(JSON.stringify(msg.error))) : handler.resolve(msg.result);
  };
  socket.onclose = () => {
    for (const handler of pending.values()) handler.reject(new Error('CDP disconnected'));
    pending.clear();
  };
  return (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++sequence;
    pending.set(id, {resolve, reject});
    socket.send(JSON.stringify({id, method, params}));
  });
}

async function start() {
  await rm(join(profile, 'DevToolsActivePort'), {force: true});
  stderr = '';
  child = spawn(binary, ['--headless', `--user-data-dir=${profile}`,
    '--remote-debugging-port=0', '--no-first-run', '--no-default-browser-check',
    '--use-mock-keychain', '--disable-background-networking', '--enable-automation'],
  {stdio: ['ignore', 'ignore', 'pipe']});
  child.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-5000); });
  const port = await until(async () => {
    try { return (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; }
    catch { return null; }
  }, 'DevTools port');
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const page = targets.find(item => item.type === 'page' &&
      !['dao://agent/', 'dao://sidebar/'].includes(item.url));
  assert(page, 'Browser page exists');
  const send = await connect(page.webSocketDebuggerUrl);
  const ui = async body => {
    const result = await send('Runtime.evaluate', {
      expression: `(() => {
        function find(selector, root = document) {
          const match = root.querySelector(selector);
          if (match) return match;
          for (const el of root.querySelectorAll('*')) {
            if (el.shadowRoot) {
              const found = find(selector, el.shadowRoot);
              if (found) return found;
            }
          }
          return null;
        }
        ${body}
      })()`, awaitPromise: true, returnByValue: true,
    });
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  await send('Page.navigate', {url: 'chrome://settings/#dao'});
  await until(() => ui(`return !!find('settings-dao-page');`), 'Dao settings page');
  assert(await ui(`return !!find('#parallelDownloadingEnabled');`),
      'You and Dao must expose parallel downloading');
  const expect = value => until(() => ui(`
    const toggle = find('#parallelDownloadingEnabled');
    return toggle?.checked === ${value} && toggle.pref.value === ${value};
  `), `setting ${value}`);
  const toggle = async value => {
    await ui(`find('#parallelDownloadingEnabled').click();`);
    await expect(value);
  };
  return {send, ui, expect, toggle};
}

async function stop(browser) {
  const exit = new Promise(resolve => child.once('exit', resolve));
  await browser.send('Browser.close');
  await exit;
  sockets.splice(0).forEach(socket => socket.close());
}

async function complete(name) {
  await until(async () => {
    try {
      const file = await readFile(join(profile, name));
      assert.equal(digest(file), digest(payload), `${name} integrity`);
      return true;
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
      return false;
    }
  }, `${name} completes`);
}

try {
  let browser = await start();
  await browser.expect(false);
  const download = name => browser.send('Page.navigate', {url: `${origin}/${name}`});
  await download('default.bin');
  await until(() => requests.has('default.bin'), 'default download starts');
  await browser.toggle(true);
  await download('parallel.bin');
  await until(() => requests.get('parallel.bin')?.filter(r => r !== 'full').length >= 2,
      'parallel Range requests without restart');
  await download('unsupported.bin');
  await browser.toggle(false);
  await download('disabled.bin');
  await Promise.all(['default.bin', 'parallel.bin', 'unsupported.bin', 'disabled.bin'].map(complete));
  for (const name of ['default.bin', 'unsupported.bin', 'disabled.bin']) {
    assert.deepEqual(requests.get(name), ['full'], `${name} stays serial`);
  }
  await browser.toggle(true);
  await browser.send('Page.reload');
  await browser.expect(true);
  await stop(browser);
  let prefs = JSON.parse(await readFile(join(profile, 'Default', 'Preferences'), 'utf8'));
  assert.equal(prefs.dao.parallel_downloading_enabled, true);
  const flags = JSON.parse(await readFile(join(profile, 'Local State'), 'utf8'))
      .browser?.enabled_labs_experiments ?? [];
  assert(!flags.some(flag => flag.startsWith('enable-parallel-downloading')),
      'Dao preference must not write chrome://flags');
  browser = await start();
  await browser.expect(true);
  await browser.toggle(false);
  await stop(browser);
  browser = await start();
  await browser.expect(false);
  await stop(browser);
  console.log('PASS: default off; live enable/disable; active downloads retain mode; unsupported server stays serial; SHA-256 integrity; reload and restart persistence; no flags changes');
} finally {
  sockets.forEach(socket => socket.close());
  if (child && child.exitCode === null) {
    const exit = new Promise(resolve => child.once('exit', resolve));
    child.kill('SIGTERM');
    await exit;
  }
  server.closeAllConnections();
  await new Promise(resolve => server.close(resolve));
  await rm(profile, {recursive: true, force: true});
}
