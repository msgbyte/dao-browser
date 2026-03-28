// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_sidebar_ui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/files/file_enumerator.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/dao_sidebar_resources.h"
#include "chrome/grit/dao_sidebar_resources_map.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "dao/browser/ui/views/sidebar/dao_file_icon_util_mac.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"

namespace dao {

namespace {

// Check if a tab is audible, matching Chrome's own detection logic.
// Uses RecentlyAudibleHelper (persists for 2s after audio stops) when
// available, falling back to IsCurrentlyAudible().
bool IsTabAudible(content::WebContents* contents) {
  auto* helper = RecentlyAudibleHelper::FromWebContents(contents);
  if (helper) {
    return helper->WasRecentlyAudible();
  }
  return contents->IsCurrentlyAudible();
}

std::string ImageSkiaToDataUrl(const gfx::ImageSkia& image) {
  if (image.isNull()) {
    return std::string();
  }
  const SkBitmap* bitmap = image.bitmap();
  if (!bitmap) {
    return std::string();
  }
  auto png_data = gfx::PNGCodec::EncodeBGRASkBitmap(
      *bitmap, /*discard_transparency=*/false);
  if (!png_data.has_value()) {
    return std::string();
  }
  return "data:image/png;base64," + base::Base64Encode(png_data.value());
}

std::string FaviconToDataUrl(content::WebContents* contents) {
  if (!contents) {
    return std::string();
  }
  gfx::Image favicon = favicon::TabFaviconFromWebContents(contents);
  if (favicon.IsEmpty()) {
    return std::string();
  }
  return ImageSkiaToDataUrl(favicon.AsImageSkia());
}

constexpr int kMaxRecentFiles = 4;
constexpr int kThumbnailSize = 40;

struct ScanResult {
  std::vector<base::FilePath> paths;
  std::vector<base::Value::Dict> entries;
};

ScanResult ScanRecentFiles(base::FilePath download_dir) {
  struct FileEntry {
    base::FilePath path;
    base::Time modified_time;
  };
  std::vector<FileEntry> entries;

  base::FileEnumerator enumerator(download_dir, /*recursive=*/false,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    if (path.BaseName().value()[0] == '.') {
      continue;
    }
    base::FileEnumerator::FileInfo info = enumerator.GetInfo();
    entries.push_back({path, info.GetLastModifiedTime()});
  }

  std::sort(entries.begin(), entries.end(),
            [](const FileEntry& a, const FileEntry& b) {
              return a.modified_time > b.modified_time;
            });

  ScanResult result;
  int count = std::min(static_cast<int>(entries.size()), kMaxRecentFiles);
  for (int i = 0; i < count; ++i) {
    result.paths.push_back(entries[i].path);

    base::Value::Dict d;
    d.Set("index", i);
    d.Set("name", entries[i].path.BaseName().value());

    // Try thumbnail first, fall back to file icon.
    gfx::ImageSkia thumb = GetFileThumbnail(entries[i].path, kThumbnailSize);
    if (!thumb.isNull()) {
      d.Set("iconUrl", ImageSkiaToDataUrl(thumb));
      d.Set("hasThumbnail", true);
    } else {
      gfx::ImageSkia icon = GetFileIcon(entries[i].path, kThumbnailSize);
      d.Set("iconUrl", ImageSkiaToDataUrl(icon));
      d.Set("hasThumbnail", false);
    }
    result.entries.push_back(std::move(d));
  }
  // Reverse: oldest first (top), newest last (closest to button).
  std::reverse(result.paths.begin(), result.paths.end());
  std::reverse(result.entries.begin(), result.entries.end());
  // Fix indices after reversal.
  for (int i = 0; i < static_cast<int>(result.entries.size()); ++i) {
    result.entries[i].Set("index", i);
  }
  return result;
}

}  // namespace

// ---- DaoSidebarUIConfig ----

DaoSidebarUIConfig::DaoSidebarUIConfig()
    : WebUIConfig(content::kChromeUIScheme, "sidebar") {}

std::unique_ptr<content::WebUIController>
DaoSidebarUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                           const GURL& url) {
  return std::make_unique<DaoSidebarUI>(web_ui);
}

// ---- DaoSidebarUIHandler ----

DaoSidebarUIHandler::DaoSidebarUIHandler() = default;

DaoSidebarUIHandler::~DaoSidebarUIHandler() {
  download_notifier_.reset();
  if (browser_) {
    browser_->tab_strip_model()->RemoveObserver(this);
  }
}

void DaoSidebarUIHandler::SetBrowser(Browser* browser) {
  if (browser_) {
    browser_->tab_strip_model()->RemoveObserver(this);
  }
  download_notifier_.reset();
  browser_ = browser;
  if (browser_) {
    browser_->tab_strip_model()->AddObserver(this);
    // Initialize download observer.
    auto* profile = browser_->profile();
    if (profile) {
      auto* download_manager = profile->GetDownloadManager();
      if (download_manager) {
        download_notifier_ =
            std::make_unique<download::AllDownloadItemNotifier>(
                download_manager, this);
      }
    }
  }
}

void DaoSidebarUIHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getInitialState",
      base::BindRepeating(&DaoSidebarUIHandler::HandleGetInitialState,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "activateTab",
      base::BindRepeating(&DaoSidebarUIHandler::HandleActivateTab,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "closeTab",
      base::BindRepeating(&DaoSidebarUIHandler::HandleCloseTab,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "toggleMute",
      base::BindRepeating(&DaoSidebarUIHandler::HandleToggleMute,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "moveTab",
      base::BindRepeating(&DaoSidebarUIHandler::HandleMoveTab,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "showCommandBarForNewTab",
      base::BindRepeating(
          &DaoSidebarUIHandler::HandleShowCommandBarForNewTab,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "handleFileDrop",
      base::BindRepeating(&DaoSidebarUIHandler::HandleFileDrop,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setDropInsertIndex",
      base::BindRepeating(&DaoSidebarUIHandler::HandleSetDropInsertIndex,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "requestDownloadState",
      base::BindRepeating(&DaoSidebarUIHandler::HandleRequestDownloadState,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openDownloadsFolder",
      base::BindRepeating(&DaoSidebarUIHandler::HandleOpenDownloadsFolder,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openRecentFile",
      base::BindRepeating(&DaoSidebarUIHandler::HandleOpenRecentFile,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelDownload",
      base::BindRepeating(&DaoSidebarUIHandler::HandleCancelDownload,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startFileDrag",
      base::BindRepeating(&DaoSidebarUIHandler::HandleStartFileDrag,
                          base::Unretained(this)));
}

void DaoSidebarUIHandler::OnJavascriptAllowed() {
  if (browser_) {
    PushFullState();
  }
}

void DaoSidebarUIHandler::OnJavascriptDisallowed() {}

void DaoSidebarUIHandler::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (!IsJavascriptAllowed()) {
    return;
  }

  // Structural changes (insert, remove, move, replace) need a full refresh
  // because tab indices shift.
  if (change.type() != TabStripModelChange::kSelectionOnly) {
    PushFullState();
    return;
  }

  // Selection-only change: just push an active-index update to avoid
  // overwriting in-flight audio state from TabChangedAt.
  if (selection.active_tab_changed()) {
    base::Value::Dict update;
    update.Set("activeIndex", tab_strip_model->active_index());
    FireWebUIListener("activeTabChanged", update);
  }
}

void DaoSidebarUIHandler::TabChangedAt(content::WebContents* contents,
                                        int index,
                                        TabChangeType change_type) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  PushTabUpdate(index);
}

void DaoSidebarUIHandler::PushFullState() {
  if (!browser_) {
    return;
  }
  TabStripModel* model = browser_->tab_strip_model();
  base::Value::List pinned_tabs;
  base::Value::List unpinned_tabs;

  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents) continue;

    base::Value::Dict tab;
    tab.Set("index", i);
    tab.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
    tab.Set("url", contents->GetVisibleURL().spec());
    tab.Set("faviconUrl", FaviconToDataUrl(contents));
    tab.Set("isActive", i == model->active_index());
    tab.Set("isPinned", model->IsTabPinned(i));
    tab.Set("isAudible", IsTabAudible(contents));
    tab.Set("isMuted", contents->IsAudioMuted());

    if (model->IsTabPinned(i)) {
      pinned_tabs.Append(std::move(tab));
    } else {
      unpinned_tabs.Append(std::move(tab));
    }
  }

  base::Value::Dict state;
  state.Set("pinnedTabs", std::move(pinned_tabs));
  state.Set("unpinnedTabs", std::move(unpinned_tabs));
  state.Set("activeIndex", model->active_index());

  FireWebUIListener("sidebarStateChanged", state);
}

void DaoSidebarUIHandler::PushTabUpdate(int index) {
  if (!browser_) {
    return;
  }
  TabStripModel* model = browser_->tab_strip_model();
  if (index < 0 || index >= model->count()) {
    return;
  }
  content::WebContents* contents = model->GetWebContentsAt(index);
  if (!contents) {
    return;
  }

  base::Value::Dict tab;
  tab.Set("index", index);
  tab.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
  tab.Set("url", contents->GetVisibleURL().spec());
  tab.Set("faviconUrl", FaviconToDataUrl(contents));
  tab.Set("isActive", index == model->active_index());
  tab.Set("isPinned", model->IsTabPinned(index));
  tab.Set("isAudible", IsTabAudible(contents));
  tab.Set("isMuted", contents->IsAudioMuted());

  FireWebUIListener("tabUpdated", tab);
}

void DaoSidebarUIHandler::HandleGetInitialState(
    const base::Value::List& args) {
  // Auto-discover Browser* if SetBrowser hasn't been called yet.
  // This handles the race where LoadInitialURL is async and GetWebUI()
  // returns null right after the call in EnsureWebUILoaded().
  if (!browser_) {
    content::WebContents* wc = web_ui()->GetWebContents();
    if (wc) {
      // The sidebar WebView's WebContents is not in any tab strip, so we
      // can't use FindBrowserWithTab. Instead, find the browser whose
      // BrowserView owns a sidebar WebView showing this WebContents.
      for (Browser* b : *BrowserList::GetInstance()) {
        BrowserView* bv = BrowserView::GetBrowserViewForBrowser(b);
        if (bv && bv->dao_sidebar() && bv->dao_sidebar()->use_webui()) {
          // Match by profile — in single-window use this is sufficient.
          if (b->profile() == Profile::FromWebUI(web_ui())) {
            SetBrowser(b);
            break;
          }
        }
      }
    }
  }
  AllowJavascript();
  PushFullState();
}

void DaoSidebarUIHandler::HandleActivateTab(
    const base::Value::List& args) {
  if (!browser_ || args.empty()) return;
  int index = args[0].GetIfInt().value_or(-1);
  if (index < 0) return;
  browser_->tab_strip_model()->ActivateTabAt(index);
}

void DaoSidebarUIHandler::HandleCloseTab(
    const base::Value::List& args) {
  if (!browser_ || args.empty()) return;
  int index = args[0].GetIfInt().value_or(-1);
  if (index < 0) return;
  browser_->tab_strip_model()->CloseWebContentsAt(
      index, TabCloseTypes::CLOSE_USER_GESTURE);
}

void DaoSidebarUIHandler::HandleToggleMute(
    const base::Value::List& args) {
  if (!browser_ || args.empty()) return;
  int index = args[0].GetIfInt().value_or(-1);
  if (index < 0) return;
  content::WebContents* contents =
      browser_->tab_strip_model()->GetWebContentsAt(index);
  if (contents) {
    contents->SetAudioMuted(!contents->IsAudioMuted());
  }
}

void DaoSidebarUIHandler::HandleMoveTab(
    const base::Value::List& args) {
  if (!browser_ || args.size() < 2) return;
  int from_index = args[0].GetIfInt().value_or(-1);
  int to_index = args[1].GetIfInt().value_or(-1);
  if (from_index < 0 || to_index < 0) return;
  browser_->tab_strip_model()->MoveWebContentsAt(
      from_index, to_index, /*select_after_move=*/false);
}

void DaoSidebarUIHandler::HandleShowCommandBarForNewTab(
    const base::Value::List& args) {
  if (!browser_) return;
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view || !browser_view->dao_command_bar()) return;

  browser_view->dao_command_bar()->ShowForNewTab();
}

void DaoSidebarUIHandler::HandleFileDrop(
    const base::Value::List& args) {
  if (!browser_ || args.empty()) return;
  const base::Value::List* url_list = args[0].GetIfList();
  if (!url_list) return;
  for (const auto& item : *url_list) {
    const std::string* url_str = item.GetIfString();
    if (!url_str) continue;
    GURL url(*url_str);
    if (!url.is_valid()) continue;
    NavigateParams params(browser_, url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  }
}

void DaoSidebarUIHandler::HandleSetDropInsertIndex(
    const base::Value::List& args) {
  if (!browser_) return;
  int index = args.empty() ? -1 : args[0].GetIfInt().value_or(-1);
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_sidebar()) {
    browser_view->dao_sidebar()->SetWebUIDropInsertIndex(index);
  }
}

// ---- Download Observer ----

void DaoSidebarUIHandler::OnDownloadCreated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (!IsJavascriptAllowed()) return;
  PushActiveDownloads();
}

void DaoSidebarUIHandler::OnDownloadUpdated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (!IsJavascriptAllowed()) return;
  PushActiveDownloads();
}

void DaoSidebarUIHandler::OnDownloadRemoved(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (!IsJavascriptAllowed()) return;
  PushActiveDownloads();
}

base::Value::List DaoSidebarUIHandler::BuildActiveDownloadList() {
  base::Value::List active;
  if (!browser_) return active;
  auto* profile = browser_->profile();
  if (!profile) return active;
  auto* dm = profile->GetDownloadManager();
  if (!dm) return active;

  content::DownloadManager::DownloadVector items;
  dm->GetAllDownloads(&items);

  for (const auto& item : items) {
    if (item->GetState() != download::DownloadItem::IN_PROGRESS) continue;
    base::Value::Dict d;
    d.Set("id", static_cast<int>(item->GetId()));
    d.Set("name", item->GetFileNameToReportUser().BaseName().value());
    d.Set("percent", item->PercentComplete());
    d.Set("speed", FormatSpeed(item->CurrentSpeed()));
    active.Append(std::move(d));
  }
  return active;
}

void DaoSidebarUIHandler::PushActiveDownloads() {
  base::Value::List active = BuildActiveDownloadList();
  FireWebUIListener("activeDownloadsChanged", active);
}

void DaoSidebarUIHandler::PushDownloadState() {
  if (!browser_) return;

  DownloadPrefs* prefs =
      DownloadPrefs::FromBrowserContext(browser_->profile());
  base::FilePath download_dir = prefs->DownloadPath();

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ScanRecentFiles, download_dir),
      base::BindOnce(
          [](base::WeakPtr<DaoSidebarUIHandler> self, ScanResult result) {
            if (!self || !self->IsJavascriptAllowed()) return;
            base::Value::List file_list;
            for (auto& entry : result.entries) {
              file_list.Append(std::move(entry));
            }
            self->OnScanResultReady(std::move(file_list),
                                    std::move(result.paths));
          },
          weak_factory_.GetWeakPtr()));
}

void DaoSidebarUIHandler::OnScanResultReady(
    base::Value::List file_entries,
    std::vector<base::FilePath> paths) {
  recent_file_paths_ = std::move(paths);

  base::Value::Dict state;
  state.Set("recentFiles", std::move(file_entries));
  state.Set("activeDownloads", BuildActiveDownloadList());

  FireWebUIListener("downloadStateChanged", state);
}

// static
std::string DaoSidebarUIHandler::FormatSpeed(int64_t bytes_per_sec) {
  if (bytes_per_sec <= 0) return "0 B/s";
  char buf[32];
  if (bytes_per_sec < 1024) {
    snprintf(buf, sizeof(buf), "%lld B/s", static_cast<long long>(bytes_per_sec));
  } else if (bytes_per_sec < 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f KB/s", bytes_per_sec / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%.1f MB/s", bytes_per_sec / (1024.0 * 1024.0));
  }
  return std::string(buf);
}

void DaoSidebarUIHandler::HandleRequestDownloadState(
    const base::Value::List& args) {
  if (!browser_) return;
  PushDownloadState();
  PushActiveDownloads();
}

void DaoSidebarUIHandler::HandleOpenDownloadsFolder(
    const base::Value::List& args) {
  if (!browser_) return;
  DownloadPrefs* prefs =
      DownloadPrefs::FromBrowserContext(browser_->profile());
  platform_util::OpenItem(browser_->profile(), prefs->DownloadPath(),
                          platform_util::OPEN_FOLDER,
                          platform_util::OpenOperationCallback());
}

void DaoSidebarUIHandler::HandleOpenRecentFile(
    const base::Value::List& args) {
  if (!browser_ || args.empty()) return;
  int index = args[0].GetIfInt().value_or(-1);
  if (index < 0 || index >= static_cast<int>(recent_file_paths_.size())) return;
  platform_util::OpenItem(browser_->profile(), recent_file_paths_[index],
                          platform_util::OPEN_FILE,
                          platform_util::OpenOperationCallback());
}

void DaoSidebarUIHandler::HandleCancelDownload(
    const base::Value::List& args) {
  if (!browser_ || args.empty()) return;
  int id = args[0].GetIfInt().value_or(-1);
  if (id < 0) return;

  auto* profile = browser_->profile();
  if (!profile) return;
  auto* dm = profile->GetDownloadManager();
  if (!dm) return;

  download::DownloadItem* item = dm->GetDownload(static_cast<uint32_t>(id));
  if (item) {
    item->Cancel(/*user_cancel=*/true);
  }
}

void DaoSidebarUIHandler::HandleStartFileDrag(
    const base::Value::List& args) {
  if (!browser_ || args.empty()) return;
  int index = args[0].GetIfInt().value_or(-1);
  if (index < 0 || index >= static_cast<int>(recent_file_paths_.size())) return;

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_sidebar()) {
    browser_view->dao_sidebar()->StartFileDrag(recent_file_paths_[index]);
  }
}

// ---- DaoSidebarUI ----

DaoSidebarUI::DaoSidebarUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, "sidebar");

  source->AddResourcePaths(kDaoSidebarResources);
  source->SetDefaultResource(IDR_DAO_SIDEBAR_SIDEBAR_HTML);

  // Allow innerHTML for Lit rendering.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop;");

  auto handler = std::make_unique<DaoSidebarUIHandler>();
  handler_ = handler.get();
  web_ui->AddMessageHandler(std::move(handler));
}

DaoSidebarUI::~DaoSidebarUI() = default;

void DaoSidebarUI::SetBrowser(Browser* browser) {
  if (handler_) {
    handler_->SetBrowser(browser);
  }
}

}  // namespace dao
