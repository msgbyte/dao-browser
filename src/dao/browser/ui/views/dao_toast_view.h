// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_TOAST_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_TOAST_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace dao {

// A lightweight toast notification that appears in the top-right corner
// of the content area. Shows a brief message with fade-in/fade-out animation.
class DaoToastView : public views::View,
                     public ui::NativeThemeObserver {
  METADATA_HEADER(DaoToastView, views::View)

 public:
  DaoToastView();
  DaoToastView(const DaoToastView&) = delete;
  DaoToastView& operator=(const DaoToastView&) = delete;
  ~DaoToastView() override;

  // Show a toast message. If a toast is already visible, it is replaced.
  void ShowToast(const std::u16string& message);

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  void StartDismissTimer();
  void FadeOut();
  void HideAfterFade();
  void ApplyTheme();

  raw_ptr<views::Label> label_ = nullptr;
  base::OneShotTimer dismiss_timer_;
  base::OneShotTimer hide_timer_;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_TOAST_VIEW_H_
