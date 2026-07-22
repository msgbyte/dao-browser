import {execFileSync, spawn} from 'node:child_process';
import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  renameSync,
  unlinkSync,
  writeFileSync,
} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

import {describe, expect, it} from 'vitest';
import * as releaseModule from '../release.js';

import {
  collectReferencedDeltaBasenames,
  buildReleaseApplication,
  formatNotarizeRecoveryCommand,
  formatReleaseFailure,
  generateReleaseAppcast,
  importReleaseSources,
  injectGitCommitIntoAppcast,
  inspectReleaseTag,
  packageReleaseArtifact,
  plannedReleasePhases,
  ReleaseError,
  type ReleaseCommandRunner,
  type ReleaseDependencies,
  type ReleasePhase,
  type ReleasePhaseContext,
  runRelease,
  runReleaseWithSignals,
  runReleaseStep,
  uploadReleaseArtifacts,
  updateInfoJson,
} from '../release.js';
import {ReleaseTransaction} from '../release-transaction.js';
import {
  createStreamingSpawnOptions,
  ProcessTerminationError,
  runStreaming,
} from '../../utils.js';

interface ReleaseFixture {
  root: string;
  daoPath: string;
  appcastPath: string;
  infoPath: string;
  createdTags: string[];
  createdTagTargets: string[];
  createdTagObjectId: string;
  deletedTags: Array<{tagName: string; expectedObjectId: string}>;
  phases: ReleasePhase[];
  failPhase: ReleasePhase | null;
  dependencies: ReleaseDependencies;
}

function releaseFixture(): ReleaseFixture {
  const root = mkdtempSync(path.join(os.tmpdir(), 'dao-release-flow-'));
  mkdirSync(path.join(root, 'website/public'), {recursive: true});
  mkdirSync(path.join(root, 'branding'), {recursive: true});
  mkdirSync(path.join(root, 'third_party/sparkle/bin'), {recursive: true});
  mkdirSync(path.join(root, 'dist'), {recursive: true});
  const daoPath = path.join(root, 'dao.json');
  const appcastPath = path.join(root, 'website/public/appcast.xml');
  const infoPath = path.join(root, 'website/public/info.json');
  writeFileSync(daoPath, JSON.stringify({
    name: 'dao',
    display_name: 'Dao',
    version: {
      product: 'chromium',
      version: '149.0.7827.201',
      display: '1.0.70',
    },
    build: {target_os: 'mac', target_cpu: 'arm64'},
  }, null, 2) + '\n');
  writeFileSync(appcastPath, '<rss><channel></channel></rss>\n');
  writeFileSync(infoPath, JSON.stringify({
    version: '1.0.70',
    chromiumVersion: '149.0.7827.201',
    releasedAt: '2026-07-11',
    platforms: {
      macArm64: {
        url: 'https://dao-release.msgbyte.com/' +
          'dao-browser-1.0.70-mac-arm64.dmg',
      },
    },
  }, null, 2) + '\n');
  writeFileSync(
    path.join(root, 'branding/appcast.template.xml'),
    '<rss><channel></channel></rss>\n',
  );
  writeFileSync(
    path.join(root, 'third_party/sparkle/bin/generate_appcast'),
    'fake\n',
  );

  const fixture = {
    root,
    daoPath,
    appcastPath,
    infoPath,
    createdTags: [] as string[],
    createdTagTargets: [] as string[],
    createdTagObjectId: 'd'.repeat(40),
    deletedTags: [] as Array<{
      tagName: string;
      expectedObjectId: string;
    }>,
    phases: [] as ReleasePhase[],
    failPhase: null as ReleasePhase | null,
    dependencies: undefined as unknown as ReleaseDependencies,
  };
  fixture.dependencies = {
    rootDir: root,
    env: {
      DAO_SIGN_IDENTITY: 'Developer ID Application: Dao Test (TEST)',
      DAO_NOTARIZE_KEYCHAIN_PROFILE: 'dao-test',
      CLOUDFLARE_ACCOUNT_ID: 'account',
      CLOUDFLARE_API_TOKEN: 'token',
      R2_BUCKET: 'releases',
    },
    now: () => new Date('2026-07-12T00:00:00+08:00'),
    head: () => 'a'.repeat(40),
    tagState: () => ({exists: false}),
    createTag: (tagName, commit) => {
      fixture.createdTags.push(tagName);
      fixture.createdTagTargets.push(commit);
      return fixture.createdTagObjectId;
    },
    deleteTag: (tagName, expectedObjectId) => {
      fixture.deletedTags.push({tagName, expectedObjectId});
    },
    runPhase: async (phase, context) => {
      fixture.phases.push(phase);
      if (fixture.failPhase === phase) {
        throw new ReleaseError(phase, phase + ' failed');
      }
      if (phase === 'package') {
        writeFileSync(context.dmgPath, 'signed dmg');
      }
      if (phase === 'appcast') {
        writeFileSync(
          context.appcastPath,
          '<rss><channel><item><enclosure url="' +
            path.basename(context.dmgPath) +
            '" /></item></channel></rss>\n',
        );
      }
    },
  };
  return fixture;
}

function fixtureContext(
  fixture: ReleaseFixture,
  options: ReleasePhaseContext['options'] = {},
): ReleasePhaseContext {
  return {
    options,
    oldVersion: '1.0.70',
    newVersion: '1.0.71',
    releaseHead: 'a'.repeat(40),
    dmgPath: path.join(
      fixture.root,
      'dist/dao-browser-1.0.71-mac-arm64.dmg',
    ),
    appcastPath: path.join(fixture.root, 'dist/appcast.xml'),
  };
}

function tempFile(name: string, content: string): string {
  const dir = mkdtempSync(path.join(os.tmpdir(), 'dao-release-test-'));
  const file = path.join(dir, name);
  writeFileSync(file, content);
  return file;
}

async function waitForFile(filePath: string): Promise<void> {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    if (existsSync(filePath)) return;
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  throw new Error('Timed out waiting for ' + filePath);
}

describe('release helpers', () => {
  it('keeps no-signal child processes in the foreground process group', () => {
    const options = createStreamingSpawnOptions(
      '/tmp',
      process.env,
      undefined,
      'darwin',
    );

    expect(Object.hasOwn(options, 'detached')).toBe(false);
  });

  it('uses a separate POSIX process group only for abort-controlled calls',
     () => {
       const signal = new AbortController().signal;
       expect(createStreamingSpawnOptions(
         '/tmp',
         process.env,
         signal,
         'darwin',
       ).detached).toBe(true);
       expect(Object.hasOwn(createStreamingSpawnOptions(
         '/tmp',
         process.env,
         signal,
         'win32',
       ), 'detached')).toBe(false);
     });

  it('allows a TERM-handling child to close before kill escalation',
     async () => {
       const root = mkdtempSync(path.join(os.tmpdir(), 'dao-release-term-'));
       const scriptPath = path.join(root, 'term-handler.cjs');
       const readyPath = path.join(root, 'ready');
       const termPath = path.join(root, 'term-handled');
       writeFileSync(scriptPath, [
         "const fs = require('node:fs');",
         'process.on("SIGTERM", () => {',
         '  fs.writeFileSync(process.argv[3], "handled");',
         '  setTimeout(() => process.exit(0), 25);',
         '});',
         'fs.writeFileSync(process.argv[2], "ready");',
         'setInterval(() => {}, 1000);',
       ].join('\n'));
       const controller = new AbortController();
       const child = runStreaming(process.execPath, [
         scriptPath,
         readyPath,
         termPath,
       ], {signal: controller.signal});

       await waitForFile(readyPath);
       controller.abort(new Error('stop child'));

       await expect(child).rejects.toMatchObject({name: 'AbortError'});
       expect(readFileSync(termPath, 'utf-8')).toBe('handled');
     });

  it('escalates to KILL when the process group ignores TERM', async () => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-release-kill-'));
    const scriptPath = path.join(root, 'ignore-term.cjs');
    const readyPath = path.join(root, 'ready');
    const termPath = path.join(root, 'term-received');
    const latePath = path.join(root, 'late-write');
    const pidPath = path.join(root, 'pid');
    writeFileSync(scriptPath, [
      "const fs = require('node:fs');",
      'process.on("SIGTERM", () => {',
      '  fs.writeFileSync(process.argv[3], "received");',
      '});',
      'fs.writeFileSync(process.argv[2], "ready");',
      'fs.writeFileSync(process.argv[5], String(process.pid));',
      'setTimeout(() => fs.writeFileSync(process.argv[4], "late"), 600);',
      'setInterval(() => {}, 1000);',
    ].join('\n'));
    const controller = new AbortController();
    const child = runStreaming(process.execPath, [
      scriptPath,
      readyPath,
      termPath,
      latePath,
      pidPath,
    ], {signal: controller.signal});

    try {
      await waitForFile(readyPath);
      controller.abort(new Error('stop child'));
      await expect(Promise.race([
        child,
        new Promise((_resolve, reject) => setTimeout(
          () => reject(new Error('kill escalation timed out')),
          1500,
        )),
      ])).rejects.toMatchObject({name: 'AbortError'});
      expect(readFileSync(termPath, 'utf-8')).toBe('received');
      await new Promise((resolve) => setTimeout(resolve, 650));
      expect(existsSync(latePath)).toBe(false);
    } finally {
      if (existsSync(pidPath)) {
        try {
          process.kill(Number(readFileSync(pidPath, 'utf-8')), 'SIGKILL');
        } catch {
          // Kill escalation already terminated the process.
        }
      }
    }
  });

  it('surfaces a bounded process-group termination timeout', async () => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-release-timeout-'));
    const scriptPath = path.join(root, 'stuck.cjs');
    const readyPath = path.join(root, 'ready');
    const pidPath = path.join(root, 'pid');
    writeFileSync(scriptPath, [
      "const fs = require('node:fs');",
      'fs.writeFileSync(process.argv[2], "ready");',
      'fs.writeFileSync(process.argv[3], String(process.pid));',
      'setInterval(() => {}, 1000);',
    ].join('\n'));
    const controller = new AbortController();
    const child = runStreaming(process.execPath, [
      scriptPath,
      readyPath,
      pidPath,
    ], {
      signal: controller.signal,
      lifecycle: {
        graceMs: 2,
        pollMs: 1,
        timeoutMs: 15,
        processGroupExists: () => true,
        signalProcessGroup: () => {},
      },
    });

    try {
      await waitForFile(readyPath);
      controller.abort(new Error('Release interrupted by caller.'));
      const failure = await child.catch((cause: unknown) => cause);
      expect(failure).toBeInstanceOf(ProcessTerminationError);
      expect(failure).toMatchObject({
        name: 'ProcessTerminationError',
        processId: expect.any(Number),
        processGroupId: expect.any(Number),
      });
      expect((failure as Error).message).toContain('may still be running');
    } finally {
      if (existsSync(pidPath)) {
        try {
          process.kill(Number(readFileSync(pidPath, 'utf-8')), 'SIGKILL');
        } catch {
          // The test child already exited.
        }
      }
    }
  });

  it('lets an outer wrapper exit after abandoning a timed-out group',
     async () => {
       const root = mkdtempSync(path.join(os.tmpdir(), 'dao-release-outer-'));
       const innerPath = path.join(root, 'inner.cjs');
       const wrapperPath = path.join(root, 'wrapper.mts');
       const readyPath = path.join(root, 'ready');
       const innerPidPath = path.join(root, 'inner-pid');
       const resultPath = path.join(root, 'result.json');
       const utilsUrl = pathToFileURL(
         path.join(process.cwd(), 'scripts/utils.ts'),
       ).href;
       const tsxCli = path.join(process.cwd(), 'node_modules/tsx/dist/cli.mjs');
       writeFileSync(innerPath, [
         "const fs = require('node:fs');",
         'fs.writeFileSync(process.argv[2], String(process.pid));',
         'fs.writeFileSync(process.argv[3], "ready");',
         'setInterval(() => {}, 1000);',
       ].join('\n'));
       writeFileSync(wrapperPath, [
         "import {existsSync, writeFileSync} from 'node:fs';",
         `import {runStreaming} from ${JSON.stringify(utilsUrl)};`,
         'const [inner, pidFile, readyFile, resultFile] = ' +
           'process.argv.slice(2);',
         'const controller = new AbortController();',
         'const child = runStreaming(process.execPath, ' +
           '[inner, pidFile, readyFile], {',
         '  signal: controller.signal,',
         '  lifecycle: {',
         '    graceMs: 2, pollMs: 1, timeoutMs: 15,',
         '    processGroupExists: () => true,',
         '    signalProcessGroup: () => {},',
         '  },',
         '});',
         'while (!existsSync(readyFile)) {',
         '  await new Promise((resolve) => setTimeout(resolve, 1));',
         '}',
         'controller.abort(new Error("wrapper abort"));',
         'try {',
         '  await child;',
         '} catch (cause) {',
         '  writeFileSync(resultFile, JSON.stringify({',
         '    name: cause?.name,',
         '    message: cause?.message,',
         '    processId: cause?.processId,',
         '    processGroupId: cause?.processGroupId,',
         '  }));',
         '}',
       ].join('\n'));
       const outer = spawn(process.execPath, [
         tsxCli,
         wrapperPath,
         innerPath,
         innerPidPath,
         readyPath,
         resultPath,
       ], {stdio: ['ignore', 'pipe', 'pipe']});
       let outerExited = false;
       const outerExit = new Promise<number|null>((resolve) => {
         outer.once('exit', (code) => {
           outerExited = true;
           resolve(code);
         });
       });
       const outerClose = new Promise<void>((resolve) => {
         outer.once('close', () => resolve());
       });

       try {
         const exitCode = await Promise.race([
           outerExit,
           new Promise<null>((resolve) => setTimeout(() => resolve(null), 1500)),
         ]);
         expect(exitCode).toBe(0);
         expect(JSON.parse(readFileSync(resultPath, 'utf-8'))).toMatchObject({
           name: 'ProcessTerminationError',
           processId: expect.any(Number),
           processGroupId: expect.any(Number),
         });
       } finally {
         let processGroupId: number | undefined;
         if (existsSync(resultPath)) {
           processGroupId = JSON.parse(
             readFileSync(resultPath, 'utf-8'),
           ).processGroupId;
         }
         if (processGroupId) {
           try {
             process.kill(-processGroupId, 'SIGKILL');
           } catch {
             // The fixture group already exited.
           }
         }
         if (existsSync(innerPidPath)) {
           try {
             process.kill(
               Number(readFileSync(innerPidPath, 'utf-8')),
               'SIGKILL',
             );
           } catch {
             // The fixture process was cleaned up with its group.
           }
         }
         if (!outerExited) outer.kill('SIGKILL');
         await outerClose;
       }
     });

  it('turns a non-zero child result into a typed phase error', async () => {
    await expect(runReleaseStep(
      'build', false, 'Building (release)', 'npx', ['tsx'], async () => 7,
    )).rejects.toMatchObject({
      name: 'ReleaseError',
      phase: 'build',
      message: 'Step failed (Building (release)): npx exited with code 7',
    });
  });

  it('peels an annotated tag to its target commit', () => {
    const root = mkdtempSync(path.join(os.tmpdir(), 'dao-release-tag-'));
    execFileSync('git', ['init'], {cwd: root, stdio: 'ignore'});
    writeFileSync(path.join(root, 'file.txt'), 'one\n');
    execFileSync('git', ['add', 'file.txt'], {cwd: root});
    execFileSync('git', [
      '-c', 'user.name=Dao Test', '-c', 'user.email=dao@example.com',
      'commit', '-m', 'initial',
    ], {cwd: root, stdio: 'ignore'});
    execFileSync('git', [
      'tag', '-a', 'v1.0.71', '-m', 'Release v1.0.71',
    ], {cwd: root});
    const head = execFileSync('git', ['rev-parse', 'HEAD'], {
      cwd: root, encoding: 'utf-8',
    }).trim();
    const objectId = execFileSync('git', ['rev-parse', 'refs/tags/v1.0.71'], {
      cwd: root, encoding: 'utf-8',
    }).trim();

    expect(inspectReleaseTag(root, 'v1.0.71'))
      .toEqual({exists: true, commit: head, objectId});
  });

  it('builds exact-object CAS arguments for release tag deletion', () => {
    const deleteArguments = (
      releaseModule as unknown as {
        releaseTagDeleteArguments?: (
          tagName: string,
          expectedObjectId: string,
        ) => string[];
      }
    ).releaseTagDeleteArguments;

    expect(deleteArguments).toBeTypeOf('function');
    expect(deleteArguments?.('v1.0.71', 'd'.repeat(40))).toEqual([
      'update-ref',
      '-d',
      'refs/tags/v1.0.71',
      'd'.repeat(40),
    ]);
  });

  it('updates info.json fields and only rewrites version segments in platform URLs',
     () => {
    const file = tempFile('info.json', JSON.stringify({
      $schema: 'kept',
      version: '1.0.0',
      chromiumVersion: '147.0.0.0',
      releasedAt: '2026-01-01',
      platforms: {
        macArm64: {
          label: 'macOS Apple Silicon',
          url: 'https://cdn.example.com/dao-browser-1.0.0-mac-arm64.dmg',
        },
        staticUrl: {
          label: 'Static',
          url: 'https://cdn.example.com/static.dmg',
        },
      },
      default: 'macArm64',
    }, null, 2));

    updateInfoJson(file, {
      version: '1.1.0',
      chromiumVersion: '148.0.0.0',
      releasedAt: '2026-02-03',
    });

    const updated = JSON.parse(readFileSync(file, 'utf-8'));
    expect(updated).toMatchObject({
      $schema: 'kept',
      version: '1.1.0',
      chromiumVersion: '148.0.0.0',
      releasedAt: '2026-02-03',
      default: 'macArm64',
    });
    expect(updated.platforms.macArm64.url)
        .toBe('https://cdn.example.com/dao-browser-1.1.0-mac-arm64.dmg');
    expect(updated.platforms.staticUrl.url)
        .toBe('https://cdn.example.com/static.dmg');
  });

  it('stamps the matching appcast item and preserves unrelated items', () => {
    const file = tempFile('appcast.xml', `<rss version="2.0">
  <channel>
    <item>
      <title>Old</title>
      <enclosure url="https://cdn.example.com/old.dmg" length="1" />
    </item>
    <item>
      <title>Current</title>
      <enclosure url="https://cdn.example.com/current.dmg" length="2" />
    </item>
  </channel>
</rss>`);

    injectGitCommitIntoAppcast(
        file, 'current.dmg',
        'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb');

    const updated = readFileSync(file, 'utf-8');
    expect(updated).toContain(
        'xmlns:dao="https://dao.msgbyte.com/xml-namespaces/dao"');
    expect(updated).toContain(
        '<dao:gitCommit>bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb</dao:gitCommit>');
    expect(updated.match(/<dao:gitCommit>/g)).toHaveLength(1);
    expect(updated).toContain('<title>Old</title>');

    injectGitCommitIntoAppcast(
        file, 'current.dmg',
        'cccccccccccccccccccccccccccccccccccccccc');

    const replaced = readFileSync(file, 'utf-8');
    expect(replaced).not.toContain('bbbbbbbb');
    expect(replaced).toContain(
        '<dao:gitCommit>cccccccccccccccccccccccccccccccccccccccc</dao:gitCommit>');
    expect(replaced.match(/<dao:gitCommit>/g)).toHaveLength(1);
  });

  it('collects only delta enclosure basenames referenced by the appcast', () => {
    const file = tempFile('appcast.xml', `<rss><channel>
      <item>
        <enclosure url="https://cdn.example.com/full.dmg" length="1" />
        <sparkle:deltas>
          <enclosure url="https://cdn.example.com/Dao2.0-1.0.delta"
                     sparkle:deltaFrom="1.0" length="2" />
          <enclosure url="https://cdn.example.com/nested/Dao2.0-0.9.delta"
                     sparkle:deltaFrom="0.9" length="3" />
        </sparkle:deltas>
      </item>
    </channel></rss>`);

    expect(collectReferencedDeltaBasenames(file)).toEqual(new Set([
      'Dao2.0-1.0.delta',
      'Dao2.0-0.9.delta',
    ]));
  });

  it.each([
    [{}, [
      'import', 'build', 'package', 'notarize', 'staple',
      'appcast', 'metadata', 'upload', 'tag',
    ]],
    [{dryRun: true}, [
      'import', 'build', 'package', 'notarize', 'staple',
      'appcast', 'metadata', 'upload', 'tag',
    ]],
    [{skipBuild: true}, ['appcast', 'metadata', 'upload', 'tag']],
    [{skipUpload: true}, [
      'import', 'build', 'package', 'notarize', 'staple',
      'appcast', 'metadata', 'tag',
    ]],
    [{skipBump: true}, [
      'import', 'build', 'package', 'notarize', 'staple',
      'appcast', 'metadata', 'upload', 'tag',
    ]],
    [{resumeFromStaple: true}, [
      'staple', 'appcast', 'metadata', 'upload', 'tag',
    ]],
  ] as const)('plans exact phases for options %o', (options, expected) => {
    expect(plannedReleasePhases(options)).toEqual(expected);
  });

  it('dry-run leaves canonical files and tags untouched', async () => {
    const fixture = releaseFixture();
    const dao = readFileSync(fixture.daoPath);
    const appcast = readFileSync(fixture.appcastPath);
    const info = readFileSync(fixture.infoPath);
    await runRelease({dryRun: true}, fixture.dependencies);
    expect(readFileSync(fixture.daoPath)).toEqual(dao);
    expect(readFileSync(fixture.appcastPath)).toEqual(appcast);
    expect(readFileSync(fixture.infoPath)).toEqual(info);
    expect(fixture.createdTags).toEqual([]);
  });

  it('preserves import, build, and package command arguments', async () => {
    const fixture = releaseFixture();
    const calls: Array<{cmd: string; args: string[]}> = [];
    const runner = async (cmd: string, args: string[]) => {
      calls.push({cmd, args});
      return 0;
    };

    await importReleaseSources(fixtureContext(fixture), runner);
    await importReleaseSources(
      fixtureContext(fixture, {forceImport: false}),
      runner,
    );
    await buildReleaseApplication(fixtureContext(fixture), runner);
    await packageReleaseArtifact(fixtureContext(fixture), runner);

    expect(calls).toEqual([
      {cmd: 'npx', args: ['tsx', 'scripts/cli.ts', 'import', '--force']},
      {cmd: 'npx', args: ['tsx', 'scripts/cli.ts', 'import']},
      {cmd: 'npx', args: ['tsx', 'scripts/cli.ts', 'build']},
      {
        cmd: 'npx',
        args: ['tsx', 'scripts/cli.ts', 'package', '--sign-id'],
      },
    ]);
  });

  it('passes the release signal to every command runner', async () => {
    const fixture = releaseFixture();
    const controller = new AbortController();
    const context = fixtureContext(fixture);
    context.signal = controller.signal;
    writeFileSync(context.dmgPath, 'signed dmg');
    const receivedSignals: Array<AbortSignal | undefined> = [];
    const runner: ReleaseCommandRunner = async (cmd, _args, options) => {
      receivedSignals.push(options?.signal);
      if (cmd.endsWith('generate_appcast')) {
        writeFileSync(
          context.appcastPath,
          '<rss><channel><item><enclosure url="' +
            path.basename(context.dmgPath) +
            '" /></item></channel></rss>\n',
        );
      }
      return 0;
    };

    await importReleaseSources(context, runner);
    await buildReleaseApplication(context, runner);
    await packageReleaseArtifact(context, runner);
    await generateReleaseAppcast(context, fixture.dependencies, runner);
    await uploadReleaseArtifacts(context, fixture.dependencies, runner);

    expect(receivedSignals).toHaveLength(5);
    expect(receivedSignals.every((signal) => signal === controller.signal))
      .toBe(true);
  });

  it('seeds and stamps the appcast with the frozen release HEAD', async () => {
    const fixture = releaseFixture();
    const context = fixtureContext(fixture);
    writeFileSync(context.dmgPath, 'signed dmg');
    fixture.dependencies.head = () => 'b'.repeat(40);
    let seededAppcast = '';

    await generateReleaseAppcast(
      context,
      fixture.dependencies,
      async (_cmd, args) => {
        expect(args).toEqual([
          '--download-url-prefix',
          'https://dao-release.msgbyte.com/',
          '--maximum-versions',
          '0',
          path.join(fixture.root, 'dist'),
        ]);
        seededAppcast = readFileSync(context.appcastPath, 'utf-8');
        writeFileSync(
          context.appcastPath,
          '<rss><channel><item><enclosure url="' +
            path.basename(context.dmgPath) +
            '" /></item></channel></rss>\n',
        );
        return 0;
      },
    );

    expect(seededAppcast).toBe('<rss><channel></channel></rss>\n');
    expect(readFileSync(context.appcastPath, 'utf-8')).toContain(
      '<dao:gitCommit>' + context.releaseHead + '</dao:gitCommit>',
    );
  });

  it('uploads only current-candidate deltas with preserved arguments',
     async () => {
    const fixture = releaseFixture();
    const context = fixtureContext(fixture, {
      bucket: 'releases',
      prefix: 'stable',
    });
    const distDir = path.join(fixture.root, 'dist');
    writeFileSync(context.dmgPath, 'signed dmg');
    writeFileSync(path.join(distDir, 'unchanged.delta'), 'old delta');

    await generateReleaseAppcast(
      context,
      fixture.dependencies,
      async () => {
        writeFileSync(path.join(distDir, 'new.delta'), 'new delta');
        writeFileSync(path.join(distDir, 'orphan.delta'), 'orphan delta');
        writeFileSync(path.join(distDir, 'historical.delta'), 'historical');
        writeFileSync(context.appcastPath, `<rss><channel>
          <item>
            <enclosure url="historical.dmg" />
            <enclosure url="historical.delta" />
          </item>
          <item>
            <enclosure url="${path.basename(context.dmgPath)}" />
            <enclosure url="unchanged.delta" />
            <enclosure url="new.delta" />
          </item>
        </channel></rss>\n`);
        return 0;
      },
    );

    const calls: Array<{cmd: string; args: string[]}> = [];
    await uploadReleaseArtifacts(
      context,
      fixture.dependencies,
      async (cmd, args) => {
        calls.push({cmd, args});
        return 0;
      },
    );

    expect(calls).toEqual([{
      cmd: 'npx',
      args: [
        'tsx', 'scripts/cli.ts', 'upload', context.dmgPath,
        path.join(distDir, 'new.delta'),
        path.join(distDir, 'unchanged.delta'),
        '--bucket', 'releases', '--prefix', 'stable',
      ],
    }]);
  });

  it('retries every current-candidate delta after a partial upload failure',
     async () => {
       const fixture = releaseFixture();
       const context = fixtureContext(fixture, {bucket: 'releases'});
       const distDir = path.join(fixture.root, 'dist');
       const retryDelta = path.join(distDir, 'retry.delta');
       writeFileSync(context.dmgPath, 'signed dmg');

       const generate = async () => {
         writeFileSync(retryDelta, 'retry delta');
         writeFileSync(path.join(distDir, 'historical.delta'), 'historical');
         writeFileSync(path.join(distDir, 'orphan.delta'), 'orphan');
         writeFileSync(context.appcastPath, `<rss><channel>
           <item>
             <enclosure url="old.dmg" />
             <enclosure url="historical.delta" />
           </item>
           <item>
             <enclosure url="${path.basename(context.dmgPath)}" />
             <enclosure url="retry.delta" />
           </item>
         </channel></rss>\n`);
         return 0;
       };

       await generateReleaseAppcast(context, fixture.dependencies, generate);
       await expect(uploadReleaseArtifacts(
         context,
         fixture.dependencies,
         async () => 1,
       )).rejects.toMatchObject({phase: 'upload'});

       const retryMtime = readFileSync(retryDelta);
       await generateReleaseAppcast(context, fixture.dependencies, async () => {
         writeFileSync(context.appcastPath, `<rss><channel>
           <item>
             <enclosure url="old.dmg" />
             <enclosure url="historical.delta" />
           </item>
           <item>
             <enclosure url="${path.basename(context.dmgPath)}" />
             <enclosure url="retry.delta" />
           </item>
         </channel></rss>\n`);
         return 0;
       });
       expect(readFileSync(retryDelta)).toEqual(retryMtime);

       const calls: Array<{cmd: string; args: string[]}> = [];
       await uploadReleaseArtifacts(
         context,
         fixture.dependencies,
         async (cmd, args) => {
           calls.push({cmd, args});
           return 0;
         },
       );

       expect(calls).toEqual([{
         cmd: 'npx',
         args: [
           'tsx', 'scripts/cli.ts', 'upload', context.dmgPath,
           retryDelta,
           '--bucket', 'releases',
         ],
       }]);
     });
});

describe('release orchestration', () => {
  it('waits for the process group to close before rollback settles',
     async () => {
       const fixture = releaseFixture();
       const processDir = path.join(fixture.root, 'process-fixture');
       mkdirSync(processDir);
       const childScript = path.join(processDir, 'child.cjs');
       const parentScript = path.join(processDir, 'parent.cjs');
       const readyPath = path.join(processDir, 'ready');
       const markerPath = path.join(processDir, 'late-write');
       const parentPidPath = path.join(processDir, 'parent-pid');
       const childPidPath = path.join(processDir, 'child-pid');
       writeFileSync(childScript, [
         "const fs = require('node:fs');",
         'setTimeout(() => fs.writeFileSync(process.argv[2], "late"), 250);',
       ].join('\n'));
       writeFileSync(parentScript, [
         "const fs = require('node:fs');",
         "const {spawn} = require('node:child_process');",
         'const child = spawn(process.execPath, [process.argv[2], ' +
           'process.argv[3]], {stdio: "ignore"});',
         'fs.writeFileSync(process.argv[4], String(process.pid));',
         'fs.writeFileSync(process.argv[5], String(child.pid));',
         'fs.writeFileSync(process.argv[6], "ready");',
         'setInterval(() => {}, 1000);',
       ].join('\n'));
       const controller = new AbortController();
       fixture.dependencies.signal = controller.signal;
       fixture.dependencies.runPhase = async (phase, context) => {
         fixture.phases.push(phase);
         if (phase === 'build') {
           await runStreaming(process.execPath, [
             parentScript,
             childScript,
             markerPath,
             parentPidPath,
             childPidPath,
             readyPath,
           ], {signal: context.signal});
         }
       };
       const release = runRelease({}, fixture.dependencies);

       try {
         await waitForFile(readyPath);
         controller.abort(new Error('Release interrupted by caller.'));
         await expect(release).rejects.toMatchObject({
           phase: 'build',
           message: 'Release interrupted by caller.',
         });
         expect(readFileSync(fixture.daoPath, 'utf-8')).toContain('1.0.70');
         await new Promise((resolve) => setTimeout(resolve, 400));
         expect(existsSync(markerPath)).toBe(false);
       } finally {
         for (const pidPath of [parentPidPath, childPidPath]) {
           if (!existsSync(pidPath)) continue;
           try {
             process.kill(Number(readFileSync(pidPath, 'utf-8')), 'SIGKILL');
           } catch {
             // The process was already terminated by the release abort.
           }
         }
       }
     });

  it('waits for the whole group after the leader exits on TERM', async () => {
    const fixture = releaseFixture();
    const processDir = path.join(fixture.root, 'leader-exit-fixture');
    mkdirSync(processDir);
    const descendantScript = path.join(processDir, 'descendant.cjs');
    const leaderScript = path.join(processDir, 'leader.cjs');
    const readyPath = path.join(processDir, 'ready');
    const leaderTermPath = path.join(processDir, 'leader-term');
    const descendantTermPath = path.join(processDir, 'descendant-term');
    const latePath = path.join(processDir, 'late-write');
    const descendantReadyPath = path.join(processDir, 'descendant-ready');
    const descendantPidPath = path.join(processDir, 'descendant-pid');
    writeFileSync(descendantScript, [
      "const fs = require('node:fs');",
      'process.on("SIGTERM", () => {',
      '  fs.writeFileSync(process.argv[2], "received");',
      '});',
      'setTimeout(() => fs.writeFileSync(process.argv[3], "late"), 600);',
      'fs.writeFileSync(process.argv[4], "ready");',
      'setInterval(() => {}, 1000);',
    ].join('\n'));
    writeFileSync(leaderScript, [
      "const fs = require('node:fs');",
      "const {spawn} = require('node:child_process');",
      'const descendant = spawn(process.execPath, [process.argv[2], ' +
        'process.argv[3], process.argv[4], process.argv[5]], ' +
        '{stdio: "ignore"});',
      'fs.writeFileSync(process.argv[6], String(descendant.pid));',
      'process.on("SIGTERM", () => {',
      '  fs.writeFileSync(process.argv[7], "received");',
      '  process.exit(0);',
      '});',
      'fs.writeFileSync(process.argv[8], "ready");',
      'setInterval(() => {}, 1000);',
    ].join('\n'));
    const controller = new AbortController();
    fixture.dependencies.signal = controller.signal;
    fixture.dependencies.runPhase = async (phase, context) => {
      fixture.phases.push(phase);
      if (phase === 'build') {
        await runStreaming(process.execPath, [
          leaderScript,
          descendantScript,
          descendantTermPath,
          latePath,
          descendantReadyPath,
          descendantPidPath,
          leaderTermPath,
          readyPath,
        ], {signal: context.signal});
      }
    };
    const release = runRelease({}, fixture.dependencies);
    let settled = false;
    void release.then(
      () => { settled = true; },
      () => { settled = true; },
    );

    try {
      await waitForFile(readyPath);
      await waitForFile(descendantReadyPath);
      controller.abort(new Error('Release interrupted by caller.'));
      await waitForFile(leaderTermPath);
      expect(settled).toBe(false);
      await expect(release).rejects.toMatchObject({
        phase: 'build',
        message: 'Release interrupted by caller.',
      });
      expect(readFileSync(descendantTermPath, 'utf-8')).toBe('received');
      expect(readFileSync(fixture.daoPath, 'utf-8')).toContain('1.0.70');
      await new Promise((resolve) => setTimeout(resolve, 650));
      expect(existsSync(latePath)).toBe(false);
    } finally {
      if (existsSync(descendantPidPath)) {
        try {
          process.kill(
            Number(readFileSync(descendantPidPath, 'utf-8')),
            'SIGKILL',
          );
        } catch {
          // Group escalation already terminated the descendant.
        }
      }
    }
  });

  it('rolls back when the active phase is aborted', async () => {
    const fixture = releaseFixture();
    const original = {
      dao: readFileSync(fixture.daoPath),
      appcast: readFileSync(fixture.appcastPath),
      info: readFileSync(fixture.infoPath),
    };
    const controller = new AbortController();
    fixture.dependencies.signal = controller.signal;
    fixture.dependencies.runPhase = async (phase) => {
      fixture.phases.push(phase);
      if (phase === 'build') {
        controller.abort(new Error('Release interrupted by SIGINT.'));
        throw controller.signal.reason;
      }
    };

    await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
      phase: 'build',
      message: 'Release interrupted by SIGINT.',
    });
    expect(readFileSync(fixture.daoPath)).toEqual(original.dao);
    expect(readFileSync(fixture.appcastPath)).toEqual(original.appcast);
    expect(readFileSync(fixture.infoPath)).toEqual(original.info);
  });

  it.each([
    ['SIGINT', 'Release interrupted by SIGINT.'],
    ['SIGTERM', 'Release interrupted by SIGTERM.'],
  ] as const)('scopes and removes the %s release handler', async (
    signalName,
    expectedMessage,
  ) => {
    const fixture = releaseFixture();
    const listenersBefore = process.listeners(signalName);
    let notifyPhaseStarted: (() => void) | undefined;
    const phaseStarted = new Promise<void>((resolve) => {
      notifyPhaseStarted = resolve;
    });
    fixture.dependencies.runPhase = async (phase, context) => {
      fixture.phases.push(phase);
      if (phase !== 'build') return;
      notifyPhaseStarted?.();
      await new Promise<void>((_resolve, reject) => {
        context.signal?.addEventListener(
          'abort',
          () => reject(context.signal?.reason),
          {once: true},
        );
      });
    };

    const release = runReleaseWithSignals({}, fixture.dependencies);
    await phaseStarted;
    const releaseHandler = process.listeners(signalName).find(
      (listener) => !listenersBefore.includes(listener),
    );
    expect(releaseHandler).toBeDefined();
    releaseHandler?.(signalName);

    await expect(release).rejects.toMatchObject({
      phase: 'build',
      message: expectedMessage,
    });
    expect(process.listeners(signalName)).toEqual(listenersBefore);
    expect(readFileSync(fixture.daoPath, 'utf-8')).toContain('1.0.70');
  });

  it('composes a pre-aborted caller signal with scoped release signals',
     async () => {
       const fixture = releaseFixture();
       const originalDao = readFileSync(fixture.daoPath);
       const controller = new AbortController();
       controller.abort(new Error('Release interrupted by caller.'));
       fixture.dependencies.signal = controller.signal;

       await expect(runReleaseWithSignals({}, fixture.dependencies))
         .rejects.toMatchObject({
           phase: 'version',
           message: 'Release interrupted by caller.',
         });
       expect(readFileSync(fixture.daoPath)).toEqual(originalDao);
       expect(fixture.phases).toEqual([]);
     });

  it('rolls back a tag created at the final interrupt boundary', async () => {
    const fixture = releaseFixture();
    const controller = new AbortController();
    fixture.dependencies.signal = controller.signal;
    fixture.dependencies.createTag = (tagName, commit) => {
      fixture.createdTags.push(tagName);
      fixture.createdTagTargets.push(commit);
      setImmediate(() => {
        controller.abort(new Error('Release interrupted before commit.'));
      });
      return fixture.createdTagObjectId;
    };

    await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
      phase: 'tag',
      message: 'Release interrupted before commit.',
    });
    expect(readFileSync(fixture.daoPath, 'utf-8')).toContain('1.0.70');
    expect(fixture.createdTags).toEqual(['v1.0.71']);
    expect(fixture.deletedTags).toEqual([{
      tagName: 'v1.0.71',
      expectedObjectId: fixture.createdTagObjectId,
    }]);
  });

  it('formats a clean rollback with accurate retry guidance', async () => {
    const fixture = releaseFixture();
    fixture.failPhase = 'build';
    const failure = await runRelease({}, fixture.dependencies)
      .catch((cause: unknown) => cause) as ReleaseError;

    const output = formatReleaseFailure(failure);

    expect(output).toContain('Release failed during build');
    expect(output).toContain('Restored canonical release metadata to 1.0.70');
    expect(output).toContain(
      'Any generated files in dist/ and any Apple notarization or R2 ' +
        'side effects were left in place and were not rolled back',
    );
    expect(output).toContain(
      'Run npm run release normally to retry candidate 1.0.71',
    );
  });

  it('prints notarization recovery only after a clean rollback is known',
     async () => {
       const fixture = releaseFixture();
       fixture.dependencies.runPhase = async (phase) => {
         fixture.phases.push(phase);
         if (phase !== 'notarize') return;
         const failure = new ReleaseError('notarize', 'notarytool failed');
         Object.assign(failure, {
           notarizationRecovery: {
             command: 'SAFE_NOTARIZATION_RECOVERY_COMMAND',
             detail: 'notarytool recovery detail',
           },
         });
         throw failure;
       };

       const failure = await runRelease({}, fixture.dependencies)
         .catch((cause: unknown) => cause) as ReleaseError;
       const output = formatReleaseFailure(failure);

       const rollbackIndex = output.indexOf(
         'Restored canonical release metadata to 1.0.70',
       );
       const commandIndex = output.indexOf(
         'SAFE_NOTARIZATION_RECOVERY_COMMAND',
       );
       expect(rollbackIndex).toBeGreaterThanOrEqual(0);
       expect(commandIndex).toBeGreaterThan(rollbackIndex);
     });

  it('withholds notarization recovery when rollback is incomplete',
     async () => {
       const fixture = releaseFixture();
       fixture.dependencies.runPhase = async (phase) => {
         fixture.phases.push(phase);
         if (phase !== 'notarize') return;
         writeFileSync(fixture.daoPath, '{"display":"user-edit"}\n');
         const failure = new ReleaseError('notarize', 'notarytool failed');
         Object.assign(failure, {
           notarizationRecovery: {
             command: 'UNSAFE_NOTARIZATION_RECOVERY_COMMAND',
             detail: 'notarytool recovery detail',
           },
         });
         throw failure;
       };

       const failure = await runRelease({}, fixture.dependencies)
         .catch((cause: unknown) => cause) as ReleaseError;
       const output = formatReleaseFailure(failure);

       expect(output).not.toContain('UNSAFE_NOTARIZATION_RECOVERY_COMMAND');
       expect(output).toContain(
         'Inspect every listed file and tag before attempting manual recovery',
       );
     });

  it('preserves termination uncertainty and blocks normal retry guidance',
     async () => {
       const fixture = releaseFixture();
       const controller = new AbortController();
       fixture.dependencies.signal = controller.signal;
       fixture.dependencies.runPhase = async (phase) => {
         fixture.phases.push(phase);
         if (phase === 'build') {
           controller.abort(new Error('Release interrupted by caller.'));
           throw new ProcessTerminationError(321, 321);
         }
       };

       const failure = await runRelease({}, fixture.dependencies)
         .catch((cause: unknown) => cause) as ReleaseError;
       const output = formatReleaseFailure(failure);

       expect(failure).toMatchObject({
         phase: 'build',
         message: expect.stringContaining('process group 321'),
         recovery: {
           terminationUncertain: {
             processId: 321,
             processGroupId: 321,
           },
         },
       });
       expect(output).toContain('may still be running');
       expect(output).toContain('PID 321');
       expect(output).toContain('PGID 321');
       expect(output).toContain(
         'Confirm and terminate the old release process group before retrying',
       );
       expect(output).toContain(
         'metadata rollback completed, but the old process may write again',
       );
       expect(output).not.toContain('Run npm run release normally');
       expect(readFileSync(fixture.daoPath, 'utf-8')).toContain('1.0.70');
     });

  it('formats rollback conflicts without claiming complete restoration',
     async () => {
       const fixture = releaseFixture();
       fixture.dependencies.runPhase = async (phase) => {
         fixture.phases.push(phase);
         if (phase === 'build') {
           writeFileSync(fixture.daoPath, '{"concurrent":true}\n');
           throw new ReleaseError('build', 'build failed');
         }
       };
       const failure = await runRelease({}, fixture.dependencies)
         .catch((cause: unknown) => cause) as ReleaseError;

       const output = formatReleaseFailure(failure);

       expect(output).toContain('Release failed during build');
       expect(output).toContain('Canonical release metadata was not fully restored');
       expect(output).toContain(fixture.daoPath);
       expect(output).not.toContain('Restored canonical release metadata');
       expect(output.match(/Release failed during build/g)).toHaveLength(1);
     });

  it('resumes a manually stapled artifact without skip-bump', () => {
    const command = formatNotarizeRecoveryCommand(
      'dist/dao-browser-1.0.71-mac-arm64.dmg',
      'dao-notary',
    );
    expect(command).toContain('npm run release -- --resume-from-staple');
    expect(command).not.toContain('--skip-bump');
  });

  it('preserves release options in manual notarization recovery', () => {
    const command = formatNotarizeRecoveryCommand(
      'dist/dao-browser-1.1.0-mac-arm64.dmg',
      'dao-notary',
      {
        bump: 'minor',
        bucket: 'release bucket',
        prefix: "candidate's",
        skipUpload: true,
      },
    );

    expect(command).toContain(
      "npm run release -- --bump minor --bucket 'release bucket' " +
        "--prefix 'candidate'\\''s' --skip-upload --resume-from-staple",
    );
  });

  it.each([
    'import', 'build', 'package', 'notarize', 'staple',
    'appcast', 'metadata', 'upload', 'tag',
  ] satisfies ReleasePhase[])('rolls back after %s failure', async (phase) => {
    const fixture = releaseFixture();
    const original = {
      dao: readFileSync(fixture.daoPath),
      appcast: readFileSync(fixture.appcastPath),
      info: readFileSync(fixture.infoPath),
    };
    fixture.failPhase = phase;

    await expect(runRelease({}, fixture.dependencies))
      .rejects.toMatchObject({phase});

    expect(readFileSync(fixture.daoPath)).toEqual(original.dao);
    expect(readFileSync(fixture.appcastPath)).toEqual(original.appcast);
    expect(readFileSync(fixture.infoPath)).toEqual(original.info);
    expect(fixture.createdTags).toEqual([]);
    expect(fixture.deletedTags).toEqual([]);
  });

  it('retries the same candidate after rollback', async () => {
    const fixture = releaseFixture();
    fixture.failPhase = 'build';
    await expect(runRelease({}, fixture.dependencies))
      .rejects.toMatchObject({phase: 'build'});
    fixture.failPhase = null;
    expect((await runRelease({}, fixture.dependencies)).newVersion)
      .toBe('1.0.71');
  });

  it('preserves release options in clean rollback retry guidance', async () => {
    const fixture = releaseFixture();
    fixture.failPhase = 'appcast';
    const failure = await runRelease({
      bump: 'minor',
      bucket: 'release bucket',
      prefix: "candidate's",
      skipUpload: true,
      skipBuild: true,
      forceImport: false,
    }, fixture.dependencies).catch((cause: unknown) => cause) as ReleaseError;

    expect(formatReleaseFailure(failure)).toContain(
      "npm run release -- --bump minor --bucket 'release bucket' " +
        "--prefix 'candidate'\\''s' --skip-upload --skip-build " +
        '--no-force-import',
    );
  });

  it('keeps metadata and creates a tag only after success', async () => {
    const fixture = releaseFixture();
    const result = await runRelease({}, fixture.dependencies);
    expect(result).toEqual({oldVersion: '1.0.70', newVersion: '1.0.71'});
    expect(readFileSync(fixture.daoPath, 'utf-8')).toContain('1.0.71');
    expect(readFileSync(fixture.infoPath, 'utf-8')).toContain('1.0.71');
    expect(fixture.createdTags).toEqual(['v1.0.71']);
    expect(fixture.createdTagTargets).toEqual(['a'.repeat(40)]);
  });

  it('types missing and malformed dao.json as preflight failures', async () => {
    const missing = releaseFixture();
    unlinkSync(missing.daoPath);
    await expect(runRelease({}, missing.dependencies)).rejects.toMatchObject({
      phase: 'preflight',
    });
    expect(missing.phases).toEqual([]);

    const malformed = releaseFixture();
    writeFileSync(malformed.daoPath, '{not json}\n');
    await expect(runRelease({}, malformed.dependencies)).rejects.toMatchObject({
      phase: 'preflight',
    });
    expect(malformed.phases).toEqual([]);
  });

  it('types an invalid display version as a version failure', async () => {
    const fixture = releaseFixture();
    const config = JSON.parse(readFileSync(fixture.daoPath, 'utf-8'));
    config.version.display = 'invalid';
    writeFileSync(fixture.daoPath, JSON.stringify(config) + '\n');

    await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
      phase: 'version',
    });
    expect(fixture.phases).toEqual([]);
  });

  it('types a preflight dependency exception without claiming rollback',
     async () => {
       const fixture = releaseFixture();
       fixture.dependencies.head = () => {
         throw new Error('git unavailable');
       };

       await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
         phase: 'preflight',
       });
       expect(fixture.phases).toEqual([]);
     });

  it('rejects an invalid frozen HEAD before mutation', async () => {
    const fixture = releaseFixture();
    const originalDao = readFileSync(fixture.daoPath);
    fixture.dependencies.head = () => 'not-a-commit';

    await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
      phase: 'preflight',
    });
    expect(readFileSync(fixture.daoPath)).toEqual(originalDao);
    expect(fixture.phases).toEqual([]);
  });

  it('types transaction snapshot initialization errors as preflight',
     async () => {
       const fixture = releaseFixture();
       unlinkSync(fixture.infoPath);
       mkdirSync(fixture.infoPath);

       await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
         phase: 'preflight',
       });
       expect(fixture.phases).toEqual([]);
     });

  it('snapshots dao.json before candidate calculation and preflight races',
     async () => {
       const fixture = releaseFixture();
       let headReads = 0;
       fixture.dependencies.head = () => {
         if (headReads++ === 0) {
           writeFileSync(fixture.daoPath, '{"display":"user-edit"}\n');
         }
         return 'a'.repeat(40);
       };

       await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
         phase: 'rollback',
         recovery: {conflicts: [fixture.daoPath]},
       });
       expect(readFileSync(fixture.daoPath, 'utf-8'))
         .toBe('{"display":"user-edit"}\n');
       expect(fixture.phases).toEqual([]);
       expect(fixture.createdTags).toEqual([]);
     });

  it('requires Sparkle generate_appcast during skip-build preflight',
     async () => {
       const fixture = releaseFixture();
       unlinkSync(path.join(
         fixture.root,
         'third_party/sparkle/bin/generate_appcast',
       ));

       await expect(runRelease(
         {skipBuild: true, skipUpload: true},
         fixture.dependencies,
       )).rejects.toMatchObject({
         phase: 'preflight',
         message: expect.stringContaining('generate_appcast not found'),
       });
       expect(fixture.phases).toEqual([]);
       expect(readFileSync(fixture.daoPath, 'utf-8')).toContain('1.0.70');
     });

  it('validates an appcast seed before any mutation or phase', async () => {
    const fixture = releaseFixture();
    const originalDao = readFileSync(fixture.daoPath);
    unlinkSync(fixture.appcastPath);
    unlinkSync(path.join(fixture.root, 'branding/appcast.template.xml'));

    await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
      phase: 'preflight',
    });
    expect(readFileSync(fixture.daoPath)).toEqual(originalDao);
    expect(fixture.phases).toEqual([]);
    expect(fixture.createdTags).toEqual([]);
  });

  it('does not mutate when the candidate tag conflicts during preflight',
     async () => {
       const fixture = releaseFixture();
       const originalDao = readFileSync(fixture.daoPath);
       fixture.dependencies.tagState = () => ({
         exists: true,
         commit: 'c'.repeat(40),
       });

       await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
         phase: 'preflight',
       });
       expect(readFileSync(fixture.daoPath)).toEqual(originalDao);
       expect(fixture.phases).toEqual([]);
       expect(fixture.createdTags).toEqual([]);
     });

  it('rolls back when HEAD drifts before final tag creation', async () => {
    const fixture = releaseFixture();
    const original = {
      dao: readFileSync(fixture.daoPath),
      appcast: readFileSync(fixture.appcastPath),
      info: readFileSync(fixture.infoPath),
    };
    let headReads = 0;
    fixture.dependencies.head = () =>
      headReads++ === 0 ? 'a'.repeat(40) : 'b'.repeat(40);

    await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
      phase: 'tag',
    });
    expect(readFileSync(fixture.daoPath)).toEqual(original.dao);
    expect(readFileSync(fixture.appcastPath)).toEqual(original.appcast);
    expect(readFileSync(fixture.infoPath)).toEqual(original.info);
    expect(fixture.createdTags).toEqual([]);
    expect(fixture.deletedTags).toEqual([]);
  });

  it('rolls back when tag state changes before final tag creation', async () => {
    const fixture = releaseFixture();
    let tagReads = 0;
    fixture.dependencies.tagState = () => tagReads++ === 0
      ? {exists: false}
      : {exists: true, commit: 'a'.repeat(40)};

    await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
      phase: 'tag',
    });
    expect(readFileSync(fixture.daoPath, 'utf-8')).toContain('1.0.70');
    expect(fixture.createdTags).toEqual([]);
    expect(fixture.deletedTags).toEqual([]);
  });

  it('rolls back exact metadata when createTag throws', async () => {
    const fixture = releaseFixture();
    const original = {
      dao: readFileSync(fixture.daoPath),
      appcast: readFileSync(fixture.appcastPath),
      info: readFileSync(fixture.infoPath),
    };
    fixture.dependencies.createTag = () => {
      throw new Error('tag creation failed');
    };

    await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
      phase: 'tag',
    });
    expect(readFileSync(fixture.daoPath)).toEqual(original.dao);
    expect(readFileSync(fixture.appcastPath)).toEqual(original.appcast);
    expect(readFileSync(fixture.infoPath)).toEqual(original.info);
    expect(fixture.deletedTags).toEqual([]);
  });

  it('deletes its tag when commit detects a concurrent canonical edit',
     async () => {
       const fixture = releaseFixture();
       const originalAppcast = readFileSync(fixture.appcastPath);
       const originalInfo = readFileSync(fixture.infoPath);
       fixture.dependencies.createTag = (tagName, commit) => {
         fixture.createdTags.push(tagName);
         fixture.createdTagTargets.push(commit);
         writeFileSync(fixture.daoPath, '{"concurrent":true}\n');
         return fixture.createdTagObjectId;
       };

       await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
         phase: 'rollback',
       });
       expect(readFileSync(fixture.daoPath, 'utf-8'))
         .toBe('{"concurrent":true}\n');
       expect(readFileSync(fixture.appcastPath)).toEqual(originalAppcast);
       expect(readFileSync(fixture.infoPath)).toEqual(originalInfo);
       expect(fixture.createdTags).toEqual(['v1.0.71']);
       expect(fixture.createdTagTargets).toEqual(['a'.repeat(40)]);
       expect(fixture.deletedTags).toEqual([{
         tagName: 'v1.0.71',
         expectedObjectId: fixture.createdTagObjectId,
       }]);
     });

  it('reports tag cleanup failure separately from restored file progress',
     async () => {
       const fixture = releaseFixture();
       fixture.dependencies.createTag = (tagName, commit) => {
         fixture.createdTags.push(tagName);
         fixture.createdTagTargets.push(commit);
         writeFileSync(fixture.daoPath, '{"concurrent":true}\n');
         return fixture.createdTagObjectId;
       };
       fixture.dependencies.deleteTag = () => {
         throw new Error('delete tag failed');
       };

       const failure = await runRelease({}, fixture.dependencies)
         .catch((cause: unknown) => cause) as ReleaseError;

       expect(failure).toMatchObject({
         name: 'ReleaseError',
         phase: 'rollback',
         recovery: {
           failedPhase: 'tag',
           conflicts: [fixture.daoPath],
           restoreFailures: [],
           tagCleanupFailure: {
             tagName: 'v1.0.71',
             expectedObjectId: fixture.createdTagObjectId,
             message: 'delete tag failed',
           },
         },
       });
       const output = formatReleaseFailure(failure);
       expect(output).toContain('Tag cleanup failed for v1.0.71');
       expect(output).toContain('delete tag failed');
       expect(output).not.toContain('Failed to restore ' + fixture.daoPath);
     });

  it('confirms metadata restoration for a pure tag cleanup failure',
     async () => {
       const fixture = releaseFixture();
       const controller = new AbortController();
       fixture.dependencies.signal = controller.signal;
       fixture.dependencies.createTag = (tagName, commit) => {
         fixture.createdTags.push(tagName);
         fixture.createdTagTargets.push(commit);
         setImmediate(() => controller.abort(
           new Error('Release interrupted before commit.'),
         ));
         return fixture.createdTagObjectId;
       };
       fixture.dependencies.deleteTag = () => {
         throw new Error('delete tag failed');
       };

       const failure = await runRelease({}, fixture.dependencies)
         .catch((cause: unknown) => cause) as ReleaseError;
       const output = formatReleaseFailure(failure);

       expect(failure.recovery).toMatchObject({
         conflicts: [],
         restoreFailures: [],
         tagCleanupFailure: {
           tagName: 'v1.0.71',
           expectedObjectId: fixture.createdTagObjectId,
           message: 'delete tag failed',
         },
       });
       expect(output).toContain(
         'Restored canonical release metadata to 1.0.70',
       );
       expect(output).toContain('Manually inspect release tag v1.0.71');
       expect(output).not.toContain(
         'Canonical release metadata was not fully restored',
       );
     });

  it('reports managed-file restore failures with the exact path',
     async () => {
       const fixture = releaseFixture();
       fixture.failPhase = 'build';
       fixture.dependencies.createTransaction = (paths) =>
         new ReleaseTransaction(paths, {
           writeTemporaryFile: (tempPath, contents) => {
             if (contents.includes(Buffer.from('1.0.70'))) {
               throw new Error('restore dao.json failed');
             }
             writeFileSync(tempPath, contents);
           },
           replaceFile: renameSync,
           removeFile: unlinkSync,
         });

       const failure = await runRelease({}, fixture.dependencies)
         .catch((cause: unknown) => cause) as ReleaseError;

       expect(failure).toMatchObject({
         name: 'ReleaseError',
         phase: 'rollback',
         recovery: {
           failedPhase: 'build',
           conflicts: [],
           restoreFailures: [{
             path: fixture.daoPath,
             message: 'restore dao.json failed',
           }],
           tagCleanupFailure: null,
         },
       });
       const output = formatReleaseFailure(failure);
       expect(output).toContain('Failed to restore ' + fixture.daoPath);
       expect(output).toContain('restore dao.json failed');
       expect(output).not.toContain('Tag cleanup failed');
     });

  it('never creates or deletes a same-HEAD pre-existing tag on success',
     async () => {
       const fixture = releaseFixture();
       fixture.dependencies.tagState = () => ({
         exists: true,
         commit: 'a'.repeat(40),
       });

       await expect(runRelease({}, fixture.dependencies)).resolves.toEqual({
         oldVersion: '1.0.70',
         newVersion: '1.0.71',
       });
       expect(fixture.createdTags).toEqual([]);
       expect(fixture.deletedTags).toEqual([]);
     });

  it('never deletes a matching tag that existed before the release',
     async () => {
       const fixture = releaseFixture();
       fixture.dependencies.tagState = () => ({
         exists: true,
         commit: 'a'.repeat(40),
       });
       fixture.failPhase = 'upload';

       await expect(runRelease({}, fixture.dependencies))
         .rejects.toMatchObject({phase: 'upload'});

       expect(fixture.createdTags).toEqual([]);
       expect(fixture.deletedTags).toEqual([]);
     });
});
