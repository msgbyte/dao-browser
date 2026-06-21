import path from 'node:path';
import {existsSync, readFileSync} from 'node:fs';

import {describe, expect, it} from 'vitest';

import {
  isChromiumRewriteManagedPath,
  mirrorDaoSchemeInQuotedChromeUrls,
  rewriteChromiumPathContent,
  rewriteChromeSchemeText,
  rewriteWebUiBaseHref,
} from '../../chromium-rewrites.js';
import {
  buildFixImportPatchesCommand,
  buildFixImportPatchesMessage,
} from '../import.js';

describe('import helpers', () => {
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
