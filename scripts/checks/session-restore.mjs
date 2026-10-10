// Run after npm run rebuild: node scripts/checks/session-restore.mjs [binary]
// Force-quits only the test browser, using disposable profiles and offline tabs.
import {strict as assert} from 'node:assert';
import {spawn} from 'node:child_process';
import {cp, mkdir, mkdtemp, readFile, readdir, rm, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const binary = resolve(process.argv[2] ??
    'engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug');
const profile = await mkdtemp(join(tmpdir(), 'dao-session-check-'));
const sockets = [];
let child;
let stderr = '';

async function until(fn, label) {
  for (let i = 0; i < 300; i++) {
    const value = await fn();
    if (value) return value;
    if (child.exitCode !== null || child.signalCode !== null) break;
    await delay(100);
  }
  throw new Error(`Timed out: ${label}\n${stderr}`);
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

const readJSON = async path => JSON.parse(await readFile(path, 'utf8'));

async function start() {
  await rm(join(profile, 'DevToolsActivePort'), {force: true});
  stderr = '';
  child = spawn(binary, ['--headless', `--user-data-dir=${profile}`,
    '--remote-debugging-port=0', '--no-first-run', '--no-default-browser-check',
    '--use-mock-keychain', '--disable-background-networking', '--enable-automation'],
  {stdio: ['ignore', 'ignore', 'pipe']});
  child.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-5000); });
  child.on('error', error => { stderr += error.message; });
  const port = await until(async () => {
    try { return (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
  }, 'DevTools port');
  const targets = async () => (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
  return {send: await connect(version.webSocketDebuggerUrl), targets};
}

async function stop(browser, crash = false) {
  const exit = new Promise(resolve => child.once('exit', resolve));
  if (crash) child.kill('SIGKILL');
  else await browser.send('Browser.close');
  await exit;
  sockets.splice(0).forEach(socket => socket.close());
}

async function waitForSavedTab(url) {
  // Wait for Chromium's delayed session write before simulating process death.
  await until(async () => {
    for (const name of ['Default', 'Profile 1']) {
      const directory = join(profile, name, 'Sessions');
      let files;
      try { files = await readdir(directory); }
      catch (error) { if (error.code === 'ENOENT') continue; throw error; }
      for (const file of files.filter(file => file.startsWith('Session_'))) {
        if ((await readFile(join(directory, file))).includes(Buffer.from(url))) {
          return true;
        }
      }
    }
    return false;
  }, `session saved for ${url}`);
}

const expected = ['data:text/html,session-one', 'data:text/html,session-two'];
async function expectRestored(browser, addedTab = false, profileCount = 2) {
  let pages = [];
  try {
    await until(async () => {
      pages = (await browser.targets()).filter(t => t.type === 'page').map(t => t.url);
      return pages.filter(url => url === 'dao://sidebar/').length === profileCount &&
          expected.every(url => pages.filter(page => page === url).length >= profileCount) &&
          (!addedTab || pages.includes('data:text/html,after-crash'));
    }, `${profileCount} profile(s) restored their saved tabs`);
  } catch (error) {
    throw new Error(`Restore failed; open pages: ${JSON.stringify(pages)}`, {cause: error});
  }
  for (const url of expected) {
    assert.equal(pages.filter(page => page === url).length, profileCount,
        `Every profile must restore ${url}; got ${JSON.stringify(pages)}`);
  }
  if (addedTab) assert(pages.includes('data:text/html,after-crash'),
      'Tabs opened after recovery must survive the next restart');
  assert(!pages.includes('data:text/html,legacy-startup-page'),
      'Legacy startup pages must not replace or accompany the saved session');
}

try {
  await mkdir(join(profile, 'Default'));
  await writeFile(join(profile, 'Default', 'Preferences'),
      JSON.stringify({dao: {welcome_shown: true}, session: {restore_on_startup: 5}}));
  let browser = await start();
  for (const url of expected) {
    await browser.send('Target.createTarget', {url});
    await waitForSavedTab(url);
  }
  await stop(browser);
  assert.equal((await readJSON(join(profile, 'Default', 'Preferences')))
      .session.restore_on_startup, 5, 'Keep the legacy blank-page setting in the fixture');
  browser = await start();
  await expectRestored(browser, false, 1);
  console.log('PASS: clean restart restores despite the legacy blank-page setting');
  await until(async () => {
    const prefs = await readJSON(join(profile, 'Default', 'Preferences'));
    const state = await readJSON(join(profile, 'Local State'));
    return prefs.profile?.exit_type === 'Crashed' && state.profile?.info_cache?.Default;
  }, 'crash marker and profile metadata saved');
  await stop(browser, true);

  // Seed another crashed profile: only the first profile gets PROCESS_STARTUP.
  await cp(join(profile, 'Default'), join(profile, 'Profile 1'), {recursive: true});
  const secondPrefsPath = join(profile, 'Profile 1', 'Preferences');
  const secondPrefs = await readJSON(secondPrefsPath);
  secondPrefs.session = {restore_on_startup: 4,
    startup_urls: ['data:text/html,legacy-startup-page']};
  await writeFile(secondPrefsPath, JSON.stringify(secondPrefs));
  const statePath = join(profile, 'Local State');
  const state = await readJSON(statePath);
  state.profile.info_cache['Profile 1'] = {...state.profile.info_cache.Default, name: 'Second profile'};
  state.profile.last_active_profiles = ['Default', 'Profile 1'];
  state.profile.last_used = 'Default';
  state.profile.show_picker_on_startup = false;
  await writeFile(statePath, JSON.stringify(state));

  browser = await start();
  await expectRestored(browser);
  await browser.send('Target.createTarget', {url: 'data:text/html,after-crash'});
  await waitForSavedTab('data:text/html,after-crash');
  await stop(browser, true);

  browser = await start();
  await expectRestored(browser, true);
  await stop(browser);
  browser = await start();
  await expectRestored(browser, true);
  await stop(browser);
  console.log('PASS: both profiles restore after force-quit and clean restart despite legacy startup settings');
} finally {
  if (child && child.exitCode === null && child.signalCode === null) {
    const exit = new Promise(resolve => child.once('exit', resolve));
    child.kill('SIGKILL');
    await exit;
  }
  sockets.splice(0).forEach(socket => socket.close());
  await rm(profile, {recursive: true, force: true});
}
