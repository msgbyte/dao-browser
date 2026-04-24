// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_colors.h"

#include "ui/native_theme/native_theme.h"

namespace dao {

bool IsDarkMode() {
  auto* theme = ui::NativeTheme::GetInstanceForNativeUi();
  return theme && theme->ShouldUseDarkColors();
}

SkColor SidebarBackground() {
  return IsDarkMode() ? SkColorSetRGB(54, 59, 64)
                      : SkColorSetRGB(231, 238, 245);
}

SkColor FrameColor() {
  return SidebarBackground();
}

SkColor TextPrimary() {
  return IsDarkMode() ? SkColorSetRGB(245, 245, 245)
                      : SkColorSetRGB(30, 20, 40);
}

SkColor TextSecondary() {
  return IsDarkMode() ? SkColorSetARGB(153, 255, 255, 255)
                      : SkColorSetARGB(153, 30, 20, 40);
}

SkColor TextMuted() {
  return IsDarkMode() ? SkColorSetARGB(102, 255, 255, 255)
                      : SkColorSetARGB(102, 30, 20, 40);
}

SkColor ActiveTabBackground() {
  return IsDarkMode() ? SkColorSetARGB(20, 255, 255, 255)
                      : SkColorSetARGB(20, 0, 0, 0);
}

SkColor SeparatorColor() {
  return ActiveTabBackground();  // alias
}

SkColor AddressBarBackground() {
  return IsDarkMode() ? SkColorSetARGB(15, 255, 255, 255)
                      : SkColorSetARGB(15, 0, 0, 0);
}

SkColor SpaceActive() {
  return SkColorSetRGB(70, 120, 190);  // accent shared across themes
}

SkColor SpaceInactive() {
  return IsDarkMode() ? SkColorSetARGB(60, 255, 255, 255)
                      : SkColorSetARGB(60, 30, 20, 40);
}

SkColor InkDropBase() {
  return IsDarkMode() ? SK_ColorWHITE : SK_ColorBLACK;
}

float InkDropOpacity() {
  return IsDarkMode() ? 0.06f : 0.04f;
}

SkColor CommandBarScrim() {
  return IsDarkMode() ? SkColorSetARGB(120, 0, 0, 0)
                      : SkColorSetARGB(80, 0, 0, 0);
}

SkColor CommandBarBackground() {
  return IsDarkMode() ? SkColorSetARGB(210, 72, 78, 84)
                      : SkColorSetARGB(186, 255, 255, 255);
}

SkColor CommandBarBorder() {
  return IsDarkMode() ? SkColorSetARGB(40, 255, 255, 255)
                      : SkColorSetARGB(40, 0, 0, 0);
}

SkColor SuggestionHover() {
  return IsDarkMode() ? SkColorSetARGB(15, 255, 255, 255)
                      : SkColorSetARGB(15, 0, 0, 0);
}

SkColor SuggestionSelected() {
  return IsDarkMode() ? SkColorSetARGB(25, 255, 255, 255)
                      : SkColorSetARGB(25, 0, 0, 0);
}

SkColor SuggestionTitleColor() {
  return IsDarkMode() ? SkColorSetRGB(250, 250, 250)
                      : SkColorSetRGB(10, 8, 16);
}

SkColor SuggestionIconColor() {
  return TextSecondary();  // alias
}

SkColor GhostTextColor() {
  return IsDarkMode() ? SkColorSetARGB(77, 255, 255, 255)
                      : SkColorSetARGB(77, 30, 20, 40);
}

SkColor DividerColor() {
  return ActiveTabBackground();  // alias
}

SkColor DividerHoverColor() {
  return SkColorSetARGB(128, 70, 120, 190);
}

SkColor DropZoneOverlay() {
  return SkColorSetARGB(38, 70, 120, 190);
}

SkColor PaneHeaderBackground() {
  return IsDarkMode() ? SkColorSetARGB(230, 70, 76, 82)
                      : SkColorSetARGB(230, 231, 238, 245);
}

SkColor PaneHeaderShadow() {
  return IsDarkMode() ? SkColorSetARGB(60, 0, 0, 0)
                      : SkColorSetARGB(40, 0, 0, 0);
}

SkColor PaneHeaderButtonHover() {
  return ActiveTabBackground();  // alias
}

SkColor PaneHeaderButtonIcon() {
  return TextSecondary();  // alias
}

SkColor ActivePaneBorder() {
  return SkColorSetARGB(153, 70, 120, 190);
}

SkColor ActivePaneGlow() {
  return SkColorSetARGB(38, 70, 120, 190);
}

float CornerShadowAlphaBase() {
  // Light mode uses 12.0f; dark mode scales x1.5 so the shadow remains
  // visible on a 54,59,64 surface.
  return IsDarkMode() ? 18.0f : 12.0f;
}

SkColor PopupBackground() {
  return IsDarkMode() ? SkColorSetARGB(230, 70, 76, 82)
                      : SkColorSetARGB(230, 255, 255, 255);
}

SkColor ToastBackground() {
  return IsDarkMode() ? SkColorSetRGB(70, 76, 82)
                      : SkColorSetRGB(255, 255, 255);
}

SkColor ToastTextColor() {
  return IsDarkMode() ? SkColorSetRGB(240, 240, 245)
                      : SkColorSetRGB(35, 35, 40);
}

SkColor ControlCenterIconDefault() {
  return IsDarkMode() ? SkColorSetARGB(160, 255, 255, 255)
                      : SkColorSetARGB(160, 0, 0, 0);
}

SkColor ControlCenterIconMuted() {
  return IsDarkMode() ? SkColorSetRGB(170, 170, 175)
                      : SkColorSetRGB(55, 55, 60);
}

SkColor ControlCenterHoverBg() {
  return IsDarkMode() ? SkColorSetARGB(20, 255, 255, 255)
                      : SkColorSetARGB(20, 0, 0, 0);
}

SkColor ControlCenterActiveBg() {
  return IsDarkMode() ? SkColorSetARGB(25, 255, 255, 255)
                      : SkColorSetARGB(25, 0, 0, 0);
}

SkColor ControlCenterLabelColor() {
  return IsDarkMode() ? SkColorSetRGB(200, 200, 205)
                      : SkColorSetRGB(100, 100, 100);
}

SkColor ControlCenterSecondaryTextColor() {
  return IsDarkMode() ? SkColorSetRGB(160, 160, 165)
                      : SkColorSetRGB(160, 160, 160);
}

SkColor PopupShadowOuter() {
  return IsDarkMode() ? SkColorSetARGB(60, 0, 0, 0)
                      : SkColorSetARGB(30, 0, 0, 0);
}

SkColor PopupShadowInner() {
  return IsDarkMode() ? SkColorSetARGB(90, 0, 0, 0)
                      : SkColorSetARGB(45, 0, 0, 0);
}

SkColor AgentLockHeaderFill() {
  return IsDarkMode() ? SkColorSetARGB(220, 70, 76, 82)
                      : SkColorSetARGB(212, 255, 255, 255);
}

SkColor AgentLockHeaderShadow() {
  return IsDarkMode() ? SkColorSetARGB(60, 0, 0, 0)
                      : SkColorSetARGB(28, 24, 16, 36);
}

SkColor AgentLockDotColor() {
  // Returned with alpha=0 — callers override via SkColorSetA.
  return IsDarkMode() ? SK_ColorBLACK : SK_ColorWHITE;
}

SkColor AgentLockMistColor(int step) {
  const int alpha = 10 + step * 10;
  return IsDarkMode() ? SkColorSetARGB(alpha, 0, 0, 0)
                      : SkColorSetARGB(alpha, 255, 255, 255);
}

}  // namespace dao
