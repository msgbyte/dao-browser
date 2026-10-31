// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/pip/dao_pip_window_event_monitor.h"

#include <array>
#include <memory>

#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/compositor/layer_type.h"
#include "ui/events/event.h"

namespace dao {
namespace {

class CountingObserver : public ui::EventObserver {
 public:
  void OnEvent(const ui::Event& event) override { ++count; }
  int count = 0;
};

class DaoPipWindowEventMonitorTest : public testing::TestWithParam<int> {
 protected:
  void SetUp() override {
    env_ = aura::Env::CreateInstance();
    for (size_t i = 0; i < windows_.size(); ++i) {
      windows_[i] = std::make_unique<aura::Window>(nullptr);
      windows_[i]->Init(ui::LAYER_NOT_DRAWN);
      monitors_[i] = std::make_unique<DaoPipWindowEventMonitor>(
          observer_, windows_[i].get());
    }
  }

  void SendKeyEvent(int window_index) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, ui::VKEY_A, ui::EF_NONE);
    ui::Event::DispatcherApi(&event).set_target(windows_[window_index].get());
    env_->NotifyEventObservers(event);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI};
  std::unique_ptr<aura::Env> env_;
  CountingObserver observer_;
  std::array<std::unique_ptr<aura::Window>, 2> windows_;
  std::array<std::unique_ptr<DaoPipWindowEventMonitor>, 2> monitors_;
};

TEST_P(DaoPipWindowEventMonitorTest, RemovingMonitorKeepsOtherWindowObserved) {
  SendKeyEvent(0);
  SendKeyEvent(1);
  ASSERT_EQ(observer_.count, 2);

  monitors_[GetParam()].reset();
  observer_.count = 0;
  SendKeyEvent(GetParam());
  EXPECT_EQ(observer_.count, 0);
  observer_.count = 0;
  SendKeyEvent(1 - GetParam());
  EXPECT_EQ(observer_.count, 1);
}

TEST_P(DaoPipWindowEventMonitorTest, DestroyingWindowKeepsOtherWindowObserved) {
  windows_[GetParam()].reset();
  monitors_[GetParam()].reset();
  SendKeyEvent(1 - GetParam());
  EXPECT_EQ(observer_.count, 1);

  // Destroy the remaining window before its monitor, as in native PiP teardown.
  windows_[1 - GetParam()].reset();
  monitors_[1 - GetParam()].reset();
}

INSTANTIATE_TEST_SUITE_P(BothDestructionOrders,
                         DaoPipWindowEventMonitorTest,
                         testing::Values(0, 1));

}  // namespace
}  // namespace dao
