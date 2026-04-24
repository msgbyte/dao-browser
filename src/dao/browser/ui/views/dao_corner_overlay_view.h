// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CORNER_OVERLAY_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CORNER_OVERLAY_VIEW_H_

#include "base/scoped_observation.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view.h"

namespace dao {

// Draws a drop shadow behind the content area to create a floating effect.
// This view is sized larger than the content area by kContentShadowMargin
// on each side so the shadow extends beyond the content bounds.
class DaoCornerOverlayView : public views::View,
                             public ui::NativeThemeObserver {
  METADATA_HEADER(DaoCornerOverlayView, views::View)

 public:
  DaoCornerOverlayView();
  DaoCornerOverlayView(const DaoCornerOverlayView&) = delete;
  DaoCornerOverlayView& operator=(const DaoCornerOverlayView&) = delete;
  ~DaoCornerOverlayView() override;

  void OnPaint(gfx::Canvas* canvas) override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CORNER_OVERLAY_VIEW_H_
