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

const ARCHIVED_TOAST_TEXT = 'Archived inactive tabs';
const NO_NEW_TABS_TOAST_TEXT = 'No new tabs were archived';
const {loadTimeDataGetString} = vi.hoisted(() => ({
  loadTimeDataGetString: vi.fn(),
}));

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('./lit_test_shim.js');
});

vi.mock('//resources/js/load_time_data.js', () => ({
  loadTimeData: {
    getString: loadTimeDataGetString,
  },
}));

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
  initialStateReceived_: boolean;
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

function fireMoveStaleTabsRequested(expirationHours?: unknown) {
  (window as unknown as {
    cr: {
      webUIListenerCallback:
          (event: string, expirationHours?: unknown) => void;
    };
  }).cr.webUIListenerCallback('moveStaleTabsRequested', expirationHours);
}

function fireSidebarStateChanged(state: SidebarState) {
  (window as unknown as {
    cr: {
      webUIListenerCallback:
          (event: string, state: SidebarState) => void;
    };
  }).cr.webUIListenerCallback('sidebarStateChanged', state);
}

function fireFolderContextMenuCommand(folderId: string, command: string) {
  (window as unknown as {
    cr: {
      webUIListenerCallback:
          (event: string, folderId: string, command: string) => void;
    };
  }).cr.webUIListenerCallback('folderContextMenuCommand', folderId, command);
}

function fireDeleteFolderConfirmed(folderId: string) {
  fireFolderContextMenuCommand(folderId, 'deleteConfirmed');
}

function didSendNative(send: ReturnType<typeof vi.fn>, method: string): boolean {
  return send.mock.calls.some(call => call[0] === method);
}

function installLoadTimeData() {
  const strings: Record<string, string> = {
    daoSidebarStaleTabsArchivedToast: ARCHIVED_TOAST_TEXT,
    daoSidebarNoNewStaleTabsArchivedToast: NO_NEW_TABS_TOAST_TEXT,
  };
  loadTimeDataGetString.mockImplementation(
      (id: string) => strings[id] ?? id);
  (globalThis as unknown as {
    loadTimeData: {getString: (id: string) => string};
  }).loadTimeData = {getString: loadTimeDataGetString};
  return loadTimeDataGetString;
}

async function loadApp() {
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  installLoadTimeData();
  await import('../dao_sidebar_app.js');
  const appClass = customElements.get('dao-sidebar-app') as typeof HTMLElement & {
    invokeLifecycleCallbacksForTesting?: boolean;
  };
  appClass.invokeLifecycleCallbacksForTesting = true;
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

  it('reports exact stale tab ids to the native handler', async () => {
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    const staleTab = tab({tabId: 'tab-stale'});
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'folder-stale',
        name: 'stale',
        collapsed: true,
        children: [{
          type: 'tab',
          tabId: staleTab.tabId,
          url: staleTab.url,
          title: staleTab.title,
        }],
      }],
    }));
    app.unpinnedTabs_ = [staleTab, tab({tabId: 'tab-regular'})];
    app.folderModelVersion_++;
    await app.updateComplete;

    expect(send).toHaveBeenCalledWith('setStaleTabIds', [['tab-stale']]);
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

  it('expands the folder containing a newly active tab', async () => {
    vi.useFakeTimers();
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    const folderTab = tab({
      tabId: 'tab-a',
      index: 3,
      title: 'A',
      isActive: false,
    });
    const looseTab = tab({
      tabId: 'tab-b',
      index: 4,
      title: 'B',
      isActive: true,
    });
    const model = installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'folder-work',
        name: 'Work',
        collapsed: true,
        children: [{
          type: 'tab',
          tabId: folderTab.tabId,
          url: folderTab.url,
          title: folderTab.title,
        }],
      }],
    }));
    fireSidebarStateChanged(sidebarState({
      activeIndex: looseTab.index,
      unpinnedTabs: [folderTab, looseTab],
    }));
    await app.updateComplete;

    const loadFoldersCall =
        send.mock.calls.find(call => call[0] === 'loadFolders');
    expect(loadFoldersCall).toBeDefined();
    const callbackId = loadFoldersCall![1][0] as string;
    (window as unknown as {
      cr: {
        webUIListenerCallback:
            (event: string, folderJson: string) => void;
      };
    }).cr.webUIListenerCallback(callbackId, model.toJson());
    await Promise.resolve();
    await app.updateComplete;

    const previousVersion = app.folderModelVersion_;
    const previousScrollToken = el.autoScrollToken_;

    fireSidebarStateChanged(sidebarState({
      activeIndex: folderTab.index,
      unpinnedTabs: [
        {...folderTab, isActive: true},
        {...looseTab, isActive: false},
      ],
    }));
    await app.updateComplete;

    expect(model.getFolders()[0]!.collapsed).toBe(false);
    expect(el.autoScrollTabId_).toBe(folderTab.tabId);
    expect(el.autoScrollToken_).toBe(previousScrollToken + 1);
    expect(app.folderModelVersion_).toBe(previousVersion + 1);

    vi.advanceTimersByTime(300);
    expect(send).toHaveBeenCalledWith(
        'saveFolders', [expect.stringContaining('"collapsed": false')]);
  });

  it('expands an active tab folder after folder loading completes', async () => {
    vi.useFakeTimers();
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    const folderTab = tab({
      tabId: 'tab-a',
      index: 3,
      title: 'A',
      isActive: false,
    });
    const looseTab = tab({
      tabId: 'tab-b',
      index: 4,
      title: 'B',
      isActive: true,
    });

    fireSidebarStateChanged(sidebarState({
      activeIndex: looseTab.index,
      unpinnedTabs: [folderTab, looseTab],
    }));
    await app.updateComplete;

    fireSidebarStateChanged(sidebarState({
      activeIndex: folderTab.index,
      unpinnedTabs: [
        {...folderTab, isActive: true},
        {...looseTab, isActive: false},
      ],
    }));
    await app.updateComplete;

    const loadFoldersCall =
        send.mock.calls.find(call => call[0] === 'loadFolders');
    expect(loadFoldersCall).toBeDefined();
    const callbackId = loadFoldersCall![1][0] as string;
    const folderJson = JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'folder-work',
        name: 'Work',
        collapsed: true,
        children: [{
          type: 'tab',
          tabId: folderTab.tabId,
          url: folderTab.url,
          title: folderTab.title,
        }],
      }],
    });
    (window as unknown as {
      cr: {
        webUIListenerCallback:
            (event: string, folderJson: string) => void;
      };
    }).cr.webUIListenerCallback(callbackId, folderJson);
    await Promise.resolve();
    await app.updateComplete;

    expect(app.folderModel_.getFolders()[0]!.collapsed).toBe(false);
    expect(el.autoScrollTabId_).toBe(folderTab.tabId);
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

  it('uses the configured stale-tab expiration from the native event',
      async () => {
        vi.spyOn(Date, 'now').mockReturnValue(1_800_000_000_000);

        const {el} = await loadApp();
        const app = el as SidebarAppInternals;
        installFolderModel(app);
        app.unpinnedTabs_ = [
          tab({
            tabId: '25-hours-old',
            lastActiveTimeMs: Date.now() - 25 * 60 * 60 * 1000,
          }),
          tab({
            tabId: '49-hours-old',
            lastActiveTimeMs: Date.now() - 49 * 60 * 60 * 1000,
          }),
        ];

        fireMoveStaleTabsRequested(48);
        await el.updateComplete;

        expect(app.folderModel_.findFolderByName('stale')?.children
            .map(child => child.tabId)).toEqual(['49-hours-old']);
      });

  it.each([
    {payload: 0, ageHours: 1, shouldArchive: false},
    {payload: 721, ageHours: 25, shouldArchive: true},
    {payload: 1.5, ageHours: 2, shouldArchive: false},
    {payload: '48', ageHours: 25, shouldArchive: true},
    {payload: undefined, ageHours: 25, shouldArchive: true},
  ])(
      'falls back to 24 hours for invalid expiration payload $payload',
      async ({payload, ageHours, shouldArchive}) => {
        vi.spyOn(Date, 'now').mockReturnValue(1_800_000_000_000);

        const {el} = await loadApp();
        const app = el as SidebarAppInternals;
        installFolderModel(app);
        app.unpinnedTabs_ = [
          tab({
            tabId: 'candidate',
            lastActiveTimeMs: Date.now() - ageHours * 60 * 60 * 1000,
          }),
        ];

        fireMoveStaleTabsRequested(payload);
        await el.updateComplete;

        const archivedTabIds =
            app.folderModel_.findFolderByName('stale')?.children
                .map(child => child.tabId) ?? [];
        expect(archivedTabIds).toEqual(
            shouldArchive ? ['candidate'] : []);
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

  it('offers Copy Tab ID only for eligible profiles while MCP is enabled', () => {
    const handlerText = readFileSync(
        'src/dao/browser/ui/webui/dao_sidebar_ui.cc', 'utf8');
    const headerText = readFileSync(
        'src/dao/browser/ui/webui/dao_sidebar_ui.h', 'utf8');
    const grdText = readFileSync(
        'src/dao/browser/strings/dao_strings.grd', 'utf8');
    const zhCnText = readFileSync(
        'src/dao/browser/strings/translations/dao_strings_zh-CN.xtb',
        'utf8');

    expect(headerText).toContain('kCopyTabId');
    expect(handlerText).toMatch(
        /GetBoolean\(prefs::kDaoMcpServerEnabled\)[\s\S]*?!browser_->profile\(\)->IsOffTheRecord\(\)[\s\S]*?!browser_->profile\(\)->IsGuestSession\(\)[\s\S]*?AddItem\(\s*kCopyTabId/);
    expect(handlerText).toMatch(
        /case kCopyTabId:[\s\S]*?GetSidebarTabId\(contents\)[\s\S]*?WriteText/);
    expect(handlerText).toContain('IDS_DAO_TAB_ID_COPIED_TOAST');
    expect(grdText).toContain('IDS_DAO_TAB_CONTEXT_COPY_TAB_ID');
    expect(grdText).toContain('IDS_DAO_TAB_ID_COPIED_TOAST');
    expect(zhCnText).toContain('复制 Tab ID');
    expect(zhCnText).toContain('已复制 Tab ID');
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
        '重命名',
      ],
      [
        'kFolderUnfolder',
        'IDS_DAO_FOLDER_CONTEXT_UNFOLDER',
        '解散文件夹',
      ],
      [
        'kFolderDelete',
        'IDS_DAO_FOLDER_CONTEXT_DELETE',
        '删除文件夹',
      ],
    ];

    for (const [commandId, messageId, translation] of menuLabels) {
      expect(grdText).toContain(`<message name="${messageId}"`);
      expect(zhCnText).toContain(`>${translation}</translation>`);
      expect(handlerText).toMatch(new RegExp(
          `${commandId}[\\s\\S]*?l10n_util::GetStringUTF16\\(\\s*` +
          `${messageId}\\s*\\)`));
    }

    const dialogStrings = [
      ['IDS_DAO_DELETE_FOLDER_DIALOG_TITLE', '删除文件夹？'],
      [
        'IDS_DAO_DELETE_FOLDER_DIALOG_DESCRIPTION',
        '这会关闭文件夹中的所有标签并删除该文件夹。',
      ],
      ['IDS_DAO_DELETE_FOLDER_DIALOG_CANCEL', '取消'],
      ['IDS_DAO_DELETE_FOLDER_DIALOG_CONFIRM', '删除'],
    ];
    for (const [messageId, translation] of dialogStrings) {
      expect(grdText).toContain(`<message name="${messageId}"`);
      expect(zhCnText).toContain(`>${translation}</translation>`);
      expect(handlerText).toContain(messageId);
    }
    expect(handlerText).toContain('"showDeleteFolderDialog"');
    expect(handlerText).toContain('ConfigureDaoSystemDialog');
    expect(handlerText).toContain('CreateBrowserModalDialogViews');
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

  it('unfolders children in place without closing tabs', async () => {
    vi.useFakeTimers();

    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [
        {type: 'tab', url: 'https://first.example/', title: 'First'},
        {
          type: 'folder',
          id: 'folder-1',
          name: 'Work',
          collapsed: false,
          children: [
            {type: 'tab', url: 'https://a.example/', title: 'A'},
            {type: 'tab', url: 'https://b.example/', title: 'B'},
          ],
        },
        {type: 'tab', url: 'https://last.example/', title: 'Last'},
      ],
    }));

    fireFolderContextMenuCommand('folder-1', 'unfolder');
    vi.advanceTimersByTime(300);

    expect(app.folderModel_.findFolderByName('Work')).toBeNull();
    expect(app.folderModel_.getOrderedItems().map(item =>
      item.type === 'tab' ? item.title : item.name))
        .toEqual(['First', 'A', 'B', 'Last']);
    expect(send).toHaveBeenCalledWith(
        'saveFolders', [expect.not.stringContaining('"name": "Work"')]);
    expect(didSendNative(send, 'showDeleteFolderDialog')).toBe(false);
    expect(didSendNative(send, 'closeTabsById')).toBe(false);
  });

  it('shows the delete dialog without mutating the folder or tabs', async () => {
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'folder-1',
        name: 'Work',
        collapsed: false,
        children: [{
          type: 'tab', tabId: 'tab-a', url: 'https://a.example/', title: 'A',
        }],
      }],
    }));
    app.unpinnedTabs_ = [tab({
      tabId: 'tab-a', url: 'https://a.example/', title: 'A',
    })];

    fireFolderContextMenuCommand('folder-1', 'delete');

    expect(send).toHaveBeenCalledWith(
        'showDeleteFolderDialog', ['folder-1']);
    expect(didSendNative(send, 'closeTabsById')).toBe(false);
    expect(didSendNative(send, 'saveFolders')).toBe(false);
    expect(app.folderModel_.findFolderByName('Work')).not.toBeNull();
  });

  it('persists only after every current child is actually closed', async () => {
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'folder-1',
        name: 'Work',
        collapsed: false,
        children: [
          {type: 'tab', tabId: 'tab-a', url: 'https://a.example/', title: 'A'},
          {type: 'tab', tabId: 'tab-b', url: 'https://b.example/', title: 'B'},
        ],
      }],
    }));
    app.unpinnedTabs_ = [
      tab({tabId: 'tab-a', url: 'https://a.example/', title: 'A'}),
      tab({tabId: 'tab-b', url: 'https://b.example/', title: 'B'}),
    ];
    app.initialStateReceived_ = true;

    fireDeleteFolderConfirmed('folder-1');

    expect(send.mock.calls.filter(
        call => call[0] === 'saveFolders' ||
            call[0] === 'closeTabsById')).toEqual([
      ['closeTabsById', [['tab-a', 'tab-b']]],
    ]);
    expect(app.folderModel_.findFolderByName('Work')).not.toBeNull();

    fireSidebarStateChanged(sidebarState({unpinnedTabs: []}));

    expect(send.mock.calls.filter(call => call[0] === 'saveFolders'))
        .toHaveLength(1);
    expect(send).toHaveBeenCalledWith(
        'saveFolders', [expect.not.stringContaining('"name": "Work"')]);
    expect(app.folderModel_.findFolderByName('Work')).toBeNull();
  });

  it('keeps the folder when a child tab close is cancelled', async () => {
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'folder-1',
        name: 'Work',
        collapsed: false,
        children: [{
          type: 'tab', tabId: 'tab-a', url: 'https://a.example/', title: 'A',
        }],
      }],
    }));
    const tabA = tab({
      tabId: 'tab-a', url: 'https://a.example/', title: 'A',
    });
    app.unpinnedTabs_ = [tabA];
    app.initialStateReceived_ = true;

    fireDeleteFolderConfirmed('folder-1');
    fireSidebarStateChanged(sidebarState({unpinnedTabs: [tabA]}));

    expect(send).toHaveBeenCalledWith('closeTabsById', [['tab-a']]);
    expect(didSendNative(send, 'saveFolders')).toBe(false);
    expect(app.folderModel_.findFolderByName('Work')).not.toBeNull();
  });

  it('deletes by stable folder ID after the folder is renamed', async () => {
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'folder-1',
        name: 'Work',
        collapsed: false,
        children: [{
          type: 'tab', tabId: 'tab-a', url: 'https://a.example/', title: 'A',
        }],
      }],
    }));
    app.unpinnedTabs_ = [
      tab({tabId: 'tab-a', url: 'https://a.example/', title: 'A'}),
    ];
    app.initialStateReceived_ = true;

    fireFolderContextMenuCommand('folder-1', 'delete');
    app.folderModel_.renameFolder('folder-1', 'Renamed');
    fireDeleteFolderConfirmed('folder-1');

    expect(app.folderModel_.findFolderByName('Renamed')).not.toBeNull();
    expect(send).toHaveBeenCalledWith('closeTabsById', [['tab-a']]);

    fireSidebarStateChanged(sidebarState({unpinnedTabs: []}));

    expect(app.folderModel_.findFolderByName('Renamed')).toBeNull();
  });

  it('closes only current folder members when confirmation arrives', async () => {
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'folder-1',
        name: 'Work',
        collapsed: false,
        children: [
          {type: 'tab', tabId: 'tab-a', url: 'https://a.example/', title: 'A'},
          {type: 'tab', tabId: 'tab-b', url: 'https://b.example/', title: 'B'},
        ],
      }],
    }));
    const tabA = tab({tabId: 'tab-a', url: 'https://a.example/', title: 'A'});
    const tabB = tab({tabId: 'tab-b', url: 'https://b.example/', title: 'B'});
    const tabC = tab({tabId: 'tab-c', url: 'https://c.example/', title: 'C'});
    app.unpinnedTabs_ = [tabA, tabB, tabC];

    fireFolderContextMenuCommand('folder-1', 'delete');
    app.folderModel_.removeTabFromFolder(tabA, 'folder-1');
    app.folderModel_.moveTabToFolder(tabC, 'folder-1');
    fireDeleteFolderConfirmed('folder-1');

    expect(send).toHaveBeenCalledWith('closeTabsById', [['tab-b', 'tab-c']]);
  });

  it('ignores missing folders for delete and confirmation', async () => {
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app);

    fireFolderContextMenuCommand('missing-folder', 'delete');
    fireDeleteFolderConfirmed('missing-folder');

    expect(didSendNative(send, 'showDeleteFolderDialog')).toBe(false);
    expect(didSendNative(send, 'closeTabsById')).toBe(false);
    expect(didSendNative(send, 'saveFolders')).toBe(false);
  });

  it('deletes an empty folder without sending a close request', async () => {
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'empty-folder',
        name: 'Empty',
        collapsed: false,
        children: [],
      }],
    }));

    fireDeleteFolderConfirmed('empty-folder');

    expect(app.folderModel_.findFolderByName('Empty')).toBeNull();
    expect(send.mock.calls.filter(call => call[0] === 'saveFolders'))
        .toHaveLength(1);
    expect(didSendNative(send, 'closeTabsById')).toBe(false);
  });

  it('uses the generic delete dialog for the stale folder', async () => {
    const {el, send} = await loadApp();
    const app = el as SidebarAppInternals;
    installFolderModel(app, JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'stale-folder',
        name: 'stale',
        collapsed: false,
        children: [],
      }],
    }));

    fireFolderContextMenuCommand('stale-folder', 'delete');

    expect(send).toHaveBeenCalledWith(
        'showDeleteFolderDialog', ['stale-folder']);
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
