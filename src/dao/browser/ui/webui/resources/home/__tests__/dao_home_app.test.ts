// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {readFileSync} from 'node:fs';

import {beforeEach, describe, expect, it, vi} from 'vitest';

const bridge = vi.hoisted(() => ({
  getHomeSnapshot: vi.fn(),
  getHomeVersions: vi.fn(),
  getHomeFiles: vi.fn(),
  readHomeFile: vi.fn(),
  getHomePermission: vi.fn(),
  approveHomePermission: vi.fn(),
  cancelHomePermission: vi.fn(),
  resolveHomeBootstrapPermission: vi.fn(),
  addListener: vi.fn(),
  removeListener: vi.fn(),
  openHomeAgent: vi.fn(),
  rollbackHome: vi.fn(),
  resetHome: vi.fn(),
  exportHome: vi.fn(),
  importHome: vi.fn(),
  cancelHomeSession: vi.fn(),
  openHomeNavigation: vi.fn(),
  setHomeSelection: vi.fn(),
  startHomeConnector: vi.fn(),
  startHomeDraftConnector: vi.fn(),
  callHomeConnectorPage: vi.fn(),
  finishHomeConnector: vi.fn(),
  resolveHomeMedia: vi.fn(),
  completeHomeAgentConnector: vi.fn(),
  notifyHomeAgentPreviewLoaded: vi.fn(),
  recordHomeRuntimeError: vi.fn(),
}));

vi.mock('//resources/lit/v3_0/lit.rollup.js', async () => {
  return await import('../../sidebar/__tests__/lit_test_shim.js');
});

vi.mock('//resources/js/load_time_data.js', () => ({
  loadTimeData: {
    getString: (key: string) => key,
    getStringF: (key: string, ...values: string[]) =>
        `${key}:${values.join(':')}`,
  },
}));

vi.mock('../home_bridge.js', () => bridge);

type HomeApp = HTMLElement&{
  loading_: boolean;
  snapshot_: {hasProject: boolean; revision: string; entry: string}|null;
  pendingImport_: string;
  readSource_: (path: string) => Promise<void>;
  updateComplete: Promise<boolean>;
};

async function createApp(): Promise<HomeApp> {
  await import('../dao_home_app.js');
  const appClass = customElements.get('dao-home-app') as typeof HTMLElement&{
    invokeLifecycleCallbacksForTesting?: boolean;
  };
  appClass.invokeLifecycleCallbacksForTesting = true;
  const app = document.createElement('dao-home-app') as HomeApp;
  document.body.appendChild(app);
  await vi.waitFor(() => expect(app.loading_).toBe(false));
  await vi.waitFor(() => {
    expect(app.shadowRoot!.querySelector('.loading')).toBeNull();
  });
  return app;
}

describe('dao-home-app', () => {
  beforeEach(() => {
    document.body.innerHTML = '';
    Object.defineProperty(document, 'hidden', {
      configurable: true,
      value: false,
    });
    vi.clearAllMocks();
    bridge.getHomeVersions.mockResolvedValue([]);
    bridge.getHomeFiles.mockResolvedValue([]);
    bridge.readHomeFile.mockResolvedValue('');
    bridge.getHomePermission.mockResolvedValue(null);
    bridge.approveHomePermission.mockResolvedValue({});
    bridge.cancelHomePermission.mockResolvedValue(true);
    bridge.resolveHomeBootstrapPermission.mockResolvedValue({});
    bridge.resetHome.mockResolvedValue({
      hasProject: false,
      revision: '',
      entry: '',
      connectors: [],
    });
    bridge.addListener.mockImplementation((eventName, listener) => ({
      eventName,
      uid: 1,
      listener,
    }));
    bridge.getHomeSnapshot.mockResolvedValue({
      hasProject: false,
      revision: '',
      entry: '',
      connectors: [],
    });
  });

  it('exposes exactly the two approved primary empty-state actions',
     async () => {
       const app = await createApp();
       const actions = [...app.shadowRoot!.querySelectorAll<HTMLElement>(
           '[data-empty-action]')];

       expect(actions.map(action => action.dataset.emptyAction)).toEqual([
         'create', 'history',
       ]);

       actions[0]!.click();
       actions[1]!.click();
       expect(bridge.openHomeAgent).toHaveBeenNthCalledWith(1, 'create');
       expect(bridge.openHomeAgent).toHaveBeenNthCalledWith(2, 'history');
     });

  it('frames a published project only at the untrusted app origin', async () => {
    bridge.getHomeSnapshot.mockResolvedValue({
      hasProject: true,
      revision: 'revision-1',
      entry: 'index.html',
      connectors: [],
    });
    const app = await createApp();
    const frame = app.shadowRoot!.querySelector<HTMLIFrameElement>(
        '[data-test="project-frame"]');

    expect(frame).not.toBeNull();
    expect(frame!.src).toBe(
        'chrome-untrusted://dao-home-app/revision-1/index.html?route=%2F');
    expect(frame!.getAttribute('sandbox'))
        .toBe('allow-scripts allow-forms');
  });

  it('lets the published project fill the browser-owned content card',
     async () => {
       await import('../dao_home_app.js');
       const ctor = customElements.get('dao-home-app') as
           typeof HTMLElement&{styles: {strings: string[]}};
       const cssText = ctor.styles.strings.join('');

       expect(cssText).toMatch(/\.canvas\s*{[^}]*inset:\s*0;/s);
       expect(cssText).not.toMatch(/\.canvas\s*{[^}]*border(?:-radius)?:/s);
     });

  it('requires trusted confirmation before opening generated navigation',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       const app = await createApp();
       const frame = app.shadowRoot!.querySelector<HTMLIFrameElement>(
           '[data-test="project-frame"]')!;

       window.dispatchEvent(new MessageEvent('message', {
         source: frame.contentWindow,
         origin: 'null',
         data: {
           daoHome: 1,
           requestId: 'navigation-1',
           revision: 'revision-1',
           method: 'navigation.open',
           params: {url: 'https://example.test/private?token=secret'},
         },
       }));

       await vi.waitFor(() => {
         expect(app.shadowRoot!.querySelector(
             '[data-test="navigation-dialog"]')).not.toBeNull();
       });
       expect(bridge.openHomeNavigation).not.toHaveBeenCalled();

       app.shadowRoot!.querySelector<HTMLButtonElement>(
           '[data-test="confirm-navigation"]')!.click();

       expect(bridge.openHomeNavigation).toHaveBeenCalledOnce();
       expect(bridge.openHomeNavigation).toHaveBeenCalledWith(
           'https://example.test/private?token=secret');
     });

  it('opens a validated launch action without a confirmation dialog',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       const app = await createApp();
       const frame = app.shadowRoot!.querySelector<HTMLIFrameElement>(
           '[data-test="project-frame"]')!;

       window.dispatchEvent(new MessageEvent('message', {
         source: frame.contentWindow,
         origin: 'null',
         data: {
           daoHome: 1,
           requestId: 'action-1',
           revision: 'revision-1',
           method: 'navigation.openAction',
           params: {
             actionId: 'github',
             url: 'https://github.com/',
           },
         },
       }));

       await vi.waitFor(() => {
         expect(bridge.openHomeNavigation).toHaveBeenCalledWith(
             'https://github.com/');
       });
       expect(app.shadowRoot!.querySelector(
           '[data-test="navigation-dialog"]')).toBeNull();
     });

  it('opens a collected feed item without a confirmation dialog', async () => {
    bridge.getHomeSnapshot.mockResolvedValue({
      hasProject: true,
      revision: 'revision-1',
      entry: 'index.html',
      connectors: [],
    });
    const app = await createApp();
    const frame = app.shadowRoot!.querySelector<HTMLIFrameElement>(
        '[data-test="project-frame"]')!;

    window.dispatchEvent(new MessageEvent('message', {
      source: frame.contentWindow,
      origin: 'null',
      data: {
        daoHome: 1,
        requestId: 'feed-1',
        revision: 'revision-1',
        method: 'navigation.openFeedItem',
        params: {
          sourceId: 'github',
          url: 'https://github.com/example/project',
        },
      },
    }));

    await vi.waitFor(() => {
      expect(bridge.openHomeNavigation).toHaveBeenCalledWith(
          'https://github.com/example/project');
    });
    expect(app.shadowRoot!.querySelector(
        '[data-test="navigation-dialog"]')).toBeNull();
  });

  it('cancels pending generated navigation when the project changes',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       const app = await createApp();
       const frame = app.shadowRoot!.querySelector<HTMLIFrameElement>(
           '[data-test="project-frame"]')!;

       window.dispatchEvent(new MessageEvent('message', {
         source: frame.contentWindow,
         origin: 'null',
         data: {
           daoHome: 1,
           requestId: 'navigation-stale',
           revision: 'revision-1',
           method: 'navigation.open',
           params: {url: 'https://example.test/stale'},
         },
       }));
       await vi.waitFor(() => {
         expect(app.shadowRoot!.querySelector(
             '[data-test="navigation-dialog"]')).not.toBeNull();
       });

       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-2',
         entry: 'index.html',
         connectors: [],
       });
       const registration = bridge.addListener.mock.calls.find(
           ([eventName]) => eventName === 'dao-home-project-changed')![1];
       registration();

       await vi.waitFor(() => {
         expect(app.shadowRoot!.querySelector(
             '[data-test="navigation-dialog"]')).toBeNull();
       });
       expect(bridge.openHomeNavigation).not.toHaveBeenCalled();
     });

  it('destroys the untrusted session whenever Home becomes hidden',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       const app = await createApp();
       expect(app.shadowRoot!.querySelectorAll('iframe')).toHaveLength(2);

       Object.defineProperty(document, 'hidden', {
         configurable: true,
         value: true,
       });
       document.dispatchEvent(new Event('visibilitychange'));

       await vi.waitFor(() => {
         expect(app.shadowRoot!.querySelectorAll('iframe')).toHaveLength(0);
       });
       expect(bridge.cancelHomeSession).toHaveBeenCalled();

       Object.defineProperty(document, 'hidden', {
         configurable: true,
         value: false,
       });
       document.dispatchEvent(new Event('visibilitychange'));

       await vi.waitFor(() => {
         expect(app.shadowRoot!.querySelectorAll('iframe')).toHaveLength(2);
       });
     });

  it('shows a localized error when an import is rejected', async () => {
    bridge.getHomeSnapshot.mockResolvedValue({
      hasProject: true,
      revision: 'revision-1',
      entry: 'index.html',
      connectors: [],
    });
    bridge.importHome.mockResolvedValue({
      code: 'runtime_error',
      error: 'Untrusted native error text',
    });
    const app = await createApp();
    app.pendingImport_ = '{}';
    await app.updateComplete;
    const confirm = await vi.waitFor(() => {
      const button = app.shadowRoot!.querySelector<HTMLButtonElement>(
          '[data-test="confirm-project-action"]');
      expect(button).not.toBeNull();
      return button!;
    });
    confirm.click();

    const error = await vi.waitFor(() => {
      const alert = app.shadowRoot!.querySelector<HTMLElement>(
          '[data-test="import-error"]');
      expect(alert).not.toBeNull();
      return alert!;
    });
    expect(error.getAttribute('role')).toBe('alert');
    expect(error.textContent).toContain('daoHomeImportFailed');
    expect(error.textContent).not.toContain('Untrusted native error text');
  });

  it('keeps trusted recovery controls available after a frame failure',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       const app = await createApp();
       const frame = app.shadowRoot!.querySelector<HTMLIFrameElement>(
           '[data-test="project-frame"]')!;

       frame.dispatchEvent(new Event('error'));
       await app.updateComplete;

       expect(app.shadowRoot!.querySelector('[data-test="runtime-error"]'))
           .not.toBeNull();
       expect(app.shadowRoot!.querySelector('[data-test="project-menu"]'))
           .not.toBeNull();
       expect(app.shadowRoot!.querySelector('[data-test="retry"]'))
           .not.toBeNull();
       app.shadowRoot!
           .querySelector<HTMLButtonElement>('[data-test="ask-dao-to-fix"]')!
           .click();
       expect(bridge.openHomeAgent).toHaveBeenCalledWith('repair');
     });

  it('requires destructive confirmation before resetting Home', async () => {
    const published = {
      hasProject: true,
      revision: 'revision-1',
      entry: 'index.html',
      connectors: [],
    };
    const empty = {
      hasProject: false,
      revision: '',
      entry: '',
      connectors: [],
    };
    bridge.getHomeSnapshot
        .mockResolvedValueOnce(published)
        .mockResolvedValue(empty);
    const app = await createApp();

    app.shadowRoot!
        .querySelector<HTMLButtonElement>('[data-test="reset-home"]')!
        .click();
    await app.updateComplete;
    expect(app.shadowRoot!.querySelector('[data-test="project-confirmation"]')!
               .textContent)
        .toContain('daoHomeConfirmResetDescription');
    expect(bridge.resetHome).not.toHaveBeenCalled();

    app.shadowRoot!
        .querySelector<HTMLButtonElement>(
            '[data-test="cancel-project-action"]')!
        .click();
    await app.updateComplete;
    expect(app.shadowRoot!.querySelector('[data-test="project-confirmation"]'))
        .toBeNull();

    app.shadowRoot!
        .querySelector<HTMLButtonElement>('[data-test="reset-home"]')!
        .click();
    await app.updateComplete;
    app.shadowRoot!
        .querySelector<HTMLButtonElement>('[data-test="confirm-reset"]')!
        .click();

    await vi.waitFor(() => {
      expect(bridge.resetHome).toHaveBeenCalledWith('revision-1');
      expect(app.shadowRoot!.querySelector('[data-empty-action="create"]'))
          .not.toBeNull();
    });
  });

  it('keeps the published Home visible when reset fails', async () => {
    bridge.getHomeSnapshot.mockResolvedValue({
      hasProject: true,
      revision: 'revision-1',
      entry: 'index.html',
      connectors: [],
    });
    bridge.resetHome.mockResolvedValue({
      error: 'Untrusted native error text',
      code: 'reset_failed',
    });
    const app = await createApp();

    app.shadowRoot!
        .querySelector<HTMLButtonElement>('[data-test="reset-home"]')!
        .click();
    await app.updateComplete;
    app.shadowRoot!
        .querySelector<HTMLButtonElement>('[data-test="confirm-reset"]')!
        .click();

    const alert = await vi.waitFor(() => {
      const value = app.shadowRoot!.querySelector<HTMLElement>(
          '[data-test="reset-error"]');
      expect(value).not.toBeNull();
      return value!;
    });
    expect(alert.textContent).toContain('daoHomeResetFailed');
    expect(alert.textContent).not.toContain('Untrusted native error text');
    expect(app.shadowRoot!.querySelector('[data-test="project-frame"]'))
        .not.toBeNull();
  });

  it('shows and resolves source permission only in the trusted host',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       bridge.getHomePermission.mockResolvedValue({
         kind: 'single',
         id: 'permission-1',
         draftId: 'draft-1',
         baseRevision: 'revision-1',
         connectorId: 'feed',
         origins: ['https://example.test'],
         paths: ['/feed'],
         capabilities: ['read_dom', 'scroll'],
         mode: 'read',
         requestedLimits: {
           maxResultBytes: 1024 * 1024,
           maxItemsPerConnector: 100,
         },
       });
       const app = await createApp();
       const dialog = app.shadowRoot!.querySelector<HTMLElement>(
           '[data-test="permission-dialog"]')!;

       expect(dialog.textContent).toContain('example.test');
       expect(dialog.textContent).toContain('/feed');
       dialog.querySelector<HTMLButtonElement>('[data-test="approve"]')!.click();
       await vi.waitFor(() => {
         expect(bridge.approveHomePermission).toHaveBeenCalledWith('permission-1');
       });
     });

  it('shows the exact resource budget expansion before approval', async () => {
    bridge.getHomeSnapshot.mockResolvedValue({
      hasProject: true,
      revision: 'revision-1',
      entry: 'index.html',
      connectors: [],
    });
    bridge.getHomePermission.mockResolvedValue({
      kind: 'single',
      id: 'permission-1',
      draftId: 'draft-1',
      baseRevision: 'revision-1',
      connectorId: 'feed',
      origins: ['https://example.test'],
      paths: ['/feed'],
      capabilities: ['read_dom'],
      mode: 'read',
      previousLimits: {
        maxResultBytes: 65536,
        maxItemsPerConnector: 25,
      },
      requestedLimits: {
        maxResultBytes: 5242880,
        maxItemsPerConnector: 250,
      },
    });

    const app = await createApp();
    const dialog = app.shadowRoot!.querySelector<HTMLElement>(
        '[data-test="permission-dialog"]')!;

    expect(dialog.textContent)
        .toContain('daoHomeResultBytesIncrease:65,536:5,242,880');
    expect(dialog.textContent)
        .toContain('daoHomeItemLimitIncrease:25:250');
  });

  it('shows exact selectable batch scopes and confirms only selected sources',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       bridge.getHomePermission.mockResolvedValue({
         kind: 'batch',
         id: 'batch-1',
         draftId: 'draft-1',
         baseRevision: 'revision-1',
         items: [
           {
             connectorId: 'github',
             label: 'GitHub',
             origins: ['https://github.com'],
             paths: ['/notifications'],
             capabilities: ['read_dom', 'scroll'],
             mode: 'read',
             requestedLimits: {
               maxResultBytes: 1048576,
               maxItemsPerConnector: 100,
             },
             authenticationMayBeRequired: true,
           },
           {
             connectorId: 'bilibili',
             label: 'Bilibili',
             origins: ['https://www.bilibili.com'],
             paths: ['/video', '/v/popular'],
             capabilities: ['read_dom', 'read_style'],
             mode: 'read',
             previousLimits: {
               maxResultBytes: 65536,
               maxItemsPerConnector: 25,
             },
             requestedLimits: {
               maxResultBytes: 5242880,
               maxItemsPerConnector: 250,
             },
             authenticationMayBeRequired: false,
           },
           {
             connectorId: 'forum',
             label: 'Forum',
             origins: ['https://forum.example'],
             paths: ['/latest'],
             capabilities: ['read_dom'],
             mode: 'read',
             requestedLimits: {
               maxResultBytes: 2048,
               maxItemsPerConnector: 10,
             },
             authenticationMayBeRequired: true,
           },
         ],
       });

       const app = await createApp();
       const dialogs = app.shadowRoot!.querySelectorAll(
           '[data-test="permission-dialog"]');
       expect(dialogs).toHaveLength(1);
       const rows = [...dialogs[0]!.querySelectorAll<HTMLElement>(
           '[data-connector-id]')];
       expect(rows.map(row => row.dataset.connectorId)).toEqual([
         'github', 'bilibili', 'forum',
       ]);
       for (const row of rows) {
         expect(row.querySelector<HTMLInputElement>('input[type="checkbox"]')!
                    .checked)
             .toBe(true);
         expect(row.textContent).toContain('daoHomeActiveOnly');
         expect(row.textContent).toContain('daoHomeNotAllowed');
       }
       expect(rows[0]!.textContent).toContain('GitHub');
       expect(rows[0]!.textContent).toContain('https://github.com');
       expect(rows[0]!.textContent).toContain('/notifications');
       expect(rows[0]!.textContent).toContain(
           'daoHomeCapabilityReadContent');
       expect(rows[0]!.textContent).toContain('daoHomeCapabilityScroll');
       expect(rows[0]!.textContent).toContain(
           'daoHomeAuthenticationMayBeRequired');
       expect(rows[0]!.textContent).toContain(
           'daoHomeResultBytesLimit:1,048,576');
       expect(rows[0]!.textContent).toContain('daoHomeItemLimit:100');
       expect(rows[1]!.textContent).toContain('Bilibili');
       expect(rows[1]!.textContent).toContain('https://www.bilibili.com');
       expect(rows[1]!.textContent).toContain('/video, /v/popular');
       expect(rows[1]!.textContent).toContain('daoHomeCapabilityReadStyle');
       expect(rows[1]!.textContent).not.toContain(
           'daoHomeAuthenticationMayBeRequired');
       expect(rows[1]!.textContent).toContain(
           'daoHomeResultBytesIncrease:65,536:5,242,880');
       expect(rows[1]!.textContent).toContain(
           'daoHomeItemLimitIncrease:25:250');
       expect(rows[2]!.textContent).toContain('Forum');
       expect(rows[2]!.textContent).toContain('https://forum.example');
       expect(rows[2]!.textContent).toContain('/latest');
       expect(rows[2]!.textContent).toContain(
           'daoHomeResultBytesLimit:2,048');
       expect(rows[2]!.textContent).toContain('daoHomeItemLimit:10');

       rows[1]!
           .querySelector<HTMLInputElement>('input[type="checkbox"]')!
           .click();
       await app.updateComplete;
       dialogs[0]!.querySelector<HTMLButtonElement>(
           '[data-test="confirm-bootstrap-permission"]')!.click();

       await vi.waitFor(() => {
         expect(bridge.resolveHomeBootstrapPermission).toHaveBeenCalledOnce();
         expect(bridge.resolveHomeBootstrapPermission).toHaveBeenCalledWith(
             'batch-1', ['github', 'forum']);
         expect(app.shadowRoot!.querySelector(
             '[data-test="permission-dialog"]')).toBeNull();
       });
     });

  it('rejects the batch with an empty selection and removes the dialog',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: false,
         revision: '',
         entry: '',
         connectors: [],
       });
       bridge.getHomePermission.mockResolvedValue({
         kind: 'batch',
         id: 'batch-1',
         draftId: 'draft-1',
         baseRevision: '',
         items: [{
           connectorId: 'github',
           label: 'GitHub',
           origins: ['https://github.com'],
           paths: ['/notifications'],
           capabilities: ['read_dom'],
           mode: 'read',
           requestedLimits: {
             maxResultBytes: 1048576,
             maxItemsPerConnector: 100,
           },
           authenticationMayBeRequired: false,
         }],
       });

       const app = await createApp();
       app.shadowRoot!.querySelector<HTMLButtonElement>(
           '[data-test="reject-bootstrap-permission"]')!.click();

       await vi.waitFor(() => {
         expect(bridge.resolveHomeBootstrapPermission).toHaveBeenCalledOnce();
         expect(bridge.resolveHomeBootstrapPermission).toHaveBeenCalledWith(
             'batch-1', []);
         expect(app.shadowRoot!.querySelector(
             '[data-test="permission-dialog"]')).toBeNull();
       });
     });

  it('keeps project source read-only and behind a secondary action',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       bridge.getHomeFiles.mockResolvedValue(['index.html', 'src/app.js']);
       bridge.readHomeFile.mockResolvedValue('<main>Home</main>');
       const app = await createApp();

       app.shadowRoot!.querySelector<HTMLButtonElement>(
           '[data-test="view-source"]')!.click();
       await vi.waitFor(() => {
         expect(bridge.getHomeFiles).toHaveBeenCalledWith('revision-1');
         expect(app.shadowRoot!.querySelector(
             '[data-source-path="index.html"]')).not.toBeNull();
       });
       await app.readSource_('index.html');
       await vi.waitFor(() => {
         expect(bridge.readHomeFile).toHaveBeenCalledWith(
             'revision-1', 'index.html');
       });
       expect(app.shadowRoot!.querySelector('pre')!.textContent)
           .toContain('<main>Home</main>');
       expect(app.shadowRoot!.querySelector('textarea')).toBeNull();
     });

  it('runs an explicit Agent sample through the active trusted host',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       bridge.startHomeConnector.mockResolvedValue({
         error: 'Sign in required',
         code: 'auth_required',
       });
       await createApp();
       const registration = bridge.addListener.mock.calls.find(
           ([eventName]) =>
               eventName === 'dao-home-agent-connector-request');
       expect(registration).toBeDefined();

       registration![1]({
         requestId: 'request-1',
         connectorId: 'feed',
         input: {page: 1},
       });

       await vi.waitFor(() => {
         expect(bridge.completeHomeAgentConnector).toHaveBeenCalledWith(
             'request-1', {
               ok: false,
               code: 'auth_required',
             });
       });
     });

  it('runs an approved Agent draft sample without publishing first',
     async () => {
       bridge.getHomeSnapshot.mockResolvedValue({
         hasProject: true,
         revision: 'revision-1',
         entry: 'index.html',
         connectors: [],
       });
       bridge.startHomeDraftConnector.mockResolvedValue({
         error: 'Sign in required',
         code: 'auth_required',
       });
       await createApp();
       const registration = bridge.addListener.mock.calls.find(
           ([eventName]) =>
               eventName === 'dao-home-agent-connector-request');

       registration![1]({
         requestId: 'request-1',
         draftId: 'draft-1',
         connectorId: 'feed',
         input: {page: 1},
       });

       await vi.waitFor(() => {
         expect(bridge.startHomeDraftConnector).toHaveBeenCalledWith(
             'draft-1', 'feed', {page: 1});
       });
     });

  it('runs the first connector draft before a project exists', async () => {
    bridge.startHomeDraftConnector.mockResolvedValue({
      error: 'Sign in required',
      code: 'auth_required',
    });
    await createApp();
    const registration = bridge.addListener.mock.calls.find(
        ([eventName]) =>
            eventName === 'dao-home-agent-connector-request');

    registration![1]({
      requestId: 'request-1',
      draftId: 'draft-1',
      connectorId: 'feed',
      input: {page: 1},
    });

    await vi.waitFor(() => {
      expect(bridge.startHomeDraftConnector).toHaveBeenCalledWith(
          'draft-1', 'feed', {page: 1});
    });
  });

  it('does not package a generated-realm preview verdict script', () => {
    const build = readFileSync(
        'src/dao/browser/ui/webui/resources/home/BUILD.gn', 'utf8');

    expect(build).not.toContain('preview_bootstrap');
  });

  it('keeps the trusted preview frame until native validation completes',
     async () => {
       const app = await createApp();
       const registration = bridge.addListener.mock.calls.find(
           ([eventName]) => eventName === 'dao-home-agent-preview-request');
       const completion = bridge.addListener.mock.calls.find(
           ([eventName]) => eventName === 'dao-home-agent-preview-ended');
       expect(registration).toBeDefined();
       expect(completion).toBeDefined();

       registration![1]({
         requestId: 'preview-1',
         draftId: '11111111-1111-4111-8111-111111111111',
         entry: 'index.html',
       });

       const frame = await vi.waitFor(() => {
         const value = app.shadowRoot!.querySelector<HTMLIFrameElement>(
             '[data-test="preview-frame"]');
         expect(value).not.toBeNull();
         return value!;
       });
       expect(frame.src).toBe(
           'chrome-untrusted://dao-home-app/preview/11111111-1111-4111-8111-111111111111/index.html');
       expect(frame.getAttribute('sandbox')).toBe('allow-scripts allow-forms');

       window.dispatchEvent(new MessageEvent('message', {
         source: frame.contentWindow,
         origin: 'null',
         data: {
           daoHome: 1,
           requestId: 'runtime-1',
           revision: '11111111-1111-4111-8111-111111111111',
           method: 'runtime.previewReady',
           params: {},
         },
       }));

       await Promise.resolve();
       expect(bridge.notifyHomeAgentPreviewLoaded).not.toHaveBeenCalled();
       expect(app.shadowRoot!.querySelector('[data-test="preview-frame"]'))
           .toBe(frame);

       frame.dispatchEvent(new Event('load'));
       await vi.waitFor(() => {
         expect(bridge.notifyHomeAgentPreviewLoaded).toHaveBeenCalledWith(
             'preview-1');
         expect(app.shadowRoot!.querySelector('[data-test="preview-frame"]'))
             .toBe(frame);
       });

       completion![1]('another-preview');
       await app.updateComplete;
       expect(app.shadowRoot!.querySelector('[data-test="preview-frame"]'))
           .toBe(frame);

       completion![1]('preview-1');
       await vi.waitFor(() => {
         expect(app.shadowRoot!.querySelector('[data-test="preview-frame"]'))
             .toBeNull();
       });
     });

  it('does not let generated runtime errors complete the preview', async () => {
    const app = await createApp();
    const registration = bridge.addListener.mock.calls.find(
        ([eventName]) => eventName === 'dao-home-agent-preview-request');

    registration![1]({
      requestId: 'preview-1',
      draftId: '11111111-1111-4111-8111-111111111111',
      entry: 'index.html',
    });
    const frame = await vi.waitFor(() => {
      const value = app.shadowRoot!.querySelector<HTMLIFrameElement>(
          '[data-test="preview-frame"]');
      expect(value).not.toBeNull();
      return value!;
    });
    window.dispatchEvent(new MessageEvent('message', {
      source: frame.contentWindow,
      origin: 'null',
      data: {
        daoHome: 1,
        requestId: 'runtime-1',
        revision: '11111111-1111-4111-8111-111111111111',
        method: 'runtime.report',
        params: {kind: 'error'},
      },
    }));

    await Promise.resolve();
    expect(bridge.notifyHomeAgentPreviewLoaded).not.toHaveBeenCalled();
    expect(app.shadowRoot!.querySelector('[data-test="preview-frame"]'))
        .toBe(frame);
  });

  it('shows a read-only line diff for an older source version', async () => {
    const {createSourceDiff} = await import('../dao_home_app.js');

    expect(createSourceDiff(
        'const color = "red";\nrender();',
        'const color = "blue";\nrender();')).toBe(
        '--- selected version\n+++ current version\n' +
        '- const color = "red";\n+ const color = "blue";\n  render();');
  });
});
