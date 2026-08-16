// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {CrLitElement, css, html, nothing} from '//resources/lit/v3_0/lit.rollup.js';
import {loadTimeData} from '//resources/js/load_time_data.js';

import {
  addListener,
  approveHomePermission,
  cancelHomePermission,
  completeHomeAgentConnector,
  exportHome,
  getHomeFiles,
  getHomePermission,
  getHomeSnapshot,
  getHomeVersions,
  importHome,
  notifyHomeAgentPreviewLoaded,
  openHomeAgent,
  readHomeFile,
  recordHomeRuntimeError,
  removeListener,
  resolveHomeBootstrapPermission,
  resolveHomeMedia,
  resetHome,
  rollbackHome,
} from './home_bridge.js';
import type {
  HomePermissionBatchItem,
  HomePermissionRequest,
  HomeResourceLimits,
  HomeSnapshot,
  HomeVersion,
  WebUiListener,
} from './home_bridge.js';
import {
  cancelHomeSession,
  openHomeNavigation,
  setHomeSelection,
} from './home_bridge.js';
import {ConnectorHost} from './connector_host.js';

interface PendingNavigation {
  url: string;
  reply: (value: {result?: unknown; error?: string; code?: string}) => void;
}

interface PendingPreview {
  requestId: string;
  draftId: string;
  entry: string;
  loaded: boolean;
}

export function createSourceDiff(before: string, after: string): string {
  const previous = before.split('\n');
  const current = after.split('\n');
  const lines = ['--- selected version', '+++ current version'];
  const count = Math.max(previous.length, current.length);
  for (let index = 0; index < count; ++index) {
    if (previous[index] === current[index]) {
      lines.push(`  ${previous[index] ?? ''}`);
      continue;
    }
    if (previous[index] !== undefined) lines.push(`- ${previous[index]}`);
    if (current[index] !== undefined) lines.push(`+ ${current[index]}`);
  }
  return lines.join('\n');
}

export class DaoHomeApp extends CrLitElement {
  static get is() {
    return 'dao-home-app';
  }

  static override get properties() {
    return {
      loading_: {type: Boolean},
      runtimeFailed_: {type: Boolean},
      snapshot_: {type: Object},
      versions_: {type: Array},
      permission_: {type: Object},
      selectedBootstrapConnectorIds_: {type: Object},
      panel_: {type: String},
      files_: {type: Array},
      selectedFile_: {type: String},
      source_: {type: String},
      sourceRevision_: {type: String},
      sourceDiff_: {type: String},
      pendingImport_: {type: String},
      importFailed_: {type: Boolean},
      pendingRollback_: {type: Object},
      pendingReset_: {type: Boolean},
      resetFailed_: {type: Boolean},
      pendingNavigation_: {type: Object},
      pendingPreview_: {type: Object},
      sessionActive_: {type: Boolean},
    };
  }

  static override get styles() {
    return css`
      :host {
        --cloud: rgb(231, 238, 245);
        --ink: rgb(31, 44, 56);
        --accent: rgb(70, 120, 190);
        --mist: rgb(247, 250, 252);
        --hairline: rgb(200, 214, 227);
        --surface: rgba(255, 255, 255, .78);
        --secondary: rgba(31, 44, 56, .62);
        background: var(--cloud);
        color: var(--ink);
        display: block;
        height: 100vh;
      }
      * { box-sizing: border-box; }
      button { font: inherit; }
      button:focus-visible, summary:focus-visible {
        outline: 2px solid var(--accent);
        outline-offset: 3px;
      }
      .shell { height: 100%; position: relative; }
      .loading, .empty {
        align-items: center;
        display: flex;
        flex-direction: column;
        height: 100%;
        justify-content: center;
        padding: 32px;
        text-align: center;
      }
      .mark {
        background: radial-gradient(circle at 35% 30%, #fff 0 8%,
                    #8fb2df 9% 28%, var(--accent) 29% 64%, #315c91 65%);
        border: 6px solid rgba(255, 255, 255, .64);
        border-radius: 24px;
        box-shadow: 0 18px 50px rgba(47, 83, 126, .22);
        height: 76px;
        margin-bottom: 24px;
        transform: rotate(-6deg);
        width: 76px;
      }
      h1 {
        font-size: clamp(28px, 4vw, 44px);
        letter-spacing: -.045em;
        line-height: 1.04;
        margin: 0;
      }
      .empty p {
        color: var(--secondary);
        line-height: 1.55;
        margin: 14px auto 28px;
        max-width: 480px;
      }
      .actions { display: flex; flex-wrap: wrap; gap: 10px; justify-content: center; }
      .action {
        background: var(--surface);
        border: 1px solid var(--hairline);
        border-radius: 12px;
        color: var(--ink);
        cursor: pointer;
        min-height: 44px;
        padding: 0 18px;
        transition: transform 120ms ease, background 120ms ease;
      }
      .action.primary { background: var(--accent); border-color: var(--accent); color: white; }
      .action:hover { background: white; }
      .action.primary:hover { background: rgb(55, 103, 170); }
      .action:active { transform: scale(.98); }
      .canvas {
        background: var(--mist);
        inset: 0;
        overflow: hidden;
        position: absolute;
      }
      iframe { border: 0; height: 100%; width: 100%; }
      .connector-sandbox { display: none; }
      .preview-sandbox {
        height: 768px;
        left: -10000px;
        pointer-events: none;
        position: fixed;
        top: 0;
        width: 1024px;
      }
      .toolbar {
        align-items: center;
        display: flex;
        gap: 8px;
        position: absolute;
        right: 26px;
        top: 26px;
        z-index: 3;
      }
      details { position: relative; }
      summary {
        background: rgba(247, 250, 252, .9);
        border: 1px solid var(--hairline);
        border-radius: 11px;
        color: var(--ink);
        cursor: pointer;
        font-size: 13px;
        list-style: none;
        padding: 9px 13px;
      }
      summary::-webkit-details-marker { display: none; }
      .menu {
        background: var(--mist);
        border: 1px solid var(--hairline);
        border-radius: 12px;
        box-shadow: 0 18px 48px rgba(31, 44, 56, .18);
        display: grid;
        gap: 4px;
        min-width: 220px;
        padding: 8px;
        position: absolute;
        right: 0;
        top: calc(100% + 8px);
      }
      .menu button, .import-label {
        background: transparent;
        border: 0;
        border-radius: 8px;
        color: var(--ink);
        cursor: pointer;
        padding: 9px 10px;
        text-align: start;
      }
      .menu button:hover, .import-label:hover { background: rgba(70, 120, 190, .1); }
      .menu button.danger {
        border-top: 1px solid var(--hairline);
        border-radius: 0 0 8px 8px;
        color: rgb(166, 48, 48);
        margin-top: 4px;
        padding-top: 13px;
      }
      .versions { border-top: 1px solid var(--hairline); margin-top: 4px; padding-top: 5px; }
      .version { color: var(--secondary); font-size: 11px; padding: 5px 10px; }
      input[type=file] { display: none; }
      .runtime-error {
        align-items: center;
        backdrop-filter: blur(12px);
        background: rgba(247, 250, 252, .88);
        display: flex;
        flex-direction: column;
        inset: 0;
        justify-content: center;
        position: absolute;
        z-index: 2;
      }
      .import-error, .reset-error {
        background: var(--surface);
        border: 1px solid var(--hairline);
        border-radius: 12px;
        box-shadow: 0 12px 30px rgba(31, 44, 56, .16);
        left: 50%;
        max-width: min(420px, calc(100% - 32px));
        padding: 12px 16px;
        position: absolute;
        top: 64px;
        transform: translateX(-50%);
        z-index: 5;
      }
      .runtime-error h2 { margin: 0 0 8px; }
      .runtime-error p { color: var(--secondary); margin: 0 0 20px; }
      .scrim {
        align-items: center;
        background: rgba(17, 28, 39, .34);
        display: flex;
        inset: 0;
        justify-content: center;
        padding: 24px;
        position: absolute;
        z-index: 6;
      }
      .dialog, .panel {
        background: var(--mist);
        border: 1px solid var(--hairline);
        border-radius: 16px;
        box-shadow: 0 24px 72px rgba(17, 28, 39, .25);
        max-height: min(720px, calc(100vh - 48px));
        overflow: auto;
      }
      .dialog { max-width: 520px; padding: 24px; width: 100%; }
      .dialog h2, .panel h2 { font-size: 20px; margin: 0 0 8px; }
      .dialog p { color: var(--secondary); line-height: 1.5; margin: 0 0 18px; }
      .scope {
        background: var(--surface);
        border: 1px solid var(--hairline);
        border-radius: 12px;
        display: grid;
        gap: 12px;
        margin: 16px 0 20px;
        padding: 14px;
      }
      .scope-row { display: grid; gap: 3px; }
      .scope-label { color: var(--secondary); font-size: 11px; text-transform: uppercase; }
      .batch-sources { display: grid; gap: 12px; margin: 16px 0 20px; }
      .batch-source {
        background: var(--surface);
        border: 1px solid var(--hairline);
        border-radius: 12px;
        display: grid;
        gap: 12px;
        padding: 14px;
      }
      .batch-source-choice {
        align-items: center;
        cursor: pointer;
        display: flex;
        font-weight: 600;
        gap: 10px;
      }
      .batch-source-choice input { accent-color: var(--accent); }
      .dialog-actions { display: flex; gap: 8px; justify-content: flex-end; }
      .action.danger {
        background: rgb(166, 48, 48);
        border-color: rgb(166, 48, 48);
        color: white;
      }
      .action.danger:hover { background: rgb(142, 38, 38); }
      .panel {
        display: flex;
        flex-direction: column;
        height: min(720px, calc(100vh - 48px));
        max-width: 960px;
        padding: 18px;
        width: 100%;
      }
      .panel-header { align-items: center; display: flex; justify-content: space-between; }
      .icon-button {
        background: transparent;
        border: 0;
        color: var(--ink);
        cursor: pointer;
        font-size: 20px;
        padding: 6px 10px;
      }
      .source-layout {
        border: 1px solid var(--hairline);
        border-radius: 12px;
        display: grid;
        flex: 1;
        grid-template-columns: minmax(170px, 240px) 1fr;
        min-height: 0;
        overflow: hidden;
      }
      .source-controls {
        align-items: center;
        display: flex;
        gap: 8px;
        margin: 0 0 10px;
      }
      .source-controls select {
        background: var(--surface);
        border: 1px solid var(--hairline);
        border-radius: 8px;
        color: var(--ink);
        font: inherit;
        padding: 6px 9px;
      }
      .file-list { border-right: 1px solid var(--hairline); overflow: auto; padding: 8px; }
      .file-list button {
        background: transparent;
        border: 0;
        border-radius: 7px;
        color: var(--ink);
        cursor: pointer;
        display: block;
        font: 12px ui-monospace, monospace;
        overflow: hidden;
        padding: 7px 8px;
        text-align: start;
        text-overflow: ellipsis;
        width: 100%;
      }
      .file-list button:hover, .file-list button[selected] { background: rgba(70, 120, 190, .1); }
      pre {
        font: 12px/1.55 ui-monospace, SFMono-Regular, Menlo, monospace;
        margin: 0;
        overflow: auto;
        padding: 18px;
        tab-size: 2;
        white-space: pre-wrap;
      }
      .version-list { display: grid; gap: 8px; overflow: auto; padding-top: 10px; }
      .version-card {
        background: var(--surface);
        border: 1px solid var(--hairline);
        border-radius: 10px;
        color: var(--ink);
        cursor: pointer;
        padding: 12px;
        text-align: start;
      }
      .version-meta { color: var(--secondary); font-size: 11px; margin-top: 5px; }
      @media (prefers-color-scheme: dark) {
        :host {
          --cloud: rgb(54, 59, 64);
          --ink: rgba(255, 255, 255, .92);
          --mist: rgb(45, 50, 55);
          --hairline: rgba(255, 255, 255, .14);
          --surface: rgba(255, 255, 255, .08);
          --secondary: rgba(255, 255, 255, .62);
        }
        summary { background: rgba(45, 50, 55, .9); }
        .action:hover { background: rgba(255, 255, 255, .13); }
        .menu button.danger { color: rgb(255, 166, 166); }
        .action.danger, .action.danger:hover {
          background: rgb(176, 58, 58);
          color: white;
        }
        .runtime-error { background: rgba(45, 50, 55, .9); }
      }
      @media (prefers-reduced-motion: reduce) {
        .action { transition: none; }
      }
    `;
  }

  declare protected loading_: boolean;
  declare protected runtimeFailed_: boolean;
  declare protected snapshot_: HomeSnapshot|null;
  declare protected versions_: HomeVersion[];
  declare protected permission_: HomePermissionRequest|null;
  declare protected selectedBootstrapConnectorIds_: Set<string>;
  declare protected panel_: 'source'|'versions'|null;
  declare protected files_: string[];
  declare protected selectedFile_: string;
  declare protected source_: string;
  declare protected sourceRevision_: string;
  declare protected sourceDiff_: string;
  declare protected pendingImport_: string;
  declare protected importFailed_: boolean;
  declare protected pendingRollback_: HomeVersion|null;
  declare protected pendingReset_: boolean;
  declare protected resetFailed_: boolean;
  declare protected pendingNavigation_: PendingNavigation|null;
  declare protected pendingPreview_: PendingPreview|null;
  declare protected sessionActive_: boolean;
  private connectorHost_: ConnectorHost|null = null;
  private projectChangedListener_: WebUiListener|null = null;
  private permissionChangedListener_: WebUiListener|null = null;
  private agentConnectorListener_: WebUiListener|null = null;
  private agentPreviewListener_: WebUiListener|null = null;
  private agentPreviewEndedListener_: WebUiListener|null = null;
  private readonly onRuntimeMessage_ = (event: MessageEvent) => {
    void this.handleRuntimeMessage_(event);
  };
  private readonly onVisibilityChanged_ = () => {
    const active = !document.hidden;
    if (active === this.sessionActive_) {
      return;
    }
    if (!active) {
      this.cancelNavigation_();
      this.pendingPreview_ = null;
      this.connectorHost_?.disconnect();
      this.connectorHost_ = null;
      cancelHomeSession();
    }
    this.sessionActive_ = active;
  };

  constructor() {
    super();
    this.loading_ = true;
    this.runtimeFailed_ = false;
    this.snapshot_ = null;
    this.versions_ = [];
    this.permission_ = null;
    this.selectedBootstrapConnectorIds_ = new Set();
    this.panel_ = null;
    this.files_ = [];
    this.selectedFile_ = '';
    this.source_ = '';
    this.sourceRevision_ = '';
    this.sourceDiff_ = '';
    this.pendingImport_ = '';
    this.importFailed_ = false;
    this.pendingRollback_ = null;
    this.pendingReset_ = false;
    this.resetFailed_ = false;
    this.pendingNavigation_ = null;
    this.pendingPreview_ = null;
    this.sessionActive_ = !document.hidden;
  }

  override connectedCallback(): void {
    super.connectedCallback();
    window.addEventListener('message', this.onRuntimeMessage_);
    document.addEventListener(
        'visibilitychange', this.onVisibilityChanged_);
    this.projectChangedListener_ = addListener(
        'dao-home-project-changed', () => {
          this.cancelNavigation_();
          this.pendingPreview_ = null;
          void this.refresh_();
        });
    this.permissionChangedListener_ = addListener(
        'dao-home-permission-changed', request => {
          this.setPermission_(request as HomePermissionRequest|null);
        });
    this.agentConnectorListener_ = addListener(
        'dao-home-agent-connector-request', request => {
          void this.handleAgentConnectorRequest_(request);
        });
    this.agentPreviewListener_ = addListener(
        'dao-home-agent-preview-request', request => {
          this.handleAgentPreviewRequest_(request);
        });
    this.agentPreviewEndedListener_ = addListener(
        'dao-home-agent-preview-ended', requestId => {
          if (typeof requestId === 'string' &&
              requestId === this.pendingPreview_?.requestId) {
            this.pendingPreview_ = null;
          }
        });
    void this.refresh_();
  }

  override disconnectedCallback(): void {
    window.removeEventListener('message', this.onRuntimeMessage_);
    document.removeEventListener(
        'visibilitychange', this.onVisibilityChanged_);
    this.cancelNavigation_();
    this.pendingPreview_ = null;
    this.connectorHost_?.disconnect();
    this.connectorHost_ = null;
    if (this.projectChangedListener_)
      removeListener(this.projectChangedListener_);
    if (this.permissionChangedListener_)
      removeListener(this.permissionChangedListener_);
    if (this.agentConnectorListener_)
      removeListener(this.agentConnectorListener_);
    if (this.agentPreviewListener_)
      removeListener(this.agentPreviewListener_);
    if (this.agentPreviewEndedListener_)
      removeListener(this.agentPreviewEndedListener_);
    cancelHomeSession();
  }

  override updated(): void {
    if (!this.sessionActive_ || !this.snapshot_ || this.connectorHost_) {
      return;
    }
    const frame = this.shadowRoot!.querySelector<HTMLIFrameElement>(
        '[data-test="connector-sandbox"]');
    if (frame) {
      this.connectorHost_ = new ConnectorHost(frame, this.snapshot_.revision);
    }
  }

  private async refresh_(): Promise<void> {
    this.loading_ = true;
    this.runtimeFailed_ = false;
    try {
      const hadSnapshot = this.snapshot_ !== null;
      const previousRevision = this.snapshot_?.revision ?? '';
      const [snapshot, versions, permission] = await Promise.all([
        getHomeSnapshot(), getHomeVersions(), getHomePermission(),
      ]);
      this.snapshot_ = snapshot;
      this.versions_ = versions;
      this.setPermission_(permission);
      if (hadSnapshot && previousRevision !== this.snapshot_.revision) {
        this.connectorHost_?.disconnect();
        this.connectorHost_ = null;
        cancelHomeSession();
      }
    } finally {
      this.loading_ = false;
    }
  }

  private frameUrl_(): string {
    const snapshot = this.snapshot_!;
    const route = encodeURIComponent(location.pathname || '/');
    return `chrome-untrusted://dao-home-app/${snapshot.revision}/${snapshot.entry}?route=${route}`;
  }

  private async handleAgentConnectorRequest_(request: unknown): Promise<void> {
    const value = request as {
      requestId?: unknown;
      draftId?: unknown;
      connectorId?: unknown;
      input?: unknown;
    }|null;
    if (!value || typeof value.requestId !== 'string' ||
        typeof value.connectorId !== 'string') {
      return;
    }
    try {
      if (!this.connectorHost_) {
        throw new Error('The Home connector host is unavailable.');
      }
      const result = typeof value.draftId === 'string' && value.draftId ?
          await this.connectorHost_.collectDraft(
              value.draftId, value.connectorId, value.input ?? {}) :
          await this.connectorHost_.collect(
              value.connectorId, value.input ?? {});
      completeHomeAgentConnector(value.requestId, {ok: true, result});
    } catch (error) {
      completeHomeAgentConnector(value.requestId, {
        ok: false,
        code: error instanceof Error &&
                typeof (error as Error&{code?: unknown}).code === 'string' ?
            (error as Error&{code: string}).code : 'temporarily_unavailable',
      });
    }
  }

  private handleAgentPreviewRequest_(request: unknown): void {
    const value = request as {
      requestId?: unknown;
      draftId?: unknown;
      entry?: unknown;
    }|null;
    if (!value || typeof value.requestId !== 'string' ||
        typeof value.draftId !== 'string' ||
        !/^[a-f0-9-]{36}$/.test(value.draftId) ||
        typeof value.entry !== 'string' || !value.entry ||
        value.entry.startsWith('/') || value.entry.includes('..')) {
      return;
    }
    this.pendingPreview_ = {
      requestId: value.requestId,
      draftId: value.draftId,
      entry: value.entry,
      loaded: false,
    };
  }

  private finishAgentPreviewLoad_(): void {
    const preview = this.pendingPreview_;
    if (preview && !preview.loaded) {
      preview.loaded = true;
      notifyHomeAgentPreviewLoaded(preview.requestId);
    }
  }

  private async handleRuntimeMessage_(event: MessageEvent): Promise<void> {
    if (await this.connectorHost_?.handleMessage(event)) {
      return;
    }
    const projectFrame = this.shadowRoot?.querySelector<HTMLIFrameElement>(
        '[data-test="project-frame"]');
    const envelope = event.data as {
      daoHome?: number;
      requestId?: string;
      revision?: string;
      method?: string;
      params?: Record<string, unknown>;
    }|null;
    if (!projectFrame || event.source !== projectFrame.contentWindow ||
        event.origin !== 'null' ||
        envelope?.daoHome !== 1 ||
        envelope.revision !== this.snapshot_?.revision ||
        typeof envelope.requestId !== 'string' ||
        typeof envelope.method !== 'string') {
      return;
    }
    const responseTarget = projectFrame.contentWindow;
    const reply = (value: {result?: unknown; error?: string; code?: string}) =>
        responseTarget?.postMessage({
          daoHome: 1,
          requestId: envelope.requestId,
          revision: envelope.revision,
          ...value,
        }, '*');
    try {
      if (envelope.method === 'sources.collect') {
        const connectorId = envelope.params?.['connectorId'];
        if (typeof connectorId !== 'string' || !this.connectorHost_) {
          throw new Error('Invalid Home connector request.');
        }
        reply({result: await this.connectorHost_.collect(
          connectorId, envelope.params?.['input'] ?? {},
        )});
        return;
      }
      if (envelope.method === 'navigation.open') {
        const url = envelope.params?.['url'];
        let destination: URL|null = null;
        try {
          destination = typeof url === 'string' && url.length <= 2048 ?
              new URL(url) : null;
        } catch {
          destination = null;
        }
        if (!destination ||
            (destination.protocol !== 'http:' &&
             destination.protocol !== 'https:')) {
          throw new Error('Only HTTP(S) navigation is allowed.');
        }
        if (this.pendingNavigation_) {
          throw new Error('Another navigation request is awaiting approval.');
        }
        this.pendingNavigation_ = {url: destination.href, reply};
        return;
      }
      if (envelope.method === 'navigation.openAction') {
        const actionId = envelope.params?.['actionId'];
        const url = envelope.params?.['url'];
        let destination: URL|null = null;
        try {
          destination = typeof url === 'string' && url.length <= 2048 ?
              new URL(url) : null;
        } catch {
          destination = null;
        }
        if (typeof actionId !== 'string' ||
            !/^[a-zA-Z0-9._-]{1,128}$/.test(actionId) || !destination ||
            (destination.protocol !== 'http:' &&
             destination.protocol !== 'https:')) {
          throw new Error('Invalid Home launch action.');
        }
        openHomeNavigation(destination.href);
        reply({result: {opened: true}});
        return;
      }
      if (envelope.method === 'navigation.openFeedItem') {
        const sourceId = envelope.params?.['sourceId'];
        const url = envelope.params?.['url'];
        let destination: URL|null = null;
        try {
          destination = typeof url === 'string' && url.length <= 2048 ?
              new URL(url) : null;
        } catch {
          destination = null;
        }
        if (typeof sourceId !== 'string' ||
            !/^[a-zA-Z0-9._-]{1,128}$/.test(sourceId) || !destination ||
            (destination.protocol !== 'http:' &&
             destination.protocol !== 'https:')) {
          throw new Error('Invalid Home feed item.');
        }
        openHomeNavigation(destination.href);
        reply({result: {opened: true}});
        return;
      }
      if (envelope.method === 'media.resolve') {
        const handle = envelope.params?.['handle'];
        if (typeof handle !== 'string' ||
            !/^dao-media:[a-f0-9-]{36}$/.test(handle)) {
          throw new Error('Invalid Home media handle.');
        }
        const result = await resolveHomeMedia(handle) as {
          error?: string; code?: string;
        }|unknown;
        if (result && typeof result === 'object' &&
            typeof (result as {error?: unknown}).error === 'string') {
          const error = new Error(
              (result as {error: string}).error) as Error&{code?: string};
          error.code = (result as {code?: string}).code;
          throw error;
        }
        reply({result});
        return;
      }
      if (envelope.method === 'selection.set') {
        const nodeId = envelope.params?.['nodeId'];
        if (typeof nodeId !== 'string' ||
            !/^[a-zA-Z0-9._-]{1,128}$/.test(nodeId)) {
          throw new Error('Invalid Home node selection.');
        }
        setHomeSelection(nodeId);
        reply({result: {selected: true}});
        return;
      }
      if (envelope.method === 'runtime.report') {
        const kind = envelope.params?.['kind'];
        if (kind !== 'error' && kind !== 'unhandled_rejection') {
          throw new Error('Invalid Home runtime diagnostic.');
        }
        this.runtimeFailed_ = true;
        recordHomeRuntimeError(this.snapshot_!.revision, kind);
        reply({result: {recorded: true}});
        return;
      }
      throw new Error('Unsupported Dao Home runtime capability.');
    } catch (error) {
      reply({
        error: error instanceof Error ? error.message : String(error),
        code: error instanceof Error &&
                typeof (error as Error&{code?: unknown}).code === 'string' ?
            (error as Error&{code: string}).code : 'temporarily_unavailable',
      });
    }
  }

  private async export_(): Promise<void> {
    const contents = await exportHome();
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob([contents], {type: 'application/json'}));
    link.download = 'dao-home.json';
    link.click();
    URL.revokeObjectURL(link.href);
  }

  private async import_(event: Event): Promise<void> {
    const file = (event.target as HTMLInputElement).files?.[0];
    if (!file) {
      return;
    }
    this.importFailed_ = false;
    this.pendingImport_ = await file.text();
    (event.target as HTMLInputElement).value = '';
  }

  private async rollback_(version: HomeVersion): Promise<void> {
    this.pendingRollback_ = version;
  }

  private async confirmImport_(): Promise<void> {
    const packageJson = this.pendingImport_;
    this.pendingImport_ = '';
    this.importFailed_ = false;
    const result = await importHome(packageJson);
    if (!('hasProject' in result)) {
      this.importFailed_ = true;
      return;
    }
    await this.refresh_();
  }

  private async confirmRollback_(): Promise<void> {
    const version = this.pendingRollback_;
    this.pendingRollback_ = null;
    if (!version) {
      return;
    }
    await rollbackHome(this.snapshot_!.revision, version.id);
    await this.refresh_();
  }

  private async confirmReset_(): Promise<void> {
    this.pendingReset_ = false;
    this.resetFailed_ = false;
    const result = await resetHome(this.snapshot_!.revision);
    if (!('hasProject' in result)) {
      this.resetFailed_ = true;
      return;
    }
    await this.refresh_();
  }

  private async openSource_(): Promise<void> {
    this.panel_ = 'source';
    this.sourceRevision_ = this.snapshot_!.revision;
    this.files_ = await getHomeFiles(this.sourceRevision_);
    this.selectedFile_ = '';
    this.source_ = '';
    this.sourceDiff_ = '';
  }

  private async selectSourceRevision_(revision: string): Promise<void> {
    this.sourceRevision_ = revision;
    this.files_ = await getHomeFiles(revision);
    this.selectedFile_ = '';
    this.source_ = '';
    this.sourceDiff_ = '';
  }

  private async readSource_(path: string): Promise<void> {
    this.selectedFile_ = path;
    const source = await readHomeFile(this.sourceRevision_, path);
    this.source_ = typeof source === 'string' ? source : '';
    this.sourceDiff_ = '';
    if (this.sourceRevision_ !== this.snapshot_!.revision) {
      const current = await readHomeFile(this.snapshot_!.revision, path);
      if (typeof current === 'string') {
        this.sourceDiff_ = createSourceDiff(this.source_, current);
      }
    }
  }

  private async approvePermission_(): Promise<void> {
    const request = this.permission_;
    if (!request || request.kind !== 'single') {
      return;
    }
    await approveHomePermission(request.id);
    this.permission_ = null;
    await this.refresh_();
  }

  private async cancelPermission_(): Promise<void> {
    const request = this.permission_;
    if (!request || request.kind !== 'single') {
      return;
    }
    await cancelHomePermission(request.id);
    this.permission_ = null;
  }

  private setPermission_(request: HomePermissionRequest|null): void {
    this.permission_ = request;
    this.selectedBootstrapConnectorIds_ = request?.kind === 'batch' ?
        new Set(request.items.map(item => item.connectorId)) : new Set();
  }

  private toggleBootstrapConnector_(connectorId: string, selected: boolean):
      void {
    const next = new Set(this.selectedBootstrapConnectorIds_);
    if (selected) {
      next.add(connectorId);
    } else {
      next.delete(connectorId);
    }
    this.selectedBootstrapConnectorIds_ = next;
  }

  private async resolveBootstrapPermission_(connectSelected: boolean):
      Promise<void> {
    const request = this.permission_;
    if (!request || request.kind !== 'batch') {
      return;
    }
    const selectedConnectorIds = connectSelected ?
        request.items
            .map(item => item.connectorId)
            .filter(id => this.selectedBootstrapConnectorIds_.has(id)) : [];
    this.permission_ = null;
    this.selectedBootstrapConnectorIds_ = new Set();
    await resolveHomeBootstrapPermission(request.id, selectedConnectorIds);
  }

  private cancelNavigation_(): void {
    const navigation = this.pendingNavigation_;
    this.pendingNavigation_ = null;
    navigation?.reply({
      error: 'Navigation was cancelled.',
      code: 'cancelled',
    });
  }

  private confirmNavigation_(): void {
    const navigation = this.pendingNavigation_;
    this.pendingNavigation_ = null;
    if (!navigation) {
      return;
    }
    openHomeNavigation(navigation.url);
    navigation.reply({result: {opened: true}});
  }

  private capabilityLabel_(capability: string): string {
    const key = capability === 'scroll' ? 'daoHomeCapabilityScroll' :
        capability === 'read_style' ? 'daoHomeCapabilityReadStyle' :
                                      'daoHomeCapabilityReadContent';
    return loadTimeData.getString(key);
  }

  private renderMenu_() {
    return html`
      <details data-test="project-menu">
        <summary>${loadTimeData.getString('daoHomeMenu')}</summary>
        <div class="menu">
          <button @click=${() => openHomeAgent('create')}>
            ${loadTimeData.getString('daoHomeEditWithDao')}
          </button>
          <button data-test="view-source" @click=${() => this.openSource_()}>
            ${loadTimeData.getString('daoHomeViewSource')}
          </button>
          <button @click=${() => this.panel_ = 'versions'}>
            ${loadTimeData.getString('daoHomeVersionHistory')}
          </button>
          <button @click=${() => this.export_()}>
            ${loadTimeData.getString('daoHomeExport')}
          </button>
          <label class="import-label">
            ${loadTimeData.getString('daoHomeImport')}
            <input type="file" accept="application/json" @change=${(event: Event) => this.import_(event)}>
          </label>
          <button class="danger" data-test="reset-home"
              @click=${() => this.pendingReset_ = true}>
            ${loadTimeData.getString('daoHomeReset')}
          </button>
        </div>
      </details>
    `;
  }

  private renderPanel_() {
    if (!this.panel_) {
      return nothing;
    }
    return html`
      <div class="scrim" @click=${(event: Event) => {
        if (event.target === event.currentTarget) this.panel_ = null;
      }}>
        <section class="panel" role="dialog" aria-modal="true">
          <div class="panel-header">
            <h2>${loadTimeData.getString(
                this.panel_ === 'source' ? 'daoHomeViewSource' :
                                           'daoHomeVersionHistory')}</h2>
            <button class="icon-button" aria-label=${loadTimeData.getString('daoHomeClose')}
                @click=${() => this.panel_ = null}>×</button>
          </div>
          ${this.panel_ === 'source' ? html`
            <div class="source-controls">
              <label for="source-version">
                ${loadTimeData.getString('daoHomeCompareVersion')}
              </label>
              <select id="source-version" .value=${this.sourceRevision_}
                  @change=${(event: Event) => this.selectSourceRevision_(
                      (event.target as HTMLSelectElement).value)}>
                ${[...this.versions_].reverse().map(version => html`
                  <option value=${version.id}>${version.summary}</option>
                `)}
              </select>
              ${this.sourceDiff_ ? html`<span>${loadTimeData.getString(
                  'daoHomeDiffCurrent')}</span>` : nothing}
            </div>
            <div class="source-layout">
              <nav class="file-list" aria-label=${loadTimeData.getString('daoHomeFiles')}>
                ${this.files_.map(path => html`
                  <button data-source-path=${path}
                      ?selected=${path === this.selectedFile_}
                      @click=${() => this.readSource_(path)}>${path}</button>
                `)}
              </nav>
              <pre><code>${this.sourceDiff_ || this.source_ ||
                  loadTimeData.getString('daoHomeSelectFile')}</code></pre>
            </div>
          ` : html`
            <div class="version-list">
              ${[...this.versions_].reverse().map((version, index) => html`
                <button class="version-card" ?disabled=${index === 0}
                    @click=${() => this.rollback_(version)}>
                  <div>${version.summary}</div>
                  <div class="version-meta">${index === 0 ?
                    loadTimeData.getString('daoHomeCurrentVersion') :
                    loadTimeData.getString('daoHomeRestoreVersion')}</div>
                </button>
              `)}
            </div>
          `}
        </section>
      </div>
    `;
  }

  private renderPermission_() {
    if (!this.permission_) {
      return nothing;
    }
    const request = this.permission_;
    if (request.kind === 'batch') {
      return html`
        <div class="scrim">
          <section class="dialog" role="dialog" aria-modal="true"
              aria-labelledby="bootstrap-permission-title"
              data-test="permission-dialog">
            <h2 id="bootstrap-permission-title">
              ${loadTimeData.getString('daoHomeConnectSourcesTitle')}
            </h2>
            <p>${loadTimeData.getString(
                'daoHomeConnectSourcesDescription')}</p>
            <div class="batch-sources">
              ${request.items.map(item => this.renderBatchPermissionItem_(
                  item))}
            </div>
            <div class="dialog-actions">
              <button class="action" data-test="reject-bootstrap-permission"
                  @click=${() => this.resolveBootstrapPermission_(false)}>
                ${loadTimeData.getString(
                    'daoHomeContinueWithoutSources')}
              </button>
              <button class="action primary"
                  data-test="confirm-bootstrap-permission"
                  @click=${() => this.resolveBootstrapPermission_(true)}>
                ${loadTimeData.getString('daoHomeConnectSelected')}
              </button>
            </div>
          </section>
        </div>
      `;
    }
    return html`
      <div class="scrim">
        <section class="dialog" role="dialog" aria-modal="true"
            data-test="permission-dialog">
          <h2>${loadTimeData.getStringF(
              'daoHomeConnectSource', request.connectorId)}</h2>
          <p>${loadTimeData.getString('daoHomePermissionDescription')}</p>
          <div class="scope">
            <div class="scope-row"><span class="scope-label">${loadTimeData.getString('daoHomeWebsite')}</span>
              <span>${request.origins.join(', ')}</span></div>
            <div class="scope-row"><span class="scope-label">${loadTimeData.getString('daoHomePageScope')}</span>
              <span>${request.paths.join(', ')}</span></div>
            <div class="scope-row"><span class="scope-label">${loadTimeData.getString('daoHomeCapabilities')}</span>
              <span>${request.capabilities.map(value => this.capabilityLabel_(value)).join(', ')}</span></div>
            <div class="scope-row"><span class="scope-label">${loadTimeData.getString('daoHomeRuns')}</span>
              <span>${loadTimeData.getString('daoHomeActiveOnly')}</span></div>
            <div class="scope-row"><span class="scope-label">${loadTimeData.getString('daoHomeWriteAccess')}</span>
              <span>${loadTimeData.getString('daoHomeNotAllowed')}</span></div>
            ${this.renderResourceLimits_(request)}
          </div>
          <div class="dialog-actions">
            <button class="action" data-test="cancel"
                @click=${() => this.cancelPermission_()}>${loadTimeData.getString('daoHomeCancel')}</button>
            <button class="action primary" data-test="approve"
                @click=${() => this.approvePermission_()}>${loadTimeData.getString('daoHomeConnect')}</button>
          </div>
        </section>
      </div>
    `;
  }

  private renderBatchPermissionItem_(item: HomePermissionBatchItem) {
    const detailsId = `bootstrap-source-${item.connectorId}`;
    return html`
      <section class="batch-source" data-connector-id=${item.connectorId}>
        <label class="batch-source-choice">
          <input type="checkbox"
              aria-describedby=${detailsId}
              .checked=${this.selectedBootstrapConnectorIds_.has(
                  item.connectorId)}
              @change=${(event: Event) => this.toggleBootstrapConnector_(
                  item.connectorId,
                  (event.target as HTMLInputElement).checked)}>
          <span>${item.label}</span>
        </label>
        <div class="scope" id=${detailsId}>
          <div class="scope-row">
            <span class="scope-label">${loadTimeData.getString(
                'daoHomeWebsite')}</span>
            <span>${item.origins.join(', ')}</span>
          </div>
          <div class="scope-row">
            <span class="scope-label">${loadTimeData.getString(
                'daoHomePageScope')}</span>
            <span>${item.paths.join(', ')}</span>
          </div>
          <div class="scope-row">
            <span class="scope-label">${loadTimeData.getString(
                'daoHomeCapabilities')}</span>
            <span>${item.capabilities.map(
                value => this.capabilityLabel_(value)).join(', ')}</span>
          </div>
          ${item.authenticationMayBeRequired ? html`
            <div class="scope-row">
              <span>${loadTimeData.getString(
                  'daoHomeAuthenticationMayBeRequired')}</span>
            </div>
          ` : nothing}
          <div class="scope-row">
            <span class="scope-label">${loadTimeData.getString(
                'daoHomeRuns')}</span>
            <span>${loadTimeData.getString('daoHomeActiveOnly')}</span>
          </div>
          <div class="scope-row">
            <span class="scope-label">${loadTimeData.getString(
                'daoHomeWriteAccess')}</span>
            <span>${loadTimeData.getString('daoHomeNotAllowed')}</span>
          </div>
          ${this.renderResourceLimits_(item)}
        </div>
      </section>
    `;
  }

  private renderResourceLimits_(request: {
    previousLimits?: HomeResourceLimits;
    requestedLimits: HomeResourceLimits;
  }) {
    const previous = request.previousLimits;
    const requested = request.requestedLimits;
    const formatter = new Intl.NumberFormat();
    const rows = [];
    if (!previous) {
      rows.push(loadTimeData.getStringF(
          'daoHomeResultBytesLimit',
          formatter.format(requested.maxResultBytes)));
      rows.push(loadTimeData.getStringF(
          'daoHomeItemLimit',
          formatter.format(requested.maxItemsPerConnector)));
    } else {
      if (requested.maxResultBytes > previous.maxResultBytes) {
        rows.push(loadTimeData.getStringF(
            'daoHomeResultBytesIncrease',
            formatter.format(previous.maxResultBytes),
            formatter.format(requested.maxResultBytes)));
      }
      if (requested.maxItemsPerConnector >
          previous.maxItemsPerConnector) {
        rows.push(loadTimeData.getStringF(
            'daoHomeItemLimitIncrease',
            formatter.format(previous.maxItemsPerConnector),
            formatter.format(requested.maxItemsPerConnector)));
      }
    }
    if (rows.length === 0) {
      return nothing;
    }
    return html`
      <div class="scope-row">
        <span class="scope-label">${loadTimeData.getString('daoHomeResourceLimits')}</span>
        <span>${rows.map(row => html`<div>${row}</div>`)}</span>
      </div>
    `;
  }

  private renderConfirmation_() {
    if (!this.pendingImport_ && !this.pendingRollback_ && !this.pendingReset_) {
      return nothing;
    }
    const importing = Boolean(this.pendingImport_);
    const resetting = this.pendingReset_;
    return html`
      <div class="scrim">
        <section class="dialog" role="dialog" aria-modal="true"
            data-test="project-confirmation">
          <h2>${loadTimeData.getString(
              importing ? 'daoHomeConfirmImportTitle' :
              resetting ? 'daoHomeConfirmResetTitle' :
                          'daoHomeConfirmRestoreTitle')}</h2>
          <p>${loadTimeData.getString(
              importing ? 'daoHomeConfirmImportDescription' :
              resetting ? 'daoHomeConfirmResetDescription' :
                          'daoHomeConfirmRestoreDescription')}</p>
          <div class="dialog-actions">
            <button class="action" data-test="cancel-project-action"
                @click=${() => {
              this.pendingImport_ = '';
              this.pendingRollback_ = null;
              this.pendingReset_ = false;
            }}>${loadTimeData.getString('daoHomeCancel')}</button>
            <button class=${resetting ? 'action danger' : 'action primary'}
                data-test=${resetting ? 'confirm-reset' : 'confirm-project-action'}
                @click=${() => importing ? this.confirmImport_() :
                    resetting ? this.confirmReset_() :
                                this.confirmRollback_()}>${loadTimeData.getString(
                    resetting ? 'daoHomeReset' : 'daoHomeConfirm')}</button>
          </div>
        </section>
      </div>
    `;
  }

  private renderNavigation_() {
    if (!this.pendingNavigation_) {
      return nothing;
    }
    return html`
      <div class="scrim">
        <section class="dialog" role="dialog" aria-modal="true"
            data-test="navigation-dialog">
          <h2>${loadTimeData.getString('daoHomeConfirmNavigationTitle')}</h2>
          <p>${loadTimeData.getString(
              'daoHomeConfirmNavigationDescription')}</p>
          <div class="scope">
            <div class="scope-row">
              <span class="scope-label">${loadTimeData.getString(
                  'daoHomeWebsite')}</span>
              <span>${this.pendingNavigation_.url}</span>
            </div>
          </div>
          <div class="dialog-actions">
            <button class="action" data-test="cancel-navigation"
                @click=${() => this.cancelNavigation_()}>
              ${loadTimeData.getString('daoHomeCancel')}
            </button>
            <button class="action primary" data-test="confirm-navigation"
                @click=${() => this.confirmNavigation_()}>
              ${loadTimeData.getString('daoHomeConfirm')}
            </button>
          </div>
        </section>
      </div>
    `;
  }

  private renderPreview_() {
    const preview = this.pendingPreview_;
    if (!preview) {
      return nothing;
    }
    const entry = preview.entry.split('/').map(encodeURIComponent).join('/');
    return html`
      <iframe class="preview-sandbox" data-test="preview-frame"
          src=${`chrome-untrusted://dao-home-app/preview/${preview.draftId}/${entry}`}
          sandbox="allow-scripts allow-forms"
          @load=${() => this.finishAgentPreviewLoad_()}></iframe>
    `;
  }

  override render() {
    if (this.loading_) {
      return html`<div class="loading">${loadTimeData.getString('daoHomeLoading')}</div>`;
    }
    if (!this.snapshot_?.hasProject) {
      return html`
        ${this.sessionActive_ ? html`
          <iframe class="connector-sandbox" data-test="connector-sandbox"
              src="chrome-untrusted://dao-home-connector/"
              sandbox="allow-scripts"></iframe>
        ` : nothing}
        <main class="empty">
          <div class="mark" aria-hidden="true"></div>
          <h1>${loadTimeData.getString('daoHomeEmptyTitle')}</h1>
          <p>${loadTimeData.getString('daoHomeEmptyDescription')}</p>
          <div class="actions">
            <button class="action primary" data-empty-action="create"
                @click=${() => openHomeAgent('create')}>
              ${loadTimeData.getString('daoHomeCreate')}
            </button>
            <button class="action" data-empty-action="history"
                @click=${() => openHomeAgent('history')}>
              ${loadTimeData.getString('daoHomeCreateFromHistory')}
            </button>
          </div>
        </main>
        ${this.renderPreview_()}
        ${this.renderPermission_()}
      `;
    }
    return html`
      <main class="shell">
        ${this.renderPreview_()}
        ${this.sessionActive_ ? html`
          <iframe class="connector-sandbox" data-test="connector-sandbox"
              src="chrome-untrusted://dao-home-connector/"
              sandbox="allow-scripts"></iframe>
          <div class="canvas">
            <iframe data-test="project-frame" src=${this.frameUrl_()}
                sandbox="allow-scripts allow-forms"
                @error=${() => this.runtimeFailed_ = true}></iframe>
            ${this.runtimeFailed_ ? html`
              <section class="runtime-error" data-test="runtime-error">
                <h2>${loadTimeData.getString('daoHomeRuntimeErrorTitle')}</h2>
                <p>${loadTimeData.getString(
                    'daoHomeRuntimeErrorDescription')}</p>
                <div class="actions">
                <button class="action primary" data-test="retry"
                    @click=${() => this.refresh_()}>
                  ${loadTimeData.getString('daoHomeRetry')}
                </button>
                <button class="action" data-test="ask-dao-to-fix"
                    @click=${() => openHomeAgent('repair')}>
                  ${loadTimeData.getString('daoHomeAskDaoToFix')}
                </button>
                </div>
              </section>
            ` : nothing}
          </div>
        ` : nothing}
        ${this.importFailed_ ? html`
          <section class="import-error" data-test="import-error" role="alert">
            ${loadTimeData.getString('daoHomeImportFailed')}
          </section>
        ` : nothing}
        ${this.resetFailed_ ? html`
          <section class="reset-error" data-test="reset-error" role="alert">
            ${loadTimeData.getString('daoHomeResetFailed')}
          </section>
        ` : nothing}
        <div class="toolbar">${this.renderMenu_()}</div>
        ${this.renderPanel_()}
        ${this.renderConfirmation_()}
        ${this.renderPermission_()}
        ${this.renderNavigation_()}
      </main>
    `;
  }
}

customElements.define(DaoHomeApp.is, DaoHomeApp);

declare global {
  interface HTMLElementTagNameMap {
    'dao-home-app': DaoHomeApp;
  }
}
