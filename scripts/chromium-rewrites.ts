import {existsSync, readFileSync, writeFileSync} from "node:fs";
import path from "node:path";
import {globSync} from "glob";

export interface RewriteResult {
  content: string;
  replacements: number;
}

export interface ChromiumRewriteSummary {
  filesChanged: number;
  filesUnchanged: number;
  filesMissing: number;
  replacements: number;
}

const CHROME_SCHEME = "chrome://";
const DAO_SCHEME = "dao://";

const SCHEME_TEXT_REWRITE_PATHS = [
  "chrome/app/media_router_strings.grdp",
  "chrome/app/os_settings_strings.grdp",
  "chrome/app/password_manager_ui_strings.grdp",
  "chrome/app/support_tool_strings.grdp",
  "components/autofill_payments_strings.grdp",
  "components/error_page_strings.grdp",
  "components/flags_strings.grdp",
  "components/history_clusters_strings.grdp",
  "components/omnibox_pedal_ui_strings.grdp",
  "components/site_settings_strings.grdp",
  "components/version_ui_strings.grdp",
  "third_party/blink/public/common/chrome_debug_urls.h",
];

const WEBUI_BASE_HREF_PATHS = [
  "chrome/browser/resources/app_home/app_home.html",
  "chrome/browser/resources/bookmarks/bookmarks.html",
  "chrome/browser/resources/certificate_manager/certificate_manager_dialog.html",
  "chrome/browser/resources/downloads/downloads.html",
  "chrome/browser/resources/extensions/extensions.html",
  "chrome/browser/resources/history/history.html",
  "chrome/browser/resources/password_manager/password_manager.html",
  "chrome/browser/resources/print_preview/print_preview.html",
  "chrome/browser/resources/settings/settings.html",
  "chrome/browser/resources/signin/profile_picker/profile_picker.html",
];

const EXTENSION_API_FEATURE_PATHS = [
  "chrome/common/extensions/api/_api_features.json",
  "extensions/common/api/_api_features.json",
];

const EXTENSION_API_FEATURE_URLS_BY_PATH: Record<string, Set<string>> = {
  "chrome/common/extensions/api/_api_features.json": new Set([
    "chrome://*/*",
    "chrome://add-supervision/*",
    "chrome://bookmarks-side-panel.top-chrome/*",
    "chrome://bookmarks/*",
    "chrome://camera-app/*",
    "chrome://chrome-signin/*",
    "chrome://contextual-tasks/*",
    "chrome://extensions/*",
    "chrome://file-manager/*",
    "chrome://glic-fre/*",
    "chrome://glic/*",
    "chrome://graduation/*",
    "chrome://lock-reauth/*",
    "chrome://media-app/*",
    "chrome://mobilesetup/*",
    "chrome://multidevice-setup/*",
    "chrome://oobe/*",
    "chrome://os-settings/*",
    "chrome://parent-access/*",
    "chrome://password-change/*",
    "chrome://password-manager/*",
    "chrome://print/*",
    "chrome://read-later.top-chrome/*",
    "chrome://recorder-app/*",
    "chrome://settings/*",
    "chrome://tab-strip.top-chrome/*",
    "chrome://version/*",
    "chrome://webui-test/*",
    "chrome://welcome/*",
  ]),
  "extensions/common/api/_api_features.json": new Set([
    "chrome://add-supervision/*",
    "chrome://app-settings/*",
    "chrome://apps/*",
    "chrome://bluetooth-pairing/*",
    "chrome://bookmarks-side-panel.top-chrome/*",
    "chrome://bookmarks/*",
    "chrome://cast-feedback/*",
    "chrome://certificate-manager/*",
    "chrome://chrome-signin/*",
    "chrome://compare/*",
    "chrome://contextual-tasks/*",
    "chrome://customize-chrome-side-panel.top-chrome/*",
    "chrome://eche-app/*",
    "chrome://extensions-zero-state/*",
    "chrome://extensions/*",
    "chrome://feedback/*",
    "chrome://file-manager/*",
    "chrome://glic-fre/*",
    "chrome://glic/*",
    "chrome://graduation/*",
    "chrome://help-app/*",
    "chrome://history/*",
    "chrome://home/*",
    "chrome://internet-config-dialog/*",
    "chrome://internet-detail-dialog/*",
    "chrome://lock-reauth/*",
    "chrome://mobilesetup/*",
    "chrome://network/*",
    "chrome://new-tab-page/*",
    "chrome://newtab-footer/*",
    "chrome://omnibox-popup.top-chrome/*",
    "chrome://oobe/*",
    "chrome://os-feedback/*",
    "chrome://os-settings/*",
    "chrome://parent-access/*",
    "chrome://password-change/*",
    "chrome://password-manager/*",
    "chrome://personalization/*",
    "chrome://profile-picker/*",
    "chrome://read-later.top-chrome/*",
    "chrome://search-engine-choice/*",
    "chrome://settings/*",
    "chrome://shopping-insights-side-panel.top-chrome/*",
    "chrome://sync-confirmation/*",
    "chrome://tab-search.top-chrome/*",
    "chrome://tab-strip.top-chrome/*",
    "chrome://vc-background/*",
    "chrome://webui-test/*",
  ]),
};

const EXACT_REWRITE_MANAGED_PATHS = new Set([
  ...SCHEME_TEXT_REWRITE_PATHS,
  ...WEBUI_BASE_HREF_PATHS,
  ...EXTENSION_API_FEATURE_PATHS,
]);

function normalizeChromiumPath(filePath: string): string {
  return filePath.split(path.sep).join("/");
}

export function isChromiumRewriteManagedPath(filePath: string): boolean {
  const normalized = normalizeChromiumPath(filePath);
  return EXACT_REWRITE_MANAGED_PATHS.has(normalized) ||
      /^components\/resources\/terms\/terms_[^/]+\.html$/.test(normalized);
}

export function rewriteChromeSchemeText(content: string): RewriteResult {
  const replacements = content.split(CHROME_SCHEME).length - 1;
  if (replacements === 0) {
    return {content, replacements};
  }
  return {
    content: content.split(CHROME_SCHEME).join(DAO_SCHEME),
    replacements,
  };
}

export function rewriteWebUiBaseHref(content: string): RewriteResult {
  let replacements = 0;
  const rewritten = content.replace(
      /<base href="chrome:\/\/([^"]+)">/g,
      (_match, hostAndPath: string) => {
        replacements++;
        return `<base href="dao://${hostAndPath}">`;
      });
  return {content: rewritten, replacements};
}

export function mirrorDaoSchemeInQuotedChromeUrls(
    content: string): RewriteResult {
  return mirrorDaoSchemeInQuotedChromeUrlsMatching(content);
}

function mirrorDaoSchemeInQuotedChromeUrlsMatching(
    content: string, allowedChromeUrls?: Set<string>): RewriteResult {
  let replacements = 0;
  const rewritten = content.replace(
      /"chrome:\/\/([^"]+)"/g,
      (match: string, hostAndPath: string, offset: number) => {
        const chromeUrl = `chrome://${hostAndPath}`;
        if (allowedChromeUrls && !allowedChromeUrls.has(chromeUrl)) {
          return match;
        }
        const daoMatch = `"dao://${hostAndPath}"`;
        const lineStart = content.lastIndexOf("\n", offset) + 1;
        const nextLineBreak = content.indexOf("\n", offset);
        const lineEnd = nextLineBreak === -1 ? content.length : nextLineBreak;
        const line = content.slice(lineStart, lineEnd);
        const nextLineStart = lineEnd < content.length ? lineEnd + 1 :
          content.length;
        const followingLineBreak = content.indexOf("\n", nextLineStart);
        const nextLineEnd =
          followingLineBreak === -1 ? content.length : followingLineBreak;
        const nextLine = content.slice(nextLineStart, nextLineEnd);
        if (line.includes(daoMatch) || nextLine.includes(daoMatch)) {
          return match;
        }
        replacements++;
        if (line.trim() === match) {
          const indent = line.slice(0, line.indexOf(match));
          return `${match},\n${indent}${daoMatch}`;
        }
        return `${match}, ${daoMatch}`;
      });
  return {content: rewritten, replacements};
}

export function rewriteChromiumPathContent(
    filePath: string, content: string): RewriteResult|null {
  const normalized = normalizeChromiumPath(filePath);
  if (/^components\/resources\/terms\/terms_[^/]+\.html$/.test(normalized) ||
      SCHEME_TEXT_REWRITE_PATHS.includes(normalized)) {
    return rewriteChromeSchemeText(content);
  }
  if (WEBUI_BASE_HREF_PATHS.includes(normalized)) {
    return rewriteWebUiBaseHref(content);
  }
  if (EXTENSION_API_FEATURE_PATHS.includes(normalized)) {
    return mirrorDaoSchemeInQuotedChromeUrlsMatching(
        content, EXTENSION_API_FEATURE_URLS_BY_PATH[normalized]);
  }
  return null;
}

export function applyChromiumRewrites(srcDir: string): ChromiumRewriteSummary {
  const summary: ChromiumRewriteSummary = {
    filesChanged: 0,
    filesUnchanged: 0,
    filesMissing: 0,
    replacements: 0,
  };

  const termsPaths = globSync("components/resources/terms/terms_*.html", {
    cwd: srcDir,
    nodir: true,
  }).sort();

  for (const filePath of [
    ...termsPaths,
    ...SCHEME_TEXT_REWRITE_PATHS,
    ...WEBUI_BASE_HREF_PATHS,
    ...EXTENSION_API_FEATURE_PATHS,
  ]) {
    applyRewriteToFile(srcDir, filePath, summary);
  }

  return summary;
}

function applyRewriteToFile(
    srcDir: string,
    filePath: string,
    summary: ChromiumRewriteSummary): void {
  const fullPath = path.join(srcDir, filePath);
  if (!existsSync(fullPath)) {
    summary.filesMissing++;
    return;
  }

  const original = readFileSync(fullPath, "utf-8");
  const result = rewriteChromiumPathContent(filePath, original);
  if (!result) {
    summary.filesUnchanged++;
    return;
  }
  summary.replacements += result.replacements;

  if (result.content === original) {
    summary.filesUnchanged++;
    return;
  }

  writeFileSync(fullPath, result.content);
  summary.filesChanged++;
}
