import {readFileSync} from 'node:fs';

import {describe, expect, it} from 'vitest';

describe('Dao update i18n wiring', () => {
  it('localizes the macOS Check for Updates menu item', () => {
    const mainMenuPatch = readFileSync(
        'src/patches/chrome/browser/ui/cocoa/main_menu_builder.mm.patch',
        'utf8');
    const grdText = readFileSync(
        'src/dao/browser/strings/dao_strings.grd', 'utf8');
    const zhCnText = readFileSync(
        'src/dao/browser/strings/translations/dao_strings_zh-CN.xtb',
        'utf8');

    expect(grdText).toContain(
        '<message name="IDS_DAO_CHECK_FOR_UPDATES_MENU"');
    expect(zhCnText).toContain(
        '<translation id="7532108947552493901">检查更新...</translation>');
    expect(mainMenuPatch).toContain(
        '#include "dao/browser/strings/grit/dao_strings.h"');
    expect(mainMenuPatch).toContain(
        'l10n_util::GetNSString(IDS_DAO_CHECK_FOR_UPDATES_MENU)');
    expect(mainMenuPatch).not.toContain('initWithTitle:@"Check for Updates..."');
  });

  it('localizes the macOS settings Check for updates button', () => {
    const settingsPatch = readFileSync(
        'src/patches/chrome/browser/ui/webui/settings/' +
            'settings_localized_strings_provider.cc.patch',
        'utf8');
    const grdText = readFileSync(
        'src/dao/browser/strings/dao_strings.grd', 'utf8');
    const zhCnText = readFileSync(
        'src/dao/browser/strings/translations/dao_strings_zh-CN.xtb',
        'utf8');

    expect(grdText).toContain(
        '<message name="IDS_DAO_CHECK_FOR_UPDATES_BUTTON"');
    expect(zhCnText).toContain(
        '<translation id="7717845620320228976">检查更新</translation>');
    expect(settingsPatch).toContain(
        '#include "dao/browser/strings/grit/dao_strings.h"');
    expect(settingsPatch).toContain(
        '{"aboutDaoCheckForUpdates", IDS_DAO_CHECK_FOR_UPDATES_BUTTON}');
    expect(settingsPatch).not.toContain(
        'html_source->AddString("aboutDaoCheckForUpdates", u"Check for updates")');
  });
});
