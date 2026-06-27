import path from 'node:path';
import {existsSync, readFileSync} from 'node:fs';

import {describe, expect, it} from 'vitest';

const daoSettingsTranslations = [
  {
    id: '4833302064619809816',
    translation: '您与 Dao',
  },
  {
    id: '7260333196265292710',
    translation: '启用 Little Dao',
  },
  {
    id: '2977203776743842399',
    translation: '在紧凑的 Little Dao 窗口中打开来自其他应用的链接',
  },
];

describe('settings i18n patches', () => {
  it('provides Simplified Chinese translations for Dao settings strings', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/app/resources/generated_resources_zh-CN.xtb.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    for (const entry of daoSettingsTranslations) {
      expect(patch).toContain(
          `+<translation id="${entry.id}">${entry.translation}</translation>`);
    }
  });
});
