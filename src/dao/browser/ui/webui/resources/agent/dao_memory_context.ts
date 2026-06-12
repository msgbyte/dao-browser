// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {PiAttachment} from './dao_page_capture.js';

export interface NativePreference {
  key: string;
  value: string;
  confidence?: number;
}

export interface NativeEpisode {
  title?: string;
  intent?: string;
  outcome?: string;
  confidence?: number;
}

export interface NativeRecentMessage {
  role: string;
  content: string;
}

export interface NativeRelevantSummary {
  summary: string;
  primaryDomain: string;
}

export interface NativeMemoryContext {
  preferences?: NativePreference[];
  episodes?: NativeEpisode[];
  recentMessages?: NativeRecentMessage[];
  relevantSummary?: NativeRelevantSummary | null;
}

interface MemoryContextBuildOptions {
  url: string;
  domain: string;
  payload: NativeMemoryContext;
  charBudget?: number;
}

type RenderedSection = {text: string; truncated: boolean};

type RenderEntryKind =
    'preference'|'episode'|'recent-message'|'summary';

interface RenderEntry {
  kind: RenderEntryKind;
  value: string;
  key?: string;
  title?: string;
  role?: string;
  confidence?: number;
  domain?: string;
  defaultBudget: number;
}

const DEFAULT_MEMORY_CONTEXT_CHAR_BUDGET = 7000;
const DEFAULT_PREFERENCE_TEXT_BUDGET = 600;
const DEFAULT_EPISODE_TEXT_BUDGET = 800;
const DEFAULT_MESSAGE_TEXT_BUDGET = 240;
const DEFAULT_SUMMARY_TEXT_BUDGET = 1000;
const TRUNCATION_MARKER = '<truncated>true</truncated>';

function isRecord(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

function isNonEmptyString(value: unknown): value is string {
  return typeof value === 'string' && value.trim().length > 0;
}

function isPreference(value: unknown): value is NativePreference {
  return isRecord(value) &&
      isNonEmptyString(value['key']) &&
      isNonEmptyString(value['value']) &&
      (value['confidence'] === undefined ||
       typeof value['confidence'] === 'number');
}

function isEpisode(value: unknown): value is NativeEpisode {
  return isRecord(value) &&
      (isNonEmptyString(value['title']) ||
       isNonEmptyString(value['intent']) ||
       isNonEmptyString(value['outcome'])) &&
      (value['confidence'] === undefined ||
       typeof value['confidence'] === 'number');
}

function isRecentMessage(value: unknown): value is NativeRecentMessage {
  return isRecord(value) &&
      isNonEmptyString(value['role']) &&
      isNonEmptyString(value['content']);
}

function isRelevantSummary(value: unknown): value is NativeRelevantSummary {
  return isRecord(value) &&
      isNonEmptyString(value['summary']) &&
      isNonEmptyString(value['primaryDomain']);
}

function utf8ToBase64(s: string): string {
  const bytes = new TextEncoder().encode(s);
  let binary = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode.apply(
        null, Array.from(bytes.subarray(i, i + chunk)) as number[]);
  }
  return btoa(binary);
}

function escapeXmlText(value: string): string {
  return value
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
}

function escapeXmlAttribute(value: string): string {
  return escapeXmlText(value);
}

function trimToBudget(value: string, budget: number): RenderedSection {
  if (budget <= 0) {
    return {text: '', truncated: value.length > 0};
  }
  if (value.length === 0) {
    return {text: '', truncated: false};
  }

  const suffix = '...';
  if (budget < suffix.length) {
    return {text: '', truncated: true};
  }

  const contentBudget = budget - suffix.length;
  if (contentBudget === 0) {
    return {text: suffix, truncated: true};
  }

  let full = true;
  let output = '';
  for (const unit of Array.from(value)) {
    const escaped = escapeXmlText(unit);
    if ((output.length + escaped.length) > contentBudget) {
      full = false;
      break;
    }
    output += escaped;
  }

  if (full) {
    return {text: output, truncated: false};
  }

  return {
    text: output + suffix,
    truncated: true,
  };
}

function entryDefaultBudget(kind: RenderEntryKind): number {
  switch (kind) {
    case 'preference':
      return DEFAULT_PREFERENCE_TEXT_BUDGET;
    case 'episode':
      return DEFAULT_EPISODE_TEXT_BUDGET;
    case 'recent-message':
      return DEFAULT_MESSAGE_TEXT_BUDGET;
    case 'summary':
      return DEFAULT_SUMMARY_TEXT_BUDGET;
  }
}

function makeRenderableEntries(payload: NativeMemoryContext): RenderEntry[] {
  const entries: RenderEntry[] = [];

  if (Array.isArray(payload.preferences)) {
    for (const preference of payload.preferences) {
      if (!isPreference(preference)) continue;
      entries.push({
        kind: 'preference',
        value: preference.value,
        key: preference.key,
        confidence: preference.confidence,
        defaultBudget: DEFAULT_PREFERENCE_TEXT_BUDGET,
      });
    }
  }

  if (payload.relevantSummary && isRelevantSummary(payload.relevantSummary)) {
    entries.push({
      kind: 'summary',
      value: payload.relevantSummary.summary,
      domain: payload.relevantSummary.primaryDomain,
      defaultBudget: DEFAULT_SUMMARY_TEXT_BUDGET,
    });
  }

  if (Array.isArray(payload.episodes)) {
    for (const episode of payload.episodes) {
      if (!isEpisode(episode)) continue;
      const lines = [
        episode.intent ? `intent: ${episode.intent}` : '',
        episode.outcome ? `outcome: ${episode.outcome}` : '',
      ];
      entries.push({
        kind: 'episode',
        value: lines.filter(Boolean).join('\n'),
        title: episode.title,
        confidence: episode.confidence,
        defaultBudget: DEFAULT_EPISODE_TEXT_BUDGET,
      });
    }
  }

  if (Array.isArray(payload.recentMessages)) {
    for (const msg of payload.recentMessages) {
      if (!isRecentMessage(msg)) continue;
      entries.push({
        kind: 'recent-message',
        value: msg.content,
        role: msg.role,
        defaultBudget: DEFAULT_MESSAGE_TEXT_BUDGET,
      });
    }
  }

  return entries;
}

function buildEntryText(entry: RenderEntry, textBudget: number): RenderedSection {
  const trimmed = trimToBudget(String(entry.value || ''), textBudget);
  switch (entry.kind) {
    case 'preference': {
      const attr = [`key="${escapeXmlAttribute(entry.key || '')}"`];
      if (typeof entry.confidence === 'number') {
        attr.push(`confidence="${entry.confidence}"`);
      }
      return {
        text: `<preference ${attr.join(' ')}>${trimmed.text}</preference>`,
        truncated: trimmed.truncated,
      };
    }
    case 'episode': {
      const attr = [] as string[];
      if (typeof entry.confidence === 'number') {
        attr.push(`confidence="${entry.confidence}"`);
      }
      if (isNonEmptyString(entry.title)) {
        attr.push(`title="${escapeXmlAttribute(entry.title)}"`);
      }
      const attrs = attr.length ? ` ${attr.join(' ')}` : '';
      return {
        text: `<episode${attrs}>${trimmed.text}</episode>`,
        truncated: trimmed.truncated,
      };
    }
    case 'recent-message': {
      const role = isNonEmptyString(entry.role) ? ` role="${escapeXmlAttribute(entry.role)}"` : '';
      return {
        text: `<recent-message${role}>${trimmed.text}</recent-message>`,
        truncated: trimmed.truncated,
      };
    }
    case 'summary': {
      const domain = isNonEmptyString(entry.domain) ?
          ` domain="${escapeXmlAttribute(entry.domain)}"` : '';
      return {
        text: `<summary${domain}>${trimmed.text}</summary>`,
        truncated: trimmed.truncated,
      };
    }
  }
}

function normalizePositiveNumber(input: unknown): number {
  const value = Number(input);
  return Number.isFinite(value) && value > 0 ?
      value :
      DEFAULT_MEMORY_CONTEXT_CHAR_BUDGET;
}

function allocateTextBudgets(entries: RenderEntry[], available: number): number[] {
  if (entries.length === 0) return [];

  const defaults = entries.map(entry => entry.defaultBudget);

  if (available <= 0) return defaults.map(() => 0);

  const defaultTotal = defaults.reduce((sum, value) => sum + value, 0);
  if (defaultTotal <= available) return defaults;

  const minBudget = Math.max(8, Math.floor(available / Math.max(1, entries.length * 4)));
  if (available < minBudget * entries.length) {
    const floorBudget = Math.floor(available / entries.length);
    return entries.map(() => floorBudget);
  }

  let budgets = defaults.map(value =>
    Math.max(minBudget, Math.floor(value * available / defaultTotal)));
  let remaining = available - budgets.reduce((sum, value) => sum + value, 0);

  while (remaining > 0) {
    let changed = false;
    for (let i = 0; remaining > 0 && i < budgets.length; i++) {
      const budget = budgets[i]!;
      const defaultBudget = defaults[i]!;
      if (budget < defaultBudget) {
        budgets[i] = budget + 1;
        remaining--;
        changed = true;
      }
    }
    if (!changed) {
      break;
    }
  }

  while (remaining < 0) {
    let changed = false;
    for (let i = 0; remaining < 0 && i < budgets.length; i++) {
      const budget = budgets[i]!;
      if (budget > minBudget) {
        budgets[i] = budget - 1;
        remaining++;
        changed = true;
      }
    }
    if (!changed) {
      break;
    }
  }

  return budgets;
}

export function hasMemoryContextPayload(payload: unknown):
    payload is NativeMemoryContext {
  if (!isRecord(payload)) return false;

  const hasPreferences = Array.isArray(payload['preferences']) &&
      payload['preferences'].some(isPreference);
  const hasEpisodes = Array.isArray(payload['episodes']) &&
      payload['episodes'].some(isEpisode);
  const hasRecentMessages = Array.isArray(payload['recentMessages']) &&
      payload['recentMessages'].some(isRecentMessage);
  const hasSummary =
      payload['relevantSummary'] !== null &&
      isRelevantSummary(payload['relevantSummary']);

  return hasPreferences || hasEpisodes || hasRecentMessages || hasSummary;
}

export function buildMemoryContextText(options: MemoryContextBuildOptions): string {
  const charBudget = normalizePositiveNumber(options.charBudget);
  const budget = Math.max(160, charBudget);
  const domain = isNonEmptyString(options.domain) ? options.domain : 'unknown';
  const url = isNonEmptyString(options.url) ? options.url : 'about:blank';

  const entries = makeRenderableEntries(options.payload);
  if (entries.length === 0) {
    return '';
  }

  const open =
      `<memory-context source="dao-agent-memory" domain="${escapeXmlAttribute(
          domain)}" url="${escapeXmlAttribute(url)}">`;
  const close = '</memory-context>';
  const overhead = open.length + close.length + 2;

  if (budget <= overhead) {
    return `${open}${TRUNCATION_MARKER}${close}`;
  }

  let activeEntries = entries.slice();
  while (activeEntries.length > 0) {
    const marker = TRUNCATION_MARKER;
    const bodyBudget = Math.max(0, budget - overhead - marker.length - 2);
    if (bodyBudget <= 0) {
      activeEntries = activeEntries.slice(0, activeEntries.length - 1);
      if (activeEntries.length === 0) {
        return `${open}\n${TRUNCATION_MARKER}\n${close}`;
      }
      continue;
    }

    const budgets = allocateTextBudgets(activeEntries, bodyBudget);
    const rendered = activeEntries.map((entry, index) =>
        buildEntryText(entry, budgets[index] ?? 0));
    const hasTruncation = rendered.some(
        (entry, index) =>
          entry.truncated ||
          (budgets[index] ?? 0) <
              entryDefaultBudget(activeEntries[index]!.kind));
    const body = rendered.map(entry => entry.text).join('\n');
    const text = `${open}\n${body}\n${hasTruncation ? marker + '\n' : ''}${close}`;
    if (text.length <= budget) {
      return text;
    }

    activeEntries = activeEntries.slice(0, -1);
  }

  return `${open}\n${TRUNCATION_MARKER}\n${close}`;
}

export function buildMemoryContextAttachment(
    options: MemoryContextBuildOptions): PiAttachment | null {
  if (!hasMemoryContextPayload(options.payload)) {
    return null;
  }

  const extractedText = buildMemoryContextText(options);
  const id = `dao-memory-context-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  return {
    id,
    type: 'document',
    fileName: 'dao-memory-context.md',
    mimeType: 'text/markdown',
    size: new TextEncoder().encode(extractedText).length,
    content: utf8ToBase64(extractedText),
    extractedText,
    preview: '',
  };
}
