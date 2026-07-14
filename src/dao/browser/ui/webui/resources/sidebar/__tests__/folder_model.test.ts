// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {beforeEach, describe, expect, it, vi} from 'vitest';

import {FolderModel} from '../dao_folder_model.js';
import type {TabData} from '../sidebar_bridge.js';

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

  it('serializes folders without volatile runtime tab ids', () => {
    const model = new FolderModel();
    const folder = model.addFolder('Research');

    model.moveTabToFolder(
        tab('runtime-1', 'https://example.com/a', 'A'), folder.id);

    const persisted = JSON.parse(model.toJson());
    expect(persisted.items[0].children[0]).toEqual({
      type: 'tab',
      url: 'https://example.com/a',
      title: 'A',
    });
    expect(persisted.items[0].children[0]).not.toHaveProperty('tabId');
  });

  it('releases folder children at the folder position when deleting', () => {
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

    model.deleteFolder('f1');

    expect(model.getOrderedItems().map(item => item.type === 'tab'
      ? item.title
      : item.name)).toEqual(['First', 'Child', 'Last']);
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

  it('reconciles stored folders with actual tabs and drops stale refs', () => {
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
      children: [{type: 'tab', tabId: 'b', title: 'B'}],
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
      tab('duplicate-a', 'https://docs.example', 'Docs'),
      tab('duplicate-b', 'https://docs.example', 'Docs'),
      tab('middle', 'https://middle.example', 'Middle'),
    ]);

    model.reconcile([
      tab('duplicate-b', 'https://docs.example', 'Docs'),
      tab('middle', 'https://middle.example', 'Middle'),
    ]);

    expect(model.getOrderedItems().map(item =>
      item.type === 'tab' ? item.tabId : item.id))
        .toEqual(['middle', 'duplicate-b']);
  });

  it('keeps a replaced WebContents in its folder', () => {
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
      tab('new-contents', 'https://article.example', 'Article'),
    ]);

    expect(model.getOrderedItems()).toMatchObject([{
      type: 'folder',
      id: 'reading',
      children: [{tabId: 'new-contents'}],
    }]);
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
