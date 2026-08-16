# Pinned Tabs Grid Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a persistent pinned tabs grid above Dao Browser's vertical Today tab list, where open pinned items behave like tabs and closed pinned items remain as dormant reopenable tiles.

**Architecture:** Add a Dao-owned pinned item model persisted to `dao_pinned_tabs.json`, reconcile it with Chromium's native `TabStripModel::IsTabPinned()` state, and expose the reconciled items to a new Lit grid component. Keep Today/folder behavior scoped to unpinned tabs.

**Tech Stack:** Chromium C++ Views/WebUI message handlers, `TabStripModel`, profile file persistence via `base::ThreadPool`, Lit TypeScript WebUI, Vitest, Dao browser tests.

---

## File Structure

- Create `src/dao/browser/ui/webui/dao_pinned_tab_model.h`: persistent item structs and an in-memory model API for load, save serialization, add, remove, reorder, and tab reconciliation helpers.
- Create `src/dao/browser/ui/webui/dao_pinned_tab_model.cc`: JSON parsing/serialization and model mutation logic.
- Modify `src/dao/browser/ui/webui/dao_sidebar_ui.h`: own the model, add message handlers, add pinned context menu commands, add helper declarations.
- Modify `src/dao/browser/ui/webui/dao_sidebar_ui.cc`: load/save `dao_pinned_tabs.json`, build `pinnedItems`, pin/unpin/open/close/reorder handlers, localized menu labels.
- Modify `src/patches/chrome/browser/ui/BUILD.gn.patch`: add the new C++ model files to the Dao source list.
- Modify `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`: add `PinnedItemData` and `pinnedItems` to `SidebarState`.
- Create `src/dao/browser/ui/webui/resources/sidebar/dao_pinned_tabs_grid.ts`: labeled tile grid with active, open, dormant, drag reorder, click, and context menu behavior.
- Create `src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts`: grid rendering and command dispatch tests.
- Modify `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`: render `dao-pinned-tabs-grid`, route pinned item events, stop using `dao-favorites-view` for pinned tabs.
- Modify `src/dao/browser/ui/webui/resources/sidebar/BUILD.gn`: include the new grid file and keep `dao_favorites_view.ts` until a separate cleanup removes the unused component.
- Modify `src/dao/browser/strings/dao_strings.grd`: add localized string IDs for pinned tab commands.
- Modify `src/dao/browser/ui/views/dao_browser_browsertest.cc`: add browser tests for pin, close-to-dormant, reopen, unpin, and folder isolation.

## Task 1: Add WebUI Types And Pinned Grid Test

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`
- Create: `src/dao/browser/ui/webui/resources/sidebar/dao_pinned_tabs_grid.ts`
- Create: `src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/BUILD.gn`

- [ ] **Step 1: Add bridge types**

Add this interface near `TabData` in `sidebar_bridge.ts`:

```ts
export interface PinnedItemData {
  id: string;
  title: string;
  url: string;
  faviconUrl: string;
  isOpen: boolean;
  openTabIndex: number;
  isActive: boolean;
  isFaviconLight?: boolean;
}
```

Update `SidebarState`:

```ts
export interface SidebarState {
  pinnedItems: PinnedItemData[];
  pinnedTabs: TabData[];
  unpinnedTabs: TabData[];
  activeIndex: number;
  sessionId: number;
}
```

- [ ] **Step 2: Write the failing grid test**

Create `src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts`:

```ts
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import type {PinnedItemData, SidebarState} from '../sidebar_bridge.js';

function item(extra: Partial<PinnedItemData> = {}): PinnedItemData {
  return {
    id: 'pin-1',
    title: 'GitHub',
    url: 'https://github.com/',
    faviconUrl: '',
    isOpen: true,
    openTabIndex: 0,
    isActive: false,
    ...extra,
  };
}

async function loadGrid() {
  vi.resetModules();
  const send = vi.fn();
  (globalThis as unknown as {chrome: {send: typeof send}}).chrome = {send};
  await import('../dao_pinned_tabs_grid.js');
  const el = document.createElement('dao-pinned-tabs-grid') as HTMLElement & {
    items: PinnedItemData[];
    updateComplete: Promise<boolean>;
  };
  document.body.appendChild(el);
  return {el, send};
}

describe('dao-pinned-tabs-grid', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
  });

  afterEach(() => {
    document.body.innerHTML = '';
    vi.restoreAllMocks();
    delete (globalThis as unknown as {chrome?: unknown}).chrome;
  });

  it('renders active, open, and dormant tile states', async () => {
    const {el} = await loadGrid();
    el.items = [
      item({id: 'active', isActive: true, title: 'Active'}),
      item({id: 'open', isActive: false, title: 'Open'}),
      item({id: 'dormant', isOpen: false, openTabIndex: -1, title: 'Dormant'}),
    ];
    await el.updateComplete;

    const tiles = el.shadowRoot!.querySelectorAll('.tile');
    expect(tiles).toHaveLength(3);
    expect(tiles[0]!.classList.contains('active')).toBe(true);
    expect(tiles[1]!.classList.contains('open')).toBe(true);
    expect(tiles[2]!.classList.contains('dormant')).toBe(true);
  });

  it('activates or opens the clicked pinned item', async () => {
    const {el, send} = await loadGrid();
    el.items = [item({id: 'pin-click'})];
    await el.updateComplete;

    const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
    tile.click();

    expect(send).toHaveBeenCalledWith(
        'activateOrOpenPinnedItem', ['pin-click']);
  });

  it('shows a pinned item context menu with the item id', async () => {
    const {el, send} = await loadGrid();
    el.items = [item({id: 'pin-menu'})];
    await el.updateComplete;

    const tile = el.shadowRoot!.querySelector('.tile') as HTMLElement;
    tile.dispatchEvent(new MouseEvent('contextmenu', {
      bubbles: true,
      cancelable: true,
      screenX: 12,
      screenY: 34,
    }));

    expect(send).toHaveBeenCalledWith(
        'showPinnedItemContextMenu', ['pin-menu', 12, 34]);
  });
});
```

- [ ] **Step 3: Run the failing test**

Run:

```bash
npm run test:webui -- pinned_tabs_grid
```

Expected: FAIL because `../dao_pinned_tabs_grid.js` does not exist.

- [ ] **Step 4: Create the minimal grid component**

Create `src/dao/browser/ui/webui/resources/sidebar/dao_pinned_tabs_grid.ts`:

```ts
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, html, css} from '//resources/lit/v3_0/lit.rollup.js';

import {sendNative} from './sidebar_bridge.js';
import type {PinnedItemData} from './sidebar_bridge.js';

export class DaoPinnedTabsGrid extends CrLitElement {
  static get is() {
    return 'dao-pinned-tabs-grid';
  }

  static override get styles() {
    return css`
      :host {
        display: block;
        padding: 4px 10px 6px;
      }

      .grid {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(58px, 1fr));
        gap: 6px;
      }

      .tile {
        min-width: 0;
        height: 44px;
        border: none;
        border-radius: 10px;
        background: transparent;
        color: var(--text-primary);
        cursor: default;
        padding: 4px 6px;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 3px;
        transition: background 0.12s ease, box-shadow 0.12s ease,
                    opacity 0.12s ease;
      }

      .tile:hover {
        background: var(--ink-drop);
      }

      .tile.active,
      .tile.active:hover {
        background: var(--surface-active);
        box-shadow: 0 0 0 0.5px rgba(70, 120, 190, 0.36),
                    0 1px 3px rgba(0, 0, 0, 0.10);
      }

      .tile.dormant {
        opacity: 0.62;
      }

      .favicon {
        width: 16px;
        height: 16px;
        border-radius: 4px;
        flex: none;
      }

      .favicon.light-icon {
        filter: invert(1);
      }

      .placeholder {
        width: 16px;
        height: 16px;
        border-radius: 4px;
        background: var(--surface);
      }

      .title {
        max-width: 100%;
        min-width: 0;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
        font-size: 10px;
        line-height: 12px;
      }
    `;
  }

  static override get properties() {
    return {
      items: {type: Array},
    };
  }

  declare items: PinnedItemData[];

  constructor() {
    super();
    this.items = [];
  }

  override render() {
    return html`
      <div class="grid">
        ${this.items.map(item => this.renderItem_(item))}
      </div>
    `;
  }

  private renderItem_(item: PinnedItemData) {
    const classes = [
      'tile',
      item.isActive ? 'active' : '',
      item.isOpen ? 'open' : 'dormant',
    ].filter(Boolean).join(' ');
    const title = item.title || item.url;

    return html`
      <button class=${classes}
              title=${title}
              @click=${() => this.onActivateOrOpen_(item)}
              @contextmenu=${(e: MouseEvent) => this.onContextMenu_(e, item)}>
        ${item.faviconUrl ? html`
          <img class=${item.isFaviconLight ? 'favicon light-icon' : 'favicon'}
               src=${item.faviconUrl}
               alt="">
        ` : html`<div class="placeholder"></div>`}
        <span class="title">${title}</span>
      </button>
    `;
  }

  private onActivateOrOpen_(item: PinnedItemData) {
    sendNative('activateOrOpenPinnedItem', item.id);
  }

  private onContextMenu_(e: MouseEvent, item: PinnedItemData) {
    e.preventDefault();
    e.stopPropagation();
    sendNative('showPinnedItemContextMenu', item.id, e.screenX, e.screenY);
  }
}

customElements.define('dao-pinned-tabs-grid', DaoPinnedTabsGrid);
```

- [ ] **Step 5: Add the component to BUILD.gn**

Modify `src/dao/browser/ui/webui/resources/sidebar/BUILD.gn`:

```gn
  ts_files = [
    "sidebar.ts",
    "sidebar_bridge.ts",
    "dao_sidebar_app.ts",
    "dao_new_tab_button.ts",
    "dao_sidebar_section.ts",
    "dao_pinned_tabs_grid.ts",
    "dao_favorites_view.ts",
    "dao_tab_list.ts",
    "dao_tab_item.ts",
    "dao_download_button.ts",
    "dao_media_control.ts",
    "dao_folder_item.ts",
    "dao_folder_model.ts",
  ]
```

- [ ] **Step 6: Run the WebUI test**

Run:

```bash
npm run test:webui -- pinned_tabs_grid
```

Expected: PASS for the new grid tests.

- [ ] **Step 7: Authorization checkpoint**

Stop and report the changed files. Do not run `git add`, `git commit`, or any state-changing git command unless the latest user message explicitly authorizes that exact action.

## Task 2: Wire The Grid Into The Sidebar App

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts`
- Test: `src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts`

- [ ] **Step 1: Add an app-level state smoke test**

Extend `pinned_tabs_grid.test.ts` with a payload-level regression test that verifies `PinnedItemData` supports dormant items:

```ts
it('allows dormant pinned items in sidebar state payloads', async () => {
  const state: SidebarState = {
    pinnedItems: [{
      id: 'dormant',
      title: 'Docs',
      url: 'https://docs.example/',
      faviconUrl: '',
      isOpen: false,
      openTabIndex: -1,
      isActive: false,
    }],
    pinnedTabs: [],
    unpinnedTabs: [],
    activeIndex: -1,
    sessionId: 1,
  };

  expect(state.pinnedItems[0]!.isOpen).toBe(false);
});
```

- [ ] **Step 2: Run the type-focused test**

Run:

```bash
npm run test:webui -- pinned_tabs_grid
```

Expected: PASS after Task 1 types exist.

- [ ] **Step 3: Import and render the grid**

In `dao_sidebar_app.ts`, update imports:

```ts
import type {SidebarState, TabData, FolderAction, PinnedItemData} from './sidebar_bridge.js';
import './dao_pinned_tabs_grid.js';
```

Add reactive property:

```ts
      pinnedItems_: {type: Array},
```

Add declaration:

```ts
  declare protected pinnedItems_: PinnedItemData[];
```

Initialize in the constructor:

```ts
    this.pinnedItems_ = [];
```

Set it in the `sidebarStateChanged` listener:

```ts
      this.pinnedItems_ = state.pinnedItems || [];
```

Render it where `dao-favorites-view` currently appears:

```ts
        ${this.pinnedItems_.length > 0 ? html`
          <dao-pinned-tabs-grid
            .items=${this.pinnedItems_}>
          </dao-pinned-tabs-grid>
        ` : ''}
```

Keep `pinnedTabs_` temporarily because `tabUpdated` and `activeTabChanged` still reference it until C++ sends `pinnedItems` consistently.

- [ ] **Step 4: Run WebUI verification**

Run:

```bash
npm run test:webui -- pinned_tabs_grid
npm run lint:lit
```

Expected: both commands pass.

- [ ] **Step 5: Authorization checkpoint**

Stop and report the changed files. Do not run state-changing git commands without exact authorization.

## Task 3: Add The C++ Pinned Item Model

**Files:**
- Create: `src/dao/browser/ui/webui/dao_pinned_tab_model.h`
- Create: `src/dao/browser/ui/webui/dao_pinned_tab_model.cc`
- Modify: `src/patches/chrome/browser/ui/BUILD.gn.patch`

- [ ] **Step 1: Create the model header**

Create `src/dao/browser/ui/webui/dao_pinned_tab_model.h`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_PINNED_TAB_MODEL_H_
#define DAO_BROWSER_UI_WEBUI_DAO_PINNED_TAB_MODEL_H_

#include <string>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"

namespace dao {

struct DaoPinnedTabItem {
  std::string id;
  std::string title;
  std::string url;
  std::string favicon_url;
  base::Time created_at;
  base::Time updated_at;
};

class DaoPinnedTabModel {
 public:
  DaoPinnedTabModel();
  DaoPinnedTabModel(const DaoPinnedTabModel&) = delete;
  DaoPinnedTabModel& operator=(const DaoPinnedTabModel&) = delete;
  ~DaoPinnedTabModel();

  bool LoadFromJson(const std::string& json);
  std::string ToJson() const;

  const std::vector<DaoPinnedTabItem>& items() const { return items_; }

  DaoPinnedTabItem* FindById(const std::string& id);
  const DaoPinnedTabItem* FindById(const std::string& id) const;
  DaoPinnedTabItem* FindByUrl(const std::string& url);
  const DaoPinnedTabItem* FindByUrl(const std::string& url) const;

  DaoPinnedTabItem& AddOrUpdate(const std::string& title,
                                const std::string& url,
                                const std::string& favicon_url);
  bool RemoveById(const std::string& id);
  bool Move(const std::string& id, size_t to_index);

 private:
  std::vector<DaoPinnedTabItem> items_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_PINNED_TAB_MODEL_H_
```

- [ ] **Step 2: Create the model implementation**

Create `src/dao/browser/ui/webui/dao_pinned_tab_model.cc`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_pinned_tab_model.h"

#include <algorithm>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace dao {

namespace {

std::string NewPinnedItemId() {
  uint64_t high = base::RandUint64();
  uint64_t low = base::RandUint64();
  return base::NumberToString(high) + "-" + base::NumberToString(low);
}

int64_t ToUnixSeconds(base::Time time) {
  return time.InSecondsFSinceUnixEpoch();
}

base::Time FromUnixSeconds(int64_t seconds) {
  return base::Time::FromSecondsSinceUnixEpoch(seconds);
}

}  // namespace

DaoPinnedTabModel::DaoPinnedTabModel() = default;
DaoPinnedTabModel::~DaoPinnedTabModel() = default;

bool DaoPinnedTabModel::LoadFromJson(const std::string& json) {
  items_.clear();
  if (json.empty()) {
    return true;
  }

  std::optional<base::Value> parsed = base::JSONReader::Read(json);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }

  const base::Value::List* items =
      parsed->GetDict().FindList("items");
  if (!items) {
    return true;
  }

  for (const base::Value& value : *items) {
    if (!value.is_dict()) {
      continue;
    }
    const base::Value::Dict& dict = value.GetDict();
    const std::string* id = dict.FindString("id");
    const std::string* title = dict.FindString("title");
    const std::string* url = dict.FindString("url");
    if (!id || !title || !url || !GURL(*url).is_valid()) {
      continue;
    }

    DaoPinnedTabItem item;
    item.id = *id;
    item.title = *title;
    item.url = *url;
    if (const std::string* favicon = dict.FindString("faviconUrl")) {
      item.favicon_url = *favicon;
    }
    item.created_at =
        FromUnixSeconds(dict.FindInt("createdAt").value_or(0));
    item.updated_at =
        FromUnixSeconds(dict.FindInt("updatedAt").value_or(0));
    items_.push_back(std::move(item));
  }

  return true;
}

std::string DaoPinnedTabModel::ToJson() const {
  base::Value::Dict root;
  root.Set("version", 1);
  base::Value::List items;
  for (const DaoPinnedTabItem& item : items_) {
    base::Value::Dict dict;
    dict.Set("id", item.id);
    dict.Set("title", item.title);
    dict.Set("url", item.url);
    dict.Set("faviconUrl", item.favicon_url);
    dict.Set("createdAt", ToUnixSeconds(item.created_at));
    dict.Set("updatedAt", ToUnixSeconds(item.updated_at));
    items.Append(std::move(dict));
  }
  root.Set("items", std::move(items));

  std::string json;
  base::JSONWriter::Write(root, &json);
  return json;
}

DaoPinnedTabItem* DaoPinnedTabModel::FindById(const std::string& id) {
  auto it = std::ranges::find(items_, id, &DaoPinnedTabItem::id);
  return it == items_.end() ? nullptr : &*it;
}

const DaoPinnedTabItem* DaoPinnedTabModel::FindById(
    const std::string& id) const {
  auto it = std::ranges::find(items_, id, &DaoPinnedTabItem::id);
  return it == items_.end() ? nullptr : &*it;
}

DaoPinnedTabItem* DaoPinnedTabModel::FindByUrl(const std::string& url) {
  auto it = std::ranges::find(items_, url, &DaoPinnedTabItem::url);
  return it == items_.end() ? nullptr : &*it;
}

const DaoPinnedTabItem* DaoPinnedTabModel::FindByUrl(
    const std::string& url) const {
  auto it = std::ranges::find(items_, url, &DaoPinnedTabItem::url);
  return it == items_.end() ? nullptr : &*it;
}

DaoPinnedTabItem& DaoPinnedTabModel::AddOrUpdate(
    const std::string& title,
    const std::string& url,
    const std::string& favicon_url) {
  if (DaoPinnedTabItem* existing = FindByUrl(url)) {
    existing->title = title;
    existing->favicon_url = favicon_url;
    existing->updated_at = base::Time::Now();
    return *existing;
  }

  DaoPinnedTabItem item;
  item.id = NewPinnedItemId();
  item.title = title;
  item.url = url;
  item.favicon_url = favicon_url;
  item.created_at = base::Time::Now();
  item.updated_at = item.created_at;
  items_.push_back(std::move(item));
  return items_.back();
}

bool DaoPinnedTabModel::RemoveById(const std::string& id) {
  auto it = std::ranges::find(items_, id, &DaoPinnedTabItem::id);
  if (it == items_.end()) {
    return false;
  }
  items_.erase(it);
  return true;
}

bool DaoPinnedTabModel::Move(const std::string& id, size_t to_index) {
  auto it = std::ranges::find(items_, id, &DaoPinnedTabItem::id);
  if (it == items_.end()) {
    return false;
  }
  if (to_index >= items_.size()) {
    to_index = items_.size() - 1;
  }
  size_t from_index = static_cast<size_t>(std::distance(items_.begin(), it));
  if (from_index == to_index) {
    return false;
  }

  DaoPinnedTabItem item = std::move(*it);
  items_.erase(it);
  items_.insert(items_.begin() + to_index, std::move(item));
  return true;
}

}  // namespace dao
```

- [ ] **Step 3: Add the model to Chromium UI BUILD patch**

In `src/patches/chrome/browser/ui/BUILD.gn.patch`, add the files next to `dao_sidebar_ui`:

```diff
+    "//dao/browser/ui/webui/dao_pinned_tab_model.cc",
+    "//dao/browser/ui/webui/dao_pinned_tab_model.h",
     "//dao/browser/ui/webui/dao_sidebar_ui.cc",
     "//dao/browser/ui/webui/dao_sidebar_ui.h",
```

- [ ] **Step 4: Authorization checkpoint**

Stop and report the changed files. Do not run state-changing git commands without exact authorization.

## Task 4: Add C++ Sidebar Reconciliation And Commands

**Files:**
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.h`
- Modify: `src/dao/browser/ui/webui/dao_sidebar_ui.cc`
- Modify: `src/dao/browser/strings/dao_strings.grd`

- [ ] **Step 1: Add localized strings**

Add messages under the sidebar messages in `dao_strings.grd`:

```xml
      <message name="IDS_DAO_TAB_CONTEXT_PIN_TAB" desc="Context menu item that pins the selected tab into the Dao sidebar pinned grid.">
        Pin Tab
      </message>
      <message name="IDS_DAO_PINNED_TAB_CONTEXT_OPEN" desc="Context menu item that opens a dormant pinned sidebar item.">
        Open
      </message>
      <message name="IDS_DAO_PINNED_TAB_CONTEXT_UNPIN" desc="Context menu item that removes an item from the Dao sidebar pinned grid.">
        Unpin
      </message>
      <message name="IDS_DAO_PINNED_TAB_CONTEXT_CLOSE_TAB" desc="Context menu item that closes the open tab represented by a pinned sidebar item while keeping the pinned item.">
        Close Tab
      </message>
      <message name="IDS_DAO_PINNED_TAB_CONTEXT_COPY_LINK" desc="Context menu item that copies the URL represented by a pinned sidebar item.">
        Copy Link
      </message>
```

- [ ] **Step 2: Extend sidebar handler declarations**

In `dao_sidebar_ui.h`, include the model:

```cpp
#include "dao/browser/ui/webui/dao_pinned_tab_model.h"
```

Add handlers:

```cpp
  void HandlePinTab(const base::ListValue& args);
  void HandleUnpinPinnedItem(const base::ListValue& args);
  void HandleActivateOrOpenPinnedItem(const base::ListValue& args);
  void HandleClosePinnedItemTab(const base::ListValue& args);
  void HandleMovePinnedItem(const base::ListValue& args);
  void HandleShowPinnedItemContextMenu(const base::ListValue& args);
```

Add helpers:

```cpp
  base::Value::List BuildPinnedItems();
  base::Value::Dict BuildSidebarStateForTesting();
  int FindOpenPinnedTabIndexForItem(const DaoPinnedTabItem& item) const;
  void SavePinnedItems();
  void LoadPinnedItems();
  void OnPinnedItemsLoaded(std::string json);
  void PinTabAtIndex(int index);
  void UnpinPinnedItemForTesting(const std::string& id);
```

Extend command IDs:

```cpp
    kPinTab,
    kPinnedOpen,
    kPinnedUnpin,
    kPinnedCloseTab,
    kPinnedCopyLink,
```

Add state:

```cpp
  DaoPinnedTabModel pinned_tab_model_;
  bool pinned_items_loaded_ = false;
  std::string context_menu_pinned_item_id_;
```

For testing support, add methods on `DaoSidebarUI`:

```cpp
  base::Value::List GetPinnedItemsForTesting();
  base::Value::Dict GetSidebarStateForTesting();
  void PinTabForTesting(int index);
  void UnpinPinnedItemForTesting(const std::string& id);
```

- [ ] **Step 3: Register WebUI messages**

In `DaoSidebarUIHandler::RegisterMessages()` add:

```cpp
  web_ui()->RegisterMessageCallback(
      "pinTab",
      base::BindRepeating(&DaoSidebarUIHandler::HandlePinTab,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "unpinPinnedItem",
      base::BindRepeating(&DaoSidebarUIHandler::HandleUnpinPinnedItem,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "activateOrOpenPinnedItem",
      base::BindRepeating(&DaoSidebarUIHandler::HandleActivateOrOpenPinnedItem,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "closePinnedItemTab",
      base::BindRepeating(&DaoSidebarUIHandler::HandleClosePinnedItemTab,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "movePinnedItem",
      base::BindRepeating(&DaoSidebarUIHandler::HandleMovePinnedItem,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "showPinnedItemContextMenu",
      base::BindRepeating(&DaoSidebarUIHandler::HandleShowPinnedItemContextMenu,
                          base::Unretained(this)));
```

- [ ] **Step 4: Load pinned items on initial state**

Call `LoadPinnedItems()` from `HandleGetInitialState()` before `PushFullState()`:

```cpp
  if (!pinned_items_loaded_) {
    LoadPinnedItems();
    return;
  }

  PushFullState();
```

Implement async load/save next to folder load/save:

```cpp
void DaoSidebarUIHandler::LoadPinnedItems() {
  if (!browser_) return;
  Profile* profile = browser_->profile();
  if (!profile) return;
  base::FilePath path = profile->GetPath().AppendASCII("dao_pinned_tabs.json");

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](base::FilePath path) {
        std::string json;
        base::ReadFileToString(path, &json);
        return json;
      }, path),
      base::BindOnce(&DaoSidebarUIHandler::OnPinnedItemsLoaded,
                     weak_factory_.GetWeakPtr()));
}

void DaoSidebarUIHandler::OnPinnedItemsLoaded(std::string json) {
  pinned_tab_model_.LoadFromJson(json);
  pinned_items_loaded_ = true;
  PushFullState();
}

void DaoSidebarUIHandler::SavePinnedItems() {
  if (!browser_) return;
  Profile* profile = browser_->profile();
  if (!profile) return;
  std::string json = pinned_tab_model_.ToJson();
  base::FilePath path = profile->GetPath().AppendASCII("dao_pinned_tabs.json");
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(
          [](base::FilePath path, std::string json) {
            base::WriteFile(path, json);
          },
          path, std::move(json)));
}
```

Make sure `dao_sidebar_ui.cc` includes:

```cpp
#include "base/files/file_util.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "ui/base/l10n/l10n_util.h"
```

- [ ] **Step 5: Build pinned item state**

Add this implementation:

```cpp
int DaoSidebarUIHandler::FindOpenPinnedTabIndexForItem(
    const DaoPinnedTabItem& item) const {
  if (!browser_) return -1;
  TabStripModel* model = browser_->tab_strip_model();
  for (int i = 0; i < model->count(); ++i) {
    if (!model->IsTabPinned(i)) {
      continue;
    }
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    if (contents->GetVisibleURL().spec() == item.url) {
      return i;
    }
  }
  return -1;
}

base::Value::List DaoSidebarUIHandler::BuildPinnedItems() {
  base::Value::List items;
  if (!browser_) return items;

  TabStripModel* model = browser_->tab_strip_model();
  for (int i = 0; i < model->count(); ++i) {
    if (!model->IsTabPinned(i)) {
      continue;
    }
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    pinned_tab_model_.AddOrUpdate(
        base::UTF16ToUTF8(contents->GetTitle()),
        contents->GetVisibleURL().spec(),
        FaviconToDataUrl(contents));
  }

  for (const DaoPinnedTabItem& item : pinned_tab_model_.items()) {
    int open_index = FindOpenPinnedTabIndexForItem(item);
    base::Value::Dict dict;
    dict.Set("id", item.id);
    dict.Set("title", item.title);
    dict.Set("url", item.url);
    dict.Set("faviconUrl", item.favicon_url);
    dict.Set("isOpen", open_index >= 0);
    dict.Set("openTabIndex", open_index);
    dict.Set("isActive", open_index >= 0 &&
                            open_index == model->active_index());
    if (open_index >= 0) {
      content::WebContents* contents = model->GetWebContentsAt(open_index);
      dict.Set("isFaviconLight", IsFaviconLight(contents));
    }
    items.Append(std::move(dict));
  }
  SavePinnedItems();
  return items;
}
```

Set it in `PushFullState()`:

```cpp
  state.Set("pinnedItems", BuildPinnedItems());
```

- [ ] **Step 6: Implement pin/open/close/unpin handlers**

Add:

```cpp
void DaoSidebarUIHandler::PinTabAtIndex(int index) {
  if (!browser_) return;
  TabStripModel* model = browser_->tab_strip_model();
  if (index < 0 || index >= model->count()) return;
  content::WebContents* contents = model->GetWebContentsAt(index);
  if (!contents) return;

  pinned_tab_model_.AddOrUpdate(
      base::UTF16ToUTF8(contents->GetTitle()),
      contents->GetVisibleURL().spec(),
      FaviconToDataUrl(contents));
  model->SetTabPinned(index, true);
  SavePinnedItems();
  PushFullState();
}

void DaoSidebarUIHandler::HandlePinTab(const base::ListValue& args) {
  int index = args.empty() ? -1 : args[0].GetIfInt().value_or(-1);
  PinTabAtIndex(index);
}

void DaoSidebarUIHandler::HandleActivateOrOpenPinnedItem(
    const base::ListValue& args) {
  if (!browser_ || args.empty()) return;
  const std::string* id = args[0].GetIfString();
  if (!id) return;
  DaoPinnedTabItem* item = pinned_tab_model_.FindById(*id);
  if (!item) return;

  int open_index = FindOpenPinnedTabIndexForItem(*item);
  TabStripModel* model = browser_->tab_strip_model();
  if (open_index >= 0) {
    model->ActivateTabAt(open_index);
    return;
  }

  GURL url(item->url);
  if (!url.is_valid()) return;
  NavigateParams params(browser_, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  int active_index = model->active_index();
  if (active_index >= 0) {
    model->SetTabPinned(active_index, true);
  }
  PushFullState();
}

void DaoSidebarUIHandler::HandleClosePinnedItemTab(
    const base::ListValue& args) {
  if (!browser_ || args.empty()) return;
  const std::string* id = args[0].GetIfString();
  if (!id) return;
  DaoPinnedTabItem* item = pinned_tab_model_.FindById(*id);
  if (!item) return;
  int open_index = FindOpenPinnedTabIndexForItem(*item);
  if (open_index < 0) return;
  browser_->tab_strip_model()->CloseWebContentsAt(
      open_index, TabCloseTypes::CLOSE_USER_GESTURE);
  PushFullState();
}

void DaoSidebarUIHandler::HandleUnpinPinnedItem(
    const base::ListValue& args) {
  if (!browser_ || args.empty()) return;
  const std::string* id = args[0].GetIfString();
  if (!id) return;
  DaoPinnedTabItem* item = pinned_tab_model_.FindById(*id);
  if (!item) return;
  int open_index = FindOpenPinnedTabIndexForItem(*item);
  if (open_index >= 0) {
    browser_->tab_strip_model()->SetTabPinned(open_index, false);
  }
  pinned_tab_model_.RemoveById(*id);
  SavePinnedItems();
  PushFullState();
}

void DaoSidebarUIHandler::HandleMovePinnedItem(
    const base::ListValue& args) {
  if (args.size() < 2) return;
  const std::string* id = args[0].GetIfString();
  int to_index = args[1].GetIfInt().value_or(-1);
  if (!id || to_index < 0) return;
  if (pinned_tab_model_.Move(*id, static_cast<size_t>(to_index))) {
    SavePinnedItems();
    PushFullState();
  }
}
```

- [ ] **Step 7: Implement pinned context menu**

Add handler:

```cpp
void DaoSidebarUIHandler::HandleShowPinnedItemContextMenu(
    const base::ListValue& args) {
  if (!browser_ || args.size() < 3) return;
  const std::string* id = args[0].GetIfString();
  if (!id) return;
  int screen_x = args[1].GetIfInt().value_or(0);
  int screen_y = args[2].GetIfInt().value_or(0);
  DaoPinnedTabItem* item = pinned_tab_model_.FindById(*id);
  if (!item) return;

  context_menu_tab_index_ = -1;
  context_menu_pinned_item_id_ = *id;
  tab_context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);

  int open_index = FindOpenPinnedTabIndexForItem(*item);
  if (open_index >= 0) {
    tab_context_menu_model_->AddItem(
        kPinnedCloseTab,
        l10n_util::GetStringUTF16(IDS_DAO_PINNED_TAB_CONTEXT_CLOSE_TAB));
  } else {
    tab_context_menu_model_->AddItem(
        kPinnedOpen,
        l10n_util::GetStringUTF16(IDS_DAO_PINNED_TAB_CONTEXT_OPEN));
  }
  tab_context_menu_model_->AddItem(
      kPinnedUnpin,
      l10n_util::GetStringUTF16(IDS_DAO_PINNED_TAB_CONTEXT_UNPIN));
  tab_context_menu_model_->AddItem(
      kPinnedCopyLink,
      l10n_util::GetStringUTF16(IDS_DAO_PINNED_TAB_CONTEXT_COPY_LINK));

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view || !browser_view->dao_sidebar()) return;
  views::Widget* widget = browser_view->dao_sidebar()->GetWidget();
  if (!widget) return;

  tab_context_menu_runner_ = std::make_unique<views::MenuRunner>(
      tab_context_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);
  gfx::Rect anchor_rect(gfx::Point(screen_x, screen_y), gfx::Size());
  tab_context_menu_runner_->RunMenuAt(
      widget, nullptr, anchor_rect,
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
}
```

Update normal tab menu to include pin:

```cpp
  if (!model->IsTabPinned(tab_index)) {
    tab_context_menu_model_->AddItem(
        kPinTab,
        l10n_util::GetStringUTF16(IDS_DAO_TAB_CONTEXT_PIN_TAB));
    tab_context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  }
```

Extend `IsCommandIdEnabled()`:

```cpp
    case kPinTab:
      return !model->IsTabPinned(context_menu_tab_index_);
    case kPinnedOpen:
    case kPinnedUnpin:
    case kPinnedCloseTab:
    case kPinnedCopyLink:
      return !context_menu_pinned_item_id_.empty();
```

Extend `ExecuteCommand()` before the normal tab-index guard:

```cpp
  if (!context_menu_pinned_item_id_.empty()) {
    base::Value::List args;
    args.Append(context_menu_pinned_item_id_);
    switch (command_id) {
      case kPinnedOpen:
        HandleActivateOrOpenPinnedItem(args);
        break;
      case kPinnedUnpin:
        HandleUnpinPinnedItem(args);
        break;
      case kPinnedCloseTab:
        HandleClosePinnedItemTab(args);
        break;
      case kPinnedCopyLink: {
        const DaoPinnedTabItem* item =
            pinned_tab_model_.FindById(context_menu_pinned_item_id_);
        if (item) {
          ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
          writer.WriteText(base::UTF8ToUTF16(item->url));
        }
        break;
      }
      default:
        break;
    }
    ClearContextMenuState();
    return;
  }
```

Add `kPinTab` to the normal switch:

```cpp
    case kPinTab:
      PinTabAtIndex(context_menu_tab_index_);
      break;
```

Clear pinned menu state:

```cpp
  context_menu_pinned_item_id_.clear();
```

- [ ] **Step 8: Add testing accessors**

In `DaoSidebarUI`, implement:

```cpp
base::Value::List DaoSidebarUI::GetPinnedItemsForTesting() {
  return handler_ ? handler_->BuildPinnedItems() : base::Value::List();
}

void DaoSidebarUI::PinTabForTesting(int index) {
  if (handler_) {
    handler_->PinTabAtIndex(index);
  }
}
```

Move `BuildPinnedItems()` and `PinTabAtIndex(int index)` from private to public on `DaoSidebarUIHandler`, and add this public helper:

```cpp
void DaoSidebarUIHandler::UnpinPinnedItemForTesting(const std::string& id) {
  base::Value::List args;
  args.Append(id);
  HandleUnpinPinnedItem(args);
}
```

Add a state helper that mirrors `PushFullState()` without firing a WebUI event:

```cpp
base::Value::Dict DaoSidebarUIHandler::BuildSidebarStateForTesting() {
  base::Value::Dict state;
  state.Set("pinnedItems", BuildPinnedItems());
  state.Set("pinnedTabs", base::Value::List());
  state.Set("unpinnedTabs", base::Value::List());
  if (!browser_) {
    state.Set("activeIndex", -1);
    state.Set("sessionId", 0);
    return state;
  }

  TabStripModel* model = browser_->tab_strip_model();
  base::Value::List pinned_tabs;
  base::Value::List unpinned_tabs;
  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents) continue;
    base::Value::Dict tab;
    tab.Set("tabId", GetSidebarTabId(contents));
    tab.Set("index", i);
    tab.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
    tab.Set("url", contents->GetVisibleURL().spec());
    tab.Set("faviconUrl", FaviconToDataUrl(contents));
    tab.Set("isFaviconLight", IsFaviconLight(contents));
    tab.Set("isActive", i == model->active_index());
    tab.Set("isPinned", model->IsTabPinned(i));
    tab.Set("isAudible", IsTabAudible(contents));
    tab.Set("isMuted", contents->IsAudioMuted());
    tab.Set("isAgentLocked", DaoAgentLockTabHelper::IsLocked(contents));
    tab.Set("isInSplit", IsInAnySplitGroup(contents));
    if (model->IsTabPinned(i)) {
      pinned_tabs.Append(std::move(tab));
    } else {
      unpinned_tabs.Append(std::move(tab));
    }
  }
  state.Set("pinnedTabs", std::move(pinned_tabs));
  state.Set("unpinnedTabs", std::move(unpinned_tabs));
  state.Set("activeIndex", model->active_index());
  state.Set("sessionId", static_cast<int>(browser_->session_id().id()));
  return state;
}
```

Then implement the `DaoSidebarUI` forwarding methods:

```cpp
base::Value::Dict DaoSidebarUI::GetSidebarStateForTesting() {
  return handler_ ? handler_->BuildSidebarStateForTesting()
                  : base::Value::Dict();
}

void DaoSidebarUI::UnpinPinnedItemForTesting(const std::string& id) {
  if (handler_) {
    handler_->UnpinPinnedItemForTesting(id);
  }
}
```

- [ ] **Step 9: Run compile confirmation**

Run only:

```bash
npm run rebuild
```

Expected: build completes successfully. If it fails, fix compile errors in the touched Dao-owned files and rerun `npm run rebuild`.

- [ ] **Step 10: Authorization checkpoint**

Stop and report the changed files and `npm run rebuild` result. Do not run state-changing git commands without exact authorization.

## Task 5: Add Reorder Support In The Grid

**Files:**
- Modify: `src/dao/browser/ui/webui/resources/sidebar/dao_pinned_tabs_grid.ts`
- Modify: `src/dao/browser/ui/webui/resources/sidebar/__tests__/pinned_tabs_grid.test.ts`

- [ ] **Step 1: Add failing reorder test**

Add:

```ts
it('sends movePinnedItem after dropping a tile before another tile', async () => {
  const {el, send} = await loadGrid();
  el.items = [
    item({id: 'a', title: 'A'}),
    item({id: 'b', title: 'B'}),
  ];
  await el.updateComplete;

  const tiles = el.shadowRoot!.querySelectorAll('.tile');
  const data = new DataTransfer();
  tiles[0]!.dispatchEvent(new DragEvent('dragstart', {
    bubbles: true,
    dataTransfer: data,
  }));
  tiles[1]!.dispatchEvent(new DragEvent('drop', {
    bubbles: true,
    cancelable: true,
    dataTransfer: data,
  }));

  expect(send).toHaveBeenCalledWith('movePinnedItem', ['a', 1]);
});
```

- [ ] **Step 2: Run the failing reorder test**

Run:

```bash
npm run test:webui -- pinned_tabs_grid
```

Expected: FAIL because drag handlers are not implemented.

- [ ] **Step 3: Implement tile drag and drop**

In `dao_pinned_tabs_grid.ts`, add private state:

```ts
  private draggedItemId_: string = '';
```

Add attributes to the button:

```ts
              draggable="true"
              @dragstart=${(e: DragEvent) => this.onDragStart_(e, item)}
              @dragover=${this.onDragOver_}
              @drop=${(e: DragEvent) => this.onDrop_(e, item)}
```

Add methods:

```ts
  private onDragStart_(e: DragEvent, item: PinnedItemData) {
    this.draggedItemId_ = item.id;
    if (e.dataTransfer) {
      e.dataTransfer.setData('text/plain', item.id);
      e.dataTransfer.effectAllowed = 'move';
    }
  }

  private onDragOver_(e: DragEvent) {
    e.preventDefault();
    if (e.dataTransfer) {
      e.dataTransfer.dropEffect = 'move';
    }
  }

  private onDrop_(e: DragEvent, target: PinnedItemData) {
    e.preventDefault();
    e.stopPropagation();
    const draggedId = e.dataTransfer?.getData('text/plain') ||
        this.draggedItemId_;
    if (!draggedId || draggedId === target.id) {
      return;
    }
    const toIndex = this.items.findIndex(item => item.id === target.id);
    if (toIndex < 0) {
      return;
    }
    sendNative('movePinnedItem', draggedId, toIndex);
    this.draggedItemId_ = '';
  }
```

- [ ] **Step 4: Run WebUI verification**

Run:

```bash
npm run test:webui -- pinned_tabs_grid
npm run lint:lit
```

Expected: both commands pass.

- [ ] **Step 5: Authorization checkpoint**

Stop and report the changed files. Do not run state-changing git commands without exact authorization.

## Task 6: Add Browser Behavior Tests

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Add dormant close/reopen test**

Add:

```cpp
IN_PROC_BROWSER_TEST_F(DaoTabBrowserTest, ClosingPinnedTabLeavesDormantItem) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("https://example.com/"),
                            ui::PAGE_TRANSITION_TYPED));
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_NE(nullptr, browser_view);
  auto* sidebar_ui =
      browser_view->dao_sidebar()->GetSidebarUIForTesting();
  ASSERT_NE(nullptr, sidebar_ui);

  sidebar_ui->PinTabForTesting(1);
  TabStripModel* model = browser()->tab_strip_model();
  ASSERT_TRUE(model->IsTabPinned(0));

  model->CloseWebContentsAt(0, TabCloseTypes::CLOSE_USER_GESTURE);

  base::Value::List items = sidebar_ui->GetPinnedItemsForTesting();
  ASSERT_EQ(1u, items.size());
  const base::Value::Dict& item = items[0].GetDict();
  EXPECT_FALSE(item.FindBool("isOpen").value_or(true));
  EXPECT_EQ(-1, item.FindInt("openTabIndex").value_or(0));
}
```

- [ ] **Step 2: Add unpin-open keeps tab test**

Add a testing method `UnpinPinnedItemForTesting(const std::string& id)` on `DaoSidebarUI`, then add:

```cpp
IN_PROC_BROWSER_TEST_F(DaoTabBrowserTest, UnpinOpenPinnedItemKeepsTabOpen) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("https://example.com/"),
                            ui::PAGE_TRANSITION_TYPED));
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_NE(nullptr, browser_view);
  auto* sidebar_ui =
      browser_view->dao_sidebar()->GetSidebarUIForTesting();
  ASSERT_NE(nullptr, sidebar_ui);

  sidebar_ui->PinTabForTesting(1);
  base::Value::List items = sidebar_ui->GetPinnedItemsForTesting();
  ASSERT_EQ(1u, items.size());
  std::string id = *items[0].GetDict().FindString("id");

  sidebar_ui->UnpinPinnedItemForTesting(id);

  TabStripModel* model = browser()->tab_strip_model();
  EXPECT_EQ(2, model->count());
  EXPECT_FALSE(model->IsTabPinned(0));
  EXPECT_TRUE(sidebar_ui->GetPinnedItemsForTesting().empty());
}
```

- [ ] **Step 3: Add folder isolation regression**

Add:

```cpp
IN_PROC_BROWSER_TEST_F(DaoTabBrowserTest, PinnedTabsStayOutOfUnpinnedState) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("https://example.com/"),
                            ui::PAGE_TRANSITION_TYPED));
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_NE(nullptr, browser_view);
  auto* sidebar_ui =
      browser_view->dao_sidebar()->GetSidebarUIForTesting();
  ASSERT_NE(nullptr, sidebar_ui);

  sidebar_ui->PinTabForTesting(1);
  base::Value::Dict state = sidebar_ui->GetSidebarStateForTesting();
  const base::Value::List* unpinned = state.FindList("unpinnedTabs");
  ASSERT_NE(nullptr, unpinned);
  for (const base::Value& value : *unpinned) {
    EXPECT_FALSE(value.GetDict().FindBool("isPinned").value_or(false));
  }
}
```

- [ ] **Step 4: Run Dao browser tests if practical**

Run:

```bash
npm run test
```

Expected: Dao browser tests pass. Treat this as test verification, not compile confirmation.

- [ ] **Step 5: Run compile confirmation**

Run:

```bash
npm run rebuild
```

Expected: build completes successfully.

- [ ] **Step 6: Authorization checkpoint**

Stop and report test/build results. Do not run state-changing git commands without exact authorization.

## Task 7: Final Verification And Cleanup

**Files:**
- Review all files touched by Tasks 1-6.

- [ ] **Step 1: Run WebUI checks**

Run:

```bash
npm run test:webui
npm run lint:lit
```

Expected: both commands pass.

- [ ] **Step 2: Run integration checks**

Run:

```bash
npm run test
npm run rebuild
```

Expected: `npm run test` passes; `npm run rebuild` passes and is the compile confirmation.

- [ ] **Step 3: Inspect tracked diff**

Read-only command:

```bash
git diff --stat
git diff -- src/dao/browser/ui/webui/dao_sidebar_ui.cc \
  src/dao/browser/ui/webui/dao_sidebar_ui.h \
  src/dao/browser/ui/webui/dao_pinned_tab_model.cc \
  src/dao/browser/ui/webui/dao_pinned_tab_model.h \
  src/dao/browser/ui/webui/resources/sidebar/dao_pinned_tabs_grid.ts \
  src/dao/browser/ui/webui/resources/sidebar/dao_sidebar_app.ts \
  src/dao/browser/ui/webui/resources/sidebar/sidebar_bridge.ts
```

Expected: diff only contains the pinned tabs grid work and generated build patch/source list changes.

- [ ] **Step 4: Request exact git authorization if needed**

If the user wants a commit, ask for exact authorization to run:

```bash
git add <specific changed files>
git commit -m "feat(sidebar): add persistent pinned tab grid"
```

Do not run those commands until the user explicitly authorizes that exact action.

## Self-Review Notes

- Spec coverage: persistent grid, active/open/dormant states, click-to-reopen, close-to-dormant, unpin behavior, reorder, context menus, folder isolation, i18n, and verification commands all map to tasks above.
- Scope control: drag normal tab into pinned grid and drag pinned tile into Today/folders remain out of scope.
- Git safety: plan uses authorization checkpoints instead of automatic commit steps because this repository forbids state-changing git commands without explicit latest-message authorization.
