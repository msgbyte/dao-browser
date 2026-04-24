// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_
#define DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_

#include "third_party/skia/include/core/SkColor.h"

namespace dao {

constexpr SkColor kSidebarBackground = SkColorSetRGB(231, 238, 245);

constexpr SkColor kTextPrimary = SkColorSetRGB(30, 20, 40);
constexpr SkColor kTextSecondary = SkColorSetARGB(153, 30, 20, 40);
constexpr SkColor kTextMuted = SkColorSetARGB(102, 30, 20, 40);

constexpr SkColor kActiveTabBackground = SkColorSetARGB(20, 0, 0, 0);
constexpr SkColor kSeparatorColor = SkColorSetARGB(20, 0, 0, 0);
constexpr SkColor kAddressBarBackground = SkColorSetARGB(15, 0, 0, 0);

constexpr SkColor kFrameColor = SkColorSetRGB(231, 238, 245);

constexpr SkColor kSpaceActive = SkColorSetRGB(70, 120, 190);
constexpr SkColor kSpaceInactive = SkColorSetARGB(60, 30, 20, 40);

constexpr SkColor kInkDropBase = SK_ColorBLACK;
constexpr float kInkDropOpacity = 0.04f;

constexpr SkColor kCommandBarScrim = SkColorSetARGB(80, 0, 0, 0);
constexpr SkColor kCommandBarBackground = SkColorSetARGB(186, 255, 255, 255);
constexpr SkColor kCommandBarBorder = SkColorSetARGB(40, 0, 0, 0);
constexpr float kCommandBarBlurSigma = 16.0f;

constexpr SkColor kSuggestionHover = SkColorSetARGB(15, 0, 0, 0);
constexpr SkColor kSuggestionSelected = SkColorSetARGB(25, 0, 0, 0);
constexpr SkColor kSuggestionTitleColor = SkColorSetRGB(10, 8, 16);
constexpr SkColor kSuggestionIconColor = SkColorSetARGB(153, 30, 20, 40);
constexpr SkColor kGhostTextColor = SkColorSetARGB(77, 30, 20, 40);

// Content area styling
constexpr int kContentCornerRadius = 10;
constexpr int kContentShadowMargin = 8;
constexpr int kContentInsetTop = 6;
constexpr int kContentInsetRight = 6;
constexpr int kContentInsetBottom = 6;

// Split view styling
constexpr SkColor kDividerColor =
    SkColorSetARGB(20, 0, 0, 0);                      // black 8%
constexpr SkColor kDividerHoverColor =
    SkColorSetARGB(128, 70, 120, 190);
constexpr SkColor kDropZoneOverlay =
    SkColorSetARGB(38, 70, 120, 190);
constexpr int kDividerWidth = 4;
constexpr int kDropZoneEdgeSize = 40;
constexpr int kMinPaneSize = 200;

// Pane header (frosted glass pill)
constexpr SkColor kPaneHeaderBackground =
    SkColorSetARGB(230, 231, 238, 245);               // rgba(231,238,245,0.90)
constexpr SkColor kPaneHeaderShadow =
    SkColorSetARGB(40, 0, 0, 0);                      // rgba(0,0,0,0.16)
constexpr SkColor kPaneHeaderButtonHover =
    SkColorSetARGB(20, 0, 0, 0);                      // black 8%
constexpr SkColor kPaneHeaderButtonIcon =
    SkColorSetARGB(153, 30, 20, 40);                  // dark 60%
constexpr int kPaneHeaderCornerRadius = 8;
constexpr int kPaneHeaderButtonSize = 22;
constexpr int kPaneHeaderButtonRadius = 6;

// Active pane indicator (glow border)
constexpr SkColor kActivePaneBorder =
    SkColorSetARGB(153, 70, 120, 190);
constexpr SkColor kActivePaneGlow =
    SkColorSetARGB(38, 70, 120, 190);
constexpr int kActivePaneBorderWidth = 2;
constexpr int kActivePaneGlowRadius = 12;

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_
