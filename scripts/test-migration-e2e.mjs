// Run with Node 22+ after npm run rebuild. Uses the real WebUI and native stores.
// Windows only: refuses to run if a Chrome User Data directory already exists.
// The temporary destination and screenshot are retained for inspection.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdir, mkdtemp, readFile, realpath, rmdir, unlink, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {DatabaseSync} from 'node:sqlite';
import {setTimeout as delay} from 'node:timers/promises';

assert.equal(process.platform, 'win32', 'This fixture tests Windows file locks');
const sourceRoot = path.join(process.env.LOCALAPPDATA, 'Google/Chrome/User Data');
await mkdir(path.dirname(sourceRoot), {recursive: true});
await mkdir(sourceRoot); // EEXIST is intentional: never touch a real profile.
const source = path.join(sourceRoot, 'Default');
const destination = await mkdtemp(path.join(tmpdir(), 'dao-migration-e2e-'));
console.log('Destination and artifacts: ' + destination);
const ownedFiles = new Map();
const digest = data => createHash('sha256').update(data).digest('hex');
async function fixture(name, data) {
  const filename = path.join(sourceRoot, name);
  await writeFile(filename, data);
  ownedFiles.set(filename, digest(await readFile(filename)));
}
const i32 = value => {const bytes = Buffer.alloc(4); bytes.writeInt32LE(value); return bytes;};
function pickleString(text, encoding = 'utf8') {
  const bytes = Buffer.from(text, encoding);
  return Buffer.concat([i32(encoding === 'utf16le' ? text.length : bytes.length),
    bytes, Buffer.alloc((4 - bytes.length % 4) % 4)]);
}
function command(id, payload = Buffer.alloc(0)) {
  const header = Buffer.alloc(3);
  header.writeUInt16LE(payload.length + 1);
  header[2] = id;
  return Buffer.concat([header, payload]);
}
// Chromium SNSS version 3, with a complete-state marker (command 255).
const urls = ['https://migration-one.example/', 'https://migration-two.example/'];
function sessionFixture() {
  const records = [Buffer.from('534e535303000000', 'hex'),
    command(9, Buffer.concat([i32(1), i32(0)]))];
  urls.forEach((url, index) => {
    const tab = index + 2;
    const navigation = Buffer.concat([i32(tab), i32(0), pickleString(url),
      pickleString('Imported tab ' + index, 'utf16le'), pickleString(''), i32(0)]);
    records.push(command(0, Buffer.concat([i32(1), i32(tab)])),
      command(2, Buffer.concat([i32(tab), i32(index)])),
      command(6, Buffer.concat([i32(navigation.length), navigation])),
      command(7, Buffer.concat([i32(tab), i32(0)])));
  });
  return Buffer.concat([...records, command(255)]);
}
let child, socket, lock;
let stderr = '';
const pending = new Map();
let nextId = 0;
function send(method, params = {}) {
  const id = ++nextId;
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {pending.delete(id); reject(new Error(method + ' timed out: ' + (params.expression || '')));}, 30000);
    pending.set(id, {resolve, reject, timeout});
    socket.send(JSON.stringify({id, method, params}));
  });
}
async function connect(url) {
  const connection = new WebSocket(url);
  connection.addEventListener('message', event => {
    const result = JSON.parse(event.data);
    const request = pending.get(result.id);
    if (!request) return;
    pending.delete(result.id);
    clearTimeout(request.timeout);
    if (result.error) request.reject(new Error(JSON.stringify(result.error)));
    else request.resolve(result.result);
  });
  await new Promise((resolve, reject) => {
    connection.addEventListener('open', resolve, {once: true});
    connection.addEventListener('error', reject, {once: true});
  });
  return connection;
}
async function evaluate(expression) {
  const result = await send('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true});
  assert.ok(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
async function waitFor(check, message) {
  for (let i = 0; i < 240; ++i) {
    const value = await check();
    if (value) return value;
    await delay(250);
  }
  throw new Error(message);
}
async function terminal() {
  return waitFor(async () => {
    const state = await evaluate("import('./import_bridge.js').then(b => b.getBrowserMigrationState())");
    return state?.terminal && state;
  }, 'Migration did not finish');
}
async function releaseLock() {
  const process = lock;
  lock = null;
  if (!process || process.exitCode !== null || process.signalCode !== null) return;
  process.stdin.end('\n');
  await Promise.race([new Promise(resolve => process.once('exit', resolve)), delay(5000)]);
  if (process.exitCode === null && process.signalCode === null) {
    process.kill();
    await new Promise(resolve => process.once('exit', resolve));
  }
}
const category = (state, name) => state.categories.find(item => item.category === name);
try {
  await mkdir(path.join(source, 'Sessions'), {recursive: true});
  await fixture('Local State', JSON.stringify({profile: {info_cache: {Default: {name: 'Dao migration E2E'}}}}));
  const bookmark = (name, url) => ({type: 'url', name, url});
  const bookmarks = JSON.stringify({roots: {bookmark_bar: {children: [
    {type: 'folder', name: 'Parent', children: [
      {type: 'folder', name: 'Child', children: [bookmark('Nested', urls[0])]},
      bookmark('Sibling', urls[1])]}, bookmark('Root sibling', 'https://migration-root.example/')
  ]}}});
  await fixture('Default/Bookmarks', bookmarks);
  await fixture('Default/Preferences', '{}');
  await fixture('Default/Sessions/Session_13400000000000000', sessionFixture());
  const sourceDb = new DatabaseSync(path.join(source, 'History'));
  sourceDb.exec('CREATE TABLE urls (id INTEGER PRIMARY KEY, url TEXT, title TEXT); CREATE TABLE visits (id INTEGER PRIMARY KEY, url INTEGER, visit_time INTEGER);');
  sourceDb.prepare('INSERT INTO urls VALUES (1, ?, ?)').run(urls[0], 'Imported history');
  const visitTime = (Date.now() + 11644473600000) * 1000 - 100000000;
  sourceDb.prepare('INSERT INTO visits VALUES (1, 1, ?), (2, 1, ?)').run(visitTime, visitTime + 1000);
  sourceDb.close();
  ownedFiles.set(path.join(source, 'History'), digest(await readFile(path.join(source, 'History'))));

  const extension = path.join(destination, 'fixture-extension');
  await mkdir(extension);
  await writeFile(path.join(extension, 'manifest.json'), JSON.stringify({manifest_version: 3,
    name: 'Migration E2E', version: '1.0'}));
  child = spawn(path.resolve('engine/src/out/dao-debug/chrome.exe'), [
    '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
    '--disable-background-networking', '--disable-component-update', '--disable-sync',
    '--enable-unsafe-extension-debugging', '--enable-logging=stderr', '--lang=en-US', '--remote-debugging-port=0',
    '--user-data-dir=' + destination, 'dao://import/',
  ], {windowsHide: true, stdio: ['ignore', 'ignore', 'pipe']});
  child.stderr.on('data', data => {stderr = (stderr + data).slice(-32000);});
  child.once('exit', code => {
    for (const request of pending.values()) {
      clearTimeout(request.timeout);
      request.reject(new Error('Browser exited: ' + code + '\n' + stderr.slice(-2000)));
    }
    pending.clear();
  });
  const port = await waitFor(async () => {
    assert.equal(child.exitCode, null, 'Browser exited before DevTools started');
    try {return (await readFile(path.join(destination, 'DevToolsActivePort'), 'utf8')).split('\n')[0];} catch {return null;}
  }, 'DevTools unavailable');
  const version = await (await fetch('http://127.0.0.1:' + port + '/json/version')).json();
  socket = await connect(version.webSocketDebuggerUrl);
  const installed = await send('Extensions.loadUnpacked', {path: extension});
  console.log('Loaded fixture extension');
  socket.close();
  const pages = await (await fetch('http://127.0.0.1:' + port + '/json/list')).json();
  socket = await connect(pages.find(page => page.type === 'page').webSocketDebuggerUrl);
  const securePreferences = JSON.stringify({extensions: {settings: {
    [installed.id]: {from_webstore: true, state: 1, manifest: {name: 'Migration E2E'}}
  }}});
  await fixture('Default/Secure Preferences', securePreferences);
  // Emulate a running Chromium browser's exclusive session handle.
  lock = spawn('powershell.exe', ['-NoProfile', '-Command',
    "$f=[IO.File]::Open($env:DAO_E2E_SESSION,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None); Write-Output 'locked'; [Console]::ReadLine() | Out-Null; $f.Dispose()"],
    {windowsHide: true, env: {...process.env, DAO_E2E_SESSION: path.join(source, 'Sessions/Session_13400000000000000')}, stdio: ['pipe', 'pipe', 'pipe']});
  await new Promise((resolve, reject) => {
    lock.stdout.once('data', resolve);
    lock.once('error', reject);
    lock.once('exit', code => reject(new Error('Lock process exited: ' + code)));
  });
  await send('Page.navigate', {url: 'dao://import/'});
  await waitFor(() => evaluate("!!document.querySelector('dao-import-app')?.sources_?.some(s => s.profileName === 'Dao migration E2E')"), 'Fixture source not detected');
  console.log('Detected fixture source');
  await evaluate(`(async () => {
    const app = document.querySelector('dao-import-app');
    const source = app.sources_.find(s => s.profileName === 'Dao migration E2E');
    app.shadowRoot.querySelector('[data-source-id="' + source.id + '"]').click();
    await app.updateComplete;
    app.shadowRoot.querySelector('[data-test="continue"]').click();
    await app.updateComplete;
    app.shadowRoot.querySelector('[data-test="start"]').click();
  })()`);
  const first = await terminal();
  console.log('First migration: ' + JSON.stringify(first));
  assert.equal(category(first, 'bookmarks').imported, 5);
  assert.equal(category(first, 'history').imported, 2);
  assert.equal(category(first, 'extensions').skipped, 1);
  assert.equal(category(first, 'tabs').errorCode, 'source_in_use');
  assert.match(await evaluate("document.querySelector('dao-import-app').shadowRoot.textContent"), /Completely quit the source browser/);
  await releaseLock();
  assert.equal(await evaluate("import('./import_bridge.js').then(b => {const app=document.querySelector('dao-import-app'); return b.getImportItemCount(app.selectedSourceId_, 'tabs');})"), 2);
  await evaluate("document.querySelector('dao-import-app').shadowRoot.querySelector('footer .primary').click()");
  const retried = await terminal();
  console.log('Retry finished');
  assert.ok(retried.categories.every(item => item.phase === 'succeeded'), JSON.stringify(retried));
  assert.equal(category(retried, 'tabs').imported, 2);
  assert.deepEqual(retried.categories.map(item => item.category), ['tabs']);
  const screenshot = await send('Page.captureScreenshot');
  await writeFile(path.join(destination, 'migration-e2e.png'), Buffer.from(screenshot.data, 'base64'));
  const folders = JSON.parse(await readFile(path.join(destination, 'Default/dao_folders.json'), 'utf8'));
  assert.ok(JSON.stringify(folders).includes(urls[0]) && JSON.stringify(folders).includes(urls[1]));

  // Exercise the real installer rejection callback without store downloads.
  await fixture('Default/Secure Preferences', JSON.stringify({extensions: {settings: {
    'invalid-extension-id': {from_webstore: true, state: 1}
  }}}));
  await evaluate("import('./import_bridge.js').then(b => b.startBrowserMigration(document.querySelector('dao-import-app').selectedSourceId_, ['extensions']))");
  const installFailed = await terminal();
  assert.equal(category(installFailed, 'extensions').errorCode, 'extension_install_failed');
  assert.equal(category(installFailed, 'extensions').failed, 1);
  await fixture('Default/Secure Preferences', securePreferences);
  await evaluate("document.querySelector('dao-import-app').shadowRoot.querySelector('footer .primary').click()");
  const extensionRetried = await terminal();
  assert.equal(category(extensionRetried, 'extensions').phase, 'succeeded');
  assert.equal(category(extensionRetried, 'extensions').skipped, 1);

  await fixture('Default/Bookmarks', '{');
  await evaluate("import('./import_bridge.js').then(b => b.startBrowserMigration(document.querySelector('dao-import-app').selectedSourceId_, ['bookmarks']))");
  const failed = await terminal();
  assert.equal(category(failed, 'bookmarks').errorCode, 'invalid_bookmarks');
  assert.match(await evaluate("document.querySelector('dao-import-app').shadowRoot.querySelector('h1').textContent"), /could not be migrated/);
  await fixture('Default/Bookmarks', bookmarks);
  await evaluate("document.querySelector('dao-import-app').shadowRoot.querySelector('footer .primary').click()");
  const recovered = await terminal();
  assert.equal(category(recovered, 'bookmarks').phase, 'succeeded');
  assert.equal(category(recovered, 'bookmarks').skipped, 5);

  await send('Browser.close');
  await waitFor(() => child.exitCode !== null, 'Browser did not exit');
  const saved = JSON.parse(await readFile(path.join(destination, 'Default/Bookmarks'), 'utf8'));
  const imported = saved.roots.bookmark_bar.children.find(node => node.name === 'Imported browser data');
  assert.ok(imported, 'Imported bookmark root missing');
  const parent = imported.children.find(node => node.name === 'Parent');
  assert.equal(parent.children.find(node => node.name === 'Sibling').url, urls[1]);
  assert.equal(parent.children.find(node => node.name === 'Child').children[0].url, urls[0]);
  const destDb = new DatabaseSync(path.join(destination, 'Default/History'), {readOnly: true});
  assert.equal(destDb.prepare('SELECT COUNT(*) AS n FROM visits JOIN urls ON visits.url=urls.id WHERE urls.url=? AND visit_time IN (?, ?)').get(urls[0], visitTime, visitTime + 1000).n, 2);
  destDb.close();
  console.log('PASS: real WebUI -> native detection -> snapshots -> persisted bookmarks/history/tabs; protected extension deduplication and installer rejection/retry; locked-session hint/retry; corrupt-source failure/recovery; bookmark retry without duplication');
  console.log(JSON.stringify(retried, null, 2));
} finally {
  await releaseLock();
  if (socket?.readyState === WebSocket.OPEN) {
    try {await send('Browser.close');} catch {}
    socket.close();
  }
  if (child && child.exitCode === null) {
    await Promise.race([new Promise(resolve => child.once('exit', resolve)), delay(5000)]);
    if (child.exitCode === null) child.kill();
  }
  for (const request of pending.values()) clearTimeout(request.timeout);
  await writeFile(path.join(destination, 'browser-stderr.log'), stderr);
  assert.equal(await realpath(sourceRoot), path.resolve(sourceRoot));
  // Remove only exact fixture files whose contents still match our writes.
  // Unexpected files or externally changed data are retained, never erased.
  for (const [filename, hash] of ownedFiles) {
    assert.equal(digest(await readFile(filename)), hash, 'Source was modified: ' + filename);
    await unlink(filename);
  }
  for (const directory of [path.join(source, 'Sessions'), source, sourceRoot]) await rmdir(directory);
}
