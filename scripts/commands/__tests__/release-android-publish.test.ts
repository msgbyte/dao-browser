// @vitest-environment node
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {afterEach, beforeEach, expect, it} from 'vitest';

const workflow = readFileSync('.github/workflows/publish-android-github-release.yml', 'utf8');
function script(name: string) {
  const step = workflow.split(`      - name: ${name}\n`)[1]?.split('\n      - name:')[0];
  if (!step?.includes('run: |\n')) throw new Error(`Missing workflow script: ${name}`);
  return step.split('run: |\n')[1].replace(/^          /gm, '');
}

// Execute the workflow scripts while replacing GitHub network calls.
const fakeGh = String.raw`#!/usr/bin/env node
const fs = require('node:fs');
const stateFile = process.env.DAO_RELEASE_TEST_STATE;
const state = JSON.parse(fs.readFileSync(stateFile));
const args = process.argv.slice(2);
const save = () => fs.writeFileSync(stateFile, JSON.stringify(state));
const fail = message => {save(); console.error(message); process.exit(1);};
state.calls.push(args);
if (args[0] === 'api' && args[1] === 'graphql') {
  if (state.deny) fail('HTTP 403');
  save(); console.log(state.release); process.exit(0);
}
if (args[0] !== 'release') fail('Unexpected GitHub command');
if (args[1] === 'create') {
  if (state.release !== 'missing' || !args.includes('--draft') || !args.includes('--verify-tag') || !args.includes('--latest=false')) fail('Unsafe create');
  state.release = 'draft';
} else if (args[1] === 'upload') {
  if (state.release !== 'draft' || !args.includes('--clobber')) fail('Unsafe replacement');
  if (state.failUpload) {state.assets = {'dao-browser-0.1.1-android.apk': ''}; fail('HTTP 502');}
  state.assets = {};
  for (const file of args.slice(3).filter(arg => !arg.startsWith('--'))) {
    state.assets[require('node:path').basename(file)] = fs.readFileSync(file, 'utf8');
  }
} else if (args[1] === 'edit') {
  if (state.release !== 'draft' || !args.includes('--draft=false') || !args.includes('--latest=false') || !args.includes('--prerelease=false')) fail('Unsafe publish');
  if (Object.keys(state.assets || {}).length !== 2) fail('Published before upload');
  state.release = 'published';
} else fail('Unexpected release command');
save();
`;
let directory: string;
let stateFile: string;
let outputFile: string;
const state = () => JSON.parse(readFileSync(stateFile, 'utf8'));
const save = (value: unknown) => writeFileSync(stateFile, JSON.stringify(value));
const run = (name: string, env: NodeJS.ProcessEnv = {}) => spawnSync('bash', ['-e', '-u', '-o', 'pipefail', '-c', script(name)], {
  cwd: directory, encoding: 'utf8',
  env: {...process.env, PATH: `${directory}/bin${path.delimiter}${process.env.PATH}`,
    DAO_RELEASE_TEST_STATE: stateFile, GITHUB_OUTPUT: outputFile,
    GITHUB_REPOSITORY: 'msgbyte/dao-browser', GITHUB_REF_NAME: 'android-v0.1.1',
    VERSION: '0.1.1', VERSION_CODE: '2', ...env},
});
beforeEach(() => {
  directory = mkdtempSync(path.join(tmpdir(), 'dao-android-workflow-'));
  stateFile = path.join(directory, 'state.json');
  outputFile = path.join(directory, 'output');
  mkdirSync(path.join(directory, 'bin'));
  mkdirSync(path.join(directory, 'android/app'), {recursive: true});
  mkdirSync(path.join(directory, 'dist'));
  writeFileSync(path.join(directory, 'android/app/build.gradle.kts'), 'versionName = "0.1.1"\nversionCode = 2\n');
  writeFileSync(path.join(directory, 'bin/gh'), fakeGh, {mode: 0o755});
  writeFileSync(path.join(directory, 'dist/dao-browser-0.1.1-android.apk'), 'verified signed APK');
  writeFileSync(path.join(directory, 'dist/SHA256SUMS'), 'checksum');
  save({release: 'missing', calls: [], assets: {}});
});
afterEach(() => rmSync(directory, {recursive: true, force: true}));
it('rejects invalid or mismatched tags before enabling a build', () => {
  const valid = run('Validate release');
  expect(valid.status, valid.stderr).toBe(0);
  expect(readFileSync(outputFile, 'utf8')).toContain('build=true');
  expect(readFileSync(outputFile, 'utf8')).toContain('version_code=2');
  for (const tag of ['v1.0.0', 'android-v0.1.2', 'android-v01.1.1']) {
    expect(run('Validate release', {GITHUB_REF_NAME: tag}).status).not.toBe(0);
  }
});
it('skips published tags and stops on GitHub access errors before building', () => {
  save({...state(), release: 'published'});
  expect(run('Validate release').status).toBe(0);
  expect(readFileSync(outputFile, 'utf8')).toContain('build=false');
  save({...state(), deny: true});
  expect(run('Validate release').status).not.toBe(0);
});
it('checks the APK signature, package, version and debug flag before packaging its checksum', () => {
  const sdk = path.join(directory, 'sdk');
  const tools = path.join(sdk, 'build-tools/36.0.0');
  const apk = path.join(directory, 'android/app/build/outputs/apk/release/app-release.apk');
  mkdirSync(tools, {recursive: true});
  mkdirSync(path.dirname(apk), {recursive: true});
  writeFileSync(apk, 'built signed APK');
  writeFileSync(path.join(tools, 'apksigner'), '#!/bin/sh\ntest "$1" = verify || exit 2\nexit "${SIGNATURE_EXIT:-0}"\n', {mode: 0o755});
  writeFileSync(path.join(tools, 'aapt2'), '#!/bin/sh\nprintf "%s\\n" "$BADGING"\n', {mode: 0o755});
  const badging = "package: name='com.msgbyte.dao' versionCode='2' versionName='0.1.1'";
  const env = {ANDROID_HOME: sdk, BADGING: badging};
  const valid = run('Verify and package APK', env);
  expect(valid.status, valid.stderr).toBe(0);
  expect(readFileSync(path.join(directory, 'dist/dao-browser-0.1.1-android.apk'), 'utf8')).toBe('built signed APK');
  const digest = createHash('sha256').update('built signed APK').digest('hex');
  expect(readFileSync(path.join(directory, 'dist/SHA256SUMS'), 'utf8')).toBe(`${digest}  dao-browser-0.1.1-android.apk\n`);
  for (const invalid of [
    {SIGNATURE_EXIT: '1'},
    {BADGING: badging.replace('com.msgbyte.dao', 'other.app')},
    {BADGING: badging.replace("versionCode='2'", "versionCode='1'")},
    {BADGING: badging.replace('0.1.1', '0.1.0')},
    {BADGING: `${badging}\napplication-debuggable`},
  ]) expect(run('Verify and package APK', {...env, ...invalid}).status).not.toBe(0);
});
it('recovers an interrupted draft but never overwrites published APK bytes', () => {
  save({...state(), failUpload: true});
  expect(run('Publish GitHub Release').status).not.toBe(0);
  expect(state().release).toBe('draft');
  expect(state().assets['dao-browser-0.1.1-android.apk']).toBe('');
  save({...state(), failUpload: false});
  const resumed = run('Publish GitHub Release');
  expect(resumed.status, resumed.stderr).toBe(0);
  expect(state().release).toBe('published');
  expect(state().assets).toEqual({'dao-browser-0.1.1-android.apk': 'verified signed APK', SHA256SUMS: 'checksum'});
  writeFileSync(path.join(directory, 'dist/dao-browser-0.1.1-android.apk'), 'rebuilt different bytes');
  save({...state(), calls: []});
  expect(run('Publish GitHub Release').status).toBe(0);
  expect(state().calls).toHaveLength(1);
  expect(state().assets['dao-browser-0.1.1-android.apk']).toBe('verified signed APK');
});
it('does not create a release after a failed lookup', () => {
  save({...state(), deny: true});
  expect(run('Publish GitHub Release').status).not.toBe(0);
  expect(state().release).toBe('missing');
  expect(state().calls).toHaveLength(1);
});
