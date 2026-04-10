// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_sidebar_ui.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/dao_pref_names.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "dao/browser/ui/views/sidebar/dao_tab_tooltip_view.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "chrome/grit/dao_sidebar_resources.h"
#include "chrome/grit/dao_sidebar_resources_map.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/rect.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "dao/browser/ui/views/sidebar/dao_file_icon_util_mac.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/mojom/menu_source_type.mojom-shared.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

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
  // Request the 2x representation for Retina displays.  If only a 1x
  // bitmap exists, ImageSkia will scale it up automatically, so the
  // result is never worse than before and is sharp when a 2x rep is
  // available.
  const gfx::ImageSkiaRep& rep = image.GetRepresentation(2.0f);
  const SkBitmap& bitmap = rep.GetBitmap();
  if (bitmap.drawsNothing()) {
    return std::string();
  }
  auto png_data = gfx::PNGCodec::EncodeBGRASkBitmap(
      bitmap, /*discard_transparency=*/false);
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

// Returns true if the favicon is predominantly light-colored and would be
// invisible on a dark sidebar background.
bool IsFaviconLight(content::WebContents* contents) {
  if (!contents) {
    return false;
  }
  gfx::Image favicon = favicon::TabFaviconFromWebContents(contents);
  if (favicon.IsEmpty()) {
    return false;
  }
  const SkBitmap* bitmap = favicon.ToSkBitmap();
  if (!bitmap || bitmap->drawsNothing()) {
    return false;
  }

  double luminance_sum = 0.0;
  int opaque_pixels = 0;

  for (int y = 0; y < bitmap->height(); ++y) {
    for (int x = 0; x < bitmap->width(); ++x) {
      SkColor color = bitmap->getColor(x, y);
      int alpha = SkColorGetA(color);
      // Skip fully transparent pixels.
      if (alpha < 10) {
        continue;
      }
      double r = SkColorGetR(color) / 255.0;
      double g = SkColorGetG(color) / 255.0;
      double b = SkColorGetB(color) / 255.0;
      luminance_sum += 0.299 * r + 0.587 * g + 0.114 * b;
      ++opaque_pixels;
    }
  }

  if (opaque_pixels == 0) {
    return false;
  }
  return (luminance_sum / opaque_pixels) > 0.85;
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

DaoSplitView* DaoSidebarUIHandler::GetSplitView() const {
  if (!browser_) return nullptr;
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  return bv ? bv->dao_split_view() : nullptr;
}

bool DaoSidebarUIHandler::IsInAnySplitGroup(
    content::WebContents* contents) const {
  DaoSplitView* split = GetSplitView();
  if (!split) return false;
  for (const auto& summary : split->GetSplitGroupSummaries()) {
    for (auto* wc : summary.contents) {
      if (wc == contents) return true;
    }
  }
  return false;
}

void DaoSidebarUIHandler::PlaceGroupAroundAnchor(
    const std::vector<content::WebContents*>& group_ordered,
    content::WebContents* anchor) {
  TabStripModel* model = browser_->tab_strip_model();

  size_t anchor_pos = 0;
  for (size_t i = 0; i < group_ordered.size(); i++) {
    if (group_ordered[i] == anchor) {
      anchor_pos = i;
      break;
    }
  }

  // Insert members before anchor in reverse so each lands just before it.
  for (int i = static_cast<int>(anchor_pos) - 1; i >= 0; i--) {
    int a = model->GetIndexOfWebContents(anchor);
    int cur = model->GetIndexOfWebContents(group_ordered[i]);
    if (cur == TabStripModel::kNoTab) continue;
    int target = (cur < a) ? a - 1 : a;
    if (cur != target)
      model->MoveWebContentsAt(cur, target, false);
  }

  // Insert members after anchor in order.
  for (size_t i = anchor_pos + 1; i < group_ordered.size(); i++) {
    int a = model->GetIndexOfWebContents(anchor);
    int offset = static_cast<int>(i - anchor_pos);
    int cur = model->GetIndexOfWebContents(group_ordered[i]);
    if (cur == TabStripModel::kNoTab) continue;
    int target = (cur < a) ? a + offset - 1 : a + offset;
    if (cur != target)
      model->MoveWebContentsAt(cur, target, false);
  }
}

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
    // Split callback is wired lazily in HandleGetInitialState because
    // BrowserView may not be ready when SetBrowser is called.
    // Initialize download observer.
    auto* profile = browser_->profile();
    if (profile) {
      auto* download_manager = profile->GetDownloadManager();
      if (download_manager) {
        download_notifier_ =
            std::make_unique<download::AllDownloadItemNotifier>(
                download_manager, this);
      }
      // Open dao://welcome on first launch.
      if (!profile->GetPrefs()->GetBoolean(prefs::kDaoWelcomeShown)) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](base::WeakPtr<DaoSidebarUIHandler> self) {
                  if (!self || !self->browser_) return;
                  NavigateParams params(
                      self->browser_, GURL("dao://welcome"),
                      ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
                  params.disposition =
                      WindowOpenDisposition::NEW_FOREGROUND_TAB;
                  Navigate(&params);
                },
                weak_factory_.GetWeakPtr()));
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
  web_ui()->RegisterMessageCallback(
      "tabDragActive",
      base::BindRepeating(&DaoSidebarUIHandler::HandleTabDragActive,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "moveTabCrossWindow",
      base::BindRepeating(&DaoSidebarUIHandler::HandleMoveTabCrossWindow,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "detachTabToNewWindow",
      base::BindRepeating(&DaoSidebarUIHandler::HandleDetachTabToNewWindow,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "loadFolders",
      base::BindRepeating(&DaoSidebarUIHandler::HandleLoadFolders,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "saveFolders",
      base::BindRepeating(&DaoSidebarUIHandler::HandleSaveFolders,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "showTabContextMenu",
      base::BindRepeating(&DaoSidebarUIHandler::HandleShowTabContextMenu,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "showTabTooltip",
      base::BindRepeating(&DaoSidebarUIHandler::HandleShowTabTooltip,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "hideTabTooltip",
      base::BindRepeating(&DaoSidebarUIHandler::HandleHideTabTooltip,
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

  // Selection-only change: push full state because split group membership
  // depends on which tab is active (IsSplitActive / GetSplitContents).
  if (selection.active_tab_changed()) {
    PushFullState();
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

void DaoSidebarUIHandler::OnSplitStateChanged() {
  ConsolidateSplitGroupTabs();
  PushFullState();
}

void DaoSidebarUIHandler::ConsolidateSplitGroupTabs() {
  DaoSplitView* split = GetSplitView();
  if (!split) return;

  TabStripModel* model = browser_->tab_strip_model();
  for (const auto& summary : split->GetSplitGroupSummaries()) {
    // Sort group members by current model index to get canonical order.
    std::vector<std::pair<int, content::WebContents*>> indexed;
    for (auto* wc : summary.contents) {
      int idx = model->GetIndexOfWebContents(wc);
      if (idx != TabStripModel::kNoTab)
        indexed.emplace_back(idx, wc);
    }
    std::sort(indexed.begin(), indexed.end());
    if (indexed.size() <= 1) continue;

    // Skip if already contiguous.
    bool contiguous = true;
    for (size_t i = 1; i < indexed.size(); i++) {
      if (indexed[i].first != indexed[i - 1].first + 1) {
        contiguous = false;
        break;
      }
    }
    if (contiguous) continue;

    // Use lowest-index member as anchor and consolidate around it.
    std::vector<content::WebContents*> ordered;
    ordered.reserve(indexed.size());
    for (auto& [idx, wc] : indexed)
      ordered.push_back(wc);
    PlaceGroupAroundAnchor(ordered, ordered.front());
  }
}

void DaoSidebarUIHandler::PushFullState() {
  if (!browser_) {
    return;
  }
  TabStripModel* model = browser_->tab_strip_model();
  base::Value::List pinned_tabs;
  base::Value::List unpinned_tabs;

  std::set<content::WebContents*> split_contents;
  DaoSplitView* split = GetSplitView();
  if (split) {
    for (const auto& summary : split->GetSplitGroupSummaries())
      split_contents.insert(summary.contents.begin(),
                            summary.contents.end());
  }

  for (int i = 0; i < model->count(); ++i) {
    content::WebContents* contents = model->GetWebContentsAt(i);
    if (!contents) continue;

    base::Value::Dict tab;
    tab.Set("tabId", GetSidebarTabId(contents));
    tab.Set("index", i);
    tab.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
    tab.Set("url", contents->GetVisibleURL().spec());
    tab.Set("faviconUrl", FaviconToDataUrl(contents));
    tab.Set("isFaviconLight", IsFaviconLight(contents));
    tab.Set("isActive", i == model->active_index());
    tab.Set("isPinned", model->IsTabPinned(i));
    tab.Set("isAudible", IsTabAudible(contents));
    tab.Set("isMuted", contents->IsAudioMuted());
    tab.Set("isAgentLocked", DaoAgentLockTabHelper::IsLocked(contents));
    tab.Set("isInSplit", split_contents.count(contents) > 0);

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
  state.Set("sessionId",
            static_cast<int>(browser_->session_id().id()));

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
  tab.Set("tabId", GetSidebarTabId(contents));
  tab.Set("index", index);
  tab.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
  tab.Set("url", contents->GetVisibleURL().spec());
  tab.Set("faviconUrl", FaviconToDataUrl(contents));
  tab.Set("isFaviconLight", IsFaviconLight(contents));
  tab.Set("isActive", index == model->active_index());
  tab.Set("isPinned", model->IsTabPinned(index));
  tab.Set("isAudible", IsTabAudible(contents));
  tab.Set("isMuted", contents->IsAudioMuted());
  tab.Set("isAgentLocked", DaoAgentLockTabHelper::IsLocked(contents));

  tab.Set("isInSplit", IsInAnySplitGroup(contents));

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

  // Wire split-state callback now that BrowserView is guaranteed to exist.
  if (DaoSplitView* split = GetSplitView()) {
    split->set_split_state_changed_callback(
        base::BindRepeating(&DaoSidebarUIHandler::OnSplitStateChanged,
                            weak_factory_.GetWeakPtr()));
  }

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

  TabStripModel* model = browser_->tab_strip_model();
  content::WebContents* moved = model->GetWebContentsAt(from_index);
  if (!moved) return;

  // Collect the split group (sorted by model index) before any moves.
  std::vector<content::WebContents*> group_ordered;
  if (DaoSplitView* split = GetSplitView()) {
    for (const auto& summary : split->GetSplitGroupSummaries()) {
      bool in_group = false;
      for (auto* wc : summary.contents) {
        if (wc == moved) { in_group = true; break; }
      }
      if (!in_group) continue;

      std::vector<std::pair<int, content::WebContents*>> indexed;
      for (auto* wc : summary.contents) {
        int idx = model->GetIndexOfWebContents(wc);
        if (idx != TabStripModel::kNoTab)
          indexed.emplace_back(idx, wc);
      }
      std::sort(indexed.begin(), indexed.end());
      for (auto& [idx, wc] : indexed)
        group_ordered.push_back(wc);
      break;
    }
  }

  if (group_ordered.size() <= 1) {
    model->MoveWebContentsAt(from_index, to_index, false);
    return;
  }

  // Move dragged tab first, then consolidate group around it.
  model->MoveWebContentsAt(from_index, to_index, false);
  PlaceGroupAroundAnchor(group_ordered, moved);
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

void DaoSidebarUIHandler::HandleTabDragActive(
    const base::Value::List& args) {
  if (!browser_ || args.empty()) return;
  bool active = args[0].GetIfBool().value_or(false);
  // Activate/deactivate tab drag on ALL windows' split views so any
  // window can receive the cross-window drop.
  for (Browser* browser : *BrowserList::GetInstance()) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser);
    if (bv && bv->dao_split_view()) {
      bv->dao_split_view()->SetTabDragActive(active);
    }
  }
  if (!active && IsJavascriptAllowed())
    PushFullState();
}

void DaoSidebarUIHandler::HandleMoveTabCrossWindow(
    const base::Value::List& args) {
  if (!browser_ || args.size() < 3) return;
  int source_session_id = args[0].GetIfInt().value_or(-1);
  int source_tab_index = args[1].GetIfInt().value_or(-1);
  int target_insert_index = args[2].GetIfInt().value_or(-1);
  if (source_session_id < 0 || source_tab_index < 0 ||
      target_insert_index < 0) {
    return;
  }

  // Find source browser by session ID.
  Browser* source_browser = nullptr;
  for (Browser* b : *BrowserList::GetInstance()) {
    if (static_cast<int>(b->session_id().id()) == source_session_id) {
      source_browser = b;
      break;
    }
  }
  if (!source_browser || source_browser == browser_) return;

  TabStripModel* source_model = source_browser->tab_strip_model();
  if (source_tab_index >= source_model->count()) return;

  std::unique_ptr<content::WebContents> detached =
      source_model->DetachWebContentsAtForInsertion(source_tab_index);
  if (!detached) return;

  TabStripModel* target_model = browser_->tab_strip_model();
  int clamped_index =
      std::min(target_insert_index, target_model->count());
  target_model->InsertWebContentsAt(
      clamped_index, std::move(detached), AddTabTypes::ADD_ACTIVE);

  // Auto-close source window if empty.
  if (source_model->count() == 0) {
    source_browser->window()->Close();
  }
}

void DaoSidebarUIHandler::HandleDetachTabToNewWindow(
    const base::Value::List& args) {
  if (!browser_ || args.size() < 3) return;
  int tab_index = args[0].GetIfInt().value_or(-1);
  int screen_x = args[1].GetIfInt().value_or(0);
  int screen_y = args[2].GetIfInt().value_or(0);
  if (tab_index < 0) return;

  TabStripModel* model = browser_->tab_strip_model();
  if (tab_index >= model->count()) return;

  // Don't detach the last tab — just move the window instead.
  if (model->count() <= 1) return;

  std::unique_ptr<content::WebContents> detached =
      model->DetachWebContentsAtForInsertion(tab_index);
  if (!detached) return;

  // Use the source window's size so the new window feels natural.
  gfx::Rect source_bounds = browser_->window()->GetBounds();
  Browser::CreateParams params(browser_->profile(), /*user_gesture=*/true);
  params.initial_bounds = gfx::Rect(
      screen_x - source_bounds.width() / 4,
      screen_y - 40,
      source_bounds.width(),
      source_bounds.height());
  Browser* new_browser = Browser::Create(params);
  if (!new_browser) return;

  new_browser->tab_strip_model()->InsertWebContentsAt(
      -1, std::move(detached), AddTabTypes::ADD_ACTIVE);
  new_browser->window()->Show();
  new_browser->window()->Activate();
}

void DaoSidebarUIHandler::HandleLoadFolders(
    const base::Value::List& args) {
  AllowJavascript();

  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  // If we already have in-memory data for this window, return it directly.
  if (!folder_json_.empty()) {
    FireWebUIListener(callback_id, base::Value(folder_json_));
    return;
  }

  // Otherwise, read from the shared profile file on a background thread.
  Profile* profile = Profile::FromWebUI(web_ui());
  base::FilePath path = profile->GetPath().AppendASCII("dao_folders.json");

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([](base::FilePath file_path) -> std::string {
        std::string contents;
        base::ReadFileToString(file_path, &contents);
        return contents;
      }, path),
      base::BindOnce(
          [](base::WeakPtr<DaoSidebarUIHandler> self,
             std::string callback_id, std::string contents) {
            if (!self || !self->IsJavascriptAllowed()) return;
            self->FireWebUIListener(callback_id, base::Value(contents));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoSidebarUIHandler::HandleSaveFolders(
    const base::Value::List& args) {
  if (args.empty()) return;
  const std::string* json = args[0].GetIfString();
  if (!json) return;

  // Update in-memory cache for this window.
  folder_json_ = *json;

  // Persist to shared profile file on a background thread.
  Profile* profile = Profile::FromWebUI(web_ui());
  base::FilePath path = profile->GetPath().AppendASCII("dao_folders.json");

  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce([](base::FilePath file_path, std::string data) {
        base::WriteFile(file_path, data);
      }, path, *json));
}

void DaoSidebarUIHandler::HandleShowTabContextMenu(
    const base::Value::List& args) {
  if (!browser_ || args.size() < 3) return;
  int tab_index = args[0].GetIfInt().value_or(-1);
  int screen_x = args[1].GetIfInt().value_or(0);
  int screen_y = args[2].GetIfInt().value_or(0);
  if (tab_index < 0) return;

  TabStripModel* model = browser_->tab_strip_model();
  if (tab_index >= model->count()) return;

  context_menu_tab_index_ = tab_index;
  content::WebContents* contents = model->GetWebContentsAt(tab_index);
  if (!contents) return;

  // Parse folder tab indices (arg[3] is an optional array of indices).
  folder_tab_indices_.clear();
  if (args.size() > 3 && args[3].is_list()) {
    for (const auto& val : args[3].GetList()) {
      if (val.is_int()) {
        folder_tab_indices_.insert(val.GetInt());
      }
    }
  }

  // Parse visual tab order (arg[4] is an array of model indices in
  // top-to-bottom visual order).
  visual_tab_order_.clear();
  if (args.size() > 4 && args[4].is_list()) {
    for (const auto& val : args[4].GetList()) {
      if (val.is_int()) {
        visual_tab_order_.push_back(val.GetInt());
      }
    }
  }

  // Build the menu model.
  tab_context_menu_model_ = std::make_unique<ui::SimpleMenuModel>(this);

  tab_context_menu_model_->AddItem(kDuplicateTab, u"Duplicate Tab");
  tab_context_menu_model_->AddItem(kCopyLink, u"Copy Link");

  bool is_muted = contents->IsAudioMuted();
  tab_context_menu_model_->AddItem(
      kToggleMute, is_muted ? u"Unmute Site" : u"Mute Site");

  tab_context_menu_model_->AddSeparator(ui::NORMAL_SEPARATOR);

  tab_context_menu_model_->AddItem(kCloseTab, u"Close Tab");
  tab_context_menu_model_->AddItem(kCloseOtherTabs, u"Close Other Tabs");
  tab_context_menu_model_->AddItem(kCloseTabsAbove, u"Close Tabs Above");
  tab_context_menu_model_->AddItem(kCloseTabsBelow, u"Close Tabs Below");

  // Get the Widget from the sidebar.
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view || !browser_view->dao_sidebar()) return;

  views::Widget* widget = browser_view->dao_sidebar()->GetWidget();
  if (!widget) return;

  // Create the menu runner and show the menu.
  tab_context_menu_runner_ = std::make_unique<views::MenuRunner>(
      tab_context_menu_model_.get(),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);

  gfx::Rect anchor_rect(gfx::Point(screen_x, screen_y), gfx::Size());
  tab_context_menu_runner_->RunMenuAt(
      widget, nullptr, anchor_rect,
      views::MenuAnchorPosition::kTopLeft,
      ui::mojom::MenuSourceType::kMouse);
}

void DaoSidebarUIHandler::HandleShowTabTooltip(
    const base::Value::List& args) {
  if (!browser_ || args.size() < 3) return;
  int screen_x = args[0].GetIfInt().value_or(0);
  int screen_y = args[1].GetIfInt().value_or(0);
  const std::string* title_str = args[2].GetIfString();
  if (!title_str) return;

  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!bv || !bv->dao_tab_tooltip()) return;

  // Convert screen coordinates to BrowserView coordinates.
  gfx::Point anchor(screen_x, screen_y);
  views::View::ConvertPointFromScreen(bv, &anchor);

  bv->dao_tab_tooltip()->ShowTooltip(
      base::UTF8ToUTF16(*title_str), anchor);
  bv->InvalidateLayout();
}

void DaoSidebarUIHandler::HandleHideTabTooltip(
    const base::Value::List& args) {
  if (!browser_) return;
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!bv || !bv->dao_tab_tooltip()) return;
  bv->dao_tab_tooltip()->HideTooltip();
}

int DaoSidebarUIHandler::FindVisualPosition(int tab_index) const {
  auto it = std::find(visual_tab_order_.begin(), visual_tab_order_.end(),
                      tab_index);
  if (it == visual_tab_order_.end()) return -1;
  return static_cast<int>(std::distance(visual_tab_order_.begin(), it));
}

void DaoSidebarUIHandler::CloseTabsInVisualRange(int from, int to) {
  TabStripModel* model = browser_->tab_strip_model();
  std::vector<int> to_close;
  int end = std::min(to, static_cast<int>(visual_tab_order_.size()));
  for (int i = from; i < end; ++i) {
    int idx = visual_tab_order_[i];
    if (idx != context_menu_tab_index_ &&
        folder_tab_indices_.find(idx) == folder_tab_indices_.end()) {
      to_close.push_back(idx);
    }
  }
  // Sort descending so closing doesn't shift earlier indices.
  std::sort(to_close.begin(), to_close.end(), std::greater<int>());
  for (int idx : to_close) {
    model->CloseWebContentsAt(idx, TabCloseTypes::CLOSE_USER_GESTURE);
  }
}

void DaoSidebarUIHandler::ClearContextMenuState() {
  context_menu_tab_index_ = -1;
  folder_tab_indices_.clear();
  visual_tab_order_.clear();
}

bool DaoSidebarUIHandler::IsCommandIdEnabled(int command_id) const {
  if (!browser_) return false;
  TabStripModel* model = browser_->tab_strip_model();
  if (context_menu_tab_index_ < 0 ||
      context_menu_tab_index_ >= model->count()) {
    return false;
  }

  int visual_pos = FindVisualPosition(context_menu_tab_index_);
  int total = static_cast<int>(visual_tab_order_.size());

  // Helper: count closable tabs in a visual range.
  auto countClosable = [&](int from, int to) -> int {
    int count = 0;
    int end = std::min(to, total);
    for (int i = from; i < end; ++i) {
      int idx = visual_tab_order_[i];
      if (idx != context_menu_tab_index_ &&
          folder_tab_indices_.find(idx) == folder_tab_indices_.end()) {
        count++;
      }
    }
    return count;
  };

  switch (command_id) {
    case kDuplicateTab:
      return chrome::CanDuplicateTabAt(browser_, context_menu_tab_index_);
    case kCopyLink:
    case kToggleMute:
    case kCloseTab:
      return true;
    case kCloseOtherTabs:
      return countClosable(0, total) > 0;
    case kCloseTabsAbove:
      return visual_pos > 0 && countClosable(0, visual_pos) > 0;
    case kCloseTabsBelow:
      return visual_pos >= 0 && countClosable(visual_pos + 1, total) > 0;
    default:
      return false;
  }
}

void DaoSidebarUIHandler::ExecuteCommand(int command_id, int event_flags) {
  if (!browser_) return;
  TabStripModel* model = browser_->tab_strip_model();
  if (context_menu_tab_index_ < 0 ||
      context_menu_tab_index_ >= model->count()) {
    return;
  }

  switch (command_id) {
    case kDuplicateTab:
      chrome::DuplicateTabAt(browser_, context_menu_tab_index_);
      break;

    case kCopyLink: {
      content::WebContents* contents =
          model->GetWebContentsAt(context_menu_tab_index_);
      if (contents) {
        ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
        writer.WriteText(
            base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
      }
      break;
    }

    case kToggleMute: {
      content::WebContents* contents =
          model->GetWebContentsAt(context_menu_tab_index_);
      if (contents) {
        contents->SetAudioMuted(!contents->IsAudioMuted());
      }
      break;
    }

    case kCloseTab:
      model->CloseWebContentsAt(context_menu_tab_index_,
                                TabCloseTypes::CLOSE_USER_GESTURE);
      break;

    case kCloseOtherTabs:
      CloseTabsInVisualRange(0,
                             static_cast<int>(visual_tab_order_.size()));
      break;

    case kCloseTabsAbove: {
      int visual_pos = FindVisualPosition(context_menu_tab_index_);
      if (visual_pos > 0) {
        CloseTabsInVisualRange(0, visual_pos);
      }
      break;
    }

    case kCloseTabsBelow: {
      int visual_pos = FindVisualPosition(context_menu_tab_index_);
      if (visual_pos >= 0) {
        CloseTabsInVisualRange(visual_pos + 1,
                               static_cast<int>(visual_tab_order_.size()));
      }
      break;
    }

    default:
      break;
  }

  ClearContextMenuState();
}

void DaoSidebarUIHandler::MenuClosed(ui::SimpleMenuModel* source) {
  ClearContextMenuState();
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
