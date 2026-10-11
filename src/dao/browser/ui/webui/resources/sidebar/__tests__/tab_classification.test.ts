// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {describe, expect, it} from 'vitest';

import {FolderModel} from '../dao_folder_model.js';
import {applyTabClassification, getClassificationTabs} from '../dao_tab_classification.js';
import type {TabData} from '../sidebar_bridge.js';

function tab(tabId: string, extra: Partial<TabData> = {}): TabData {
  return {tabId, index: 0, title: tabId, url: `https://${tabId}.example/`,
    faviconUrl: '', isActive: false, isPinned: false, isAudible: false,
    isMuted: false, ...extra};
}

describe('tab classification', () => {
  it('selects only loose tabs below the anchor in sidebar order', () => {
    const tabs = ['a', 'b', 'c', 'd', 'e'].map(id => tab(id));
    const model = new FolderModel();
    model.reconcile(tabs);
    model.reorder(2, 0);
    const folder = model.addFolder('Work');
    model.moveTabsToFolder([tabs[3]!], folder.id);
    expect(getClassificationTabs(model, tabs, 'a').map(t => t.tabId))
        .toEqual(['b', 'e']);
    expect(getClassificationTabs(model, tabs, 'missing')).toEqual([]);
  });

  it('excludes pinned, split, controlled and non-web tabs', () => {
    const tabs = [tab('normal'), tab('pin', {isPinned: true}),
      tab('split', {isInSplit: true}), tab('locked', {isAgentLocked: true}),
      tab('mcp', {isMcpControlled: true}), tab('internal', {url: 'dao://settings'})];
    const model = new FolderModel();
    model.reconcile(tabs);
    expect(getClassificationTabs(model, tabs).map(t => t.tabId))
        .toEqual(['normal']);
  });

  it('reuses existing folders and preserves the selected tabs visual order', () => {
    const tabs = [tab('a'), tab('b')];
    const model = new FolderModel();
    model.reconcile(tabs);
    const folder = model.addFolder('Work');
    const folders = model.getFolders().map(({id, name}) => ({id, name}));
    expect(applyTabClassification(model, tabs, tabs, folders, [
      {folderId: '', name: ' work ', tabIds: ['b', 'a']},
    ])).toBe(2);
    expect(model.getFolders()).toHaveLength(1);
    expect(model.getMatchedTabs(folder.id, tabs).map(t => t.tabId))
        .toEqual(['a', 'b']);
  });

  it('skips tabs closed, navigated, pinned or manually grouped while waiting', () => {
    const original = ['closed', 'navigated', 'pinned', 'grouped', 'valid'].map(id => tab(id));
    const model = new FolderModel();
    model.reconcile(original);
    const work = model.addFolder('Work');
    const folders = [{id: work.id, name: work.name}];
    model.moveTabsToFolder([original[3]!], work.id);
    const current = [tab('navigated', {url: 'https://other.example/'}),
      tab('pinned', {isPinned: true}), original[3]!, original[4]!, tab('new')];
    expect(applyTabClassification(model, current, original, folders, [
      {folderId: '', name: 'Reading', tabIds: original.map(t => t.tabId)},
    ])).toBe(1);
    expect(model.findTabFolder(current[0]!)).toBeNull();
    expect(model.findTabFolder(current[1]!)).toBeNull();
    expect(model.findTabFolder(current[2]!)).toBe(work.id);
    expect(model.findTabFolder(current[4]!)).toBeNull();
    expect(model.findFolderByName('Reading')!.children.map(t => t.tabId))
        .toEqual(['valid']);
  });

  it.each([
    [{folderId: '', name: 'Work', tabIds: ['a', 'foreign']}],
    [{folderId: '', name: 'Work', tabIds: ['a', 'a']}],
    [{folderId: '', name: 'Work', tabIds: ['a']}],
    [{folderId: 'unknown', name: 'Work', tabIds: ['a', 'b']}],
    [{folderId: '', name: ' ', tabIds: ['a', 'b']}],
    [{folderId: '', name: 'x'.repeat(101), tabIds: ['a', 'b']}],
    [{folderId: '', name: 'Work', tabIds: ['a']},
      {folderId: '', name: ' work ', tabIds: ['b']}],
  ])('rejects invalid output before mutating any folder (%j)', (...groups) => {
    const model = new FolderModel();
    const tabs = [tab('a'), tab('b')];
    model.reconcile(tabs);
    const before = model.toJson();
    expect(() => applyTabClassification(model, tabs, tabs, [], groups)).toThrow();
    expect(model.toJson()).toBe(before);
  });

  it.each([true, false])('does not recreate a deleted folder (with ID: %s)', withId => {
    const model = new FolderModel();
    const tabs = [tab('a')];
    model.reconcile(tabs);
    const folder = model.addFolder('Work');
    const folders = [{id: folder.id, name: folder.name}];
    model.unfolder(folder.id);
    const before = model.toJson();
    expect(() => applyTabClassification(model, tabs, tabs, folders, [
      {folderId: withId ? folder.id : '', name: 'Work', tabIds: ['a']},
    ])).toThrow('changed');
    expect(model.toJson()).toBe(before);
  });

  it('creates no empty folder when every requested tab has gone', () => {
    const model = new FolderModel();
    expect(applyTabClassification(model, [], [tab('gone')], [], [
      {folderId: '', name: 'Reading', tabIds: ['gone']},
    ])).toBe(0);
    expect(model.getFolders()).toEqual([]);
  });
});
