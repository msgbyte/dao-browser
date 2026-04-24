// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_SIDEBAR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_SIDEBAR_VIEW_H_

#include <set>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/drop_data.h"
#include "third_party/blink/public/common/input/web_gesture_event.h"
#include "third_party/blink/public/common/page/drag_operation.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/controls/resize_area_delegate.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class Button;
class WebView;
}

namespace dao {

class DaoSidebarView : public views::View,
                       public gfx::AnimationDelegate,
                       public views::ResizeAreaDelegate,
                       public ui::ImplicitAnimationObserver,
                       public ui::NativeThemeObserver,
                       public content::WebContentsDelegate {
  METADATA_HEADER(DaoSidebarView, views::View)

 public:
  static constexpr int kDefaultWidth = 212;
  static constexpr int kCollapsedWidth = 6;
  static constexpr int kMinWidth = 150;
  static constexpr int kMaxWidth = 400;
  static constexpr int kResizeAreaWidth = 6;

  explicit DaoSidebarView(Browser* browser);
  DaoSidebarView(const DaoSidebarView&) = delete;
  DaoSidebarView& operator=(const DaoSidebarView&) = delete;
  ~DaoSidebarView() override;

  void ToggleCollapsed();
  bool collapsed() const { return collapsed_; }

  // Returns the header row and toggle button bounds in the sidebar's own
  // coordinate space (caller adds sidebar->bounds().origin() for BrowserView).
  gfx::Rect header_bounds_in_sidebar() const;
  gfx::Rect toggle_button_bounds_in_sidebar() const;

  void ShowOmniboxPopup();
  void HideOmniboxPopup();

  // Push new-tab button highlight state to the sidebar WebUI.
  void SetNewTabButtonHighlight(bool highlighted);

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

  // views::ResizeAreaDelegate:
  void OnResize(int resize_amount, bool done_resizing) override;

  // gfx::AnimationDelegate (used only for resize):
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsCompleted() override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

  // views::View:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

  // content::WebContentsDelegate:
  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;
  bool CanDragEnter(content::WebContents* source,
                    const content::DropData& data,
                    blink::DragOperationsMask operations_allowed) override;
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;
  bool PreHandleGestureEvent(
      content::WebContents* source,
      const blink::WebGestureEvent& event) override;

  // WebUI sidebar management
  bool use_webui() const { return true; }
  void SetWebUIDropInsertIndex(int index) { webui_drop_insert_index_ = index; }
  void StartFileDrag(const base::FilePath& path);

  // views::View (drop target):
  bool GetDropFormats(
      int* formats,
      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  void OnDragEntered(const ui::DropTargetEvent& event) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;

 private:
  raw_ptr<Browser> browser_;
  raw_ptr<views::View> inner_container_ = nullptr;
  raw_ptr<views::View> header_row_ = nullptr;
  raw_ptr<views::Button> toggle_button_ = nullptr;
  raw_ptr<views::ResizeArea> resize_area_ = nullptr;
  raw_ptr<views::View> drop_overlay_ = nullptr;
  raw_ptr<views::WebView> sidebar_web_view_ = nullptr;

  void AnimateLayerSlide(int old_width, int new_width);
  void EnsureWebUILoaded();
  void DoStartFileDrag(const base::FilePath& path);
  void ApplyTheme();
  static void SchedulePaintRecursive(views::View* view);

  bool webui_loaded_ = false;
  bool collapsed_ = false;
  bool auto_expanded_ = false;
  bool is_drop_target_active_ = false;
  bool drop_auto_expanded_ = false;
  bool is_resizing_ = false;
  int user_width_ = kDefaultWidth;
  int current_width_ = kDefaultWidth;
  int start_width_ = kDefaultWidth;
  int target_width_ = kDefaultWidth;
  int resize_start_width_ = kDefaultWidth;
  gfx::LinearAnimation collapse_animation_;

  int drop_target_index_ = -1;   // Tab model index where file will be inserted
  int webui_drop_insert_index_ = -1;  // Drop index set by WebUI JS

  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};

  base::WeakPtrFactory<DaoSidebarView> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_SIDEBAR_VIEW_H_
