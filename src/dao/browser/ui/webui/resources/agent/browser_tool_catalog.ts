// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {ToolDefinition} from './agent_bridge.js';

export type BrowserToolClient = 'dao_agent'|'mcp';
export type BrowserToolGroup = 'devtools'|'page'|'tabs';
export type BrowserToolSideEffect = 'read'|'interaction'|'destructive';

type ToolInputSchema = ToolDefinition['function']['parameters'];

export interface BrowserToolCatalogEntry {
  name: string;
  description: string;
  inputSchema: ToolInputSchema;
  outputSchema?: unknown;
  group: BrowserToolGroup;
  clients: BrowserToolClient[];
  sideEffect: BrowserToolSideEffect;
  timeoutMs: number;
}

export interface BrowserToolCatalog {
  version: number;
  tools: BrowserToolCatalogEntry[];
}

let catalogPromise: Promise<BrowserToolCatalog>|undefined;
let catalog: BrowserToolCatalog|undefined;

function isRecord(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

async function readInjectedCatalog(): Promise<unknown> {
  let json: string;
  try {
    const {loadTimeData} =
        await import('//resources/js/load_time_data.js');
    json = loadTimeData.getString('browser_tool_catalog_json');
  } catch {
    throw new Error(
        'browser tool catalog load failed: injected data unavailable');
  }
  try {
    return JSON.parse(json);
  } catch {
    throw new Error(
        'browser tool catalog load failed: invalid injected JSON');
  }
}

function validateInputSchema(value: unknown, name: string): ToolInputSchema {
  if (!isRecord(value) || value['type'] !== 'object' ||
      !isRecord(value['properties']) || !Array.isArray(value['required']) ||
      !value['required'].every(required => typeof required === 'string')) {
    throw new Error(`invalid browser tool input schema: ${name}`);
  }
  for (const property of Object.values(value['properties'])) {
    if (!isRecord(property) || typeof property['type'] !== 'string') {
      throw new Error(`invalid browser tool input schema: ${name}`);
    }
  }
  return value as ToolInputSchema;
}

export function validateBrowserToolCatalog(value: unknown): BrowserToolCatalog {
  if (!isRecord(value) || value['version'] !== 1 ||
      !Array.isArray(value['tools'])) {
    throw new Error('invalid browser tool catalog');
  }

  const names = new Set<string>();
  const tools = value['tools'].map((raw): BrowserToolCatalogEntry => {
    if (!isRecord(raw) || typeof raw['name'] !== 'string' ||
        typeof raw['description'] !== 'string' ||
        typeof raw['group'] !== 'string' || !Array.isArray(raw['clients']) ||
        typeof raw['sideEffect'] !== 'string' ||
        typeof raw['timeoutMs'] !== 'number' ||
        !Number.isFinite(raw['timeoutMs']) || raw['timeoutMs'] <= 0) {
      throw new Error('invalid browser tool catalog entry');
    }
    if (names.has(raw['name'])) {
      throw new Error(`duplicate browser tool name: ${raw['name']}`);
    }
    names.add(raw['name']);

    if (raw['group'] !== 'devtools' && raw['group'] !== 'page' &&
        raw['group'] !== 'tabs') {
      throw new Error(`invalid browser tool group: ${raw['name']}`);
    }
    if (!raw['clients'].every(
            client => client === 'dao_agent' || client === 'mcp')) {
      throw new Error(`invalid browser tool clients: ${raw['name']}`);
    }
    if (raw['sideEffect'] !== 'read' && raw['sideEffect'] !== 'interaction' &&
        raw['sideEffect'] !== 'destructive') {
      throw new Error(`invalid browser tool side effect: ${raw['name']}`);
    }

    return {
      name: raw['name'],
      description: raw['description'],
      inputSchema: validateInputSchema(raw['inputSchema'], raw['name']),
      ...(raw['outputSchema'] === undefined ? {} :
         {outputSchema: raw['outputSchema']}),
      group: raw['group'],
      clients: raw['clients'] as BrowserToolClient[],
      sideEffect: raw['sideEffect'],
      timeoutMs: raw['timeoutMs'],
    };
  });

  return {version: value['version'], tools};
}

export function initializeBrowserToolCatalog(): Promise<void> {
  if (!catalogPromise) {
    const loadPromise = Promise.resolve()
        .then(readInjectedCatalog)
        .then(validateBrowserToolCatalog)
        .then(loaded => {
          catalog = loaded;
          return loaded;
        });
    catalogPromise = loadPromise;
    void loadPromise.catch(() => {
      if (catalogPromise === loadPromise) {
        catalogPromise = undefined;
      }
    });
  }
  return catalogPromise.then(() => undefined);
}

function getInitializedCatalog(): BrowserToolCatalog {
  if (!catalog) {
    throw new Error('browser tool catalog has not been initialized');
  }
  return catalog;
}

export function getCatalogEntries(client: BrowserToolClient):
    BrowserToolCatalogEntry[] {
  return getInitializedCatalog().tools.filter(
      entry => entry.clients.includes(client));
}

export function getBrowserToolDefinitions(client: BrowserToolClient):
    ToolDefinition[] {
  return getCatalogEntries(client).map(entry => ({
    type: 'function',
    function: {
      name: entry.name,
      description: entry.description,
      parameters: entry.inputSchema,
    },
  }));
}
