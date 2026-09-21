// Run after npm run rebuild: node scripts/checks/folder-persistence.mjs [binary]
// Exercises the native folder writer and actual sidebar using a temporary profile.
import {strict as assert} from 'node:assert';
import {spawn} from 'node:child_process';
import {mkdir, mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';

const binary = resolve(process.argv[2] ??
    'engine/src/out/dao-debug/Dao Debug.app/Contents/MacOS/Dao Debug');
const profile = await mkdtemp(join(tmpdir(), 'dao-folder-check-'));
const folderPath = join(profile, 'Default', 'dao_folders.json');
await mkdir(join(profile, 'Default'));
await writeFile(join(profile, 'Default', 'Preferences'), JSON.stringify({
  dao: {welcome_shown: true}, session: {restore_on_startup: 1},
}));
const sockets = [];
let child;
let stderr = '';

async function until(fn, label) {
  for (let i = 0; i < 300; i++) {
    const value = await fn();
    if (value) return value;
    if (child?.exitCode != null) throw new Error(`Browser exited: ${stderr}`);
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

async function evaluate(send, body) {
  const result = await send('Runtime.evaluate', {
    expression: `(async () => {
      const app = document.querySelector('dao-sidebar-app');
      ${body}
    })()`, awaitPromise: true, returnByValue: true,
  });
  if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
  return result.result.value;
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
  const targets = async () => (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
  const send = await connect(version.webSocketDebuggerUrl);
  return {send, targets};
}

async function sidebars(browser, count) {
  const targets = await until(async () => {
    const found = (await browser.targets()).filter(t => t.url === 'dao://sidebar/');
    return found.length === count && found;
  }, `${count} sidebars`);
  const connections = await Promise.all(targets.map(t => connect(t.webSocketDebuggerUrl)));
  for (const send of connections) {
    await until(() => evaluate(send, 'return app?.foldersLoaded_ && app.unpinnedTabs_.length;'),
        'sidebar folder initialization');
  }
  return connections;
}

async function stop(browser) {
  const exit = new Promise(resolve => child.once('exit', resolve));
  await browser.send('Browser.close');
  await exit;
  sockets.splice(0).forEach(socket => socket.close());
}

const stored = async () => JSON.parse(await readFile(folderPath, 'utf8'));
const folders = data => {
  assert(Array.isArray(data.items), `Invalid folder data: ${JSON.stringify(data)}`);
  return data.items.filter(item => item.type === 'folder');
};
const grouping = data => folders(data).map(({id, name, children}) =>
  ({id, name, tabs: children.map(tab => tab.tabId)}));
async function save(send, base, next) {
  // The queued read is a barrier for the shared native writer.
  return evaluate(send, `
    chrome.send('saveFolders', ${JSON.stringify([JSON.stringify(next), JSON.stringify(base)])});
    return new Promise(resolve => {
      const original = cr.webUIListenerCallback;
      const callback = 'folder-check-' + crypto.randomUUID();
      cr.webUIListenerCallback = (event, ...args) => {
        if (event === callback) {
          cr.webUIListenerCallback = original;
          resolve(args[0]);
        } else original(event, ...args);
      };
      chrome.send('loadFolders', [callback]);
    });
  `);
}

async function expectProjection(connections, expectedIds) {
  for (const send of connections) {
    try {
      await until(() => evaluate(send, `
        return app.folderModel_.getFolders().length === 2 && app.unpinnedTabs_.length === 1 &&
            app.unpinnedTabs_.some(t => ${JSON.stringify(expectedIds)}.includes(t.tabId));
      `), 'restored folder identities');
    } catch (error) {
      console.error('Expected tab identities:', expectedIds);
      console.error('Sidebar state:', await evaluate(send, `return {
        tabs: app.unpinnedTabs_.map(({tabId, url}) => ({tabId, url})),
        folders: app.folderModel_.getFolders(),
      };`));
      console.error('Stored folders:', await stored());
      throw error;
    }
    const visible = await evaluate(send, `
      await app.updateComplete;
      const list = app.shadowRoot.querySelector('dao-tab-list');
      await list.updateComplete;
      return [...list.shadowRoot.querySelectorAll('dao-folder-item')].map(el => el.folder.name);
    `);
    assert.equal(visible.length, 1, 'Each window renders only its own nonempty folder: ' +
        JSON.stringify(await evaluate(send, `return {
          tabs: app.unpinnedTabs_.map(({tabId, url}) => ({tabId, url})),
          folders: app.folderModel_.getFolders(), visible: ${JSON.stringify(visible)},
        };`)));
  }
}

async function checkConcurrentMembership(connections) {
  const original = await stored();
  const ref = tabId => ({type: 'tab', tabId, url: 'about:blank', title: tabId});
  const folder = (id, children) => ({type: 'folder', id, name: id,
    collapsed: false, children: children.map(ref)});
  const base = {version: 1, items: [folder('F', ['a', 'b']), folder('G', [])]};
  const unfolder = {version: 1, items: [ref('a'), ref('b'), folder('G', [])]};
  const move = {version: 1, items: [folder('F', ['a']), folder('G', ['b'])]};
  const close = {version: 1, items: [folder('F', ['a']), folder('G', [])]};
  const insert = {version: 1, items: [folder('F', ['a', 'b', 'c']), folder('G', [])]};
  const staleCollapse = structuredClone(base);
  folders(staleCollapse)[0].collapsed = true;
  for (const [first, second, expected] of [
    [unfolder, move, {a: '', b: 'G'}],
    [move, unfolder, {a: '', b: ''}],
    [close, move, {a: 'F'}],
    [insert, unfolder, {a: '', b: '', c: ''}],
    [unfolder, insert, {a: '', b: '', c: ''}],
    [move, staleCollapse, {a: 'F', b: 'G'}],
  ]) {
    await save(connections[0], await stored(), base);
    await save(connections[0], base, first);
    await save(connections[1], base, second);
    const membership = {};
    const visit = (items, parent = '') => {
      for (const item of items) {
        if (item.type === 'folder') {
          visit(item.children, item.id);
        } else {
          assert(!(item.tabId in membership), `Duplicate membership: ${item.tabId}`);
          membership[item.tabId] = parent;
        }
      }
    };
    visit((await stored()).items);
    assert.deepEqual(membership, expected, 'Concurrent edits preserve unique tab membership');
  }
  await save(connections[0], await stored(), original);
}

try {
  let browser = await start();
  await sidebars(browser, 1);
  const page = (await browser.targets()).find(t => t.type === 'page' &&
      !['dao://sidebar/', 'dao://agent/'].includes(t.url));
  assert(page, 'Initial browser page exists');
  await (await connect(page.webSocketDebuggerUrl))('Page.navigate', {url: 'about:blank'});
  await browser.send('Target.createTarget', {url: 'about:blank', newWindow: true});
  let connections = await sidebars(browser, 2);
  const plans = [];
  for (const [i, send] of connections.entries()) {
    await until(() => evaluate(send, "return app.unpinnedTabs_[0]?.url === 'about:blank';"),
        'same URL in both windows');
    plans.push(await evaluate(send, `
      const folder = app.folderModel_.addFolder('Window ${i + 1}');
      const tab = app.unpinnedTabs_[0];
      app.folderModel_.moveTabToFolder(tab, folder.id);
      return {base: app.folderBaseJson_ ? JSON.parse(app.folderBaseJson_) : {version: 1, items: []},
        next: JSON.parse(app.folderModel_.toJson()), tabId: tab.tabId, folderId: folder.id};
    `));
  }
  assert.notEqual(plans[0].tabId, plans[1].tabId, 'Windows have distinct stable tab identities');
  for (const [i, plan] of plans.entries()) await save(connections[i], plan.base, plan.next);
  assert.equal(folders(await stored()).length, 2, 'Stale creation snapshots merge');
  await expectProjection(connections, plans.map(p => p.tabId));

  const base = await stored();
  const rename = structuredClone(base);
  const collapse = structuredClone(base);
  folders(rename)[0].name = 'Renamed';
  folders(collapse)[0].collapsed = true;
  await save(connections[0], base, rename);
  await save(connections[1], base, collapse);
  assert.equal(folders(await stored())[0].name, 'Renamed', 'Concurrent rename survives collapse');
  assert.equal(folders(await stored())[0].collapsed, true);
  await stop(browser);

  // Upgrade from the previous window snapshot format, including a duplicate
  // copy of a folder. Loading must preserve every stable identity exactly once.
  const beforeMigration = await stored();
  const legacySnapshots = JSON.stringify({version: 2,
    windows: [...beforeMigration.items, beforeMigration.items[0]].map((item, i) => ({
      id: `legacy-window-${i}`, tabIds: [],
      data: JSON.stringify({version: 1, items: [item]}),
    })),
  });
  await writeFile(folderPath, legacySnapshots);
  browser = await start();
  connections = await sidebars(browser, 2);
  assert.equal(await readFile(folderPath, 'utf8'), legacySnapshots,
      'Loading legacy window snapshots never rewrites the file');
  const migrated = JSON.parse(await save(connections[0], {version: 1, items: []},
      {version: 1, items: []}));
  assert.deepEqual(grouping(migrated), grouping(beforeMigration),
      'Legacy window snapshots migrate without losing or duplicating tabs');
  await expectProjection(connections, plans.map(p => p.tabId));
  assert.equal(folders(await stored()).length, 2, 'Restart never prunes the other window');
  const {browserContextId} = await browser.send('Target.createBrowserContext');
  await browser.send('Target.createTarget', {
    url: 'about:blank', newWindow: true, browserContextId,
  });
  const privateTarget = await until(async () => {
    for (const target of await browser.targets()) {
      if (target.url !== 'dao://sidebar/') continue;
      const {targetInfo} = await browser.send('Target.getTargetInfo', {targetId: target.id});
      if (targetInfo.browserContextId === browserContextId) return target;
    }
  }, 'incognito sidebar');
  const privateSend = await connect(privateTarget.webSocketDebuggerUrl);
  await until(() => evaluate(privateSend, 'return app?.foldersLoaded_;'), 'private folders loaded');
  assert.equal(await evaluate(privateSend, 'return app.folderModel_.getFolders().length;'),
      0, 'Incognito never reads regular folders');
  const privateFolders = {version: 1, items: [{type: 'folder', id: 'private',
    name: 'Private', collapsed: false, children: []}]};
  const regularDisk = await readFile(folderPath, 'utf8');
  await save(privateSend, {version: 1, items: []}, privateFolders);
  await privateSend('Page.reload');
  await until(() => evaluate(privateSend, `return app?.foldersLoaded_ &&
    app.folderModel_.getFolders().some(f => f.id === 'private');`), 'incognito sidebar reload');
  assert.equal(await readFile(folderPath, 'utf8'), regularDisk,
      'Incognito saves and reloads never change regular profile data');
  await browser.send('Target.disposeBrowserContext', {browserContextId});
  // Activating the moved tab may expand its folder and refresh its URL/title.
  const beforeTransfer = grouping(await stored());
  const moving = await evaluate(connections[0], `return {
    sessionId: app.sessionId_, index: app.unpinnedTabs_[0].index,
    tabId: app.unpinnedTabs_[0].tabId,
  };`);
  await evaluate(connections[1], `
    chrome.send('moveTabCrossWindow', [${moving.sessionId}, ${moving.index}, 0, ${JSON.stringify(moving.tabId)}]);
  `);
  connections = await sidebars(browser, 1);
  await until(() => evaluate(connections[0], `
    return app.unpinnedTabs_.length === 2 &&
        app.unpinnedTabs_.some(t => t.tabId === ${JSON.stringify(moving.tabId)});
  `), 'cross-window move keeps the stable identity');
  assert.deepEqual(grouping(await stored()), beforeTransfer, 'Window teardown preserves folder references');
  await evaluate(connections[0], `
    const tab = app.unpinnedTabs_.find(t => t.tabId === ${JSON.stringify(moving.tabId)});
    chrome.send('detachTabToNewWindow', [tab.index, 100, 100]);
  `);
  connections = await sidebars(browser, 2);
  await expectProjection(connections, plans.map(p => p.tabId));
  assert.deepEqual(grouping(await stored()), beforeTransfer, 'Detaching preserves folder membership');

  await browser.send('Target.createTarget', {url: 'about:blank#pin-identity'});
  const pinOwner = await until(async () => {
    for (const send of connections) {
      const tab = await evaluate(send, `
        return app.unpinnedTabs_.find(t => t.url === 'about:blank#pin-identity');
      `);
      if (tab) return {send, tab};
    }
  }, 'extra tab for pinned identity regression');
  await evaluate(pinOwner.send, `chrome.send('pinTab', [${pinOwner.tab.index}]);`);
  const pin = await until(() => evaluate(pinOwner.send, `
    return app.pinnedItems_.find(p => p.url === 'about:blank#pin-identity');
  `), 'pinned identity');
  await evaluate(pinOwner.send, `chrome.send('closePinnedItemTab', [${JSON.stringify(pin.id)}]);`);
  await until(() => evaluate(pinOwner.send, `
    return app.pinnedItems_.some(p => p.id === ${JSON.stringify(pin.id)} && p.state === 'dormant');
  `), 'dormant pin');
  await evaluate(pinOwner.send, `chrome.send('activateOrOpenPinnedItem', [${JSON.stringify(pin.id)}]);`);
  await until(() => evaluate(pinOwner.send, `
    return app.pinnedTabs_.some(t => t.tabId === ${JSON.stringify(pinOwner.tab.tabId)});
  `), 'reopened pin keeps its original identity');
  const reopenedPage = await until(async () => (await browser.targets()).find(
      target => target.type === 'page' && target.url === 'about:blank#pin-identity'),
  'reopened pin navigation');
  const reopened = await connect(reopenedPage.webSocketDebuggerUrl);
  await until(async () => {
    const history = await reopened('Page.getNavigationHistory');
    return history.entries[history.currentIndex]?.url === 'about:blank#pin-identity';
  }, 'reopened pin has a committed navigation to restore');
  await stop(browser);
  browser = await start();
  connections = await sidebars(browser, 2);
  await expectProjection(connections, plans.map(p => p.tabId));
  const restoredPins = await until(async () => {
    const ids = (await Promise.all(connections.map(send => evaluate(send,
        'return app.pinnedTabs_.map(t => t.tabId);')))).flat();
    return ids.length && ids;
  }, 'reopened pin session restore');
  assert.deepEqual(restoredPins, [pinOwner.tab.tabId], 'Reopened pin persists its final identity');
  await checkConcurrentMembership(connections);

  // Keep the pinned page open so closing the grouped tab does not close its window.
  for (const send of connections) {
    const closing = await evaluate(send, `
      return app.pinnedTabs_.length ? app.unpinnedTabs_[0] : null;
    `);
    if (!closing) continue;
    await evaluate(send, `chrome.send('closeTab', [${closing.index}]);`);
    await until(async () => !folders(await stored()).some(folder =>
      folder.children.some(tab => tab.tabId === closing.tabId)), 'closed tab leaves its folder');
    const remaining = plans.find(plan => plan.tabId !== closing.tabId);
    assert(folders(await stored()).some(folder =>
      folder.children.some(tab => tab.tabId === remaining.tabId)),
    'Closing one tab preserves the other window membership');
    break;
  }
  const beforeDelete = await stored();
  const deleted = structuredClone(beforeDelete);
  deleted.items = deleted.items.filter(item => item.id !== plans[0].folderId);
  await save(connections[0], beforeDelete, deleted);
  await save(connections[1], beforeDelete, beforeDelete);
  assert.equal(folders(await stored()).length, 1, 'Stale snapshot cannot resurrect deleted folders');

  const good = await stored();
  await writeFile(folderPath, '{corrupt');
  await save(connections[0], good, good);
  assert.equal(await readFile(folderPath, 'utf8'), '{corrupt', 'Invalid files are never overwritten');
  await stop(browser);
  console.log('PASS: legacy migration without load-time writes; incognito isolation and sidebar reload; multiwindow stale saves; concurrent folder fields and membership; per-window rendering; stable identities after restart; cross-window move and detach; reopened pin identity; tab closure; deletion; corrupt-file protection');
} finally {
  sockets.forEach(socket => socket.close());
  if (child && child.exitCode === null) {
    const exit = new Promise(resolve => child.once('exit', resolve));
    child.kill('SIGTERM');
    await exit;
  }
  await rm(profile, {recursive: true, force: true});
}
