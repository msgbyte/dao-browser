// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {callNative, callNativeArgs} from './agent_bridge.js';

// ---- Interfaces ----

export interface SkillRegistryEntry {
  id: string;
  name: string;
  description: string;
  source: string;
  hosts: string[];
  requiresPageContent: boolean;
}

export interface SkillContent {
  metadata: SkillRegistryEntry;
  instructions: string;
}

// ---- Cached Registry ----
//
// The registry is module-local and therefore per-WebUI: dao://agent and
// dao://skills each load their own copy of this file with its own
// `cachedRegistry`. That means `saveUserSkill` writing to disk from one
// page does NOT propagate to the other page's cache. Callers in
// dao://agent must call `refreshSkillRegistryIfStale` (or
// `refreshSkillRegistry`) on UI signals where the registry might have
// changed in another tab — most importantly when the slash picker is
// opened. Without that, a skill created in dao://skills won't be
// invokable as `/<id>` until the sidebar reloads.

let cachedRegistry: SkillRegistryEntry[] = [];
let lastRefreshAt_ = 0;
let inflightRefresh_: Promise<void>|null = null;

export function initSkillRegistry(): Promise<void> {
  if (inflightRefresh_) return inflightRefresh_;
  inflightRefresh_ = (async () => {
    try {
      const result =
          await callNative('getSkillRegistry') as SkillRegistryEntry[];
      cachedRegistry = result || [];
    } catch (_) {
      cachedRegistry = [];
    } finally {
      lastRefreshAt_ = Date.now();
      inflightRefresh_ = null;
    }
  })();
  return inflightRefresh_;
}

export function refreshSkillRegistry(): Promise<void> {
  return initSkillRegistry();
}

// On-demand cross-WebUI sync. Refreshes the cache from disk only when the
// last fetch was older than `maxAgeMs`, or when another refresh is
// already in flight (in which case it just awaits it). Returns true when
// a real refresh actually ran (so the caller knows the cache may have
// changed and any dependent UI should re-render); false when throttled.
export async function refreshSkillRegistryIfStale(
    maxAgeMs = 1500): Promise<boolean> {
  if (inflightRefresh_) {
    await inflightRefresh_;
    return true;
  }
  if (lastRefreshAt_ !== 0 && Date.now() - lastRefreshAt_ < maxAgeMs) {
    return false;
  }
  await initSkillRegistry();
  return true;
}

export function getAllSkills(): SkillRegistryEntry[] {
  return cachedRegistry;
}

export function getAvailableSkills(currentHost: string): SkillRegistryEntry[] {
  return cachedRegistry.filter(
      skill => isSkillAvailableForHost(skill, currentHost));
}

function isSkillAvailableForHost(
    skill: SkillRegistryEntry, host: string): boolean {
  if (!skill.hosts || skill.hosts.length === 0 ||
      skill.hosts.includes('*')) {
    return true;
  }
  return skill.hosts.some(
      h => host === h || host.endsWith('.' + h));
}

export async function loadSkillInstructions(
    skillId: string): Promise<SkillContent|null> {
  try {
    const result =
        await callNativeArgs('getSkillContent', skillId) as SkillContent|null;
    return result || null;
  } catch (_) {
    return null;
  }
}

export async function saveUserSkill(
    skillId: string, skillMdContent: string,
    host: string): Promise<boolean> {
  try {
    const result = await callNativeArgs(
        'saveUserSkill', skillId, skillMdContent, host) as boolean;
    // Refresh the registry so the new skill shows up immediately.
    await refreshSkillRegistry();
    return result;
  } catch (_) {
    return false;
  }
}

export async function deleteUserSkill(skillId: string): Promise<boolean> {
  try {
    const result =
        await callNativeArgs('deleteUserSkill', skillId) as boolean;
    await refreshSkillRegistry();
    return result;
  } catch (_) {
    return false;
  }
}
