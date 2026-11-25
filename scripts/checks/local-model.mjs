// Run after npm run rebuild. Uses a disposable profile and a model endpoint
// that deliberately returns no CORS headers.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {mkdtemp, readFile, rm} from 'node:fs/promises';
import {createServer} from 'node:http';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const requests = [];
let mode = 'reply';
let closed;
const streamClosed = new Promise(ok => { closed = ok; });
const server = createServer(async (req, res) => {
  requests.push([req.method, req.url]);
  if (req.method !== 'POST') return res.writeHead(405).end();
  assert.equal(req.headers.cookie, undefined);
  if (req.url === '/sink') return res.writeHead(500).end();
  if (req.url === '/gateway/v1/chat/completions') {
    assert.equal(req.headers.authorization, 'Bearer local-test-key');
    res.writeHead(200, {'Content-Type': 'text/event-stream'});
    res.write('data: ' + JSON.stringify({id: 'test', choices: [{index: 0,
      delta: {role: 'assistant', content: '你好'}, finish_reason: null}]}) + '\n\n');
    res.once('close', closed);
    return;  // Only cancellation can end this response.
  }
  assert.equal(req.headers['x-api-key'], 'local-test-key');
  if (mode === 'redirect') return res.writeHead(307, {location: '/sink'}).end();
  if (mode === 'error') return res.writeHead(401, {'content-type': 'application/json'})
      .end(JSON.stringify({error: {type: 'authentication_error', message: 'fixture key rejected'}}));
  let body = '';
  for await (const chunk of req) body += chunk;
  assert.equal(JSON.parse(body).stream, true);
  res.writeHead(200, {'Content-Type': 'text/event-stream'});
  const event = (type, fields) => res.write(
      `event: ${type}\ndata: ${JSON.stringify({type, ...fields})}\n\n`);
  event('message_start', {message: {id: 'test', role: 'assistant',
    type: 'message', content: [], model: 'local', usage: {input_tokens: 3, output_tokens: 0}}});
  event('content_block_start', {index: 0, content_block: {type: 'text', text: ''}});
  event('content_block_delta', {index: 0, delta: {type: 'text_delta', text: 'OK'}});
  event('content_block_stop', {index: 0});
  event('message_delta', {delta: {stop_reason: 'end_turn'}, usage: {output_tokens: 1}});
  event('message_stop', {});
  res.end();
});
await new Promise(ok => server.listen(0, '127.0.0.1', ok));
const baseUrl = 'http://127.0.0.1:' + server.address().port;
const profile = await mkdtemp(join(tmpdir(), 'dao-local-model-'));
const binary = resolve(process.argv[2] ??
    'engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug');
const browser = spawn(binary, ['--headless', '--test-type', '--use-mock-keychain',
  '--no-first-run', '--disable-background-networking', '--remote-debugging-port=0',
  '--user-data-dir=' + profile, 'dao://settings/'], {stdio: ['ignore', 'ignore', 'pipe']});
let stderr = '';
browser.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-12000); });
const exited = new Promise(ok => browser.once('close', ok));
async function evaluate(target, expression) {
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  const deadline = AbortSignal.timeout(45000);
  try {
    await new Promise((ok, fail) => { socket.onopen = ok; socket.onerror = fail; });
    return await new Promise((ok, fail) => {
      deadline.onabort = () => fail(deadline.reason);
      socket.onclose = () => fail(new Error('Browser disconnected: ' + stderr));
      socket.onmessage = ({data}) => {
        const message = JSON.parse(data);
        if (message.id !== 1) return;
        if (message.error || message.result.exceptionDetails) fail(new Error(JSON.stringify(message)));
        else ok(message.result.result.value);
      };
      socket.send(JSON.stringify({id: 1, method: 'Runtime.evaluate', params: {
        expression: `(async () => { ${expression} })()`, awaitPromise: true, returnByValue: true}}));
    });
  } finally {
    deadline.onabort = null;
    socket.close();
  }
}
try {
  let port;
  for (let i = 0; i < 200 && !port; i++) {
    await delay(100);
    try { [port] = (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n'); }
    catch {}
  }
  assert(port, 'Browser exposed its DevTools port');
  const open = async url => (await fetch(`http://127.0.0.1:${port}/json/new?` +
      encodeURIComponent(url), {method: 'PUT'})).json();
  const agent = await open('dao://agent/');
  const settings = await open('dao://settings/');
  const config = {provider: 'anthropic-compatible', model: 'local',
    baseUrl, apiKey: 'local-test-key'};
  const testConnection = () => evaluate(settings, `
      const {sendWithPromise} = await import('dao://resources/js/cr.js');
      for (let i = 0; i < 100; i++) {
        const result = await sendWithPromise('testDaoAgentConnection', ${JSON.stringify(config)});
        if (result.errorCode !== 'unavailable') return result;
        await new Promise(ok => setTimeout(ok, 100));
      }
      throw new Error('Agent did not register its connection tester');
    `);
  const result = await testConnection();
  assert.equal(result.ok, true, JSON.stringify({result, requests}));
  assert.equal(requests.length, 1);
  assert.equal(requests[0][0], 'POST');
  assert.equal(new URL(requests[0][1], baseUrl).pathname, '/v1/messages');
  console.log('PASS: unsaved local Anthropic endpoint without CORS');

  mode = 'error';
  assert.match((await testConnection()).error, /fixture key rejected/);
  mode = 'redirect';
  assert.equal((await testConnection()).ok, false);
  assert(!requests.some(([, path]) => path === '/sink'), 'Redirect receives no credentials');
  console.log('PASS: provider error and redirect handling');

  // Save a different provider with a path prefix. The completed temporary
  // Anthropic test must no longer authorize that endpoint.
  const openai = {...config, provider: 'openai-compatible', baseUrl: baseUrl + '/gateway/v1'};
  await evaluate(settings, `
    const {sendWithPromise} = await import('dao://resources/js/cr.js');
    await sendWithPromise('setDaoAgentSetting', 'dao_agent_providers',
        JSON.stringify({'openai-compatible': ${JSON.stringify(openai)}}));
    await sendWithPromise('setDaoAgentSetting', 'dao_agent_active_provider', 'openai-compatible');
  `);
  const count = requests.length;
  const denied = await evaluate(agent, `
    const {callNative} = await import('./agent_bridge.js');
    return callNative('nativeFetch', {streamId: crypto.randomUUID(), method: 'POST',
      url: ${JSON.stringify(baseUrl + '/v1/messages')}, body: '{}'});
  `);
  assert.equal(denied.status, 0);
  assert.equal(count, requests.length, 'Expired test permission stops the POST');

  const text = await evaluate(agent, `
    const {Agent} = await import('./vendor/pi_runtime_bundle.js');
    const {resolvePiModel} = await import('./pi_model.js');
    const config = ${JSON.stringify(openai)};
    const chat = new Agent({initialState: {model: resolvePiModel(config.provider,
      config.model, config.baseUrl), thinkingLevel: 'off', tools: [], messages: []},
      getApiKey: () => config.apiKey});
    let text = '';
    chat.subscribe(event => {
      if (event.type === 'message_update' && event.assistantMessageEvent.type === 'text_delta') {
        text += event.assistantMessageEvent.delta;
        chat.abort();
      }
    });
    await chat.prompt('Hello');
    return text;
  `);
  assert.equal(text, '你好', 'Chat receives a token while the HTTP response is still open');
  await Promise.race([streamClosed, delay(5000, null, {ref: false}).then(() => {
    throw new Error('Cancelling chat did not disconnect the server');
  })]);
  assert.equal(requests.at(-1)[1], '/gateway/v1/chat/completions');
  console.log('PASS: saved OpenAI-compatible chat streams and cancels');
} finally {
  browser.kill('SIGTERM');
  const timer = setTimeout(() => browser.kill('SIGKILL'), 5000);
  await exited;
  clearTimeout(timer);
  server.closeAllConnections();
  await new Promise(ok => server.close(ok));
  await rm(profile, {recursive: true, force: true});
}
