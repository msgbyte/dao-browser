// Run after npm run rebuild. Isolated profiles; local pages; no user data.
// Usage: node scripts/checks/resource-usage.mjs [--benchmark] [--functional] [--settings]
import {strict as assert} from 'node:assert';
import {spawn} from 'node:child_process';
import {mkdir, mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const binary = resolve(process.env.DAO_BINARY ??
    'engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug');
const duration = Number(process.env.DAO_SAMPLE_SECONDS ?? 180);
const tabCount = Number(process.env.DAO_SAMPLE_TABS ?? 100);
const repetitions = Number(process.env.DAO_SAMPLE_PAIRS ?? 2);
const reportPath = process.env.DAO_SAMPLE_REPORT ?? '/tmp/dao-resource-usage.json';
const server = createServer((req, res) => {
  res.setHeader('Content-Type', 'text/html');
  res.end('<!doctype html><title>Resource check</title><link rel="icon" href="data:,">' +
    '<h1>Resource check</h1><p>Static local page</p>');
});
await new Promise(ok => server.listen(0, '127.0.0.1', ok));
const port = server.address().port;
let child, profile, stderr = '';
const sockets = [];

async function until(fn, label, seconds = 30) {
  const end = Date.now() + seconds * 1000;
  while (Date.now() < end) {
    const result = await fn();
    if (result) return result;
    if (child?.exitCode != null || child?.signalCode != null) {
      throw new Error(`Browser exited: ${stderr}`);
    }
    await delay(500);
  }
  throw new Error(`Timed out: ${label}\n${stderr.slice(-2000)}`);
}
async function connect(url) {
  const socket = new WebSocket(url); sockets.push(socket);
  await new Promise((ok, fail) => {
    const timeout = setTimeout(() => {
      socket.close();
      fail(new Error(`DevTools connection timed out: ${url}`));
    }, 15000);
    socket.onopen = () => { clearTimeout(timeout); ok(); };
    socket.onerror = error => { clearTimeout(timeout); fail(error); };
  });
  let sequence = 0;
  const pending = new Map();
  socket.onmessage = e => {
    const result = JSON.parse(e.data), handler = pending.get(result.id);
    if (!handler) return;
    pending.delete(result.id);
    result.error ? handler.reject(new Error(JSON.stringify(result.error))) : handler.resolve(result.result);
  };
  socket.onclose = () => {
    for (const handler of pending.values()) handler.reject(new Error('CDP disconnected'));
    pending.clear();
  };
  return (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++sequence;
    const timeout = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`DevTools request timed out: ${method}`));
    }, 15000);
    pending.set(id, {
      resolve: value => { clearTimeout(timeout); resolve(value); },
      reject: error => { clearTimeout(timeout); reject(error); },
    });
    socket.send(JSON.stringify({id, method, params}));
  });
}
async function evaluate(send, expression) {
  const result = await send('Runtime.evaluate', {expression, returnByValue: true, awaitPromise: true});
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
async function start(enabled, existingProfile) {
  profile = existingProfile ?? await mkdtemp(join(tmpdir(), 'dao-resource-check-'));
  if (!existingProfile) {
    await mkdir(join(profile, 'Default'));
    await writeFile(join(profile, 'Default', 'Preferences'),
      JSON.stringify({dao: {welcome_shown: true}}));
  }
  // A stopped browser can leave discovery files pointing at its old ports.
  await rm(join(profile, 'DevToolsActivePort'), {force: true});
  await rm(join(profile, 'UIDevToolsActivePort'), {force: true});
  stderr = '';
  child = spawn(binary, ['--headless', `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', '--test-type', '--use-mock-keychain', '--lang=en-US',
    '--disable-background-networking', '--remote-debugging-port=0',
    '--enable-ui-devtools=0', '--enable-features=ui-debug-tools-enable-synthetic-events',
    '--host-resolver-rules=MAP *.resource.test 127.0.0.1',
    // Keep isolated load tests out of the user's system notification center.
    `--disable-features=NativeNotifications,SystemNotifications${enabled ? '' : ',DaoResourceUsageAlerts'}`],
    {stdio: ['ignore', 'ignore', 'pipe']});
  child.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-100000); });
  const debugPort = await until(async () => {
    try { return (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; }
    catch { return false; }
  }, 'DevTools port');
  const targets = () => fetch(`http://127.0.0.1:${debugPort}/json/list`).then(r => r.json());
  const version = await fetch(`http://127.0.0.1:${debugPort}/json/version`).then(r => r.json());
  const send = await connect(version.webSocketDebuggerUrl);
  const sidebarTarget = await until(async () => (await targets()).find(t => t.url === 'dao://sidebar/'), 'sidebar');
  const sidebar = await connect(sidebarTarget.webSocketDebuggerUrl);
  await until(() => evaluate(sidebar, '!!document.querySelector("dao-sidebar-app")?.foldersLoaded_'), 'sidebar ready');
  return {send, sidebar, targets};
}
async function stop(browser, keepProfile = false) {
  if (child && child.exitCode == null && child.signalCode == null) {
    const exit = new Promise(ok => child.once('exit', ok));
    try { await browser.send('Browser.close'); } catch {}
    await Promise.race([exit, delay(10000)]);
    if (child.exitCode == null && child.signalCode == null) { child.kill('SIGTERM'); await exit; }
  }
  sockets.splice(0).forEach(s => s.close());
  if (!keepProfile) await rm(profile, {recursive: true, force: true});
  child = null;
}
async function sample() {
  const process = spawn('python3', [resolve('scripts/checks/resource-usage-macos.py'),
    String(child.pid), String(duration)], {stdio: ['ignore', 'pipe', 'pipe']});
  let output = '', error = '';
  process.stdout.on('data', c => { output += c; });
  process.stderr.on('data', c => { error += c; });
  const code = await new Promise(ok => process.on('exit', ok));
  assert.equal(code, 0, error);
  return JSON.parse(output);
}
async function benchmark() {
  const results = [];
  for (let pair = 0; pair < repetitions; pair++) {
    // Reverse the order for the second pair to reduce warm-up/order bias.
    for (const enabled of pair % 2 ? [true, false] : [false, true]) {
      let browser;
      try {
        browser = await start(enabled);
        for (let i = 0; i < tabCount; i++) {
          await browser.send('Target.createTarget', {
            url: `http://p${i % 20}.resource.test:${port}/?tab=${i}`, background: true});
        }
        await until(() => evaluate(browser.sidebar,
          `document.querySelector('dao-sidebar-app').unpinnedTabs_.filter(t => t.url.includes('.resource.test')).length === ${tabCount}`), 'all benchmark tabs');
        console.log(`WARMUP pair=${pair} enabled=${enabled} tabs=${tabCount}`);
        await delay(120000);
        // No DevTools polling during the measurement window.
        console.log(`SAMPLE pair=${pair} enabled=${enabled} seconds=${duration}`);
        const queryNames = ['PerformanceManager.ResourceQueryTime.CPUMeasurementMonitor',
          'PerformanceManager.ResourceQueryTime.MemoryMeasurementProvider'];
        const histograms = async () => Object.fromEntries(await Promise.all(queryNames.map(async name => {
          try { return [name, (await browser.send('Browser.getHistogram', {name})).histogram]; }
          catch { return [name, {count: 0, sum: 0}]; }
        })));
        const before = await histograms();
        const measurement = await sample();
        const after = await histograms();
        measurement.resourceQueries = Object.fromEntries(queryNames.map(name => [name, {
          count: after[name].count - before[name].count,
          // These two Chromium provider histograms use microsecond units.
          processingMicroseconds: after[name].sum - before[name].sum,
        }]));
        results.push({pair, enabled, tabCount, ...measurement});
        await writeFile(reportPath, JSON.stringify({binary, results}, null, 2));
        console.log(JSON.stringify({pair, enabled, cpu: measurement.cpuPercentOneCore,
          rss: measurement.medianRssMiB, wakeups: measurement.interruptWakeupsPerSecond}));
      } finally { if (browser) await stop(browser); }
    }
  }
  console.log(`REPORT ${reportPath}`);
}
async function functional() {
  let browser;
  try {
    browser = await start(true);
    const {targetId} = await browser.send('Target.createTarget', {url: `http://load.resource.test:${port}/`});
    const target = await until(async () => (await browser.targets()).find(t => t.id === targetId), 'load target');
    const page = await connect(target.webSocketDebuggerUrl);
    await evaluate(page, `window.worker = new Worker(URL.createObjectURL(new Blob(['while (true) {}'], {type: 'text/javascript'}))); true`);
    const getAlert = () => evaluate(browser.sidebar,
      `document.querySelector('dao-sidebar-app').unpinnedTabs_.find(t => t.url.includes('load.resource.test'))?.resourceAlert`);
    await until(async () => (await getAlert()) === 'cpu', 'sustained CPU marker', 210);
    console.log('PASS real sustained CPU produces a sidebar marker');
    const uiPort = (await readFile(join(profile, 'UIDevToolsActivePort'), 'utf8')).trim();
    const native = await connect(`ws://127.0.0.1:${uiPort}/0`);
    const flatten = node => [node, ...(node.children ?? []).flatMap(flatten)];
    const nodes = flatten((await native('DOM.getDocument')).root);
    const toast = nodes.find(n => (n.attributes ?? []).includes('DaoToastView'));
    assert(toast, 'Shared native toast must exist');
    const props = async node => {
      const styles = await native('CSS.getMatchedStylesForNode', {nodeId: node.nodeId});
      return Object.fromEntries((styles.matchedCSSRules ?? []).flatMap(m =>
        m.rule.style.cssProperties.map(p => [p.name, p.value])));
    };
    const visible = async () => String((await props(toast)).Visible).toLowerCase() === 'true';
    assert.equal(await visible(), false, 'Foreground CPU load must not show a reminder');
    const quiet = await browser.send('Target.createTarget', {url: `http://quiet.resource.test:${port}/`});
    await browser.send('Target.activateTarget', {targetId: quiet.targetId});
    await until(visible, 'background persistent reminder', 120);
    await delay(35000);
    assert.equal(await visible(), true, 'Reminder must survive ordinary toast timeouts and another sample');
    console.log('PASS foreground suppression and background reminder persistence');
    // Native macOS UI follows the system locale even with --lang=en-US.
    // The shared toast's last button is the explicit dismiss control.
    const buttons = flatten(toast).filter(n => (n.attributes ?? []).includes('LabelButton'));
    assert.equal(buttons.length, 2, 'Reminder must expose view-tab and dismiss controls');
    const close = buttons.at(-1);
    assert.equal(String((await props(close)).Visible).toLowerCase(), 'true');
    for (const type of ['mousePressed', 'mouseReleased']) {
      await native('DOM.dispatchMouseEvent', {nodeId: close.nodeId,
        event: {type, x: 8, y: 8, button: 'left', wheelDirection: 'none'}});
    }
    await until(async () => !(await visible()), 'manual dismissal');
    await delay(35000);
    assert.equal(await visible(), false, 'Next sample must not resurrect the dismissed episode');
    assert.equal(await getAlert(), 'cpu', 'Dismissing the reminder must retain the sidebar marker');
    console.log('PASS manual dismissal survives the next sample and retains the marker');
    await evaluate(page, 'worker.terminate(); true');
    await until(async () => !(await getAlert()), 'CPU recovery', 120);
    console.log('PASS CPU marker clears after sustained recovery');
  } catch (error) {
    await writeFile('/tmp/dao-resource-functional-browser.log', stderr);
    console.error('FUNCTIONAL FAILURE', error.message, stderr.slice(-4000));
    throw error;
  } finally { if (browser) await stop(browser); }
}

async function settingsCheck() {
  let browser;
  const openSettings = async () => {
    const {targetId} = await browser.send('Target.createTarget', {url: 'dao://settings/performance'});
    const target = await until(async () => (await browser.targets()).find(t => t.id === targetId), 'settings target');
    const send = await connect(target.webSocketDebuggerUrl);
    const ui = body => evaluate(send, `(() => {
      function find(root = document) {
        const toggle = root.querySelector('#performanceInterventionToggleButton');
        if (toggle) return toggle;
        for (const element of root.querySelectorAll('*')) {
          if (element.shadowRoot) {
            const found = find(element.shadowRoot);
            if (found) return found;
          }
        }
        return null;
      }
      const toggle = find();
      ${body}
    })()`);
    await until(() => ui('return !!toggle;'), 'performance reminder control');
    return {send, ui};
  };
  try {
    browser = await start(true);
    const settings = await openSettings();
    await until(() => settings.ui('return toggle.checked === true;'), 'default enabled');
    assert(await settings.ui('return !!toggle.label && !!toggle.subLabel;'), 'Localized setting copy');
    const screenshot = await settings.send('Page.captureScreenshot');
    await writeFile('/tmp/dao-resource-settings.png', Buffer.from(screenshot.data, 'base64'));

    const {targetId} = await browser.send('Target.createTarget', {url: `http://load.resource.test:${port}/`});
    const target = await until(async () => (await browser.targets()).find(t => t.id === targetId), 'load target');
    const page = await connect(target.webSocketDebuggerUrl);
    await evaluate(page, `window.worker = new Worker(URL.createObjectURL(new Blob(['while (true) {}'], {type: 'text/javascript'}))); true`);
    const getAlert = () => evaluate(browser.sidebar,
      `document.querySelector('dao-sidebar-app').unpinnedTabs_.find(t => t.url.includes('load.resource.test'))?.resourceAlert`);
    await until(async () => (await getAlert()) === 'cpu', 'CPU alert before disabling', 210);
    await settings.ui('toggle.click();');
    await until(() => settings.ui('return toggle.checked === false;'), 'disabled setting');
    await until(async () => !(await getAlert()), 'setting clears existing marker');
    await delay(35000);
    assert.equal(await getAlert(), '', 'A disabled monitor must stay clear after its former timer deadline');
    await settings.ui('toggle.click();');
    await until(() => settings.ui('return toggle.checked === true;'), 'live re-enable');
    assert.equal(await getAlert(), '', 'Re-enabling must start with fresh history');
    await settings.ui('toggle.click();');
    await until(() => settings.ui('return toggle.checked === false;'), 'disabled before restart');
    await evaluate(page, 'worker.terminate(); true');
    const savedProfile = profile;
    await stop(browser, true);
    browser = null;
    const localState = JSON.parse(await readFile(join(savedProfile, 'Local State'), 'utf8'));
    assert.equal(localState.performance_tuning.intervention_notification.enabled, false);
    console.log('PASS live setting changes and saved disabled preference');
    browser = await start(true, savedProfile);
    const restored = await openSettings();
    await until(() => restored.ui('return toggle.checked === false;'), 'disabled setting survives restart');
    console.log('PASS default-on Settings control, live disable/clear, stopped timer, fresh re-enable, and restart persistence');
  } finally { if (browser) await stop(browser); }
}
try {
  if (process.argv.includes('--functional') || process.argv.length === 2) await functional();
  if (process.argv.includes('--settings')) await settingsCheck();
  if (process.argv.includes('--benchmark')) await benchmark();
} finally {
  if (child && child.exitCode == null && child.signalCode == null) {
    const exited = new Promise(ok => child.once('exit', ok));
    child.kill('SIGTERM');
    await exited;
  }
  sockets.forEach(s => s.close());
  server.closeAllConnections();
  await new Promise(ok => server.close(ok));
  if (profile) await rm(profile, {recursive: true, force: true});
}
