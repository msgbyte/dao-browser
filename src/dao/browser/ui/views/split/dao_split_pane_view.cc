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
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {

constexpr int kPaneHeaderHeight = 28;
constexpr int kPaneHeaderPadding = 6;
constexpr int kPaneHeaderButtonSize = 20;
constexpr int kPaneHeaderSpacing = 4;
constexpr int kPaneHeaderTopInset = 5;
constexpr int kPaneHeaderCornerRadius = 10;

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
      flags.setColor(SkColorSetARGB(24, 255, 255, 255));
      canvas->DrawRoundRect(
          gfx::RectF(0, 0, width(), height()), kPaneHeaderCornerRadius, flags);
    }

    gfx::RectF icon_rect(2, 2, width() - 4, height() - 4);
    DrawLucideIcon(canvas, icon_, icon_rect, dao::kTextSecondary);
  }

 private:
  const dao::LucideIcon icon_;
  bool hovered_ = false;
};

class PaneRearrangeButton : public PaneHeaderButton {
 public:
  PaneRearrangeButton(DaoSplitPaneView* pane, DaoSplitView* split_view)
      : PaneHeaderButton(LucideIcon::kEllipsis, u"Rearrange pane",
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
    : browser_(browser), split_view_(split_view) {
  static_cast<void>(pane_index);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);

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
          gfx::Insets::VH(kPaneHeaderPadding, kPaneHeaderPadding),
          kPaneHeaderSpacing));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  header_container->SetBackground(views::CreateRoundedRectBackground(
      SkColorSetARGB(220, 40, 34, 48), kPaneHeaderCornerRadius));
  header_container->SetPaintToLayer();
  header_container->layer()->SetFillsBoundsOpaquely(false);
  header_container->SetVisible(false);
  header_container_ = AddChildView(std::move(header_container));

  header_container_->AddChildView(static_cast<views::View*>(
      std::make_unique<PaneRearrangeButton>(this, split_view_).release()));
  header_container_->AddChildView(static_cast<views::View*>(
      std::make_unique<PaneHeaderButton>(
          LucideIcon::kShare, u"Pop out pane",
          base::BindRepeating(
              [](DaoSplitPaneView* pane) {
                if (pane && pane->split_view() && pane->web_contents()) {
                  pane->split_view()->PopOutPane(pane->web_contents());
                }
              },
              base::Unretained(this)))
          .release()));
  header_container_->AddChildView(static_cast<views::View*>(
      std::make_unique<PaneHeaderButton>(
          LucideIcon::kX, u"Unsplit pane",
          base::BindRepeating(
              [](DaoSplitPaneView* pane) {
                if (pane && pane->split_view() && pane->web_contents()) {
                  pane->split_view()->ClosePane(pane->web_contents());
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

void DaoSplitPaneView::SetActive(bool active) {
  if (is_active_ == active)
    return;
  is_active_ = active;

  if (active) {
    SetBorder(views::CreateRoundedRectBorder(
        kActivePaneBorderWidth, kContentCornerRadius, kActivePaneBorder));
  } else {
    SetBorder(nullptr);
  }
  SchedulePaint();
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
  if (header_container_) {
    bool should_show = (hovered || header_drag_active_) && web_contents_;
    header_container_->SetVisible(should_show);
  }
}

void DaoSplitPaneView::SetHeaderDragActive(bool active) {
  header_drag_active_ = active;
  if (header_container_) {
    if (active && web_contents_) {
      header_container_->SetVisible(true);
    } else if (!active && !header_hovered_) {
      header_container_->SetVisible(false);
    }
  }
}

}  // namespace dao
