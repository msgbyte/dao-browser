import path from 'node:path';

import {describe, expect, it} from 'vitest';

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
});
