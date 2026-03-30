// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/split/dao_split_pane_view.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/tab_contents/chrome_web_contents_view_focus_helper.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {

constexpr int kPaneHeaderHeight = 28;
constexpr int kPaneHeaderTopInset = 4;

class PaneHeaderButton : public views::Button {
 public:
  PaneHeaderButton(dao::LucideIcon icon,
                   const std::u16string& accessible_name,
                   PressedCallback callback)
      : Button(std::move(callback)),
        icon_(icon) {
    SetPreferredSize(
        gfx::Size(kPaneHeaderButtonSize, kPaneHeaderButtonSize));
    SetInstallFocusRingOnFocus(false);
    SetAccessibleName(accessible_name);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    Button::OnMouseEntered(event);
    hovered_ = true;
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    hovered_ = false;
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (hovered_) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kFill_Style);
      flags.setColor(kPaneHeaderButtonHover);
      canvas->DrawRoundRect(
          gfx::RectF(0, 0, width(), height()), kPaneHeaderButtonRadius, flags);
    }

    gfx::RectF icon_rect(2, 2, width() - 4, height() - 4);
    DrawLucideIcon(canvas, icon_, icon_rect, kPaneHeaderButtonIcon);
  }

 private:
  const dao::LucideIcon icon_;
  bool hovered_ = false;
};

class PaneRearrangeButton : public PaneHeaderButton {
 public:
  PaneRearrangeButton(DaoSplitPaneView* pane, DaoSplitView* split_view)
      : PaneHeaderButton(LucideIcon::kGripHorizontal, u"Rearrange pane",
                         PressedCallback()),
        pane_(pane),
        split_view_(split_view) {}

  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!pane_ || !split_view_ || !pane_->web_contents()) {
      return false;
    }

    split_view_->SetActivePane(pane_);
    split_view_->BeginPaneRearrange(pane_->web_contents());
    pane_->SetHeaderDragActive(true);
    dragging_ = true;
    UpdateDrag(event);
    return true;
  }

  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if (!dragging_ || !split_view_) {
      return false;
    }

    UpdateDrag(event);
    return true;
  }

  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (!dragging_ || !split_view_) {
      return;
    }

    UpdateDrag(event);
    split_view_->EndPaneRearrange(last_drag_point_);
    dragging_ = false;
    if (pane_)
      pane_->SetHeaderDragActive(false);
  }

  void OnMouseCaptureLost() override {
    if (!dragging_ || !split_view_) {
      return;
    }

    split_view_->EndPaneRearrange(last_drag_point_);
    dragging_ = false;
    if (pane_)
      pane_->SetHeaderDragActive(false);
  }

 private:
  void UpdateDrag(const ui::MouseEvent& event) {
    gfx::Point point = event.location();
    views::View::ConvertPointToTarget(this, split_view_, &point);
    last_drag_point_ = point;
    split_view_->UpdatePaneRearrange(point);
  }

  raw_ptr<DaoSplitPaneView> pane_;
  raw_ptr<DaoSplitView> split_view_;
  bool dragging_ = false;
  gfx::Point last_drag_point_;
};

}  // namespace

BEGIN_METADATA(DaoSplitPaneView)
END_METADATA

DaoSplitPaneView::DaoSplitPaneView(Browser* browser,
                                   DaoSplitView* split_view,
                                   int pane_index)
    : browser_(browser), split_view_(split_view),
      glow_animation_(base::Milliseconds(200), 60, this) {
  static_cast<void>(pane_index);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);

  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  // Create per-pane address bar.
  address_bar_ = AddChildView(
      std::make_unique<DaoAddressBarView>(browser));

  // Create the web contents view.
  contents_web_view_ = AddChildView(
      std::make_unique<ContentsWebView>(browser->profile()));

  auto header_container = std::make_unique<views::View>();
  auto* header_layout = header_container->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(3, 4),
          2));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  header_container->SetBackground(views::CreateRoundedRectBackground(
      kPaneHeaderBackground, kPaneHeaderCornerRadius));
  header_container->SetPaintToLayer();
  header_container->layer()->SetFillsBoundsOpaquely(false);
  header_container->layer()->SetOpacity(0.0f);
  header_container_ = AddChildView(std::move(header_container));

  header_container_->AddChildView(static_cast<views::View*>(
      std::make_unique<PaneRearrangeButton>(this, split_view_).release()));
  header_container_->AddChildView(static_cast<views::View*>(
      std::make_unique<PaneHeaderButton>(
          LucideIcon::kSquareArrowDownLeft, u"Unsplit pane",
          base::BindRepeating(
              [](DaoSplitPaneView* pane) {
                if (pane && pane->split_view() && pane->web_contents()) {
                  pane->split_view()->UnsplitKeepingPane(
                      pane->web_contents());
                }
              },
              base::Unretained(this)))
          .release()));
}

DaoSplitPaneView::~DaoSplitPaneView() = default;

void DaoSplitPaneView::SetWebContents(content::WebContents* web_contents) {
  if (web_contents_ == web_contents)
    return;

  if (web_contents_ && contents_visible_) {
    web_contents_->WasHidden();
  }

  web_contents_ = web_contents;
  contents_web_view_->SetWebContents(web_contents);

  if (web_contents && contents_visible_) {
    web_contents->WasShown();
  }

  UpdateAddressBar();
}

void DaoSplitPaneView::SetContentsVisible(bool visible) {
  if (contents_visible_ == visible) {
    return;
  }

  contents_visible_ = visible;
  if (!web_contents_) {
    return;
  }

  if (contents_visible_) {
    EnsureContentsAttached();
    web_contents_->WasShown();
  } else {
    web_contents_->WasHidden();
  }
}

void DaoSplitPaneView::EnsureContentsAttached(bool force_reattach) {
  if (!contents_web_view_) {
    return;
  }

  if (!force_reattach && contents_web_view_->web_contents() == web_contents_) {
    return;
  }

  if (force_reattach && contents_web_view_->web_contents() == web_contents_) {
    contents_web_view_->SetWebContents(nullptr);
  }

  contents_web_view_->SetWebContents(web_contents_);
}

content::WebContents* DaoSplitPaneView::TakeWebContents() {
  content::WebContents* wc = web_contents_;
  web_contents_ = nullptr;
  contents_visible_ = false;
  if (contents_web_view_) {
    contents_web_view_->SetWebContents(nullptr);
  }
  return wc;
}

void DaoSplitPaneView::SetActive(bool active) {
  if (is_active_ == active)
    return;
  is_active_ = active;
  SetBorder(nullptr);

  glow_target_active_ = active;
  glow_animation_.Stop();
  glow_animation_.Start();
}

void DaoSplitPaneView::UpdateAddressBar() {
  if (!web_contents_ || !address_bar_)
    return;
  // The address bar observes the active tab via TabStripModel, but in split
  // mode each pane's address bar should show its own pane's URL.
  // For now we trigger a repaint; full per-pane URL tracking is handled
  // by the address bar's WebContentsObserver when we wire it up.
}

void DaoSplitPaneView::Layout(PassKey) {
  int addr_height = DaoAddressBarView::kBarHeight;
  int w = width();
  int h = height();

  if (address_bar_) {
    address_bar_->SetBounds(0, 0, w, addr_height);
    address_bar_->SetVisible(true);
  }

  if (contents_web_view_) {
    contents_web_view_->SetBounds(0, addr_height, w,
                                   std::max(0, h - addr_height));
  }

  if (header_container_) {
    const gfx::Size header_size = header_container_->GetPreferredSize();
    header_container_->SetBounds(
        std::max(0, (w - header_size.width()) / 2), kPaneHeaderTopInset,
        std::min(w, header_size.width()), kPaneHeaderHeight);
  }
}

void DaoSplitPaneView::OnBoundsChanged(const gfx::Rect& previous_bounds) {
  View::OnBoundsChanged(previous_bounds);
}

bool DaoSplitPaneView::OnMousePressed(const ui::MouseEvent& event) {
  OnPaneFocused();
  return false;  // Let event propagate.
}

void DaoSplitPaneView::OnPaneFocused() {
  if (split_view_) {
    split_view_->SetActivePane(this);
  }
}

void DaoSplitPaneView::SetHeaderHovered(bool hovered) {
  if (header_hovered_ == hovered)
    return;
  header_hovered_ = hovered;
  AnimateHeaderVisibility(hovered || header_drag_active_);
}

void DaoSplitPaneView::SetHeaderDragActive(bool active) {
  header_drag_active_ = active;
  if (active) {
    AnimateHeaderVisibility(true);
  } else if (!header_hovered_) {
    AnimateHeaderVisibility(false);
  }
}

void DaoSplitPaneView::AnimateHeaderVisibility(bool visible) {
  if (!header_container_ || !web_contents_)
    return;

  float target_opacity = visible ? 1.0f : 0.0f;
  if (header_container_->layer()->GetTargetOpacity() == target_opacity)
    return;

  ui::ScopedLayerAnimationSettings settings(
      header_container_->layer()->GetAnimator());
  settings.SetTransitionDuration(base::Milliseconds(150));
  settings.SetTweenType(visible ? gfx::Tween::EASE_OUT : gfx::Tween::EASE_IN);
  header_container_->layer()->SetOpacity(target_opacity);
}

void DaoSplitPaneView::AnimationProgressed(const gfx::Animation* animation) {
  double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT,
                                         animation->GetCurrentValue());
  glow_opacity_ = glow_target_active_
                       ? static_cast<float>(t)
                       : static_cast<float>(1.0 - t);
  SchedulePaint();
}

void DaoSplitPaneView::AnimationEnded(const gfx::Animation* animation) {
  glow_opacity_ = glow_target_active_ ? 1.0f : 0.0f;
  SchedulePaint();
}

void DaoSplitPaneView::OnPaint(gfx::Canvas* canvas) {
  View::OnPaint(canvas);

  if (glow_opacity_ <= 0.0f)
    return;

  const gfx::Rect bounds = GetLocalBounds();
  const float r = static_cast<float>(kContentCornerRadius);

  // Outer glow.
  {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(static_cast<float>(kActivePaneGlowRadius));
    flags.setColor(SkColorSetA(
        kActivePaneGlow,
        static_cast<int>(SkColorGetA(kActivePaneGlow) * glow_opacity_)));
    float inset = static_cast<float>(kActivePaneGlowRadius) / 2.0f;
    gfx::RectF glow_rect(bounds);
    glow_rect.Inset(inset);
    canvas->DrawRoundRect(glow_rect, r, flags);
  }

  // Border.
  {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(static_cast<float>(kActivePaneBorderWidth));
    flags.setColor(SkColorSetA(
        kActivePaneBorder,
        static_cast<int>(SkColorGetA(kActivePaneBorder) * glow_opacity_)));
    float inset = static_cast<float>(kActivePaneBorderWidth) / 2.0f;
    gfx::RectF border_rect(bounds);
    border_rect.Inset(inset);
    canvas->DrawRoundRect(border_rect, r, flags);
  }
}

}  // namespace dao
