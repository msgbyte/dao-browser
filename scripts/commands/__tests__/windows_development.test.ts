// @vitest-environment node
import {existsSync, mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {afterEach, describe, expect, it} from 'vitest';

import * as utils from '../../utils.js';
import * as build from '../build.js';
import {parsePatchTargets, buildFixImportPatchesCommand} from '../import.js';

const temporaryRoots: string[] = [];
afterEach(() => {
  for (const root of temporaryRoots.splice(0)) {
    rmSync(root, {recursive: true, force: true});
  }
});

function fixtureDirectory(): string {
  const root = mkdtempSync(path.join(os.tmpdir(), 'dao-tools-'));
  temporaryRoots.push(root);
  const directory = path.join(root, 'tools with spaces \u96ea');
  mkdirSync(directory);
  return directory;
}

describe('native development target', () => {
  it('uses the host without rewriting the project configuration', () => {
    const config = utils.loadConfig();
    const original = structuredClone(config);
    expect(utils.resolveBuildTarget(config, 'win32', 'x64'))
        .toEqual({os: 'win', cpu: 'x64'});
    expect(utils.resolveBuildTarget(config, 'darwin', 'arm64'))
        .toEqual({os: 'mac', cpu: 'arm64'});
    expect(utils.resolveBuildTarget(config, 'darwin', 'x64'))
        .toEqual({os: 'mac', cpu: 'arm64'});
    expect(config).toEqual(original);
    expect(() => utils.resolveBuildTarget(config, 'win32', 'arm64')).toThrow();
    expect(() => utils.resolveBuildTarget(config, 'linux', 'x64')).toThrow();
  });

  it('keeps Windows lld and macOS development linker behavior', () => {
    const config = utils.loadConfig();
    const common = readFileSync(path.join(utils.CONFIGS_DIR, 'common.gn'), 'utf8');
    const args = build.createBuildArgs(
        config, {os: 'win', cpu: 'x64'}, true, common, 'use_lld = true\n');
    expect(args).toContain('target_os = "win"');
    expect(args).toContain('target_cpu = "x64"');
    expect(args).not.toContain('target_cpu = "arm64"');
    expect(args).not.toContain('use_lld = false');
    expect(args).toContain('is_component_build = true');
    expect(args).toContain('is_official_build = false');
    expect(args).toContain('dao_display_version = "1.0.108.0"');
    expect(build.createBuildArgs(
        config, {os: 'mac', cpu: 'arm64'}, true, common, 'use_lld = true\n'))
        .toContain('use_lld = false');
  });
});

describe('portable patch paths', () => {
  it('rejects Windows escape paths in portable patch headers', () => {
    for (const target of ['C:/outside.txt', 'C:\\outside.txt',
      '\\\\server\\share\\outside.txt', '..\\outside.txt']) {
      expect(() => parsePatchTargets(
          `diff --git a/${target} b/${target}\n--- a/${target}\n+++ b/${target}\n`))
          .toThrow(/unsafe/i);
    }
  });

  it('formats repair paths for the shell without drive-letter paths', () => {
    const patch = path.join(utils.PATCHES_DIR, 'chrome', 'test.cc.patch');
    expect(buildFixImportPatchesCommand([patch]))
        .toBe("sh scripts/fix-import-patches.sh 'src/patches/chrome/test.cc.patch'");
  });
});

describe('development tool invocation', () => {
  it('finds installed executables and reports a missing tool', () => {
    expect(utils.which('node')).toBeTruthy();
    expect(utils.which('dao-tool-that-does-not-exist')).toBeNull();
  });

  it('passes executable arguments literally, including shell syntax', async () => {
    const directory = fixtureDirectory();
    const script = path.join(directory, 'record.cjs');
    const output = path.join(directory, 'arguments.json');
    writeFileSync(script,
        'require("node:fs").writeFileSync(process.argv[2], JSON.stringify(process.argv.slice(3)));');
    const args = ['a b', '', '& echo unsafe', '%PATH%', 'quote"value', '(value)', '雪'];
    expect(await utils.runStreaming(process.execPath, [script, output, ...args])).toBe(0);
    expect(JSON.parse(readFileSync(output, 'utf8'))).toEqual(args);
  });

  it.runIf(process.platform === 'win32')('runs batch tools from paths with spaces', async () => {
    const directory = fixtureDirectory();
    const script = path.join(directory, 'record.cjs');
    const command = path.join(directory, 'tool.cmd');
    const output = path.join(directory, 'arguments.json');
    writeFileSync(script,
        'require("node:fs").writeFileSync(process.argv[2], JSON.stringify(process.argv.slice(3))); process.exit(7);');
    writeFileSync(command, `@echo off\r\n"${process.execPath}" "${script}" %*\r\n`);
    const args = ['a b', '', '& echo unsafe', '(value)', '雪'];
    expect(await utils.runStreaming(command, [output, ...args])).toBe(7);
    expect(JSON.parse(readFileSync(output, 'utf8'))).toEqual(args);
    for (const unsafe of ['%PATH%', '" & echo unsafe', 'line\nbreak', '!PATH!']) {
      await expect(utils.runStreaming(command, [unsafe])).rejects.toThrow(/batch/i);
    }
    const sentinel = path.join(directory, 'injected.txt');
    await expect(utils.runStreaming(command, [
      `" & echo injected > "${sentinel}" & rem "`,
    ])).rejects.toThrow(/batch/i);
    expect(existsSync(sentinel)).toBe(false);
  });
});
