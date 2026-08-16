# Import Chromium Version Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop `npm run import` before any mutation when the Chromium checkout version differs from `dao.json.version.version`.

**Architecture:** Add pure, exported parsing and validation helpers beside the import command so focused Vitest tests can exercise version-file behavior without running the import workflow. Invoke validation immediately after confirming `engine/src` exists and before `--force` or any filesystem mutation.

**Tech Stack:** TypeScript, Node.js filesystem APIs, Commander, Vitest

## Global Constraints

- Treat `src/dao/`, `src/patches/`, and tracked build scripts as canonical; do not edit `engine/`.
- Do not run direct Chromium build tools or `gn gen`.
- Do not run state-changing Git commands without explicit authorization.
- This is build tooling only, so do not update `docs/features.md` or `docs/feature-checklist.md`.

---

### Task 1: Parse and validate the Chromium version before import

**Files:**
- Modify: `scripts/commands/import.ts`
- Test: `scripts/commands/__tests__/import.test.ts`

**Interfaces:**
- Consumes: Chromium `VERSION` files containing numeric `MAJOR`, `MINOR`, `BUILD`, and `PATCH` assignments; `dao.json.version.version` as the expected string.
- Produces: `readChromiumVersion(versionFilePath: string): string` and `validateChromiumVersion(versionFilePath: string, expectedVersion: string): string`.

- [x] **Step 1: Write failing parser and validator tests**

Add imports for the new helpers and tests that create temporary `VERSION` files:

```ts
import {
  applyPatchWithAlreadyAppliedFallback,
  buildFixImportPatchesCommand,
  buildFixImportPatchesMessage,
  readChromiumVersion,
  validateChromiumVersion,
} from '../import.js';

it('reads a complete Chromium version file', () => {
  const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-version-'));
  const versionPath = path.join(tempRoot, 'VERSION');
  writeFileSync(versionPath, [
    'MAJOR=148',
    'MINOR=0',
    'BUILD=7778',
    'PATCH=217',
    '',
  ].join('\n'));

  expect(readChromiumVersion(versionPath)).toBe('148.0.7778.217');
});

it('rejects a Chromium version that differs from dao.json', () => {
  const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-version-'));
  const versionPath = path.join(tempRoot, 'VERSION');
  writeFileSync(versionPath, [
    'MAJOR=147',
    'MINOR=0',
    'BUILD=7727',
    'PATCH=135',
    '',
  ].join('\n'));

  expect(() => validateChromiumVersion(versionPath, '148.0.7778.217'))
      .toThrow(
          'Chromium version mismatch: dao.json expects 148.0.7778.217, ' +
          'but engine/src/chrome/VERSION is 147.0.7727.135.');
});

it('rejects a malformed Chromium version file', () => {
  const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-version-'));
  const versionPath = path.join(tempRoot, 'VERSION');
  writeFileSync(versionPath, 'MAJOR=148\nMINOR=0\nBUILD=7778\n');

  expect(() => readChromiumVersion(versionPath)).toThrow(
      'Invalid Chromium version file: expected numeric MAJOR, MINOR, BUILD, ' +
      'and PATCH fields.');
});
```

- [x] **Step 2: Run the focused tests and verify the new tests fail**

Run:

```bash
npx vitest run scripts/commands/__tests__/import.test.ts
```

Expected: FAIL because `readChromiumVersion` and `validateChromiumVersion` are not exported by `scripts/commands/import.ts`.

- [x] **Step 3: Add the minimal parsing and validation helpers**

Add helpers before the Commander command definition:

```ts
const CHROMIUM_VERSION_FIELDS = ['MAJOR', 'MINOR', 'BUILD', 'PATCH'] as const;

export function readChromiumVersion(versionFilePath: string): string {
  if (!existsSync(versionFilePath)) {
    throw new Error(`Chromium version file not found: ${versionFilePath}`);
  }

  const fields = new Map<string, string>();
  for (const line of readFileSync(versionFilePath, 'utf-8').split(/\r?\n/)) {
    const match = line.match(/^([A-Z]+)=(.*)$/);
    if (match) {
      fields.set(match[1], match[2]);
    }
  }

  const values = CHROMIUM_VERSION_FIELDS.map((field) => fields.get(field));
  if (values.some((value) => value === undefined || !/^\d+$/.test(value))) {
    throw new Error(
        'Invalid Chromium version file: expected numeric MAJOR, MINOR, BUILD, ' +
        'and PATCH fields.');
  }

  return values.join('.');
}

export function validateChromiumVersion(
  versionFilePath: string,
  expectedVersion: string
): string {
  const actualVersion = readChromiumVersion(versionFilePath);
  if (actualVersion !== expectedVersion) {
    throw new Error(
        `Chromium version mismatch: dao.json expects ${expectedVersion}, ` +
        `but engine/src/chrome/VERSION is ${actualVersion}.`);
  }
  return actualVersion;
}
```

- [x] **Step 4: Invoke validation before all import mutations**

Load the config after the existing `engine/src` existence check, validate
`path.join(srcDir, 'chrome', 'VERSION')`, and handle failure before the
`opts.force` block:

```ts
const config = loadConfig();
try {
  const chromiumVersion = validateChromiumVersion(
      path.join(srcDir, 'chrome', 'VERSION'),
      config.version.version);
  success(`Chromium version verified: ${chromiumVersion}`);
} catch (e) {
  error((e as Error).message);
  log("Run 'npm run download' to sync engine/src with dao.json.");
  process.exit(1);
}
```

Remove the later duplicate `const config = loadConfig();` while retaining its
use for `config.version.display`.

- [x] **Step 5: Run the focused tests and verify they pass**

Run:

```bash
npx vitest run scripts/commands/__tests__/import.test.ts
```

Expected: PASS with all tests in the file green.

- [x] **Step 6: Verify formatting and the real checkout version**

Run:

```bash
git diff --check
npm run import -- --patches-only
```

Expected: `git diff --check` exits successfully, and import first reports
`Chromium version verified: 148.0.7778.217` before continuing through the
existing patches-only workflow.

Do not stage or commit the changes without explicit user authorization.
