// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_system_dialog.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/color/color_variant.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/proposed_layout.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/window/dialog_delegate.h"

namespace dao {
namespace {

constexpr int kDaoDialogCornerRadius = 16;
constexpr int kDaoDialogButtonRadius = 8;
constexpr int kDaoShortcutGap = 8;
constexpr int kDaoShortcutBadgeHorizontalInset = 7;
constexpr int kDaoShortcutBadgeVerticalInset = 2;
constexpr int kDaoShortcutBadgeRadius = 6;

constexpr gfx::Insets kDaoDialogContentMargins =
    gfx::Insets::TLBR(18, 22, 16, 22);
constexpr gfx::Insets kDaoDialogTitleMargins =
    gfx::Insets::TLBR(20, 22, 8, 22);
constexpr gfx::Insets kDaoDialogButtonPadding = gfx::Insets::VH(7, 13);

SkColor DialogButtonBackground(ui::ButtonStyle style) {
  if (style == ui::ButtonStyle::kProminent) {
    return IsDarkMode() ? SkColorSetRGB(248, 250, 252)
                        : SkColorSetRGB(16, 24, 40);
  }
  return IsDarkMode() ? SkColorSetARGB(46, 255, 255, 255)
                      : SkColorSetARGB(18, 16, 24, 40);
}

SkColor DialogButtonText(ui::ButtonStyle style) {
  if (style == ui::ButtonStyle::kProminent) {
    return IsDarkMode() ? SkColorSetRGB(17, 24, 39)
                        : SkColorSetRGB(255, 255, 255);
  }
  return TextPrimary();
}

SkColor DialogButtonStroke(ui::ButtonStyle style) {
  if (style == ui::ButtonStyle::kProminent) {
    return IsDarkMode() ? SkColorSetARGB(56, 255, 255, 255)
                        : SkColorSetARGB(28, 15, 23, 42);
  }
  return IsDarkMode() ? SkColorSetARGB(36, 255, 255, 255)
                      : SkColorSetARGB(24, 15, 23, 42);
}

SkColor ShortcutBadgeBackground() {
  return IsDarkMode() ? SkColorSetARGB(34, 15, 23, 42)
                      : SkColorSetARGB(22, 255, 255, 255);
}

SkColor ShortcutBadgeText(ui::ButtonStyle style, bool enabled) {
  return SkColorSetA(DialogButtonText(style), enabled ? 190 : 90);
}

class DaoShortcutTextButton : public views::MdTextButton {
  METADATA_HEADER(DaoShortcutTextButton, views::MdTextButton)

 public:
  DaoShortcutTextButton(PressedCallback callback,
                        std::u16string_view text,
                        std::optional<DaoDialogShortcut> shortcut,
                        ui::ButtonStyle style)
      : views::MdTextButton(std::move(callback), text),
        shortcut_(std::move(shortcut)) {
    shortcut_badge_ = AddChildView(std::make_unique<views::Label>());
    shortcut_badge_->SetAutoColorReadabilityEnabled(false);
    shortcut_badge_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    shortcut_badge_->SetCanProcessEventsWithinSubtree(false);
    shortcut_badge_->GetViewAccessibility().SetIsIgnored(true);
    shortcut_badge_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(
        kDaoShortcutBadgeVerticalInset, kDaoShortcutBadgeHorizontalInset)));

    SetCustomPadding(kDaoDialogButtonPadding);
    SetCornerRadius(kDaoDialogButtonRadius);
    SetFocusRingCornerRadius(kDaoDialogButtonRadius);
    if (shortcut_) {
      shortcut_badge_->SetText(shortcut_->keycap);
      AddAccelerator(shortcut_->accelerator);
    }
    shortcut_badge_->SetVisible(shortcut_.has_value());
    views::MdTextButton::SetStyle(style);
    ApplyDaoStyle();
  }

  DaoShortcutTextButton(const DaoShortcutTextButton&) = delete;
  DaoShortcutTextButton& operator=(const DaoShortcutTextButton&) = delete;
  ~DaoShortcutTextButton() override = default;

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    if (!shortcut_ || accelerator != shortcut_->accelerator || !GetEnabled()) {
      return false;
    }
    NotifyClick(accelerator.ToKeyEvent());
    return true;
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size size =
        views::MdTextButton::CalculatePreferredSize(available_size);
    if (!shortcut_badge_->GetVisible()) {
      return size;
    }

    const gfx::Size badge_size = shortcut_badge_->GetPreferredSize({});
    size.Enlarge(kDaoShortcutGap + badge_size.width(), 0);
    size.SetToMax(gfx::Size(0, badge_size.height() + GetInsets().height()));
    return size;
  }

  gfx::Size GetMinimumSize() const override {
    gfx::Size size = views::MdTextButton::GetMinimumSize();
    if (!shortcut_badge_->GetVisible()) {
      return size;
    }

    const gfx::Size badge_size = shortcut_badge_->GetPreferredSize({});
    size.Enlarge(kDaoShortcutGap + badge_size.width(), 0);
    size.SetToMax(gfx::Size(0, badge_size.height() + GetInsets().height()));
    return size;
  }

  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override {
    if (!shortcut_badge_->GetVisible()) {
      return views::MdTextButton::CalculateProposedLayout(size_bounds);
    }

    views::ProposedLayout layout;
    if (!size_bounds.is_fully_bounded()) {
      layout.host_size = gfx::Size();
      return layout;
    }

    gfx::Rect content_bounds = GetLocalBounds();
    content_bounds.Inset(GetInsets());
    const gfx::Size badge_size = shortcut_badge_->GetPreferredSize({});

    gfx::Size image_size = image_container_view()->GetPreferredSize(
        views::SizeBounds(content_bounds.size()));
    image_size.SetToMin(content_bounds.size());
    if (!HasImage(GetVisualState())) {
      image_size = gfx::Size();
    }

    const int image_spacing =
        image_size.IsEmpty() ? 0 : GetImageLabelSpacing();
    const int label_available_width = std::max(
        0, content_bounds.width() - image_size.width() - image_spacing -
               kDaoShortcutGap - badge_size.width());
    const gfx::Size preferred_label_size =
        label()->GetPreferredSize(views::SizeBounds(label_available_width, {}));
    const gfx::Size label_size(
        std::min(label_available_width, preferred_label_size.width()),
        std::min(content_bounds.height(), preferred_label_size.height()));

    const int group_width = image_size.width() + image_spacing +
                            label_size.width() + kDaoShortcutGap +
                            badge_size.width();
    int x = content_bounds.x() + (content_bounds.width() - group_width) / 2;

    auto centered_y = [&content_bounds](int height) {
      return content_bounds.y() + (content_bounds.height() - height) / 2;
    };

    layout.child_layouts.emplace_back(
        const_cast<DaoShortcutTextButton*>(this)->image_container_view(),
        !image_size.IsEmpty(), gfx::Rect(x, centered_y(image_size.height()),
                                         image_size.width(),
                                         image_size.height()),
        views::SizeBounds());
    x += image_size.width() + image_spacing;

    layout.child_layouts.emplace_back(
        label(), label()->GetVisible(),
        gfx::Rect(x, centered_y(label_size.height()), label_size.width(),
                  label_size.height()),
        views::SizeBounds());
    x += label_size.width() + kDaoShortcutGap;

    layout.child_layouts.emplace_back(
        shortcut_badge_.get(), true,
        gfx::Rect(x, centered_y(badge_size.height()), badge_size.width(),
                  badge_size.height()),
        views::SizeBounds());
    layout.host_size =
        gfx::Size(size_bounds.width().value(), size_bounds.height().value());
    return layout;
  }

 protected:
  void UpdateColors() override {
    views::MdTextButton::UpdateColors();
    ApplyDaoStyle();
  }

 private:
  void ApplyDaoStyle() {
    if (!shortcut_badge_) {
      return;
    }

    const ui::ButtonStyle style = GetStyle();
    SetCornerRadius(kDaoDialogButtonRadius);
    SetFocusRingCornerRadius(kDaoDialogButtonRadius);
    SetCustomPadding(kDaoDialogButtonPadding);
    SetBgColorOverrideDeprecated(DialogButtonBackground(style));
    SetStrokeColorOverrideDeprecated(DialogButtonStroke(style));
    const SkColor text = DialogButtonText(style);
    views::LabelButton::SetTextColor(views::Button::STATE_NORMAL,
                                     ui::ColorVariant(text));
    views::LabelButton::SetTextColor(views::Button::STATE_HOVERED,
                                     ui::ColorVariant(text));
    views::LabelButton::SetTextColor(views::Button::STATE_PRESSED,
                                     ui::ColorVariant(text));
    views::LabelButton::SetTextColor(views::Button::STATE_DISABLED,
                                     ui::ColorVariant(SkColorSetA(text, 102)));
    shortcut_badge_->SetEnabledColor(
        ui::ColorVariant(ShortcutBadgeText(style, GetEnabled())));
    shortcut_badge_->SetBackground(views::CreateRoundedRectBackground(
        ShortcutBadgeBackground(), kDaoShortcutBadgeRadius));
  }

  std::optional<DaoDialogShortcut> shortcut_;
  raw_ptr<views::Label> shortcut_badge_ = nullptr;
};

BEGIN_METADATA(DaoShortcutTextButton)
END_METADATA

std::u16string PlatformAcceleratorPrefix() {
#if BUILDFLAG(IS_MAC)
  return u"Cmd";
#else
  return u"Ctrl";
#endif
}

}  // namespace

std::u16string PlatformShortcutKeycap(std::u16string_view key,
                                      bool include_shift) {
  std::u16string keycap = PlatformAcceleratorPrefix();
  if (include_shift) {
    keycap += u"+Shift";
  }
  keycap += u"+";
  keycap += key;
  return keycap;
}

void ConfigureDaoSystemDialog(views::DialogDelegate* delegate,
                              const DaoSystemDialogOptions& options) {
  if (!delegate) {
    return;
  }

  delegate->SetUseDaoSystemDialogStyle(true);
  delegate->set_center_in_web_contents(options.center_in_web_contents);
  delegate->set_corner_radius(kDaoDialogCornerRadius);
  delegate->set_frame_margins({
      .contents = kDaoDialogContentMargins,
      .title = kDaoDialogTitleMargins,
      .footnote = gfx::Insets(),
  });

  const int buttons = delegate->buttons();
  const bool has_ok = buttons & static_cast<int>(ui::mojom::DialogButton::kOk);
  const bool has_cancel =
      buttons & static_cast<int>(ui::mojom::DialogButton::kCancel);
  if (options.show_enter_for_default && has_ok &&
      delegate->GetIsDefault(ui::mojom::DialogButton::kOk)) {
    delegate->SetButtonShortcut(
        ui::mojom::DialogButton::kOk,
        ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE), u"Enter");
  } else {
    delegate->ClearButtonShortcut(ui::mojom::DialogButton::kOk);
  }

  if (options.show_esc_for_cancel && has_cancel) {
    delegate->SetButtonShortcut(
        ui::mojom::DialogButton::kCancel,
        ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE), u"Esc");
  } else {
    delegate->ClearButtonShortcut(ui::mojom::DialogButton::kCancel);
  }
}

std::unique_ptr<views::MdTextButton> CreateDaoDialogButton(
    views::Button::PressedCallback callback,
    std::u16string_view label,
    std::optional<DaoDialogShortcut> shortcut,
    ui::ButtonStyle style) {
  return std::make_unique<DaoShortcutTextButton>(
      std::move(callback), label, std::move(shortcut), style);
}

}  // namespace dao
