import path from 'node:path';
import {
  copyFileSync,
  existsSync,
  lstatSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  symlinkSync,
  writeFileSync,
} from 'node:fs';
import os from 'node:os';
import {execFileSync} from 'node:child_process';

import {describe, expect, it} from 'vitest';

import {
  isChromiumRewriteManagedPath,
  mirrorDaoSchemeInQuotedChromeUrls,
  rewriteChromiumPathContent,
  rewriteChromeSchemeText,
  rewriteWebUiBaseHref,
} from '../../chromium-rewrites.js';
import {
  applyPatchWithAlreadyAppliedFallback,
  buildFixImportPatchesCommand,
  buildFixImportPatchesMessage,
  cleanupPatchCreatedFiles,
  parsePatchTargets,
  prepareForcedImport,
  readChromiumVersion,
  validateChromiumVersion,
} from '../import.js';

describe('import helpers', () => {
  it('parses every target in a multi-file patch', () => {
    const patchContent = [
      'diff --git a/tracked.txt b/tracked.txt',
      '--- a/tracked.txt',
      '+++ b/tracked.txt',
      '@@ -1 +1 @@',
      '-old value',
      '+new value',
      'diff --git a/generated/new-file.d.ts b/generated/new-file.d.ts',
      'new file mode 100644',
      '--- /dev/null',
      '+++ b/generated/new-file.d.ts',
      '@@ -0,0 +1 @@',
      '+new definition',
      '',
    ].join('\n');

    expect(parsePatchTargets(patchContent)).toEqual([
      {path: 'tracked.txt', isNewFile: false},
      {path: 'generated/new-file.d.ts', isNewFile: true},
    ]);
  });

  it('rejects unsafe patch target paths', () => {
    const patchContent = [
      'diff --git a/../outside.txt b/../outside.txt',
      '--- /dev/null',
      '+++ b/../outside.txt',
      '',
    ].join('\n');

    expect(() => parsePatchTargets(patchContent)).toThrow(
        'Unsafe patch target path: ../outside.txt');
  });

  it('ignores target-like lines inside patch hunks', () => {
    const patchContent = [
      'diff --git a/generated/new-file.d.ts b/generated/new-file.d.ts',
      'new file mode 100644',
      '--- /dev/null',
      '+++ b/generated/new-file.d.ts',
      '@@ -0,0 +1,2 @@',
      '+new definition',
      '+++ b/unrelated.txt',
      '',
    ].join('\n');

    expect(parsePatchTargets(patchContent)).toEqual([
      {path: 'generated/new-file.d.ts', isNewFile: true},
    ]);
  });

  it('removes only patch-declared untracked files idempotently', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    const repoDir = path.join(tempRoot, 'engine/src');
    const generatedDir = path.join(repoDir, 'generated');
    mkdirSync(generatedDir, {recursive: true});

    execFileSync('git', ['init'], {cwd: repoDir, stdio: 'ignore'});
    writeFileSync(path.join(repoDir, '.gitkeep'), '');
    execFileSync('git', ['add', '.gitkeep'], {
      cwd: repoDir,
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {cwd: repoDir, stdio: 'ignore'});

    const generatedPath = path.join(generatedDir, 'new-file.d.ts');
    const unrelatedPath = path.join(repoDir, 'notes.txt');
    writeFileSync(generatedPath, 'stale definition\n');
    writeFileSync(unrelatedPath, 'keep me\n');

    const patchPath = path.join(tempRoot, 'new-file.patch');
    writeFileSync(patchPath, [
      'diff --git a/generated/new-file.d.ts b/generated/new-file.d.ts',
      'new file mode 100644',
      '--- /dev/null',
      '+++ b/generated/new-file.d.ts',
      '@@ -0,0 +1 @@',
      '+new definition',
      '',
    ].join('\n'));

    cleanupPatchCreatedFiles(repoDir, [patchPath]);
    cleanupPatchCreatedFiles(repoDir, [patchPath]);

    expect(existsSync(generatedPath)).toBe(false);
    expect(readFileSync(unrelatedPath, 'utf-8')).toBe('keep me\n');
  });

  it('rejects a tracked collision for a patch-created file', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    const repoDir = path.join(tempRoot, 'engine/src');
    mkdirSync(repoDir, {recursive: true});

    execFileSync('git', ['init'], {cwd: repoDir, stdio: 'ignore'});
    writeFileSync(path.join(repoDir, 'tracked.txt'), 'upstream file\n');
    execFileSync('git', ['add', 'tracked.txt'], {
      cwd: repoDir,
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {cwd: repoDir, stdio: 'ignore'});

    const patchPath = path.join(tempRoot, 'new-file.patch');
    writeFileSync(patchPath, [
      'diff --git a/tracked.txt b/tracked.txt',
      'new file mode 100644',
      '--- /dev/null',
      '+++ b/tracked.txt',
      '@@ -0,0 +1 @@',
      '+Dao file',
      '',
    ].join('\n'));

    expect(() => cleanupPatchCreatedFiles(repoDir, [patchPath])).toThrow(
        'Patch new-file target is tracked: tracked.txt');
  });

  it('rejects patch targets beneath a symlink parent', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    const repoDir = path.join(tempRoot, 'engine/src');
    const outsideDir = path.join(tempRoot, 'outside');
    mkdirSync(repoDir, {recursive: true});
    mkdirSync(outsideDir, {recursive: true});

    execFileSync('git', ['init'], {cwd: repoDir, stdio: 'ignore'});
    writeFileSync(path.join(repoDir, '.gitkeep'), '');
    execFileSync('git', ['add', '.gitkeep'], {
      cwd: repoDir,
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {cwd: repoDir, stdio: 'ignore'});

    const outsidePath = path.join(outsideDir, 'victim.txt');
    writeFileSync(outsidePath, 'keep me\n');
    symlinkSync(outsideDir, path.join(repoDir, 'linked'), 'dir');

    const patchPath = path.join(tempRoot, 'new-file.patch');
    writeFileSync(patchPath, [
      'diff --git a/linked/victim.txt b/linked/victim.txt',
      'new file mode 100644',
      '--- /dev/null',
      '+++ b/linked/victim.txt',
      '@@ -0,0 +1 @@',
      '+Dao file',
      '',
    ].join('\n'));

    expect(() => cleanupPatchCreatedFiles(repoDir, [patchPath])).toThrow(
        'Patch target has a symlink parent: linked/victim.txt');
    expect(readFileSync(outsidePath, 'utf-8')).toBe('keep me\n');
  });

  it('removes a dangling symlink at an exact new-file target', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    const repoDir = path.join(tempRoot, 'engine/src');
    mkdirSync(repoDir, {recursive: true});

    execFileSync('git', ['init'], {cwd: repoDir, stdio: 'ignore'});
    writeFileSync(path.join(repoDir, '.gitkeep'), '');
    execFileSync('git', ['add', '.gitkeep'], {
      cwd: repoDir,
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {cwd: repoDir, stdio: 'ignore'});

    const targetPath = path.join(repoDir, 'generated.d.ts');
    symlinkSync(path.join(tempRoot, 'missing-target'), targetPath);
    const patchPath = path.join(tempRoot, 'new-file.patch');
    writeFileSync(patchPath, [
      'diff --git a/generated.d.ts b/generated.d.ts',
      'new file mode 100644',
      '--- /dev/null',
      '+++ b/generated.d.ts',
      '@@ -0,0 +1 @@',
      '+Dao file',
      '',
    ].join('\n'));

    expect(lstatSync(targetPath).isSymbolicLink()).toBe(true);

    cleanupPatchCreatedFiles(repoDir, [patchPath]);

    expect(() => lstatSync(targetPath)).toThrow();
  });

  it('prepares an idempotent forced-import baseline', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    const repoDir = path.join(tempRoot, 'engine/src');
    const generatedDir = path.join(repoDir, 'generated');
    mkdirSync(generatedDir, {recursive: true});

    execFileSync('git', ['init'], {cwd: repoDir, stdio: 'ignore'});
    const trackedPath = path.join(repoDir, 'tracked.txt');
    writeFileSync(trackedPath, 'upstream value\n');
    execFileSync('git', ['add', 'tracked.txt'], {
      cwd: repoDir,
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {cwd: repoDir, stdio: 'ignore'});

    const generatedPath = path.join(generatedDir, 'new-file.d.ts');
    const unrelatedPath = path.join(repoDir, 'notes.txt');
    const patchPath = path.join(tempRoot, 'multi-file.patch');
    writeFileSync(patchPath, [
      'diff --git a/tracked.txt b/tracked.txt',
      '--- a/tracked.txt',
      '+++ b/tracked.txt',
      '@@ -1 +1 @@',
      '-upstream value',
      '+Dao value',
      'diff --git a/generated/new-file.d.ts b/generated/new-file.d.ts',
      'new file mode 100644',
      '--- /dev/null',
      '+++ b/generated/new-file.d.ts',
      '@@ -0,0 +1 @@',
      '+new definition',
      '',
    ].join('\n'));
    writeFileSync(unrelatedPath, 'keep me\n');

    for (let release = 0; release < 2; release++) {
      writeFileSync(trackedPath, 'Dao value\n');
      writeFileSync(generatedPath, 'new definition\n');

      prepareForcedImport(repoDir, [patchPath]);

      expect(readFileSync(trackedPath, 'utf-8')).toBe('upstream value\n');
      expect(existsSync(generatedPath)).toBe(false);
      expect(readFileSync(unrelatedPath, 'utf-8')).toBe('keep me\n');
    }
  });

  it('reads a complete Chromium version file', () => {
    const tempRoot = mkdtempSync(
        path.join(os.tmpdir(), 'dao-import-version-'));
    const versionPath = path.join(tempRoot, 'VERSION');
    writeFileSync(versionPath, [
      'MAJOR=149',
      'MINOR=0',
      'BUILD=7827',
      'PATCH=201',
      '',
    ].join('\n'));

    expect(readChromiumVersion(versionPath)).toBe('149.0.7827.201');
  });

  it('rejects a Chromium version that differs from dao.json', () => {
    const tempRoot = mkdtempSync(
        path.join(os.tmpdir(), 'dao-import-version-'));
    const versionPath = path.join(tempRoot, 'VERSION');
    writeFileSync(versionPath, [
      'MAJOR=147',
      'MINOR=0',
      'BUILD=7727',
      'PATCH=135',
      '',
    ].join('\n'));

    expect(() => validateChromiumVersion(
        versionPath, '149.0.7827.201')).toThrow(
        'Chromium version mismatch: dao.json expects 149.0.7827.201, ' +
        'but engine/src/chrome/VERSION is 147.0.7727.135.');
  });

  it('rejects a malformed Chromium version file', () => {
    const tempRoot = mkdtempSync(
        path.join(os.tmpdir(), 'dao-import-version-'));
    const versionPath = path.join(tempRoot, 'VERSION');
    writeFileSync(versionPath, 'MAJOR=149\nMINOR=0\nBUILD=7827\n');

    expect(() => readChromiumVersion(versionPath)).toThrow(
        'Invalid Chromium version file: expected numeric MAJOR, MINOR, ' +
        'BUILD, and PATCH fields.');
  });

  it('prints a copyable repair command for failed patch paths', () => {
    expect(buildFixImportPatchesCommand([
      'chrome/browser/ui/BUILD.gn.patch',
      'third_party/blink/renderer/core/html/resources/html.css.patch',
    ])).toBe(
        "sh scripts/fix-import-patches.sh " +
        "'src/patches/chrome/browser/ui/BUILD.gn.patch' " +
        "'src/patches/third_party/blink/renderer/core/html/resources/html.css.patch'");
  });

  it('keeps repo-relative patch paths and quotes shell metacharacters', () => {
    expect(buildFixImportPatchesCommand([
      "src/patches/foo/bar's file.patch",
    ])).toBe(
        "sh scripts/fix-import-patches.sh " +
        "'src/patches/foo/bar'\\''s file.patch'");
  });

  it('converts absolute patch paths to repo-relative arguments', () => {
    const patchPath = path.resolve(
        'src/patches/chrome/browser/ui/BUILD.gn.patch');

    expect(buildFixImportPatchesCommand([patchPath])).toBe(
        "sh scripts/fix-import-patches.sh " +
        "'src/patches/chrome/browser/ui/BUILD.gn.patch'");
  });

  it('formats the repair command as a visible final error hint', () => {
    expect(buildFixImportPatchesMessage([
      'chrome/browser/ui/BUILD.gn.patch',
    ])).toBe([
      'Repair failed patch targets with:',
      "sh scripts/fix-import-patches.sh 'src/patches/chrome/browser/ui/BUILD.gn.patch'",
      'Then re-run: npm run import',
    ].join('\n'));
  });

  it('treats fallback apply failures as already applied when reverse-check passes', async () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    const repoDir = path.join(tempRoot, 'engine/src');
    mkdirSync(repoDir, {recursive: true});

    execFileSync('git', ['init'], {
      cwd: repoDir,
      stdio: 'ignore',
    });
    writeFileSync(path.join(repoDir, 'example.txt'), 'old value\n');
    execFileSync('git', ['add', 'example.txt'], {
      cwd: repoDir,
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {
          cwd: repoDir,
          stdio: 'ignore',
        });

    const patchPath = path.join(tempRoot, 'example.patch');
    writeFileSync(
        patchPath,
        [
          'diff --git a/example.txt b/example.txt',
          '--- a/example.txt',
          '+++ b/example.txt',
          '@@ -1 +1 @@',
          '-old value',
          '+new value',
          '',
        ].join('\n'));

    execFileSync('git', ['apply', patchPath], {
      cwd: repoDir,
      stdio: 'ignore',
    });

    await expect(applyPatchWithAlreadyAppliedFallback(repoDir, patchPath))
        .resolves.toBe('already-applied');
  });

  it('repairs patch files whose names do not match the target path', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    mkdirSync(path.join(tempRoot, 'scripts'), {recursive: true});
    mkdirSync(path.join(tempRoot, 'src/patches/chrome/app'), {
      recursive: true,
    });
    mkdirSync(path.join(tempRoot, 'engine/src/chrome/app'), {recursive: true});

    copyFileSync(
        path.join(process.cwd(), 'scripts/fix-import-patches.sh'),
        path.join(tempRoot, 'scripts/fix-import-patches.sh'));

    const targetPath =
        path.join(tempRoot, 'engine/src/chrome/app/settings_strings.grdp');
    writeFileSync(targetPath, 'line 1\nold value\nline 3\n');

    execFileSync('git', ['init'], {
      cwd: path.join(tempRoot, 'engine/src'),
      stdio: 'ignore',
    });
    execFileSync('git', ['add', 'chrome/app/settings_strings.grdp'], {
      cwd: path.join(tempRoot, 'engine/src'),
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {
          cwd: path.join(tempRoot, 'engine/src'),
          stdio: 'ignore',
        });

    writeFileSync(
        path.join(
            tempRoot, 'src/patches/chrome/app/settings_strings_dao.grdp.patch'),
        [
          'diff --git a/chrome/app/settings_strings.grdp b/chrome/app/settings_strings.grdp',
          '--- a/chrome/app/settings_strings.grdp',
          '+++ b/chrome/app/settings_strings.grdp',
          '@@ -1,3 +1,3 @@',
          ' line 1',
          '-old value',
          '+new value',
          ' line 3',
          '',
        ].join('\n'));

    execFileSync(
        'sh',
        [
          'scripts/fix-import-patches.sh',
          'src/patches/chrome/app/settings_strings_dao.grdp.patch',
        ],
        {
          cwd: tempRoot,
          stdio: 'pipe',
        });

    expect(readFileSync(targetPath, 'utf-8')).toBe(
        'line 1\nnew value\nline 3\n');
  });

  it('repairs new-file patches when an older untracked target exists', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    mkdirSync(path.join(tempRoot, 'scripts'), {recursive: true});
    mkdirSync(path.join(tempRoot, 'src/patches/chrome/test'), {
      recursive: true,
    });
    mkdirSync(path.join(tempRoot, 'engine/src/chrome/test'), {recursive: true});

    copyFileSync(
        path.join(process.cwd(), 'scripts/fix-import-patches.sh'),
        path.join(tempRoot, 'scripts/fix-import-patches.sh'));

    execFileSync('git', ['init'], {
      cwd: path.join(tempRoot, 'engine/src'),
      stdio: 'ignore',
    });
    writeFileSync(path.join(tempRoot, 'engine/src/.gitkeep'), '');
    execFileSync('git', ['add', '.gitkeep'], {
      cwd: path.join(tempRoot, 'engine/src'),
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {
          cwd: path.join(tempRoot, 'engine/src'),
          stdio: 'ignore',
        });

    const targetPath =
        path.join(tempRoot, 'engine/src/chrome/test/dao_page_test.ts');
    writeFileSync(targetPath, 'old test\n');

    writeFileSync(
        path.join(tempRoot, 'src/patches/chrome/test/dao_page_test.ts.patch'),
        [
          'diff --git a/chrome/test/dao_page_test.ts b/chrome/test/dao_page_test.ts',
          'new file mode 100644',
          'index 0000000000..0000000001',
          '--- /dev/null',
          '+++ b/chrome/test/dao_page_test.ts',
          '@@ -0,0 +1 @@',
          '+new test',
          '',
        ].join('\n'));

    execFileSync(
        'sh',
        [
          'scripts/fix-import-patches.sh',
          'src/patches/chrome/test/dao_page_test.ts.patch',
        ],
        {
          cwd: tempRoot,
          stdio: 'pipe',
        });

    expect(readFileSync(targetPath, 'utf-8')).toBe('new test\n');
  });

  it('repairs every target in a multi-file patch', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    const scriptsDir = path.join(tempRoot, 'scripts');
    const patchesDir = path.join(
        tempRoot, 'src/patches/ui/webui/resources/tools/eslint');
    const engineDir = path.join(
        tempRoot, 'engine/src/ui/webui/resources/tools/eslint');
    mkdirSync(scriptsDir, {recursive: true});
    mkdirSync(patchesDir, {recursive: true});
    mkdirSync(engineDir, {recursive: true});

    copyFileSync(
        path.join(process.cwd(), 'scripts/fix-import-patches.sh'),
        path.join(scriptsDir, 'fix-import-patches.sh'));

    const repoDir = path.join(tempRoot, 'engine/src');
    const buildPath = path.join(engineDir, 'BUILD.gn');
    const definitionPath = path.join(engineDir, 'eslint_plugin_lit.d.ts');
    const unrelatedPath = path.join(engineDir, 'notes.txt');
    execFileSync('git', ['init'], {cwd: repoDir, stdio: 'ignore'});
    writeFileSync(buildPath, 'sources = []\n');
    execFileSync(
        'git',
        ['add', 'ui/webui/resources/tools/eslint/BUILD.gn'],
        {cwd: repoDir, stdio: 'ignore'});
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {cwd: repoDir, stdio: 'ignore'});

    writeFileSync(buildPath, 'stale local value\n');
    writeFileSync(definitionPath, 'stale definition\n');
    writeFileSync(unrelatedPath, 'keep me\n');

    const patchPath = path.join(patchesDir, 'BUILD.gn.patch');
    writeFileSync(patchPath, [
      'diff --git a/ui/webui/resources/tools/eslint/BUILD.gn b/ui/webui/resources/tools/eslint/BUILD.gn',
      '--- a/ui/webui/resources/tools/eslint/BUILD.gn',
      '+++ b/ui/webui/resources/tools/eslint/BUILD.gn',
      '@@ -1 +1,2 @@',
      ' sources = []',
      '+definitions = [\"eslint_plugin_lit.d.ts\"]',
      'diff --git a/ui/webui/resources/tools/eslint/eslint_plugin_lit.d.ts b/ui/webui/resources/tools/eslint/eslint_plugin_lit.d.ts',
      'new file mode 100644',
      '--- /dev/null',
      '+++ b/ui/webui/resources/tools/eslint/eslint_plugin_lit.d.ts',
      '@@ -0,0 +1,2 @@',
      '+new definition',
      '+++ b/ui/webui/resources/tools/eslint/notes.txt',
      '',
    ].join('\n'));

    expect(() => execFileSync(
        'sh',
        [
          'scripts/fix-import-patches.sh',
          'src/patches/ui/webui/resources/tools/eslint/BUILD.gn.patch',
        ],
        {cwd: tempRoot, stdio: 'pipe'})).not.toThrow();

    expect(readFileSync(buildPath, 'utf-8')).toContain(
        'definitions = ["eslint_plugin_lit.d.ts"]');
    expect(readFileSync(definitionPath, 'utf-8')).toBe([
      'new definition',
      '++ b/ui/webui/resources/tools/eslint/notes.txt',
      '',
    ].join('\n'));
    expect(readFileSync(unrelatedPath, 'utf-8')).toBe('keep me\n');
  });

  it('rejects repair targets beneath a symlink parent', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    const scriptsDir = path.join(tempRoot, 'scripts');
    const patchesDir = path.join(tempRoot, 'src/patches/test');
    const repoDir = path.join(tempRoot, 'engine/src');
    const outsideDir = path.join(tempRoot, 'outside');
    mkdirSync(scriptsDir, {recursive: true});
    mkdirSync(patchesDir, {recursive: true});
    mkdirSync(repoDir, {recursive: true});
    mkdirSync(outsideDir, {recursive: true});

    copyFileSync(
        path.join(process.cwd(), 'scripts/fix-import-patches.sh'),
        path.join(scriptsDir, 'fix-import-patches.sh'));

    execFileSync('git', ['init'], {cwd: repoDir, stdio: 'ignore'});
    writeFileSync(path.join(repoDir, '.gitkeep'), '');
    execFileSync('git', ['add', '.gitkeep'], {
      cwd: repoDir,
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {cwd: repoDir, stdio: 'ignore'});

    const outsidePath = path.join(outsideDir, 'victim.txt');
    writeFileSync(outsidePath, 'keep me\n');
    symlinkSync(outsideDir, path.join(repoDir, 'linked'), 'dir');

    writeFileSync(path.join(patchesDir, 'symlink.patch'), [
      'diff --git a/linked/victim.txt b/linked/victim.txt',
      'new file mode 100644',
      '--- /dev/null',
      '+++ b/linked/victim.txt',
      '@@ -0,0 +1 @@',
      '+Dao file',
      '',
    ].join('\n'));

    expect(() => execFileSync(
        'sh',
        [
          'scripts/fix-import-patches.sh',
          'src/patches/test/symlink.patch',
        ],
        {cwd: tempRoot, stdio: 'pipe'})).toThrow(
        'patch target has a symlink parent: linked/victim.txt');
    expect(readFileSync(outsidePath, 'utf-8')).toBe('keep me\n');
  });

  it('preserves untracked targets not declared as new files', () => {
    const tempRoot = mkdtempSync(path.join(os.tmpdir(), 'dao-import-test-'));
    const scriptsDir = path.join(tempRoot, 'scripts');
    const patchesDir = path.join(tempRoot, 'src/patches/test');
    const repoDir = path.join(tempRoot, 'engine/src');
    mkdirSync(scriptsDir, {recursive: true});
    mkdirSync(patchesDir, {recursive: true});
    mkdirSync(repoDir, {recursive: true});

    copyFileSync(
        path.join(process.cwd(), 'scripts/fix-import-patches.sh'),
        path.join(scriptsDir, 'fix-import-patches.sh'));

    execFileSync('git', ['init'], {cwd: repoDir, stdio: 'ignore'});
    writeFileSync(path.join(repoDir, '.gitkeep'), '');
    execFileSync('git', ['add', '.gitkeep'], {
      cwd: repoDir,
      stdio: 'ignore',
    });
    execFileSync(
        'git',
        [
          '-c',
          'user.name=Dao Test',
          '-c',
          'user.email=dao-test@example.com',
          'commit',
          '-m',
          'init',
        ],
        {cwd: repoDir, stdio: 'ignore'});

    const targetPath = path.join(repoDir, 'untracked.txt');
    writeFileSync(targetPath, 'old value\n');
    writeFileSync(path.join(patchesDir, 'tracked.patch'), [
      'diff --git a/untracked.txt b/untracked.txt',
      '--- a/untracked.txt',
      '+++ b/untracked.txt',
      '@@ -1 +1 @@',
      '-old value',
      '+new value',
      '',
    ].join('\n'));

    expect(() => execFileSync(
        'sh',
        [
          'scripts/fix-import-patches.sh',
          'src/patches/test/tracked.patch',
        ],
        {cwd: tempRoot, stdio: 'pipe'})).toThrow(
        'patch target is not tracked: untracked.txt');
    expect(readFileSync(targetPath, 'utf-8')).toBe('old value\n');
  });

  it('rewrites chrome scheme text and reports replacements', () => {
    const result = rewriteChromeSchemeText(
        'Open chrome://credits and chrome://version.');

    expect(result.content).toBe('Open dao://credits and dao://version.');
    expect(result.replacements).toBe(2);
  });

  it('rewrites only WebUI base href values in HTML shells', () => {
    const result = rewriteWebUiBaseHref([
      '<base href="chrome://settings">',
      '<link rel="stylesheet" href="chrome://resources/css/md_colors.css">',
    ].join('\n'));

    expect(result.content).toBe([
      '<base href="dao://settings">',
      '<link rel="stylesheet" href="chrome://resources/css/md_colors.css">',
    ].join('\n'));
    expect(result.replacements).toBe(1);
  });

  it('mirrors quoted chrome WebUI matches without duplicating dao entries', () => {
    const result = mirrorDaoSchemeInQuotedChromeUrls([
      '      "chrome://settings/*",',
      '      "chrome://extensions/*", "dao://extensions/*",',
    ].join('\n'));

    expect(result.content).toBe([
      '      "chrome://settings/*", "dao://settings/*",',
      '      "chrome://extensions/*", "dao://extensions/*",',
    ].join('\n'));
    expect(result.replacements).toBe(1);
  });

  it('does not duplicate dao matches already inserted on the next line', () => {
    const result = mirrorDaoSchemeInQuotedChromeUrls([
      '      "chrome://settings/*",',
      '      "dao://settings/*"',
    ].join('\n'));

    expect(result.content).toBe([
      '      "chrome://settings/*",',
      '      "dao://settings/*"',
    ].join('\n'));
    expect(result.replacements).toBe(0);
  });

  it('marks generated rewrite patch targets as export-managed', () => {
    expect(isChromiumRewriteManagedPath(
        'components/resources/terms/terms_en.html')).toBe(true);
    expect(isChromiumRewriteManagedPath(
        'chrome/browser/resources/settings/settings.html')).toBe(true);
    expect(isChromiumRewriteManagedPath(
        'chrome/app/chromium_strings.grd')).toBe(false);
  });

  it('selects the rewrite behavior from a Chromium path', () => {
    expect(rewriteChromiumPathContent(
        'chrome/browser/resources/settings/settings.html',
        '<base href="chrome://settings">\n' +
            '<script src="chrome://resources/foo.js"></script>')).toEqual({
              content: '<base href="dao://settings">\n' +
                  '<script src="chrome://resources/foo.js"></script>',
              replacements: 1,
            });

    expect(rewriteChromiumPathContent(
        'chrome/app/chromium_strings.grd',
        'chrome://version')).toBeNull();
  });

  it('keeps Dao UI source lists in Dao-owned GN metadata', () => {
    const patch = readFileSync(
        path.join(process.cwd(), 'src/patches/chrome/browser/ui/BUILD.gn.patch'),
        'utf-8');
    const gniPath = path.join(
        process.cwd(), 'src/dao/browser/ui/dao_ui_sources.gni');

    expect(existsSync(gniPath)).toBe(true);
    expect(readFileSync(gniPath, 'utf-8')).toContain(
        '"//dao/browser/ui/views/sidebar/dao_sidebar_view.cc"');
    expect(patch).toContain('import("//dao/browser/ui/dao_ui_sources.gni")');
    expect(patch).not.toContain(
        '+    "//dao/browser/ui/views/dao_sidebar_view.cc",');
  });

  it('keeps extension URLPattern compatible with legacy chrome WebUI URLs', () => {
    const patch = readFileSync(
        path.join(
            process.cwd(),
            'src/patches/extensions/common/url_pattern.cc.patch'),
        'utf-8');

    expect(patch).toContain('kLegacyChromeUIScheme');
    expect(patch).toContain('URLPattern::SCHEME_CHROMEUI');
  });
});
