// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {readFileSync} from 'node:fs';

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  SIDEBAR_POINTER_EXITED_EVENT,
} from '../sidebar_bridge.js';
import {FolderModel} from '../dao_folder_model.js';
import type {
  PinnedItemData,
  SidebarState,
  TabData,
  UpdateStateData,
} from '../sidebar_bridge.js';

const ARCHIVED_TOAST_TEXT = 'Archived tabs inactive for the past 24 hours';
const NO_NEW_TABS_TOAST_TEXT = 'No new tabs were archived';

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

vi.mock('../dao_media_control.js', () => {
  if (!customElements.get('dao-media-control')) {
    customElements.define('dao-media-control', class extends HTMLElement {});
  }
  return {};
});

function pinnedItem(extra: Partial<PinnedItemData> = {}): PinnedItemData {
  return {
    id: 'pin-1',
    state: 'open',
    title: 'Pinned',
    url: 'https://example.com/',
    faviconUrl: '',
    isOpen: true,
    openTabIndex: 0,
    isActive: false,
    ...extra,
  };
}

function tab(extra: Partial<TabData> = {}): TabData {
  return {
    tabId: 'tab-1',
    index: 0,
    title: 'Docs',
    url: 'https://docs.example/',
    faviconUrl: '',
    isActive: false,
    isPinned: false,
    isAudible: false,
    isMuted: false,
    ...extra,
  };
}

function sidebarState(extra: Partial<SidebarState> = {}): SidebarState {
  return {
    pinnedItems: [],
    pinnedTabs: [],
    unpinnedTabs: [],
    activeIndex: 0,
    sessionId: 7,
    ...extra,
  };
}

type SidebarAppInternals = HTMLElement & {
  folderModel_: FolderModel;
  foldersLoaded_: boolean;
  unpinnedTabs_: TabData[];
  folderModelVersion_: number;
  updateComplete: Promise<boolean>;
};

function installFolderModel(
    el: SidebarAppInternals, json: string = ''): FolderModel {
  const model = new FolderModel();
  model.loadFromJson(json);
  el.folderModel_ = model;
  el.foldersLoaded_ = true;
  return model;
}

function fireMoveStaleTabsRequested() {
  (window as unknown as {
    cr: {webUIListenerCallback: (event: string) => void};
  }).cr.webUIListenerCallback('moveStaleTabsRequested');
}

function fireFolderContextMenuCommand(folderId: string, command: string) {
  (window as unknown as {
    cr: {
      webUIListenerCallback:
          (event: string, folderId: string, command: string) => void;
    };
  }).cr.webUIListenerCallback('folderContextMenuCommand', folderId, command);
}

function didSendNative(send: ReturnType<typeof vi.fn>, method: string): boolean {
  return send.mock.calls.some(call => call[0] === method);
}

function installLoadTimeData() {
  const strings: Record<string, string> = {
    daoSidebarStaleTabsArchivedToast: ARCHIVED_TOAST_TEXT,
    daoSidebarNoNewStaleTabsArchivedToast: NO_NEW_TABS_TOAST_TEXT,
  };
  const getString = vi.fn((id: string) => strings[id] ?? id);
  (globalThis as unknown as {
    loadTimeData: {getString: (id: string) => string};
  }).loadTimeData = {getString};
  return getString;
}

async function loadApp() {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  installLoadTimeData();
  await import('../dao_sidebar_app.js');
  const el = document.createElement('dao-sidebar-app') as HTMLElement & {
    pinnedItems_: PinnedItemData[];
    autoScrollTabId_: string;
    autoScrollToken_: number;
    updateState_: UpdateStateData | null;
    tabScrollbarVisible_: boolean;
    tabScrollbarHovered_: boolean;
    updateComplete: Promise<boolean>;
  };
  document.body.appendChild(el);
  return {el, send};
}

describe('dao-sidebar-app', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    vi.useRealTimers();
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
    delete (globalThis as unknown as {loadTimeData?: unknown}).loadTimeData;
  });

  it('renders pinned tabs above the new tab button', async () => {
    const {el} = await loadApp();
    el.pinnedItems_ = [pinnedItem()];
    await el.updateComplete;

    const children = Array.from(
        el.shadowRoot!.querySelector('.sidebar-content')!.children);
    const pinnedIndex = children.findIndex(
        child => child.tagName.toLowerCase() === 'dao-pinned-tabs-grid');
    const newTabIndex = children.findIndex(
        child => child.tagName.toLowerCase() === 'dao-new-tab-button');

    expect(pinnedIndex).toBeGreaterThanOrEqual(0);
    expect(newTabIndex).toBeGreaterThanOrEqual(0);
    expect(pinnedIndex).toBeLessThan(newTabIndex);
  });

  it('requests the initial sidebar and update states on connect', async () => {
    const {send} = await loadApp();

    expect(send).toHaveBeenCalledWith('getInitialState', []);
    expect(send).toHaveBeenCalledWith('requestUpdateState', []);
  });

  it('creates a one-shot scroll intent from sidebar state', async () => {
    const {el} = await loadApp();
    await el.updateComplete;

    (window as unknown as {
      cr: {webUIListenerCallback: (event: string, state: SidebarState) => void};
    }).cr.webUIListenerCallback('sidebarStateChanged', sidebarState({
      unpinnedTabs: [tab({tabId: 'tab-a', isActive: true})],
      scrollTargetTabId: 'tab-a',
    } as Partial<SidebarState>));
    await el.updateComplete;

    expect(el.autoScrollTabId_).toBe('tab-a');
    expect(el.autoScrollToken_).toBe(1);
  });

  it('does not refresh the scroll token when sidebar state has no target',
      async () => {
        const {el} = await loadApp();
        await el.updateComplete;

        (window as unknown as {
          cr: {
            webUIListenerCallback: (event: string, state: SidebarState) => void
          };
        }).cr.webUIListenerCallback('sidebarStateChanged', sidebarState({
          unpinnedTabs: [tab({tabId: 'tab-a', isActive: true})],
          scrollTargetTabId: 'tab-a',
        } as Partial<SidebarState>));
        await el.updateComplete;

        (window as unknown as {
          cr: {
            webUIListenerCallback: (event: string, state: SidebarState) => void
          };
        }).cr.webUIListenerCallback('sidebarStateChanged', sidebarState({
          unpinnedTabs: [tab({tabId: 'tab-a', isActive: true})],
        }));
        await el.updateComplete;

        expect(el.autoScrollTabId_).toBe('');
        expect(el.autoScrollToken_).toBe(1);
      });

  it('renders the update button before the plus menu at the toolbar end', async () => {
    const {el} = await loadApp();
    el.updateState_ = {
      state: 'ready',
      displayVersion: '1.2.3',
      label: 'Update',
      applyingLabel: 'Applying',
    };
    await el.updateComplete;

    const actions = el.shadowRoot!.querySelector('.toolbar-end-actions')!;
    const children = Array.from(actions.children);

    expect(children).toHaveLength(2);
    expect(children[0]!.tagName.toLowerCase()).toBe('dao-update-button');
    expect(children[1]!.classList.contains('plus-menu-container')).toBe(true);
  });

  it('replaces the native tab scrollbar with a thin overlay thumb', async () => {
    const {DaoSidebarApp} = await import('../dao_sidebar_app.js');
    const styles = (DaoSidebarApp as unknown as {
      styles: {strings: TemplateStringsArray};
    }).styles;
    const cssText = Array.from(styles.strings).join('');

    expect(cssText).toMatch(
        /\.tab-section-shell\s*{[^}]*position:\s*relative;/s);
    expect(cssText).toMatch(
        /\.tab-section\s*{[^}]*scrollbar-width:\s*none;/s);
    expect(cssText).toMatch(
        /\.tab-section::-webkit-scrollbar\s*{[^}]*display:\s*none;/s);
    expect(cssText).toMatch(
        /\.tab-scrollbar\s*{[^}]*right:\s*0;[^}]*width:\s*4px;/s);
    expect(cssText).toMatch(
        /\.tab-scrollbar-thumb\s*{[^}]*width:\s*4px;/s);
    expect(cssText).toMatch(
        /\.tab-scrollbar\.visible\.hovered\s*{[^}]*opacity:\s*1;/s);

    const {el} = await loadApp();
    await el.updateComplete;

    expect(el.shadowRoot!.querySelector('.tab-section-shell')).not.toBeNull();
    expect(el.shadowRoot!.querySelector('.tab-scrollbar')).not.toBeNull();
    expect(el.shadowRoot!.querySelector('.tab-scrollbar-thumb')).not.toBeNull();
  });

  it('hides the overlay scrollbar when the pointer leaves the sidebar window', async () => {
    const {el} = await loadApp();
    el.tabScrollbarVisible_ = true;
    el.tabScrollbarHovered_ = true;
    await el.updateComplete;

    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).not.toBeNull();

    window.dispatchEvent(new MouseEvent('mouseout', {
      bubbles: true,
      relatedTarget: null,
    }));
    await el.updateComplete;

    expect(el.tabScrollbarHovered_).toBe(false);
    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).toBeNull();
  });

  it('clears stale overlay scrollbar hover without a leave event', async () => {
    vi.useFakeTimers();

    const {el} = await loadApp();
    await el.updateComplete;

    const shell = el.shadowRoot!.querySelector('.tab-section-shell')!;
    shell.dispatchEvent(new Event('pointerenter'));
    await el.updateComplete;

    expect(el.tabScrollbarHovered_).toBe(true);
    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).not.toBeNull();

    vi.advanceTimersByTime(900);
    await el.updateComplete;

    expect(el.tabScrollbarHovered_).toBe(false);
    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).toBeNull();
  });

  it('hides the overlay scrollbar when the host sidebar exits', async () => {
    const {el} = await loadApp();
    el.tabScrollbarVisible_ = true;
    el.tabScrollbarHovered_ = true;
    await el.updateComplete;

    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).not.toBeNull();

    (window as unknown as {
      cr: {webUIListenerCallback: (event: string) => void};
    }).cr.webUIListenerCallback('sidebarPointerExited');
    await el.updateComplete;

    expect(el.tabScrollbarHovered_).toBe(false);
    expect(el.shadowRoot!.querySelector('.tab-scrollbar.hovered')).toBeNull();
  });

  it('broadcasts a DOM pointer-exit event when the host sidebar exits',
      async () => {
        await loadApp();
        const listener = vi.fn();
        window.addEventListener(SIDEBAR_POINTER_EXITED_EVENT, listener);

        (window as unknown as {
          cr: {webUIListenerCallback: (event: string) => void};
        }).cr.webUIListenerCallback('sidebarPointerExited');

        expect(listener).toHaveBeenCalledTimes(1);
        window.removeEventListener(SIDEBAR_POINTER_EXITED_EVENT, listener);
      });

  it('does not create stale folder or save folders when no tabs match',
      async () => {
        vi.useFakeTimers();
        vi.spyOn(Date, 'now').mockReturnValue(1_800_000_000_000);

        const {el, send} = await loadApp();
        const app = el as SidebarAppInternals;
        installFolderModel(app);
        app.unpinnedTabs_ = [
          tab({
            tabId: 'fresh',
            title: 'Fresh',
            url: 'https://fresh.example/',
            lastActiveTimeMs: Date.now() - 23 * 60 * 60 * 1000,
          }),
        ];

        fireMoveStaleTabsRequested();
        await el.updateComplete;
        vi.advanceTimersByTime(300);

        expect(app.folderModel_.getFolders()).toEqual([]);
        expect(didSendNative(send, 'saveFolders')).toBe(false);
        const toast = el.shadowRoot!.querySelector('.dao-sidebar-toast');
        expect(toast).not.toBeNull();
        expect(toast!.textContent).toContain(NO_NEW_TABS_TOAST_TEXT);
      });

  it('shows no-new-tabs toast when stale tabs are already archived',
      async () => {
        vi.useFakeTimers();
        vi.spyOn(Date, 'now').mockReturnValue(1_800_000_000_000);

        const {el, send} = await loadApp();
        const app = el as SidebarAppInternals;
        installFolderModel(app, JSON.stringify({
          version: 1,
          items: [
            {
              type: 'folder',
              id: 'stale-folder',
              name: 'stale',
              collapsed: false,
              children: [
                {
                  type: 'tab',
                  tabId: 'old',
                  url: 'https://old.example/',
                  title: 'Old',
                },
              ],
            },
          ],
        }));
        app.unpinnedTabs_ = [
          tab({
            tabId: 'old',
            title: 'Old',
            url: 'https://old.example/',
            lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
          }),
        ];
        app.folderModel_.reconcile(app.unpinnedTabs_);

        fireMoveStaleTabsRequested();
        await el.updateComplete;
        vi.advanceTimersByTime(300);

        expect(didSendNative(send, 'saveFolders')).toBe(false);
        const toast = el.shadowRoot!.querySelector('.dao-sidebar-toast');
        expect(toast).not.toBeNull();
        expect(toast!.textContent).toContain(NO_NEW_TABS_TOAST_TEXT);
      });

  it('moves only ordinary stale tabs into a new stale folder', async () => {
    vi.useFakeTimers();
    vi.spyOn(Date, 'now').mockReturnValue(1_800_000_000_000);

    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app);
    app.unpinnedTabs_ = [
      tab({
        tabId: 'old',
        title: 'Old',
        url: 'https://old.example/',
        lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
      }),
      tab({
        tabId: 'fresh',
        title: 'Fresh',
        url: 'https://fresh.example/',
        lastActiveTimeMs: Date.now() - 23 * 60 * 60 * 1000,
      }),
      tab({
        tabId: 'active-old',
        title: 'Active Old',
        url: 'https://active.example/',
        isActive: true,
        lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
      }),
      tab({
        tabId: 'pinned-old',
        title: 'Pinned Old',
        url: 'https://pinned.example/',
        isPinned: true,
        lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
      }),
      tab({
        tabId: 'audible-old',
        title: 'Audible Old',
        url: 'https://audible.example/',
        isAudible: true,
        lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
      }),
      tab({
        tabId: 'muted-old',
        title: 'Muted Old',
        url: 'https://muted.example/',
        isMuted: true,
        lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
      }),
      tab({
        tabId: 'locked-old',
        title: 'Locked Old',
        url: 'https://locked.example/',
        isAgentLocked: true,
        lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
      }),
      tab({
        tabId: 'missing-time',
        title: 'Missing Time',
        url: 'https://missing.example/',
      }),
    ];
    app.folderModel_.reconcile(app.unpinnedTabs_);

    fireMoveStaleTabsRequested();
    await el.updateComplete;
    vi.advanceTimersByTime(300);

    const stale = app.folderModel_.findFolderByName('stale');
    expect(stale?.children.map(child => child.tabId)).toEqual(['old']);
    expect(send).toHaveBeenCalledWith(
        'saveFolders', [expect.stringContaining('"name": "stale"')]);
    const toast = el.shadowRoot!.querySelector('.dao-sidebar-toast');
    expect(toast).not.toBeNull();
    expect(toast!.textContent).toContain(ARCHIVED_TOAST_TEXT);
  });

  it('loads WebUI strings before the sidebar app bundle', () => {
    const htmlText = readFileSync(
        'src/dao/browser/ui/webui/resources/sidebar/sidebar.html', 'utf8');

    const stringsIndex = htmlText.indexOf('src="strings.m.js"');
    const appIndex = htmlText.indexOf('src="sidebar.js"');

    expect(stringsIndex).toBeGreaterThanOrEqual(0);
    expect(appIndex).toBeGreaterThanOrEqual(0);
    expect(stringsIndex).toBeLessThan(appIndex);
  });

  it('does not hardcode localized stale-tab toast copy in the app source',
      () => {
        const sourceText = readFileSync(
            'src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts',
            'utf8');

        expect(sourceText).not.toMatch(/[\u4e00-\u9fff]/);
      });

  it('localizes regular tab context menu labels in the native handler', () => {
    const handlerText = readFileSync(
        'src/dao/browser/ui/webui/dao_sidebar_ui.cc', 'utf8');
    const grdText = readFileSync(
        'src/dao/browser/strings/dao_strings.grd', 'utf8');
    const zhCnText = readFileSync(
        'src/dao/browser/strings/translations/dao_strings_zh-CN.xtb',
        'utf8');

    const menuLabels = [
      [
        'kDuplicateTab',
        'IDS_DAO_TAB_CONTEXT_DUPLICATE_TAB',
        '3007771295016901659',
      ],
      ['kCopyLink', 'IDS_DAO_TAB_CONTEXT_COPY_LINK', '8717864919010420084'],
      ['kToggleMute', 'IDS_DAO_TAB_CONTEXT_MUTE_SITE', '2973324205039581528'],
      [
        'kToggleMute',
        'IDS_DAO_TAB_CONTEXT_UNMUTE_SITE',
        '1293177648337752319',
      ],
      ['kCloseTab', 'IDS_DAO_TAB_CONTEXT_CLOSE_TAB', '3551320343578183772'],
      [
        'kCloseDuplicateTabs',
        'IDS_DAO_TAB_CONTEXT_CLOSE_DUPLICATE_TABS',
        '2703180365855369896',
      ],
      [
        'kCloseOtherTabs',
        'IDS_DAO_TAB_CONTEXT_CLOSE_OTHER_TABS',
        '4322394346347055525',
      ],
      [
        'kCloseTabsAbove',
        'IDS_DAO_TAB_CONTEXT_CLOSE_TABS_ABOVE',
        '5450299006531484229',
      ],
      [
        'kCloseTabsBelow',
        'IDS_DAO_TAB_CONTEXT_CLOSE_TABS_BELOW',
        '5611474372949142946',
      ],
    ];

    for (const [commandId, messageId, translationId] of menuLabels) {
      expect(grdText).toContain(`<message name="${messageId}"`);
      expect(zhCnText).toContain(`<translation id="${translationId}">`);
      expect(handlerText).toMatch(new RegExp(
          `${commandId}[\\s\\S]*?l10n_util::GetStringUTF16\\(\\s*` +
          `${messageId}\\s*\\)`));
    }
  });

  it('maps native tab context menu items to registered accelerators', () => {
    const handlerText = readFileSync(
        'src/dao/browser/ui/webui/dao_sidebar_ui.cc', 'utf8');

    const acceleratorMappings = [
      ['kDuplicateTab', 'IDC_DAO_DUPLICATE_TAB'],
      ['kCopyLink', 'IDC_DAO_COPY_URL'],
      ['kCloseTab', 'IDC_CLOSE_TAB'],
    ];

    for (const [menuCommand, browserCommand] of acceleratorMappings) {
      expect(handlerText).toMatch(new RegExp(
          `case ${menuCommand}:[\\s\\S]*?browser_command = ` +
          `${browserCommand};`));
    }
    expect(handlerText).toContain('AcceleratorProviderForBrowser(browser_)');
    expect(handlerText).toMatch(
        /for \(int command_id : \{kDuplicateTab, kCopyLink, kCloseTab\}\)/);
    expect(handlerText).toContain('SetForceShowAcceleratorForItemAt');
  });

  it('localizes folder context menu labels in the native handler', () => {
    const handlerText = readFileSync(
        'src/dao/browser/ui/webui/dao_sidebar_ui.cc', 'utf8');
    const grdText = readFileSync(
        'src/dao/browser/strings/dao_strings.grd', 'utf8');
    const zhCnText = readFileSync(
        'src/dao/browser/strings/translations/dao_strings_zh-CN.xtb',
        'utf8');

    const menuLabels = [
      [
        'kFolderRename',
        'IDS_DAO_FOLDER_CONTEXT_RENAME',
        '1173894706177603556',
      ],
      [
        'kFolderDelete',
        'IDS_DAO_FOLDER_CONTEXT_DELETE',
        '1809939268435598390',
      ],
    ];

    for (const [commandId, messageId, translationId] of menuLabels) {
      expect(grdText).toContain(`<message name="${messageId}"`);
      expect(zhCnText).toContain(`<translation id="${translationId}">`);
      expect(handlerText).toMatch(new RegExp(
          `${commandId}[\\s\\S]*?l10n_util::GetStringUTF16\\(\\s*` +
          `${messageId}\\s*\\)`));
    }
  });

  it('starts folder rename when native folder menu selects rename',
      async () => {
        const {el} = await loadApp();
        const app = el as SidebarAppInternals;
        installFolderModel(app, JSON.stringify({
          version: 1,
          items: [{
            type: 'folder',
            id: 'folder-1',
            name: 'Work',
            collapsed: false,
            children: [],
          }],
        }));
        await el.updateComplete;

        const tabList = el.shadowRoot!.querySelector('dao-tab-list') as
            HTMLElement & {startFolderRename: (folderId: string) => void};
        const startFolderRename = vi.fn();
        tabList.startFolderRename = startFolderRename;

        fireFolderContextMenuCommand('folder-1', 'rename');

        expect(startFolderRename).toHaveBeenCalledWith('folder-1');
      });

  it('deletes folder when native folder menu selects delete', async () => {
    vi.useFakeTimers();

    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'folder-1',
        name: 'Work',
        collapsed: false,
        children: [],
      }],
    }));

    fireFolderContextMenuCommand('folder-1', 'delete');
    vi.advanceTimersByTime(300);

    expect(app.folderModel_.findFolderByName('Work')).toBeNull();
    expect(send).toHaveBeenCalledWith(
        'saveFolders', [expect.not.stringContaining('"name": "Work"')]);
  });

  it('reuses existing stale folder and moves stale tabs from other folders',
      async () => {
        vi.useFakeTimers();
        vi.spyOn(Date, 'now').mockReturnValue(1_800_000_000_000);

        const {el, send} = await loadApp();
        const app = el as SidebarAppInternals;
        installFolderModel(app, JSON.stringify({
          version: 1,
          items: [
            {
              type: 'folder',
              id: 'reading',
              name: 'Reading',
              collapsed: false,
              children: [
                {
                  type: 'tab',
                  url: 'https://old.example/',
                  title: 'Old',
                },
              ],
            },
            {
              type: 'folder',
              id: 'stale-folder',
              name: 'stale',
              collapsed: true,
              children: [
                {
                  type: 'tab',
                  url: 'https://already.example/',
                  title: 'Already',
                },
              ],
            },
          ],
        }));
        app.unpinnedTabs_ = [
          tab({
            tabId: 'old',
            title: 'Old',
            url: 'https://old.example/',
            lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
          }),
          tab({
            tabId: 'already',
            title: 'Already',
            url: 'https://already.example/',
            lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
          }),
        ];
        app.folderModel_.reconcile(app.unpinnedTabs_);

        fireMoveStaleTabsRequested();
        await el.updateComplete;
        vi.advanceTimersByTime(300);

        const stale = app.folderModel_.findFolderByName('stale');
        expect(stale?.id).toBe('stale-folder');
        expect(stale?.collapsed).toBe(false);
        expect(stale?.children.map(child => child.title))
            .toEqual(['Already', 'Old']);
        expect(app.folderModel_.findFolderByName('Reading')?.children)
            .toEqual([]);
        expect(send).toHaveBeenCalledWith(
            'saveFolders', [expect.stringContaining('"id": "stale-folder"')]);
      });

  it('hides page-level sidebar scrollbars until the page is hovered', () => {
    const cssText = readFileSync(
        'src/dao/browser/ui/webui/resources/sidebar/sidebar.css', 'utf8');

    expect(cssText).toMatch(/scrollbar-width:\s*none;/);
    expect(cssText).toMatch(/::-webkit-scrollbar\s*{[^}]*display:\s*none;/s);
  });
});
