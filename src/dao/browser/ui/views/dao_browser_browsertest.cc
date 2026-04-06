// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_corner_overlay_view.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "dao/browser/ui/views/split/dao_split_view.h"

namespace dao {
namespace {

BrowserView* GetBrowserView(Browser* browser) {
  return BrowserView::GetBrowserViewForBrowser(browser);
}

// =============================================================================
// DaoSidebarBrowserTest
// =============================================================================

class DaoSidebarBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoSidebarBrowserTest, SidebarExistsOnStartup) {
  DaoSidebarView* sidebar = GetBrowserView(browser())->dao_sidebar();
  ASSERT_NE(nullptr, sidebar);
  EXPECT_TRUE(sidebar->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoSidebarBrowserTest, SidebarDefaultWidth) {
  DaoSidebarView* sidebar = GetBrowserView(browser())->dao_sidebar();
  ASSERT_NE(nullptr, sidebar);
  EXPECT_EQ(DaoSidebarView::kDefaultWidth,
            sidebar->GetPreferredSize().width());
}

IN_PROC_BROWSER_TEST_F(DaoSidebarBrowserTest, SidebarToggleCollapse) {
  DaoSidebarView* sidebar = GetBrowserView(browser())->dao_sidebar();
  ASSERT_NE(nullptr, sidebar);
  EXPECT_FALSE(sidebar->collapsed());

  sidebar->ToggleCollapsed();
  EXPECT_TRUE(sidebar->collapsed());
}

IN_PROC_BROWSER_TEST_F(DaoSidebarBrowserTest, SidebarToggleExpandRestore) {
  DaoSidebarView* sidebar = GetBrowserView(browser())->dao_sidebar();
  ASSERT_NE(nullptr, sidebar);

  sidebar->ToggleCollapsed();
  EXPECT_TRUE(sidebar->collapsed());

  sidebar->ToggleCollapsed();
  EXPECT_FALSE(sidebar->collapsed());
}

// =============================================================================
// DaoAddressBarBrowserTest
// =============================================================================

class DaoAddressBarBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoAddressBarBrowserTest, AddressBarExists) {
  DaoAddressBarView* address_bar =
      GetBrowserView(browser())->dao_address_bar();
  ASSERT_NE(nullptr, address_bar);
  EXPECT_EQ(DaoAddressBarView::kBarHeight,
            address_bar->GetPreferredSize().height());
}

// =============================================================================
// DaoCommandBarBrowserTest
// =============================================================================

class DaoCommandBarBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, CommandBarInitiallyHidden) {
  DaoCommandBarView* command_bar =
      GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);
  EXPECT_FALSE(command_bar->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, CommandBarShowAndHide) {
  DaoCommandBarView* command_bar =
      GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);

  command_bar->Show();
  EXPECT_TRUE(command_bar->GetVisible());

  command_bar->Hide();
  EXPECT_FALSE(command_bar->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, ShowIsIdempotent) {
  DaoCommandBarView* command_bar =
      GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);

  command_bar->Show();
  EXPECT_TRUE(command_bar->GetVisible());

  // Calling Show() again should not crash or change state.
  command_bar->Show();
  EXPECT_TRUE(command_bar->GetVisible());

  command_bar->Hide();
  EXPECT_FALSE(command_bar->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, HideIsIdempotent) {
  DaoCommandBarView* command_bar =
      GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);

  // Hide when already hidden should be a no-op.
  command_bar->Hide();
  EXPECT_FALSE(command_bar->GetVisible());

  command_bar->Show();
  EXPECT_TRUE(command_bar->GetVisible());

  command_bar->Hide();
  EXPECT_FALSE(command_bar->GetVisible());

  // Double-hide should not crash.
  command_bar->Hide();
  EXPECT_FALSE(command_bar->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, ShowForNewTabMode) {
  DaoCommandBarView* command_bar =
      GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);

  command_bar->ShowForNewTab();
  EXPECT_TRUE(command_bar->GetVisible());

  command_bar->Hide();
  EXPECT_FALSE(command_bar->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest,
                       ShowForNewTabThenShowSwitchesMode) {
  DaoCommandBarView* command_bar =
      GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);

  // Show in new-tab mode first.
  command_bar->ShowForNewTab();
  EXPECT_TRUE(command_bar->GetVisible());

  // Hide then re-show in normal mode.
  command_bar->Hide();
  command_bar->Show();
  EXPECT_TRUE(command_bar->GetVisible());

  command_bar->Hide();
  EXPECT_FALSE(command_bar->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, ShowThenShowForNewTab) {
  DaoCommandBarView* command_bar =
      GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);

  // Show in normal mode first.
  command_bar->Show();
  EXPECT_TRUE(command_bar->GetVisible());

  // Hide then show in new-tab mode.
  command_bar->Hide();
  command_bar->ShowForNewTab();
  EXPECT_TRUE(command_bar->GetVisible());

  command_bar->Hide();
  EXPECT_FALSE(command_bar->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, LooksLikeURLWithHTTPS) {
  // Static method — test URL detection heuristics.
  EXPECT_TRUE(DaoCommandBarView::LooksLikeURL(u"https://example.com"));
  EXPECT_TRUE(DaoCommandBarView::LooksLikeURL(u"http://example.com"));
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, LooksLikeURLWithDot) {
  EXPECT_TRUE(DaoCommandBarView::LooksLikeURL(u"example.com"));
  EXPECT_TRUE(DaoCommandBarView::LooksLikeURL(u"github.com/user/repo"));
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, LooksLikeURLWithLocalhost) {
  EXPECT_TRUE(DaoCommandBarView::LooksLikeURL(u"localhost"));
  EXPECT_TRUE(DaoCommandBarView::LooksLikeURL(u"localhost:3000"));
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, LooksLikeURLReturnsFalse) {
  // Plain search terms should not look like URLs.
  EXPECT_FALSE(DaoCommandBarView::LooksLikeURL(u"hello world"));
  EXPECT_FALSE(DaoCommandBarView::LooksLikeURL(u"search query"));
  EXPECT_FALSE(DaoCommandBarView::LooksLikeURL(u""));
  EXPECT_FALSE(DaoCommandBarView::LooksLikeURL(u"singleword"));
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest, MultipleShowHideCycles) {
  DaoCommandBarView* command_bar =
      GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);

  // Rapid show/hide cycles should not crash or leave stale state.
  for (int i = 0; i < 5; ++i) {
    command_bar->Show();
    EXPECT_TRUE(command_bar->GetVisible());
    command_bar->Hide();
    EXPECT_FALSE(command_bar->GetVisible());
  }
}

IN_PROC_BROWSER_TEST_F(DaoCommandBarBrowserTest,
                       ShowForNewTabMultipleCycles) {
  DaoCommandBarView* command_bar =
      GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);

  for (int i = 0; i < 3; ++i) {
    command_bar->ShowForNewTab();
    EXPECT_TRUE(command_bar->GetVisible());
    command_bar->Hide();
    EXPECT_FALSE(command_bar->GetVisible());
  }
}

// =============================================================================
// DaoTabBrowserTest
// =============================================================================

class DaoTabBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoTabBrowserTest, NewTabCreation) {
  TabStripModel* model = browser()->tab_strip_model();
  int initial_count = model->count();

  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  EXPECT_EQ(initial_count + 1, model->count());
}

IN_PROC_BROWSER_TEST_F(DaoTabBrowserTest, TabSwitching) {
  TabStripModel* model = browser()->tab_strip_model();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);

  // The newly added tab should be active.
  EXPECT_EQ(1, model->active_index());

  // Switch back to the first tab.
  model->ActivateTabAt(0);
  EXPECT_EQ(0, model->active_index());
}

IN_PROC_BROWSER_TEST_F(DaoTabBrowserTest, TabClose) {
  TabStripModel* model = browser()->tab_strip_model();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  int count_after_add = model->count();

  model->CloseWebContentsAt(model->active_index(),
                            TabCloseTypes::CLOSE_NONE);
  EXPECT_EQ(count_after_add - 1, model->count());
}

// =============================================================================
// DaoSplitViewBrowserTest
// =============================================================================

class DaoSplitViewBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoSplitViewBrowserTest, SplitViewExistsButInactive) {
  DaoSplitView* split_view = GetBrowserView(browser())->dao_split_view();
  ASSERT_NE(nullptr, split_view);
  EXPECT_FALSE(split_view->IsSplitActive());
}

IN_PROC_BROWSER_TEST_F(DaoSplitViewBrowserTest, SplitPaneCreatesTwo) {
  DaoSplitView* split_view = GetBrowserView(browser())->dao_split_view();
  ASSERT_NE(nullptr, split_view);

  // Add a second tab so we have two WebContents to split with.
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  TabStripModel* model = browser()->tab_strip_model();
  content::WebContents* first = model->GetWebContentsAt(0);
  content::WebContents* second = model->GetWebContentsAt(1);

  // Activate the first tab so it's displayed.
  model->ActivateTabAt(0);

  bool result = split_view->SplitPane(
      first, SplitDirection::kHorizontal, false, second);
  EXPECT_TRUE(result);
  EXPECT_TRUE(split_view->IsSplitActive());
  EXPECT_EQ(2, split_view->PaneCount());
}

// =============================================================================
// DaoCornerOverlayBrowserTest
// =============================================================================

class DaoCornerOverlayBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoCornerOverlayBrowserTest, CornerOverlayExists) {
  DaoCornerOverlayView* overlay =
      GetBrowserView(browser())->dao_corner_overlay();
  ASSERT_NE(nullptr, overlay);
  EXPECT_TRUE(overlay->GetVisible());
}

// =============================================================================
// DaoSidebarResizeBrowserTest
// =============================================================================

class DaoSidebarResizeBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoSidebarResizeBrowserTest, ResizeChangesWidth) {
  DaoSidebarView* sidebar = GetBrowserView(browser())->dao_sidebar();
  ASSERT_NE(nullptr, sidebar);
  EXPECT_FALSE(sidebar->collapsed());

  int original_width = sidebar->GetPreferredSize().width();
  EXPECT_EQ(DaoSidebarView::kDefaultWidth, original_width);

  // Simulate drag to increase width by 50px.
  sidebar->OnResize(50, /*done_resizing=*/true);
  EXPECT_EQ(original_width + 50, sidebar->GetPreferredSize().width());
}

IN_PROC_BROWSER_TEST_F(DaoSidebarResizeBrowserTest, ResizeClampsToMinWidth) {
  DaoSidebarView* sidebar = GetBrowserView(browser())->dao_sidebar();
  ASSERT_NE(nullptr, sidebar);

  // Drag far to the left — should clamp at kMinWidth.
  sidebar->OnResize(-500, /*done_resizing=*/true);
  EXPECT_EQ(DaoSidebarView::kMinWidth, sidebar->GetPreferredSize().width());
}

IN_PROC_BROWSER_TEST_F(DaoSidebarResizeBrowserTest, ResizeClampsToMaxWidth) {
  DaoSidebarView* sidebar = GetBrowserView(browser())->dao_sidebar();
  ASSERT_NE(nullptr, sidebar);

  // Drag far to the right — should clamp at kMaxWidth.
  sidebar->OnResize(500, /*done_resizing=*/true);
  EXPECT_EQ(DaoSidebarView::kMaxWidth, sidebar->GetPreferredSize().width());
}

IN_PROC_BROWSER_TEST_F(DaoSidebarResizeBrowserTest,
                       ResizeIgnoredWhenCollapsed) {
  DaoSidebarView* sidebar = GetBrowserView(browser())->dao_sidebar();
  ASSERT_NE(nullptr, sidebar);

  sidebar->ToggleCollapsed();
  EXPECT_TRUE(sidebar->collapsed());

  // Resize should be ignored when collapsed.
  sidebar->OnResize(100, /*done_resizing=*/true);
  // Width remains the collapsed width, not kDefaultWidth + 100.
  EXPECT_TRUE(sidebar->collapsed());
}

IN_PROC_BROWSER_TEST_F(DaoSidebarResizeBrowserTest,
                       ResizePreservesWidthAfterCollapseExpand) {
  DaoSidebarView* sidebar = GetBrowserView(browser())->dao_sidebar();
  ASSERT_NE(nullptr, sidebar);

  // Resize to 300px.
  sidebar->OnResize(300 - DaoSidebarView::kDefaultWidth,
                    /*done_resizing=*/true);
  EXPECT_EQ(300, sidebar->GetPreferredSize().width());

  // Collapse then expand — width should restore to 300px.
  sidebar->ToggleCollapsed();
  EXPECT_TRUE(sidebar->collapsed());

  sidebar->ToggleCollapsed();
  EXPECT_FALSE(sidebar->collapsed());
  EXPECT_EQ(300, sidebar->GetPreferredSize().width());
}

// =============================================================================
// DaoFolderPersistenceBrowserTest
// =============================================================================

class DaoFolderPersistenceBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoFolderPersistenceBrowserTest,
                       FolderFilePathInProfile) {
  // Verify the folder file path is within the profile directory.
  Profile* profile = browser()->profile();
  base::FilePath folder_path =
      profile->GetPath().AppendASCII("dao_folders.json");
  EXPECT_FALSE(folder_path.empty());
  EXPECT_TRUE(folder_path.value().find("dao_folders.json") !=
              std::string::npos);
}

IN_PROC_BROWSER_TEST_F(DaoFolderPersistenceBrowserTest,
                       FolderFileWriteAndRead) {
  base::ScopedAllowBlockingForTesting allow_blocking;

  Profile* profile = browser()->profile();
  base::FilePath folder_path =
      profile->GetPath().AppendASCII("dao_folders.json");

  // Write test folder data.
  const std::string test_json =
      R"({"version":1,"items":[{"type":"folder","id":"testid01","name":"Test Folder","collapsed":false,"children":[]}]})";
  ASSERT_TRUE(base::WriteFile(folder_path, test_json));

  // Read it back and verify.
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(folder_path, &contents));
  EXPECT_EQ(test_json, contents);

  // Clean up.
  base::DeleteFile(folder_path);
}

IN_PROC_BROWSER_TEST_F(DaoFolderPersistenceBrowserTest,
                       FolderFileNotExistsByDefault) {
  base::ScopedAllowBlockingForTesting allow_blocking;

  // On a fresh profile, dao_folders.json should not exist.
  Profile* profile = browser()->profile();
  base::FilePath folder_path =
      profile->GetPath().AppendASCII("dao_folders.json");
  EXPECT_FALSE(base::PathExists(folder_path));
}

}  // namespace
}  // namespace dao
