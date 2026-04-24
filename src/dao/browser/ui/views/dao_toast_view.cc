// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_toast_view.h"

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"
#include "ui/views/controls/label.h"

namespace dao {

namespace {
constexpr int kToastHeight = 36;
constexpr int kToastPaddingHorizontal = 16;
constexpr int kToastFontSize = 13;
constexpr base::TimeDelta kShowDuration = base::Milliseconds(2000);
constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(200);
constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(300);
constexpr int kSlideOffset = 16;  // pixels to slide down from above
}  // namespace

BEGIN_METADATA(DaoToastView)
END_METADATA

DaoToastView::DaoToastView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0.0f);

  // Start translated upward so the slide-down animation is visible
  gfx::Transform initial_transform;
  initial_transform.Translate(0, -kSlideOffset);
  layer()->SetTransform(initial_transform);

  SetVisible(false);
  SetCanProcessEventsWithinSubtree(false);

  label_ = AddChildView(std::make_unique<views::Label>());
  label_->SetFontList(
      gfx::FontList()
          .DeriveWithSizeDelta(kToastFontSize - gfx::FontList().GetFontSize())
          .DeriveWithWeight(gfx::Font::Weight::SEMIBOLD));
  label_->SetBackgroundColor(SK_ColorTRANSPARENT);
  label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
  ApplyTheme();
}

DaoToastView::~DaoToastView() = default;

void DaoToastView::ApplyTheme() {
  if (label_) {
    label_->SetEnabledColor(ToastTextColor());
  }
}

void DaoToastView::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  ApplyTheme();
  SchedulePaint();
}

void DaoToastView::ShowToast(const std::u16string& message) {
  dismiss_timer_.Stop();
  hide_timer_.Stop();

  label_->SetText(message);
  SetVisible(true);

  // Calculate preferred width: padding + text + padding
  int text_width = label_->GetPreferredSize().width();
  int total_width = kToastPaddingHorizontal + text_width + kToastPaddingHorizontal;
  SetPreferredSize(gfx::Size(total_width, kToastHeight));

  // Set initial state: transparent and shifted up
  layer()->SetOpacity(0.0f);
  gfx::Transform start_transform;
  start_transform.Translate(0, -kSlideOffset);
  layer()->SetTransform(start_transform);

  // Animate: fade in + slide down to final position
  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(kFadeInDuration);
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  layer()->SetOpacity(1.0f);
  layer()->SetTransform(gfx::Transform());

  StartDismissTimer();
}

void DaoToastView::OnPaint(gfx::Canvas* canvas) {
  gfx::RectF bounds(GetLocalBounds());
  // Inset slightly to leave room for shadow
  bounds.Inset(gfx::InsetsF(2));
  float corner_radius = bounds.height() / 2.0f;

  // Draw tight, hard shadow (2 rings, small spread, higher opacity)
  constexpr int kShadowSteps = 2;
  for (int i = kShadowSteps; i >= 1; --i) {
    float expand = i * 0.8f;
    float alpha = 20.0f * (kShadowSteps - i + 1) / kShadowSteps;
    gfx::RectF shadow_rect(bounds.x() - expand, bounds.y() - expand + 0.5f,
                            bounds.width() + 2 * expand,
                            bounds.height() + 2 * expand);
    cc::PaintFlags shadow_flags;
    shadow_flags.setAntiAlias(true);
    shadow_flags.setStyle(cc::PaintFlags::kFill_Style);
    shadow_flags.setColor(SkColorSetARGB(static_cast<int>(alpha), 0, 0,
                                         0));  // theme-independent
    canvas->DrawRoundRect(shadow_rect, corner_radius + expand, shadow_flags);
  }

  // Draw solid pill background
  cc::PaintFlags bg_flags;
  bg_flags.setColor(ToastBackground());
  bg_flags.setAntiAlias(true);
  bg_flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRoundRect(bounds, corner_radius, bg_flags);

  // Center label
  int text_width = label_->GetPreferredSize().width();
  int text_x = (width() - text_width) / 2;
  int text_y = (height() - label_->GetPreferredSize().height()) / 2;
  label_->SetBoundsRect(gfx::Rect(text_x, text_y, text_width,
                                   label_->GetPreferredSize().height()));
}

void DaoToastView::StartDismissTimer() {
  dismiss_timer_.Stop();
  dismiss_timer_.Start(FROM_HERE, kShowDuration,
                       base::BindOnce(&DaoToastView::FadeOut,
                                      base::Unretained(this)));
}

void DaoToastView::FadeOut() {
  // Animate: fade out + slide up
  gfx::Transform end_transform;
  end_transform.Translate(0, -kSlideOffset);

  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(kFadeOutDuration);
  settings.SetTweenType(gfx::Tween::EASE_IN);
  layer()->SetOpacity(0.0f);
  layer()->SetTransform(end_transform);

  // Hide after fade-out animation completes
  hide_timer_.Start(FROM_HERE, kFadeOutDuration,
                    base::BindOnce(&DaoToastView::HideAfterFade,
                                   base::Unretained(this)));
}

void DaoToastView::HideAfterFade() {
  SetVisible(false);
}

}  // namespace dao
