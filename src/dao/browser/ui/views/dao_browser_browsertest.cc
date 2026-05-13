// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/command_line.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/startup/startup_browser_creator.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/picture_in_picture_browser_frame_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/document_picture_in_picture_window_controller.h"
#include "content/public/browser/picture_in_picture_window_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/base/hit_test.h"
#include "dao/browser/agent/dao_agent_memory_store.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "dao/browser/agent/dao_agent_scenario_registry.h"
#include "dao/browser/dao_auto_pip_visibility_helper.h"
#include "dao/browser/ui/views/dao_cross_window_drag.h"
#include "dao/browser/dao_webstore_branding_tab_helper.h"
#include "dao/browser/pip/dao_pip_interceptor.h"
#include "dao/browser/pip/dao_pip_site_rules.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_corner_overlay_view.h"
#include "dao/browser/ui/views/dao_tab_commands.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_view.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "ui/base/l10n/l10n_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom-shared.h"
#include "ui/compositor/layer.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "url/url_constants.h"

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

IN_PROC_BROWSER_TEST_F(DaoAddressBarBrowserTest,
                       AddressBarPathHasNoLeadingSpace) {
  ASSERT_TRUE(embedded_test_server()->Start());

  const GURL url = embedded_test_server()->GetURL("/hello/world?foo=bar");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));

  DaoAddressBarView* address_bar =
      GetBrowserView(browser())->dao_address_bar();
  ASSERT_NE(nullptr, address_bar);

  EXPECT_EQ(base::UTF8ToUTF16(url.host()),
            address_bar->GetHostTextForTesting());
  EXPECT_EQ(base::UTF8ToUTF16(std::string(url.path()) + "?" +
                              std::string(url.query())),
            address_bar->GetPathTextForTesting());
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

IN_PROC_BROWSER_TEST_F(DaoTabBrowserTest, DuplicateActiveTabInsertsAfterOriginal) {
  TabStripModel* model = browser()->tab_strip_model();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_EQ(3, model->count());

  model->ActivateTabAt(1);
  content::WebContents* original = model->GetWebContentsAt(1);
  ASSERT_NE(nullptr, original);

  ASSERT_TRUE(DuplicateActiveTab(browser()));
  ASSERT_EQ(4, model->count());
  EXPECT_EQ(2, model->active_index());
  EXPECT_EQ(original, model->GetWebContentsAt(1));

  content::WebContents* duplicate = model->GetWebContentsAt(2);
  ASSERT_NE(nullptr, duplicate);
  EXPECT_NE(original, duplicate);
  EXPECT_EQ(original->GetVisibleURL(), duplicate->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(DaoTabBrowserTest, DuplicateTabsGetDistinctSidebarTabIds) {
  TabStripModel* model = browser()->tab_strip_model();
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  model->ActivateTabAt(1);

  content::WebContents* original = model->GetWebContentsAt(1);
  ASSERT_NE(nullptr, original);
  const std::string original_id = GetSidebarTabId(original);

  ASSERT_TRUE(DuplicateActiveTab(browser()));
  content::WebContents* duplicate = model->GetWebContentsAt(2);
  ASSERT_NE(nullptr, duplicate);

  const std::string duplicate_id = GetSidebarTabId(duplicate);
  EXPECT_NE(original_id, duplicate_id);

  model->MoveWebContentsAt(2, 1, false);
  EXPECT_EQ(original_id, GetSidebarTabId(original));
  EXPECT_EQ(duplicate_id, GetSidebarTabId(duplicate));
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

// Regression: unsplit (close one pane via the keep-one helper) used to abort
// inside content::WebContentsViewMac::ViewsHostableDetach when the WebContents
// being detached had a stale views_host_. The detach path is now idempotent —
// the test verifies the deactivation flow runs to completion without DCHECK.
IN_PROC_BROWSER_TEST_F(DaoSplitViewBrowserTest, UnsplitDoesNotCrash) {
  DaoSplitView* split_view = GetBrowserView(browser())->dao_split_view();
  ASSERT_NE(nullptr, split_view);

  chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1, true);
  TabStripModel* model = browser()->tab_strip_model();
  content::WebContents* first = model->GetWebContentsAt(0);
  content::WebContents* second = model->GetWebContentsAt(1);
  model->ActivateTabAt(0);

  ASSERT_TRUE(split_view->SplitPane(
      first, SplitDirection::kHorizontal, false, second));
  ASSERT_TRUE(split_view->IsSplitActive());

  // Switch the active tab to a third, non-split tab. This is the path that
  // historically corrupted the primary ContentsWebView's internal wc pointer
  // because OnActiveTabChanged is intercepted while split is active.
  chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1, true);
  model->ActivateTabAt(2);

  // Bring the kept pane back to active and dissolve the split. Before the
  // idempotent-detach fix, this triggered DCHECK(views_host_) inside
  // ViewsHostableDetach during RebuildViews().
  split_view->UnsplitKeepingPane(first);

  EXPECT_FALSE(split_view->IsSplitActive());
  EXPECT_EQ(0, split_view->PaneCount());
}

// Regression: SplitPane with non-adjacent members must keep the anchor in
// place and preserve the moved member in the model. Sidebar visual adjacency
// is enforced separately at the JS layer (dao_folder_model.ts), so this test
// only locks down the model-side invariants of SplitPane itself.
IN_PROC_BROWSER_TEST_F(DaoSplitViewBrowserTest,
                       SplitMembersSurviveCreation) {
  DaoSplitView* split_view = GetBrowserView(browser())->dao_split_view();
  ASSERT_NE(nullptr, split_view);

  chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1, true);
  chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1, true);
  chrome::AddTabAt(browser(), GURL(url::kAboutBlankURL), -1, true);
  TabStripModel* model = browser()->tab_strip_model();
  ASSERT_GE(model->count(), 4);
  content::WebContents* anchor = model->GetWebContentsAt(0);
  content::WebContents* far_member = model->GetWebContentsAt(3);
  model->ActivateTabAt(0);

  ASSERT_TRUE(split_view->SplitPane(
      anchor, SplitDirection::kHorizontal, false, far_member));
  ASSERT_TRUE(split_view->IsSplitActive());

  EXPECT_EQ(0, model->GetIndexOfWebContents(anchor));
  EXPECT_NE(TabStripModel::kNoTab,
            model->GetIndexOfWebContents(far_member));
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

// =============================================================================
// DaoPipTopBarOverlayBrowserTest
//
// Exercises the Dao-specific customizations applied to the Chromium Document
// Picture-in-Picture frame view: the top bar is hosted in a separate overlay
// `views::Widget` so web contents can fill the entire inner area, the top bar
// container has its own compositor layer with rounded top corners, and the
// frame view no longer reserves layout space for the top bar.
// =============================================================================

// TODO(dao): Broken by v147 API drift — ui_test_utils::GetTestUrl was
// removed, BrowserView::frame() renamed, PictureInPictureBrowserFrameView
// members hidden. Wrap in #if 0 to unblock the rest of the test target.
// Re-enable after porting to the new helpers.
#if 0
class DaoPipTopBarOverlayBrowserTest : public InProcessBrowserTest {
 public:
  DaoPipTopBarOverlayBrowserTest() {
    scoped_feature_list_.InitWithFeatures(
        {blink::features::kDocumentPictureInPictureAPI}, {});
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  // Opens a document PiP window from the test's active tab and returns the
  // frame view of the child PiP browser. Returns null on failure.
  PictureInPictureBrowserFrameView* OpenDocumentPipAndGetFrameView() {
    const base::FilePath::CharType kDocumentPipPage[] =
        FILE_PATH_LITERAL("media/picture-in-picture/document-pip.html");
    GURL test_page_url = ui_test_utils::GetTestUrl(
        base::FilePath(base::FilePath::kCurrentDirectory),
        base::FilePath(kDocumentPipPage));
    if (!ui_test_utils::NavigateToURL(browser(), test_page_url)) {
      return nullptr;
    }

    content::WebContents* active_web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    if (!active_web_contents) {
      return nullptr;
    }

    auto* controller = content::PictureInPictureWindowController::
        GetOrCreateDocumentPictureInPictureController(active_web_contents);
    if (!controller) {
      return nullptr;
    }

    // Open a modestly sized PiP window. createDocumentPipWindow is defined in
    // chrome/test/data/media/picture-in-picture/document-pip.html.
    if (content::EvalJs(
            active_web_contents,
            "createDocumentPipWindow({width: 400, height: 300})") !=
        true) {
      return nullptr;
    }

    auto* child_web_contents = controller->GetChildWebContents();
    if (!child_web_contents) {
      return nullptr;
    }

    auto* pip_browser_view = static_cast<BrowserView*>(
        BrowserWindow::FindBrowserWindowWithWebContents(child_web_contents));
    if (!pip_browser_view || !pip_browser_view->browser() ||
        !pip_browser_view->browser()->is_type_picture_in_picture()) {
      return nullptr;
    }
    return static_cast<PictureInPictureBrowserFrameView*>(
        pip_browser_view->frame()->GetFrameView());
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

// The Dao PiP frame view should create a separate overlay Widget that hosts
// the top bar in its own native window. Before our change, the top bar was a
// direct child view of the frame view and no overlay widget existed.
IN_PROC_BROWSER_TEST_F(DaoPipTopBarOverlayBrowserTest,                        DISABLED_OverlayWidgetExistsAfterPipOpens) {
  auto* pip_frame_view = OpenDocumentPipAndGetFrameView();
  ASSERT_NE(nullptr, pip_frame_view);
  EXPECT_NE(nullptr, pip_frame_view->dao_top_bar_overlay_widget());
}

// GetTopAreaHeight() should only include the frame border top inset; it must
// NOT add kTopControlsHeight, because the top bar is hosted in the floating
// overlay widget and consumes no layout space in the main PiP window. This is
// the behavior that allows web contents to fill the full inner area.
IN_PROC_BROWSER_TEST_F(DaoPipTopBarOverlayBrowserTest,                        DISABLED_TopAreaHeightDoesNotReserveTopBar) {
  auto* pip_frame_view = OpenDocumentPipAndGetFrameView();
  ASSERT_NE(nullptr, pip_frame_view);
  EXPECT_EQ(pip_frame_view->FrameBorderInsets().top(),
            pip_frame_view->GetTopAreaHeight());
}

// The overlay widget's contents view (our top_bar_container_view_) must paint
// to a compositor layer so its opacity can be animated. Without the layer, the
// fade-in/out on hover would not work.
IN_PROC_BROWSER_TEST_F(DaoPipTopBarOverlayBrowserTest,                        DISABLED_TopBarContainerHasLayer) {
  auto* pip_frame_view = OpenDocumentPipAndGetFrameView();
  ASSERT_NE(nullptr, pip_frame_view);
  views::Widget* overlay = pip_frame_view->dao_top_bar_overlay_widget();
  ASSERT_NE(nullptr, overlay);
  views::View* container = overlay->GetContentsView();
  ASSERT_NE(nullptr, container);
  EXPECT_NE(nullptr, container->layer());
}

// The top bar container's layer should have rounded top corners (to match the
// PiP window's rounded top) but square bottom corners (so the bar meets the
// webcontents with a straight edge when fully opaque). Because the overlay is
// an independent NSWindow on macOS, the main window's corner clip does not
// apply to it — the radius must be set on the layer directly.
IN_PROC_BROWSER_TEST_F(DaoPipTopBarOverlayBrowserTest,                        DISABLED_TopBarContainerHasRoundedTopCorners) {
  auto* pip_frame_view = OpenDocumentPipAndGetFrameView();
  ASSERT_NE(nullptr, pip_frame_view);
  views::Widget* overlay = pip_frame_view->dao_top_bar_overlay_widget();
  ASSERT_NE(nullptr, overlay);
  views::View* container = overlay->GetContentsView();
  ASSERT_NE(nullptr, container);
  ASSERT_NE(nullptr, container->layer());
  const gfx::RoundedCornersF& corners =
      container->layer()->rounded_corner_radii();
  EXPECT_GT(corners.upper_left(), 0.f);
  EXPECT_GT(corners.upper_right(), 0.f);
  EXPECT_EQ(0.f, corners.lower_left());
  EXPECT_EQ(0.f, corners.lower_right());
  EXPECT_FLOAT_EQ(corners.upper_left(), corners.upper_right());
}

// =============================================================================
// DaoPipSiteRulesTest
//
// Pure-logic tests for the PiP site-rules matcher. Uses an InProcessBrowserTest
// fixture only for consistency with the rest of this file — no browser state
// is needed. Verifies bare-domain, www-prefixed, subdomain, and miss cases.
// =============================================================================

#endif  // DaoPipTopBarOverlayBrowserTest wrapped out until v147 port.

using DaoPipSiteRulesTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoPipSiteRulesTest, SeedRulesPresent) {
  const auto& rules = dao::GetAllPipSiteRules();
  ASSERT_FALSE(rules.empty());
  // The embedded rule table seeds bilibili.com.
  bool has_bilibili = false;
  for (const auto& r : rules) {
    if (r.domain == "bilibili.com") {
      has_bilibili = true;
      EXPECT_FALSE(r.target_selector.empty());
    }
  }
  EXPECT_TRUE(has_bilibili);
}

IN_PROC_BROWSER_TEST_F(DaoPipSiteRulesTest, MatchesBareDomain) {
  auto rule = dao::GetPipSiteRule(GURL("https://bilibili.com/video/BV1xx"));
  ASSERT_TRUE(rule.has_value());
  EXPECT_EQ("bilibili.com", rule->domain);
}

IN_PROC_BROWSER_TEST_F(DaoPipSiteRulesTest, MatchesWwwDomain) {
  auto rule = dao::GetPipSiteRule(GURL("https://www.bilibili.com/video/BV1xx"));
  ASSERT_TRUE(rule.has_value());
  EXPECT_EQ("bilibili.com", rule->domain);
}

IN_PROC_BROWSER_TEST_F(DaoPipSiteRulesTest, MatchesSubdomain) {
  auto rule = dao::GetPipSiteRule(GURL("https://live.bilibili.com/1234"));
  ASSERT_TRUE(rule.has_value());
  EXPECT_EQ("bilibili.com", rule->domain);
}

IN_PROC_BROWSER_TEST_F(DaoPipSiteRulesTest, DoesNotMatchUnrelatedDomain) {
  EXPECT_FALSE(dao::GetPipSiteRule(GURL("https://example.com/")).has_value());
  EXPECT_FALSE(
      dao::GetPipSiteRule(GURL("https://notbilibili.com/")).has_value());
}

IN_PROC_BROWSER_TEST_F(DaoPipSiteRulesTest, DoesNotMatchInvalidUrl) {
  EXPECT_FALSE(dao::GetPipSiteRule(GURL()).has_value());
  EXPECT_FALSE(dao::GetPipSiteRule(GURL("not a url")).has_value());
}

// =============================================================================
// DaoPipInterceptorTest
//
// Exercises DaoPipInterceptor::ShouldIntercept which reads the last committed
// URL from a WebContents and looks up the site rule. We use the mock host
// resolver to pretend the embedded test server is bilibili.com.
// =============================================================================

class DaoPipInterceptorTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
  }
};

IN_PROC_BROWSER_TEST_F(DaoPipInterceptorTest, ShouldInterceptNullIsFalse) {
  EXPECT_FALSE(dao::DaoPipInterceptor::ShouldIntercept(nullptr));
}

IN_PROC_BROWSER_TEST_F(DaoPipInterceptorTest, ShouldInterceptMatchingHost) {
  GURL url = embedded_test_server()->GetURL("bilibili.com", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);
  EXPECT_TRUE(dao::DaoPipInterceptor::ShouldIntercept(contents));
}

IN_PROC_BROWSER_TEST_F(DaoPipInterceptorTest, ShouldNotInterceptNonMatchingHost) {
  GURL url = embedded_test_server()->GetURL("example.com", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);
  EXPECT_FALSE(dao::DaoPipInterceptor::ShouldIntercept(contents));
}

// =============================================================================
// DaoAgentScenarioRegistryTest
//
// Verifies seed scenarios, personal-scenario add/remove, and match priority
// (seeds first, then personal scenarios by acceptance rate).
// =============================================================================

using DaoAgentScenarioRegistryTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoAgentScenarioRegistryTest, HasSeedScenarios) {
  dao::DaoAgentScenarioRegistry registry;
  EXPECT_FALSE(registry.seed_scenarios().empty());
  EXPECT_TRUE(registry.personal_scenarios().empty());
}

IN_PROC_BROWSER_TEST_F(DaoAgentScenarioRegistryTest, MatchesSeedPrPattern) {
  dao::DaoAgentScenarioRegistry registry;
  auto match =
      registry.Match("https://github.com/foo/bar/pull/42");
  ASSERT_TRUE(match.has_value());
  EXPECT_EQ("seed_github_pr", match->id);
}

IN_PROC_BROWSER_TEST_F(DaoAgentScenarioRegistryTest, MatchesSeedIssuePattern) {
  dao::DaoAgentScenarioRegistry registry;
  auto match =
      registry.Match("https://github.com/foo/bar/issues/7");
  ASSERT_TRUE(match.has_value());
  EXPECT_EQ("seed_github_issue", match->id);
}

IN_PROC_BROWSER_TEST_F(DaoAgentScenarioRegistryTest, NoMatchForUnrelatedUrl) {
  dao::DaoAgentScenarioRegistry registry;
  EXPECT_FALSE(registry.Match("https://example.com/").has_value());
}

IN_PROC_BROWSER_TEST_F(DaoAgentScenarioRegistryTest, AddAndRemovePersonal) {
  dao::DaoAgentScenarioRegistry registry;

  dao::ScenarioDefinition s;
  s.id = "p_1";
  s.type = "personal";
  s.name = "Custom";
  s.url_pattern = R"(^https://example\.com/app)";
  s.action_label = "custom";
  registry.AddPersonalScenario(s);

  EXPECT_EQ(1u, registry.personal_scenarios().size());

  auto match = registry.Match("https://example.com/app/home");
  ASSERT_TRUE(match.has_value());
  EXPECT_EQ("p_1", match->id);

  registry.RemovePersonalScenario("p_1");
  EXPECT_TRUE(registry.personal_scenarios().empty());
  EXPECT_FALSE(registry.Match("https://example.com/app/home").has_value());
}

IN_PROC_BROWSER_TEST_F(DaoAgentScenarioRegistryTest, SeedBeatsPersonalOnConflict) {
  dao::DaoAgentScenarioRegistry registry;

  // A personal scenario that would match a GitHub PR URL.
  dao::ScenarioDefinition s;
  s.id = "p_pr";
  s.type = "personal";
  s.url_pattern = R"(github\.com)";
  s.times_triggered = 10;
  s.times_accepted = 10;  // 100% acceptance
  registry.AddPersonalScenario(s);

  // Seed must still win because seed scenarios take priority.
  auto match = registry.Match("https://github.com/foo/bar/pull/1");
  ASSERT_TRUE(match.has_value());
  EXPECT_EQ("seed_github_pr", match->id);
}

// =============================================================================
// DaoAgentMemoryStoreTest
//
// Creates a store backed by a temp-directory SQLite file and verifies
// initialization plus round-trip of the primary data types.
//
// NOTE: These tests are DISABLED_ because in the browser_tests environment
// the DaoAgentMemoryStore's schema-creation step ("CREATE VIRTUAL TABLE
// ... USING fts5 ...") triggers SQLITE_ERROR via the database error callback,
// which in turn calls RazeAndPoison() and causes Init() to fail. In production
// the store is owned by a KeyedService and runs on a worker sequence where
// this path is exercised indirectly; direct instantiation from a browser test
// trips the poisoning logic. Re-enable with a ScopedErrorExpecter or by
// running via DaoAgentMemoryService once those plumbing changes are in.
// =============================================================================

class DaoAgentMemoryStoreTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  base::FilePath db_path() const {
    return temp_dir_.GetPath().AppendASCII("dao_agent_memory.db");
  }

 protected:
  base::ScopedTempDir temp_dir_;
};

IN_PROC_BROWSER_TEST_F(DaoAgentMemoryStoreTest, DISABLED_InitCreatesDatabase) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  dao::DaoAgentMemoryStore store(db_path());
  EXPECT_TRUE(store.Init());
  EXPECT_TRUE(base::PathExists(db_path()));
}

IN_PROC_BROWSER_TEST_F(DaoAgentMemoryStoreTest, DISABLED_PreferenceRoundTrip) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  dao::DaoAgentMemoryStore store(db_path());
  ASSERT_TRUE(store.Init());

  EXPECT_TRUE(
      store.MergePreference("prefers_language", "English", 0.9));

  auto prefs = store.GetPreferences(/*limit=*/10, /*min_confidence=*/0.0);
  ASSERT_FALSE(prefs.empty());
  bool found = false;
  for (const auto& p : prefs) {
    if (p.key == "prefers_language") {
      EXPECT_EQ("English", p.value);
      EXPECT_NEAR(0.9, p.confidence, 1e-6);
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

IN_PROC_BROWSER_TEST_F(DaoAgentMemoryStoreTest, DISABLED_EpisodeSaveAndRetrieve) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  dao::DaoAgentMemoryStore store(db_path());
  ASSERT_TRUE(store.Init());

  dao::Episode ep;
  ep.domain = "example.com";
  ep.url = "https://example.com/path";
  ep.title = "Example";
  ep.intent = "read";
  ep.outcome = "ok";
  ep.timestamp = base::Time::Now();
  ep.confidence = 0.75;
  ASSERT_TRUE(store.SaveEpisode(ep));

  auto episodes = store.GetEpisodesByDomain("example.com", /*limit=*/10);
  EXPECT_FALSE(episodes.empty());
}

IN_PROC_BROWSER_TEST_F(DaoAgentMemoryStoreTest,
                       DISABLED_PersonalScenarioRoundTrip) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  dao::DaoAgentMemoryStore store(db_path());
  ASSERT_TRUE(store.Init());

  dao::ScenarioDefinition s;
  s.id = "p_test";
  s.type = "personal";
  s.name = "Test Scenario";
  s.url_pattern = R"(example\.com)";
  s.action_label = "custom_action";
  s.created_at = base::Time::Now();
  ASSERT_TRUE(store.SavePersonalScenario(s));

  auto scenarios = store.GetPersonalScenarios();
  ASSERT_EQ(1u, scenarios.size());
  EXPECT_EQ("p_test", scenarios[0].id);

  EXPECT_TRUE(store.DeleteScenario("p_test"));
  EXPECT_TRUE(store.GetPersonalScenarios().empty());
}

IN_PROC_BROWSER_TEST_F(DaoAgentMemoryStoreTest, DISABLED_ClearAllEmptiesStore) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  dao::DaoAgentMemoryStore store(db_path());
  ASSERT_TRUE(store.Init());

  store.MergePreference("k", "v", 0.5);
  dao::Episode ep;
  ep.domain = "a.com";
  ep.timestamp = base::Time::Now();
  store.SaveEpisode(ep);

  ASSERT_TRUE(store.ClearAll());
  EXPECT_TRUE(store.GetPreferences(10, 0.0).empty());
  EXPECT_TRUE(store.GetEpisodesByDomain("a.com", 10).empty());
}

// =============================================================================
// DaoAutoPipVisibilityHelperBrowserTest
//
// Verifies the helper is auto-attached to every WebContents via tab_helpers.cc
// and that CreateForWebContents is idempotent (same instance returned).
// =============================================================================

using DaoAutoPipVisibilityHelperBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoAutoPipVisibilityHelperBrowserTest,
                       AutoAttachedToWebContents) {
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);

  // Helper is auto-installed by AttachTabHelpers for every created tab.
  auto* helper = dao::DaoAutoPipVisibilityHelper::FromWebContents(contents);
  EXPECT_NE(nullptr, helper);

  // CreateForWebContents is idempotent — calling again should not replace.
  dao::DaoAutoPipVisibilityHelper::CreateForWebContents(contents);
  EXPECT_EQ(helper, dao::DaoAutoPipVisibilityHelper::FromWebContents(contents));
}

// =============================================================================
// DaoWebstoreBrandingTabHelperBrowserTest
//
// Verifies the helper is auto-attached to every WebContents via tab_helpers.cc.
// The actual script injection only runs on webstore URLs.
// =============================================================================

using DaoWebstoreBrandingTabHelperBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoWebstoreBrandingTabHelperBrowserTest,
                       AutoAttachedToWebContents) {
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);

  auto* helper = dao::DaoWebstoreBrandingTabHelper::FromWebContents(contents);
  EXPECT_NE(nullptr, helper);

  dao::DaoWebstoreBrandingTabHelper::CreateForWebContents(contents);
  EXPECT_EQ(helper, dao::DaoWebstoreBrandingTabHelper::FromWebContents(contents));
}

// =============================================================================
// DaoToastViewBrowserTest
//
// Verifies the toast view provided by BrowserView: it exists, is initially
// hidden, and becomes visible with the correct label after ShowToast.
// =============================================================================

using DaoToastViewBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoToastViewBrowserTest, ToastExists) {
  auto* toast = GetBrowserView(browser())->dao_toast();
  ASSERT_NE(nullptr, toast);
  EXPECT_FALSE(toast->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoToastViewBrowserTest, ShowToastMakesVisible) {
  auto* toast = GetBrowserView(browser())->dao_toast();
  ASSERT_NE(nullptr, toast);

  toast->ShowToast(u"Hello, Dao");
  EXPECT_TRUE(toast->GetVisible());
  // Non-zero preferred size after text is laid out.
  EXPECT_GT(toast->GetPreferredSize().width(), 0);
  EXPECT_GT(toast->GetPreferredSize().height(), 0);
}

// =============================================================================
// DaoControlCenterPopupBrowserTest
//
// Verifies ShowAt / Hide / panel-switching on the control-center popup that
// BrowserView creates for regular browser windows.
// =============================================================================

using DaoControlCenterPopupBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoControlCenterPopupBrowserTest, PopupExists) {
  auto* popup = GetBrowserView(browser())->dao_control_center_popup();
  ASSERT_NE(nullptr, popup);
  EXPECT_FALSE(popup->GetVisible());
  EXPECT_EQ(browser(), popup->browser());
}

IN_PROC_BROWSER_TEST_F(DaoControlCenterPopupBrowserTest, ShowAtAndHide) {
  auto* popup = GetBrowserView(browser())->dao_control_center_popup();
  ASSERT_NE(nullptr, popup);

  popup->ShowAt(gfx::Point(100, 100));
  EXPECT_TRUE(popup->GetVisible());

  popup->Hide();
  EXPECT_FALSE(popup->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoControlCenterPopupBrowserTest, SwitchSubPanels) {
  auto* popup = GetBrowserView(browser())->dao_control_center_popup();
  ASSERT_NE(nullptr, popup);

  popup->ShowAt(gfx::Point(100, 100));
  // These should not crash — they toggle internal sub-panel visibility.
  popup->ShowQrView();
  popup->ShowMoreMenu();
  popup->ShowMainPanel();
  EXPECT_TRUE(popup->GetVisible());

  popup->Hide();
}

// =============================================================================
// DaoLittleDaoViewBrowserTest
//
// Verifies that a regular browser has no Little Dao view (the view is only
// created inside Little Dao popup windows), and that its kBarHeight constant
// is a positive value.
// =============================================================================

using DaoLittleDaoViewBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoLittleDaoViewBrowserTest,
                       RegularBrowserHasNoLittleDaoView) {
  // The default browser in this test is TYPE_NORMAL, so Little Dao view
  // should NOT be created.
  EXPECT_EQ(nullptr, GetBrowserView(browser())->dao_little_dao_view());
}

IN_PROC_BROWSER_TEST_F(DaoLittleDaoViewBrowserTest, BarHeightIsPositive) {
  static_assert(dao::DaoLittleDaoView::kBarHeight > 0,
                "Bar height must be positive");
  EXPECT_GT(dao::DaoLittleDaoView::kBarHeight, 0);
}

IN_PROC_BROWSER_TEST_F(DaoLittleDaoViewBrowserTest,
                       RegularBrowserIsNotLittleDaoWindow) {
  EXPECT_FALSE(dao::DaoLittleDaoController::IsLittleDaoWindow(browser()));
  EXPECT_FALSE(dao::DaoLittleDaoController::IsCreatingLittleDao());
}

// =============================================================================
// DaoAgentWebUILoadTest
//
// Navigates to chrome://agent/ and asserts the vendored pi-mono runtime
// loads far enough for the top-level <dao-agent-app> custom element to
// register. Captures every console message emitted during load; any
// message at severity kError fails the test with the full JS error string
// attached so regressions in the vendored bundle (CSP refusals,
// unresolved specifiers, stubbed-dep misuse, etc.) surface directly in
// CI output.
// =============================================================================

using DaoAgentWebUILoadTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoAgentWebUILoadTest, LoadsWithoutConsoleErrors) {
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  content::WebContentsConsoleObserver observer(web_contents);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://agent/")));

  // Wait for the top-level custom element registration. If the module
  // graph fails to evaluate (CSP, unresolved specifier, TypeError during
  // top-level import), whenDefined never resolves and EvalJs times out —
  // the captured console errors below explain why.
  constexpr char kWaitScript[] = R"(
    (async () => {
      await customElements.whenDefined('dao-agent-app');
      return !!document.querySelector('dao-agent-app');
    })()
  )";
  EXPECT_EQ(true, content::EvalJs(web_contents, kWaitScript));

  std::vector<std::string> errors;
  for (const auto& msg : observer.messages()) {
    if (msg.log_level == blink::mojom::ConsoleMessageLevel::kError) {
      errors.push_back(base::UTF16ToUTF8(msg.message));
    }
  }
  EXPECT_TRUE(errors.empty())
      << "chrome://agent/ emitted console errors during load:\n - "
      << base::JoinString(errors, "\n - ");
}

// =============================================================================
// DaoAgentShareImageTest
//
// Direct EvalJs coverage for dao_share_image.ts. Imports the deployed
// module from chrome://agent/ and asserts the renderer's contract:
//   - Always returns a PNG Blob, even on degenerate inputs
//   - Skips the question bubble when question is blank, producing a
//     visibly shorter image than the with-bubble case
//   - Falls back gracefully when answer is also blank (renderer's '—'
//     fallback path; copy/share callers should pre-filter, but the
//     renderer must not crash if they don't).
// =============================================================================

using DaoAgentShareImageTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoAgentShareImageTest, RendersValidPngWithBubble) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://agent/")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  constexpr char kScript[] = R"(
    (async () => {
      const m = await import('chrome://agent/dao_share_image.js');
      const blob = await m.renderShareImage({
        question: 'What is the capital of France?',
        answer: 'Paris is the capital of France.',
      });
      if (!(blob instanceof Blob)) return 'not-a-blob';
      if (blob.type !== 'image/png') return 'wrong-type:' + blob.type;
      if (blob.size <= 0) return 'empty-blob';
      return 'ok';
    })()
  )";
  EXPECT_EQ("ok", content::EvalJs(web_contents, kScript));
}

IN_PROC_BROWSER_TEST_F(DaoAgentShareImageTest, SkipsBubbleWhenQuestionEmpty) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://agent/")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  // Same answer body, two captures: one with a real question, one with
  // an empty question (attachment-only send). The bubbleless image must
  // be strictly smaller than the bubbled one — that's the contract the
  // hasBubble branch in renderShareImage was added to enforce.
  // Note: PNG byte size correlates with rendered area but is not strictly
  // monotonic with height because of compression of the flat background.
  // We instead read the decoded ImageBitmap height to compare layouts.
  constexpr char kScript[] = R"(
    (async () => {
      const m = await import('chrome://agent/dao_share_image.js');
      const answer = 'Paris is the capital of France.';
      const blobWith = await m.renderShareImage({question: 'q', answer});
      const blobNo = await m.renderShareImage({question: '', answer});
      const bmpWith = await createImageBitmap(blobWith);
      const bmpNo = await createImageBitmap(blobNo);
      if (bmpNo.height >= bmpWith.height) {
        return 'bubble-not-skipped:' + bmpNo.height + '>=' + bmpWith.height;
      }
      // Whitespace-only question must be treated identically to empty.
      const blobWs = await m.renderShareImage({question: '   ', answer});
      const bmpWs = await createImageBitmap(blobWs);
      if (bmpWs.height !== bmpNo.height) {
        return 'whitespace-not-trimmed:' + bmpWs.height + '!=' + bmpNo.height;
      }
      return 'ok';
    })()
  )";
  EXPECT_EQ("ok", content::EvalJs(web_contents, kScript));
}

IN_PROC_BROWSER_TEST_F(DaoAgentShareImageTest, HandlesEmptyAnswerWithoutCrash) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://agent/")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  // copyAssistantText_ / shareAssistantAsImage_ are supposed to short-
  // circuit before reaching the renderer in this case, but the renderer
  // still has a '—' fallback for direct callers. Make sure that path
  // doesn't throw.
  constexpr char kScript[] = R"(
    (async () => {
      const m = await import('chrome://agent/dao_share_image.js');
      const blob = await m.renderShareImage({question: '', answer: ''});
      if (!(blob instanceof Blob)) return 'not-a-blob';
      if (blob.type !== 'image/png') return 'wrong-type:' + blob.type;
      if (blob.size <= 0) return 'empty-blob';
      return 'ok';
    })()
  )";
  EXPECT_EQ("ok", content::EvalJs(web_contents, kScript));
}

// =============================================================================
// DaoAgentMarkdownTest
//
// Verifies that copyAssistantText_'s Markdown→HTML renderer (used to
// populate the text/html clipboard slot) emits standard HTML for the
// shapes assistant replies typically contain — headings, lists, inline
// code, emphasis, fenced code, links. If this regresses, copy-as-html
// silently degrades back to text/plain.
//
// We drive the renderer through the <dao-chat-view> testing hook
// (_daoTestRenderMarkdownToHtml). Re-importing pi_runtime_bundle.js in
// EvalJs would double-register lit-html's TrustedTypePolicy and crash
// the test page; the hook reuses the bundle the page already loaded.
// =============================================================================

using DaoAgentMarkdownTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoAgentMarkdownTest, RendersAssistantMarkdownToHtml) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://agent/")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  constexpr char kScript[] = R"(
    (async () => {
      await customElements.whenDefined('dao-chat-view');
      const view = await new Promise(resolve => {
        const found = document.querySelector('dao-chat-view');
        if (found) return resolve(found);
        const mo = new MutationObserver(() => {
          const v = document.querySelector('dao-chat-view');
          if (v) { mo.disconnect(); resolve(v); }
        });
        mo.observe(document.body, {childList: true, subtree: true});
      });
      if (typeof view._daoTestRenderMarkdownToHtml !== 'function') {
        return 'no-test-hook';
      }
      const md = '# Heading\n\n' +
                 'A paragraph with **bold** and *em* and `inline`.\n\n' +
                 '- item one\n- item two\n\n' +
                 '[link](https://example.com)\n\n' +
                 '```js\nlet x = 1;\n```';
      const html = view._daoTestRenderMarkdownToHtml(md);
      if (typeof html !== 'string') return 'not-a-string:' + typeof html;
      const want = ['<h1', '<strong', '<em', '<code',
                    '<ul', '<li', '<a', 'example.com'];
      const missing = want.filter(s => !html.includes(s));
      if (missing.length) return 'missing:' + missing.join(',') +
                                 ' html=' + html.slice(0, 200);
      // Empty input must not throw, and must not produce the <pre>
      // fallback (which would only kick in on a renderer exception).
      const empty = view._daoTestRenderMarkdownToHtml('');
      if (typeof empty !== 'string') return 'empty-not-string';
      return 'ok';
    })()
  )";
  EXPECT_EQ("ok", content::EvalJs(web_contents, kScript));
}

// =============================================================================
// DaoAgentAssistantActionsTest
//
// Covers the "after restoring history, copy/share/retry buttons appear
// on the last assistant bubble" path. We can't drive a full LLM round-
// trip from a browser test, so we drive the injector directly: stage
// a fake <assistant-message> in the chat panel, call the testing hook
// on <dao-chat-view>, and assert .dao-assistant-actions was attached
// after the last bubble. This is the same DOM contract loadSession_ now
// re-runs after hydrating IndexedDB messages.
// =============================================================================

using DaoAgentAssistantActionsTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoAgentAssistantActionsTest,
                       AttachesActionRowToLastAssistantMessage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("chrome://agent/")));
  content::WebContents* web_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(web_contents);

  constexpr char kScript[] = R"(
    (async () => {
      await customElements.whenDefined('dao-agent-app');
      // <dao-chat-view> is attached lazily inside <dao-agent-app>; wait
      // for both the element to register and an instance to mount.
      await customElements.whenDefined('dao-chat-view');
      const view = await new Promise(resolve => {
        const found = document.querySelector('dao-chat-view');
        if (found) return resolve(found);
        const mo = new MutationObserver(() => {
          const v = document.querySelector('dao-chat-view');
          if (v) { mo.disconnect(); resolve(v); }
        });
        mo.observe(document.body, {childList: true, subtree: true});
      });
      // Wait for view's panel_ wiring (mount_ resolves panel_ after the
      // first updateComplete).
      for (let i = 0; i < 20 && !view.querySelector('pi-chat-panel'); i++) {
        await new Promise(r => setTimeout(r, 50));
      }
      const panel = view.querySelector('pi-chat-panel');
      if (!panel) return 'no-panel';
      // Stage two fake assistant bubbles. refreshAssistantActions_ only
      // attaches the row to the last one — we assert exactly that.
      const a1 = document.createElement('assistant-message');
      a1.setAttribute('data-test', 'first');
      const a2 = document.createElement('assistant-message');
      a2.setAttribute('data-test', 'last');
      panel.appendChild(a1);
      panel.appendChild(a2);

      if (typeof view._daoTestRefreshAssistantActions !== 'function') {
        return 'no-test-hook';
      }
      view._daoTestRefreshAssistantActions();

      const rows = panel.querySelectorAll('.dao-assistant-actions');
      if (rows.length !== 1) return 'wrong-row-count:' + rows.length;
      const row = rows[0];
      // The injector inserts the row as a sibling immediately after the
      // last <assistant-message>.
      if (row.previousElementSibling !== a2) {
        return 'row-not-after-last:' +
               (row.previousElementSibling &&
                row.previousElementSibling.getAttribute('data-test'));
      }
      // Confirm all three buttons are present (copy / share / retry).
      const haveCopy = !!row.querySelector('.dao-copy-btn');
      const haveShare = !!row.querySelector('.dao-share-btn');
      const haveRetry = !!row.querySelector('.dao-retry-btn');
      if (!haveCopy || !haveShare || !haveRetry) {
        return 'missing-btn copy=' + haveCopy +
               ' share=' + haveShare + ' retry=' + haveRetry;
      }

      // Idempotency: a second refresh must not duplicate rows.
      view._daoTestRefreshAssistantActions();
      const rows2 = panel.querySelectorAll('.dao-assistant-actions');
      if (rows2.length !== 1) return 'duplicated:' + rows2.length;
      return 'ok';
    })()
  )";
  EXPECT_EQ("ok", content::EvalJs(web_contents, kScript));
}

// TODO(dao): Broken by v147 API drift — NativeTheme::set_use_dark_colors
// was removed and Background::get_color is now protected. Wrap in #if 0
// until ported to ColorProvider-based API.
#if 0
class DaoDarkModeBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoDarkModeBrowserTest, DISABLED_DarkMode_SidebarBackground_Light) {
  auto* theme = ui::NativeTheme::GetInstanceForNativeUi();
  theme->set_use_dark_colors(false);
  theme->NotifyOnNativeThemeUpdated();

  EXPECT_EQ(dao::SidebarBackground(), SkColorSetRGB(231, 238, 245));
  EXPECT_FALSE(dao::IsDarkMode());
}

IN_PROC_BROWSER_TEST_F(DaoDarkModeBrowserTest, DISABLED_DarkMode_SidebarBackground_Dark) {
  auto* theme = ui::NativeTheme::GetInstanceForNativeUi();
  theme->set_use_dark_colors(true);
  theme->NotifyOnNativeThemeUpdated();

  EXPECT_EQ(dao::SidebarBackground(), SkColorSetRGB(54, 59, 64));
  EXPECT_TRUE(dao::IsDarkMode());

  // Restore default for subsequent tests.
  theme->set_use_dark_colors(false);
  theme->NotifyOnNativeThemeUpdated();
}

IN_PROC_BROWSER_TEST_F(DaoDarkModeBrowserTest, DISABLED_DarkMode_SidebarRepaintsOnThemeChange) {
  auto* theme = ui::NativeTheme::GetInstanceForNativeUi();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* sidebar = view->dao_sidebar();
  ASSERT_TRUE(sidebar);

  // DaoSidebarView paints its background on its inner container (the first
  // child view), not on itself. The inner container is populated in the
  // constructor, so it is guaranteed to exist here.
  ASSERT_FALSE(sidebar->children().empty());
  views::View* inner = sidebar->children()[0];

  theme->set_use_dark_colors(true);
  theme->NotifyOnNativeThemeUpdated();

  // After the observer fires, the sidebar's background should match the
  // dark palette.
  auto* bg = inner->background();
  ASSERT_TRUE(bg);
  EXPECT_EQ(bg->get_color(), SkColorSetRGB(54, 59, 64));

  theme->set_use_dark_colors(false);
  theme->NotifyOnNativeThemeUpdated();
  EXPECT_EQ(inner->background()->get_color(),
            SkColorSetRGB(231, 238, 245));
}

IN_PROC_BROWSER_TEST_F(DaoDarkModeBrowserTest, DISABLED_DarkMode_AccentUnchanged) {
  auto* theme = ui::NativeTheme::GetInstanceForNativeUi();

  theme->set_use_dark_colors(false);
  theme->NotifyOnNativeThemeUpdated();
  const SkColor light_accent = dao::SpaceActive();

  theme->set_use_dark_colors(true);
  theme->NotifyOnNativeThemeUpdated();
  const SkColor dark_accent = dao::SpaceActive();

  EXPECT_EQ(light_accent, dark_accent);
  EXPECT_EQ(light_accent, SkColorSetRGB(70, 120, 190));

  theme->set_use_dark_colors(false);
  theme->NotifyOnNativeThemeUpdated();
}
#endif  // DaoDarkModeBrowserTest wrapped out until v147 ColorProvider port.

// =============================================================================
// DaoSessionStartupBrowserTest
//
// Verifies the Dao patches that force "restore last session" as the default
// startup behavior (see
// src/patches/chrome/browser/prefs/session_startup_pref.cc.patch and
// src/patches/chrome/browser/ui/startup/startup_browser_creator.cc.patch).
// These tests guard against regressions where a Chromium rebase silently
// drops the patches and startup falls back to NTP / DEFAULT.
// =============================================================================

class DaoSessionStartupBrowserTest : public InProcessBrowserTest {};

// The static default returned by GetDefaultStartupType() must be LAST on all
// platforms — Dao patches away the ChromeOS-only branch so desktop always
// restores the previous session.
IN_PROC_BROWSER_TEST_F(DaoSessionStartupBrowserTest,
                       DefaultStartupTypeIsLast) {
  EXPECT_EQ(SessionStartupPref::LAST,
            SessionStartupPref::GetDefaultStartupType());
}

// For a fresh profile (no user-set or managed pref) the effective startup pref
// resolved by StartupBrowserCreator::GetSessionStartupPref must be LAST —
// this covers the first-run override patch in startup_browser_creator.cc.
IN_PROC_BROWSER_TEST_F(DaoSessionStartupBrowserTest,
                       GetSessionStartupPrefResolvesToLast) {
  base::CommandLine empty_command_line(base::CommandLine::NO_PROGRAM);
  SessionStartupPref pref = StartupBrowserCreator::GetSessionStartupPref(
      empty_command_line, browser()->profile());
  EXPECT_EQ(SessionStartupPref::LAST, pref.type);
}

// SessionStartupPref::TypeIsDefault returns true when the pref has never been
// explicitly set — combined with the patched GetDefaultStartupType the
// browser should still restore on launch.
IN_PROC_BROWSER_TEST_F(DaoSessionStartupBrowserTest,
                       FreshProfileUsesDefaultPref) {
  EXPECT_TRUE(SessionStartupPref::TypeIsDefault(browser()->profile()->GetPrefs()));
  EXPECT_EQ(SessionStartupPref::LAST,
            SessionStartupPref::GetDefaultStartupType());
}

// =============================================================================
// DaoNewTabBrowserTest
//
// Cmd+T / menu-new-tab should NOT create a blank tab on a sidebar window.
// Instead it overlays the command bar on the CURRENT tab; the command bar
// creates a real tab only when the user commits a URL. These tests guard
// that behavior — a regression (e.g., a future chrome rebase that removes
// the Dao NewTab() interception) would create an extra tab and a failure
// here would surface it immediately.
// =============================================================================

using DaoNewTabCommandBarBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoNewTabCommandBarBrowserTest,
                       UserGestureNewTabShowsCommandBarWithoutAddingTab) {
  const int tabs_before = browser()->tab_strip_model()->count();
  auto* command_bar = GetBrowserView(browser())->dao_command_bar();
  ASSERT_NE(nullptr, command_bar);
  EXPECT_FALSE(command_bar->GetVisible());

  // chrome::NewTab with a user-gesture context — the Cmd+T / menu path.
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);

  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count())
      << "Cmd+T should NOT add a tab when the sidebar is active; it should "
         "only surface the command bar on the current page.";
  EXPECT_TRUE(command_bar->GetVisible())
      << "Cmd+T on a sidebar window should make the command bar visible.";
}

IN_PROC_BROWSER_TEST_F(DaoNewTabCommandBarBrowserTest,
                       ProgrammaticNewTabStillCreatesTab) {
  const int tabs_before = browser()->tab_strip_model()->count();

  // kNoUserAction — e.g., session restore, programmatic intents. MUST keep
  // the real behavior (create a tab) because callers like session restore
  // depend on it.
  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);

  EXPECT_EQ(tabs_before + 1, browser()->tab_strip_model()->count())
      << "Programmatic NewTab (kNoUserAction) must still create a real tab.";
}

// =============================================================================
// DaoAddressBarHitTestBrowserTest
//
// The NonClientHitTest in browser_view.cc turns the entire strip above the
// content area into a window-drag region when the sidebar is active. That
// would make the address bar (which sits in that strip) un-clickable unless
// we explicitly exempt its bounds. Regression here breaks every address-bar
// button (back/forward/refresh) and the URL pill click-to-open-command-bar.
// =============================================================================

using DaoAddressBarHitTestBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoAddressBarHitTestBrowserTest,
                       AddressBarCenterIsHTCLIENT) {
  BrowserView* browser_view = GetBrowserView(browser());
  ASSERT_NE(nullptr, browser_view);
  auto* address_bar = browser_view->dao_address_bar();
  ASSERT_NE(nullptr, address_bar)
      << "Address bar must exist on a normal sidebar window.";

  // Force layout so dao_address_bar_->bounds() has valid coords relative
  // to BrowserView (view bounds are only set once layout has run).
  browser_view->DeprecatedLayoutImmediately();

  const gfx::Rect bounds = address_bar->bounds();
  ASSERT_FALSE(bounds.IsEmpty())
      << "Address bar bounds are empty — layout did not position it.";

  // Hit-test the center point of the address bar in BrowserView coords.
  const gfx::Point center = bounds.CenterPoint();
  const int hit = browser_view->NonClientHitTest(center);
  EXPECT_EQ(HTCLIENT, hit)
      << "The center of the address bar must be HTCLIENT so its buttons "
         "receive clicks. Got hit code " << hit
         << " (HTNOWHERE = window drag region = unclickable).";
}

// =============================================================================
// DaoLittleDaoControllerTrackerBrowserTest
//
// DaoLittleDaoController tracks Little Dao windows in a set of raw Browser*
// pointers. The tracker is backed by a BrowserListObserver that erases
// entries on OnBrowserRemoved so pointers never dangle. If someone swapped
// it back to a plain flat_set without the observer, IsLittleDaoWindow()
// could return true for a FRESHLY-ALLOCATED Browser* that happened to reuse
// an address of a closed Little Dao window — a dangerous false positive.
// =============================================================================

using DaoLittleDaoControllerTrackerBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoLittleDaoControllerTrackerBrowserTest,
                       UnrelatedBrowserNeverMatches) {
  // Default browser() is TYPE_NORMAL, never registered with the tracker.
  EXPECT_FALSE(
      dao::DaoLittleDaoController::IsLittleDaoWindow(browser()));
}

IN_PROC_BROWSER_TEST_F(DaoLittleDaoControllerTrackerBrowserTest,
                       NullBrowserIsFalse) {
  EXPECT_FALSE(dao::DaoLittleDaoController::IsLittleDaoWindow(nullptr));
}

IN_PROC_BROWSER_TEST_F(DaoLittleDaoControllerTrackerBrowserTest,
                       NotCreatingLittleDaoInBaselineState) {
  // Outside of an active OpenInLittleDao call the flag MUST be false,
  // otherwise every BrowserView constructor would take the Little Dao
  // branch and never render the sidebar.
  EXPECT_FALSE(dao::DaoLittleDaoController::IsCreatingLittleDao());
}

// =============================================================================
// DaoCrossWindowDragBrowserTest
//
// Covers the cross-window tab-drag path. HTML5 DnD can't be fully simulated
// in a browser test, but the two pieces that matter after the v147 rebase —
// payload parsing and the detach+insert move — are both pure C++ and are
// exercised directly here. These tests guard against future regressions of
// the sort that motivated the fix (renderer-initiated drags being silently
// dropped at BridgedContentView).
// =============================================================================

using DaoCrossWindowDragBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoCrossWindowDragBrowserTest, ParsePayload_Valid) {
  int sid = 0, idx = 0;
  EXPECT_TRUE(dao::ParseDaoTabDragPayload("dao-tab-drag:1234:5", &sid, &idx));
  EXPECT_EQ(1234, sid);
  EXPECT_EQ(5, idx);
}

IN_PROC_BROWSER_TEST_F(DaoCrossWindowDragBrowserTest,
                       ParsePayload_MissingPrefix) {
  int sid = 0, idx = 0;
  EXPECT_FALSE(dao::ParseDaoTabDragPayload("1234:5", &sid, &idx));
  EXPECT_FALSE(dao::ParseDaoTabDragPayload("other-prefix:1234:5", &sid, &idx));
}

IN_PROC_BROWSER_TEST_F(DaoCrossWindowDragBrowserTest,
                       ParsePayload_MalformedBody) {
  int sid = 0, idx = 0;
  EXPECT_FALSE(
      dao::ParseDaoTabDragPayload("dao-tab-drag:noColon", &sid, &idx));
  EXPECT_FALSE(dao::ParseDaoTabDragPayload("dao-tab-drag::5", &sid, &idx));
  EXPECT_FALSE(dao::ParseDaoTabDragPayload("dao-tab-drag:1234:", &sid, &idx));
  EXPECT_FALSE(
      dao::ParseDaoTabDragPayload("dao-tab-drag:abc:def", &sid, &idx));
}

IN_PROC_BROWSER_TEST_F(DaoCrossWindowDragBrowserTest,
                       MoveTabToOtherBrowser) {
  // Append a distinctive tab we'll move across windows.
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  TabStripModel* source_model = browser()->tab_strip_model();
  const int source_tabs_before = source_model->count();
  ASSERT_GE(source_tabs_before, 2)
      << "Need at least 2 tabs so one stays in the source window.";
  const int moved_index = source_tabs_before - 1;
  content::WebContents* moving_contents =
      source_model->GetWebContentsAt(moved_index);
  const int source_sid = browser()->session_id().id();

  // Open a second browser window for the same profile.
  Browser* target = CreateBrowser(browser()->profile());
  ASSERT_NE(nullptr, target);
  ASSERT_NE(browser(), target);
  const int target_tabs_before = target->tab_strip_model()->count();

  EXPECT_TRUE(dao::ExecuteCrossWindowTabMove(target, source_sid, moved_index,
                                              /*target_insert_index=*/0));

  EXPECT_EQ(source_tabs_before - 1, source_model->count())
      << "Source should have one fewer tab after move.";
  EXPECT_EQ(target_tabs_before + 1, target->tab_strip_model()->count())
      << "Target should have one more tab after move.";
  EXPECT_EQ(moving_contents, target->tab_strip_model()->GetWebContentsAt(0))
      << "Moved tab must appear at insert index 0.";
}

IN_PROC_BROWSER_TEST_F(DaoCrossWindowDragBrowserTest,
                       NullTargetReturnsFalse) {
  EXPECT_FALSE(dao::ExecuteCrossWindowTabMove(
      nullptr, browser()->session_id().id(), 0, 0));
}

IN_PROC_BROWSER_TEST_F(DaoCrossWindowDragBrowserTest,
                       SameSourceAndTargetReturnsFalse) {
  // Passing the same browser as both source and target should short-circuit
  // — a same-window move has its own code path (moveTab via sidebar UI).
  const int tabs_before = browser()->tab_strip_model()->count();
  EXPECT_FALSE(dao::ExecuteCrossWindowTabMove(browser(),
                                               browser()->session_id().id(),
                                               /*source_tab_index=*/0,
                                               /*target_insert_index=*/0));
  // No tab should have been detached.
  EXPECT_EQ(tabs_before, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(DaoCrossWindowDragBrowserTest,
                       UnknownSourceSessionReturnsFalse) {
  Browser* target = CreateBrowser(browser()->profile());
  ASSERT_NE(nullptr, target);
  EXPECT_FALSE(dao::ExecuteCrossWindowTabMove(target,
                                               /*source_session_id=*/999999,
                                               0, 0));
}

IN_PROC_BROWSER_TEST_F(DaoCrossWindowDragBrowserTest,
                       OutOfRangeSourceIndexReturnsFalse) {
  Browser* target = CreateBrowser(browser()->profile());
  ASSERT_NE(nullptr, target);
  const int source_tabs_before = browser()->tab_strip_model()->count();
  // Asking to detach far past the end must fail without touching either
  // model.
  EXPECT_FALSE(dao::ExecuteCrossWindowTabMove(
      target, browser()->session_id().id(),
      /*source_tab_index=*/source_tabs_before + 100, 0));
  EXPECT_EQ(source_tabs_before, browser()->tab_strip_model()->count());
}

IN_PROC_BROWSER_TEST_F(DaoCrossWindowDragBrowserTest,
                       ClampsTargetInsertIndex) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_GE(browser()->tab_strip_model()->count(), 2);
  const int last_source_index =
      browser()->tab_strip_model()->count() - 1;
  Browser* target = CreateBrowser(browser()->profile());
  ASSERT_NE(nullptr, target);
  const int target_tabs_before = target->tab_strip_model()->count();

  // Asking for a way-too-large insert index should be clamped to append.
  EXPECT_TRUE(dao::ExecuteCrossWindowTabMove(
      target, browser()->session_id().id(),
      /*source_tab_index=*/last_source_index,
      /*target_insert_index=*/9999));
  EXPECT_EQ(target_tabs_before + 1, target->tab_strip_model()->count());
}

// =============================================================================
// DaoI18nBrowserTest
// =============================================================================
//
// Smoke-checks that Dao's localized string resource bundle is wired up:
//   * IDS_DAO_* identifiers resolve to the source English copy by default.
//   * Substitution placeholders ($1, …) survive the lookup path.
// We deliberately avoid asserting on the zh-CN translation here because the
// browser test harness does not honor --lang the way the production binary
// does — that would have to be a chrome-launcher integration test. The
// English fallback assertion is the one that catches the most likely
// regression: the .pak failing to merge into the chrome locale bundle.

class DaoI18nBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoI18nBrowserTest, EnglishStringsResolve) {
  // A handful of well-known IDS_DAO_* values must match the canonical source
  // text from dao_strings.grd. A failure here usually means dao_strings.pak
  // didn't end up in the locale repack, or the include guard was elided.
  //
  // Note: grit performs typographic substitution at compile time — three
  // ASCII dots become the Unicode ellipsis U+2026. Match that here.
  EXPECT_EQ(u"Type a URL or search…",
            l10n_util::GetStringUTF16(IDS_DAO_COMMAND_BAR_PLACEHOLDER));
  EXPECT_EQ(u"Control Center",
            l10n_util::GetStringUTF16(
                IDS_DAO_CONTROL_CENTER_BUTTON_ACCESSIBLE_NAME));
  EXPECT_EQ(u"QR Code Result",
            l10n_util::GetStringUTF16(IDS_DAO_QR_RESULT_DIALOG_TITLE));
}

IN_PROC_BROWSER_TEST_F(DaoI18nBrowserTest, PlaceholderSubstitutionWorks) {
  // IDS_DAO_SUGGESTION_ASK_AI is "Ask AI: $1". $1 should be replaced by the
  // argument passed to GetStringFUTF16 — if the placeholder pipeline is
  // broken (e.g. the message body lost its $1), we'd see the literal "$1".
  std::u16string result = l10n_util::GetStringFUTF16(
      IDS_DAO_SUGGESTION_ASK_AI, u"hello world");
  EXPECT_EQ(u"Ask AI: hello world", result);
}

}  // namespace
}  // namespace dao
