// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_MINI_DAO_DOWNLOAD_CARD_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_MINI_DAO_DOWNLOAD_CARD_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "ui/views/view.h"

class Browser;

namespace content {
class DownloadManager;
class WebContents;
}  // namespace content

namespace download {
class DownloadItem;
}  // namespace download

namespace views {
class Label;
}  // namespace views

namespace dao {

// Mini Dao-only download progress card. It observes the profile download
// manager, but only shows downloads triggered by this Mini Dao window.
class DaoMiniDaoDownloadCardView
    : public views::View,
      public download::AllDownloadItemNotifier::Observer {
  METADATA_HEADER(DaoMiniDaoDownloadCardView, views::View)

 public:
  explicit DaoMiniDaoDownloadCardView(Browser* browser);
  DaoMiniDaoDownloadCardView(const DaoMiniDaoDownloadCardView&) = delete;
  DaoMiniDaoDownloadCardView& operator=(
      const DaoMiniDaoDownloadCardView&) = delete;
  ~DaoMiniDaoDownloadCardView() override;

  bool HasActiveDownloadsForTesting() const;
  void CancelDownloadForTesting(int id);

  // download::AllDownloadItemNotifier::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  struct ActiveDownload {
    int id = -1;
    std::u16string name;
    int percent = -1;
    std::string speed;
  };

  std::vector<ActiveDownload> BuildActiveDownloads() const;
  bool IsDownloadFromThisWindow(download::DownloadItem* item) const;
  content::WebContents* GetActiveWebContents() const;
  void Refresh();
  bool VisibleRowsMatch(const std::vector<ActiveDownload>& downloads) const;
  void UpdateRows(const std::vector<ActiveDownload>& downloads);
  void RebuildRows(const std::vector<ActiveDownload>& downloads);
  void UpdateOverflow(const std::vector<ActiveDownload>& downloads);
  void CancelDownload(int id);

  static std::string FormatSpeed(int64_t bytes_per_sec);

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::View> rows_container_ = nullptr;
  raw_ptr<views::Label> overflow_label_ = nullptr;
  std::vector<int> active_download_ids_;
  std::vector<int> visible_download_ids_;
  std::unique_ptr<download::AllDownloadItemNotifier> download_notifier_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_MINI_DAO_DOWNLOAD_CARD_VIEW_H_
