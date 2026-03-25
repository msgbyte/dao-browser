// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_tab_item_view.h"

#include "cc/paint/paint_flags.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {
constexpr int kItemHeight = 40;
constexpr int kCornerRadius = 12;
constexpr int kAudioButtonSize = 18;
constexpr int kCloseButtonSize = 20;
}  // namespace

// A simple view that draws a Lucide X icon (not a Button, to avoid nested
// button issues).  Hit-testing is handled by the parent DaoTabItemView.
// Uses its own compositing layer so the hover highlight renders above the
// parent Button's InkDrop.
class DaoCloseIconView : public views::View {
  METADATA_HEADER(DaoCloseIconView, views::View)

 public:
  DaoCloseIconView() {
    SetPreferredSize(gfx::Size(kCloseButtonSize, kCloseButtonSize));
    // This view is purely visual — all hit-testing and interaction is handled
    // by the parent DaoTabItemView.  Without this, the view intercepts
    // OnMouseMoved events, preventing the parent from driving hover state.
    SetCanProcessEventsWithinSubtree(false);
  }

  void SetHovered(bool hovered) {
    if (hovered_ == hovered) {
      return;
    }
    hovered_ = hovered;
    SchedulePaint();
  }

  void SetPressed(bool pressed) {
    if (pressed_ == pressed) {
      return;
    }
    pressed_ = pressed;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (pressed_ || hovered_) {
      cc::PaintFlags bg_flags;
      bg_flags.setAntiAlias(true);
      bg_flags.setStyle(cc::PaintFlags::kFill_Style);
      bg_flags.setColor(pressed_ ? SkColorSetA(SK_ColorWHITE, 0x33)   // white 20%
                                 : SkColorSetA(SK_ColorWHITE, 0x22));  // white 13%
      canvas->DrawRoundRect(
          gfx::RectF(0, 0, kCloseButtonSize, kCloseButtonSize), 5, bg_flags);
    }
    gfx::RectF icon_rect(0, 0, kCloseButtonSize, kCloseButtonSize);
    icon_rect.Inset(4);
    SkColor icon_color =
        (hovered_ || pressed_) ? dao::kTextPrimary : dao::kTextMuted;
    DrawLucideIcon(canvas, LucideIcon::kX, icon_rect, icon_color);
  }

 private:
  bool hovered_ = false;
  bool pressed_ = false;
};

// Custom button that draws a Lucide volume icon.
class DaoAudioButton : public views::Button {
  METADATA_HEADER(DaoAudioButton, views::Button)

 public:
  DaoAudioButton(PressedCallback callback, bool is_muted)
      : Button(std::move(callback)), is_muted_(is_muted) {
    SetPreferredSize(gfx::Size(kAudioButtonSize, kAudioButtonSize));
    SetInstallFocusRingOnFocus(false);
    SetAccessibleName(is_muted ? u"Unmute tab" : u"Mute tab");
  }

  void SetMuted(bool muted) {
    if (is_muted_ == muted) {
      return;
    }
    is_muted_ = muted;
    SetAccessibleName(is_muted_ ? u"Unmute tab" : u"Mute tab");
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    // No background — transparent.
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    gfx::RectF icon_rect(0, 0, kAudioButtonSize, kAudioButtonSize);
    // Shrink to leave some padding.
    icon_rect.Inset(2);
    LucideIcon icon =
        is_muted_ ? LucideIcon::kVolumeX : LucideIcon::kVolume2;
    DrawLucideIcon(canvas, icon, icon_rect, dao::kTextSecondary);
  }

 private:
  bool is_muted_ = false;
};

BEGIN_METADATA(DaoAudioButton)
END_METADATA

BEGIN_METADATA(DaoCloseIconView)
END_METADATA

BEGIN_METADATA(DaoTabItemView)
END_METADATA

DaoTabItemView::DaoTabItemView(Browser* browser,
                               content::WebContents* contents,
                               int model_index,
                               bool is_active,
                               base::RepeatingClosure on_click,
                               base::RepeatingClosure on_close)
    : Button(std::move(on_click)),
      browser_(browser),
      model_index_(model_index),
      close_callback_(std::move(on_close)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, 10), 10));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  favicon_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_->SetImageSize(gfx::Size(16, 16));
  UpdateFavicon(contents);

  // Audio indicator button — between favicon and title, hidden by default.
  auto audio_btn = std::make_unique<DaoAudioButton>(
      base::BindRepeating(&DaoTabItemView::OnAudioButtonClicked,
                          base::Unretained(this)),
      false);
  audio_button_ = AddChildView(std::move(audio_btn));
  audio_button_->SetVisible(false);

  title_label_ = AddChildView(std::make_unique<views::Label>(
      contents ? contents->GetTitle() : u"New Tab"));
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_->SetSubpixelRenderingEnabled(false);
  title_label_->SetEnabledColor(
      is_active ? dao::kTextPrimary : dao::kTextSecondary);
  title_label_->SetFontList(gfx::FontList(
      {"sans-serif"}, gfx::Font::FontStyle::NORMAL, 13,
      gfx::Font::Weight::SEMIBOLD));
  layout->SetFlexForView(title_label_, 1);

  // Close icon, hidden by default.  Click is detected in OnMousePressed.
  close_button_ = AddChildView(std::make_unique<DaoCloseIconView>());
  close_button_->SetVisible(false);

  // Keep hover state when mouse moves onto child views (e.g. close button).
  SetNotifyEnterExitOnChild(true);

  if (is_active) {
    SetBackground(views::CreateRoundedRectBackground(
        dao::kActiveTabBackground, kCornerRadius));
  }

  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::Get(this)->SetBaseColor(dao::kInkDropBase);
  views::InkDrop::Get(this)->SetVisibleOpacity(dao::kInkDropOpacity);
  views::InkDrop::Get(this)->SetCreateHighlightCallback(base::BindRepeating(
      [](views::Button* host) {
        auto highlight = std::make_unique<views::InkDropHighlight>(
            gfx::SizeF(host->size()), dao::kInkDropBase);
        highlight->set_visible_opacity(dao::kInkDropOpacity);
        return highlight;
      },
      this));
  views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                 kCornerRadius);
  SetInstallFocusRingOnFocus(false);

  SetPreferredSize(gfx::Size(0, kItemHeight));
  std::u16string accessible_title =
      contents ? contents->GetTitle() : u"New Tab";
  if (accessible_title.empty()) {
    accessible_title = u"Tab";
  }
  SetAccessibleName(accessible_title);

  // Initialize audio state.
  if (contents) {
    UpdateAudioState(contents);
  }
}

DaoTabItemView::~DaoTabItemView() = default;

void DaoTabItemView::UpdateTab(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  // Update title.
  std::u16string title = contents->GetTitle();
  if (title.empty()) {
    title = u"New Tab";
  }
  if (title_label_) {
    title_label_->SetText(title);
  }
  SetAccessibleName(title);

  // Update favicon.
  UpdateFavicon(contents);

  // Update audio.
  UpdateAudioState(contents);
}

void DaoTabItemView::UpdateAudioState(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  bool audible = contents->IsCurrentlyAudible();
  bool muted = contents->IsAudioMuted();

  // Show audio button if tab is audible or muted.
  bool should_show = audible || muted;
  is_audible_ = audible;
  is_muted_ = muted;

  if (audio_button_) {
    audio_button_->SetVisible(should_show);
    audio_button_->SetMuted(muted);
  }
}

void DaoTabItemView::OnMouseEntered(const ui::MouseEvent& event) {
  Button::OnMouseEntered(event);
  if (close_button_) {
    close_button_->SetVisible(true);
  }
}

void DaoTabItemView::OnMouseExited(const ui::MouseEvent& event) {
  Button::OnMouseExited(event);
  if (close_button_) {
    static_cast<DaoCloseIconView*>(close_button_.get())->SetHovered(false);
    static_cast<DaoCloseIconView*>(close_button_.get())->SetPressed(false);
    close_button_->SetVisible(false);
  }
  close_button_pressed_ = false;
}

void DaoTabItemView::OnMouseMoved(const ui::MouseEvent& event) {
  Button::OnMouseMoved(event);
  if (close_button_ && close_button_->GetVisible()) {
    bool over_close = IsPointInCloseButton(event.location());
    static_cast<DaoCloseIconView*>(close_button_.get())->SetHovered(over_close);
  }
}

bool DaoTabItemView::OnMousePressed(const ui::MouseEvent& event) {
  // If press lands on the close icon, track pressed state but don't close yet.
  // Close fires on release for a complete click.
  if (IsPointInCloseButton(event.location())) {
    close_button_pressed_ = true;
    static_cast<DaoCloseIconView*>(close_button_.get())->SetPressed(true);
    return true;
  }
  return Button::OnMousePressed(event);
}

void DaoTabItemView::OnMouseReleased(const ui::MouseEvent& event) {
  if (close_button_pressed_) {
    close_button_pressed_ = false;
    static_cast<DaoCloseIconView*>(close_button_.get())->SetPressed(false);
    if (IsPointInCloseButton(event.location())) {
      OnCloseClicked();
    }
    return;
  }
  Button::OnMouseReleased(event);
}

bool DaoTabItemView::IsPointInCloseButton(const gfx::Point& point) const {
  if (!close_button_ || !close_button_->GetVisible()) {
    return false;
  }
  return close_button_->GetMirroredBounds().Contains(point);
}

void DaoTabItemView::OnCloseClicked() {
  if (close_callback_) {
    close_callback_.Run();
  }
}

void DaoTabItemView::OnAudioButtonClicked() {
  if (!browser_) {
    return;
  }
  TabStripModel* model = browser_->tab_strip_model();
  if (model_index_ < 0 || model_index_ >= model->count()) {
    return;
  }
  content::WebContents* contents = model->GetWebContentsAt(model_index_);
  if (!contents) {
    return;
  }
  bool currently_muted = contents->IsAudioMuted();
  contents->SetAudioMuted(!currently_muted);
  UpdateAudioState(contents);
}

void DaoTabItemView::UpdateFavicon(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  gfx::Image favicon = favicon::TabFaviconFromWebContents(contents);
  if (!favicon.IsEmpty()) {
    gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
        *favicon.ToImageSkia(), skia::ImageOperations::RESIZE_BEST,
        gfx::Size(16, 16));
    favicon_->SetImage(resized);
  }
}

}  // namespace dao
