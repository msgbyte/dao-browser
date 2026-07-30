// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <string_view>

#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/mcp/dao_mcp_session_lifecycle_monitor.h"
#include "dao/browser/automation/dao_browser_target_policy.h"
#include "net/dns/mock_host_resolver.h"
#include "url/gurl.h"

namespace dao {
namespace {

class DaoMcpEndToEndBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }
};

IN_PROC_BROWSER_TEST_F(DaoMcpEndToEndBrowserTest,
                       AllowsHttpHttpsBlankAndWebHostedPdf) {
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  EXPECT_TRUE(
      ValidateExternalTarget(browser(), browser()->profile(), contents).has_value());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  EXPECT_TRUE(
      ValidateExternalTarget(browser(), browser()->profile(), contents).has_value());
  EXPECT_TRUE(ValidateExternalTargetUrl(GURL("https://secure.example.test/page"))
                  .has_value());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/pdf/test.pdf")));
  EXPECT_TRUE(
      ValidateExternalTarget(browser(), browser()->profile(), contents).has_value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpEndToEndBrowserTest,
                       RejectsForbiddenSchemeMatrix) {
  constexpr std::array<std::string_view, 7> kForbiddenUrls = {
      "chrome://version/",
      "chrome://agent/",
      "chrome-extension://abcdefghijklmnopabcdefghijklmnop/index.html",
      "devtools://devtools/bundled/inspector.html",
      "file:///tmp/dao-mcp-forbidden.html",
      "data:text/plain,forbidden",
      "custom-scheme://forbidden/",
  };
  for (std::string_view forbidden_url : kForbiddenUrls) {
    auto result = ValidateExternalTargetUrl(GURL(forbidden_url));
    ASSERT_FALSE(result.has_value()) << forbidden_url;
    EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, result.error().code)
        << forbidden_url;
  }
}

IN_PROC_BROWSER_TEST_F(DaoMcpEndToEndBrowserTest,
                       RejectsPopupOffTheRecordAndGuestProfiles) {
  Browser* popup = CreateBrowserForPopup(browser()->profile());
  ASSERT_NE(nullptr, popup);
  content::WebContents* popup_contents =
      popup->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, popup_contents);
  auto popup_result =
      ValidateExternalTarget(popup, popup->profile(), popup_contents);
  ASSERT_FALSE(popup_result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, popup_result.error().code);

  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_NE(nullptr, incognito);
  content::WebContents* incognito_contents =
      incognito->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, incognito_contents);
  auto incognito_result =
      ValidateExternalTarget(incognito, incognito->profile(), incognito_contents);
  ASSERT_FALSE(incognito_result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, incognito_result.error().code);

  Browser* guest = CreateGuestBrowser();
  ASSERT_NE(nullptr, guest);
  content::WebContents* guest_contents =
      guest->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, guest_contents);
  auto guest_result =
      ValidateExternalTarget(guest, guest->profile(), guest_contents);
  ASSERT_FALSE(guest_result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, guest_result.error().code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpEndToEndBrowserTest, TreatsMismatchedOwnerAsGone) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);
  Browser* other_window = CreateBrowser(browser()->profile());
  ASSERT_NE(nullptr, other_window);

  auto result = ValidateExternalTarget(other_window, browser()->profile(), contents);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetGone, result.error().code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpEndToEndBrowserTest,
                       LifecycleMonitorInvalidatesForbiddenNavigation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);
  DaoBrowserAutomationSession session(browser(), contents);
  base::test::TestFuture<DaoToolError> invalidated;
  DaoMcpSessionLifecycleMonitor monitor(&session, invalidated.GetCallback());
  monitor.Start();

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), GURL("data:text/html,<title>forbidden</title>")));

  EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, invalidated.Take().code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpEndToEndBrowserTest,
                       LifecycleMonitorFollowsExplicitEligibleRetarget) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  TabListInterface* tab_list = TabListInterface::From(browser());
  ASSERT_NE(nullptr, tab_list);
  tabs::TabInterface* original = tab_list->GetActiveTab();
  tabs::TabInterface* replacement =
      tab_list->OpenTab(GURL("about:blank"), TabStripModel::kNoTab, false);
  ASSERT_NE(nullptr, original);
  ASSERT_NE(nullptr, replacement);
  DaoBrowserAutomationSession session(browser(), original->GetContents());
  base::test::TestFuture<DaoToolError> invalidated;
  DaoMcpSessionLifecycleMonitor monitor(&session, invalidated.GetCallback());
  monitor.Start();

  session.SetTarget(replacement->GetContents());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));

  EXPECT_FALSE(invalidated.IsReady());
}

}  // namespace
}  // namespace dao
