// Run after rebuilding dao_installer_ui. Only the isolated fixture is installed.
import assert from 'node:assert/strict';
import {spawn, spawnSync, type ChildProcess} from 'node:child_process';
import {copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import {setTimeout as delay} from 'node:timers/promises';
import {getWindowsInstallerHost, renderWindowsInstallerHtml} from '../commands/build-windows-installer.js';

const root = mkdtempSync(path.join(os.tmpdir(), 'dao-webview-test-'));
const captureDir = path.resolve('.dao/installer-preview');
const capture = process.argv.includes('--capture');
const processes: ChildProcess[] = [];
const host = getWindowsInstallerHost();
const html = renderWindowsInstallerHtml();
if (capture) mkdirSync(captureDir, {recursive: true});

async function port() {
  const server = net.createServer();
  await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
  const value = (server.address() as net.AddressInfo).port;
  await new Promise<void>(resolve => server.close(() => resolve()));
  return value;
}

async function waitFor<T>(probe: () => Promise<T | undefined>, label: string): Promise<T> {
  for (let attempt = 0; attempt < 150; attempt++) {
    const value = await probe();
    if (value !== undefined) return value;
    await delay(100);
  }
  throw new Error(`Timed out waiting for ${label}`);
}

async function connect(debugPort: number, process: ChildProcess) {
  const url = await waitFor(async () => {
    assert.equal(process.exitCode, null, `Host exited before readiness: ${process.exitCode}`);
    try {
      const pages = await fetch(`http://127.0.0.1:${debugPort}/json/list`).then(r => r.json()) as
        Array<{type: string; webSocketDebuggerUrl?: string}>;
      return pages.find(page => page.type === 'page')?.webSocketDebuggerUrl;
    } catch { return undefined; }
  }, 'WebView2 debugging endpoint');
  const socket = new WebSocket(url);
  await new Promise<void>((resolve, reject) => {
    socket.addEventListener('open', () => resolve(), {once: true});
    socket.addEventListener('error', reject, {once: true});
  });
  let serial = 0;
  const pending = new Map<number, {resolve: (value: any) => void; reject: (error: Error) => void}>();
  socket.addEventListener('message', event => {
    const data = JSON.parse(String(event.data));
    const callback = pending.get(data.id);
    if (!callback) return;
    pending.delete(data.id);
    if (data.error) callback.reject(new Error(JSON.stringify(data.error)));
    else callback.resolve(data.result);
  });
  socket.addEventListener('close', () => {
    for (const callback of pending.values()) callback.reject(new Error('WebView2 closed'));
    pending.clear();
  });
  function call(method: string, params: object = {}): Promise<any> {
    const id = ++serial;
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => { pending.delete(id); reject(new Error(`Timed out: ${method}`)); }, 10000);
      pending.set(id, {resolve: value => { clearTimeout(timeout); resolve(value); },
        reject: error => { clearTimeout(timeout); reject(error); }});
      socket.send(JSON.stringify({id, method, params}));
    });
  }
  async function evaluate(expression: string) {
    const result = await call('Runtime.evaluate', {expression, returnByValue: true, awaitPromise: true});
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
  return {evaluate, call, close: () => socket.close()};
}

function session(name: string, language: number, locked = false) {
  const directory = path.join(root, name);
  mkdirSync(directory);
  const executable = path.join(directory, 'dao-installer-ui.exe');
  copyFileSync(host, executable);
  writeFileSync(path.join(directory, 'index.html'), html);
  const destination = path.join(root, `Dao ${name} 雪`);
  const ini = `[Installer]\r\nExecutable=${path.join(root, 'backend.exe')}\r\nDirectory=${destination}\r\nLanguage=${language}\r\nVersion=1.0.108\r\nLocked=${locked ? 1 : 0}\r\n`;
  writeFileSync(path.join(directory, 'session.ini'), Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(ini, 'utf16le')]));
  return {executable, destination};
}

function start(executable: string, env: NodeJS.ProcessEnv) {
  const child = spawn(executable, [], {env, windowsHide: !capture, stdio: 'ignore'});
  processes.push(child);
  return child;
}

try {
  const source = `using System;
using System.IO;
public class Fixture {
  public static int Main() {
    string command = Environment.CommandLine;
    int marker = command.IndexOf("/D=", StringComparison.Ordinal);
    if (marker < 0) return 91;
    string folder = AppDomain.CurrentDomain.BaseDirectory;
    string target = command.Substring(marker + 3);
    File.AppendAllText(Path.Combine(folder, "calls.txt"), target + "\\n");
    System.Threading.Thread.Sleep(${capture ? 3000 : 800});
    if (File.Exists(Path.Combine(folder, "fail"))) return 42;
    Directory.CreateDirectory(Path.Combine(target, "Application"));
    return 0;
  }
}`;
  writeFileSync(path.join(root, 'fixture.cs'), source);
  const script = path.join(root, 'fixture.ps1');
  writeFileSync(script, `$ErrorActionPreference = 'Stop'\nAdd-Type -Path (Join-Path $PSScriptRoot 'fixture.cs') -OutputAssembly (Join-Path $PSScriptRoot 'backend.exe') -OutputType ConsoleApplication\n`);
  const compile = spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', script],
    {encoding: 'utf8', windowsHide: true});
  assert.equal(compile.status, 0, compile.stderr);

  const windowProbe = path.join(root, 'window.ps1');
  writeFileSync(windowProbe, `param([int]$InstallerProcessId, [switch]$Minimized)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class InstallerWindow {
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int show);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wp, IntPtr lp);
}
'@
$window = (Get-Process -Id $InstallerProcessId).MainWindowHandle
if ($window -eq [IntPtr]::Zero) { throw 'Installer window missing.' }
if ($Minimized) {
  if (-not [InstallerWindow]::IsIconic($window)) { throw 'Minimize did not reach the native window.' }
  [InstallerWindow]::ShowWindow($window, 9) | Out-Null
} else {
  $bounds = New-Object InstallerWindow+Rect
  $client = New-Object InstallerWindow+Rect
  [InstallerWindow]::GetWindowRect($window, [ref]$bounds) | Out-Null
  [InstallerWindow]::GetClientRect($window, [ref]$client) | Out-Null
  $scale = [InstallerWindow]::GetDpiForWindow($window) / 96
  if ($client.Right -ne [math]::Round(560 * $scale) -or $client.Bottom -ne [math]::Round(420 * $scale)) { throw 'Wrong client size.' }
  if (($bounds.Right - $bounds.Left) -ne $client.Right -or ($bounds.Bottom - $bounds.Top) -ne $client.Bottom) { throw 'Native title bar still consumes space.' }
  [InstallerWindow]::SendMessage($window, 0x112, [IntPtr]0xf030, [IntPtr]::Zero) | Out-Null
  $after = New-Object InstallerWindow+Rect
  [InstallerWindow]::GetWindowRect($window, [ref]$after) | Out-Null
  if ($after.Right -ne $bounds.Right -or $after.Bottom -ne $bounds.Bottom) { throw 'Fixed-size window maximized.' }
}
`);

  const missing = session('missing-runtime', 1033);
  const fallback = start(missing.executable, {...process.env, WEBVIEW2_BROWSER_EXECUTABLE_FOLDER: path.join(root, 'absent-runtime')});
  await waitFor(async () => fallback.exitCode === null ? undefined : fallback.exitCode, 'native fallback');
  assert.equal(fallback.exitCode, 77);
  assert.equal(existsSync(path.join(root, 'calls.txt')), false, 'Fallback must never run the backend');

  for (const language of [2052, 1033]) {
    console.log(`Checking WebView2 locale ${language}...`);
    const locked = language === 1033;
    const fixture = session(String(language), language, locked);
    const debugPort = await port();
    const child = start(fixture.executable, {...process.env,
      WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${debugPort}`});
    const browser = await connect(debugPort, child);
    await browser.call('Runtime.runIfWaitingForDebugger');
    console.log(`Connected to WebView2 locale ${language}.`);
    await waitFor(async () => await browser.evaluate(`document.getElementById('installer')?.dataset.state === 'ready'`) || undefined, 'ready page');
    assert.equal(await browser.evaluate('document.documentElement.lang'), language === 2052 ? 'zh-CN' : 'en');
    assert.equal(await browser.evaluate(`document.getElementById('directory').readOnly`), locked);
    assert.equal(await browser.evaluate(`document.getElementById('uninstall').hidden`), !locked);
    if (!locked) {
      await browser.evaluate(`window.chrome.webview.postMessage('uninstall')`);
      assert.equal(await browser.evaluate(`document.getElementById('installer').dataset.state`), 'ready');
      assert.equal(child.exitCode, null, 'Fresh installation must reject uninstall messages');
    }
    await browser.call('Page.navigate', {url: 'https://example.invalid/'});
    assert.equal(await browser.evaluate('location.href'), 'about:blank', 'External navigation must be blocked');
    assert.deepEqual(await browser.evaluate(`(() => {const r = document.querySelector('.brand-icon').getBoundingClientRect(); return [r.width, r.height];})()`), [96, 96]);
    assert.deepEqual(await browser.evaluate('[innerWidth, innerHeight]'), [560, 420]);
    assert.equal(await browser.evaluate(`getComputedStyle(document.querySelector('.titlebar')).webkitAppRegion`), 'drag');
    assert.equal(await browser.evaluate(`getComputedStyle(document.getElementById('cancel')).webkitAppRegion`), 'no-drag');
    async function screenshot(name: string) {
      if (!capture) return;
      await delay(150);
      const shot = await browser.call('Page.captureScreenshot', {format: 'png'});
      writeFileSync(path.join(captureDir, `${name}-${language}.png`), Buffer.from(shot.data, 'base64'));
    }
    if (capture) {
      await browser.call('Page.enable');
      const checkWindow = (minimized = false) => {
        const result = spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', windowProbe,
          '-InstallerProcessId', String(child.pid), ...(minimized ? ['-Minimized'] : [])], {encoding: 'utf8', windowsHide: true});
        assert.equal(result.status, 0, result.stderr);
      };
      checkWindow();
      await browser.evaluate(`document.getElementById('minimize').click()`);
      await delay(150);
      checkWindow(true);
      for (const theme of ['light', 'dark']) {
        await browser.call('Emulation.setEmulatedMedia', {features: [
          {name: 'prefers-color-scheme', value: theme}, {name: 'prefers-reduced-motion', value: 'reduce'}]});
        await screenshot(`web-ready-${theme}`);
      }
      await browser.call('Emulation.setEmulatedMedia', {features: [
        {name: 'prefers-color-scheme', value: 'light'}, {name: 'prefers-reduced-motion', value: 'reduce'}]});
    }

    const selected = locked ? fixture.destination : path.join(root, 'Edited directory 雪');
    if (!locked) await browser.evaluate(`document.getElementById('change-path').click()`);
    await browser.evaluate(`document.getElementById('directory').value = ${JSON.stringify(locked ? path.join(root, 'Must not move') : selected)}`);
    if (!locked) {
      await screenshot('web-path-edit');
      await browser.evaluate(`document.getElementById('confirm-path').click()`);
      assert.equal(await browser.evaluate(`document.getElementById('path-edit').hidden`), true);
    }
    writeFileSync(path.join(root, 'fail'), 'failure');
    await browser.evaluate(`document.getElementById('install-form').requestSubmit()`);
    await browser.evaluate(`window.chrome.webview.postMessage('install:ignored duplicate'); window.chrome.webview.postMessage('cancel'); window.chrome.webview.postMessage('uninstall')`);
    assert.equal(await browser.evaluate(`document.getElementById('cancel').disabled`), true);
    await screenshot('web-installing');
    await waitFor(async () => await browser.evaluate(`document.getElementById('installer').dataset.state === 'error'`) || undefined, 'installation error');
    assert.ok(await browser.evaluate(`document.getElementById('error-code').textContent.includes('42')`));
    assert.equal(child.exitCode, null, 'Cancellation during installation must be ignored');
    await screenshot('web-error');
    await browser.evaluate(`document.getElementById('error-detail-toggle').click()`);
    assert.equal(await browser.evaluate(`document.getElementById('error-details').open`), true);
    await screenshot('web-error-details');
    await browser.evaluate(`document.getElementById('close-details').click()`);
    rmSync(path.join(root, 'fail'));
    await browser.evaluate(`document.getElementById('install-form').requestSubmit()`);
    await waitFor(async () => await browser.evaluate(`document.getElementById('installer').dataset.state === 'complete'`) || undefined, 'successful installation');
    assert.equal(existsSync(path.join(selected, 'Application')), true);
    await browser.evaluate(`window.chrome.webview.postMessage('uninstall')`);
    assert.equal(await browser.evaluate(`document.getElementById('uninstall').hidden`), true);
    assert.equal(child.exitCode, null, 'Completed installation must reject uninstall messages');
    await screenshot('web-finish');
    await browser.evaluate(`document.getElementById('install-form').requestSubmit()`);
    await waitFor(async () => await browser.evaluate(`document.getElementById('installer').dataset.state === 'error'`) || undefined, 'launch failure');
    assert.equal(await browser.evaluate(`document.getElementById('cancel').disabled`), false);
    await browser.evaluate(`document.getElementById('cancel').click()`);
    await waitFor(async () => child.exitCode === null ? undefined : child.exitCode, 'host completion');
    assert.equal(child.exitCode, 0);
    browser.close();
    const calls = readFileSync(path.join(root, 'calls.txt'), 'utf8').trimEnd().split('\n');
    assert.deepEqual(calls.slice(-2), [selected, selected], 'Only one backend per attempt, preserving Unicode and repair path');
  }
  assert.equal(readFileSync(path.join(root, 'calls.txt'), 'utf8').trimEnd().split('\n').length, 4);
  for (const language of [2052, 1033]) {
    const fixture = session(`uninstall-${language}`, language, true);
    const debugPort = await port();
    const child = start(fixture.executable, {...process.env,
      WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${debugPort}`});
    const browser = await connect(debugPort, child);
    await waitFor(async () => await browser.evaluate(`document.getElementById('installer')?.dataset.state === 'ready'`) || undefined, 'uninstall action');
    assert.equal(await browser.evaluate(`document.getElementById('uninstall').hidden`), false);
    await browser.evaluate(`document.getElementById('uninstall').click()`);
    await waitFor(async () => child.exitCode === null ? undefined : child.exitCode, 'uninstaller handoff');
    assert.equal(child.exitCode, 78, 'The wrapper must receive the uninstall request');
    browser.close();
  }
  assert.equal(readFileSync(path.join(root, 'calls.txt'), 'utf8').trimEnd().split('\n').length, 4,
    'Uninstall must never run the installation backend');
  console.log('WebView2 host integration checks passed (isolated backend, runtime fallback, both locales, retry, repair and uninstall handoff).');
} finally {
  for (const child of processes) if (child.exitCode === null) child.kill();
  // This is the unique temporary root created by this test, never an installation.
  rmSync(root, {recursive: true, force: true, maxRetries: 10, retryDelay: 200});
}
