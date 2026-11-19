// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/performance/dao_high_performance_tab_helper.h"

#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "content/public/test/navigation_simulator.h"
#include "dao/browser/performance/dao_high_performance_mode.h"
#include "url/gurl.h"

namespace dao {

class DaoHighPerformanceTabHelperTest : public ChromeRenderViewHostTestHarness {
 public:
  void SetUp() override {
    ChromeRenderViewHostTestHarness::SetUp();
    DaoHighPerformanceTabHelper::CreateForWebContents(web_contents());
    NavigateAndCommit(GURL("https://a.test"));
  }
};

TEST_F(DaoHighPerformanceTabHelperTest, AbortedCommitRestoresSiteState) {
  for (bool enabled : {false, true}) {
    SCOPED_TRACE(enabled);
    DaoHighPerformanceTabHelper::SetAlwaysEnabled(
        profile(), GURL("https://a.test"), enabled);
    DaoHighPerformanceTabHelper::SetAlwaysEnabled(
        profile(), GURL("https://b.test"), !enabled);
    ASSERT_EQ(enabled, DaoHighPerformanceMode::IsEnabled(web_contents()));

    auto navigation = content::NavigationSimulator::CreateBrowserInitiated(
        GURL("https://b.test"), web_contents());
    navigation->ReadyToCommit();
    EXPECT_EQ(!enabled, DaoHighPerformanceMode::IsEnabled(web_contents()));
    navigation->AbortCommit();
    EXPECT_EQ(enabled, DaoHighPerformanceMode::IsEnabled(web_contents()));
  }
}

TEST_F(DaoHighPerformanceTabHelperTest, AbortedCommitPreservesManualTurnOff) {
  DaoHighPerformanceTabHelper::SetAlwaysEnabled(profile(),
                                                GURL("https://a.test"), true);
  DaoHighPerformanceTabHelper::SetAlwaysEnabled(profile(),
                                                GURL("https://b.test"), true);
  DaoHighPerformanceMode::SetEnabled(web_contents(), false);

  auto navigation = content::NavigationSimulator::CreateBrowserInitiated(
      GURL("https://b.test"), web_contents());
  navigation->ReadyToCommit();
  EXPECT_TRUE(DaoHighPerformanceMode::IsEnabled(web_contents()));
  navigation->AbortCommit();
  EXPECT_FALSE(DaoHighPerformanceMode::IsEnabled(web_contents()));
  EXPECT_TRUE(DaoHighPerformanceTabHelper::IsAlwaysEnabled(
      profile(), GURL("https://a.test")));
}

}  // namespace dao
