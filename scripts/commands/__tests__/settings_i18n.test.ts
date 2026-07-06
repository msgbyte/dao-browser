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
  {
    id: '9088288809489009615',
    translation: '增强命令栏建议',
  },
  {
    id: '3815248333120566849',
    translation: '在命令栏中使用更丰富的标签页、命令、搜索和 Dao 建议',
  },
  {
    id: '42079359526797400',
    translation: '增强的画中画 (PIP)',
  },
  {
    id: '5069578397898592708',
    translation: '在部分网站使用定制 Document Picture-in-Picture 窗口',
  },
  {
    id: '5091799886440513284',
    translation: '画中画预览',
  },
  {
    id: '8813598646318262401',
    translation: 'Great scene!',
  },
  {
    id: '7352912906049883152',
    translation: 'Nice shot',
  },
  {
    id: '2837712785763321439',
    translation: 'So smooth',
  },
  {
    id: '1765568289540142383',
    translation: 'Love this',
  },
  {
    id: '2841236032449409747',
    translation: 'Nice sync',
  },
  {
    id: '1928676582356902292',
    translation: 'Perfect timing',
  },
  {
    id: '1607811381030986058',
    translation: '增强模式',
  },
  {
    id: '666789743951309643',
    translation: '部分网站会以完整播放器窗口打开，保留控制条、字幕、弹幕和 Dao 样式。',
  },
  {
    id: '6726927422842854671',
    translation: '原版模式',
  },
  {
    id: '9038265565356311756',
    translation: '支持的网站使用浏览器原版画中画窗口，只显示视频画面。',
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

  it('renders one enhanced PIP preview based on the selected pref value', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    expect(patch).toContain(
        'if="[[prefs.dao.enhanced_pip_enabled.value]]"');
    expect(patch).toContain(
        'if="[[!prefs.dao.enhanced_pip_enabled.value]]"');
    expect(patch).toContain('id="enhancedPipPreviewSubtitles"');
    expect(patch).toContain('id="enhancedPipPreviewOriginalWindow"');
    expect(patch).toContain('id="enhancedPipPreviewOriginalVideo"');
    expect(patch).not.toContain('dao-pip-preview-grid');
    expect(patch).not.toContain('bilibili');
  });

  it('keeps the enhanced PIP preview window at a fixed 16:9 ratio', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    expect(patch).toContain('aspect-ratio: 16 / 9;');
    expect(patch).toContain('height: auto;');
    expect(patch).toContain('min-height: 160px;');
    expect(patch).not.toContain('height: 96px;');
  });

  it('makes the original PIP preview fill the shared 16:9 window', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    expect(patch).toContain('.dao-pip-preview-original-window');
    expect(patch).toContain('height: 100%;');
    expect(patch).toContain('width: 100%;');
    expect(patch).not.toContain('width: 58%;');
  });

  it('renders six enhanced PIP danmaku as tiny localized English text', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    const danmakuKeys = [
      'enhancedPipPreviewCommentPrimary',
      'enhancedPipPreviewCommentSecondary',
      'enhancedPipPreviewCommentTertiary',
      'enhancedPipPreviewCommentQuaternary',
      'enhancedPipPreviewCommentQuinary',
      'enhancedPipPreviewCommentSenary',
    ];
    for (const key of danmakuKeys) {
      expect(patch).toContain(key);
    }
    expect(patch).toContain('font-size: 8px;');
    expect(patch).toContain('line-height: 12px;');
    expect(patch).toContain('white-space: nowrap;');
    expect(patch).not.toContain(
        '<div class="dao-pip-preview-comment"></div>');
    expect(patch).not.toContain(
        '<div class="dao-pip-preview-comment secondary"></div>');
  });

  it('adds a WebUI test for switching the single enhanced PIP preview', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/test/data/webui/settings/dao_page_test.ts.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    expect(patch).toContain('enhancedPipPreviewShowsSelectedModeOnly');
    expect(patch).toContain(
        "page.set('prefs.dao.enhanced_pip_enabled.value', false);");
    expect(patch).toContain('assertTrue(!preview.querySelector(' +
        "'#enhancedPipPreviewOff'));");
    expect(patch).toContain('assertTrue(!preview.querySelector(' +
        "'#enhancedPipPreviewOn'));");
  });
});
