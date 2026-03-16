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

// Content area styling
constexpr int kContentCornerRadius = 10;
constexpr int kContentShadowMargin = 8;
constexpr int kContentInsetTop = 6;
constexpr int kContentInsetRight = 6;
constexpr int kContentInsetBottom = 6;

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_COLORS_H_
