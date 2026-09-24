// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

import {createTabRefMatchPool, FolderModel} from '../dao_folder_model.js';
import type {SidebarTabRef, TabData} from '../sidebar_bridge.js';

function tab(
    tabId: string, url: string, title: string,
    extra: Partial<TabData> = {}): TabData {
  return {
    tabId,
    index: 0,
    title,
    url,
    faviconUrl: '',
    isActive: false,
    isPinned: false,
    isAudible: false,
    isMuted: false,
    ...extra,
  };
}

describe('FolderModel', () => {
  beforeEach(() => {
    let uuidCounter = 0;
    vi.spyOn(crypto, 'randomUUID').mockImplementation(() => {
      uuidCounter++;
      return `folder${uuidCounter}-1234-4000-8000-000000000000`;
    });
  });

  it('preserves both windows and incremental restore across a save/reload', () => {
    const first = tab('window-a', 'https://a.example', 'A');
    const second = tab('window-b', 'https://b.example', 'B');
    const original = new FolderModel();
    const folder = original.addFolder('Work');
    original.moveTabToFolder(first, folder.id);
    const model = new FolderModel();
    model.loadFromJson(original.toJson());
    model.reconcile([second]);
    const restarted = new FolderModel();
    restarted.loadFromJson(model.toJson());
    restarted.reconcile([]);
    restarted.reconcile([first]);
    expect(restarted.getMatchedTabs(folder.id, [first])).toEqual([first]);
    expect(restarted.getOrderedItems()).toContainEqual(
        expect.objectContaining({tabId: second.tabId}));
  });

  it('does not steal a same-URL tab from another window', () => {
    const model = new FolderModel();
    const folder = model.addFolder('Work');
    model.moveTabToFolder(tab('window-a', 'https://same.example', 'Same'), folder.id);
    model.reconcile([tab('window-b', 'https://same.example', 'Same')]);
    expect(model.getFolders()[0]!.children[0]!.tabId).toBe('window-a');
    expect(model.getMatchedTabs(folder.id, [
      tab('window-b', 'https://same.example', 'Same'),
    ])).toEqual([]);
  });

  it.each(['loose', 'folder'])('does not place a new tab in a legacy %s slot', location => {
    const existing = tab('existing', 'https://existing.example', 'Existing');
    const fresh = tab('fresh', 'https://same.example', 'Same');
    const legacy: SidebarTabRef = {type: 'tab', url: fresh.url, title: fresh.title};
    const model = new FolderModel();
    model.loadFromJson(JSON.stringify({version: 1, items: [
      {type: 'tab', ...existing},
      location === 'loose' ? legacy : {
        type: 'folder', id: 'legacy', name: 'Legacy', collapsed: false,
        children: [legacy],
      },
    ]}));

    // Rendering and reconciliation must agree before either updates the refs.
    const pool = createTabRefMatchPool(model.getOrderedItems(), [fresh, existing]);
    expect(pool.consume(legacy)).toBeNull();
    model.reconcile([fresh, existing]);
    expect(model.getOrderedItems()[0]).toMatchObject({tabId: fresh.tabId});
    expect(model.findTabFolder(fresh)).toBeNull();

    const reloaded = new FolderModel();
    reloaded.loadFromJson(model.toJson());
    const restored = tab('restored', fresh.url, fresh.title, {isSessionRestored: true});
    reloaded.reconcile([fresh, existing, restored]);
    expect(reloaded.getOrderedItems()[0]).toMatchObject({tabId: fresh.tabId});
    if (location === 'folder') {
      expect(reloaded.getMatchedTabs('legacy', [fresh, existing, restored]))
          .toEqual([restored]);
    } else {
      expect(reloaded.getOrderedItems()[2]).toMatchObject({tabId: restored.tabId});
    }
  });

  it('places a new index-zero tab above the bottom folder containing its opener', () => {
    const opener = tab('opener', 'https://opener.example', 'Opener');
    const existing = tab('existing', 'https://existing.example', 'Existing');
    const fresh = tab('fresh', 'https://new.example', 'New');
    const model = new FolderModel();
    model.reconcile([existing, opener]);
    const folder = model.addFolder('Bottom');
    model.moveTabToFolder(opener, folder.id);
    model.reconcile([fresh, existing, opener]);
    expect(model.getOrderedItems().map(item => item.type === 'tab' ? item.tabId : item.id))
        .toEqual(['fresh', 'existing', folder.id]);
  });

  it('persists stable tab identities across restart', () => {
    const model = new FolderModel();
    const folder = model.addFolder('Research');

    model.moveTabToFolder(
        tab('runtime-1', 'https://example.com/a', 'A'), folder.id);

    const persisted = JSON.parse(model.toJson());
    expect(persisted.items[0].children[0]).toEqual({
      type: 'tab',
      tabId: 'runtime-1',
      url: 'https://example.com/a',
      title: 'A',
    });
  });

  it('releases folder children at the folder position when unfoldering', () => {
    const model = new FolderModel();
    expect(model.loadFromJson(JSON.stringify({
      version: 1,
      items: [
        {type: 'tab', url: 'https://first.example', title: 'First'},
        {
          type: 'folder',
          id: 'f1',
          name: 'Folder',
          collapsed: false,
          children: [
            {type: 'tab', url: 'https://child.example', title: 'Child'},
          ],
        },
        {type: 'tab', url: 'https://last.example', title: 'Last'},
      ],
    }))).toBe(true);

    expect(model.unfolder('f1')).toBe(true);

    expect(model.getOrderedItems().map(item => item.type === 'tab'
      ? item.title
        : item.name)).toEqual(['First', 'Child', 'Last']);
  });

  it('deletes a folder together with its child references', () => {
    const model = new FolderModel();
    expect(model.loadFromJson(JSON.stringify({
      version: 1,
      items: [
        {type: 'tab', url: 'https://first.example', title: 'First'},
        {
          type: 'folder',
          id: 'f1',
          name: 'Folder',
          collapsed: false,
          children: [
            {type: 'tab', url: 'https://child.example', title: 'Child'},
          ],
        },
        {type: 'tab', url: 'https://last.example', title: 'Last'},
      ],
    }))).toBe(true);

    expect(model.deleteFolder('f1')).toBe(true);

    expect(model.getOrderedItems().map(item => item.type === 'tab'
      ? item.title
      : item.name)).toEqual(['First', 'Last']);
  });

  it('reports missing folders without changing the model', () => {
    const model = new FolderModel();
    model.loadFromJson(JSON.stringify({
      version: 1,
      items: [{type: 'tab', url: 'https://first.example', title: 'First'}],
    }));

    expect(model.unfolder('missing')).toBe(false);
    expect(model.deleteFolder('missing')).toBe(false);
    expect(model.getOrderedItems()).toEqual([
      {type: 'tab', url: 'https://first.example', title: 'First'},
    ]);
  });

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

  it('retains unmatched refs until an explicit tab close', () => {
    const model = new FolderModel();
    model.loadFromJson(JSON.stringify({
      version: 1,
      items: [
        {type: 'tab', tabId: 'a', url: 'https://a.example', title: 'A'},
        {
          type: 'folder',
          id: 'f1',
          name: 'Reading',
          collapsed: true,
          children: [
            {type: 'tab', tabId: 'b', url: 'https://b.example', title: 'B'},
            {type: 'tab', tabId: 'stale', url: 'https://gone.example', title: 'Gone'},
          ],
        },
      ],
    }));

    model.reconcile([
      tab('a', 'https://a.example', 'A'),
      tab('c', 'https://c.example', 'C'),
      tab('b', 'https://b.example', 'B'),
      tab('d', 'https://d.example', 'D'),
    ]);

    const items = model.getOrderedItems();
    expect(items.map(item => item.type === 'tab' ? item.title : item.name))
        .toEqual(['A', 'C', 'Reading', 'D']);
    expect(items[2]).toMatchObject({
      type: 'folder',
      id: 'f1',
      collapsed: true,
      children: [
        {type: 'tab', tabId: 'b', title: 'B'},
        {type: 'tab', tabId: 'stale', title: 'Gone'},
      ],
    });
  });

  it('does not rematch a closed runtime tab to a duplicate URL', () => {
    const model = new FolderModel();
    model.loadFromJson(JSON.stringify({
      version: 1,
      items: [
        {
          type: 'tab',
          url: 'https://docs.example',
          title: 'Docs',
        },
        {
          type: 'tab',
          url: 'https://middle.example',
          title: 'Middle',
        },
        {
          type: 'tab',
          url: 'https://docs.example',
          title: 'Docs',
        },
      ],
    }));

    model.reconcile([
      tab('duplicate-a', 'https://docs.example', 'Docs', {isSessionRestored: true}),
      tab('duplicate-b', 'https://docs.example', 'Docs', {isSessionRestored: true}),
      tab('middle', 'https://middle.example', 'Middle', {isSessionRestored: true}),
    ]);

    model.forgetTabs(['duplicate-a']);
    model.reconcile([
      tab('duplicate-b', 'https://docs.example', 'Docs'),
      tab('middle', 'https://middle.example', 'Middle'),
    ]);

    expect(model.getOrderedItems().map(item =>
      item.type === 'tab' ? item.tabId : item.id))
        .toEqual(['middle', 'duplicate-b']);
  });

  it('keeps a replaced WebContents with its copied stable identity in its folder', () => {
    const model = new FolderModel();
    model.loadFromJson(JSON.stringify({
      version: 1,
      items: [{
        type: 'folder',
        id: 'reading',
        name: 'Reading',
        collapsed: false,
        children: [{
          type: 'tab',
          tabId: 'old-contents',
          url: 'https://article.example',
          title: 'Article',
        }],
      }],
    }));

    model.reconcile([
      tab('old-contents', 'https://article.example', 'Updated article'),
    ]);

    expect(model.getOrderedItems()).toMatchObject([{
      type: 'folder',
      id: 'reading',
      children: [{tabId: 'old-contents', title: 'Updated article'}],
    }]);
  });

  it('resolves only the current tabs belonging to a folder', () => {
    const model = new FolderModel();
    model.loadFromJson(JSON.stringify({
      version: 1,
      items: [
        {
          type: 'tab',
          tabId: 'outside',
          url: 'https://duplicate.example',
          title: 'Duplicate',
        },
        {
          type: 'folder',
          id: 'stale-id',
          name: 'stale',
          collapsed: false,
          children: [
            {
              type: 'tab',
              tabId: 'stale-a',
              url: 'https://duplicate.example',
              title: 'Duplicate',
            },
            {
              type: 'tab',
              tabId: 'stale-b',
              url: 'https://stale-b.example',
              title: 'Stale B',
            },
          ],
        },
      ],
    }));
    const runtimeTabs = [
      tab('outside', 'https://duplicate.example', 'Duplicate'),
      tab('stale-a', 'https://duplicate.example', 'Duplicate'),
      tab('stale-b', 'https://stale-b.example', 'Stale B'),
    ];

    expect(model.getMatchedTabs('stale-id', runtimeTabs).map(tab => tab.tabId))
        .toEqual(['stale-a', 'stale-b']);
    expect(model.getMatchedTabs('missing', runtimeTabs)).toEqual([]);
  });

  it('keeps loose split siblings adjacent after reconcile', () => {
    const model = new FolderModel();
    model.loadFromJson(JSON.stringify({
      version: 1,
      items: [
        {type: 'tab', tabId: 'a', url: 'https://a.example', title: 'A'},
        {type: 'tab', tabId: 'c', url: 'https://c.example', title: 'C'},
        {type: 'tab', tabId: 'b', url: 'https://b.example', title: 'B'},
      ],
    }));

    model.reconcile([
      tab('a', 'https://a.example', 'A', {isInSplit: true}),
      tab('b', 'https://b.example', 'B', {isInSplit: true}),
      tab('c', 'https://c.example', 'C'),
    ]);

    expect(model.getOrderedItems().map(item => item.type === 'tab'
      ? item.title
      : item.name)).toEqual(['A', 'B', 'C']);
  });
});
