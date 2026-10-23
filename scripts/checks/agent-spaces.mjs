// Run after npm run rebuild: node scripts/checks/agent-spaces.mjs
// Drives a real Dao Debug build through the dao-browser CLI in an isolated,
// headless profile: background-space approval with "Always allow", a space
// created by `call`, a multi-step `run` script, take over (hard stop), and
// finish. --test-type keeps Dao from installing skills into the real home.
import {strict as assert} from 'node:assert';
import {spawn} from 'node:child_process';
import {mkdir, mkdtemp, readFile, realpath, rm, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {tmpdir} from 'node:os';
import {dirname, join, resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const binary = resolve(process.argv[2] ??
    'engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug');
const cli = resolve(dirname(binary), '../Helpers/dao-browser');
const profile = await realpath(await mkdtemp(join(tmpdir(), 'dao-spaces-check-')));
const sockets = [];
let child, stderr = '';

const server = createServer((req, res) => {
  res.setHeader('Content-Type', 'text/html; charset=utf-8');
  res.end(`<!doctype html><title>Space check</title>
    <button onclick="document.body.dataset.clicked = 'yes'">Press me</button>`);
});
await new Promise(resolveListen => server.listen(0, '127.0.0.1', resolveListen));
const origin = `http://127.0.0.1:${server.address().port}`;

async function until(fn, label, attempts = 300) {
  for (let i = 0; i < attempts; i++) {
    const result = await fn();
    if (result) return result;
    if (child?.exitCode != null || child?.signalCode != null) {
      throw new Error(`Browser exited: ${stderr.slice(-2000)}`);
    }
    await delay(100);
  }
  throw new Error(`Timed out: ${label}\n${stderr.slice(-2000)}`);
}

async function connect(url) {
  const socket = new WebSocket(url); sockets.push(socket);
  await new Promise((ok, fail) => { socket.onopen = ok; socket.onerror = fail; });
  let sequence = 0; const pending = new Map();
  socket.onmessage = event => {
    const msg = JSON.parse(event.data); const handler = pending.get(msg.id);
    if (!handler) return;
    pending.delete(msg.id);
    msg.error ? handler.reject(new Error(JSON.stringify(msg.error))) : handler.resolve(msg.result);
  };
  return (method, params = {}) => new Promise((ok, fail) => {
    const id = ++sequence; pending.set(id, {resolve: ok, reject: fail});
    socket.send(JSON.stringify({id, method, params}));
  });
}

async function evaluate(send, expression) {
  const result = await send('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true});
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
}

// CPU and memory of every Dao process, sampled over `ms`.
async function sampleUsage(ms) {
  const read = async () => {
    const ps = spawn('ps', ['-A', '-o', 'pid=,ppid=,time=,rss=']);
    let out = '';
    ps.stdout.on('data', chunk => { out += chunk; });
    await new Promise(ok => ps.on('exit', ok));
    const rows = out.trim().split('\n').map(line => line.trim().split(/\s+/));
    const children = new Map();
    for (const [pid, ppid] of rows) {
      children.set(ppid, [...(children.get(ppid) ?? []), pid]);
    }
    const tree = new Set([String(child.pid)]);
    for (const pid of tree) (children.get(pid) ?? []).forEach(c => tree.add(c));
    const seconds = time => time.split(':').reduce((sum, part) => sum * 60 + Number(part), 0);
    let cpu = 0, rss = 0;
    for (const [pid, , time, kb] of rows) {
      if (tree.has(pid)) { cpu += seconds(time); rss += Number(kb); }
    }
    return {cpu, rss};
  };
  const start = await read();
  await delay(ms);
  const end = await read();
  return {cpuPercent: (end.cpu - start.cpu) / (ms / 1000) * 100, rssMb: end.rss / 1024};
}

function runCli(args, input = '') {
  const proc = spawn(cli, [...args, `--user-data-dir=${profile}`, '--no-launch',
    '--client=Dao spaces check'], {stdio: ['pipe', 'pipe', 'pipe']});
  let out = '', err = '';
  proc.stdout.on('data', chunk => { out += chunk; });
  proc.stderr.on('data', chunk => { err += chunk; });
  proc.stdin.end(input);
  return new Promise(ok => proc.on('exit', code => ok({code, out, err})));
}

// Clicks "Always allow" and then Allow in the native approval dialog.
async function approveAlways(finished = () => null) {
  const uiPort = (await readFile(join(profile, 'UIDevToolsActivePort'), 'utf8')).trim();
  const flatten = node => [node, ...(node.children ?? []).flatMap(flatten)];
  // UI DevTools snapshots widgets per connection, so reconnect until the
  // dialog exists, then keep using that connection.
  let native;
  const nodes = async () => flatten((await native('DOM.getDocument')).root);
  const click = nodeId => Promise.all(['mousePressed', 'mouseReleased'].map(type =>
    native('DOM.dispatchMouseEvent', {nodeId,
      event: {type, x: 8, y: 8, button: 'left', wheelDirection: 'none'}})));
  const checkbox = await until(async () => {
    if (finished()) {
      throw new Error(`The request ended before approval: ${JSON.stringify(finished())}`);
    }
    // The UI DevTools server takes one client at a time.
    if (native) sockets.pop()?.close();
    await delay(400);
    try {
      native = await connect(`ws://127.0.0.1:${uiPort}/0`);
    } catch {
      native = null;
      return false;
    }
    return (await nodes()).find(node => (node.attributes ?? []).includes('Checkbox'));
  }, 'Always allow checkbox', 60);
  // The dialog's input protector ignores clicks right after it opens.
  await delay(1000);
  await click(checkbox.nodeId);
  const allow = await until(async () => {
    for (const node of (await nodes()).filter(node =>
      (node.attributes ?? []).includes('DaoSystemDialogButton'))) {
      const styles = await native('CSS.getMatchedStylesForNode', {nodeId: node.nodeId});
      const properties = (styles.matchedCSSRules ?? []).flatMap(match => match.rule.style.cssProperties);
      if (properties.some(p => p.name === 'Style' && p.value === 'kProminent')) return node;
    }
    return false;
  }, 'Allow button');
  await click(allow.nodeId);
}

try {
  await mkdir(profile, {recursive: true});
  await writeFile(join(profile, 'Local State'),
                  JSON.stringify({dao: {mcp_server_enabled: true}}));
  child = spawn(binary, ['--headless', `--user-data-dir=${profile}`, '--remote-debugging-port=0',
    '--no-first-run', '--no-default-browser-check', '--use-mock-keychain', '--test-type',
    '--disable-background-networking', '--enable-ui-devtools=0',
    '--enable-features=ui-debug-tools-enable-synthetic-events'], {stdio: ['ignore', 'ignore', 'pipe']});
  child.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-10000); });
  const port = await until(async () => {
    try { return (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; } catch { return null; }
  }, 'DevTools port');
  await until(async () => {
    try { await readFile(join(profile, 'MCP', 'runtime.json')); return true; } catch { return false; }
  }, 'MCP runtime');

  const status = await runCli(['status']);
  assert.equal(status.code, 0, status.err);
  const skill = await runCli(['skill']);
  assert.match(skill.out, /<!-- dao-browser guide [\d.]+ -->/);
  assert.match(skill.out, /<!-- end of dao-browser guide [\d.]+ -->/);
  console.log('PASS status and versioned skill guide');

  // Let startup work settle before measuring the idle baseline.
  await delay(15000);
  const idle = await sampleUsage(10000);
  const pagesBefore = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json())
      .filter(target => target.type === 'page' && target.url.startsWith('http')).length;
  const created = runCli(['call', 'create_space',
    JSON.stringify({name: 'Spaces check', url: `${origin}/`})]);
  let createdEarly = null;
  created.then(result => { createdEarly = result; });
  await approveAlways(() => createdEarly);
  const createdResult = await created;
  assert.equal(createdResult.code, 0, createdResult.err);
  const spaceId = JSON.parse(createdResult.out).data.space_id;
  assert.ok(spaceId);
  const pagesAfter = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json())
      .filter(target => target.type === 'page' && target.url.startsWith(origin)).length;
  assert.equal(pagesAfter, pagesBefore + 1);
  console.log('PASS background-space approval and create_space');
  await delay(5000);
  const withSpace = await sampleUsage(10000);
  // RSS is summed per process, so shared pages count more than once.
  console.log(`INFO baseline without spaces: ${idle.cpuPercent.toFixed(1)}% CPU, ` +
              `${idle.rssMb.toFixed(0)} MB summed RSS; with one idle space: ` +
              `${withSpace.cpuPercent.toFixed(1)}% CPU, ` +
              `${withSpace.rssMb.toFixed(0)} MB summed RSS`);

  // A new connection must not prompt again for the remembered client.
  const script = await runCli(['run', '--space', spaceId], `
    await space.status('Pressing the button');
    const result = await space.waitFor({role: 'button', name: 'Press me'});
    await space.click(result.matches[0], {label: 'Press the button'});
    console.log('clicked');
    return await space.eval('document.body.dataset.clicked');`);
  assert.equal(script.code, 0, script.err);
  assert.match(script.out, /clicked/);
  assert.match(script.out, /"yes"/);
  console.log('PASS remembered client runs a multi-step script without another prompt');

  const savedPath = join(profile, 'saved', 'result.json');
  const saved = await runCli(['run', '--space', spaceId], `
    dao.saveFile(${JSON.stringify(savedPath)}, {title: (await space.info()).title});
    return 'saved';`);
  assert.equal(saved.code, 0, saved.err);
  assert.match(saved.out, /\[dao-browser\] saved /);
  assert.equal(JSON.parse(await readFile(savedPath, 'utf8')).title, 'Space check');
  const skills = await runCli(['call', 'get_site_skills', JSON.stringify({url: origin})]);
  assert.equal(skills.code, 0, skills.err);
  assert.ok(Array.isArray(JSON.parse(skills.out).data.skills));
  console.log('PASS the CLI saves script files locally and lists site skills');

  const sidebar = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json())
      .find(target => target.url === 'dao://sidebar/');
  assert.ok(sidebar, 'sidebar WebUI target');
  const sidebarSend = await connect(sidebar.webSocketDebuggerUrl);
  await evaluate(sidebarSend, `chrome.send('agentSpaceAction', ['take_over', ${JSON.stringify(spaceId)}])`);
  const stopped = await runCli(['run', '--space', spaceId],
                               `console.log('partial output'); return await space.info();`);
  assert.equal(stopped.code, 2, stopped.out + stopped.err);
  assert.match(stopped.out, /\[dao-browser:stop\]/);
  assert.doesNotMatch(stopped.out, /partial output/);
  console.log('PASS take over hard-stops scripts and drops their output');

  await evaluate(sidebarSend, `chrome.send('agentSpaceAction', ['hand_back', ${JSON.stringify(spaceId)}])`);
  const finished = await runCli(['call', 'finish_space',
    JSON.stringify({space_id: spaceId, keep: [], summary: 'Done'})]);
  assert.equal(finished.code, 0, finished.err);
  assert.equal(JSON.parse(finished.out).data.closed, true);
  console.log('PASS hand back and finish_space');
} finally {
  sockets.forEach(socket => socket.close());
  if (child && child.exitCode === null && child.signalCode === null) {
    const exit = new Promise(ok => child.once('exit', ok)); child.kill('SIGTERM'); await exit;
  }
  server.closeAllConnections(); await new Promise(ok => server.close(ok));
  await rm(profile, {recursive: true, force: true});
}
