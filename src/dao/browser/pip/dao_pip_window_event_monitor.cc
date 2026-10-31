// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/pip/dao_pip_window_event_monitor.h"

namespace dao {

DaoPipWindowEventMonitor::DaoPipWindowEventMonitor(ui::EventObserver& observer,
                                                 gfx::NativeWindow window)
    : observer_(observer),
      monitor_(views::EventMonitor::CreateWindowMonitor(
          this, window,
          {ui::EventType::kMouseMoved, ui::EventType::kMouseExited,
           ui::EventType::kKeyPressed, ui::EventType::kKeyReleased})) {}

DaoPipWindowEventMonitor::~DaoPipWindowEventMonitor() = default;

gfx::Point DaoPipWindowEventMonitor::GetLastMouseLocation() {
  return monitor_->GetLastMouseLocation();
}

void DaoPipWindowEventMonitor::OnEvent(const ui::Event& event) {
  observer_->OnEvent(event);
}

}  // namespace dao
