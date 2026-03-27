// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_
#define DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_

#include "third_party/skia/include/core/SkColor.h"

namespace dao {

constexpr SkColor kSidebarBackground = SkColorSetRGB(40, 32, 48);

constexpr SkColor kTextPrimary = SK_ColorWHITE;
constexpr SkColor kTextSecondary = SkColorSetARGB(150, 255, 255, 255);
constexpr SkColor kTextMuted = SkColorSetARGB(100, 255, 255, 255);

constexpr SkColor kActiveTabBackground = SkColorSetARGB(35, 255, 255, 255);
constexpr SkColor kSeparatorColor = SkColorSetARGB(30, 255, 255, 255);
constexpr SkColor kAddressBarBackground = SkColorSetARGB(35, 255, 255, 255);

constexpr SkColor kFrameColor = SkColorSetRGB(40, 32, 48);

constexpr SkColor kSpaceActive = SkColorSetRGB(140, 100, 220);
constexpr SkColor kSpaceInactive = SkColorSetARGB(60, 255, 255, 255);

constexpr SkColor kInkDropBase = SK_ColorWHITE;
constexpr float kInkDropOpacity = 0.06f;

constexpr SkColor kCommandBarScrim = SkColorSetARGB(120, 0, 0, 0);
constexpr SkColor kCommandBarBackground = SkColorSetRGB(50, 42, 58);

constexpr SkColor kSuggestionHover = SkColorSetARGB(20, 255, 255, 255);
constexpr SkColor kSuggestionSelected = SkColorSetARGB(40, 255, 255, 255);
constexpr SkColor kSuggestionIconColor = SkColorSetARGB(180, 255, 255, 255);
constexpr SkColor kGhostTextColor = SkColorSetARGB(80, 255, 255, 255);

// Content area styling
constexpr int kContentCornerRadius = 10;
constexpr int kContentShadowMargin = 8;
constexpr int kContentInsetTop = 6;
constexpr int kContentInsetRight = 6;
constexpr int kContentInsetBottom = 6;

// Split view styling
constexpr SkColor kDividerColor =
    SkColorSetARGB(30, 255, 255, 255);                // white 12%
constexpr SkColor kDividerHoverColor =
    SkColorSetARGB(128, 140, 100, 220);               // purple 50%
constexpr SkColor kDropZoneOverlay =
    SkColorSetARGB(38, 140, 100, 220);                // purple 15%
constexpr int kDividerWidth = 4;
constexpr int kDropZoneEdgeSize = 40;
constexpr int kMinPaneSize = 200;

// Pane header (frosted glass pill)
constexpr SkColor kPaneHeaderBackground =
    SkColorSetARGB(217, 30, 24, 38);                  // rgba(30,24,38,0.85)
constexpr SkColor kPaneHeaderShadow =
    SkColorSetARGB(77, 0, 0, 0);                      // rgba(0,0,0,0.3)
constexpr SkColor kPaneHeaderButtonHover =
    SkColorSetARGB(26, 255, 255, 255);                // white 10%
constexpr SkColor kPaneHeaderButtonIcon =
    SkColorSetARGB(179, 255, 255, 255);               // white 70%
constexpr int kPaneHeaderCornerRadius = 8;
constexpr int kPaneHeaderButtonSize = 22;
constexpr int kPaneHeaderButtonRadius = 6;

// Active pane indicator (glow border)
constexpr SkColor kActivePaneBorder =
    SkColorSetARGB(153, 140, 100, 220);               // purple 60%
constexpr SkColor kActivePaneGlow =
    SkColorSetARGB(38, 140, 100, 220);                // purple 15%
constexpr int kActivePaneBorderWidth = 2;
constexpr int kActivePaneGlowRadius = 12;

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_
