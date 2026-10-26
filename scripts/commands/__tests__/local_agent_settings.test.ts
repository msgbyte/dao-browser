import {readFileSync} from 'node:fs';
import vm from 'node:vm';

import ts from 'typescript';
import {describe, expect, it, vi} from 'vitest';

const routes = {
  BASIC: {},
  DAO: {},
  DAO_LOCAL_AGENTS: {},
};

const status = {
  agents: [
    {id: 'shared', detected: true, installed: true, needsUpdate: true},
    {id: 'claude', detected: false, installed: false, needsUpdate: false},
  ],
  cliLinked: true,
  cliNeedsUpdate: false,
};

function createPage() {
  const source = readFileSync(
      'src/patches/chrome/browser/resources/settings/dao_page/dao_local_agent_page.ts.patch',
      'utf8').split('\n')
      .filter(line => line.startsWith('+') && !line.startsWith('+++'))
      .map(line => line.slice(1)).join('\n')
      .replace(/^import[\s\S]*?;\n/gm, '')
      .replace(/^export /gm, '');
  const output = ts.transpileModule(source, {
    compilerOptions: {target: ts.ScriptTarget.ES2022},
  }).outputText;
  const Page = vm.runInNewContext(output + '; SettingsDaoLocalAgentPageElement', {
    PolymerElement: class {},
    WebUiListenerMixin: (base: unknown) => base,
    RouteObserverMixin: (base: new () => object) => class extends base {
      currentRouteChanged() {}
    },
    routes,
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
      agents: [{...status.agents[0], needsUpdate: false}],
    }),
  };
  return page;
}

function createSettingsController(
    component: string, methods: string[], globals: Record<string, unknown> = {}) {
  const source = readFileSync(
      `src/patches/chrome/browser/resources/settings/${component}/${component}.ts.patch`,
      'utf8').split('\n')
      .filter(line => /^[ +]/.test(line) && !line.startsWith('+++'))
      .map(line => line.slice(1)).join('\n');
  const members = methods.map(name => {
    const start = [...source.matchAll(
      new RegExp(`^  (?:private )?(?:async )?${name}\\(`, 'gm'),
    )].at(-1)?.index ?? -1;
    expect(start).toBeGreaterThanOrEqual(0);
    const parsed = ts.createSourceFile(
        'controller.ts', `class Controller {\n${source.slice(start)}\n}`,
        ts.ScriptTarget.Latest, true);
    return (parsed.statements[0] as ts.ClassDeclaration).members[0].getText(parsed);
  });
  const output = ts.transpileModule(`class Controller {${members.join('\n')}}`, {
    compilerOptions: {target: ts.ScriptTarget.ES2022},
  }).outputText;
  const Controller = vm.runInNewContext(output + '; Controller', {
    CustomEvent, CSS: {escape: (value: string) => value}, routes, ...globals,
  });
  return new Controller();
}

describe('manual local agent integration', () => {
  it.each(['sidebar', 'scroll'])(
      'refreshes externally removed skills on %s return without changing route',
      async entry => {
        const page = createPage();
        await page.refreshLocalAgents();
        const sections = ['dao', 'localAgents'].map((id, index) => ({
          id, scrollIntoView: vi.fn(),
          getBoundingClientRect: () => ({top: index * 100}),
        }));
        const container = {
          scrollTop: 0, clientHeight: 400, scrollHeight: 1000,
          getBoundingClientRect: () => ({top: 0}),
        };
        const main = createSettingsController('settings_main', [
          'scrollToOverviewSection', 'updateActiveOverviewSectionFromScroll_',
          'updateActiveOverviewSection_',
        ]);
        Object.assign(main, {
          overviewMode_: true, activeOverviewSection_: 'localAgents',
          $: {switcher: {querySelector: (selector: string) =>
            selector === 'settings-dao-local-agent-page' ? page :
                sections.find(section => selector === `#${section.id}`)}},
          getOverviewSections_: () => sections,
          getScrollContainer_: () => container,
          dispatchEvent: vi.fn(),
        });
        const router = {
          getCurrentRoute: () => routes.BASIC,
          getQueryParameters: () => new URLSearchParams(),
          navigateTo: vi.fn(),
        };
        const ui = createSettingsController(
            'settings_ui', ['onSettingsSectionActivate_'],
            {Router: {getInstance: () => router}});
        Object.assign(ui, {
          $: {main, leftMenu: {setActiveSection: vi.fn()}},
          isOverviewRoute_: () => true, replaceOverviewHash_: vi.fn(),
        });
        const activate = async (section: string) => {
          if (entry === 'sidebar') {
            await ui.onSettingsSectionActivate_(
                new CustomEvent('settings-section-activate', {
                  detail: {section, behavior: 'auto'},
                }));
          } else {
            container.getBoundingClientRect = () =>
              ({top: section === 'localAgents' ? 100 : 0});
            main.updateActiveOverviewSectionFromScroll_();
          }
        };
        await activate('dao');
        page.daoMcpBrowserProxy_.getDaoLocalAgents.mockResolvedValue({
          ...status,
          agents: [{...status.agents[0], installed: false, needsUpdate: false}],
        });

        await activate('localAgents');

        await vi.waitFor(() => expect(page.daoLocalAgents_[0]).toMatchObject({
          installed: false, action: 'daoLocalAgentInstall',
          status: 'daoLocalAgentNotInstalled',
        }));
        await activate('localAgents');
        expect(page.daoMcpBrowserProxy_.getDaoLocalAgents).toHaveBeenCalledTimes(2);
        expect(router.navigateTo).not.toHaveBeenCalled();
        expect(page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled)
            .not.toHaveBeenCalled();
      });

  it.each(['DAO_LOCAL_AGENTS', 'BASIC'] as const)(
      'refreshes externally removed skills when returning to %s', async route => {
        const page = createPage();
        await page.refreshLocalAgents();
        page.currentRouteChanged?.(routes.DAO, routes[route]);
        page.daoMcpBrowserProxy_.getDaoLocalAgents.mockResolvedValueOnce({
          ...status,
          agents: [{...status.agents[0], installed: false, needsUpdate: false}],
        });

        await page.currentRouteChanged?.(routes[route], routes.DAO);

        await vi.waitFor(() => expect(page.daoLocalAgents_[0]).toMatchObject({
          id: 'shared', installed: false, action: 'daoLocalAgentInstall',
          status: 'daoLocalAgentNotInstalled',
        }));
        expect(page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled)
            .not.toHaveBeenCalled();
      });

  it('does not refresh on unrelated or unchanged routes', async () => {
    const page = createPage();
    await page.currentRouteChanged?.(routes.DAO, routes.DAO_LOCAL_AGENTS);
    await page.currentRouteChanged?.(
        routes.DAO_LOCAL_AGENTS, routes.DAO_LOCAL_AGENTS);
    expect(page.daoMcpBrowserProxy_.getDaoLocalAgents).not.toHaveBeenCalled();
  });

  it('shows stale skills and leaves files unchanged while checking status', async () => {
    const page = createPage();
    await page.refreshLocalAgents();
    expect(page.daoLocalAgents_[0]).toMatchObject({
      id: 'shared', installed: true, canInstall: true,
      status: 'daoLocalAgentNeedsUpdate', action: 'daoLocalAgentUpdate',
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
    expect(page.daoLocalAgents_[0].action).toBe('daoLocalAgentUpdate');
  });

  it('reports read and mutation rejections and permits retry', async () => {
    const page = createPage();
    page.daoMcpBrowserProxy_.getDaoLocalAgents.mockRejectedValueOnce(new Error());
    await page.refreshLocalAgents();
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
    const reading = page.refreshLocalAgents();
    page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled.mockReturnValueOnce(
        new Promise(resolve => resolveWrite = resolve));
    const event = {currentTarget: {dataset: {id: 'shared', install: 'true'}}};
    const writing = page.onDaoLocalAgentAction_(event);
    expect(page.daoLocalAgentsBusy_).toBe(true);
    await page.onDaoLocalAgentAction_(event);
    await page.refreshLocalAgents();
    expect(page.daoMcpBrowserProxy_.setDaoLocalAgentInstalled).toHaveBeenCalledTimes(1);
    expect(page.daoMcpBrowserProxy_.getDaoLocalAgents).toHaveBeenCalledTimes(1);
    resolveWrite({...status, success: true, agents: []});
    await writing;
    resolveRead(status);
    await reading;
    expect(page.daoLocalAgents_.map((row: {id: string}) => row.id)).toEqual(['cli']);
  });
});
