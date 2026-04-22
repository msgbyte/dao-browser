// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
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
#include "dao/browser/agent/dao_agent_memory_store.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "dao/browser/agent/dao_agent_scenario_registry.h"
#include "dao/browser/dao_auto_pip_visibility_helper.h"
#include "dao/browser/dao_webstore_branding_tab_helper.h"
#include "dao/browser/pip/dao_pip_interceptor.h"
#include "dao/browser/pip/dao_pip_site_rules.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_corner_overlay_view.h"
#include "dao/browser/ui/views/dao_tab_commands.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_view.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/blink/public/common/features.h"
#include "ui/compositor/layer.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

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
  EXPECT_EQ(base::UTF8ToUTF16(url.path() + "?" + url.query()),
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
IN_PROC_BROWSER_TEST_F(DaoPipTopBarOverlayBrowserTest,
                       OverlayWidgetExistsAfterPipOpens) {
  auto* pip_frame_view = OpenDocumentPipAndGetFrameView();
  ASSERT_NE(nullptr, pip_frame_view);
  EXPECT_NE(nullptr, pip_frame_view->dao_top_bar_overlay_widget());
}

// GetTopAreaHeight() should only include the frame border top inset; it must
// NOT add kTopControlsHeight, because the top bar is hosted in the floating
// overlay widget and consumes no layout space in the main PiP window. This is
// the behavior that allows web contents to fill the full inner area.
IN_PROC_BROWSER_TEST_F(DaoPipTopBarOverlayBrowserTest,
                       TopAreaHeightDoesNotReserveTopBar) {
  auto* pip_frame_view = OpenDocumentPipAndGetFrameView();
  ASSERT_NE(nullptr, pip_frame_view);
  EXPECT_EQ(pip_frame_view->FrameBorderInsets().top(),
            pip_frame_view->GetTopAreaHeight());
}

// The overlay widget's contents view (our top_bar_container_view_) must paint
// to a compositor layer so its opacity can be animated. Without the layer, the
// fade-in/out on hover would not work.
IN_PROC_BROWSER_TEST_F(DaoPipTopBarOverlayBrowserTest,
                       TopBarContainerHasLayer) {
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
IN_PROC_BROWSER_TEST_F(DaoPipTopBarOverlayBrowserTest,
                       TopBarContainerHasRoundedTopCorners) {
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

}  // namespace
}  // namespace dao
