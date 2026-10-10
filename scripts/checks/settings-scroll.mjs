// Run after npm run rebuild: node scripts/checks/settings-scroll.mjs [browser]
// Uses a disposable profile without credentials or changes to user settings.
import {strict as assert} from 'node:assert';
import {spawn} from 'node:child_process';
import {mkdtemp, readFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const binary = resolve(process.argv[2] ??
    'engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug');
const profile = await mkdtemp(join(tmpdir(), 'dao-settings-scroll-'));
const child = spawn(binary, ['--headless', `--user-data-dir=${profile}`,
  '--remote-debugging-port=0', '--no-first-run', '--no-default-browser-check',
  '--use-mock-keychain', '--disable-background-networking', '--enable-automation'],
{stdio: ['ignore', 'ignore', 'pipe']});
let stderr = '', launchError, socket;
child.on('error', error => { launchError = error; });
child.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-5000); });
const exited = new Promise(resolve => child.once('close', resolve));

async function until(fn, label) {
  for (let i = 0; i < 300; i++) {
    if (launchError) throw launchError;
    if (child.exitCode !== null || child.signalCode !== null) {
      throw new Error(`Browser exited: ${stderr}`);
    }
    const result = await fn();
    if (result) return result;
    await delay(100);
  }
  throw new Error(`Timed out: ${label}\n${stderr}`);
}

try {
  const port = await until(async () => {
    try { return (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; }
    catch { return null; }
  }, 'DevTools port');
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const page = targets.find(target => target.type === 'page' &&
      !['dao://agent/', 'dao://sidebar/'].includes(target.url));
  assert(page, 'Browser page exists');
  socket = new WebSocket(page.webSocketDebuggerUrl);
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
    message.error ? handler.reject(new Error(JSON.stringify(message.error))) :
        handler.resolve(message.result);
  };
  socket.onclose = () => {
    for (const handler of pending.values()) handler.reject(new Error('CDP disconnected'));
    pending.clear();
  };
  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++sequence;
    pending.set(id, {resolve, reject});
    socket.send(JSON.stringify({id, method, params}));
  });
  const ui = async body => {
    const result = await send('Runtime.evaluate', {
      expression: `(async () => {
        const ui = document.querySelector('settings-ui');
        const main = ui?.$.main;
        const container = ui?.$.container;
        const index = main?.shadowRoot.querySelector('settings-dao-agent-page-index');
        ${body}
      })()`, awaitPromise: true, returnByValue: true,
    });
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  const settle = () => ui(`
    await main.whenViewSwitchingDone();
    await new Promise(requestAnimationFrame);
    await new Promise(requestAnimationFrame);
    return container.scrollTop;
  `);
  await send('Emulation.setDeviceMetricsOverride', {
    width: 1280, height: 800, deviceScaleFactor: 1, mobile: false,
  });
  await send('Page.navigate', {url: 'dao://settings/'});
  await until(() => ui('return !!index?.$.viewManager;'), 'Settings overview');
  await settle();

  for (let visit = 0; visit < 2; visit++) {
    const overviewScroll = await ui(`
      main.scrollToOverviewSection('agent', 'auto');
      return container.scrollTop;
    `);
    assert(overviewScroll > 0, 'Overview is scrolled before opening Agent');
    await ui(`index.shadowRoot.querySelector('#openAgentSettings').click();`);
    assert.equal(await settle(), 0, 'Agent details must start at the top');
    await until(() => ui(`return !!index.shadowRoot.querySelector(
        'settings-dao-agent-page')?.shadowRoot.querySelector('#daoAgentProvider');`),
    'Agent configuration loaded');
    assert.equal(await settle(), 0, 'Loading Agent configuration must keep the page at the top');
    await ui('container.scrollTop = container.scrollHeight;');
    assert(await ui('return container.scrollTop > 0;'), 'Agent details can scroll');
    await ui('history.back();');
    await until(() => ui(`return main.shadowRoot.querySelectorAll(
        '#switcher > [slot=view].active').length > 1;`), 'Return to overview');
    assert(Math.abs(await settle() - overviewScroll) < 2,
        'Back must restore the overview position, not the Agent detail position');
  }
  console.log('PASS: Agent entry, repeated entry, and overview scroll restoration');
} finally {
  socket?.close();
  if (child.exitCode === null && child.signalCode === null) child.kill('SIGTERM');
  const killTimer = setTimeout(() => child.kill('SIGKILL'), 5000);
  await exited;
  clearTimeout(killTimer);
  await rm(profile, {recursive: true, force: true});
}
