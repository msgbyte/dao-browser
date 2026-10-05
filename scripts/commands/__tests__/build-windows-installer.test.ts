// @vitest-environment node
import {createHash} from 'node:crypto';
import {mkdirSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import path from 'node:path';
import {afterAll, beforeEach, describe, expect, it, vi} from 'vitest';

const {root} = await vi.hoisted(async () => {
  const fs = await import('node:fs');
  const os = await import('node:os');
  const path = await import('node:path');
  return {root: fs.mkdtempSync(path.join(os.tmpdir(), 'dao-host-test-'))};
});
vi.mock('../../utils.js', async importOriginal => ({
  ...await importOriginal<typeof import('../../utils.js')>(),
  ROOT_DIR: root,
}));
import {createCrossInstallerBuildCommands, getWindowsInstallerHost, renderWindowsInstallerHtml} from '../build-windows-installer.js';

const inputs = [
  'scripts/commands/build-windows-installer.ts',
  'scripts/windows-toolchain.ts',
  'scripts/windows-installer/native/host.cc',
  'scripts/windows-installer/native/host.manifest',
  'branding/win/dao.ico',
  'scripts/windows-installer/web/locales/en.json',
  'scripts/windows-installer/web/locales/zh-CN.json',
];
function write(relative: string, content: string) {
  const file = path.join(root, relative);
  mkdirSync(path.dirname(file), {recursive: true});
  writeFileSync(file, content);
}
const hash = (content: string | Buffer) => createHash('sha256').update(content).digest('hex');
beforeEach(() => {
  rmSync(root, {recursive: true, force: true});
  for (const input of inputs) write(input, input);
  write('.dao/installer/dao-installer-ui.exe', 'compiled host');
  write('.dao/installer/WebView2-LICENSE.txt', 'SDK license');
  write('.dao/installer/build.json', JSON.stringify({
    version: 1,
    sdkVersion: '1.0.4258.31',
    inputs: Object.fromEntries(inputs.map(input => [input, hash(input)])),
    executableSha256: hash('compiled host'),
    licenseSha256: hash('SDK license'),
  }));
});
afterAll(() => rmSync(root, {recursive: true, force: true}));

describe('Windows installer host assets', () => {
  it('cross-compiles resources and native sources with Chromium tools and explicit SDK paths', () => {
    const sdk = path.join(root, 'WebView SDK');
    const staging = path.join(root, 'Cross build');
    const includeDirs = [path.join(root, 'VS/include'), path.join(root, 'SDK/Include/um')];
    const libDirs = [path.join(root, 'VS/lib/x64'), path.join(root, 'SDK/Lib/um/x64')];
    const toolchain = {
      toolchainRoot: path.join(root, 'VS'), sdkDir: path.join(root, 'SDK'), includeDirs, libDirs,
      clangCl: path.join(root, 'clang-cl'), lldLink: path.join(root, 'lld-link'),
      rcScript: path.join(root, 'rc.py'), env: {},
    };
    const [resources, compile, link] = createCrossInstallerBuildCommands(toolchain, sdk, staging);
    expect(resources.command).toBe('python3');
    expect(resources.args).toContain(toolchain.rcScript);
    expect(resources.args).toContain(`/fo${path.join(staging, 'host.res')}`);
    expect(resources.args).not.toContain('/fo');
    expect(resources.args).toContain(`-imsvc${includeDirs[0]}`);
    expect(compile.command).toBe(toolchain.clangCl);
    expect(compile.args).toEqual(expect.arrayContaining(['--target=x86_64-pc-windows-msvc', '/c', '/MT', '/X']));
    expect(compile.args).toContain(`-imsvc${includeDirs[1]}`);
    expect(compile.args).toContain(path.join(sdk, 'build/native/include'));
    expect(link.command).toBe(toolchain.lldLink);
    expect(link.args).toEqual(expect.arrayContaining(['/MACHINE:X64', '/SUBSYSTEM:WINDOWS', 'kernel32.lib']));
    expect(link.args).toContain(`/LIBPATH:${libDirs[1]}`);
    expect(link.args).toContain(path.join(sdk, 'build/native/x64/WebView2LoaderStatic.lib'));
    expect(link.args).toContain(path.join(staging, 'host.obj'));
    expect(link.args).toContain(`/OUT:${path.join(staging, 'dao-installer-ui.exe')}`);
  });

  it('accepts a matching host and rejects source changes with a rebuild command', () => {
    expect(getWindowsInstallerHost()).toBe(path.join(root, '.dao/installer/dao-installer-ui.exe'));
    write('scripts/windows-installer/native/host.cc', 'changed native source');
    expect(() => getWindowsInstallerHost()).toThrow(/npm run rebuild -- -- --release --target dao_installer_ui/);
  });

  it.each(['.dao/installer/dao-installer-ui.exe', '.dao/installer/build.json',
    '.dao/installer/WebView2-LICENSE.txt'])('rejects a missing or corrupted %s', file => {
    write(file, 'corrupted');
    expect(() => getWindowsInstallerHost()).toThrow(/rebuild/);
    rmSync(path.join(root, file));
    expect(() => getWindowsInstallerHost()).toThrow(/rebuild/);
  });

  it('embeds the complete local page and escapes locale script delimiters', () => {
    write('scripts/windows-installer/web/index.html', '<style>{{STYLE}}</style><img src="{{ICON_DATA_URL}}"><script>const locales={{LOCALES}};</script><script type="module">{{SCRIPT}}</script>');
    write('scripts/windows-installer/web/style.css', 'body { color: blue; }');
    write('scripts/windows-installer/web/app.js', 'export function mountInstaller() {}');
    write('scripts/windows-installer/web/locales/en.json', JSON.stringify({title: '</script><script>alert(1)</script>'}));
    write('scripts/windows-installer/web/locales/zh-CN.json', JSON.stringify({title: 'Localized title'}));
    write('scripts/windows-installer/web/assets/dao-logo.png', 'icon');
    const html = renderWindowsInstallerHtml();
    expect(html).toContain('body { color: blue; }');
    expect(html).toContain('export function mountInstaller() {}');
    expect(html).toContain('data:image/png;base64,aWNvbg==');
    expect(html).toContain('\\u003c/script>');
    expect(html.match(/<script/g)).toHaveLength(2);
    expect(html).not.toMatch(/\{\{(?:STYLE|SCRIPT|LOCALES|ICON_DATA_URL)\}\}/);
  });
});
