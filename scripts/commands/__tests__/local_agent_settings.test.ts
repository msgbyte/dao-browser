import {readFileSync} from 'node:fs';
import vm from 'node:vm';

import ts from 'typescript';
import {describe, expect, it, vi} from 'vitest';

const status = {
  agents: [
    {id: 'shared', detected: true, installed: true},
    {id: 'claude', detected: false, installed: false},
  ],
  cliLinked: true,
};

function createPage() {
  const source = readFileSync(
      'src/patches/chrome/browser/resources/settings/dao_page/dao_page.ts.patch',
      'utf8').split('\n')
      .filter(line => line.startsWith('+') && !line.startsWith('+++'))
      .map(line => line.slice(1)).join('\n')
      .replace(/^import[\s\S]*?;\n/gm, '')
      .replace(/^export /gm, '');
  const output = ts.transpileModule(source, {
    compilerOptions: {target: ts.ScriptTarget.ES2022},
  }).outputText;
  const Page = vm.runInNewContext(output + '; SettingsDaoPageElement', {
    PolymerElement: class {},
    WebUiListenerMixin: (base: unknown) => base,
    PrefsMixin: (base: unknown) => base,
    SiteSettingsBrowserProxyImpl: {getInstance: () => ({})},
    customElements: {define: () => {}},
    loadTimeData: {getString: (key: string) => key},
  });
  const page = new Page();
  page.daoLocalAgents_ = [];
  page.daoLocalAgentsBusy_ = false;
  page.daoLocalAgentsError_ = '';
  page.daoMcpBrowserProxy_ = {
    getDaoLocalAgents: vi.fn().mockResolvedValue(status),
    setDaoLocalAgentInstalled: vi.fn().mockResolvedValue({
      ...status, success: true,
      agents: [{...status.agents[0]}],
    }),
  };
  return page;
}

describe('manual local agent integration', () => {
  it('shows installed skills and leaves files unchanged while checking status', async () => {
    const page = createPage();
    await page.refreshDaoLocalAgents_();
    expect(page.daoLocalAgents_[0]).toMatchObject({
      id: 'shared', installed: true, canInstall: false,
      status: 'daoLocalAgentInstalled', action: 'daoLocalAgentInstall',
    });
    expect(page.daoLocalAgents_[1]).toMatchObject({
      id: 'claude', installed: false, canInstall: true,
      action: 'daoLocalAgentInstall',
    });
    expect(page.daoLocalAgents_[2]).toMatchObject({
      id: 'cli', installed: true, canInstall: false,
    });
    expect(page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled)
        .not.toHaveBeenCalled();
  });

  it.each([['shared', true], ['claude', true], ['shared', false], ['cli', false]])(
      'changes only the selected integration %s to %s', async (id, install) => {
        const page = createPage();
        await page.onDaoLocalAgentAction_({
          currentTarget: {dataset: {id, install: String(install)}},
        });
        expect(page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled)
            .toHaveBeenCalledExactlyOnceWith(id, install);
        expect(page.daoLocalAgentsBusy_).toBe(false);
      });

  it('applies the returned status and reports filesystem failure', async () => {
    const page = createPage();
    page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled.mockResolvedValueOnce({
      ...status, success: false,
    });
    await page.onDaoLocalAgentAction_({
      currentTarget: {dataset: {id: 'shared', install: 'true'}},
    });
    expect(page.daoLocalAgentsError_).toBe('daoLocalAgentsError');
    expect(page.daoLocalAgents_[0].installed).toBe(true);
  });

  it('reports read and mutation rejections and permits retry', async () => {
    const page = createPage();
    page.daoMcpBrowserProxy_.getDaoLocalAgents.mockRejectedValueOnce(new Error());
    await page.refreshDaoLocalAgents_();
    expect(page.daoLocalAgentsError_).toBe('daoLocalAgentsError');
    page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled.mockRejectedValueOnce(
        new Error());
    const event = {currentTarget: {dataset: {id: 'shared', install: 'true'}}};
    await page.onDaoLocalAgentAction_(event);
    expect(page.daoLocalAgentsError_).toBe('daoLocalAgentsError');
    expect(page.daoLocalAgentsBusy_).toBe(false);
    await page.onDaoLocalAgentAction_(event);
    expect(page.daoLocalAgentsError_).toBe('');
    expect(page.daoLocalAgents_[0].canInstall).toBe(false);
  });

  it('ignores stale reads and prevents repeated clicks during an operation', async () => {
    const page = createPage();
    let resolveRead!: (value: unknown) => void;
    let resolveWrite!: (value: unknown) => void;
    page.daoMcpBrowserProxy_.getDaoLocalAgents.mockReturnValueOnce(
        new Promise(resolve => resolveRead = resolve));
    const reading = page.refreshDaoLocalAgents_();
    page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled.mockReturnValueOnce(
        new Promise(resolve => resolveWrite = resolve));
    const event = {currentTarget: {dataset: {id: 'shared', install: 'true'}}};
    const writing = page.onDaoLocalAgentAction_(event);
    expect(page.daoLocalAgentsBusy_).toBe(true);
    await page.onDaoLocalAgentAction_(event);
    await page.refreshDaoLocalAgents_();
    expect(page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled).toHaveBeenCalledTimes(1);
    expect(page.daoMcpBrowserProxy_.getDaoLocalAgents).toHaveBeenCalledTimes(1);
    resolveWrite({...status, success: true, agents: []});
    await writing;
    resolveRead(status);
    await reading;
    expect(page.daoLocalAgents_.map((row: {id: string}) => row.id)).toEqual(['cli']);
  });
});
