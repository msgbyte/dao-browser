// Run after npm run rebuild: node scripts/checks/jev-plugin.mjs
// Uses an isolated profile and a local Jev-compatible server; no real credentials.
import {strict as assert} from 'node:assert';
import {spawn} from 'node:child_process';
import {mkdtemp, readFile, realpath, rm, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {tmpdir} from 'node:os';
import {dirname, join, resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const binary = resolve(process.argv[2] ??
    'engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug');
const profile = await realpath(await mkdtemp(join(tmpdir(), 'dao-jev-check-')));
const sockets = [];
let child, helper, port, stderr = '', mode = 'normal', requests = 0, disconnected = 0;
let pendingResponse, pendingBody, redirectedRequests = 0, submissions = 0, navigationDelay = 0;
const choice = (question, selected) => ({choice: selected, confidence: 1,
  probabilities: Object.fromEntries(Object.keys(question.criteria).map(key => [key, Number(key === selected)]))});
function normalAnswer(body) {
  const q = body.questions;
  const fills = Object.entries(q.type_text_target?.criteria ?? {});
  const fill = fills.find(([, value]) => value.element === 'Name');
  const click = Object.entries(q.click_target?.criteria ?? {}).find(([, value]) => value.element === 'Submit');
  const answers = {operation: choice(q.operation, fill ? 'TYPE_TEXT' : 'CLICK')};
  if (fill) {
    answers.type_text_target = choice(q.type_text_target, fill[0]);
    const input = Object.entries(q.type_text_input.criteria).find(([, value]) => value.field === 'Name' && value.value === 'Alice');
    assert(input, 'Known Name input is offered');
    answers.type_text_input = choice(q.type_text_input, input[0]);
    if (mode === 'forged-target') answers.type_text_target.choice = 'not-offered';
    if (mode === 'forged-input') answers.type_text_input.choice = '999';
  } else { assert(click, 'Submit is offered'); answers.click_target = choice(q.click_target, click[0]); }
  return {answers};
}
const server = createServer(async (req, res) => {
  if (req.url === '/saved') {
    submissions++;
    await delay(navigationDelay);
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.end('<!doctype html><p>Saved successfully</p>');
    return;
  }
  if (req.url === '/form') {
    res.setHeader('Content-Type', 'text/html; charset=utf-8');
    res.end(`<!doctype html><title>Jev local integration</title><style>body{font:20px sans-serif;margin:80px}input,button{font:inherit;padding:10px}</style><form onsubmit="event.preventDefault();document.getElementById('status').textContent='Saved successfully'"><label>Name <input name="name" aria-label="Name"></label><button type="submit">Submit</button></form><p id="status">Waiting for submission</p>${['', 'true', 'plaintext-only', 'TRUE', 'PLAINTEXT-ONLY'].map(value => `<div contenteditable="${value}">private-editor-draft <button>private-editor-action</button></div>`).join('')}`);
    return;
  }
  if (req.url === '/sink') { redirectedRequests++; res.end('{}'); return; }
  if (req.url !== '/jev') { res.writeHead(404).end(); return; }
  requests++;
  assert.equal(req.headers.authorization, 'Bearer local-test-token');
  let raw = '';
  for await (const data of req) raw += data;
  assert(!raw.includes('local-test-token'), 'Token is excluded from model input');
  assert(!raw.includes('private-editor-'), 'Editable drafts and nested controls are excluded');
  const body = JSON.parse(raw);
  assert.equal(body.model, 'jev-latest');
  for (const question of Object.values(body.questions)) {
    assert(Object.keys(question.criteria).length <= 255, 'Every choice respects the service limit');
  }
  if (mode === 'hold') {
    pendingResponse = res;
    pendingBody = body;
    res.on('close', () => disconnected++);
    return;
  }
  if (mode === 'redirect') { res.writeHead(302, {Location: `${origin}/sink`}).end(); return; }
  const q = body.questions;
  if (['done', 'wait', 'blocked', 'bad-probability', 'forged-action'].includes(mode)) {
    const answer = choice(q.operation, mode === 'done' ? 'DONE' : mode === 'blocked' ? 'BLOCKED' : 'WAIT');
    if (mode === 'bad-probability') answer.probabilities.WAIT = 0.5;
    if (mode === 'forged-action') answer.choice = 'DELETE';
    res.end(JSON.stringify({answers: {operation: answer}}));
    return;
  }
  res.setHeader('Content-Type', 'application/json');
  res.end(JSON.stringify(normalAnswer(body)));
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
async function until(fn, label) {
  for (let i = 0; i < 300; i++) {
    const result = await fn();
    if (result) return result;
    if (child?.exitCode != null || child?.signalCode != null) throw new Error(`Browser exited: ${stderr.slice(-2000)}`);
    await delay(100);
  }
  throw new Error(`Timed out: ${label}\n${stderr.slice(-2000)}`);
}
async function connect(url) {
  const socket = new WebSocket(url); sockets.push(socket);
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
  let sequence = 0; const pending = new Map();
  socket.onmessage = event => {
    const msg = JSON.parse(event.data); const handler = pending.get(msg.id);
    if (!handler) return;
    pending.delete(msg.id);
    msg.error ? handler.reject(new Error(JSON.stringify(msg.error))) : handler.resolve(msg.result);
  };
  socket.onclose = () => { for (const handler of pending.values()) handler.reject(new Error('CDP disconnected')); pending.clear(); };
  return (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++sequence; pending.set(id, {resolve, reject}); socket.send(JSON.stringify({id, method, params}));
  });
}
async function evaluate(send, expression) {
  const result = await send('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true});
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}
function uiFor(send) {
  return body => evaluate(send, `(() => {
    function find(selector, root = document) {
      const match = root.querySelector(selector); if (match) return match;
      for (const el of root.querySelectorAll('*')) if (el.shadowRoot) {
        const found = find(selector, el.shadowRoot); if (found) return found;
      }
      return null;
    }
    ${body}
  })()`);
}
const targets = async () => (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
const enabled = '[data-setting="dao_plugin_jev_enabled"]';
const url = '[data-setting="dao_plugin_jev_url"]';
const token = '[data-setting="dao_plugin_jev_token"]';
const permission = '[data-setting="dao_plugin_jev_permission"]';
async function start() {
  await rm(join(profile, 'DevToolsActivePort'), {force: true});
  await rm(join(profile, 'UIDevToolsActivePort'), {force: true});
  child = spawn(binary, ['--headless', `--user-data-dir=${profile}`, '--remote-debugging-port=0',
    '--no-first-run', '--no-default-browser-check', '--use-mock-keychain',
    '--disable-background-networking', '--enable-automation', '--enable-ui-devtools=0',
    '--enable-features=ui-debug-tools-enable-synthetic-events'], {stdio: ['ignore', 'ignore', 'pipe']});
  child.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-10000); });
  port = await until(async () => { try { return (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; } catch { return null; } }, 'DevTools port');
  const page = (await targets()).find(t => t.type === 'page' && !['dao://agent/', 'dao://sidebar/'].includes(t.url));
  assert(page); const send = await connect(page.webSocketDebuggerUrl); const ui = uiFor(send);
  await send('Emulation.setDeviceMetricsOverride', {width: 1360, height: 1000, deviceScaleFactor: 1, mobile: false});
  await send('Page.navigate', {url: 'chrome://settings/agent'});
  await until(() => ui(`return !!find('${enabled}');`), 'Jev settings');
  await until(async () => (await targets()).some(target => target.url === 'dao://agent/'), 'hidden Agent preload');
  return {send, ui, targetId: page.id};
}
async function stop(send) {
  const exit = new Promise(resolve => child.once('exit', resolve));
  await send('Browser.close'); await exit;
  sockets.splice(0).forEach(socket => socket.close());
}
function startMcp() {
  helper = spawn(resolve(dirname(binary), '../Helpers/dao-mcp'), [`--user-data-dir=${profile}`]);
  let sequence = 0, buffer = '';
  const pending = new Map();
  helper.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-10000); });
  helper.stdout.on('data', chunk => {
    buffer += chunk;
    while (buffer.includes('\n')) {
      const end = buffer.indexOf('\n');
      const message = JSON.parse(buffer.slice(0, end)); buffer = buffer.slice(end + 1);
      const handler = pending.get(message.id);
      if (!handler) continue;
      pending.delete(message.id);
      message.error ? handler.reject(new Error(JSON.stringify(message.error))) : handler.resolve(message.result);
    }
  });
  helper.on('exit', () => {
    for (const handler of pending.values()) handler.reject(new Error(`MCP helper exited: ${stderr}`));
    pending.clear();
  });
  function notify(method, params) { helper.stdin.write(JSON.stringify({jsonrpc: '2.0', method, params}) + '\n'); }
  function call(method, params) {
    const id = ++sequence;
    const result = new Promise((resolve, reject) => pending.set(id, {resolve, reject}));
    result.catch(() => {}); // The caller may first need to resolve native approval.
    helper.stdin.write(JSON.stringify({jsonrpc: '2.0', id, method, params}) + '\n');
    return {id, result};
  }
  return {call, notify};
}
async function approveIsolatedMcp() {
  // Exercise the real native approval dialog in this disposable headless profile.
  const uiPort = (await readFile(join(profile, 'UIDevToolsActivePort'), 'utf8')).trim();
  const native = await connect(`ws://127.0.0.1:${uiPort}/0`);
  const flatten = node => [node, ...(node.children ?? []).flatMap(flatten)];
  const button = await until(async () => {
    const {root} = await native('DOM.getDocument');
    const nodes = flatten(root);
    for (const node of nodes.filter(node =>
      (node.attributes ?? []).includes('DaoSystemDialogButton'))) {
      const styles = await native('CSS.getMatchedStylesForNode', {nodeId: node.nodeId});
      const properties = (styles.matchedCSSRules ?? []).flatMap(match => match.rule.style.cssProperties);
      if (properties.some(property => property.name === 'Style' && property.value === 'kProminent')) return node;
    }
    return false;
  }, 'isolated MCP Allow button');
  // The dialog's input protector ignores clicks immediately after opening.
  await delay(1000);
  for (const type of ['mousePressed', 'mouseReleased']) {
    await native('DOM.dispatchMouseEvent', {nodeId: button.nodeId,
      event: {type, x: 30, y: 15, button: 'left', wheelDirection: 'none'}});
  }
}
const task = {goal: 'Fill Name with Alice and submit the form', known_inputs: [{field: 'Name', value: 'Alice'}],
  completion: [{kind: 'field', field: 'Name', value: 'Alice'}, {kind: 'text', value: 'Saved successfully'}], max_steps: 5};
try {
  let {send, ui} = await start();
  assert.equal(await ui(`return find('${enabled}').checked;`), false);
  assert.equal(await ui(`return find('${permission}').checked;`), false);
  assert.equal(await ui(`return find('${permission}').disabled;`), true);
  assert.equal(await ui(`return !!find('${url}');`), false);
  assert.equal(await ui(`return find('settings-dao-agent-page').shadowRoot.querySelectorAll('.dao-agent-experimental').length;`), 2);
  await ui(`find('${enabled}').click();`);
  await until(() => ui(`return !!find('${url}');`), 'expanded connection');
  assert.equal(await ui(`return find('${url}').invalid && find('${token}').invalid && find('${permission}').disabled;`), true);
  const setField = async (selector, value) => {
    await ui(`const input = find('${selector}'); input.value = ${JSON.stringify(value)}; input.dispatchEvent(new Event('input', {bubbles: true, composed: true}));`);
    await until(() => ui(`return find('settings-dao-agent-page').agentSettingsValues_[find('${selector}').dataset.setting] === ${JSON.stringify(value)};`), 'field saved');
  };
  await setField(url, `${origin}/jev`);
  await setField(token, 'local-test-token');
  await until(() => ui(`return !find('${permission}').disabled;`), 'valid connection');
  assert.equal(await ui(`return find('${token}').type;`), 'password');
  assert.equal(await ui(`return find('${permission}').checked;`), false);
  await ui(`find('${enabled}').scrollIntoView({block: 'center'});`);
  await delay(100);
  await writeFile('/tmp/dao-jev-connection.png', Buffer.from((await send('Page.captureScreenshot')).data, 'base64'));
  await ui(`find('${permission}').click();`);
  await until(() => ui(`return find('settings-dao-agent-page').agentPlugins_[0].effective;`), 'explicit tool permission');
  await ui(`find('${permission}').scrollIntoView({block: 'center'});`);
  await delay(100);
  await writeFile('/tmp/dao-jev-tools.png', Buffer.from((await send('Page.captureScreenshot')).data, 'base64'));
  await ui(`find('${enabled}').click();`);
  await until(() => ui(`return find('${permission}').disabled;`), 'connection revocation');
  assert.equal(await ui(`return find('settings-dao-agent-page').agentPlugins_[0].effective;`), false);
  await ui(`find('${enabled}').click();`);
  await until(() => ui(`return find('settings-dao-agent-page').agentPlugins_[0].effective;`), 'retained connection');
  assert.equal(requests, 0, 'Editing settings must not send Jev requests');
  await stop(send);
  ({send, ui} = await start());
  await until(() => ui(`return find('settings-dao-agent-page').agentPlugins_[0].effective;`), 'restart persistence');
  assert.equal(await ui(`return find('${url}').value;`), `${origin}/jev`);
  assert.equal(await ui(`return find('${token}').value;`), 'local-test-token');
  assert.equal(requests, 0);
  console.log('PASS settings: defaults, expansion, validation, masking, two badges, explicit permission, collapse and restart persistence; zero inference requests');

  let agent = (await targets()).find(t => t.url === 'dao://agent/');
  if (!agent) {
    const {targetId} = await send('Target.createTarget', {url: 'dao://agent/'});
    agent = await until(async () => (await targets()).find(t => t.id === targetId), 'agent target');
  }
  const agentSend = await connect(agent.webSocketDebuggerUrl);
  await until(() => evaluate(agentSend, `document.readyState === 'complete'`), 'agent load');
  await evaluate(agentSend, `(async () => {
    window.jevBridge = await import('./agent_bridge.js');
    window.jevSettings = await import('./agent_settings_native_bridge.js');
    window.jevPlugins = await import('./agent_plugins.js');
    await window.jevSettings.startAgentSettingsSync();
    return true;
  })()`);
  assert.equal(await evaluate(agentSend, `window.jevPlugins.isPluginEnabled('run_browser_task')`), true);
  assert.equal(await evaluate(agentSend, `localStorage.getItem('dao_plugin_jev_token')`), null);
  const {targetId: formId} = await send('Target.createTarget', {url: `${origin}/form`});
  const formTarget = await until(async () => (await targets()).find(t => t.id === formId), 'form target');
  const formSend = await connect(formTarget.webSocketDebuggerUrl);
  await until(() => evaluate(formSend, `!!document.querySelector('input')`), 'form loaded');
  await send('Target.activateTarget', {targetId: formId});
  const turn = await evaluate(agentSend, `window.jevBridge.callNative('beginAgentTurn')`);
  assert(turn.success, JSON.stringify(turn));
  const result = await evaluate(agentSend, `window.jevBridge.executeTool('run_browser_task', ${JSON.stringify(task)})`);
  console.log('Native integration result:', JSON.stringify(result));
  assert.equal(result.status, 'completed');
  assert.deepEqual(result.progress.actions.map(a => a.operation), ['TYPE_TEXT', 'CLICK']);
  assert.equal(await evaluate(formSend, `document.querySelector('input').value`), 'Alice');
  assert.equal(requests, 2);
  await evaluate(agentSend, `window.jevBridge.callNative('endAgentTurn', {turnId: ${JSON.stringify(turn.turnId)}})`);

  const navigationTask = {...task, completion: [{kind: 'text', value: 'Saved successfully'}]};
  const navigationButtons = [
    '<button type="submit">Submit</button>',
    '<input type="submit" value="Submit">',
    '<div role="button" tabindex="0" onclick="this.closest(\'form\').requestSubmit()">Submit</div>',
  ];
  const prepareNavigation = async (button = navigationButtons[0]) => {
    await formSend('Page.navigate', {url: `${origin}/form`});
    await until(() => evaluate(formSend, `location.pathname === '/form' && !!document.querySelector('input') && !document.querySelector('input').value`), 'fresh navigation form');
    await evaluate(formSend, `document.querySelector('button[type="submit"]').outerHTML = ${JSON.stringify(button)}`);
    await evaluate(formSend, `document.querySelector('form').onsubmit = event => { event.preventDefault(); location.href = '/saved'; }`);
  };
  const checkNavigation = async (run, index) => {
    navigationDelay = index === 1 ? 200 : 0;
    const before = submissions;
    await prepareNavigation(navigationButtons[index]);
    const navigated = await run(navigationTask);
    assert.equal(navigated.status, 'completed', JSON.stringify(navigated));
    assert.equal(await evaluate(formSend, 'location.pathname'), '/saved');
    assert.equal(submissions - before, 1, 'A navigation must not repeat the submission');
  };
  for (let i = 0; i < 3; i++) {
    const navigationTurn = await evaluate(agentSend, `window.jevBridge.callNative('beginAgentTurn')`);
    assert(navigationTurn.success);
    await checkNavigation(args => evaluate(agentSend, `window.jevBridge.executeTool('run_browser_task', ${JSON.stringify(args)})`), i);
    await evaluate(agentSend, `window.jevBridge.callNative('endAgentTurn', {turnId: ${JSON.stringify(navigationTurn.turnId)}})`);
  }
  console.log('PASS Agent waits for form navigation and verifies completion without resubmitting');

  await formSend('Page.navigate', {url: `${origin}/form`});
  await until(() => evaluate(formSend, `!!document.querySelector('input') && !document.querySelector('input').value`), 'fresh form');
  mode = 'redirect';
  const redirectTurn = await evaluate(agentSend, `window.jevBridge.callNative('beginAgentTurn')`);
  assert(redirectTurn.success);
  const redirected = await evaluate(agentSend, `window.jevBridge.executeTool('run_browser_task', ${JSON.stringify(task)})`);
  assert.equal(redirected.status, 'network_error');
  assert.equal(redirectedRequests, 0, 'Credentialed redirects are not followed');
  await evaluate(agentSend, `window.jevBridge.callNative('endAgentTurn', {turnId: ${JSON.stringify(redirectTurn.turnId)}})`);
  console.log('PASS credentialed redirect is rejected before reaching the redirect target');

  mode = 'hold';
  const nextTurn = await evaluate(agentSend, `window.jevBridge.callNative('beginAgentTurn')`);
  assert(nextTurn.success);
  const running = evaluate(agentSend, `window.jevBridge.executeTool('run_browser_task', ${JSON.stringify(task)})`);
  await until(() => pendingResponse, 'pending service request');
  await evaluate(agentSend, `window.jevSettings.setCanonicalAgentSetting('dao_plugin_jev_permission', 'false')`);
  const cancelled = await running;
  assert.notEqual(cancelled.status, 'completed');
  await until(() => disconnected > 0, 'native network cancellation');
  pendingResponse.end(JSON.stringify({answers: {}}));
  await delay(300);
  assert.equal(await evaluate(formSend, `document.querySelector('input').value`), '');
  const before = requests;
  const rejected = await evaluate(agentSend, `window.jevBridge.executeTool('run_browser_task', ${JSON.stringify(task)})`);
  assert.equal(rejected.code, 'permission_denied');
  assert.equal(requests, before);
  console.log('PASS native integration: guarded fill + click + local AND completion; native request cancellation, no late action, stale tool permission rejection');
  await evaluate(agentSend, `window.jevBridge.callNative('endAgentTurn', {turnId: ${JSON.stringify(nextTurn.turnId)}})`);

  await evaluate(formSend, `(() => {
    window.waitPolls = 0;
    const query = document.querySelectorAll.bind(document);
    document.querySelectorAll = selector => {
      if (selector === '#never-created') window.waitPolls++;
      return query(selector);
    };
  })()`);
  assert((await evaluate(agentSend, `window.jevBridge.callNative('beginAgentTurn')`)).success);
  await evaluate(agentSend, `(() => {
    window.pendingWait = window.jevBridge.executeTool('wait_for_element', {
      scope: {selector: '#never-created'}, timeout_ms: 30000,
    });
    return true;
  })()`);
  await until(() => evaluate(formSend, `window.waitPolls > 0`), 'native wait started');
  await agentSend('Page.reload');
  await until(() => evaluate(agentSend, `document.readyState === 'complete' && !window.jevBridge`), 'agent reloaded');
  await evaluate(agentSend, `import('./agent_bridge.js').then(bridge => { window.jevBridge = bridge; })`);
  const resumedTurn = await evaluate(agentSend, `window.jevBridge.callNative('beginAgentTurn')`);
  assert(resumedTurn.success, JSON.stringify(resumedTurn));
  const observed = await evaluate(agentSend, `window.jevBridge.executeTool('get_accessibility_tree', {filter: 'compact'})`);
  assert.equal(observed.url, `${origin}/form`);
  await evaluate(agentSend, `window.jevBridge.callNative('endAgentTurn', {turnId: ${JSON.stringify(resumedTurn.turnId)}})`);
  console.log('PASS pending native wait is cancelled on Agent reload; browser survives and a new turn executes tools');

  await ui(`find('${permission}').click();`);
  await until(() => ui(`return find('settings-dao-agent-page').agentPlugins_[0].effective;`), 'Jev re-enabled for MCP');
  await evaluate(send, `chrome.send('setDaoMcpEnabled', [true])`);
  await until(async () => { try { return await readFile(join(profile, 'MCP/runtime.json'), 'utf8'); } catch { return false; } }, 'MCP runtime');
  const runtime = JSON.parse(await readFile(join(profile, 'MCP/runtime.json'), 'utf8'));
  assert.equal(runtime.socket_path, join(profile, 'MCP/mcp.sock'), 'Helper and browser must use the same profile path');
  // Unload Agent documents, including the browser's prewarmed hidden WebView.
  // Keeping that WebView alive at about:blank prevents it from being recreated.
  for (const target of await targets()) {
    if (target.url === 'dao://agent/') {
      const agentPage = await connect(target.webSocketDebuggerUrl);
      await agentPage('Page.navigate', {url: 'about:blank'});
    }
  }
  await until(async () => !(await targets()).some(target => target.url === 'dao://agent/'), 'Agent documents unloaded');
  await send('Target.activateTarget', {targetId: formId});
  const mcp = startMcp();
  await mcp.call('initialize', {protocolVersion: '2025-11-25', capabilities: {},
    clientInfo: {name: 'Jev isolated integration', version: '1'}}).result;
  mcp.notify('notifications/initialized', {});
  const catalog = await mcp.call('tools/list', {}).result;
  assert.equal(catalog.tools.length, 34);
  assert(catalog.tools.some(tool => tool.name === 'run_browser_task'));
  const beforeDiscovery = requests;
  for (const selector of [enabled, permission]) {
    await ui(`find('${selector}').click();`);
    await until(() => ui(`return !find('settings-dao-agent-page').agentPlugins_[0].effective;`), 'Jev disabled');
    const hidden = await mcp.call('tools/list', {}).result;
    assert(!hidden.tools.some(tool => tool.name === 'run_browser_task'), 'Disabled Jev is hidden from MCP discovery');
    assert.equal(hidden.tools.length, 33);
    await ui(`find('${selector}').click();`);
    await until(() => ui(`return find('settings-dao-agent-page').agentPlugins_[0].effective;`), 'Jev restored');
    assert((await mcp.call('tools/list', {}).result).tools.some(tool => tool.name === 'run_browser_task'));
  }
  await setField(token, '');
  assert(!(await mcp.call('tools/list', {}).result).tools.some(tool => tool.name === 'run_browser_task'), 'Incomplete configuration is hidden');
  await setField(token, 'local-test-token');
  await until(() => ui(`return find('settings-dao-agent-page').agentPlugins_[0].effective;`), 'valid Jev configuration');
  assert.equal(requests, beforeDiscovery, 'Discovery and settings changes do not call Jev');
  mode = 'normal';
  const firstCall = mcp.call('tools/call', {name: 'run_browser_task', arguments: {...task,
    reason: 'Test the local Jev form in an isolated browser profile'}});
  await approveIsolatedMcp();
  const external = await firstCall.result;
  console.log('MCP integration result:', JSON.stringify(external));
  assert.equal(external.isError, false);
  assert.equal(external.structuredContent.status, 'completed');
  assert.deepEqual(external.structuredContent.progress.actions.map(action => action.operation), ['TYPE_TEXT', 'CLICK']);
  assert(!(await targets()).some(target => target.url === 'dao://agent/'));
  console.log('PASS MCP discovery, native approval and task execution with Agent documents unloaded');
  const resetForm = async () => {
    await formSend('Page.reload');
    await until(() => evaluate(formSend, `!!document.querySelector('input') && !document.querySelector('input').value`), 'fresh MCP form');
  };
  const runMcp = (args = task) => mcp.call('tools/call', {name: 'run_browser_task', arguments: args});
  for (let i = 0; i < 3; i++) {
    await checkNavigation(async args => (await runMcp(args).result).structuredContent, i);
  }
  await prepareNavigation();
  mode = 'hold'; pendingResponse = null;
  const staleDecision = runMcp(navigationTask);
  await until(() => pendingResponse, 'Jev decision before navigation');
  await formSend('Page.navigate', {url: `${origin}/saved`});
  await until(() => evaluate(formSend, `document.body.textContent.includes('Saved successfully')`), 'navigation during decision');
  pendingResponse.end(JSON.stringify({answers: {operation: choice(pendingBody.questions.operation, 'BLOCKED')}}));
  const freshCompletion = await staleDecision.result;
  assert.equal(freshCompletion.structuredContent.status, 'completed', JSON.stringify(freshCompletion));
  assert.equal(freshCompletion.structuredContent.progress.actions.length, 0, 'Discard the obsolete decision');

  await prepareNavigation(); mode = 'normal'; navigationDelay = 2000;
  const navigationTimeout = await runMcp({...navigationTask, timeout_ms: 1000}).result;
  assert.equal(navigationTimeout.structuredContent.status, 'timeout', JSON.stringify(navigationTimeout));
  const requestsAtTimeout = requests;
  await until(() => evaluate(formSend, `location.pathname === '/saved'`), 'navigation after task timeout');
  await delay(150);
  assert.equal(requests, requestsAtTimeout, 'Navigation waiting stops at the task deadline');
  navigationDelay = 0;
  await formSend('Page.navigate', {url: `${origin}/form`});
  await until(() => evaluate(formSend, `location.pathname === '/form' && !!document.querySelector('input')`), 'MCP form restored');
  console.log('PASS MCP navigation across button types, stale decision rejection and navigation timeout without resubmitting');
  for (const [responseMode, expected] of [
    ['done', 'completion_unverified'], ['blocked', 'blocked'],
    ['bad-probability', 'invalid_response'], ['forged-action', 'invalid_response'],
    ['forged-target', 'invalid_response'], ['forged-input', 'invalid_response'],
    ['wait', 'no_progress'],
  ]) {
    await resetForm(); mode = responseMode;
    const result = await runMcp().result;
    assert.equal(result.isError, true, JSON.stringify(result));
    assert.equal(result.structuredContent.status, expected, JSON.stringify(result));
    assert.equal(await evaluate(formSend, `document.querySelector('input').value`), '');
  }
  await resetForm(); mode = 'normal';
  const partial = await runMcp({...task, max_steps: 1}).result;
  assert.equal(partial.isError, true);
  assert.equal(partial.structuredContent.status, 'step_limit');
  assert.deepEqual(partial.structuredContent.progress.verified_conditions, [0]);
  assert.equal(partial.structuredContent.progress.actions.length, 1);
  console.log('PASS MCP rejects unverified completion and forged choices; no-progress and step limits preserve partial progress');

  for (const invalid of [{...task, goal: ''}, {...task, completion: []},
    {...task, known_inputs: Array.from({length: 21}, () => ({field: 'Name', value: 'Alice'}))}]) {
    const before = requests;
    const result = await runMcp(invalid).result;
    assert.equal(result.structuredContent.status, 'invalid_arguments');
    assert.equal(requests, before);
  }
  // 140 fields x 20 inputs would overflow a Cartesian choice question (255).
  await resetForm();
  await evaluate(formSend, `document.body.insertAdjacentHTML('beforeend', Array.from({length: 140}, (_, i) => '<input aria-label="Field ' + i + '">').join(''))`);
  const manyInputs = [task.known_inputs[0], ...Array.from({length: 19}, (_, i) => ({field: 'Field ' + i, value: 'Value ' + i}))];
  const bounded = await runMcp({...task, known_inputs: manyInputs, max_steps: 1}).result;
  assert.equal(bounded.structuredContent.status, 'step_limit');
  console.log('PASS native argument bounds and split field/input choices remain within the Jev choice limit');

  for (const cancellation of ['timeout', 'client', 'permission']) {
    await resetForm(); mode = 'hold'; pendingResponse = null;
    const beforeDisconnect = disconnected;
    const running = runMcp({...task, timeout_ms: cancellation === 'timeout' ? 1000 : 60000});
    await until(() => pendingResponse, 'pending external Jev request');
    if (cancellation === 'client') mcp.notify('notifications/cancelled', {requestId: running.id, reason: 'Integration cancellation'});
    if (cancellation === 'permission') await ui(`find('${permission}').click();`);
    // MCP cancellation deliberately suppresses the cancelled request's reply.
    if (cancellation !== 'client') {
      const result = await running.result;
      const expected = cancellation === 'permission' ? 'configuration_changed' : 'timeout';
      assert.equal(result.structuredContent.status, expected, JSON.stringify(result));
      assert.equal(result.isError, true);
    }
    await until(() => disconnected > beforeDisconnect, 'external network request cancelled');
    pendingResponse.end(JSON.stringify(normalAnswer(pendingBody)));
    await delay(100);
    assert.equal(await evaluate(formSend, `document.querySelector('input').value`), '');
  }
  const beforeDenied = requests;
  const denied = await runMcp().result;
  assert.equal(denied.structuredContent.status, 'permission_denied');
  assert.equal(requests, beforeDenied);
  await ui(`find('${permission}').click();`);
  await until(() => ui(`return find('settings-dao-agent-page').agentPlugins_[0].effective;`), 'Jev permission restored');
  console.log('PASS external cancellation, timeout and permission revocation abort network requests with no late page mutation');

  await resetForm(); mode = 'hold'; pendingResponse = null;
  const pinned = runMcp();
  await until(() => pendingResponse, 'request before target switch');
  const {targetId: otherId} = await send('Target.createTarget', {url: `${origin}/form`});
  const otherTarget = await until(async () => (await targets()).find(t => t.id === otherId), 'second form target');
  const otherSend = await connect(otherTarget.webSocketDebuggerUrl);
  await until(() => evaluate(otherSend, `!!document.querySelector('input')`), 'second form loaded');
  await send('Target.activateTarget', {targetId: otherId});
  mode = 'normal'; pendingResponse.end(JSON.stringify(normalAnswer(pendingBody)));
  const pinnedResult = await pinned.result;
  assert.equal(pinnedResult.structuredContent.status, 'completed', JSON.stringify(pinnedResult));
  assert.equal(await evaluate(formSend, `document.querySelector('input').value`), 'Alice');
  assert.equal(await evaluate(otherSend, `document.querySelector('input').value`), '');
  const atomic = await mcp.call('tools/call', {name: 'get_accessibility_tree', arguments: {filter: 'compact'}}).result;
  assert.equal(atomic.isError, false);
  assert.equal(atomic.structuredContent.url, `${origin}/form`);
  console.log('PASS task keeps its authorized tab after active-tab switch; ordinary MCP tools still work');

  await resetForm(); mode = 'hold'; pendingResponse = null;
  const beforeStop = disconnected;
  const stopped = runMcp();
  await until(() => pendingResponse, 'request before Stop control');
  await evaluate(send, `chrome.send('stopDaoMcpControl', [])`);
  const stoppedResult = await stopped.result;
  assert.equal(stoppedResult.isError, true);
  await until(() => disconnected > beforeStop, 'Stop control cancels network');
  pendingResponse.end(JSON.stringify(normalAnswer(pendingBody)));
  assert.equal(await evaluate(formSend, `document.querySelector('input').value`), '');
  console.log('PASS MCP Stop control cancels the task and service request');
  helper.stdin.end();
  await stop(send);
} finally {
  if (helper && helper.exitCode === null) helper.kill();
  sockets.forEach(socket => socket.close());
  if (child && child.exitCode === null && child.signalCode === null) {
    const exit = new Promise(resolve => child.once('exit', resolve)); child.kill('SIGTERM'); await exit;
  }
  server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
  await rm(profile, {recursive: true, force: true});
}
