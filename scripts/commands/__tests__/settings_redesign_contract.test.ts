import { execFileSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import path from "node:path";

import { describe, expect, it } from "vitest";

const patchRoot = "src/patches/chrome/browser/resources/settings";

const AGENT_PAGE_PATCHES = [
  "dao_page/dao_page.ts.patch",
  "dao_page/dao_page.html.patch",
  "dao_page/dao_agent_page.ts.patch",
  "dao_page/dao_agent_page.html.patch",
] as const;

const TASK5_AGENT_MESSAGES = [
  [
    "daoAgentGroupModelAndConnection",
    "IDS_SETTINGS_DAO_AGENT_GROUP_MODEL_AND_CONNECTION",
    "Model and connection",
    "2962101795780393124",
  ],
  [
    "daoAgentGroupBehaviorAndContext",
    "IDS_SETTINGS_DAO_AGENT_GROUP_BEHAVIOR_AND_CONTEXT",
    "Behavior and context",
    "1595006407868785606",
  ],
  [
    "daoAgentGroupCapabilities",
    "IDS_SETTINGS_DAO_AGENT_GROUP_CAPABILITIES",
    "Capabilities",
    "458359153530663938",
  ],
  [
    "daoAgentGroupLearningAndAnalysis",
    "IDS_SETTINGS_DAO_AGENT_GROUP_LEARNING_AND_ANALYSIS",
    "Learning and analysis",
    "5748220550726968773",
  ],
  [
    "daoAgentGroupDataAndManagement",
    "IDS_SETTINGS_DAO_AGENT_GROUP_DATA_AND_MANAGEMENT",
    "Data and management",
    "5388338688303617354",
  ],
] as const;

const AGENT_SETTING_OWNERSHIP_MARKERS = [
  ["dao_agent_providers", /setAgentSetting_\(\s*'dao_agent_providers'/g],
  [
    "dao_agent_active_provider",
    /setAgentSetting_\(\s*'dao_agent_active_provider'/g,
  ],
  ["dao_agent_soul", /data-setting="dao_agent_soul"/g],
  ["dao_disabled_tools", /setSetting\(\s*'dao_disabled_tools', value\)/g],
  ["dao_tool_call_show_details", /data-setting="dao_tool_call_show_details"/g],
  ["dao_agent_debug_mode", /data-setting="dao_agent_debug_mode"/g],
  ["dao_resume_last_session", /data-setting="dao_resume_last_session"/g],
  ["dao_resume_stale_hours", /data-setting="dao_resume_stale_hours"/g],
  ["dao_proactive_enabled", /data-setting="dao_proactive_enabled"/g],
  ["dao_page_context_enabled", /data-setting="dao_page_context_enabled"/g],
  ["dao_conversation_enabled", /data-setting="dao_conversation_enabled"/g],
  ["dao_agent_memory_enabled", /data-setting="dao_agent_memory_enabled"/g],
  ["dao_dream_enabled", /data-setting="dao_dream_enabled"/g],
  ["dao_dream_debug", /data-setting="dao_dream_debug"/g],
  ["dao_proactive_threshold", /data-setting="dao_proactive_threshold"/g],
  ["dao_search_source", /data-setting="dao_search_source"/g],
  ["dao_jina_api_key", /data-setting="dao_jina_api_key"/g],
  [
    "dao_dream_excluded_domains",
    /setAgentSetting_\(\s*'dao_dream_excluded_domains'/g,
  ],
] as const;

const AGENT_CONTROL_AND_ACTION_IDS = [
  "daoAgentProvider",
  "daoAgentModel",
  "daoAgentApiKey",
  "daoAgentBaseUrl",
  "daoAgentResumeHours",
  "daoAgentSoul",
  "daoAgentSearchSource",
  "daoAgentJinaApiKey",
  "daoAgentThreshold",
  "daoAgentDreamExcludedDomains",
  "resetAgentSoulButton",
  "runDreamNowButton",
  "openDreamHistoryLink",
  "agentSettingsRetry",
  "agentMemoryRetry",
  "agentMemoryRefreshRetry",
  "agentWorkspaceRetry",
  "agentUsageRetry",
  "agentUsageRefreshRetry",
  "clearAllMemoryButton",
  "openAgentWorkspaceButton",
  "resetUsageStatsButton",
  "clearAllMemoryDialog",
  "clearAllMemoryCancel",
  "clearAllMemoryConfirm",
  "resetUsageStatsDialog",
  "resetUsageStatsCancel",
  "resetUsageStatsConfirm",
] as const;

const AGENT_TOOL_GROUP_NAMES = [
  "page",
  "tabs",
  "devtools",
  "memory",
  "web",
  "workspace",
] as const;

const AGENT_CRITICAL_HANDLERS = [
  "loadAgentSettings_",
  "updateAgentSettings_",
  "queueDisabledToolsWrite_",
  "setAgentSetting_",
  "onRetryAgentSettings_",
  "onAgentBooleanSettingChange_",
  "onAgentTextSettingChange_",
  "onAgentProviderChange_",
  "onAgentProviderFieldChange_",
  "onAgentDreamDomainsChange_",
  "onResetAgentSoul_",
  "onRunDreamNow_",
  "isDreamRunDisabled_",
  "onAgentToolGroupChange_",
  "isAgentToolEnabled_",
  "onAgentToolChange_",
  "loadMemorySummary_",
  "loadWorkspaceSummary_",
  "loadUsageStats_",
  "updateUsageStats_",
  "onRetryMemory_",
  "onRetryWorkspace_",
  "onRetryUsage_",
  "onClearMemory_",
  "onClearMemoryDialogClose_",
  "onCancelClearMemory_",
  "onConfirmClearMemory_",
  "onOpenWorkspace_",
  "onResetUsage_",
  "onResetUsageDialogClose_",
  "onCancelResetUsage_",
  "onConfirmResetUsage_",
] as const;

function patchPath(relativePath: string): string {
  return path.join(process.cwd(), patchRoot, relativePath);
}

function readPatch(relativePath: string): string {
  return readFileSync(patchPath(relativePath), "utf-8");
}

function readDaoSource(relativePath: string): string {
  return readFileSync(path.join(process.cwd(), relativePath), "utf-8");
}

function addedPayload(patch: string): string {
  return patch
    .split("\n")
    .filter((line) => line.startsWith("+") && !line.startsWith("+++"))
    .map((line) => line.substring(1))
    .join("\n");
}

function occurrenceCount(source: string, pattern: RegExp | string): number {
  if (typeof pattern === "string") {
    return source.split(pattern).length - 1;
  }
  return source.match(pattern)?.length || 0;
}

function extractBetween(
  source: string,
  startToken: string,
  endToken: string,
): string {
  const start = source.indexOf(startToken);
  const end = source.indexOf(endToken, start + startToken.length);
  expect(start, startToken).toBeGreaterThanOrEqual(0);
  expect(end, endToken).toBeGreaterThan(start);
  return source.slice(start, end);
}

function deletionLines(patch: string): string[] {
  return patch
    .split("\n")
    .filter((line) => line.startsWith("-") && !line.startsWith("---"));
}

function preservedContractToken(line: string): string | null {
  return (
    line.match(/href="[^"]+"/)?.[0] ??
    line.match(/routes_\.[A-Z0-9_]+/)?.[0] ??
    line.match(/pageVisibility_\.[A-Za-z0-9_]+/)?.[0] ??
    line.match(/prefs="?[{[]?{?prefs/)?.[0] ??
    line.match(/<settings-[a-z0-9-]+/)?.[0] ??
    null
  );
}

describe("settings continuous overview contract", () => {
  it("provides an integrated table of contents and overview controller", () => {
    const requiredPatches = [
      "settings_ui/settings_ui.html.patch",
      "settings_ui/settings_ui.ts.patch",
      "settings_menu/settings_menu.html.patch",
      "settings_menu/settings_menu.ts.patch",
      "settings_main/settings_main.ts.patch",
      "settings_page/settings_section.html.patch",
      "settings_shared.css.patch",
    ];

    for (const relativePath of requiredPatches) {
      expect(existsSync(patchPath(relativePath)), relativePath).toBe(true);
    }

    const uiPatch = readPatch("settings_ui/settings_ui.html.patch");
    const menuPatch = readPatch("settings_menu/settings_menu.html.patch");
    const menuTsPatch = readPatch("settings_menu/settings_menu.ts.patch");
    const mainTsPatch = readPatch("settings_main/settings_main.ts.patch");
    const uiTsPatch = readPatch("settings_ui/settings_ui.ts.patch");
    const sharedCssPatch = readPatch("settings_shared.css.patch");
    expect(uiPatch).toContain("--settings-menu-width: 184px");
    expect(uiPatch).toContain('id="searchField"');
    expect(uiPatch).not.toContain('id="daoSettingsBrand"');
    expect(uiPatch).not.toContain('id="daoSettingsRailFooter"');
    expect(menuPatch).toContain('data-section="dao"');
    expect(menuPatch).not.toContain("dao-settings-menu-group");
    expect(menuTsPatch).toContain("settings-section-activate");
    expect(mainTsPatch).toContain("overviewMode_");
    expect(mainTsPatch).toContain("switchViews(");
    expect(uiTsPatch).toContain(
      "const requestedSection = window.location.hash.substring(1)",
    );
    expect(uiTsPatch).toContain(
      "this.syncOverviewRoute_(route, requestedSection)",
    );
    expect(sharedCssPatch).toContain("--dao-settings-content-width: 680px");
  });

  it("keeps the real Polymer search key aligned across the shell", () => {
    const htmlPatch = readPatch("settings_ui/settings_ui.html.patch");
    const tsPatch = readPatch("settings_ui/settings_ui.ts.patch");
    const testPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/settings_ui_test.ts.patch",
    );

    expect(htmlPatch).toContain('id="searchField"');
    expect(htmlPatch).not.toContain('id="daoSettingsSearch"');
    expect(tsPatch).toContain("searchField: CrToolbarSearchFieldElement");
    expect(testPatch).toContain("querySelector('#searchField')");
  });

  it("coalesces expensive overview searches while keeping clear immediate", () => {
    const uiPatch = readPatch("settings_ui/settings_ui.ts.patch");
    const testPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/" +
        "settings_ui_test.ts.patch",
    );

    expect(uiPatch).toContain("SEARCH_DEBOUNCE_MS");
    expect(uiPatch).toContain("clearTimeout(this.searchDebounceTimer_)");
    expect(uiPatch).toContain("this.commitSearch_(query)");
    expect(testPatch).toContain("coalescesRapidSearchChanges");
    expect(testPatch).toContain("clearingSearchIsImmediate");
  });

  it("shows menu focus only for keyboard-visible focus", () => {
    const menuPatch = readPatch("settings_menu/settings_menu.html.patch");

    expect(menuPatch).toContain(".cr-nav-menu-item:focus:not(:focus-visible)");
    expect(menuPatch).toContain("outline: none");
  });

  it("connects overview search matches to menu filtering and scroll restore", () => {
    const mainPatch = readPatch("settings_main/settings_main.ts.patch");
    const menuPatch = readPatch("settings_menu/settings_menu.ts.patch");
    const uiPatch = readPatch("settings_ui/settings_ui.ts.patch");
    const mainTestPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/" +
        "settings_main_test.ts.patch",
    );
    const menuTestPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/" +
        "settings_menu_test.ts.patch",
    );

    expect(mainPatch).toContain("settings-overview-search-results-changed");
    expect(mainPatch).toContain("rememberOverviewScroll_()");
    expect(mainPatch).toContain("this.restoreOverviewScroll()");
    expect(menuPatch).toContain("setSearchResultSections(");
    expect(menuPatch).toContain("this.$.menu.selected = ''");
    expect(uiPatch).toContain("onOverviewSearchResultsChanged_");
    expect(mainTestPatch).toContain(
      "searchReportsMatchingSectionsAndRestoresCapturedScroll",
    );
    expect(menuTestPatch).toContain(
      "searchFiltersMenuAndClearsHiddenSelection",
    );
  });

  it("does not delete route, setting, preference, or visibility markup", () => {
    const behavioralDeletionPatterns = [
      /<a\b.*href=/,
      /routes_\.[A-Z0-9_]+/,
      /pageVisibility_\.[A-Za-z0-9_]+/,
      /prefs=/,
      /<settings-toggle-button\b/,
      /<cr-link-row\b/,
    ];
    const patches = [
      "settings_ui/settings_ui.html.patch",
      "settings_ui/settings_ui.ts.patch",
      "settings_menu/settings_menu.html.patch",
      "settings_main/settings_main.html.patch",
      "settings_shared.css.patch",
      "dao_page/dao_page.html.patch",
      "dao_page/dao_page.ts.patch",
    ].filter((relativePath) => existsSync(patchPath(relativePath)));

    for (const relativePath of patches) {
      const patch = readPatch(relativePath);
      const deleted = deletionLines(patch);
      const patterns =
        relativePath.includes("settings_main/") ||
        relativePath.includes("dao_page/")
          ? [...behavioralDeletionPatterns, /<settings-[a-z0-9-]+\b/]
          : behavioralDeletionPatterns;
      const additions = patch
        .split("\n")
        .filter((line) => line.startsWith("+") && !line.startsWith("+++"))
        .join("\n");
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

  it("preserves fine-grained Agent tool permissions", () => {
    const htmlPatch = readPatch("dao_page/dao_agent_page.html.patch");
    const tsPatch = readPatch("dao_page/dao_agent_page.ts.patch");

    expect(htmlPatch).toContain('data-tool-name$="[[tool]]"');
    expect(tsPatch).toContain("onAgentToolChange_");
    expect(tsPatch).toContain("Object.values(AGENT_TOOL_GROUPS).flat()");
  });

  it("preserves legacy Agent actions and one shared content inset", () => {
    const htmlPatch = addedPayload(
      readPatch("dao_page/dao_agent_page.html.patch"),
    );
    const tsPatch = addedPayload(
      readPatch("dao_page/dao_agent_page.ts.patch"),
    );
    const proxyPatch = addedPayload(
      readPatch("dao_page/dao_agent_settings_browser_proxy.ts.patch"),
    );
    const handler = readDaoSource(
      "src/dao/browser/agent/dao_agent_settings_handler.cc",
    );

    for (const id of AGENT_CONTROL_AND_ACTION_IDS) {
      expect(htmlPatch, id).toContain(`id="${id}"`);
    }
    for (const handlerName of AGENT_CRITICAL_HANDLERS) {
      expect(tsPatch, handlerName).toContain(handlerName);
    }
    expect(proxyPatch).toContain("startManualDream(): Promise<boolean>");
    expect(proxyPatch).toContain("sendWithPromise('startManualDaoDream')");
    expect(handler).toContain('"startManualDaoDream"');

    expect(htmlPatch).toContain("--dao-agent-content-inset: 18px");
    expect(htmlPatch).toContain("margin: 12px auto 0");
    expect(htmlPatch).not.toMatch(/padding:\s*0 16px/);
  });

  it("separates Agent management modules into compact inset cards", () => {
    const htmlPatch = addedPayload(
      readPatch("dao_page/dao_agent_page.html.patch"),
    );

    expect(htmlPatch).toContain("--dao-agent-management-gap: 16px");
    expect(htmlPatch).toContain("--dao-agent-management-padding: 16px");
    expect(htmlPatch).toContain("gap: var(--dao-agent-management-gap)");
    expect(htmlPatch).toContain(
      "padding: var(--dao-agent-management-padding)",
    );
    expect(htmlPatch).toMatch(
      /\.dao-agent-management-card\s*\{[\s\S]*?border: 1px solid var\(--dao-settings-border\)/,
    );
    expect(htmlPatch).toMatch(
      /\.dao-agent-management-card\s*\{[\s\S]*?border-radius: 12px/,
    );
    expect(htmlPatch).toMatch(
      /\.dao-agent-management-card\s*\{[\s\S]*?box-shadow: var\(--dao-settings-shadow\)/,
    );
    const managementCardHeader = htmlPatch.match(
      /\.dao-agent-management-card-header \{[^}]*\}/,
    )?.[0];
    expect(managementCardHeader).toBeDefined();
    expect(managementCardHeader).toContain("align-items: center;");

    const managementCardTitle = htmlPatch.match(
      /\.dao-agent-management-card-title \{[^}]*\}/,
    )?.[0];
    expect(managementCardTitle).toBeDefined();
    expect(managementCardTitle).toContain(
      "border-inline-start: 3px solid var(--dao-settings-accent);",
    );
    expect(managementCardTitle).toContain("font-size: 14px;");
    expect(managementCardTitle).toContain("font-weight: 650;");
    expect(managementCardTitle).toContain("padding-inline-start: 10px;");
    expect(occurrenceCount(
      htmlPatch,
      '<h4 class="dao-agent-management-card-title">',
    )).toBe(3);
    expect(htmlPatch).toContain("min-height: 54px");
    expect(htmlPatch).toContain(
      "background: var(--dao-agent-management-action-surface)",
    );
    expect(htmlPatch).toContain("--dao-agent-management-gap: 12px");
    expect(htmlPatch).toContain("--dao-agent-management-padding: 12px");
  });

  it("packages Agent settings in the shared Settings lazy bundle", () => {
    const lazyLoad = "lazy_load.ts.patch";

    expect(existsSync(patchPath(lazyLoad)), lazyLoad).toBe(true);
    if (!existsSync(patchPath(lazyLoad))) {
      return;
    }

    expect(addedPayload(readPatch(lazyLoad))).toContain(
      "import './dao_page/dao_agent_page.js';",
    );
  });

  it("keeps a compact Agent overview entry and routes details to a subpage", () => {
    const routePatch = addedPayload(readPatch("route.ts.patch"));
    const routerPatch = addedPayload(readPatch("router_dao.ts.patch"));
    const menuPatch = addedPayload(
      readPatch("settings_menu/settings_menu.html.patch"),
    );
    const mainPatch = addedPayload(
      readPatch("settings_main/settings_main.html.patch"),
    );
    const daoHtml = addedPayload(readPatch("dao_page/dao_page.html.patch"));
    const daoTs = addedPayload(readPatch("dao_page/dao_page.ts.patch"));
    const indexTsPath = "dao_page/dao_agent_page_index.ts.patch";
    const indexHtmlPath = "dao_page/dao_agent_page_index.html.patch";
    const hasIndexPatches =
      existsSync(patchPath(indexTsPath)) &&
      existsSync(patchPath(indexHtmlPath));
    expect(hasIndexPatches).toBe(true);
    if (!hasIndexPatches) return;
    const indexHtml = addedPayload(readPatch(indexHtmlPath));
    const agentHtml = addedPayload(
      readPatch("dao_page/dao_agent_page.html.patch"),
    );
    const agentTs = addedPayload(readPatch("dao_page/dao_agent_page.ts.patch"));

    expect(routePatch).toContain(
      "r.DAO_AGENT = r.BASIC.createSection(\n" +
        "        '/agentOverview', 'agent',\n" +
        "        loadTimeData.getString('daoAgentSettingsTitle'));",
    );
    expect(routePatch).toContain(
      "r.DAO_AGENT_DETAILS = r.DAO_AGENT.createChild('/agent');",
    );
    expect(routerPatch).toContain("DAO_AGENT_DETAILS: Route;");
    expect(routePatch).not.toContain("r.DAO.createChild('/dao/agent')");

    const daoMenuIndex = menuPatch.indexOf(
      'id="dao" href="/dao" data-section="dao"',
    );
    const agentMenuIndex = menuPatch.indexOf(
      'id="agent" href="/agent" data-section="agent"',
    );
    expect(daoMenuIndex).toBeGreaterThanOrEqual(0);
    expect(agentMenuIndex).toBeGreaterThan(daoMenuIndex);
    expect(menuPatch).toContain('<cr-icon icon="settings:assignment">');

    expect(mainPatch).toContain('<div slot="view" id="dao">');
    expect(mainPatch).toContain('<settings-dao-page prefs="{{prefs}}">');
    expect(mainPatch).toContain('<div slot="view" id="agent">');
    expect(mainPatch).toContain(
      '<settings-dao-agent-page-index prefs="{{prefs}}">',
    );
    expect(mainPatch).not.toContain(
      '<settings-dao-agent-page prefs="{{prefs}}">',
    );
    expect(mainPatch).not.toContain("<settings-dao-page-index");

    expect(indexHtml).toContain('id="parent"');
    expect(indexHtml).toContain('id="agentSettings"');
    expect(indexHtml).toContain('data-parent-view-id="parent"');

    for (const forbidden of [
      "daoAgentSummary",
      "DaoAgentSettingsBrowserProxyImpl",
      "agentOverviewSettingsReady_",
      "onConfigureDaoAgent_",
      "loadAgentOverviewSettings_",
    ]) {
      expect(daoHtml + daoTs, forbidden).not.toContain(forbidden);
    }

    expect(agentHtml).toContain("<settings-section");
    expect(agentHtml).not.toContain("<settings-subpage");
    expect(agentTs).toContain("implements SettingsPlugin");
    expect(agentTs).toContain("searchContents(query: string)");
+    expect(agentTs).toContain("getSearchManager().search(query, this)");
    expect(agentTs).not.toContain("routePath");

    for (const obsoletePatch of [
      "dao_page/dao_page_index.html.patch",
      "dao_page/dao_page_index.ts.patch",
    ]) {
      expect(existsSync(patchPath(obsoletePatch)), obsoletePatch).toBe(false);
    }

    const menuTest = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/settings_menu_test.ts.patch",
    );
    const mainTest = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/settings_main_test.ts.patch",
    );
    const agentTest = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/dao_agent_page_test.ts.patch",
    );
    expect(menuTest).toContain("tracksAgentAsSiblingDaoExclusiveSection");
    expect(mainTest).toContain(
      "#switcher > #agent settings-dao-agent-page-index",
    );
    expect(agentTest).toContain(
      "rendersFiveVerticalSettingsSections",
    );
  });

  it("uses immediate positioning for overview menu activation", () => {
    const menuTs = addedPayload(
      readPatch("settings_menu/settings_menu.ts.patch"),
    );

    expect(menuTs).toContain("behavior: 'auto',");
    expect(menuTs).not.toContain("prefers-reduced-motion");
  });

  it("keeps Dao Agent Settings localization English-first and complete", () => {
    const pageSource = AGENT_PAGE_PATCHES.map((relativePath) =>
      addedPayload(readPatch(relativePath)),
    ).join("\n");
    const agentHtml = addedPayload(
      readPatch("dao_page/dao_agent_page.html.patch"),
    );
    const grdp = addedPayload(
      readDaoSource("src/patches/chrome/app/settings_strings.grdp.patch"),
    );
    const provider = addedPayload(
      readDaoSource(
        "src/patches/chrome/browser/ui/webui/settings/" +
          "settings_localized_strings_provider.cc.patch",
      ),
    );
    const zhCn = addedPayload(
      readDaoSource(
        "src/patches/chrome/app/resources/" +
          "generated_resources_zh-CN.xtb.patch",
      ),
    );

    expect(pageSource).not.toMatch(/[\u3400-\u9fff]/u);

    const usedMessageIds = new Set(
      [...pageSource.matchAll(/\$i18n\{(daoAgent[A-Za-z0-9]+)\}/g)].map(
        (match) => match[1],
      ),
    );
    for (const messageId of usedMessageIds) {
      const providerMatch = provider.match(
        new RegExp(
          `\\{"${messageId}",\\s*(IDS_SETTINGS_DAO_AGENT_[A-Z0-9_]+)\\}`,
        ),
      );
      expect(
        providerMatch,
        `${messageId} provider registration`,
      ).not.toBeNull();
      expect(grdp, `${messageId} English source`).toContain(
        `name="${providerMatch![1]}"`,
      );
    }

    for (const [
      messageId,
      resourceId,
      english,
      translationId,
    ] of TASK5_AGENT_MESSAGES) {
      expect(pageSource, `${messageId} usage`).toContain(`$i18n{${messageId}}`);
      expect(provider, `${messageId} provider registration`).toMatch(
        new RegExp(`\\{"${messageId}",\\s*${resourceId}\\}`),
      );
      const messageBlock = grdp.match(
        new RegExp(`<message name="${resourceId}"[^>]*>([\\s\\S]*?)</message>`),
      );
      expect(messageBlock, `${resourceId} English source`).not.toBeNull();
      expect(messageBlock![1]).toContain(english);
      expect(messageBlock![1]).not.toMatch(/[\u3400-\u9fff]/u);

      const translation = zhCn.match(
        new RegExp(
          `<translation id="${translationId}">([\\s\\S]*?)</translation>`,
        ),
      );
      expect(translation, `${resourceId} zh-CN translation`).not.toBeNull();
      expect(translation![1]).toMatch(/[\u3400-\u9fff]/u);
    }

    for (const [sectionId, messageId] of [
      [
        "modelAndConnection",
        "daoAgentGroupModelAndConnection",
      ],
      [
        "behaviorAndContext",
        "daoAgentGroupBehaviorAndContext",
      ],
      ["capabilities", "daoAgentGroupCapabilities"],
      [
        "learningAndAnalysis",
        "daoAgentGroupLearningAndAnalysis",
      ],
      [
        "dataAndManagement",
        "daoAgentGroupDataAndManagement",
      ],
    ] as const) {
      expect(agentHtml, sectionId).toMatch(
        new RegExp(
          `<settings-section id="${sectionId}"[\\s\\S]*?` +
            `page-title="\\$i18n\\{${messageId}\\}"`,
        ),
      );
    }
    expect(occurrenceCount(agentHtml, "<settings-section id=")).toBe(5);
    expect(agentHtml).not.toMatch(
      /role="tablist"|section-navigation|section-rail/,
    );
  });

  it("documents the compact Agent entry and secondary page contract", () => {
    const features = readDaoSource("docs/features.md");
    const checklist = readDaoSource("docs/feature-checklist.md");

    for (const marker of [
      "`dao://settings/agent`",
      "compact top-level Dao-exclusive Settings entry",
      "five vertical Settings sections",
      "Chromium's shared Settings lazy bundle",
      "closes the sidebar only after the navigation succeeds",
    ]) {
      expect(features, marker).toContain(marker);
    }
    for (const marker of [
      "one compact Agent entry and no full Agent form",
      "navigating directly to `/agent`",
      "Back restores the overview",
      "Settings search finds the compact entry",
      "light/dark themes",
      "below 760 px",
      "keyboard",
      "reduced motion",
    ]) {
      expect(checklist, marker).toContain(marker);
    }
  });

  it("classifies the Agent proxy as TypeScript and pages as Web Components", () => {
    const buildPatch = readPatch("BUILD.gn.patch");
    const hunks = buildPatch
      .split(/\n(?=@@ )/)
      .filter((hunk) => hunk.startsWith("@@ "));
    const proxy = '"dao_page/dao_agent_settings_browser_proxy.ts"';
    const components = [
      '"dao_page/dao_agent_page.ts"',
      '"dao_page/dao_agent_page_index.ts"',
      '"dao_page/dao_page.ts"',
    ];
    const webComponentHunk = hunks.find((hunk) =>
      hunk.includes(`+    ${components[0]}`),
    );
    const tsFilesHunk = hunks.find(
      (hunk) =>
        hunk.includes(`+    ${proxy}`) &&
        !hunk.includes(`+    ${components[0]}`),
    );

    expect(webComponentHunk, "web_component_files hunk").toBeDefined();
    expect(tsFilesHunk, "ts_files hunk").toBeDefined();
    expect(occurrenceCount(addedPayload(buildPatch), proxy)).toBe(1);
    expect(occurrenceCount(addedPayload(webComponentHunk!), proxy)).toBe(0);
    expect(occurrenceCount(addedPayload(tsFilesHunk!), proxy)).toBe(1);
    for (const component of components) {
      expect(
        occurrenceCount(addedPayload(webComponentHunk!), component),
        component,
      ).toBe(1);
      expect(
        occurrenceCount(addedPayload(tsFilesHunk!), component),
        component,
      ).toBe(0);
    }
  });

  it("keeps the Agent proxy new-file hunk count aligned with its payload", () => {
    const proxyPath = patchPath(
      "dao_page/dao_agent_settings_browser_proxy.ts.patch",
    );
    const proxyPatch = readFileSync(proxyPath, "utf-8");
    const hunkHeader = proxyPatch.match(/^@@ -0,0 \+1,(\d+) @@$/m);
    const payloadCount = proxyPatch
      .split("\n")
      .filter((line) => line.startsWith("+") && !line.startsWith("+++")).length;

    expect(hunkHeader, "new-file hunk header").not.toBeNull();
    expect(payloadCount).toBe(182);
    expect(Number(hunkHeader![1]), "declared new-file line count").toBe(
      payloadCount,
    );

    const numstat = execFileSync("git", ["apply", "--numstat", proxyPath], {
      cwd: process.cwd(),
      encoding: "utf-8",
    });
    const appliedCount = Number(numstat.split("\t", 1)[0]);
    expect(appliedCount, "git apply added line count").toBe(payloadCount);
  });

  it("keeps active Dao and Agent new-file patch payload counts exact", () => {
    for (const relativePath of [
      "dao_page/dao_page.html.patch",
      "dao_page/dao_page.ts.patch",
      "dao_page/dao_agent_page.html.patch",
      "dao_page/dao_agent_page.ts.patch",
      "../../../test/data/webui/settings/dao_page_test.ts.patch",
      "../../../test/data/webui/settings/dao_agent_page_test.ts.patch",
    ]) {
      const patch = readPatch(relativePath);
      const hunkHeader = patch.match(/^@@ -0,0 \+1,(\d+) @@$/m);
      const payloadCount = patch
        .split("\n")
        .filter(
          (line) => line.startsWith("+") && !line.startsWith("+++"),
        ).length;
      expect(hunkHeader, relativePath).not.toBeNull();
      expect(Number(hunkHeader![1]), relativePath).toBe(payloadCount);
    }
  });

  it("wires each Dao page WebUI test target to its disabled browser runner", () => {
    const buildPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/BUILD.gn.patch",
    );
    const runnerPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/" +
        "settings_browsertest.cc.patch",
    );
    const buildPayload = addedPayload(buildPatch);
    const runnerPayload = addedPayload(runnerPatch);

    const testTargets = [
      ["dao_page_test", "DaoPage"],
      ["dao_agent_page_index_test", "DaoAgentPageIndex"],
      ["dao_agent_page_test", "DaoAgentPage"],
    ] as const;
    for (const [source, runner] of testTargets) {
      expect(occurrenceCount(buildPayload, `"${source}.ts"`), source).toBe(1);
      expect(
        occurrenceCount(
          runnerPayload,
          `IN_PROC_BROWSER_TEST_F(SettingsTest, DISABLED_${runner})`,
        ),
        runner,
      ).toBe(1);
      expect(
        occurrenceCount(
          runnerPayload,
          `RunTest("settings/${source}.js", "mocha.run()")`,
        ),
        source,
      ).toBe(1);
    }
    expect(
      occurrenceCount(
        runnerPayload,
        "IN_PROC_BROWSER_TEST_F(SettingsTest, DISABLED_Dao",
      ),
    ).toBe(3);
    expect(runnerPayload).not.toMatch(
      /IN_PROC_BROWSER_TEST_F\(SettingsTest, Dao(?:Agent)?Page(?:Index)?\)/,
    );
  });

  it("preserves the complete Agent settings inventory and shared proxy boundary", () => {
    const agentProxy = "dao_page/dao_agent_settings_browser_proxy.ts.patch";
    const agentTs = "dao_page/dao_agent_page.ts.patch";
    const agentHtml = "dao_page/dao_agent_page.html.patch";
    const agentTest =
      "src/patches/chrome/test/data/webui/settings/" +
      "dao_agent_page_test.ts.patch";

    expect(existsSync(patchPath(agentProxy)), agentProxy).toBe(true);
    expect(existsSync(patchPath(agentTs)), agentTs).toBe(true);
    expect(existsSync(patchPath(agentHtml)), agentHtml).toBe(true);
    expect(existsSync(path.join(process.cwd(), agentTest)), agentTest).toBe(
      true,
    );

    const overviewHtml = addedPayload(
      readPatch("dao_page/dao_page.html.patch"),
    );
    const agentHtmlPatch = addedPayload(readPatch(agentHtml));
    const overviewTs = addedPayload(readPatch("dao_page/dao_page.ts.patch"));
    const agentTsPatch = addedPayload(readPatch(agentTs));
    const overviewSource = overviewHtml + "\n" + overviewTs;
    const agentSource = agentHtmlPatch + "\n" + agentTsPatch;
    const combinedSource = overviewSource + "\n" + agentSource;

    for (const [key, pattern] of AGENT_SETTING_OWNERSHIP_MARKERS) {
      expect(
        occurrenceCount(combinedSource, pattern),
        `${key} combined owner count`,
      ).toBe(1);
      expect(
        occurrenceCount(agentSource, pattern),
        `${key} subpage owner`,
      ).toBe(1);
      expect(occurrenceCount(overviewSource, pattern), `${key} overview`).toBe(
        0,
      );
    }

    for (const id of AGENT_CONTROL_AND_ACTION_IDS) {
      const marker = `id="${id}"`;
      expect(
        occurrenceCount(combinedSource, marker),
        `${id} combined owner`,
      ).toBe(1);
      expect(occurrenceCount(agentSource, marker), `${id} subpage owner`).toBe(
        1,
      );
      expect(occurrenceCount(overviewSource, marker), `${id} overview`).toBe(0);
    }

    const proxyPayload = addedPayload(readPatch(agentProxy));
    const toolGroups = extractBetween(
      proxyPayload,
      "export const AGENT_TOOL_GROUPS",
      "export const AGENT_PROVIDER_DEFAULTS",
    );
    for (const group of AGENT_TOOL_GROUP_NAMES) {
      const controlMarker = `data-tool-group="${group}"`;
      expect(
        occurrenceCount(overviewHtml + "\n" + agentHtmlPatch, controlMarker),
        `${group} tool-group control`,
      ).toBe(1);
      expect(occurrenceCount(agentHtmlPatch, controlMarker)).toBe(1);
      expect(occurrenceCount(overviewHtml, controlMarker)).toBe(0);
      expect(
        occurrenceCount(toolGroups, new RegExp(`^\\s*${group}:`, "gm")),
      ).toBe(1);
    }
    expect(occurrenceCount(combinedSource, 'data-tool-name$="[[tool]]"')).toBe(
      1,
    );
    expect(occurrenceCount(agentSource, 'data-tool-name$="[[tool]]"')).toBe(1);
    expect(overviewSource).not.toContain("data-tool-name");

    for (const handler of AGENT_CRITICAL_HANDLERS) {
      const definition = new RegExp(
        `private (?:async )?${handler}\\s*\\(`,
        "g",
      );
      expect(
        occurrenceCount(combinedSource, definition),
        `${handler} combined owner`,
      ).toBe(1);
      expect(
        occurrenceCount(agentSource, definition),
        `${handler} subpage owner`,
      ).toBe(1);
      expect(
        occurrenceCount(overviewSource, definition),
        `${handler} overview`,
      ).toBe(0);
    }

    for (const sectionId of [
      "modelAndConnection",
      "behaviorAndContext",
      "capabilities",
      "learningAndAnalysis",
      "dataAndManagement",
    ]) {
      expect(occurrenceCount(combinedSource, `id="${sectionId}"`)).toBe(1);
      expect(occurrenceCount(agentSource, `id="${sectionId}"`)).toBe(1);
      expect(occurrenceCount(overviewSource, `id="${sectionId}"`)).toBe(0);
    }
    expect(agentHtmlPatch).toContain("<settings-section");
    expect(agentHtmlPatch).not.toContain("<settings-subpage");
    expect(agentHtmlPatch).toContain("@media (max-width: 760px)");
    expect(agentHtmlPatch).toMatch(/<details class="dao-agent-tool-details">/);
    expect(agentHtmlPatch).toMatch(
      /<details class="dao-agent-management-detail"/,
    );

    const proxyPatch = readPatch(agentProxy);
    for (const token of [
      "export interface DaoAgentSettingsBrowserProxy",
      "export class DaoAgentSettingsBrowserProxyImpl",
      "export interface DaoAgentSettingsSnapshot",
      "export interface DaoAgentMemorySummary",
      "export interface DaoAgentWorkspaceSummary",
      "export interface DaoAgentUsageStats",
      "isDaoAgentMemorySummary",
      "isDaoAgentWorkspaceSummary",
      "export const AGENT_TOOL_GROUPS",
      "export const AGENT_PROVIDER_DEFAULTS",
      "getSettings(): Promise<DaoAgentSettingsSnapshot>;",
      "setSetting(key: string, value: string|null): Promise<boolean>;",
      "getMemorySummary(): Promise<unknown>;",
      "clearAllMemory(): Promise<boolean>;",
      "getWorkspaceSummary(): Promise<unknown>;",
      "openWorkspace(): Promise<boolean>;",
      "getUsageStats(): Promise<DaoAgentUsageStats>;",
      "resetUsageStats(): Promise<boolean>;",
    ]) {
      expect(proxyPatch).toContain(token);
    }
  });

  it("serializes complete optimistic disabled-tool writes", () => {
    const tsPatch = addedPayload(readPatch("dao_page/dao_agent_page.ts.patch"));
    const webUiTestPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/" +
        "dao_agent_page_test.ts.patch",
    );
    const queueMethod = extractBetween(
      tsPatch,
      "private queueDisabledToolsWrite_()",
      "private async setAgentSetting_",
    );

    expect(queueMethod).toMatch(
      /const value = this\.serializeDisabledTools_\(\);[\s\S]*?const generation = \+\+this\.disabledToolsMutationGeneration_;[\s\S]*?\+\+this\.disabledToolsWritesPending_;[\s\S]*?this\.preserveOptimisticDisabledTools_ = true;[\s\S]*?dao_disabled_tools: value/,
    );
    expect(queueMethod).toMatch(
      /this\.disabledToolsWritePromise_ =\s*this\.disabledToolsWritePromise_\.then\(async \(\) => \{[\s\S]*?setSetting\(\s*'dao_disabled_tools', value\)[\s\S]*?--this\.disabledToolsWritesPending_;[\s\S]*?if \(this\.disabledToolsWritesPending_ !== 0\) \{\s*return;/,
    );
    expect(queueMethod).toMatch(
      /await this\.agentSettingsBrowserProxy_\.getSettings\(\)[\s\S]*?generation === this\.disabledToolsMutationGeneration_[\s\S]*?this\.disabledToolsWritesPending_ === 0[\s\S]*?this\.preserveOptimisticDisabledTools_ = false;[\s\S]*?this\.updateAgentSettings_\(snapshot, true\)/,
    );
    expect(
      occurrenceCount(
        addedPayload(webUiTestPatch),
        "test('concurrentToolTogglesPersistCompleteDisabledArray'",
      ),
    ).toBe(1);
  });

  it("normalizes partial legacy usage without weakening stored snapshots", () => {
    const handler = readDaoSource(
      "src/dao/browser/agent/dao_agent_settings_handler.cc",
    );
    const handlerTest = readDaoSource(
      "src/dao/browser/agent/dao_agent_settings_handler_unittest.cc",
    );

    expect(handler).toContain("NormalizeLegacyUsageStats");
    expect(handlerTest).toContain("MigratesPartialLegacyUsageStats");
    expect(handlerTest).toContain("RejectsMalformedPartialLegacyUsageStats");
    expect(handlerTest).toContain(
      "CanonicalUsageStatsStillRequireCompleteSchema",
    );
  });

  it("restores the Agent resume window default and zero-hour minimum", () => {
    const htmlPatch = readPatch("dao_page/dao_agent_page.html.patch");
    const tsPatch = readPatch("dao_page/dao_agent_page.ts.patch");
    const webUiTestPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/" +
        "dao_agent_page_test.ts.patch",
    );

    expect(htmlPatch).toMatch(
      /id="daoAgentResumeHours"[^>]*type="number" min="0"/,
    );
    expect(tsPatch).toContain(
      "agentResumeStaleHours_: {type: String, value: '3'}",
    );
    expect(tsPatch).toContain(
      "snapshot.values['dao_resume_stale_hours'] || '3'",
    );
    expect(webUiTestPatch).toContain(
      "agentResumeWindowDefaultsToThreeAndAllowsZero",
    );
  });

  it("isolates configuration failure from the management cards", () => {
    const htmlPatch = readPatch("dao_page/dao_agent_page.html.patch");
    const tsPatch = readPatch("dao_page/dao_agent_page.ts.patch");
    const webUiTestPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/" +
        "dao_agent_page_test.ts.patch",
    );
    const managementStart = htmlPatch.indexOf(
      '<settings-section id="dataAndManagement"',
    );
    const settingsGateEnd = htmlPatch.lastIndexOf(
      "</template>",
      managementStart,
    );

    expect(tsPatch).toContain("agentSettingsError_");
    expect(tsPatch).toContain("loadAgentSettings_");
    expect(htmlPatch).toContain('id="agentSettingsRetry"');
    expect(managementStart).toBeGreaterThanOrEqual(0);
    expect(settingsGateEnd).toBeLessThan(managementStart);
    expect(webUiTestPatch).toContain(
      "configurationFailureKeepsManagementCardsUsable",
    );
  });

  it("keeps nested top-level page views in overview document flow", () => {
    const nestedPagePatches = [
      "people_page/people_page_index.html.patch",
      "autofill_page/autofill_page_index.html.patch",
      "your_saved_info_page/your_saved_info_page_index.html.patch",
      "appearance_page/appearance_page_index.html.patch",
      "a11y_page/a11y_page_index.html.patch",
    ];

    for (const relativePath of nestedPagePatches) {
      expect(existsSync(patchPath(relativePath)), relativePath).toBe(true);
      expect(readPatch(relativePath), relativePath).toContain(
        "[slot=view]:not(.closing)",
      );
      expect(readPatch(relativePath), relativePath).toContain(
        "position: initial",
      );
    }
  });

  it("keeps usage-statistic mutations inside their scoped pref updates", () => {
    const handler = readDaoSource(
      "src/dao/browser/agent/dao_agent_settings_handler.cc",
    );
    const api_usage = handler.match(
      /void RecordDaoAgentApiUsage[\s\S]*?\n}\n\nvoid RecordDaoAgentToolUsage/,
    )?.[0];
    const tool_usage = handler.match(
      /void RecordDaoAgentToolUsage[\s\S]*?\n}\n\nvoid ResetDaoAgentUsageStats/,
    )?.[0];

    expect(api_usage).toBeDefined();
    expect(tool_usage).toBeDefined();
    expect(api_usage).not.toContain("ReadUsageStatsOrDefault");
    expect(tool_usage).not.toContain("ReadUsageStatsOrDefault");
    expect(api_usage).toMatch(
      /ScopedDictPrefUpdate update[\s\S]*?update->FindDouble/,
    );
    expect(tool_usage).toMatch(
      /ScopedDictPrefUpdate update[\s\S]*?update->FindDict/,
    );
  });

  it("exposes only the restricted Agent management surface", () => {
    const handler = readDaoSource(
      "src/dao/browser/agent/dao_agent_settings_handler.cc",
    );
    for (const message of [
      "getDaoAgentMemorySummary",
      "clearAllDaoAgentMemory",
      "getDaoAgentWorkspaceSummary",
      "openDaoAgentWorkspace",
      "getDaoAgentUsageStats",
      "resetDaoAgentUsageStats",
    ]) {
      expect(handler).toContain(`"${message}"`);
    }
    for (const forbidden of [
      "workspaceRead",
      "workspaceWrite",
      "workspaceEdit",
      "workspaceApplyPatch",
      "workspaceList",
      "workspaceDownload",
    ]) {
      expect(handler).not.toContain(`"${forbidden}"`);
    }
  });

  it("renders the complete Agent management ledger without dropping links", () => {
    const htmlPatch = readPatch("dao_page/dao_agent_page.html.patch");
    const tsPatch = readPatch("dao_page/dao_agent_page.ts.patch");
    const managementSource = tsPatch + htmlPatch;

    for (const token of [
      "conversationCount",
      "preferenceCount",
      "episodeCount",
      "totalSize",
      "usedBytes",
      "capBytes",
      "fileCount",
      "recentActivity",
      "apiCalls",
      "toolCalls",
      "promptTokens",
      "completionTokens",
      "totalTokens",
      "estimatedCost",
      "clearAllMemory",
      "openWorkspace",
      "resetUsageStats",
      'href="dao://skills"',
      'href="dao://memory"',
      'href="dao://dream"',
    ]) {
      expect(managementSource).toContain(token);
    }

    for (const id of [
      "daoAgentMemoryManagement",
      "daoAgentWorkspaceManagement",
      "daoAgentUsageManagement",
      "clearAllMemoryDialog",
      "resetUsageStatsDialog",
    ]) {
      expect(htmlPatch).toContain(`id="${id}"`);
    }

    const memoryCard = extractBetween(
      htmlPatch,
      'id="daoAgentMemoryManagement"',
      'id="daoAgentWorkspaceManagement"',
    );
    const workspaceCard = extractBetween(
      htmlPatch,
      'id="daoAgentWorkspaceManagement"',
      'id="daoAgentUsageManagement"',
    );
    const usageCard = extractBetween(
      htmlPatch,
      'id="daoAgentUsageManagement"',
      'id="daoAgentClearMemoryDialog"',
    );
    const clearDialog = extractBetween(
      htmlPatch,
      'id="clearAllMemoryDialog"',
      'id="daoAgentResetUsageDialog"',
    );
    const resetDialog = extractBetween(
      htmlPatch,
      'id="resetUsageStatsDialog"',
      "</settings-section>",
    );

    expect(memoryCard).toMatch(
      /if="\[\[showSummaryLoadError_\(memoryError_, memoryActionSucceeded_\)\]\]"[\s\S]*?id="memoryLoadError"[\s\S]*?id="agentMemoryRetry"[\s\S]*?on-click="onRetryMemory_"/,
    );
    expect(memoryCard).toMatch(
      /if="\[\[showSummaryRefreshError_\(memoryError_, memoryActionSucceeded_\)\]\]"[\s\S]*?id="memoryRefreshError"[\s\S]*?daoAgentManagementMemoryRefreshError[\s\S]*?id="agentMemoryRefreshRetry"/,
    );
    expect(workspaceCard).toMatch(
      /if="\[\[workspaceError_\]\]"[\s\S]*?id="agentWorkspaceRetry"[\s\S]*?on-click="onRetryWorkspace_"/,
    );
    expect(usageCard).toMatch(
      /if="\[\[showSummaryLoadError_\(usageError_, usageActionSucceeded_\)\]\]"[\s\S]*?id="usageLoadError"[\s\S]*?id="agentUsageRetry"[\s\S]*?on-click="onRetryUsage_"/,
    );
    expect(usageCard).toMatch(
      /if="\[\[showSummaryRefreshError_\(usageError_, usageActionSucceeded_\)\]\]"[\s\S]*?id="usageRefreshError"[\s\S]*?daoAgentManagementUsageRefreshError[\s\S]*?id="agentUsageRefreshRetry"/,
    );
    expect(memoryCard).toContain('if="[[memoryActionError_]]"');
    expect(memoryCard).toContain("$i18n{daoAgentManagementMemoryRefreshError}");
    expect(workspaceCard).toContain('if="[[workspaceActionError_]]"');
    expect(usageCard).toContain('if="[[usageActionError_]]"');
    expect(usageCard).toContain("$i18n{daoAgentManagementUsageRefreshError}");

    expect(clearDialog).toMatch(
      /on-close="onClearMemoryDialogClose_"[\s\S]*?id="clearAllMemoryConfirm"[\s\S]*?disabled="\[\[clearMemoryPending_\]\]"[\s\S]*?on-click="onConfirmClearMemory_"/,
    );
    expect(resetDialog).toMatch(
      /on-close="onResetUsageDialogClose_"[\s\S]*?id="resetUsageStatsConfirm"[\s\S]*?disabled="\[\[resetUsagePending_\]\]"[\s\S]*?on-click="onConfirmResetUsage_"/,
    );

    expect(memoryCard.match(/dao-agent-management-loading-row/g)).toHaveLength(
      4,
    );
    expect(
      workspaceCard.match(/dao-agent-management-loading-row/g),
    ).toHaveLength(3);
    expect(usageCard.match(/dao-agent-management-loading-row/g)).toHaveLength(
      7,
    );
    expect(htmlPatch).toMatch(
      /\.dao-agent-management-state \{[\s\S]*?min-height: 54px;/,
    );
    expect(htmlPatch).toMatch(
      /@media \(max-width: 760px\)[\s\S]*?\.dao-agent-management-state\.error \{[\s\S]*?flex-direction: column;/,
    );

    expect(workspaceCard).toMatch(
      /aria-label\$="\[\[formatActivityLabel_\(item\.operation, item\.path\)\]\]"/,
    );
    expect(workspaceCard).toContain('datetime$="[[item.timestamp]]"');
    expect(workspaceCard).not.toContain("[[item.operation]] · [[item.path]]");

    expect(htmlPatch).toContain('<dl class="dao-agent-management-metrics">');
    expect(htmlPatch).toContain('<ol class="dao-agent-activity-list">');
    expect(htmlPatch).toContain('<ul class="dao-agent-tool-usage-list">');
    expect(htmlPatch).toContain('aria-live="polite"');
    expect(htmlPatch).toContain("@media (max-width: 760px)");
    expect(htmlPatch).toContain(":focus-visible");
    expect(tsPatch).toContain("cr_dialog/cr_dialog.js");
    const webUiTestPatch = readDaoSource(
      "src/patches/chrome/test/data/webui/settings/" +
        "dao_agent_page_test.ts.patch",
    );
    expect(webUiTestPatch).toContain(
      "clearSuccessRefreshFailureDoesNotSuggestClearingAgain",
    );
    expect(webUiTestPatch).toContain(
      "resetSuccessRefreshFailureDoesNotSuggestResettingAgain",
    );
    expect(webUiTestPatch).toContain("clearFailureUsesActionSpecificFeedback");
    expect(webUiTestPatch).toContain("resetFailureUsesActionSpecificFeedback");
  });
});
