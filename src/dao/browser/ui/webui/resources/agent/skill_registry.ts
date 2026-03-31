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

let cachedRegistry: SkillRegistryEntry[] = [];

export async function initSkillRegistry(): Promise<void> {
  try {
    const result = await callNative('getSkillRegistry') as SkillRegistryEntry[];
    cachedRegistry = result || [];
  } catch (_) {
    cachedRegistry = [];
  }
}

export async function refreshSkillRegistry(): Promise<void> {
  await initSkillRegistry();
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
