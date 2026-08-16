# Sidebar Stale Tabs Folder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a sidebar context menu action that moves ordinary unpinned tabs that have not been activated in the last 24 hours into a folder named `stale`.

**Architecture:** C++ owns native tab lifecycle metadata and adds `lastActiveTimeMs` to each sidebar tab payload. The Lit WebUI owns stale filtering and folder mutation by reusing `FolderModel`; native menu commands only request the WebUI action through a listener.

**Tech Stack:** Chromium C++ WebUI message handler, `TabStripModel`, Dao sidebar Lit TypeScript, `FolderModel`, Vitest, Dao browser tests.

---

## Project Constraints

- Communicate with the user in Chinese; write code, comments, docs, and commit titles in English.
- Do not edit `engine/` directly.
- Do not run `autoninja`, `ninja`, `siso`, direct Chromium build tools, `gn gen`, `npm run build`, `npm run build:debug`, or `npm run test:build`.
- After C++ or resource edits, run `npm run import` before compile verification.
- For compile confirmation, use only `npm run rebuild`.
- For WebUI checks, use focused `npm run test:webui -- <pattern>` while iterating, then `npm run test:webui` and `npm run lint:lit`.
- Do not run state-changing git commands unless the user explicitly authorizes the exact action. This plan has no automatic `git add` or `git commit` steps.

## File Structure

- Modify `src/dao/browser/ui/webui/resources/sidebar/dao_folder_model.ts`
  - Add exact-name folder lookup and batch tab-to-folder movement helpers.
- Modify `src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_model.test.ts`
  - Add pure model coverage for finding, creating, moving, and avoiding duplicates in `stale`.
- Modify `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`
  - Add `lastActiveTimeMs?: number` to `TabData`.
- Modify `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`
  - Listen for `moveStaleTabsRequested`, filter stale `unpinnedTabs_`, and move candidates into `stale`.
- Modify `src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts`
  - Add WebUI action coverage for 24-hour filtering, exclusions, no-op behavior, folder reuse, and debounced persistence.
- Modify `src/dao/browser/ui/webui/dao_sidebar_ui.h`
  - Add native timestamp helpers, state storage, and the stale context-menu command.
- Modify `src/dao/browser/ui/webui/dao_sidebar_ui.cc`
  - Populate `lastActiveTimeMs`, update timestamps on activation, prune removed tabs, add menu items, and fire `moveStaleTabsRequested`.
- Modify `src/dao/browser/strings/dao_strings.grd`
  - Add localized menu text for `Move Stale Tabs to "stale"`.
- Modify `src/dao/browser/ui/views/dao_browser_browsertest.cc`
  - Add narrow browser tests for `lastActiveTimeMs` serialization and activation refresh.

---

### Task 1: Add FolderModel Batch Helpers

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/dao_folder_model.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_model.test.ts`

- [ ] **Step 1: Add failing FolderModel tests**

Append these tests inside the existing `describe('FolderModel', () => { ... })` block in `src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_model.test.ts`:

```ts
  it('finds or creates folders by exact name', () => {
    const model = new FolderModel();

    const created = model.findOrCreateFolderByName('stale');
    const reused = model.findOrCreateFolderByName('stale');
    const differentlyCased = model.findOrCreateFolderByName('Stale');

    expect(created.id).toBe(reused.id);
    expect(differentlyCased.id).not.toBe(created.id);
    expect(model.getFolders().map(folder => folder.name))
        .toEqual(['stale', 'Stale']);
  });

  it('moves multiple tabs into a target folder without duplicates', () => {
    const model = new FolderModel();
    model.loadFromJson(JSON.stringify({
      version: 1,
      items: [
        {type: 'tab', tabId: 'a', url: 'https://a.example', title: 'A'},
        {
          type: 'folder',
          id: 'reading',
          name: 'Reading',
          collapsed: false,
          children: [
            {type: 'tab', tabId: 'b', url: 'https://b.example', title: 'B'},
          ],
        },
        {
          type: 'folder',
          id: 'stale-id',
          name: 'stale',
          collapsed: true,
          children: [
            {type: 'tab', tabId: 'c', url: 'https://c.example', title: 'C'},
          ],
        },
      ],
    }));

    const stale = model.findOrCreateFolderByName('stale');
    model.moveTabsToFolder([
      tab('a', 'https://a.example', 'A'),
      tab('b', 'https://b.example', 'B'),
      tab('c', 'https://c.example', 'C'),
    ], stale.id);

    expect(stale.collapsed).toBe(false);
    expect(stale.children.map(child => child.title)).toEqual(['C', 'A', 'B']);

    const reading = model.findFolderByName('Reading');
    expect(reading?.children).toEqual([]);
  });
```

- [ ] **Step 2: Run the failing model tests**

Run:

```bash
npm run test:webui -- folder_model
```

Expected: FAIL because `findOrCreateFolderByName`, `findFolderByName`, and `moveTabsToFolder` are not implemented.

- [ ] **Step 3: Implement FolderModel helpers**

In `src/dao/browser/ui/webui/resources/sidebar/dao_folder_model.ts`, add these public methods after `getFolders()` and before `hasData()`:

```ts
  /**
   * Return the first folder with an exact name match.
   */
  findFolderByName(name: string): FolderData | null {
    const item = this.items_.find(
        item => item.type === 'folder' && item.name === name);
    return item ? item as FolderData : null;
  }

  /**
   * Return the first exact-name folder match, or create it at the end.
   */
  findOrCreateFolderByName(name: string): FolderData {
    const existing = this.findFolderByName(name);
    if (existing) {
      existing.collapsed = false;
      return existing;
    }
    return this.addFolder(name);
  }

  /**
   * Move tabs into the target folder, removing them from their current folder
   * or top-level position first. Tabs already in the target folder are skipped.
   */
  moveTabsToFolder(
      tabs: Array<Pick<TabData, 'tabId'|'url'|'title'>>,
      folderId: string): void {
    const targetFolder = this.findFolder_(folderId);
    if (!targetFolder) return;

    for (const tab of tabs) {
      const sourceFolderId = this.findTabFolder(tab);
      if (sourceFolderId === folderId) {
        continue;
      }
      this.moveTabToFolder(tab, folderId, sourceFolderId || undefined);
    }

    targetFolder.collapsed = false;
  }
```

- [ ] **Step 4: Run the model tests again**

Run:

```bash
npm run test:webui -- folder_model
```

Expected: PASS.

---

### Task 2: Add WebUI Stale Filtering And Event Handling

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts`

- [ ] **Step 1: Add the WebUI tab timestamp type**

In `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`, update `TabData`:

```ts
export interface TabData {
  tabId: string;
  index: number;
  title: string;
  url: string;
  faviconUrl: string;
  isActive: boolean;
  isPinned: boolean;
  isAudible: boolean;
  isMuted: boolean;
  isAgentLocked?: boolean;
  isInSplit?: boolean;
  isFaviconLight?: boolean;
  lastActiveTimeMs?: number;
}
```

- [ ] **Step 2: Add failing sidebar app tests**

In `src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts`, add this import near the existing imports:

```ts
import {FolderModel} from '../dao_folder_model.js';
```

Extend the `loadApp()` element type with the private fields used by these tests:

```ts
    folderModel_: FolderModel;
    foldersLoaded_: boolean;
    unpinnedTabs_: TabData[];
    folderModelVersion_: number;
```

Add this helper below `sidebarState()`:

```ts
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

function didSendNative(send: ReturnType<typeof vi.fn>, method: string): boolean {
  return send.mock.calls.some(call => call[0] === method);
}
```

Append these tests inside `describe('dao-sidebar-app', () => { ... })`:

```ts
  it('does not create stale folder when no tabs match', async () => {
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
  });

  it('moves only stale ordinary tabs into a new stale folder', async () => {
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
    ];
    app.folderModel_.reconcile(app.unpinnedTabs_);

    fireMoveStaleTabsRequested();
    await el.updateComplete;
    vi.advanceTimersByTime(300);

    const stale = app.folderModel_.findFolderByName('stale');
    expect(stale?.children.map(child => child.tabId)).toEqual(['old']);
    expect(send).toHaveBeenCalledWith(
        'saveFolders', [expect.stringContaining('"name": "stale"')]);
  });

  it('reuses existing stale folder and moves stale tabs from other folders',
      async () => {
        vi.useFakeTimers();
        vi.spyOn(Date, 'now').mockReturnValue(1_800_000_000_000);

        const {el} = await loadApp();
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

        const stale = app.folderModel_.findFolderByName('stale');
        expect(stale?.id).toBe('stale-folder');
        expect(stale?.collapsed).toBe(false);
        expect(stale?.children.map(child => child.title))
            .toEqual(['Already', 'Old']);
        expect(app.folderModel_.findFolderByName('Reading')?.children)
            .toEqual([]);
      });
```

- [ ] **Step 3: Run the failing sidebar app tests**

Run:

```bash
npm run test:webui -- sidebar_app
```

Expected: FAIL because the WebUI does not listen for `moveStaleTabsRequested` and does not implement stale filtering.

- [ ] **Step 4: Implement stale filtering in the sidebar app**

In `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`, add this constant near `TAB_SCROLLBAR_STALE_HOVER_MS`:

```ts
const STALE_TAB_THRESHOLD_MS = 24 * 60 * 60 * 1000;
const STALE_TABS_FOLDER_NAME = 'stale';
```

In `connectedCallback()`, after the existing `sidebarPointerExited` listener, add:

```ts
    this.addSidebarListener_(
        'moveStaleTabsRequested', () => this.moveStaleTabsToFolder_());
```

Add these methods near `findUnpinnedTabById_()`:

```ts
  private getStaleTabs_(nowMs: number = Date.now()): TabData[] {
    const threshold = nowMs - STALE_TAB_THRESHOLD_MS;
    return this.unpinnedTabs_.filter(tab =>
      !tab.isActive &&
      !tab.isPinned &&
      !tab.isAudible &&
      !tab.isMuted &&
      !tab.isAgentLocked &&
      typeof tab.lastActiveTimeMs === 'number' &&
      tab.lastActiveTimeMs < threshold);
  }

  private moveStaleTabsToFolder_() {
    if (!this.foldersLoaded_) {
      return;
    }

    const staleTabs = this.getStaleTabs_();
    if (staleTabs.length === 0) {
      return;
    }

    this.folderModel_.reconcile(this.unpinnedTabs_);
    const folder =
        this.folderModel_.findOrCreateFolderByName(STALE_TABS_FOLDER_NAME);
    this.folderModel_.moveTabsToFolder(staleTabs, folder.id);
    this.saveFolders_();
  }
```

- [ ] **Step 5: Run sidebar WebUI tests**

Run:

```bash
npm run test:webui -- folder_model sidebar_app
```

Expected: PASS.

---

### Task 3: Add Native Last-Active Timestamps

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.h`
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.cc`
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Add failing browser tests for timestamp serialization**

In `src/dao/browser/ui/views/dao_browser_browsertest.cc`, add this helper near `GetIntField()`:

```cpp
double GetDoubleField(const base::DictValue& dict, const char* key) {
  return dict.FindDouble(key).value_or(0.0);
}
```

Add these tests near the existing `DaoSidebarBrowserTest` pinned-tab tests:

```cpp
IN_PROC_BROWSER_TEST_F(DaoSidebarBrowserTest,
                       SidebarStateIncludesLastActiveTimeMs) {
  ASSERT_TRUE(embedded_test_server()->Start());

  const GURL first_url = embedded_test_server()->GetURL("/title1.html");
  const GURL second_url = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  chrome::AddTabAt(browser(), second_url, 1, true);
  ASSERT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents()));

  dao::DaoSidebarUIHandler handler;
  AttachSidebarHandlerForTesting(browser(), &handler);

  base::DictValue state = handler.GetSidebarStateForTesting();
  const base::ListValue* unpinned_tabs = state.FindList("unpinnedTabs");
  ASSERT_NE(nullptr, unpinned_tabs);

  const base::DictValue* first_tab =
      FindDictByStringField(*unpinned_tabs, "url", first_url.spec());
  ASSERT_NE(nullptr, first_tab);
  EXPECT_GT(GetDoubleField(*first_tab, "lastActiveTimeMs"), 0.0);

  const base::DictValue* second_tab =
      FindDictByStringField(*unpinned_tabs, "url", second_url.spec());
  ASSERT_NE(nullptr, second_tab);
  EXPECT_GT(GetDoubleField(*second_tab, "lastActiveTimeMs"), 0.0);
}

IN_PROC_BROWSER_TEST_F(DaoSidebarBrowserTest,
                       ActivatingTabRefreshesLastActiveTimeMs) {
  ASSERT_TRUE(embedded_test_server()->Start());

  const GURL first_url = embedded_test_server()->GetURL("/title1.html");
  const GURL second_url = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  chrome::AddTabAt(browser(), second_url, 1, true);
  ASSERT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents()));

  dao::DaoSidebarUIHandler handler;
  AttachSidebarHandlerForTesting(browser(), &handler);

  base::DictValue initial_state = handler.GetSidebarStateForTesting();
  const base::ListValue* initial_tabs = initial_state.FindList("unpinnedTabs");
  ASSERT_NE(nullptr, initial_tabs);
  const base::DictValue* first_initial =
      FindDictByStringField(*initial_tabs, "url", first_url.spec());
  ASSERT_NE(nullptr, first_initial);
  const double initial_time =
      GetDoubleField(*first_initial, "lastActiveTimeMs");

  base::PlatformThread::Sleep(base::Milliseconds(2));
  browser()->tab_strip_model()->ActivateTabAt(
      FindTabIndexByUrl(browser(), first_url));

  base::DictValue refreshed_state = handler.GetSidebarStateForTesting();
  const base::ListValue* refreshed_tabs =
      refreshed_state.FindList("unpinnedTabs");
  ASSERT_NE(nullptr, refreshed_tabs);
  const base::DictValue* first_refreshed =
      FindDictByStringField(*refreshed_tabs, "url", first_url.spec());
  ASSERT_NE(nullptr, first_refreshed);
  EXPECT_GT(GetDoubleField(*first_refreshed, "lastActiveTimeMs"),
            initial_time);
}
```

Also add this include near the other base includes:

```cpp
#include "base/time/time.h"
#include "base/threading/platform_thread.h"
```

- [ ] **Step 2: Defer native compile until implementation is present**

Do not use direct Chromium build tools. This task will fail to compile before implementation. After the C++ implementation in this task, compile with the allowed project path in Step 5.

- [ ] **Step 3: Add timestamp storage declarations**

In `src/dao/browser/ui/webui/dao_sidebar_ui.h`, add:

```cpp
#include <map>

#include "base/time/time.h"
```

Add these private helpers near `BuildSidebarState()`:

```cpp
  base::Time GetOrCreateTabLastActiveTime(const std::string& tab_id);
  void MarkActiveTabLastActiveNow();
  void PruneTabLastActiveTimes();
```

Add this private member near `pending_scroll_target_tab_id_`:

```cpp
  std::map<std::string, base::Time> tab_last_active_times_;
```

- [ ] **Step 4: Implement timestamp storage and serialization**

Add the helper implementations before `PushFullState()`:

```cpp
base::Time DaoSidebarUIHandler::GetOrCreateTabLastActiveTime(
    const std::string& tab_id) {
  auto it = tab_last_active_times_.find(tab_id);
  if (it == tab_last_active_times_.end()) {
    it = tab_last_active_times_.emplace(tab_id, base::Time::Now()).first;
  }
  return it->second;
}

void DaoSidebarUIHandler::MarkActiveTabLastActiveNow() {
  if (!browser_) {
    return;
  }
  content::WebContents* active =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!active) {
    return;
  }
  tab_last_active_times_[GetSidebarTabId(active)] = base::Time::Now();
}

void DaoSidebarUIHandler::PruneTabLastActiveTimes() {
  if (!browser_) {
    tab_last_active_times_.clear();
    return;
  }

  std::set<std::string> live_tab_ids;
  TabStripModel* model = browser_->tab_strip_model();
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (contents) {
      live_tab_ids.insert(GetSidebarTabId(contents));
    }
  }

  for (auto it = tab_last_active_times_.begin();
       it != tab_last_active_times_.end();) {
    if (live_tab_ids.find(it->first) == live_tab_ids.end()) {
      it = tab_last_active_times_.erase(it);
    } else {
      ++it;
    }
  }
}
```

In `DaoSidebarUIHandler::SetBrowser()`, inside the existing `if (browser_changed) { ... }` block, add:

```cpp
    tab_last_active_times_.clear();
```

At the start of `DaoSidebarUIHandler::OnTabStripModelChanged()`, before the `IsJavascriptAllowed()` guard, add:

```cpp
  if (selection.active_tab_changed()) {
    MarkActiveTabLastActiveNow();
  }
  if (change.type() != TabStripModelChange::kSelectionOnly) {
    PruneTabLastActiveTimes();
  }
```

In `BuildSidebarState()`, replace the direct tab ID set:

```cpp
    tab.Set("tabId", GetSidebarTabId(contents));
```

with:

```cpp
    const std::string tab_id = GetSidebarTabId(contents);
    tab.Set("tabId", tab_id);
    tab.Set("lastActiveTimeMs",
            GetOrCreateTabLastActiveTime(tab_id)
                .InMillisecondsFSinceUnixEpoch());
```

- [ ] **Step 5: Compile-check the native timestamp work**

Run:

```bash
npm run import
npm run rebuild
```

Expected: PASS. If `npm run rebuild` fails, fix only the compile errors caused by this task.

---

### Task 4: Add Native Context Menu Command

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.h`
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.cc`
- Modify: `src/dao/browser/strings/dao_strings.grd`

- [ ] **Step 1: Add the localized string**

In `src/dao/browser/strings/dao_strings.grd`, add this message near the existing sidebar/tab context menu strings:

```xml
      <message name="IDS_DAO_SIDEBAR_CONTEXT_MOVE_STALE_TABS" desc="Context menu item in the Dao sidebar that moves tabs inactive for more than 24 hours into a folder named stale.">
        Move Stale Tabs to "stale"
      </message>
```

- [ ] **Step 2: Add the command ID**

In `src/dao/browser/ui/webui/dao_sidebar_ui.h`, add `kMoveStaleTabsToFolder` to `TabContextMenuCommand` after `kPinTab`:

```cpp
    kPinTab,
    kMoveStaleTabsToFolder,
    kPinnedOpen,
```

- [ ] **Step 3: Add the command to tab and sidebar menus**

In `DaoSidebarUIHandler::HandleShowTabContextMenu()`, after the optional `Pin Tab` item and before mute, add:

```cpp
  tab_context_menu_model_->AddItem(
      kMoveStaleTabsToFolder,
      l10n_util::GetStringUTF16(IDS_DAO_SIDEBAR_CONTEXT_MOVE_STALE_TABS));
```

In `DaoSidebarUIHandler::HandleShowSidebarContextMenu()`, before the `#if DCHECK_IS_ON()` block, add:

```cpp
  tab_context_menu_model_->AddItem(
      kMoveStaleTabsToFolder,
      l10n_util::GetStringUTF16(IDS_DAO_SIDEBAR_CONTEXT_MOVE_STALE_TABS));
```

- [ ] **Step 4: Enable and execute the command**

In `DaoSidebarUIHandler::IsCommandIdEnabled()`, add this before pinned item handling:

```cpp
  if (command_id == kMoveStaleTabsToFolder) {
    return true;
  }
```

In `DaoSidebarUIHandler::ExecuteCommand()`, add this after the `!browser_` guard and before pinned item handling:

```cpp
  if (command_id == kMoveStaleTabsToFolder) {
    if (IsJavascriptAllowed()) {
      FireWebUIListener("moveStaleTabsRequested");
    }
    ClearContextMenuState();
    return;
  }
```

- [ ] **Step 5: Compile-check the menu command**

Run:

```bash
npm run import
npm run rebuild
```

Expected: PASS.

---

### Task 5: Run Focused And Final Verification

**Files:**
- No new edits unless verification reveals a bug.

- [ ] **Step 1: Run focused WebUI tests**

Run:

```bash
npm run test:webui -- folder_model sidebar_app
```

Expected: PASS.

- [ ] **Step 2: Run full WebUI tests**

Run:

```bash
npm run test:webui
```

Expected: PASS.

- [ ] **Step 3: Run Lit lint**

Run:

```bash
npm run lint:lit
```

Expected: PASS.

- [ ] **Step 4: Run compile confirmation**

Run:

```bash
npm run import
npm run rebuild
```

Expected: PASS.

- [ ] **Step 5: Inspect the final diff**

Run:

```bash
git diff -- docs/superpowers/specs/2026-06-21-sidebar-stale-tabs-folder-design.md docs/superpowers/plans/2026-06-21-sidebar-stale-tabs-folder.md src/dao/browser/ui/webui/resources/sidebar/dao_folder_model.ts src/dao/browser/ui/webui/resources/sidebar/__tests__/folder_model.test.ts src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts src/dao/browser/ui/webui/resources/sidebar/__tests__/sidebar_app.test.ts src/dao/browser/ui/webui/dao_sidebar_ui.h src/dao/browser/ui/webui/dao_sidebar_ui.cc src/dao/browser/strings/dao_strings.grd src/dao/browser/ui/views/dao_browser_browsertest.cc
```

Expected: Diff only contains the stale-tabs feature, tests, and the two superpowers docs. Do not stage or commit unless the user explicitly authorizes those exact git actions.
