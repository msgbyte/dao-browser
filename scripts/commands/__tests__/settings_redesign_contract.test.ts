import {existsSync, readFileSync} from 'node:fs';
import path from 'node:path';

import {describe, expect, it} from 'vitest';

const patchRoot =
    'src/patches/chrome/browser/resources/settings';

function patchPath(relativePath: string): string {
  return path.join(process.cwd(), patchRoot, relativePath);
}

function readPatch(relativePath: string): string {
  return readFileSync(patchPath(relativePath), 'utf-8');
}

function readDaoSource(relativePath: string): string {
  return readFileSync(path.join(process.cwd(), relativePath), 'utf-8');
}

function deletionLines(patch: string): string[] {
  return patch.split('\n').filter(
      line => line.startsWith('-') && !line.startsWith('---'));
}

function preservedContractToken(line: string): string|null {
  return line.match(/href="[^"]+"/)?.[0] ??
      line.match(/routes_\.[A-Z0-9_]+/)?.[0] ??
      line.match(/pageVisibility_\.[A-Za-z0-9_]+/)?.[0] ??
      line.match(/prefs="?[{[]?{?prefs/)?.[0] ??
      line.match(/<settings-[a-z0-9-]+/)?.[0] ?? null;
}

describe('settings continuous overview contract', () => {
  it('provides an integrated table of contents and overview controller', () => {
    const requiredPatches = [
      'settings_ui/settings_ui.html.patch',
      'settings_ui/settings_ui.ts.patch',
      'settings_menu/settings_menu.html.patch',
      'settings_menu/settings_menu.ts.patch',
      'settings_main/settings_main.ts.patch',
      'settings_page/settings_section.html.patch',
      'settings_shared.css.patch',
    ];

    for (const relativePath of requiredPatches) {
      expect(existsSync(patchPath(relativePath)), relativePath).toBe(true);
    }

    const uiPatch = readPatch('settings_ui/settings_ui.html.patch');
    const menuPatch = readPatch('settings_menu/settings_menu.html.patch');
    const menuTsPatch = readPatch('settings_menu/settings_menu.ts.patch');
    const mainTsPatch = readPatch('settings_main/settings_main.ts.patch');
    const uiTsPatch = readPatch('settings_ui/settings_ui.ts.patch');
    const sharedCssPatch = readPatch('settings_shared.css.patch');
    expect(uiPatch).toContain('--settings-menu-width: 184px');
    expect(uiPatch).toContain('id="daoSettingsSearch"');
    expect(uiPatch).not.toContain('id="daoSettingsBrand"');
    expect(uiPatch).not.toContain('id="daoSettingsRailFooter"');
    expect(menuPatch).toContain('data-section="dao"');
    expect(menuPatch).not.toContain('dao-settings-menu-group');
    expect(menuTsPatch).toContain('settings-section-activate');
    expect(mainTsPatch).toContain('overviewMode_');
    expect(mainTsPatch).toContain('switchViews(');
    expect(uiTsPatch).toContain(
        'const requestedSection = window.location.hash.substring(1)');
    expect(uiTsPatch).toContain(
        'this.syncOverviewRoute_(route, requestedSection)');
    expect(sharedCssPatch).toContain(
        '--dao-settings-content-width: 680px');
  });

  it('does not delete route, setting, preference, or visibility markup', () => {
    const behavioralDeletionPatterns = [
      /<a\b.*href=/,
      /routes_\.[A-Z0-9_]+/,
      /pageVisibility_\.[A-Za-z0-9_]+/,
      /prefs=/,
      /<settings-toggle-button\b/,
      /<cr-link-row\b/,
    ];
    const patches = [
      'settings_ui/settings_ui.html.patch',
      'settings_ui/settings_ui.ts.patch',
      'settings_menu/settings_menu.html.patch',
      'settings_main/settings_main.html.patch',
      'settings_shared.css.patch',
      'dao_page/dao_page.html.patch',
      'dao_page/dao_page.ts.patch',
    ].filter(relativePath => existsSync(patchPath(relativePath)));

    for (const relativePath of patches) {
      const patch = readPatch(relativePath);
      const deleted = deletionLines(patch);
      const patterns = relativePath.includes('settings_main/') ||
              relativePath.includes('dao_page/') ?
          [...behavioralDeletionPatterns, /<settings-[a-z0-9-]+\b/] :
          behavioralDeletionPatterns;
      const additions = patch.split('\n')
                            .filter(line => line.startsWith('+') &&
                                !line.startsWith('+++'))
                            .join('\n');
      for (const line of deleted) {
        for (const pattern of patterns) {
          if (pattern.test(line)) {
            const token = preservedContractToken(line);
            expect(token, `${relativePath}: ${line}`).not.toBeNull();
            expect(additions, `${relativePath}: ${line}`).toContain(token!);
          }
        }
      }
    }
  });

  it('preserves fine-grained Agent tool permissions', () => {
    const htmlPatch = readPatch('dao_page/dao_page.html.patch');
    const tsPatch = readPatch('dao_page/dao_page.ts.patch');

    expect(htmlPatch).toContain('data-tool-name$="[[tool]]"');
    expect(tsPatch).toContain('onAgentToolChange_');
    expect(tsPatch).toContain('Object.values(AGENT_TOOL_GROUPS).flat()');
  });

  it('keeps nested top-level page views in overview document flow', () => {
    const nestedPagePatches = [
      'people_page/people_page_index.html.patch',
      'autofill_page/autofill_page_index.html.patch',
      'your_saved_info_page/your_saved_info_page_index.html.patch',
      'appearance_page/appearance_page_index.html.patch',
      'a11y_page/a11y_page_index.html.patch',
    ];

    for (const relativePath of nestedPagePatches) {
      expect(existsSync(patchPath(relativePath)), relativePath).toBe(true);
      expect(readPatch(relativePath), relativePath)
          .toContain('[slot=view]:not(.closing)');
      expect(readPatch(relativePath), relativePath)
          .toContain('position: initial');
    }
  });

  it('keeps usage-statistic mutations inside their scoped pref updates', () => {
    const handler = readDaoSource(
        'src/dao/browser/agent/dao_agent_settings_handler.cc');
    const api_usage = handler.match(
        /void RecordDaoAgentApiUsage[\s\S]*?\n}\n\nvoid RecordDaoAgentToolUsage/)?.[0];
    const tool_usage = handler.match(
        /void RecordDaoAgentToolUsage[\s\S]*?\n}\n\nvoid ResetDaoAgentUsageStats/)?.[0];

    expect(api_usage).toBeDefined();
    expect(tool_usage).toBeDefined();
    expect(api_usage).not.toContain('ReadUsageStatsOrDefault');
    expect(tool_usage).not.toContain('ReadUsageStatsOrDefault');
    expect(api_usage).toMatch(
        /ScopedDictPrefUpdate update[\s\S]*?update->FindDouble/);
    expect(tool_usage).toMatch(
        /ScopedDictPrefUpdate update[\s\S]*?update->FindDict/);
  });

  it('exposes only the restricted Agent management surface', () => {
    const handler = readDaoSource(
        'src/dao/browser/agent/dao_agent_settings_handler.cc');
    for (const message of [
      'getDaoAgentMemorySummary',
      'clearAllDaoAgentMemory',
      'getDaoAgentWorkspaceSummary',
      'openDaoAgentWorkspace',
      'getDaoAgentUsageStats',
      'resetDaoAgentUsageStats',
    ]) {
      expect(handler).toContain(`"${message}"`);
    }
    for (const forbidden of [
      'workspaceRead',
      'workspaceWrite',
      'workspaceEdit',
      'workspaceApplyPatch',
      'workspaceList',
      'workspaceDownload',
    ]) {
      expect(handler).not.toContain(`"${forbidden}"`);
    }
  });

  it('renders the complete Agent management ledger without dropping links', () => {
    const htmlPatch = readPatch('dao_page/dao_page.html.patch');
    const tsPatch = readPatch('dao_page/dao_page.ts.patch');
    const managementSource = tsPatch + htmlPatch;

    for (const token of [
      'conversationCount',
      'preferenceCount',
      'episodeCount',
      'totalSize',
      'usedBytes',
      'capBytes',
      'fileCount',
      'recentActivity',
      'apiCalls',
      'toolCalls',
      'promptTokens',
      'completionTokens',
      'totalTokens',
      'estimatedCost',
      'clearAllMemory',
      'openWorkspace',
      'resetUsageStats',
      'href="dao://skills"',
      'href="dao://memory"',
      'href="dao://dream"',
    ]) {
      expect(managementSource).toContain(token);
    }

    for (const id of [
      'daoAgentMemoryManagement',
      'daoAgentWorkspaceManagement',
      'daoAgentUsageManagement',
      'clearAllMemoryDialog',
      'resetUsageStatsDialog',
    ]) {
      expect(htmlPatch).toContain(`id="${id}"`);
    }

    for (const binding of [
      'id="agentMemoryRetry"',
      'on-click="onRetryMemory_"',
      'id="agentWorkspaceRetry"',
      'on-click="onRetryWorkspace_"',
      'id="agentUsageRetry"',
      'on-click="onRetryUsage_"',
      'id="clearAllMemoryButton"',
      'on-click="onClearMemory_"',
      'id="clearAllMemoryCancel"',
      'on-click="onCancelClearMemory_"',
      'id="clearAllMemoryConfirm"',
      'on-click="onConfirmClearMemory_"',
      'disabled="[[clearMemoryPending_]]"',
      'id="openAgentWorkspaceButton"',
      'on-click="onOpenWorkspace_"',
      'id="resetUsageStatsButton"',
      'on-click="onResetUsage_"',
      'id="resetUsageStatsCancel"',
      'on-click="onCancelResetUsage_"',
      'id="resetUsageStatsConfirm"',
      'on-click="onConfirmResetUsage_"',
      'disabled="[[resetUsagePending_]]"',
    ]) {
      expect(htmlPatch).toContain(binding);
    }

    expect(htmlPatch).toContain('<dl class="dao-agent-management-metrics">');
    expect(htmlPatch).toContain('<ol class="dao-agent-activity-list">');
    expect(htmlPatch).toContain('<ul class="dao-agent-tool-usage-list">');
    expect(htmlPatch).toContain('aria-live="polite"');
    expect(htmlPatch).toContain('@media (max-width: 620px)');
    expect(htmlPatch).toContain(':focus-visible');
    expect(tsPatch).toContain("cr_dialog/cr_dialog.js");
  });
});
