// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_
#define DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_

#include "third_party/skia/include/core/SkColor.h"

class Browser;

namespace dao {

// Returns true when the current system appearance is dark.
bool IsDarkMode();

SkColor SidebarBackground();
SkColor SidebarBackground(const Browser* browser);
SkColor FrameColor();

SkColor TextPrimary();
SkColor TextSecondary();
SkColor TextSecondary(const Browser* browser);
SkColor TextMuted();

SkColor ActiveTabBackground();
SkColor SeparatorColor();
SkColor AddressBarBackground();

SkColor SpaceActive();
SkColor SpaceInactive();

SkColor InkDropBase();
float InkDropOpacity();

SkColor CommandBarScrim();
SkColor CommandBarBackground();
SkColor CommandBarBorder();
constexpr float kCommandBarBlurSigma = 16.0f;  // unchanged across themes

SkColor SuggestionHover();
SkColor SuggestionSelected();
SkColor SuggestionTitleColor();
SkColor SuggestionIconColor();
// Selected-style ghost text mirrors the selected suggestion row.
SkColor GhostTextSelectedBackground();

// Content area styling
constexpr int kContentCornerRadius = 10;
constexpr int kContentShadowMargin = 8;
constexpr int kContentInsetTop = 6;
constexpr int kContentInsetRight = 6;
constexpr int kContentInsetBottom = 6;

// Split view styling
SkColor DividerColor();
SkColor DividerHoverColor();
SkColor DropZoneOverlay();
constexpr int kDividerWidth = 4;
constexpr int kDropZoneEdgeSize = 40;
constexpr int kMinPaneSize = 200;

// Pane header (frosted glass pill)
SkColor PaneHeaderBackground();
SkColor PaneHeaderShadow();
SkColor PaneHeaderButtonHover();
SkColor PaneHeaderButtonIcon();
constexpr int kPaneHeaderCornerRadius = 8;
constexpr int kPaneHeaderButtonSize = 22;
constexpr int kPaneHeaderButtonRadius = 6;

// Active pane indicator (glow border)
SkColor ActivePaneBorder();
SkColor ActivePaneGlow();
constexpr int kActivePaneBorderWidth = 2;
constexpr int kActivePaneGlowRadius = 12;

// Per-step alpha base for DaoCornerOverlayView drop-shadow. Dark mode
// scales up so the shadow remains visible on the 54,59,64 surface.
float CornerShadowAlphaBase();

// Surface / popup palette — flat surfaces used by popups, toasts, control
// center cards. Slightly brighter than sidebar in dark mode for depth.
SkColor PopupBackground();
SkColor ToastBackground();
SkColor ToastTextColor();

// Neutral icon / hover tints for control center buttons and menus.
SkColor ControlCenterIconDefault();
SkColor ControlCenterIconMuted();
SkColor ControlCenterHoverBg();
SkColor ControlCenterHoverBg(const Browser* browser);
SkColor ControlCenterActiveBg();
SkColor ControlCenterLabelColor();
SkColor ControlCenterSecondaryTextColor();

// Generic drop-shadow colors used across popups.
SkColor PopupShadowOuter();  // 40px blur step
SkColor PopupShadowInner();  // 16px blur step, y=4

// Agent lock banner — themed surfaces over the brand overlay.
SkColor AgentLockHeaderFill();
SkColor AgentLockHeaderShadow();
SkColor AgentLockDotColor();
SkColor AgentLockMistColor(int step);  // step is 0..N

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_
