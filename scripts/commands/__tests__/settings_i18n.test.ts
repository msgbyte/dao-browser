import path from 'node:path';
import {existsSync, readFileSync} from 'node:fs';

import {describe, expect, it} from 'vitest';

const daoSettingsTranslations = [
  {
    id: '4599588625161804776',
    translation: 'Dao 浏览器',
  },
  {
    id: '5655808454255500256',
    translation: '基于 Chromium',
  },
  {
    id: '441389943317387777',
    translation: 'Dao 专属',
  },
  {
    id: '8413644221083874626',
    translation: '单项工具权限',
  },
  {
    id: '4833302064619809816',
    translation: '您与 Dao',
  },
  {
    id: '8911605311910049446',
    translation: 'MCP 服务器',
  },
  {
    id: '2106478259385421742',
    translation: '启用 MCP 服务器',
  },
  {
    id: '9186595045539973103',
    translation: '允许外部 MCP 客户端请求控制 Dao 浏览器',
  },
  {
    id: '2220066681252264193',
    translation: '等待批准',
  },
  {
    id: '6707751271223344615',
    translation:
        '<ph name="CLIENT_NAME" />（PID <ph name="CLIENT_PID" />）',
  },
  {
    id: '7605815648537354367',
    translation: '复制 MCP 配置',
  },
  {
    id: '2600196300617410059',
    translation: '停止控制',
  },
  {
    id: '532636291227772742',
    translation: '快速接入',
  },
  {
    id: '6649293836182248224',
    translation: '选择一个选项，然后复制并使用下方显示的命令或配置。',
  },
  {
    id: '6304318647555713317',
    translation: '客户端',
  },
  {
    id: '2073751931036911603',
    translation: 'Codex CLI',
  },
  {
    id: '3730858284451874727',
    translation: 'Claude Code CLI',
  },
  {
    id: '1155720053110256197',
    translation: '通用 MCP',
  },
  {
    id: '6837890588055266999',
    translation: '复制安装命令',
  },
  {
    id: '8552183253311757171',
    translation: '安装命令已复制',
  },
  {
    id: '4555594373285194249',
    translation: 'MCP 配置已复制',
  },
  {
    id: '7260333196265292710',
    translation: '启用 Little Dao',
  },
  {
    id: '2977203776743842399',
    translation: '在紧凑的 Little Dao 窗口中打开来自其他应用的链接',
  },
  {
    id: '9088288809489009615',
    translation: '增强命令栏建议',
  },
  {
    id: '3815248333120566849',
    translation: '在命令栏中使用更丰富的标签页、命令、搜索和 Dao 建议',
  },
  {
    id: '8653714013042599949',
    translation: 'Stale 标签过期时间',
  },
  {
    id: '1360400029259294658',
    translation: '将超过此时间未活跃的标签移入 stale 文件夹',
  },
  {
    id: '7673697353781729403',
    translation: '小时',
  },
  {
    id: '1683171364685569077',
    translation: '请输入 1 到 720 之间的整数',
  },
  {
    id: '42079359526797400',
    translation: '增强的画中画 (PIP)',
  },
  {
    id: '5069578397898592708',
    translation: '在部分网站使用定制 Document Picture-in-Picture 窗口',
  },
  {
    id: '5091799886440513284',
    translation: '画中画预览',
  },
  {
    id: '8813598646318262401',
    translation: 'Great scene!',
  },
  {
    id: '7352912906049883152',
    translation: 'Nice shot',
  },
  {
    id: '2837712785763321439',
    translation: 'So smooth',
  },
  {
    id: '1765568289540142383',
    translation: 'Love this',
  },
  {
    id: '2841236032449409747',
    translation: 'Nice sync',
  },
  {
    id: '1928676582356902292',
    translation: 'Perfect timing',
  },
  {
    id: '1607811381030986058',
    translation: '增强模式',
  },
  {
    id: '666789743951309643',
    translation: '部分网站会以完整播放器窗口打开，保留控制条、字幕、弹幕和 Dao 样式。',
  },
  {
    id: '6726927422842854671',
    translation: '原版模式',
  },
  {
    id: '9038265565356311756',
    translation: '支持的网站使用浏览器原版画中画窗口，只显示视频画面。',
  },
];

const daoAgentManagementTranslations = [
  ['DAO_AGENT_MANAGEMENT_TITLE', 'daoAgentManagementTitle', '数据与管理'],
  ['DAO_AGENT_MANAGEMENT_MEMORY_TITLE', 'daoAgentManagementMemoryTitle', '记忆'],
  ['DAO_AGENT_MANAGEMENT_WORKSPACE_TITLE', 'daoAgentManagementWorkspaceTitle', '工作区'],
  ['DAO_AGENT_MANAGEMENT_USAGE_TITLE', 'daoAgentManagementUsageTitle', '用量'],
  ['DAO_AGENT_MANAGEMENT_CONVERSATIONS', 'daoAgentManagementConversations', '对话'],
  ['DAO_AGENT_MANAGEMENT_PREFERENCES', 'daoAgentManagementPreferences', '偏好'],
  ['DAO_AGENT_MANAGEMENT_EPISODES', 'daoAgentManagementEpisodes', '事件'],
  ['DAO_AGENT_MANAGEMENT_TOTAL_SIZE', 'daoAgentManagementTotalSize', '总大小'],
  ['DAO_AGENT_MANAGEMENT_ROOT', 'daoAgentManagementRoot', '根目录'],
  ['DAO_AGENT_MANAGEMENT_STORAGE', 'daoAgentManagementStorage', '存储空间'],
  ['DAO_AGENT_MANAGEMENT_FILES', 'daoAgentManagementFiles', '文件'],
  ['DAO_AGENT_MANAGEMENT_RECENT_ACTIVITY', 'daoAgentManagementRecentActivity', '近期活动'],
  ['DAO_AGENT_MANAGEMENT_API_CALLS', 'daoAgentManagementApiCalls', 'API 调用'],
  ['DAO_AGENT_MANAGEMENT_TOOL_CALLS', 'daoAgentManagementToolCalls', '工具调用'],
  ['DAO_AGENT_MANAGEMENT_PROMPT_TOKENS', 'daoAgentManagementPromptTokens', '提示词元'],
  ['DAO_AGENT_MANAGEMENT_COMPLETION_TOKENS', 'daoAgentManagementCompletionTokens', '补全词元'],
  ['DAO_AGENT_MANAGEMENT_TOTAL_TOKENS', 'daoAgentManagementTotalTokens', '总词元'],
  ['DAO_AGENT_MANAGEMENT_ESTIMATED_COST', 'daoAgentManagementEstimatedCost', '预估费用'],
  ['DAO_AGENT_MANAGEMENT_LAST_RESET', 'daoAgentManagementLastReset', '上次重置'],
  ['DAO_AGENT_MANAGEMENT_LOADING', 'daoAgentManagementLoading', '正在加载…'],
  ['DAO_AGENT_MANAGEMENT_MEMORY_ERROR', 'daoAgentManagementMemoryError', '无法加载或清除记忆。请重试刷新，再次清除记忆。'],
  ['DAO_AGENT_MANAGEMENT_WORKSPACE_ERROR', 'daoAgentManagementWorkspaceError', '无法加载或打开工作区。请重试刷新，再次打开工作区。'],
  ['DAO_AGENT_MANAGEMENT_USAGE_ERROR', 'daoAgentManagementUsageError', '无法加载或重置用量。请重试刷新，再次重置用量。'],
  ['DAO_AGENT_MANAGEMENT_RETRY', 'daoAgentManagementRetry', '重试'],
  ['DAO_AGENT_MANAGEMENT_CLEAR_MEMORY', 'daoAgentManagementClearMemory', '清除记忆'],
  ['DAO_AGENT_MANAGEMENT_OPEN_WORKSPACE', 'daoAgentManagementOpenWorkspace', '打开工作区'],
  ['DAO_AGENT_MANAGEMENT_RESET_USAGE', 'daoAgentManagementResetUsage', '重置用量'],
  ['DAO_AGENT_MANAGEMENT_CLEAR_MEMORY_DIALOG_TITLE', 'daoAgentManagementClearMemoryDialogTitle', '清除全部记忆？'],
  ['DAO_AGENT_MANAGEMENT_CLEAR_MEMORY_DIALOG_DESCRIPTION', 'daoAgentManagementClearMemoryDialogDescription', '这会永久删除所有对话记忆、偏好和事件，且无法撤销。'],
  ['DAO_AGENT_MANAGEMENT_CLEAR_MEMORY_CANCEL', 'daoAgentManagementClearMemoryCancel', '取消'],
  ['DAO_AGENT_MANAGEMENT_CLEAR_MEMORY_CONFIRM', 'daoAgentManagementClearMemoryConfirm', '清除记忆'],
  ['DAO_AGENT_MANAGEMENT_RESET_USAGE_DIALOG_TITLE', 'daoAgentManagementResetUsageDialogTitle', '重置用量统计？'],
  ['DAO_AGENT_MANAGEMENT_RESET_USAGE_DIALOG_DESCRIPTION', 'daoAgentManagementResetUsageDialogDescription', '这会将所有 API、工具和词元计数清零。'],
  ['DAO_AGENT_MANAGEMENT_RESET_USAGE_CANCEL', 'daoAgentManagementResetUsageCancel', '取消'],
  ['DAO_AGENT_MANAGEMENT_RESET_USAGE_CONFIRM', 'daoAgentManagementResetUsageConfirm', '重置用量'],
  ['DAO_AGENT_MANAGEMENT_NO_RECENT_ACTIVITY', 'daoAgentManagementNoRecentActivity', '暂无近期活动'],
  ['DAO_AGENT_MANAGEMENT_NO_TOOL_CALLS', 'daoAgentManagementNoToolCalls', '暂无工具调用'],
  ['DAO_AGENT_MANAGEMENT_MEMORY_CLEARED', 'daoAgentManagementMemoryCleared', '记忆已清除'],
  ['DAO_AGENT_MANAGEMENT_WORKSPACE_OPENED', 'daoAgentManagementWorkspaceOpened', '工作区已打开'],
  ['DAO_AGENT_MANAGEMENT_USAGE_RESET', 'daoAgentManagementUsageReset', '用量已重置'],
] as const;

describe('settings i18n patches', () => {
  it('localizes every Agent management state through the settings provider', () => {
    const grdpPatch = readFileSync(path.join(
        process.cwd(), 'src/patches/chrome/app/settings_strings.grdp.patch'),
    'utf-8');
    const providerPatch = readFileSync(path.join(
        process.cwd(),
        'src/patches/chrome/browser/ui/webui/settings/' +
            'settings_localized_strings_provider.cc.patch'), 'utf-8');
    const zhCnPatch = readFileSync(path.join(
        process.cwd(),
        'src/patches/chrome/app/resources/' +
            'generated_resources_zh-CN.xtb.patch'), 'utf-8');

    for (const [resourceSuffix, providerKey, translation] of
         daoAgentManagementTranslations) {
      const resourceName = `IDS_SETTINGS_${resourceSuffix}`;
      expect(grdpPatch, resourceName)
          .toContain(`<message name="${resourceName}"`);
      expect(providerPatch, providerKey)
          .toContain(`{"${providerKey}", ${resourceName}}`);
      expect(zhCnPatch, providerKey).toMatch(
          new RegExp(`<translation id="\\d+">${translation}</translation>`));
    }
  });

  it('does not repeat translation IDs inside the zh-CN patch', () => {
    const patch = readFileSync(path.join(
        process.cwd(),
        'src/patches/chrome/app/resources/generated_resources_zh-CN.xtb.patch'),
    'utf-8');
    const ids = [...patch.matchAll(/translation id="(\d+)"/g)]
                    .map(match => match[1]);
    expect(new Set(ids).size).toBe(ids.length);
  });

  it('keeps Agent search options aligned with the runtime override type', () => {
    const pagePatch = readFileSync(path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch'), 'utf-8');
    const typeSource = readFileSync(path.join(
        process.cwd(),
        'src/dao/browser/ui/webui/resources/agent/web_search/types.ts'),
    'utf-8');
    const options = [...pagePatch.matchAll(
        /<option value="(auto|provider|duckduckgo)">/g)]
                        .map(match => match[1]);
    const override = typeSource.match(
        /SearchSourceOverride =\s*([^;]+);/)?.[1] ?? '';

    expect(options).toEqual(['auto', 'provider', 'duckduckgo']);
    for (const option of options) {
      expect(override).toContain(`'${option}'`);
    }
    expect(pagePatch).not.toContain('<option value="jina">');
    expect(pagePatch).not.toContain('<option value="browser">');
  });

  it('keeps proactive thresholds aligned with the Agent runtime', () => {
    const pagePatch = readFileSync(path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch'), 'utf-8');
    const bridgeSource = readFileSync(path.join(
        process.cwd(),
        'src/dao/browser/ui/webui/resources/agent/agent_bridge.ts'),
    'utf-8');
    const options = [...pagePatch.matchAll(
        /<option value="(quiet|balanced|active|conservative|proactive)">/g)]
                        .map(match => match[1]);

    expect(options).toEqual(['quiet', 'balanced', 'active']);
    for (const option of options) {
      expect(bridgeSource).toContain(`'${option}':`);
    }
  });

  it('provides Simplified Chinese translations for Dao settings strings', () => {
    const patchPaths = [
      'src/patches/chrome/app/resources/generated_resources_zh-CN.xtb.patch',
    ].map(relativePath => path.join(process.cwd(), relativePath));

    for (const patchPath of patchPaths) {
      expect(existsSync(patchPath)).toBe(true);
    }

    const patch =
        patchPaths.map(patchPath => readFileSync(patchPath, 'utf-8')).join('\n');
    for (const entry of daoSettingsTranslations) {
      expect(patch).toContain(
          `+<translation id="${entry.id}">${entry.translation}</translation>`);
    }
  });

  it('provides Simplified Chinese configurable stale archive feedback', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/dao/browser/strings/translations/dao_strings_zh-CN.xtb');

    expect(existsSync(patchPath)).toBe(true);

    const translation = readFileSync(patchPath, 'utf-8');
    expect(translation).toContain(
        '<translation id="9144412711146411188">' +
        '已归档不活跃的标签</translation>');
  });

  it('renders one enhanced PIP preview based on the selected pref value', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    expect(patch).toContain(
        'if="[[prefs.dao.enhanced_pip_enabled.value]]"');
    expect(patch).toContain(
        'if="[[!prefs.dao.enhanced_pip_enabled.value]]"');
    expect(patch).toContain('id="enhancedPipPreviewSubtitles"');
    expect(patch).toContain('id="enhancedPipPreviewOriginalWindow"');
    expect(patch).toContain('id="enhancedPipPreviewOriginalVideo"');
    expect(patch).not.toContain('dao-pip-preview-grid');
    expect(patch).not.toContain('bilibili');
  });

  it('keeps the MCP master switch outside profile prefs', () => {
    const pagePatchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.ts.patch');
    const handlerPath = path.join(
        process.cwd(),
        'src/dao/browser/mcp/dao_mcp_settings_handler.cc');

    const pagePatch = readFileSync(pagePatchPath, 'utf-8');
    const handler = readFileSync(handlerPath, 'utf-8');
    expect(pagePatch).toContain("key: 'dao.mcp_server_enabled'");
    expect(pagePatch).not.toContain('prefs.dao.mcp_server_enabled');
    expect(handler).toContain('prefs::kDaoMcpServerEnabled');
    expect(handler).toContain('DaoMcpService::Get()->SetEnabled(enabled)');
  });

  it('hides MCP client details outside an active connection', () => {
    const pagePatchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.ts.patch');
    const handlerPath = path.join(
        process.cwd(),
        'src/dao/browser/mcp/dao_mcp_settings_handler.cc');

    const pagePatch = readFileSync(pagePatchPath, 'utf-8');
    const handler = readFileSync(handlerPath, 'utf-8');
    expect(pagePatch).toContain("status.state === 'connected'");
    expect(handler).toContain(
        'status.state == DaoMcpStatus::kLeaseActive');
    expect(handler).toContain('result.Set("canStop", can_stop);');
  });

  it('localizes and wires the enabled-only MCP quick setup command preview', () => {
    const readPatches = (relativePaths: string[]) =>
        relativePaths
            .map(relativePath => readFileSync(
                     path.join(process.cwd(), relativePath), 'utf-8'))
            .join('\n');
    const stringsPatch = readPatches([
      'src/patches/chrome/app/settings_strings.grdp.patch',
    ]);
    const providerPatch = readPatches([
      'src/patches/chrome/browser/ui/webui/settings/' +
          'settings_localized_strings_provider.cc.patch',
    ]);
    const pageHtmlPatch = readPatches([
      'src/patches/chrome/browser/resources/settings/dao_page/' +
          'dao_page.html.patch',
    ]);
    const pageTsPatch = readPatches([
      'src/patches/chrome/browser/resources/settings/dao_page/' +
          'dao_page.ts.patch',
    ]);

    for (const messageName of [
      'IDS_SETTINGS_DAO_MCP_QUICK_SETUP_TITLE',
      'IDS_SETTINGS_DAO_MCP_QUICK_SETUP_DESCRIPTION',
      'IDS_SETTINGS_DAO_MCP_CLIENT_LABEL',
      'IDS_SETTINGS_DAO_MCP_CLIENT_CODEX',
      'IDS_SETTINGS_DAO_MCP_CLIENT_CLAUDE_CODE',
      'IDS_SETTINGS_DAO_MCP_CLIENT_GENERIC',
      'IDS_SETTINGS_DAO_MCP_COPY_INSTALL_COMMAND',
      'IDS_SETTINGS_DAO_MCP_INSTALL_COMMAND_COPIED',
      'IDS_SETTINGS_DAO_MCP_CONFIGURATION_COPIED',
    ]) {
      expect(stringsPatch).toContain(messageName);
    }

    for (const key of [
      'daoMcpQuickSetupTitle',
      'daoMcpQuickSetupDescription',
      'daoMcpClientLabel',
      'daoMcpClientCodex',
      'daoMcpClientClaudeCode',
      'daoMcpClientGeneric',
      'daoMcpCopyInstallCommand',
      'daoMcpInstallCommandCopied',
      'daoMcpCopyConfiguration',
      'daoMcpConfigurationCopied',
    ]) {
      expect(providerPatch).toContain(`{"${key}",`);
      expect(pageHtmlPatch).toContain(`$i18n{${key}}`);
    }

    expect(pageHtmlPatch).toContain('if="[[showDaoMcpQuickSetup_]]"');
    expect(pageHtmlPatch).toContain('id="daoMcpConnectionSection"');
    expect(pageHtmlPatch).toContain('id="daoMcpSetupControls"');
    expect(pageHtmlPatch).toContain('id="daoMcpSetupPreview"');
    expect(pageHtmlPatch).toContain('white-space: pre;');
    expect(pageHtmlPatch).toContain('option value="generic-mcp"');
    expect(pageTsPatch).toContain(
        "export type DaoMcpSetupOption = 'codex'|'claude-code'|'generic-mcp';");
    expect(pageTsPatch).toContain(
        'getDaoMcpSetupContent(option: DaoMcpSetupOption)');
    expect(pageTsPatch).toContain(
        'copyDaoMcpSetupContent(option: DaoMcpSetupOption)');
    expect(pageHtmlPatch).not.toContain('id="daoMcpCopyConfig"');
  });

  it('keeps the enhanced PIP preview window at a fixed 16:9 ratio', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    expect(patch).toContain('aspect-ratio: 16 / 9;');
    expect(patch).toContain('height: auto;');
    expect(patch).toContain('min-height: 160px;');
    expect(patch).not.toContain('height: 96px;');
  });

  it('makes the original PIP preview fill the shared 16:9 window', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    expect(patch).toContain('.dao-pip-preview-original-window');
    expect(patch).toContain('height: 100%;');
    expect(patch).toContain('width: 100%;');
    expect(patch).not.toContain('width: 58%;');
  });

  it('renders six enhanced PIP danmaku as tiny localized English text', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/browser/resources/settings/dao_page/' +
            'dao_page.html.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    const danmakuKeys = [
      'enhancedPipPreviewCommentPrimary',
      'enhancedPipPreviewCommentSecondary',
      'enhancedPipPreviewCommentTertiary',
      'enhancedPipPreviewCommentQuaternary',
      'enhancedPipPreviewCommentQuinary',
      'enhancedPipPreviewCommentSenary',
    ];
    for (const key of danmakuKeys) {
      expect(patch).toContain(key);
    }
    expect(patch).toContain('font-size: 8px;');
    expect(patch).toContain('line-height: 12px;');
    expect(patch).toContain('white-space: nowrap;');
    expect(patch).not.toContain(
        '<div class="dao-pip-preview-comment"></div>');
    expect(patch).not.toContain(
        '<div class="dao-pip-preview-comment secondary"></div>');
  });

  it('adds a WebUI test for switching the single enhanced PIP preview', () => {
    const patchPath = path.join(
        process.cwd(),
        'src/patches/chrome/test/data/webui/settings/dao_page_test.ts.patch');

    expect(existsSync(patchPath)).toBe(true);

    const patch = readFileSync(patchPath, 'utf-8');
    expect(patch).toContain('enhancedPipPreviewShowsSelectedModeOnly');
    expect(patch).toContain(
        "page.set('prefs.dao.enhanced_pip_enabled.value', false);");
    expect(patch).toContain('assertTrue(!preview.querySelector(' +
        "'#enhancedPipPreviewOff'));");
    expect(patch).toContain('assertTrue(!preview.querySelector(' +
        "'#enhancedPipPreviewOn'));");
  });
});
