// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_DOWNLOAD_BUTTON_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_DOWNLOAD_BUTTON_VIEW_H_

#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"

class Browser;

namespace download {
class DownloadItem;
}  // namespace download

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace dao {

// Result from the background file scan: path + icon + optional thumbnail.
struct FileIconEntry {
  FileIconEntry();
  FileIconEntry(FileIconEntry&&);
  FileIconEntry& operator=(FileIconEntry&&);
  ~FileIconEntry();

  base::FilePath path;
  gfx::ImageSkia icon;
  gfx::ImageSkia thumbnail;
  bool has_thumbnail = false;
};

class DaoDownloadButtonView
    : public views::View,
      public gfx::AnimationDelegate,
      public download::AllDownloadItemNotifier::Observer {
  METADATA_HEADER(DaoDownloadButtonView, views::View)

 public:
  explicit DaoDownloadButtonView(Browser* browser);

  // Used by the custom event targeter to decide hit-test area.
  bool is_expanded_for_hit_test() const {
    return is_expanded_ || GetActiveItemCount() > 0;
  }
  DaoDownloadButtonView(const DaoDownloadButtonView&) = delete;
  DaoDownloadButtonView& operator=(const DaoDownloadButtonView&) = delete;
  ~DaoDownloadButtonView() override;

  // views::View:
  void AddedToWidget() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnDragDone() override;
  void OnPaint(gfx::Canvas* canvas) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // download::AllDownloadItemNotifier::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override;

 private:
  static constexpr int kMaxFileItems = 4;
  static constexpr int kFileItemHeight = 48;
  static constexpr int kActiveItemHeight = 40;
  static constexpr int kIconButtonSize = 32;
  static constexpr int kIconButtonRadius = 8;
  static constexpr int kFileIconSize = 16;
  static constexpr int kThumbnailSize = 40;
  static constexpr int kThumbnailRadius = 4;
  static constexpr int kDragThreshold = 5;
  static constexpr int kProgressBarHeight = 3;

  struct FileItemViews {
    raw_ptr<views::View> row = nullptr;
    raw_ptr<views::ImageView> icon = nullptr;
    raw_ptr<views::Label> name_label = nullptr;
  };

  struct ActiveDownloadViews {
    raw_ptr<views::View> row = nullptr;
    raw_ptr<views::ImageView> icon = nullptr;
    raw_ptr<views::Label> name_label = nullptr;
    raw_ptr<views::View> progress_bar = nullptr;
    raw_ptr<views::View> progress_fill = nullptr;
    raw_ptr<views::Label> speed_label = nullptr;
    raw_ptr<views::ImageView> cancel_button = nullptr;
    raw_ptr<download::DownloadItem> item = nullptr;
  };

  void RefreshFileList();
  void OnFileListReady(std::vector<FileIconEntry> entries);
  void UpdateButtonBackground();
  void OnButtonClicked();
  void OnFileItemClicked(int index);
  void OnCancelClicked(download::DownloadItem* item);
  void SetExpanded(bool expanded);
  int HitTestFileItem(const gfx::Point& point) const;
  int HitTestActiveItem(const gfx::Point& point) const;
  int HitTestCancelButton(const gfx::Point& point) const;
  void StartExpandAnimation();
  void StartCollapseAnimation();
  int GetTargetFileListHeight() const;

  void TriggerFlyoutAnimation();
  void UpdateActiveDownloadUI();
  void RemoveActiveDownload(download::DownloadItem* item);
  static std::u16string FormatSpeed(int64_t bytes_per_sec);
  int GetActiveItemCount() const;

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> active_download_container_ = nullptr;
  raw_ptr<views::View> file_list_container_ = nullptr;
  raw_ptr<views::View> button_row_ = nullptr;

  std::vector<FileItemViews> file_items_;
  std::vector<ActiveDownloadViews> active_items_;
  std::vector<base::FilePath> recent_file_paths_;

  // Download observer.
  std::unique_ptr<download::AllDownloadItemNotifier> notifier_;

  bool is_expanded_ = false;
  bool is_hovered_ = false;
  bool is_dragging_ = false;

  // Animation state.
  gfx::LinearAnimation expand_animation_;
  bool is_animating_expand_ = false;  // true = expanding, false = collapsing
  int anim_start_height_ = 0;
  int anim_target_height_ = 0;
  int current_file_list_height_ = 0;

  // Drag tracking: index of file being dragged (-1 = none).
  int drag_file_index_ = -1;
  gfx::Point drag_press_pt_;

  base::WeakPtrFactory<DaoDownloadButtonView> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_DOWNLOAD_BUTTON_VIEW_H_
