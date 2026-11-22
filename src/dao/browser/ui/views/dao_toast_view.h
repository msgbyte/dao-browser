// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_TOAST_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_TOAST_VIEW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view.h"

namespace views {
class Label;
class LabelButton;
}

namespace dao {

// A lightweight toast notification that appears in the top-right corner
// of the content area. Shows a brief message with fade-in/fade-out animation.
class DaoToastView : public views::View,
                     public ui::NativeThemeObserver {
  METADATA_HEADER(DaoToastView, views::View)

 public:
  DaoToastView();
  // Hosts interactive toast content; the controller owns visibility and timing.
  explicit DaoToastView(std::unique_ptr<views::View> contents);
  DaoToastView(const DaoToastView&) = delete;
  DaoToastView& operator=(const DaoToastView&) = delete;
  ~DaoToastView() override;

  // Show a toast message. If a toast is already visible, it is replaced.
  void ShowToast(const std::u16string& message,
                 const std::u16string& action_text = {},
                 base::OnceClosure action = {});

  // Retained beneath transient feedback until cleared or explicitly dismissed.
  void SetPersistentToast(const std::u16string& message,
                          const std::u16string& action_text,
                          base::RepeatingClosure action,
                          base::RepeatingClosure dismiss);
  void ClearPersistentToast();
  // Chromium transient toasts use a separate widget on the same surface.
  void SetPersistentToastSuppressed(bool suppressed);
  bool IsShowingPersistentToast() const { return showing_persistent_; }

  bool IsShowingToast(const std::u16string& message) const;
  void HideToast();

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  void PresentToast(const std::u16string& message,
                    const std::u16string& action_text);
  void RestorePersistentToast();
  void DismissPersistentToast();
  void StartDismissTimer();
  void FadeOut();
  void ApplyTheme();
  void RunAction();

  raw_ptr<views::Label> label_ = nullptr;
  raw_ptr<views::LabelButton> action_button_ = nullptr;
  raw_ptr<views::LabelButton> close_button_ = nullptr;
  base::OnceClosure action_;
  std::u16string persistent_message_;
  std::u16string persistent_action_text_;
  base::RepeatingClosure persistent_action_;
  base::RepeatingClosure persistent_dismiss_;
  bool showing_persistent_ = false;
  bool persistent_suppressed_ = false;
  base::OneShotTimer dismiss_timer_;
  base::OneShotTimer hide_timer_;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_TOAST_VIEW_H_
