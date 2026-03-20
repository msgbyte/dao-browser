// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_utility_section.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/bubble_anchor_util.h"
#include "chrome/browser/ui/page_info/page_info_dialog.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

#if BUILDFLAG(IS_MAC)
#include "dao/browser/ui/views/dao_native_share_mac.h"
#endif

namespace dao {

namespace {

constexpr int kUtilButtonSize = 56;
constexpr int kUtilIconSize = 18;
constexpr int kUtilCornerRadius = 10;
constexpr int kUtilFontSize = 10;
constexpr SkColor kIconColor = SkColorSetRGB(55, 55, 60);

enum class IconType { kShare, kQr, kLock, kMore };

// Paints a Lucide icon. Each icon is drawn at 18x18 to match Lucide's 24x24
// canonical grid scaled to the available area.
class IconPainterView : public views::View {
 public:
  explicit IconPainterView(IconType icon_type) : icon_type_(icon_type) {
    SetCanProcessEventsWithinSubtree(false);
    SetPreferredSize(gfx::Size(kUtilIconSize, kUtilIconSize));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.5f);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
    flags.setColor(kIconColor);

    // Scale factor: map Lucide's 24x24 viewport to our kUtilIconSize
    float s = kUtilIconSize / 24.0f;

    switch (icon_type_) {
      case IconType::kShare:
        DrawLucideShare(canvas, flags, s);
        break;
      case IconType::kQr:
        DrawLucideQrCode(canvas, flags, s);
        break;
      case IconType::kLock:
        DrawLucideShieldCheck(canvas, flags, s);
        break;
      case IconType::kMore:
        DrawLucideEllipsis(canvas, flags, s);
        break;
    }
  }

 private:
  // Lucide "share" icon: polyline path + arrow
  void DrawLucideShare(gfx::Canvas* canvas, cc::PaintFlags& flags, float s) {
    // Path: M4 12v8a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-8
    SkPath box;
    box.moveTo(4 * s, 12 * s);
    box.lineTo(4 * s, 20 * s);
    box.cubicTo(4 * s, 21.1f * s, 4.9f * s, 22 * s, 6 * s, 22 * s);
    box.lineTo(18 * s, 22 * s);
    box.cubicTo(19.1f * s, 22 * s, 20 * s, 21.1f * s, 20 * s, 20 * s);
    box.lineTo(20 * s, 12 * s);
    canvas->DrawPath(box, flags);
    // polyline: 16 6 12 2 8 6
    SkPath arrow;
    arrow.moveTo(16 * s, 6 * s);
    arrow.lineTo(12 * s, 2 * s);
    arrow.lineTo(8 * s, 6 * s);
    canvas->DrawPath(arrow, flags);
    // line: 12 2 -> 12 15
    canvas->DrawLine(gfx::PointF(12 * s, 2 * s),
                     gfx::PointF(12 * s, 15 * s), flags);
  }

  // Lucide "qr-code" icon
  void DrawLucideQrCode(gfx::Canvas* canvas, cc::PaintFlags& flags, float s) {
    // Top-left rect
    canvas->DrawRect(gfx::RectF(3 * s, 3 * s, 7 * s, 7 * s), flags);
    // Bottom-left rect
    canvas->DrawRect(gfx::RectF(3 * s, 14 * s, 7 * s, 7 * s), flags);
    // Top-right rect
    canvas->DrawRect(gfx::RectF(14 * s, 3 * s, 7 * s, 7 * s), flags);
    // Dots inside the three rects
    cc::PaintFlags fill = flags;
    fill.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRect(gfx::RectF(5.5f * s, 5.5f * s, 2 * s, 2 * s), fill);
    canvas->DrawRect(gfx::RectF(5.5f * s, 16.5f * s, 2 * s, 2 * s), fill);
    canvas->DrawRect(gfx::RectF(16.5f * s, 5.5f * s, 2 * s, 2 * s), fill);
    // Bottom-right pattern lines
    canvas->DrawLine(gfx::PointF(14 * s, 14 * s),
                     gfx::PointF(14 * s, 17 * s), flags);
    canvas->DrawLine(gfx::PointF(14 * s, 17 * s),
                     gfx::PointF(17 * s, 17 * s), flags);
    canvas->DrawLine(gfx::PointF(17 * s, 14 * s),
                     gfx::PointF(21 * s, 14 * s), flags);
    canvas->DrawLine(gfx::PointF(21 * s, 14 * s),
                     gfx::PointF(21 * s, 17 * s), flags);
    canvas->DrawLine(gfx::PointF(17 * s, 20 * s),
                     gfx::PointF(21 * s, 20 * s), flags);
  }

  // Lucide "shield-check" icon
  void DrawLucideShieldCheck(gfx::Canvas* canvas, cc::PaintFlags& flags,
                             float s) {
    // Shield outline: M20 13c0 5-3.5 7.5-8 8.5-4.5-1-8-3.5-8-8.5V5l8-3 8 3z
    SkPath shield;
    shield.moveTo(20 * s, 13 * s);
    shield.cubicTo(20 * s, 18 * s, 16.5f * s, 20.5f * s, 12 * s, 21.5f * s);
    shield.cubicTo(7.5f * s, 20.5f * s, 4 * s, 18 * s, 4 * s, 13 * s);
    shield.lineTo(4 * s, 5 * s);
    shield.lineTo(12 * s, 2 * s);
    shield.lineTo(20 * s, 5 * s);
    shield.close();
    canvas->DrawPath(shield, flags);
    // Check mark: M9 12l2 2 4-4
    SkPath check;
    check.moveTo(9 * s, 12 * s);
    check.lineTo(11 * s, 14 * s);
    check.lineTo(15 * s, 10 * s);
    canvas->DrawPath(check, flags);
  }

  // Lucide "ellipsis" icon: three circles
  void DrawLucideEllipsis(gfx::Canvas* canvas, cc::PaintFlags& /*flags*/,
                          float s) {
    cc::PaintFlags fill;
    fill.setAntiAlias(true);
    fill.setStyle(cc::PaintFlags::kFill_Style);
    fill.setColor(kIconColor);
    float r = 1.2f * s;
    canvas->DrawCircle(gfx::PointF(5 * s, 12 * s), r, fill);
    canvas->DrawCircle(gfx::PointF(12 * s, 12 * s), r, fill);
    canvas->DrawCircle(gfx::PointF(19 * s, 12 * s), r, fill);
  }

  IconType icon_type_;
};

// A utility button with an icon child view and a text label.
class UtilityButton : public views::Button {
 public:
  UtilityButton(const std::u16string& label_text,
                IconType icon_type,
                views::Button::PressedCallback callback)
      : Button(std::move(callback)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(6, 0), 2));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);

    AddChildView(static_cast<views::View*>(
        std::make_unique<IconPainterView>(icon_type).release()));

    auto* label =
        AddChildView(std::make_unique<views::Label>(label_text));
    label->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL,
                                      kUtilFontSize,
                                      gfx::Font::Weight::NORMAL));
    label->SetEnabledColor(SkColorSetRGB(100, 100, 100));
    label->SetCanProcessEventsWithinSubtree(false);

    SetPreferredSize(gfx::Size(kUtilButtonSize, kUtilButtonSize));
    SetInstallFocusRingOnFocus(false);
    SetAccessibleName(label_text);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    Button::OnMouseEntered(event);
    SetBackground(views::CreateRoundedRectBackground(
        SkColorSetARGB(15, 0, 0, 0), kUtilCornerRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }
};

}  // namespace

BEGIN_METADATA(DaoControlCenterUtilitySection)
END_METADATA

DaoControlCenterUtilitySection::DaoControlCenterUtilitySection(
    DaoControlCenterPopup* popup)
    : popup_(popup) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(8, 0), 0));
  layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"Share", IconType::kShare,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnShareClicked,
              base::Unretained(this)))
          .release()));

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"QR Code", IconType::kQr,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnQrClicked,
              base::Unretained(this)))
          .release()));

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"Security", IconType::kLock,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnLockClicked,
              base::Unretained(this)))
          .release()));

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"More", IconType::kMore,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnMoreClicked,
              base::Unretained(this)))
          .release()));
}

DaoControlCenterUtilitySection::~DaoControlCenterUtilitySection() = default;

void DaoControlCenterUtilitySection::OnShareClicked() {
#if BUILDFLAG(IS_MAC)
  if (!popup_ || !popup_->browser()) {
    return;
  }
  auto* web_contents =
      popup_->browser()->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }
  std::string url = web_contents->GetVisibleURL().spec();
  std::string title = web_contents->GetTitle().empty()
                          ? url
                          : base::UTF16ToUTF8(web_contents->GetTitle());

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(popup_->browser());
  if (!browser_view || !browser_view->GetWidget()) {
    return;
  }
  gfx::NativeView native_view = browser_view->GetWidget()->GetNativeView();
  gfx::Rect anchor_rect = GetBoundsInScreen();
  dao::ShowNativeShareMac(url, title, native_view, anchor_rect);
#endif
}

void DaoControlCenterUtilitySection::OnQrClicked() {
  if (popup_) {
    popup_->ShowQrView();
  }
}

void DaoControlCenterUtilitySection::OnLockClicked() {
  if (!popup_ || !popup_->browser()) {
    return;
  }
  auto* web_contents =
      popup_->browser()->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }
  ShowPageInfoDialog(web_contents, PageInfoClosingCallback(),
                     bubble_anchor_util::Anchor::kLocationBar);
  if (popup_) {
    popup_->Hide();
  }
}

void DaoControlCenterUtilitySection::OnMoreClicked() {
  if (popup_) {
    popup_->ShowMoreMenu();
  }
}

}  // namespace dao
