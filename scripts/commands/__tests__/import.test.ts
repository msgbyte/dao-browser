import path from 'node:path';
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
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
  readChromiumVersion,
  validateChromiumVersion,
} from '../import.js';

describe('import helpers', () => {
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
