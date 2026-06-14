// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export interface DaoIndexPage {
  id: string;
  url: string;
  titleKey: string;
  descriptionKey: string;
}

export const daoIndexPages: DaoIndexPage[] = [
  {
    id: 'index',
    url: 'dao://index',
    titleKey: 'index.page.index.title',
    descriptionKey: 'index.page.index.desc',
  },
  {
    id: 'agent',
    url: 'dao://agent',
    titleKey: 'index.page.agent.title',
    descriptionKey: 'index.page.agent.desc',
  },
  {
    id: 'skills',
    url: 'dao://skills',
    titleKey: 'index.page.skills.title',
    descriptionKey: 'index.page.skills.desc',
  },
  {
    id: 'dream',
    url: 'dao://dream',
    titleKey: 'index.page.dream.title',
    descriptionKey: 'index.page.dream.desc',
  },
  {
    id: 'memory',
    url: 'dao://memory',
    titleKey: 'index.page.memory.title',
    descriptionKey: 'index.page.memory.desc',
  },
  {
    id: 'sidebar',
    url: 'dao://sidebar',
    titleKey: 'index.page.sidebar.title',
    descriptionKey: 'index.page.sidebar.desc',
  },
  {
    id: 'welcome',
    url: 'dao://welcome',
    titleKey: 'index.page.welcome.title',
    descriptionKey: 'index.page.welcome.desc',
  },
];
