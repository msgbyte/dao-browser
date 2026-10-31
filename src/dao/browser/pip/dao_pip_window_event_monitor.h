// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_PIP_DAO_PIP_WINDOW_EVENT_MONITOR_H_
#define DAO_BROWSER_PIP_DAO_PIP_WINDOW_EVENT_MONITOR_H_

#include <memory>

#include "base/memory/raw_ref.h"
#include "ui/events/event_observer.h"
#include "ui/views/event_monitor.h"

namespace dao {

// Each PiP window needs its own observer identity: Aura removes event monitors
// by observer pointer, so sharing one between the frame and overlay can detach
// the wrong window and leave an adapter pointing at a destroyed native window.
class DaoPipWindowEventMonitor : public ui::EventObserver {
 public:
  DaoPipWindowEventMonitor(ui::EventObserver& observer,
                           gfx::NativeWindow window);

  DaoPipWindowEventMonitor(const DaoPipWindowEventMonitor&) = delete;
  DaoPipWindowEventMonitor& operator=(const DaoPipWindowEventMonitor&) = delete;
  ~DaoPipWindowEventMonitor() override;

  gfx::Point GetLastMouseLocation();

 private:
  void OnEvent(const ui::Event& event) override;

  const raw_ref<ui::EventObserver> observer_;
  // Destroy the native registration while this forwarding observer is alive.
  std::unique_ptr<views::EventMonitor> monitor_;
};

}  // namespace dao

#endif  // DAO_BROWSER_PIP_DAO_PIP_WINDOW_EVENT_MONITOR_H_
