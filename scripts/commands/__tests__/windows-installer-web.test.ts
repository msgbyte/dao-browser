import {existsSync, readFileSync} from 'node:fs';
import path from 'node:path';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

const webRoot = path.resolve('scripts/windows-installer/web');
const read = (file: string) => readFileSync(path.join(webRoot, file), 'utf8');
type Bridge = {
  postMessage: (message: string) => void;
  addEventListener: (event: string, handler: (event: {data: unknown}) => void) => void;
};
type Mount = (bridge: Bridge, locales: Record<string, Record<string, string>>) => void;

async function mount(language = 'en', overrides: Record<string, unknown> = {}) {
  expect(existsSync(path.join(webRoot, 'index.html')), 'Bundled installer HTML exists').toBe(true);
  const html = new DOMParser().parseFromString(read('index.html'), 'text/html');
  document.body.innerHTML = html.body.innerHTML;
  const locales = {en: JSON.parse(read('locales/en.json')), 'zh-CN': JSON.parse(read('locales/zh-CN.json'))};
  const source = path.join(webRoot, 'app.js');
  const {mountInstaller}: {mountInstaller: Mount} = await import(/* @vite-ignore */ source);
  const messages: string[] = [];
  let handler: (event: {data: unknown}) => void = () => {};
  mountInstaller({
    postMessage: message => { messages.push(message); },
    addEventListener: (_event, callback) => { handler = callback; },
  }, locales);
  expect(messages).toEqual(['ready']);
  const send = (data: unknown) => handler({data});
  send({type: 'init', language, directory: 'C:\\Users\\Alice\\Dao', locked: false,
    version: '1.2.3', logPath: 'C:\\Users\\Alice\\Temp\\dao-install.log', ...overrides});
  return {messages, send, locales};
}

const button = (id: string) => document.getElementById(id) as HTMLButtonElement;
const input = (id: string) => document.getElementById(id) as HTMLInputElement;

beforeEach(() => { vi.useFakeTimers(); document.body.replaceChildren(); });
afterEach(() => { vi.clearAllTimers(); vi.useRealTimers(); });

describe('Windows installer WebView UI', () => {
  it.each(['en', 'zh-CN'])('hands an existing installation to the uninstaller once in %s', async language => {
    const {messages, locales} = await mount(language, {locked: true});
    expect(button('uninstall'), 'Existing installations expose an uninstall action').not.toBeNull();
    expect(button('uninstall').hidden).toBe(false);
    expect(button('uninstall').textContent).toBe(locales[language].uninstall);
    button('uninstall').click();
    button('uninstall').click();
    button('primary').click();
    button('cancel').click();
    expect(messages).toEqual(['ready', 'uninstall']);
  });

  it('does not offer uninstall for a fresh installation', async () => {
    const {messages} = await mount();
    expect(button('uninstall'), 'The uninstall action exists but is unavailable').not.toBeNull();
    expect(button('uninstall').hidden).toBe(true);
    button('uninstall').click();
    expect(messages).toEqual(['ready']);
  });

  it('blocks uninstall during repair and after completion', async () => {
    const {messages, send} = await mount('en', {locked: true});
    expect(button('uninstall')).not.toBeNull();
    button('primary').click();
    expect(button('uninstall').hidden).toBe(true);
    button('uninstall').click();
    send({type: 'state', state: 'complete'});
    expect(button('uninstall').hidden).toBe(true);
    button('uninstall').click();
    expect(messages).toEqual(['ready', 'install:C:\\Users\\Alice\\Dao']);
  });

  it('installs once, locks close while working, and closes without launching on completion', async () => {
    const {messages, send, locales} = await mount();
    input('directory').value = 'D:\\My Apps\\Dao';
    button('primary').click();
    button('primary').click();
    button('browse').click();
    button('cancel').click();
    document.getElementById('installer')!.dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape'}));
    expect(messages).toEqual(['ready', 'install:D:\\My Apps\\Dao']);
    expect(input('directory').disabled).toBe(true);
    expect(document.getElementById('progress')!.hidden).toBe(false);
    expect(document.querySelector('[role="progressbar"]')!.hasAttribute('aria-valuenow')).toBe(false);
    expect(document.getElementById('headline')!.textContent).toBe(locales.en.installingTitle);
    send({type: 'state', state: 'complete'});
    expect(document.getElementById('progress')!.hidden).toBe(true);
    button('cancel').click();
    button('cancel').click();
    expect(messages).toEqual(['ready', 'install:D:\\My Apps\\Dao', 'finish']);
  });

  it('receives a native folder choice and submits Unicode paths without interpreting them as HTML', async () => {
    const {messages, send} = await mount('zh-CN');
    button('browse').click();
    const directory = 'D:\\应用 & Dao';
    send({type: 'directory', directory});
    expect(input('directory').value).toBe(directory);
    expect(document.querySelectorAll('img')).toHaveLength(1);
    button('primary').click();
    expect(messages).toEqual(['ready', 'browse:C:\\Users\\Alice\\Dao', `install:${directory}`]);
    send({type: 'directory', directory: 'D:\\late-folder'});
    expect(input('directory').value).toBe(directory);
  });

  it('locks an existing installation path and exposes localized repair guidance', async () => {
    const {messages, send, locales} = await mount('zh-CN', {locked: true});
    expect(document.documentElement.lang).toBe('zh-CN');
    expect(document.title).toBe(locales['zh-CN'].windowTitle);
    expect(input('directory').readOnly).toBe(true);
    expect(button('browse').disabled).toBe(true);
    expect(document.getElementById('directory-hint')!.textContent).toBe(locales['zh-CN'].lockedDirectory);
    expect(button('primary').textContent).toBe(locales['zh-CN'].repair);
    send({type: 'directory', directory: 'D:\\unexpected-folder'});
    input('directory').value = 'D:\\synthetic-edit';
    button('primary').click();
    expect(messages).toEqual(['ready', 'install:C:\\Users\\Alice\\Dao']);
  });

  it('shows backend errors, the exit code and log path, then permits retry', async () => {
    const {messages, send, locales} = await mount();
    button('primary').click();
    send({type: 'state', state: 'error', error: 'installFailed', code: 42});
    expect(document.getElementById('description')!.textContent).toBe(locales.en.installFailed);
    expect(document.getElementById('error-details')!.hidden).toBe(false);
    expect(document.getElementById('error-code')!.textContent).toContain('42');
    expect(document.getElementById('log-path')!.textContent).toBe('C:\\Users\\Alice\\Temp\\dao-install.log');
    expect(button('primary').disabled).toBe(false);
    expect(input('directory').disabled).toBe(false);
    input('directory').value = 'D:\\Dao';
    button('primary').click();
    expect(messages.at(-1)).toBe('install:D:\\Dao');
    expect(document.getElementById('error-details')!.hidden).toBe(true);
  });

  it('keeps launch failures recoverable without reinstalling', async () => {
    const {messages, send, locales} = await mount();
    button('primary').click();
    send({type: 'state', state: 'complete'});
    button('primary').click();
    expect(messages.at(-1)).toBe('launch');
    send({type: 'state', state: 'error', error: 'launchFailed', code: 2});
    expect(document.getElementById('description')!.textContent).toBe(locales.en.launchFailed);
    expect(button('cancel').disabled).toBe(false);
    button('primary').click();
    expect(messages.at(-1)).toBe('launch');
    send({type: 'state', state: 'error', error: 'launchFailed', code: 2});
    button('cancel').click();
    expect(messages.at(-1)).toBe('finish');
    expect(messages.filter(message => message.startsWith('install:'))).toHaveLength(1);
  });

  it('localizes labels and validates an empty path before asking the host to install', async () => {
    const {messages, locales} = await mount('zh-CN');
    expect(Object.keys(locales.en).sort()).toEqual(Object.keys(locales['zh-CN']).sort());
    for (const element of document.querySelectorAll<HTMLElement>('[data-i18n]')) {
      expect(element.textContent).toBe(locales['zh-CN'][element.dataset.i18n!]);
    }
    input('directory').value = '   ';
    button('primary').click();
    expect(messages).toEqual(['ready']);
    expect(document.getElementById('directory-hint')!.textContent).toBe(locales['zh-CN'].invalidDirectory);
    expect(input('directory').getAttribute('aria-invalid')).toBe('true');
    expect(document.activeElement).toBe(input('directory'));
  });

  it('falls back to English and handles keyboard cancellation only once', async () => {
    const {messages, locales} = await mount('unknown', {version: '<script>version</script>'});
    expect(document.documentElement.lang).toBe('en');
    expect(document.title).toBe(locales.en.windowTitle);
    expect(document.getElementById('version')!.textContent).toContain('<script>version</script>');
    document.getElementById('installer')!.dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape'}));
    button('cancel').click();
    expect(messages).toEqual(['ready', 'cancel']);
  });

  it('edits the path inline, cancels edits with Escape and commits with Enter', async () => {
    const {messages} = await mount();
    expect(document.getElementById('path-edit')!.hidden).toBe(true);
    button('change-path').click();
    input('directory').value = 'D:\\Discarded';
    input('directory').dispatchEvent(new KeyboardEvent('keydown', {key: 'Escape', bubbles: true}));
    expect(input('directory').value).toBe('C:\\Users\\Alice\\Dao');
    expect(messages).toEqual(['ready']);
    button('change-path').click();
    input('directory').value = 'D:\\My Apps\\Dao';
    input('directory').dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}));
    expect(document.getElementById('path-text')!.textContent).toBe('D:\\My Apps\\Dao');
    expect(document.getElementById('path-edit')!.hidden).toBe(true);
    expect(messages).toEqual(['ready']);
    button('browse').click();
    expect(messages.at(-1)).toBe('browse:D:\\My Apps\\Dao');
    button('primary').click();
    expect(messages.at(-1)).toBe('install:D:\\My Apps\\Dao');
  });

  it('rejects malformed paths inline without losing the editable value', async () => {
    const {messages, send} = await mount();
    send({type: 'state', state: 'error', error: 'installFailed', code: 42});
    button('change-path').click();
    input('directory').value = 'relative\\Dao';
    button('confirm-path').click();
    expect(document.getElementById('path-edit')!.hidden).toBe(false);
    expect(input('directory').getAttribute('aria-invalid')).toBe('true');
    button('primary').click();
    expect(messages).toEqual(['ready']);
    input('directory').value = 'D:\\Dao';
    button('confirm-path').click();
    expect(document.getElementById('installer')!.dataset.state).toBe('ready');
    expect(document.getElementById('error-details')!.hidden).toBe(true);
    expect(document.getElementById('description')!.hidden).toBe(true);
  });

  it('minimizes during installation and rotates localized tips without inventing progress', async () => {
    const {messages, send} = await mount('zh-CN');
    button('primary').click();
    button('minimize').click();
    expect(messages.at(-1)).toBe('minimize');
    const initial = document.getElementById('tip')!.textContent;
    vi.advanceTimersByTime(3200);
    expect(document.getElementById('tip')!.textContent).not.toBe(initial);
    expect(document.querySelector('[role="progressbar"]')!.hasAttribute('aria-valuenow')).toBe(false);
    send({type: 'state', state: 'complete'});
    const completed = document.getElementById('tip')!.textContent;
    vi.advanceTimersByTime(6400);
    expect(document.getElementById('tip')!.textContent).toBe(completed);
  });
});
