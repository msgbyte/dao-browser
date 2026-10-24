import {readFileSync} from 'node:fs';
import vm from 'node:vm';

import ts from 'typescript';
import {describe, expect, it, vi} from 'vitest';

function createPage() {
  const patch = readFileSync(
      'src/patches/chrome/browser/resources/settings/dao_page/dao_page.ts.patch',
      'utf8');
  const source = patch.split('\n')
      .filter(line => line.startsWith('+') && !line.startsWith('+++'))
      .map(line => line.slice(1)).join('\n')
      .replace(/^import[\s\S]*?;\n/gm, '')
      .replace(/^export /gm, '');
  const output = ts.transpileModule(source, {
    compilerOptions: {target: ts.ScriptTarget.ES2022},
  }).outputText;
  const Page = vm.runInNewContext(output + '; SettingsDaoPageElement', {
    PolymerElement: class {},
    WebUiListenerMixin: (base: unknown) => base,
    afterNextRender: () => {},
    PrefsMixin: (base: unknown) => base,
    SiteSettingsBrowserProxyImpl: {getInstance: () => ({})},
    customElements: {define: () => {}},
    loadTimeData: {getString: (key: string) => key},
  });
  const page = new Page();
  page.daoTabCategories_ = [{id: 'work', name: 'Work'}];
  page.daoTabCategoryName_ = '';
  page.daoTabCategoryEditingId_ = '';
  page.daoTabCategoriesBusy_ = false;
  page.daoTabCategoriesLoaded_ = true;
  page.daoTabCategoryError_ = '';
  page.daoFolderBrowserProxy_ = {
    getCategories: vi.fn().mockResolvedValue(page.daoTabCategories_),
    editCategory: vi.fn().mockResolvedValue(page.daoTabCategories_),
  };
  return page;
}

describe('folder settings behavior', () => {
  it('rejects blank and oversized names before sending a mutation', () => {
    const page = createPage();
    for (const name of ['', ' \t ', 'x'.repeat(101)]) {
      page.daoTabCategoryName_ = name;
      page.onDaoTabCategorySave_();
      expect(page.daoTabCategoryError_).toBe('daoTabCategoriesEditError');
    }
    expect(page.daoFolderBrowserProxy_.editCategory).not.toHaveBeenCalled();
  });

  it('trims names and counts Unicode codepoints rather than UTF-16 units', () => {
    const page = createPage();
    page.daoTabCategoryName_ = ' ' + '🌍'.repeat(100) + ' ';
    page.onDaoTabCategorySave_();
    expect(page.daoFolderBrowserProxy_.editCategory).toHaveBeenCalledWith(
        'add', '', '🌍'.repeat(100));
  });

  it('keeps the editor and previous categories after a failed save', async () => {
    const page = createPage();
    page.daoTabCategoryName_ = 'Research';
    page.daoTabCategoryEditingId_ = 'work';
    page.daoFolderBrowserProxy_.editCategory.mockRejectedValueOnce(new Error());
    await page.editDaoTabCategory_('rename', 'work', 'Research');
    expect(page.daoTabCategoryName_).toBe('Research');
    expect(page.daoTabCategoryEditingId_).toBe('work');
    expect(page.daoTabCategories_).toEqual([{id: 'work', name: 'Work'}]);
    expect(page.daoTabCategoryError_).toBe('daoTabCategoriesEditError');
    expect(page.daoTabCategoriesBusy_).toBe(false);
  });

  it('disables mutations after an unreadable load and permits retry', async () => {
    const page = createPage();
    page.daoFolderBrowserProxy_.getCategories.mockRejectedValueOnce(new Error());
    await page.refreshDaoTabCategories_();
    expect(page.daoTabCategoriesLoaded_).toBe(false);
    await page.editDaoTabCategory_('remove', 'work', '');
    expect(page.daoFolderBrowserProxy_.editCategory).not.toHaveBeenCalled();
    await page.refreshDaoTabCategories_();
    expect(page.daoTabCategoriesLoaded_).toBe(true);
    expect(page.daoTabCategoryError_).toBe('');
  });

  it('ignores a read begun before a newer edit', async () => {
    const page = createPage();
    let resolveRead!: (value: unknown[]) => void;
    page.daoFolderBrowserProxy_.getCategories.mockReturnValueOnce(
        new Promise(resolve => resolveRead = resolve));
    const loading = page.refreshDaoTabCategories_();
    page.daoFolderBrowserProxy_.editCategory.mockResolvedValueOnce([
      {id: 'work', name: 'Research'},
    ]);
    await page.editDaoTabCategory_('rename', 'work', 'Research');
    resolveRead([{id: 'work', name: 'Old'}]);
    await loading;
    expect(page.daoTabCategories_).toEqual([{id: 'work', name: 'Research'}]);
  });

  it('sends only the selected folder identity when removing a category', () => {
    const page = createPage();
    page.onDaoTabCategoryRemove_({currentTarget: {dataset: {id: 'work'}}});
    expect(page.daoFolderBrowserProxy_.editCategory).toHaveBeenCalledWith(
        'remove', 'work', '');
  });

  it('adds an in-place editor chip that saves on Enter after composition', () => {
    const page = createPage();
    page.onDaoTabCategoryAdd_();
    expect(page.daoTabCategoryChips_(
        page.daoTabCategories_, page.daoTabCategoryAdding_)).toEqual([
      {id: 'work', name: 'Work'}, {id: '', name: ''},
    ]);
    expect(page.isDaoTabCategoryEditing_('', page.daoTabCategoryEditingId_))
        .toBe(true);
    expect(page.isDaoTabCategoryEditing_('work', page.daoTabCategoryEditingId_))
        .toBe(false);

    page.daoTabCategoryName_ = 'Research';
    const preventDefault = vi.fn();
    page.onDaoTabCategoryNameKeydown_(
        {key: 'Enter', isComposing: true, preventDefault});
    expect(page.daoFolderBrowserProxy_.editCategory).not.toHaveBeenCalled();
    page.onDaoTabCategoryNameKeydown_(
        {key: 'Enter', isComposing: false, preventDefault});
    expect(page.daoFolderBrowserProxy_.editCategory).toHaveBeenCalledWith(
        'add', '', 'Research');
  });

  it('renames in place and closes the editor on Escape without saving', () => {
    const page = createPage();
    page.onDaoTabCategoryAdd_();
    page.onDaoTabCategoryRename_({currentTarget: {dataset: {id: 'work'}}});
    expect(page.daoTabCategoryAdding_).toBe(false);
    expect(page.daoTabCategoryName_).toBe('Work');
    expect(page.isDaoTabCategoryEditing_('work', page.daoTabCategoryEditingId_))
        .toBe(true);

    page.onDaoTabCategoryNameKeydown_(
        {key: 'Escape', isComposing: false, preventDefault: vi.fn()});
    expect(page.daoTabCategoryEditingId_).toBe('');
    expect(page.daoTabCategoryChips_(
        page.daoTabCategories_, page.daoTabCategoryAdding_)).toEqual([
      {id: 'work', name: 'Work'},
    ]);
    expect(page.daoFolderBrowserProxy_.editCategory).not.toHaveBeenCalled();
  });
});

describe('stale tab expiration settings behavior', () => {
  it('keeps custom mode until a preset is chosen', () => {
    const page = createPage();
    page.staleTabExpirationCustom_ = false;
    page.onStaleTabExpirationPrefChanged_(120);
    expect(page.staleTabExpirationCustom_).toBe(false);
    page.onStaleTabExpirationPrefChanged_(36);
    expect(page.staleTabExpirationCustom_).toBe(true);
    page.onStaleTabExpirationPrefChanged_(72);
    expect(page.staleTabExpirationCustom_).toBe(true);
    expect(page.staleTabExpirationPresetPressed_(72, 72, true)).toBe('false');

    page.$ = {staleTabExpirationHours: {value: '0'}};
    page.setPrefValue = vi.fn();
    page.staleTabExpirationHoursInvalid_ = true;
    page.onStaleTabExpirationPresetClick_(
        {currentTarget: {dataset: {hours: '168'}}});
    expect(page.staleTabExpirationCustom_).toBe(false);
    expect(page.staleTabExpirationHoursInvalid_).toBe(false);
    expect(page.$.staleTabExpirationHours.value).toBe('168');
    expect(page.setPrefValue).toHaveBeenCalledWith(
        'dao.stale_tab_expiration_hours', 168);
    expect(page.staleTabExpirationPresetPressed_(168, 168, false)).toBe('true');
  });
});
