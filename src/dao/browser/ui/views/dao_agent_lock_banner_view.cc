// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_agent_lock_banner_view.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"

namespace dao {

namespace {
constexpr int kHeaderHeight = 72;
constexpr int kHeaderHorizontalPadding = 18;
constexpr int kHeaderVerticalPadding = 16;
constexpr int kHeaderCornerRadius = 18;
constexpr int kHeaderIconSize = 18;
constexpr int kHeaderIconContainerSize = 32;
constexpr int kHeaderIconGap = 12;
constexpr int kDotSpacing = 24;
constexpr int kDotInset = 20;
constexpr int kDotTravel = 8;
constexpr int kAnimationIntervalMs = 33;
// Brand accent colors — theme-independent (shared across light/dark).
constexpr SkColor kOverlayBase = SkColorSetARGB(126, 126, 99, 162);
constexpr SkColor kHeaderStroke = SkColorSetARGB(30, 70, 120, 190);
constexpr SkColor kAccentGlow = SkColorSetARGB(140, 70, 120, 190);
constexpr SkColor kDotAccent = SkColorSetARGB(60, 70, 120, 190);
}  // namespace

BEGIN_METADATA(DaoAgentLockBannerView)
END_METADATA

DaoAgentLockBannerView::DaoAgentLockBannerView() {
  SetPreferredSize(gfx::Size());
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);
  animation_start_time_ = base::TimeTicks::Now();
  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
}

DaoAgentLockBannerView::~DaoAgentLockBannerView() = default;

void DaoAgentLockBannerView::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  SchedulePaint();
}

void DaoAgentLockBannerView::SetLocked(bool locked) {
  if (locked_ == locked) {
    return;
  }
  locked_ = locked;
  SetVisible(locked);
  if (locked_) {
    animation_start_time_ = base::TimeTicks::Now();
    animation_timer_.Start(
        FROM_HERE, base::Milliseconds(kAnimationIntervalMs),
        base::BindRepeating(&DaoAgentLockBannerView::SchedulePaint,
                            base::Unretained(this)));
    SchedulePaint();
  } else {
    animation_timer_.Stop();
  }
}

float DaoAgentLockBannerView::GetAnimationPhase() const {
  const float elapsed_seconds =
      (base::TimeTicks::Now() - animation_start_time_).InMillisecondsF() /
      1000.0f;
  return elapsed_seconds * 1.8f;
}

void DaoAgentLockBannerView::OnPaint(gfx::Canvas* canvas) {
  const gfx::Rect bounds = GetLocalBounds();
  const float phase = GetAnimationPhase();

  cc::PaintFlags base_flags;
  base_flags.setAntiAlias(true);
  base_flags.setStyle(cc::PaintFlags::kFill_Style);
  base_flags.setColor(kOverlayBase);
  canvas->DrawRect(gfx::RectF(bounds), base_flags);

  {
    const gfx::Point center(width() * 0.78f, height() * 0.18f);
    for (int i = 5; i >= 1; --i) {
      cc::PaintFlags glow_flags;
      glow_flags.setAntiAlias(true);
      glow_flags.setStyle(cc::PaintFlags::kFill_Style);
      glow_flags.setColor(SkColorSetA(kAccentGlow, 12 + i * 10));
      canvas->DrawCircle(center, std::max(width(), height()) * (0.10f * i),
                         glow_flags);
    }
  }

  {
    const gfx::Point center(width() * 0.28f, height() * 0.82f);
    for (int i = 4; i >= 1; --i) {
      cc::PaintFlags mist_flags;
      mist_flags.setAntiAlias(true);
      mist_flags.setStyle(cc::PaintFlags::kFill_Style);
      mist_flags.setColor(AgentLockMistColor(i));
      canvas->DrawCircle(center, std::max(width(), height()) * (0.08f * i),
                         mist_flags);
    }
  }

  for (int y = kDotInset; y < height() - kDotInset; y += kDotSpacing) {
    for (int x = kDotInset; x < width() - kDotInset; x += kDotSpacing) {
      const float wave =
          0.5f + 0.5f * std::sin(phase * 1.7f + x * 0.028f + y * 0.022f);
      const float drift =
          std::sin(phase + y * 0.018f) * static_cast<float>(kDotTravel);
      const int draw_x = x + static_cast<int>(drift);
      const float radius = 0.8f + wave * 1.6f;

      cc::PaintFlags dot_flags;
      dot_flags.setAntiAlias(true);
      dot_flags.setStyle(cc::PaintFlags::kFill_Style);
      dot_flags.setColor(
          SkColorSetA(AgentLockDotColor(), 10 + static_cast<int>(wave * 24)));
      canvas->DrawCircle(gfx::Point(draw_x, y), radius, dot_flags);

      if (wave > 0.72f) {
        cc::PaintFlags accent_flags;
        accent_flags.setAntiAlias(true);
        accent_flags.setStyle(cc::PaintFlags::kFill_Style);
        accent_flags.setColor(
            SkColorSetA(kDotAccent, 16 + static_cast<int>(wave * 34)));
        canvas->DrawCircle(gfx::Point(draw_x, y), radius + 0.75f, accent_flags);
      }
    }
  }

  const gfx::Rect header_rect(
      kHeaderHorizontalPadding, kHeaderVerticalPadding,
      std::min(width() - kHeaderHorizontalPadding * 2, 360), kHeaderHeight);
  if (header_rect.width() <= 0) {
    return;
  }

  {
    gfx::RectF shadow_rect(header_rect.x(), header_rect.y() + 8.0f,
                           header_rect.width(), header_rect.height());
    cc::PaintFlags shadow_flags;
    shadow_flags.setAntiAlias(true);
    shadow_flags.setStyle(cc::PaintFlags::kFill_Style);
    shadow_flags.setColor(AgentLockHeaderShadow());
    canvas->DrawRoundRect(shadow_rect, 20.0f, shadow_flags);
  }

  {
    cc::PaintFlags header_flags;
    header_flags.setAntiAlias(true);
    header_flags.setStyle(cc::PaintFlags::kFill_Style);
    header_flags.setColor(AgentLockHeaderFill());
    canvas->DrawRoundRect(gfx::RectF(header_rect), kHeaderCornerRadius,
                          header_flags);

    cc::PaintFlags stroke_flags;
    stroke_flags.setAntiAlias(true);
    stroke_flags.setStyle(cc::PaintFlags::kStroke_Style);
    stroke_flags.setStrokeWidth(1.0f);
    stroke_flags.setColor(kHeaderStroke);
    canvas->DrawRoundRect(gfx::RectF(header_rect), kHeaderCornerRadius,
                          stroke_flags);
  }

  const int icon_container_x = header_rect.x() + 14;
  const int icon_container_y =
      header_rect.y() + (header_rect.height() - kHeaderIconContainerSize) / 2;
  {
    cc::PaintFlags icon_bg_flags;
    icon_bg_flags.setAntiAlias(true);
    icon_bg_flags.setStyle(cc::PaintFlags::kFill_Style);
    // theme-independent brand accent (shared across light/dark).
    icon_bg_flags.setColor(SkColorSetARGB(
        26 + static_cast<int>((0.5f + 0.5f * std::sin(phase * 2.2f)) * 36),
        70, 120, 190));
    canvas->DrawRoundRect(
        gfx::RectF(icon_container_x, icon_container_y, kHeaderIconContainerSize,
                   kHeaderIconContainerSize),
        10.0f, icon_bg_flags);
  }

  DrawLucideIcon(
      canvas, LucideIcon::kBot,
      gfx::RectF(icon_container_x +
                     (kHeaderIconContainerSize - kHeaderIconSize) / 2,
                 icon_container_y +
                     (kHeaderIconContainerSize - kHeaderIconSize) / 2,
                 kHeaderIconSize, kHeaderIconSize),
      dao::TextPrimary());

  const int text_x =
      icon_container_x + kHeaderIconContainerSize + kHeaderIconGap;
  const int text_width = header_rect.right() - text_x - 16;
  gfx::FontList title_font({"sans-serif"}, gfx::Font::FontStyle::NORMAL, 13,
                           gfx::Font::Weight::SEMIBOLD);
  gfx::FontList subtitle_font({"sans-serif"}, gfx::Font::FontStyle::NORMAL, 12,
                              gfx::Font::Weight::NORMAL);
  canvas->DrawStringRectWithFlags(
      u"AI is operating on this page", title_font, dao::TextPrimary(),
      gfx::Rect(text_x, header_rect.y() + 15, text_width, 18),
      gfx::Canvas::TEXT_ALIGN_LEFT | gfx::Canvas::NO_SUBPIXEL_RENDERING);
  canvas->DrawStringRectWithFlags(
      u"User input is temporarily disabled", subtitle_font,
      dao::TextSecondary(),
      gfx::Rect(text_x, header_rect.y() + 36, text_width, 16),
      gfx::Canvas::TEXT_ALIGN_LEFT | gfx::Canvas::NO_SUBPIXEL_RENDERING);
}

}  // namespace dao
