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

function extractBetween(
    source: string, startToken: string, endToken: string): string {
  const start = source.indexOf(startToken);
  const end = source.indexOf(endToken, start + startToken.length);
  expect(start, startToken).toBeGreaterThanOrEqual(0);
  expect(end, endToken).toBeGreaterThan(start);
  return source.slice(start, end);
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

    const memoryCard = extractBetween(
        htmlPatch, 'id="daoAgentMemoryManagement"',
        'id="daoAgentWorkspaceManagement"');
    const workspaceCard = extractBetween(
        htmlPatch, 'id="daoAgentWorkspaceManagement"',
        'id="daoAgentUsageManagement"');
    const usageCard = extractBetween(
        htmlPatch, 'id="daoAgentUsageManagement"',
        'id="daoAgentClearMemoryDialog"');
    const clearDialog = extractBetween(
        htmlPatch, 'id="clearAllMemoryDialog"',
        'id="daoAgentResetUsageDialog"');
    const resetDialog = extractBetween(
        htmlPatch, 'id="resetUsageStatsDialog"', '</section>');

    expect(memoryCard).toMatch(
        /if="\[\[showSummaryLoadError_\(memoryError_, memoryActionSucceeded_\)\]\]"[\s\S]*?id="memoryLoadError"[\s\S]*?id="agentMemoryRetry"[\s\S]*?on-click="onRetryMemory_"/);
    expect(memoryCard).toMatch(
        /if="\[\[showSummaryRefreshError_\(memoryError_, memoryActionSucceeded_\)\]\]"[\s\S]*?id="memoryRefreshError"[\s\S]*?daoAgentManagementMemoryRefreshError[\s\S]*?id="agentMemoryRetry"/);
    expect(workspaceCard).toMatch(
        /if="\[\[workspaceError_\]\]"[\s\S]*?id="agentWorkspaceRetry"[\s\S]*?on-click="onRetryWorkspace_"/);
    expect(usageCard).toMatch(
        /if="\[\[showSummaryLoadError_\(usageError_, usageActionSucceeded_\)\]\]"[\s\S]*?id="usageLoadError"[\s\S]*?id="agentUsageRetry"[\s\S]*?on-click="onRetryUsage_"/);
    expect(usageCard).toMatch(
        /if="\[\[showSummaryRefreshError_\(usageError_, usageActionSucceeded_\)\]\]"[\s\S]*?id="usageRefreshError"[\s\S]*?daoAgentManagementUsageRefreshError[\s\S]*?id="agentUsageRetry"/);
    expect(memoryCard).toContain('if="[[memoryActionError_]]"');
    expect(memoryCard).toContain('$i18n{daoAgentManagementMemoryRefreshError}');
    expect(workspaceCard).toContain('if="[[workspaceActionError_]]"');
    expect(usageCard).toContain('if="[[usageActionError_]]"');
    expect(usageCard).toContain('$i18n{daoAgentManagementUsageRefreshError}');

    expect(clearDialog).toMatch(
        /on-close="onClearMemoryDialogClose_"[\s\S]*?id="clearAllMemoryConfirm"[\s\S]*?disabled="\[\[clearMemoryPending_\]\]"[\s\S]*?on-click="onConfirmClearMemory_"/);
    expect(resetDialog).toMatch(
        /on-close="onResetUsageDialogClose_"[\s\S]*?id="resetUsageStatsConfirm"[\s\S]*?disabled="\[\[resetUsagePending_\]\]"[\s\S]*?on-click="onConfirmResetUsage_"/);

    expect(memoryCard.match(/dao-agent-management-loading-row/g))
        .toHaveLength(4);
    expect(workspaceCard.match(/dao-agent-management-loading-row/g))
        .toHaveLength(3);
    expect(usageCard.match(/dao-agent-management-loading-row/g))
        .toHaveLength(7);
    expect(htmlPatch).toMatch(
        /\.dao-agent-management-state \{[\s\S]*?min-height: 62px;/);
    expect(htmlPatch).toMatch(
        /@media \(max-width: 620px\)[\s\S]*?\.dao-agent-management-state\.error \{[\s\S]*?flex-direction: column;/);

    expect(workspaceCard).toMatch(
        /aria-label\$="\[\[formatActivityLabel_\(item\.operation, item\.path\)\]\]"/);
    expect(workspaceCard).toContain('datetime$="[[item.timestamp]]"');
    expect(workspaceCard).not.toContain('[[item.operation]] · [[item.path]]');

    expect(htmlPatch).toContain('<dl class="dao-agent-management-metrics">');
    expect(htmlPatch).toContain('<ol class="dao-agent-activity-list">');
    expect(htmlPatch).toContain('<ul class="dao-agent-tool-usage-list">');
    expect(htmlPatch).toContain('aria-live="polite"');
    expect(htmlPatch).toContain('@media (max-width: 620px)');
    expect(htmlPatch).toContain(':focus-visible');
    expect(tsPatch).toContain("cr_dialog/cr_dialog.js");
    const webUiTestPatch = readDaoSource(
        'src/patches/chrome/test/data/webui/settings/dao_page_test.ts.patch');
    expect(webUiTestPatch).toContain(
        'clearSuccessRefreshFailureDoesNotSuggestClearingAgain');
    expect(webUiTestPatch).toContain(
        'resetSuccessRefreshFailureDoesNotSuggestResettingAgain');
    expect(webUiTestPatch).toContain(
        'clearFailureUsesActionSpecificFeedback');
    expect(webUiTestPatch).toContain(
        'resetFailureUsesActionSpecificFeedback');
  });
});
