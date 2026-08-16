# Release Transaction and Failure Rollback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every controlled `npm run release` failure restore canonical release metadata so the next normal attempt retries the same candidate version.

**Architecture:** Add a focused `ReleaseTransaction` that snapshots and safely restores release-owned files, then wrap the existing release pipeline in a testable `runRelease()` function. Convert nested exits to typed errors, delay tag creation until the pipeline succeeds, and route normal interrupts through the same rollback path while retaining generated artifacts and remote intermediates.

**Tech Stack:** TypeScript, Node.js filesystem and child-process APIs, Commander, Vitest, Git, Sparkle, Cloudflare R2.

## Global Constraints

- Work in the primary checkout on `main`; do not create a branch or worktree.
- Do not edit `engine/` directly or run direct Chromium build tools.
- Do not invoke the real `npm run release` during verification.
- Preserve pre-existing staged and unstaged changes byte-for-byte.
- Never use `git restore`, `git checkout`, or `git reset` for rollback.
- Keep generated files under `engine/` and `dist/` after failure.
- Do not roll back Apple notarization submissions or R2 objects.
- Keep code, comments, tests, documentation, and commit messages in English.
- Do not update `docs/features.md` or `docs/feature-checklist.md`; this is release tooling, not a browser feature.
- Run state-changing Git commands only when the latest user message explicitly authorizes them. Commit steps below are conditional; otherwise skip them and report the suggested commit.

## File Map

- Create `scripts/commands/release-transaction.ts` for snapshot, mutation ownership, concurrent-edit detection, commit, and rollback.
- Create `scripts/commands/__tests__/release-transaction.test.ts` for isolated transaction tests.
- Modify `scripts/commands/release.ts` for typed orchestration, rollback, delayed tags, and recovery output.
- Modify `scripts/commands/__tests__/release.test.ts` for phase failure, retry, tag, resume, and interrupt coverage.
- Modify `scripts/utils.ts` to add optional `AbortSignal` support to `runStreaming()`.
- Modify `docs/release-signing.md` to document rollback and retry behavior.

---

### Task 1: Exact-Byte Release Transaction

**Files:**
- Create: `scripts/commands/release-transaction.ts`
- Create: `scripts/commands/__tests__/release-transaction.test.ts`

**Interfaces:**
- Consumes: absolute managed file paths and a tag-deletion callback.
- Produces: `ReleaseTransaction`, `ReleaseRollbackResult`, and `ReleaseConcurrentEditError`.

- [ ] **Step 1: Write the failing transaction tests**

Create `scripts/commands/__tests__/release-transaction.test.ts` with these concrete cases:

```ts
import {existsSync, mkdtempSync, readFileSync, writeFileSync} from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {describe, expect, it, vi} from 'vitest';
import {ReleaseTransaction} from '../release-transaction.js';

function fixture() {
  const root = mkdtempSync(path.join(os.tmpdir(), 'dao-release-transaction-'));
  const dao = path.join(root, 'dao.json');
  const appcast = path.join(root, 'appcast.xml');
  writeFileSync(dao, '{"display":"1.0.70"}\n');
  return {dao, appcast};
}

describe('ReleaseTransaction', () => {
  it('restores exact bytes and removes a file created by the release', () => {
    const files = fixture();
    const transaction = new ReleaseTransaction([files.dao, files.appcast]);
    transaction.mutateFile(files.dao, () => {
      writeFileSync(files.dao, '{"display":"1.0.71"}\n');
    });
    transaction.mutateFile(files.appcast, () => {
      writeFileSync(files.appcast, '<rss>1.0.71</rss>\n');
    });

    expect(transaction.rollback(vi.fn())).toEqual({
      restored: [files.dao, files.appcast],
      conflicts: [],
    });
    expect(readFileSync(files.dao, 'utf-8')).toBe('{"display":"1.0.70"}\n');
    expect(existsSync(files.appcast)).toBe(false);
  });

  it('preserves and reports a concurrent edit', () => {
    const files = fixture();
    const transaction = new ReleaseTransaction([files.dao]);
    transaction.mutateFile(files.dao, () => {
      writeFileSync(files.dao, '{"display":"1.0.71"}\n');
    });
    writeFileSync(files.dao, '{"display":"user-edit"}\n');

    expect(transaction.rollback(vi.fn())).toEqual({
      restored: [],
      conflicts: [files.dao],
    });
    expect(readFileSync(files.dao, 'utf-8')).toBe('{"display":"user-edit"}\n');
  });

  it('deletes only a tag recorded as created by the transaction', () => {
    const files = fixture();
    const deleteTag = vi.fn();
    const transaction = new ReleaseTransaction([files.dao]);
    transaction.markTagCreated('v1.0.71');
    transaction.rollback(deleteTag);
    expect(deleteTag).toHaveBeenCalledWith('v1.0.71');
  });

  it('commit keeps transaction-owned contents', () => {
    const files = fixture();
    const transaction = new ReleaseTransaction([files.dao]);
    transaction.mutateFile(files.dao, () => {
      writeFileSync(files.dao, '{"display":"1.0.71"}\n');
    });
    transaction.commit();
    expect(readFileSync(files.dao, 'utf-8')).toBe('{"display":"1.0.71"}\n');
    expect(() => transaction.rollback(vi.fn())).toThrow(
      'Release transaction has already been committed.');
  });
});
```

- [ ] **Step 2: Run the test and observe the missing-module failure**

Run: `npm run test:webui -- scripts/commands/__tests__/release-transaction.test.ts`

Expected: FAIL because `../release-transaction.js` does not exist.

- [ ] **Step 3: Implement the transaction unit**

Create `scripts/commands/release-transaction.ts` with this exact public API:

```ts
import {existsSync, readFileSync, unlinkSync, writeFileSync} from 'node:fs';

interface FileState { existed: boolean; contents: Buffer | null; }

export interface ReleaseRollbackResult {
  restored: string[];
  conflicts: string[];
}

export class ReleaseConcurrentEditError extends Error {
  constructor(readonly filePath: string) {
    super('Managed release file changed concurrently: ' + filePath);
    this.name = 'ReleaseConcurrentEditError';
  }
}

function readState(filePath: string): FileState {
  return existsSync(filePath)
    ? {existed: true, contents: readFileSync(filePath)}
    : {existed: false, contents: null};
}

function equal(left: FileState, right: FileState): boolean {
  if (left.existed !== right.existed) return false;
  return !left.existed || left.contents!.equals(right.contents!);
}

export class ReleaseTransaction {
  private readonly original = new Map<string, FileState>();
  private readonly owned = new Map<string, FileState>();
  private createdTag: string | null = null;
  private state: 'active' | 'committed' | 'rolled-back' = 'active';

  constructor(paths: string[]) {
    for (const filePath of paths) {
      const state = readState(filePath);
      this.original.set(filePath, state);
      this.owned.set(filePath, state);
    }
  }

  mutateFile(filePath: string, mutation: () => void): void {
    this.assertActive();
    const expected = this.managed(filePath, this.owned);
    if (!equal(readState(filePath), expected)) {
      throw new ReleaseConcurrentEditError(filePath);
    }
    mutation();
    this.owned.set(filePath, readState(filePath));
  }

  markTagCreated(tagName: string): void {
    this.assertActive();
    this.createdTag = tagName;
  }

  commit(): void {
    this.assertActive();
    for (const [filePath, expected] of this.owned) {
      if (!equal(readState(filePath), expected)) {
        throw new ReleaseConcurrentEditError(filePath);
      }
    }
    this.state = 'committed';
  }

  rollback(deleteTag: (tagName: string) => void): ReleaseRollbackResult {
    if (this.state === 'committed') {
      throw new Error('Release transaction has already been committed.');
    }
    if (this.state === 'rolled-back') {
      throw new Error('Release transaction has already been rolled back.');
    }
    const restored: string[] = [];
    const conflicts: string[] = [];
    for (const [filePath, original] of this.original) {
      const expected = this.managed(filePath, this.owned);
      if (!equal(readState(filePath), expected)) {
        conflicts.push(filePath);
        continue;
      }
      if (equal(original, expected)) continue;
      if (original.existed) writeFileSync(filePath, original.contents!);
      else if (existsSync(filePath)) unlinkSync(filePath);
      restored.push(filePath);
    }
    if (this.createdTag) deleteTag(this.createdTag);
    this.state = 'rolled-back';
    return {restored, conflicts};
  }

  private assertActive(): void {
    if (this.state !== 'active') {
      throw new Error('Release transaction is already ' + this.state + '.');
    }
  }

  private managed(filePath: string, states: Map<string, FileState>): FileState {
    const state = states.get(filePath);
    if (!state) throw new Error('Unmanaged release file: ' + filePath);
    return state;
  }
}
```

- [ ] **Step 4: Run the transaction tests**

Run: `npm run test:webui -- scripts/commands/__tests__/release-transaction.test.ts`

Expected: PASS.

- [ ] **Step 5: Conditionally commit**

Only with explicit Git authorization:

```bash
git add scripts/commands/release-transaction.ts scripts/commands/__tests__/release-transaction.test.ts
git commit -m "feat(release): add rollback transaction"
```

Otherwise leave changes unstaged.

---

### Task 2: Typed Errors and Tag Preflight

**Files:**
- Modify: `scripts/commands/release.ts`
- Modify: `scripts/commands/__tests__/release.test.ts`

**Interfaces:**
- Consumes: existing release helpers.
- Produces: `ReleasePhase`, `ReleaseError`, `runReleaseStep()`, and `inspectReleaseTag()`.

- [ ] **Step 1: Write failing helper tests**

Add these imports and cases to `release.test.ts`:

```ts
import {execFileSync} from 'node:child_process';
import {
  inspectReleaseTag,
  runReleaseStep,
} from '../release.js';

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
  execFileSync('git', ['tag', '-a', 'v1.0.71', '-m', 'Release v1.0.71'], {cwd: root});
  const head = execFileSync('git', ['rev-parse', 'HEAD'], {
    cwd: root, encoding: 'utf-8',
  }).trim();
  expect(inspectReleaseTag(root, 'v1.0.71')).toEqual({exists: true, commit: head});
});
```

- [ ] **Step 2: Run tests and observe missing exports**

Run: `npm run test:webui -- scripts/commands/__tests__/release.test.ts`

Expected: FAIL because the new exports do not exist.

- [ ] **Step 3: Add the typed error model and throwing step runner**

```ts
export type ReleasePhase =
  | 'preflight' | 'version' | 'import' | 'build' | 'package'
  | 'notarize' | 'staple' | 'appcast' | 'metadata'
  | 'upload' | 'tag' | 'rollback';

export class ReleaseError extends Error {
  constructor(readonly phase: ReleasePhase, message: string, options?: ErrorOptions) {
    super(message, options);
    this.name = 'ReleaseError';
  }
}

export async function runReleaseStep(
  phase: ReleasePhase,
  dryRun: boolean | undefined,
  description: string,
  cmd: string,
  args: string[],
  runner: (cmd: string, args: string[]) => Promise<number> = runStreaming,
): Promise<void> {
  log(description);
  if (dryRun) {
    console.log('  [dry-run] ' + cmd + ' ' + args.join(' '));
    return;
  }
  const code = await runner(cmd, args);
  if (code !== 0) {
    throw new ReleaseError(
      phase,
      'Step failed (' + description + '): ' + cmd + ' exited with code ' + code,
    );
  }
}
```

Task 2 stops at the typed helper boundary. Task 3 replaces
orchestration-level `process.exit(1)` calls with `ReleaseError`, changes
transformation helpers such as `bumpVersion()` and `updateInfoJson()` to throw
ordinary `Error`, and wraps them with the active phase inside `runRelease()`.

- [ ] **Step 4: Add read-only annotated-tag inspection**

```ts
export interface ReleaseTagState { exists: boolean; commit?: string; }

export function inspectReleaseTag(rootDir: string, tagName: string): ReleaseTagState {
  const result = spawnSync(
    'git', ['rev-parse', '--verify', 'refs/tags/' + tagName + '^{}'],
    {cwd: rootDir, encoding: 'utf-8'},
  );
  if (result.status !== 0) return {exists: false};
  return {exists: true, commit: (result.stdout || '').trim()};
}
```

Task 3 wires this helper into preflight before mutation: it fails when the
peeled commit differs from `HEAD`, and treats a same-`HEAD` tag as pre-existing
and never rollback-owned.

- [ ] **Step 5: Run focused tests**

Run: `npm run test:webui -- scripts/commands/__tests__/release.test.ts scripts/commands/__tests__/release-transaction.test.ts`

Expected: PASS.

- [ ] **Step 6: Conditionally commit**

Only with explicit Git authorization:

```bash
git add scripts/commands/release.ts scripts/commands/__tests__/release.test.ts
git commit -m "refactor(release): use typed pipeline errors"
```

Otherwise leave changes unstaged.

---

### Task 3: Transactional Orchestration and Delayed Tagging

**Files:**
- Modify: `scripts/commands/release.ts`
- Modify: `scripts/commands/__tests__/release.test.ts`

**Interfaces:**
- Consumes: Task 1 transaction and Task 2 errors/tag state.
- Produces: `ReleaseDependencies`, `ReleasePhaseContext`, and `runRelease()`.

- [ ] **Step 1: Add a fake release fixture and failure matrix**

Add an isolated fixture that creates `dao.json`, public appcast, `info.json`,
`dist/`, and a fake Sparkle binary. Its dependency object records tags and
fails a selected phase without invoking external tools:

```ts
interface ReleaseFixture {
  root: string;
  daoPath: string;
  appcastPath: string;
  infoPath: string;
  createdTags: string[];
  deletedTags: string[];
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
      version: '148.0.7778.217',
      display: '1.0.70',
    },
    build: {target_os: 'mac', target_cpu: 'arm64'},
  }, null, 2) + '\n');
  writeFileSync(appcastPath, '<rss><channel></channel></rss>\n');
  writeFileSync(infoPath, JSON.stringify({
    version: '1.0.70',
    chromiumVersion: '148.0.7778.217',
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
    deletedTags: [] as string[],
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
    createTag: (tagName) => fixture.createdTags.push(tagName),
    deleteTag: (tagName) => fixture.deletedTags.push(tagName),
    runPhase: async (phase, context) => {
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
            path.basename(context.dmgPath) + '" /></item></channel></rss>\n',
        );
      }
    },
  };
  return fixture;
}

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

  await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({phase});

  expect(readFileSync(fixture.daoPath)).toEqual(original.dao);
  expect(readFileSync(fixture.appcastPath)).toEqual(original.appcast);
  expect(readFileSync(fixture.infoPath)).toEqual(original.info);
  expect(fixture.createdTags).toEqual([]);
  expect(fixture.deletedTags).toEqual([]);
});

it('retries the same candidate after rollback', async () => {
  const fixture = releaseFixture();
  fixture.failPhase = 'build';
  await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({phase: 'build'});
  fixture.failPhase = null;
  expect((await runRelease({}, fixture.dependencies)).newVersion).toBe('1.0.71');
});

it('keeps metadata and creates a tag only after success', async () => {
  const fixture = releaseFixture();
  const result = await runRelease({}, fixture.dependencies);
  expect(result).toEqual({oldVersion: '1.0.70', newVersion: '1.0.71'});
  expect(readFileSync(fixture.daoPath, 'utf-8')).toContain('1.0.71');
  expect(readFileSync(fixture.infoPath, 'utf-8')).toContain('1.0.71');
  expect(fixture.createdTags).toEqual(['v1.0.71']);
});

it('never deletes a matching tag that existed before the release', async () => {
  const fixture = releaseFixture();
  fixture.dependencies.tagState = () => ({
    exists: true,
    commit: 'a'.repeat(40),
  });
  fixture.failPhase = 'upload';

  await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
    phase: 'upload',
  });

  expect(fixture.createdTags).toEqual([]);
  expect(fixture.deletedTags).toEqual([]);
});
```

- [ ] **Step 2: Run tests and observe missing orchestration exports**

Run: `npm run test:webui -- scripts/commands/__tests__/release.test.ts`

Expected: FAIL because `runRelease()` and dependency interfaces are absent.

- [ ] **Step 3: Define orchestration boundaries**

```ts
export interface ReleaseDependencies {
  rootDir: string;
  env: NodeJS.ProcessEnv;
  now: () => Date;
  head: () => string;
  tagState: (tagName: string) => ReleaseTagState;
  createTag: (tagName: string) => void;
  deleteTag: (tagName: string) => void;
  runPhase: (phase: ReleasePhase, context: ReleasePhaseContext) => Promise<void>;
  signal?: AbortSignal;
}

export interface ReleasePhaseContext {
  options: ReleaseOptions;
  oldVersion: string;
  newVersion: string;
  dmgPath: string;
  appcastPath: string;
}

export interface ReleaseResult { oldVersion: string; newVersion: string; }
```

Export the existing `ReleaseOptions` interface. Add these helper definitions so
the orchestration has no implicit phase behavior:

```ts
function buildReleasePhaseContext(
  rootDir: string,
  options: ReleaseOptions,
  oldVersion: string,
  newVersion: string,
): ReleasePhaseContext {
  const config = JSON.parse(
    readFileSync(path.join(rootDir, 'dao.json'), 'utf-8'),
  ) as DaoConfig;
  const baseName = 'dao-browser-' + newVersion + '-mac-' + config.build.target_cpu;
  return {
    options,
    oldVersion,
    newVersion,
    dmgPath: path.join(rootDir, 'dist', baseName + '.dmg'),
    appcastPath: path.join(rootDir, 'dist', 'appcast.xml'),
  };
}

export function plannedReleasePhases(options: ReleaseOptions): ReleasePhase[] {
  const phases: ReleasePhase[] = [];
  if (!options.skipBuild && !options.resumeFromStaple) {
    phases.push('import', 'build', 'package', 'notarize', 'staple');
  } else if (options.resumeFromStaple) {
    phases.push('staple');
  }
  phases.push('appcast', 'metadata');
  if (!options.skipUpload) phases.push('upload');
  phases.push('tag');
  return phases;
}

function runReleasePreflight(
  options: ReleaseOptions,
  dependencies: ReleaseDependencies,
  config: DaoConfig,
  tagName: string,
): void {
  const problems = collectReleasePreflightProblems(
    options, dependencies.rootDir, dependencies.env, config,
  );
  const tag = dependencies.tagState(tagName);
  const head = dependencies.head();
  if (tag.exists && tag.commit !== head) {
    problems.push(
      'Tag ' + tagName + ' points at ' + tag.commit + ', not HEAD (' + head + ').',
    );
  }
  if (problems.length) {
    throw new ReleaseError('preflight', problems.join('\n\n'));
  }
}

function applyCanonicalPhaseMutation(
  phase: ReleasePhase,
  context: ReleasePhaseContext,
  transaction: ReleaseTransaction,
  publicAppcast: string,
  infoPath: string,
  chromiumVersion: string,
  now: Date,
): void {
  if (context.options.dryRun) return;
  if (phase === 'appcast') {
    transaction.mutateFile(publicAppcast, () => {
      copyFileSync(context.appcastPath, publicAppcast);
    });
  }
  if (phase === 'metadata') {
    transaction.mutateFile(infoPath, () => {
      updateInfoJson(infoPath, {
        version: context.newVersion,
        chromiumVersion,
        releasedAt: formatReleaseDate(now),
      });
    });
  }
}
```

Extract the current preflight checks into
`collectReleasePreflightProblems(options, rootDir, env, config): string[]`.
Extract the current appcast-generation block into
`generateReleaseAppcast(context, dependencies): Promise<void>` and the current
delta-selection/upload block into
`uploadReleaseArtifacts(context, dependencies): Promise<void>` without changing
their command arguments or filtering rules.

Define `defaultReleaseDependencies` with direct wrappers for Git and a complete
phase switch:

```ts
const defaultReleaseDependencies: ReleaseDependencies = {
  rootDir: ROOT_DIR,
  env: process.env,
  now: () => new Date(),
  head: () => readGitHead(ROOT_DIR) || '',
  tagState: (tagName) => inspectReleaseTag(ROOT_DIR, tagName),
  createTag: (tagName) => createReleaseTag(ROOT_DIR, tagName),
  deleteTag: (tagName) => deleteReleaseTag(ROOT_DIR, tagName),
  runPhase: async (phase, context) => {
    switch (phase) {
      case 'import':
        await importReleaseSources(context);
        return;
      case 'build':
        await buildReleaseApplication(context);
        return;
      case 'package':
        await packageReleaseArtifact(context);
        return;
      case 'notarize':
        await notarizeOrGuide(context.dmgPath, path.basename(context.dmgPath));
        return;
      case 'staple':
        if (context.options.resumeFromStaple) await assertStapled(context.dmgPath);
        else await stapleOrGuide(context.dmgPath, path.basename(context.dmgPath));
        return;
      case 'appcast':
        await generateReleaseAppcast(context, defaultReleaseDependencies);
        return;
      case 'upload':
        await uploadReleaseArtifacts(context, defaultReleaseDependencies);
        return;
      case 'metadata':
      case 'tag':
        return;
      default:
        throw new ReleaseError(phase, 'Unsupported release phase: ' + phase);
    }
  },
};
```

`importReleaseSources()`, `buildReleaseApplication()`, and
`packageReleaseArtifact()` are thin wrappers around the current exact `npx tsx
scripts/cli.ts ...` argument arrays and call `runReleaseStep()` with phases
`import`, `build`, and `package`, respectively. Tests replace these external
boundaries; canonical metadata writes stay inside `runRelease()`.

- [ ] **Step 4: Implement the transaction wrapper**

Use this control structure around the existing release steps:

```ts
export async function runRelease(
  options: ReleaseOptions,
  dependencies: ReleaseDependencies = defaultReleaseDependencies,
): Promise<ReleaseResult> {
  const daoPath = path.join(dependencies.rootDir, 'dao.json');
  const publicAppcast = path.join(dependencies.rootDir, 'website/public/appcast.xml');
  const infoPath = path.join(dependencies.rootDir, 'website/public/info.json');
  const config = JSON.parse(readFileSync(daoPath, 'utf-8')) as DaoConfig;
  const oldVersion = config.version.display;
  const newVersion = options.skipBump
    ? oldVersion
    : bumpVersion(oldVersion, options.bump || 'patch');
  const tagName = 'v' + newVersion;

  runReleasePreflight(options, dependencies, config, tagName);
  const preExistingTag = dependencies.tagState(tagName).exists;
  const transaction = new ReleaseTransaction([daoPath, publicAppcast, infoPath]);
  let currentPhase: ReleasePhase = 'version';

  try {
    if (!options.skipBump && !options.dryRun) {
      transaction.mutateFile(daoPath, () => writeDaoVersion(daoPath, newVersion));
    }
    const context = buildReleasePhaseContext(
      dependencies.rootDir, options, oldVersion, newVersion,
    );
    for (const phase of plannedReleasePhases(options)) {
      currentPhase = phase;
      await dependencies.runPhase(phase, context);
      applyCanonicalPhaseMutation(
        phase, context, transaction, publicAppcast, infoPath,
        config.version.version, dependencies.now(),
      );
    }
    if (!options.skipBump && !preExistingTag && !options.dryRun) {
      currentPhase = 'tag';
      dependencies.createTag(tagName);
      transaction.markTagCreated(tagName);
    }
    transaction.commit();
    return {oldVersion, newVersion};
  } catch (cause) {
    const failure = cause instanceof ReleaseError
      ? cause
      : new ReleaseError(currentPhase, String(cause), {cause});
    const rollback = transaction.rollback(dependencies.deleteTag);
    if (rollback.conflicts.length) {
      throw new ReleaseError(
        'rollback',
        'Release failed during ' + failure.phase +
          '; rollback conflicts: ' + rollback.conflicts.join(', '),
        {cause: failure},
      );
    }
    throw failure;
  }
}
```

`plannedReleasePhases()` preserves `--skip-build`, `--skip-upload`, and
`--resume-from-staple`. Include a synthetic final `tag` phase so tests can
inject failure immediately before actual tag creation.

- [ ] **Step 5: Route each canonical write through the transaction**

Change helper signatures to accept explicit paths:

```ts
function writeDaoVersion(configPath: string, newVersion: string): void;
function readGitHead(rootDir: string): string | null;
function createReleaseTag(rootDir: string, tagName: string): void;
function deleteReleaseTag(rootDir: string, tagName: string): void;
```

Wrap public metadata changes exactly as follows:

```ts
transaction.mutateFile(publicAppcast, () => {
  copyFileSync(context.appcastPath, publicAppcast);
});
transaction.mutateFile(infoPath, () => {
  updateInfoJson(infoPath, {
    version: context.newVersion,
    chromiumVersion,
    releasedAt: formatReleaseDate(now),
  });
});
```

Keep `dist/appcast.xml`, DMGs, deltas, and `engine/` outside the transaction.

- [ ] **Step 6: Make Commander own final exit behavior**

```ts
.action(async (options: ReleaseOptions) => {
  try {
    const result = await runRelease(options);
    printReleaseSuccess(result.newVersion);
  } catch (cause) {
    const failure = cause instanceof ReleaseError
      ? cause
      : new ReleaseError('rollback', String(cause), {cause});
    error(failure.message);
    process.exitCode = 1;
  }
});
```

No nested helper in `release.ts` may call `process.exit()`.

- [ ] **Step 7: Run focused tests**

Run: `npm run test:webui -- scripts/commands/__tests__/release.test.ts scripts/commands/__tests__/release-transaction.test.ts`

Expected: PASS for the complete failure matrix, same-version retry, and success.

- [ ] **Step 8: Conditionally commit**

Only with explicit Git authorization:

```bash
git add scripts/commands/release.ts scripts/commands/__tests__/release.test.ts
git commit -m "fix(release): roll back failed version bumps"
```

Otherwise leave changes unstaged.

---

### Task 4: Interrupts and Correct Recovery Guidance

**Files:**
- Modify: `scripts/utils.ts`
- Modify: `scripts/commands/release.ts`
- Modify: `scripts/commands/__tests__/release.test.ts`
- Modify: `docs/release-signing.md`

**Interfaces:**
- Consumes: `runRelease()` and `ReleaseDependencies`.
- Produces: abortable child execution, `runReleaseWithSignals()`, `formatReleaseFailure()`, and `formatNotarizeRecoveryCommand()`.

- [ ] **Step 1: Write failing abort and recovery-output tests**

```ts
it('rolls back when the active phase is aborted', async () => {
  const fixture = releaseFixture();
  const originalDao = readFileSync(fixture.daoPath);
  fixture.dependencies.runPhase = async (phase) => {
    if (phase === 'build') {
      throw new ReleaseError('build', 'Release interrupted by SIGINT.');
    }
  };
  await expect(runRelease({}, fixture.dependencies)).rejects.toMatchObject({
    phase: 'build', message: 'Release interrupted by SIGINT.',
  });
  expect(readFileSync(fixture.daoPath)).toEqual(originalDao);
});

it('resumes a manually stapled artifact without skip-bump', () => {
  const command = formatNotarizeRecoveryCommand(
    'dist/dao-browser-1.0.71-mac-arm64.dmg', 'dao-notary',
  );
  expect(command).toContain('npm run release -- --resume-from-staple');
  expect(command).not.toContain('--skip-bump');
});
```

- [ ] **Step 2: Run tests and observe missing behavior**

Run: `npm run test:webui -- scripts/commands/__tests__/release.test.ts`

Expected: FAIL because the recovery formatter is absent.

- [ ] **Step 3: Make `runStreaming()` abortable**

Extend its options without changing existing callers:

```ts
opts?: {
  cwd?: string;
  env?: NodeJS.ProcessEnv;
  signal?: AbortSignal;
}
```

Add `signal: opts?.signal` to `spawnOpts`. Pass `dependencies.signal` through
every production release child process.

- [ ] **Step 4: Scope SIGINT and SIGTERM handlers to the release command**

```ts
export async function runReleaseWithSignals(
  options: ReleaseOptions,
  dependencies: ReleaseDependencies = defaultReleaseDependencies,
): Promise<ReleaseResult> {
  const controller = new AbortController();
  const onSigint = () => controller.abort(new Error('Release interrupted by SIGINT.'));
  const onSigterm = () => controller.abort(new Error('Release interrupted by SIGTERM.'));
  process.once('SIGINT', onSigint);
  process.once('SIGTERM', onSigterm);
  try {
    return await runRelease(options, {...dependencies, signal: controller.signal});
  } finally {
    process.off('SIGINT', onSigint);
    process.off('SIGTERM', onSigterm);
  }
}
```

Normalize child abort errors to the active `ReleasePhase`, allowing the
standard catch block to restore metadata before setting a non-zero exit code.

- [ ] **Step 5: Implement pure recovery formatting**

```ts
export function formatNotarizeRecoveryCommand(
  dmgPath: string,
  profile: string,
): string {
  return [
    'xcrun notarytool submit ' + dmgPath +
      ' --keychain-profile ' + profile + ' --wait \\',
    '  && xcrun stapler staple ' + dmgPath + ' \\',
    '  && npm run release -- --resume-from-staple',
  ].join('\n');
}
```

Failure output must include the failed phase, restored old version, retained
`dist/` artifacts, and the fact that a normal retry uses the same candidate.
If rollback reports conflicts, list the exact paths and never claim complete
restoration.

- [ ] **Step 6: Update `docs/release-signing.md`**

Add this operator guidance:

````markdown
### Failed release behavior

For controlled failures and normal terminal interrupts, `npm run release`
restores `dao.json`, `website/public/appcast.xml`, and
`website/public/info.json` to their exact pre-release contents. Generated
files, Apple notarization submissions, and partial R2 uploads are retained.

Rerun `npm run release` normally to retry the same candidate version. If the
candidate DMG was notarized and stapled manually, run:

```bash
npm run release -- --resume-from-staple
```

Do not add `--skip-bump` after automatic rollback. It is reserved for manual
recovery when `dao.json` already contains the candidate version.
````

- [ ] **Step 7: Run focused tests and whitespace checks**

Run:

```bash
npm run test:webui -- scripts/commands/__tests__/release.test.ts scripts/commands/__tests__/release-transaction.test.ts
git diff --check
```

Expected: tests PASS and `git diff --check` produces no output.

- [ ] **Step 8: Conditionally commit**

Only with explicit Git authorization:

```bash
git add scripts/utils.ts scripts/commands/release.ts scripts/commands/__tests__/release.test.ts docs/release-signing.md
git commit -m "fix(release): roll back interrupted runs"
```

Otherwise leave changes unstaged.

---

### Task 5: Final Focused Verification

**Files:**
- Verify: `scripts/commands/release-transaction.ts`
- Verify: `scripts/commands/release.ts`
- Verify: `scripts/utils.ts`
- Verify: `scripts/commands/__tests__/release-transaction.test.ts`
- Verify: `scripts/commands/__tests__/release.test.ts`
- Verify: `docs/release-signing.md`

**Interfaces:**
- Consumes: Tasks 1-4.
- Produces: focused evidence and a narrow handoff.

- [ ] **Step 1: Run all related TypeScript tests**

Run:

```bash
npm run test:webui -- scripts/commands/__tests__/release.test.ts scripts/commands/__tests__/release-transaction.test.ts scripts/commands/__tests__/upload.test.ts scripts/commands/__tests__/package_scripts.test.ts
```

Expected: PASS with no failed Vitest cases.

- [ ] **Step 2: Verify dry-run is non-mutating through the fake fixture**

Add this regression test if it is not already present:

```ts
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
```

Run: `npm run test:webui -- scripts/commands/__tests__/release.test.ts`

Expected: PASS.

- [ ] **Step 3: Check obsolete exits and recovery commands**

Run:

```bash
rg -n "process\.exit\(|--skip-bump --resume-from-staple" scripts/commands/release.ts docs/release-signing.md
```

Expected: no matches. `process.exitCode = 1` at the Commander boundary remains allowed.

- [ ] **Step 4: Verify narrow repository state**

Run:

```bash
git status --short --untracked-files=all
git diff --check
git diff --stat
```

Expected: only planned release tooling, focused tests, and release-signing
documentation changed. Ignored design and plan documents may not appear in
ordinary Git status.

- [ ] **Step 5: Report exact verification evidence**

Report the Vitest commands and results, rollback guarantees covered, and any
skipped Git commits. Do not run or claim `npm run rebuild`: this change affects
only Node/TypeScript release tooling.
