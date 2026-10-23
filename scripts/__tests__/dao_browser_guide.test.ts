import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, it } from 'vitest';

// `dao-browser skill` prints a hand-written guide; the script runtime lives in
// dao_agent_script_run.cc. Keep the two describing the same API.
const root = path.resolve(__dirname, '..', '..');

function read(relative: string): string {
  return readFileSync(path.join(root, relative), 'utf8');
}

const runtime = read('src/dao/browser/agent_space/dao_agent_script_run.cc');
const guide = read('src/dao/browser/cli/dao_browser_guide.md');
const catalog = JSON.parse(read(
    'src/dao/browser/ui/webui/resources/agent/browser_tool_catalog.json')) as {
  tools: Array<{ name: string; clients: string[] }>;
};

function between(text: string, start: string, end: string): string {
  const from = text.indexOf(start);
  expect(from).toBeGreaterThanOrEqual(0);
  const to = text.indexOf(end, from + start.length);
  expect(to).toBeGreaterThan(from);
  return text.slice(from + start.length, to);
}

function runtimeMethods(body: string): string[] {
  return [...body.matchAll(/^ {4}(?:async )?([a-zA-Z]+)\(/gm)]
      .map(match => match[1]!)
      .filter(name => name !== 'constructor');
}

describe('dao-browser guide', () => {
  const spaceMethods =
      runtimeMethods(between(runtime, 'class Space {', '\n  }\n'));
  const daoMethods = [
    ...runtimeMethods(
        between(runtime, 'const dao = Object.freeze({', '\n  });')),
    'call',
  ];

  it('documents every Space method of the script runtime', () => {
    expect(spaceMethods.length).toBeGreaterThan(10);
    for (const method of spaceMethods) {
      expect(guide, `s.${method}`).toContain(`s.${method}(`);
    }
  });

  it('documents every dao.* function of the script runtime', () => {
    for (const method of daoMethods) {
      expect(guide, `dao.${method}`).toContain(`dao.${method}(`);
    }
  });

  it('only documents methods the runtime has', () => {
    for (const [, method] of guide.matchAll(/`s\.([a-zA-Z]+)\(/g)) {
      expect(spaceMethods, `s.${method}`).toContain(method);
    }
    for (const [, method] of guide.matchAll(/`(?:await )?dao\.([a-zA-Z]+)\(/g)) {
      expect(daoMethods, `dao.${method}`).toContain(method);
    }
  });

  it('names only tools that local agents can call', () => {
    const mcpTools = new Set(catalog.tools
        .filter(tool => tool.clients.includes('mcp'))
        .map(tool => tool.name));
    const table = between(guide, '| Method | Tool |', '\n\n');
    const rows = table.split('\n').filter(row => row.startsWith('| `'));
    expect(rows.length).toBeGreaterThan(10);
    for (const row of rows) {
      // The tool cell starts with the tools it wraps, then explains them.
      const lead = /^\s*((?:`[a-z_]+`(?:\s*\/\s*)?)+)/.exec(
          row.split('|')[2]!);
      for (const [, tool] of (lead?.[1] ?? '').matchAll(/`([a-z_]+)`/g)) {
        expect(mcpTools, tool).toContain(tool);
      }
    }
  });
});
