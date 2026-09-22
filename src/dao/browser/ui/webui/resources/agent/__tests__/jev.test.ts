import {afterEach, describe, expect, it} from 'vitest';
import {getAgentPlugins, isPluginEnabled, isPluginTool, syncAgentPlugins} from '../agent_plugins.js';

const plugin = {id: 'jev', name: 'Jev', tool: 'run_browser_task', revision: 'revision-1',
  enabledKey: 'enabled', urlKey: 'url', tokenKey: 'token', permissionKey: 'permission'};
const settings = {enabled: 'true', url: 'https://service.test/full/path',
  token: 'private-token', permission: 'true'};

afterEach(() => syncAgentPlugins([], {}));

describe('Jev Agent tool availability', () => {
  it('requires an enabled connection and independent tool permission', () => {
    syncAgentPlugins([plugin], settings);
    expect(isPluginEnabled('run_browser_task')).toBe(true);
    expect(getAgentPlugins()[0]?.revision).toBe('revision-1');
    for (const patch of [{enabled: 'false'}, {permission: 'false'}, {token: ' '},
      {token: 'line\nbreak'}, {token: 'carriage\rreturn'},
      {url: ''}, {url: 'file:///tmp/jev'}, {url: 'https://user:password@service.test'},
      {url: 'https://service.test/#fragment'}, {url: 'https://service.test/path#'}]) {
      syncAgentPlugins([plugin], {...settings, ...patch});
      expect(isPluginEnabled('run_browser_task')).toBe(false);
    }
  });

  it('keeps stale tool definitions fail-closed after removal or revocation', () => {
    syncAgentPlugins([plugin], settings);
    expect(isPluginEnabled('run_browser_task')).toBe(true);
    syncAgentPlugins([plugin], {...settings, permission: 'false'});
    expect(isPluginEnabled('run_browser_task')).toBe(false);
    syncAgentPlugins([], {});
    expect(isPluginTool('run_browser_task')).toBe(true);
    expect(isPluginEnabled('run_browser_task')).toBe(false);
    expect(isPluginTool('click_by_ref')).toBe(false);
  });
});
// Task execution is shared native code. scripts/checks/jev-plugin.mjs exercises
// its decision boundary, cancellation and guarded actions through both clients.
