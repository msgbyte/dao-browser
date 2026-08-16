# Mini Dao Site Center Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Mini Dao-only site center icon inside the URL pill, with a popover for current-site extensions and site actions.

**Architecture:** `DaoLittleDaoView` owns the top-bar compound URL pill and opens a new Little Dao-only overlay, `DaoMiniDaoSiteCenterPopup`. The popup reuses `DaoControlCenterExtensionsSection` after adding host-surface callbacks, while Page Info, Share, and QR actions stay scoped to the active Mini Dao `WebContents`.

**Tech Stack:** Chromium C++ Views, Dao `src/dao` source injection through `dao_ui_sources.gni`, Chromium BrowserView patch files, Dao browser tests.

---

## File Structure

- Modify `src/dao/browser/ui/views/little_dao/dao_little_dao_view.h`: expose site-center bounds/testing hooks and replace the single URL button model.
- Modify `src/dao/browser/ui/views/little_dao/dao_little_dao_view.cc`: build the compound URL pill, paint the site-center icon, and open the popup.
- Create `src/dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.h`: Mini Dao-only overlay API.
- Create `src/dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.cc`: popover, extension section, Page Info, Share, QR panel, hide behavior.
- Modify `src/dao/browser/ui/views/dao_control_center_extensions_section.h`: add optional close and anchor callbacks for non-Control-Center hosts.
- Modify `src/dao/browser/ui/views/dao_control_center_extensions_section.cc`: use the callbacks when present, preserving normal Control Center behavior.
- Modify `src/dao/browser/ui/dao_ui_sources.gni`: include the new Mini Dao popup sources.
- Modify `src/dao/browser/strings/dao_strings.grd`: add localized strings for the Mini Dao site center.
- Modify `src/patches/chrome/browser/ui/views/frame/browser_view.h.patch`: add `DaoMiniDaoSiteCenterPopup` forward declaration, accessor, and raw pointer.
- Modify `src/patches/chrome/browser/ui/views/frame/browser_view.cc.patch`: create the popup for Little Dao windows and include it in hit testing.
- Modify `src/patches/chrome/browser/ui/views/frame/layout/browser_view_popup_layout_impl.cc.patch`: lay out the Mini Dao popup overlay.
- Modify `src/dao/browser/ui/views/dao_browser_browsertest.cc`: add focused browser tests.

No git staging or commits are part of this plan because project instructions forbid state-changing git commands unless the latest user message explicitly authorizes the exact action.

---

### Task 1: Add Failing Browser Tests

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Add the new include near the other Dao UI includes**

```cpp
#include "dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.h"
```

- [ ] **Step 2: Add focused tests after the existing `DaoLittleDaoViewBrowserTest` block**

```cpp
IN_PROC_BROWSER_TEST_F(DaoLittleDaoViewBrowserTest,
                       MiniDaoCreatesSiteCenterPopupButNotNormalControlCenter) {
  Browser* little_dao_browser = dao::DaoLittleDaoController::OpenInLittleDao(
      browser()->profile(), GURL("data:text/html,mini-site-center"));
  ASSERT_NE(nullptr, little_dao_browser);
  ASSERT_TRUE(dao::DaoLittleDaoController::IsLittleDaoWindow(little_dao_browser));

  BrowserView* little_browser_view = GetBrowserView(little_dao_browser);
  ASSERT_NE(nullptr, little_browser_view);
  EXPECT_NE(nullptr, little_browser_view->dao_little_dao_view());
  EXPECT_NE(nullptr, little_browser_view->dao_mini_dao_site_center_popup());
  EXPECT_EQ(nullptr, little_browser_view->dao_control_center_popup());

  BrowserRemovedWaiter removed(little_dao_browser);
  little_dao_browser->window()->Close();
  removed.Wait();
}

IN_PROC_BROWSER_TEST_F(DaoLittleDaoViewBrowserTest,
                       SiteCenterButtonIsHitTestable) {
  Browser* little_dao_browser = dao::DaoLittleDaoController::OpenInLittleDao(
      browser()->profile(), GURL("data:text/html,hit-test"));
  ASSERT_NE(nullptr, little_dao_browser);

  BrowserView* little_browser_view = GetBrowserView(little_dao_browser);
  ASSERT_NE(nullptr, little_browser_view);
  little_browser_view->DeprecatedLayoutImmediately();

  auto* little_view = little_browser_view->dao_little_dao_view();
  ASSERT_NE(nullptr, little_view);
  const gfx::Rect site_bounds = little_view->site_center_button_bounds();
  ASSERT_FALSE(site_bounds.IsEmpty());
  EXPECT_EQ(HTCLIENT,
            little_browser_view->NonClientHitTest(site_bounds.CenterPoint()));

  BrowserRemovedWaiter removed(little_dao_browser);
  little_dao_browser->window()->Close();
  removed.Wait();
}

IN_PROC_BROWSER_TEST_F(DaoLittleDaoViewBrowserTest,
                       SiteCenterPopupShowsWithoutMiniDaoExtractionButton) {
  Browser* little_dao_browser = dao::DaoLittleDaoController::OpenInLittleDao(
      browser()->profile(), GURL("data:text/html,site-center-popup"));
  ASSERT_NE(nullptr, little_dao_browser);

  BrowserView* little_browser_view = GetBrowserView(little_dao_browser);
  ASSERT_NE(nullptr, little_browser_view);
  little_browser_view->DeprecatedLayoutImmediately();

  auto* little_view = little_browser_view->dao_little_dao_view();
  ASSERT_NE(nullptr, little_view);
  auto* popup = little_browser_view->dao_mini_dao_site_center_popup();
  ASSERT_NE(nullptr, popup);
  EXPECT_FALSE(popup->GetVisible());

  little_view->ShowMiniDaoSiteCenterForTesting();
  EXPECT_TRUE(popup->GetVisible());
  EXPECT_EQ(nullptr, FindButtonWithAccessibleName(
                         popup, l10n_util::GetStringUTF16(
                                    IDS_DAO_CONTROL_CENTER_MINI_DAO)));
  EXPECT_NE(nullptr, FindButtonWithAccessibleName(
                         popup, l10n_util::GetStringUTF16(
                                    IDS_DAO_MINI_DAO_SITE_CENTER_PAGE_INFO)));

  popup->Hide();
  BrowserRemovedWaiter removed(little_dao_browser);
  little_dao_browser->window()->Close();
  removed.Wait();
}
```

- [ ] **Step 3: Run import and compile to verify the tests fail for missing APIs**

Run:

```bash
npm run import
npm run rebuild
```

Expected: build fails with unresolved references such as `dao_mini_dao_site_center_popup`, `dao_mini_dao_site_center_popup()`, `site_center_button_bounds()`, and `ShowMiniDaoSiteCenterForTesting()`.

---

### Task 2: Add Strings and Build Entries

**Files:**
- Modify: `src/dao/browser/strings/dao_strings.grd`
- Modify: `src/dao/browser/ui/dao_ui_sources.gni`

- [ ] **Step 1: Add Mini Dao site center strings after the existing Little Dao strings**

```xml
      <message name="IDS_DAO_MINI_DAO_SITE_CENTER_ACCESSIBLE_NAME" desc="Accessible name for the site center button inside the Mini Dao URL pill.">
        Site center
      </message>
      <message name="IDS_DAO_MINI_DAO_SITE_CENTER_EXTENSIONS_LABEL" desc="Section heading for extension actions inside the Mini Dao site center.">
        Extensions on this site
      </message>
      <message name="IDS_DAO_MINI_DAO_SITE_CENTER_PAGE_INFO" desc="Button label for opening Page Info from the Mini Dao site center.">
        Site settings
      </message>
      <message name="IDS_DAO_MINI_DAO_SITE_CENTER_SHARE" desc="Button label for sharing the current page from the Mini Dao site center.">
        Share
      </message>
      <message name="IDS_DAO_MINI_DAO_SITE_CENTER_QR_CODE" desc="Button label for showing a QR code for the current page from the Mini Dao site center.">
        QR Code
      </message>
      <message name="IDS_DAO_MINI_DAO_SITE_CENTER_MORE" desc="Button label for additional current-site actions inside the Mini Dao site center.">
        More
      </message>
      <message name="IDS_DAO_MINI_DAO_SITE_CENTER_BACK_BUTTON_LABEL" desc="Label for returning from a Mini Dao site center sub-panel to the main site center panel. Includes a left arrow.">
        ← Back
      </message>
      <message name="IDS_DAO_MINI_DAO_SITE_CENTER_BACK_BUTTON_ACCESSIBLE_NAME" desc="Accessible name for returning from a Mini Dao site center sub-panel to the main site center panel.">
        Back
      </message>
```

- [ ] **Step 2: Add the new source files to `dao_browser_ui_sources` next to the existing Little Dao files**

```gn
  "//dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.cc",
  "//dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.h",
```

- [ ] **Step 3: Run import**

Run:

```bash
npm run import
```

Expected: import succeeds after the new source files exist in Task 4; before Task 4 it may fail because GN references missing files.

---

### Task 3: Make Extension Section Host-Aware

**Files:**
- Modify: `src/dao/browser/ui/views/dao_control_center_extensions_section.h`
- Modify: `src/dao/browser/ui/views/dao_control_center_extensions_section.cc`

- [ ] **Step 1: Update the header constructor and members**

```cpp
#include "base/functional/callback.h"

class DaoControlCenterExtensionsSection
    : public views::View,
      public ToolbarActionsModel::Observer,
      public TabStripModelObserver,
      public views::ContextMenuController,
      public extensions::ExtensionActionIconFactory::Observer {
 public:
  explicit DaoControlCenterExtensionsSection(
      Browser* browser,
      base::RepeatingClosure close_host_callback = base::RepeatingClosure(),
      base::RepeatingCallback<views::View*()> anchor_view_callback =
          base::RepeatingCallback<views::View*()>());

 private:
  void CloseHostSurface();
  views::View* GetExtensionPopupAnchor(BrowserView* browser_view) const;

  base::RepeatingClosure close_host_callback_;
  base::RepeatingCallback<views::View*()> anchor_view_callback_;
};
```

- [ ] **Step 2: Update the constructor definition**

```cpp
DaoControlCenterExtensionsSection::DaoControlCenterExtensionsSection(
    Browser* browser,
    base::RepeatingClosure close_host_callback,
    base::RepeatingCallback<views::View*()> anchor_view_callback)
    : browser_(browser),
      close_host_callback_(std::move(close_host_callback)),
      anchor_view_callback_(std::move(anchor_view_callback)) {
```

- [ ] **Step 3: Add host helper methods**

```cpp
void DaoControlCenterExtensionsSection::CloseHostSurface() {
  if (close_host_callback_) {
    close_host_callback_.Run();
    return;
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }
  auto* cc_popup = browser_view->dao_control_center_popup();
  if (cc_popup) {
    cc_popup->Hide();
  }
}

views::View* DaoControlCenterExtensionsSection::GetExtensionPopupAnchor(
    BrowserView* browser_view) const {
  if (anchor_view_callback_) {
    if (views::View* anchor = anchor_view_callback_.Run()) {
      return anchor;
    }
  }
  if (browser_view && browser_view->dao_address_bar()) {
    if (views::View* anchor =
            browser_view->dao_address_bar()->control_center_button()) {
      return anchor;
    }
    return browser_view->dao_address_bar();
  }
  return nullptr;
}
```

- [ ] **Step 4: Replace the duplicated normal popup hide blocks**

In `OnExtensionClicked()`, `OnAddClicked()`, and `OnManageClicked()`, replace the current `BrowserView* browser_view = ...; if (browser_view) { ... cc_popup->Hide(); }` block with:

```cpp
BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
CloseHostSurface();
```

In the extension popup branch, replace the anchor selection block with:

```cpp
views::View* anchor = GetExtensionPopupAnchor(browser_view);
if (!anchor) {
  return;
}
```

- [ ] **Step 5: Run the focused compile check**

Run:

```bash
npm run import
npm run rebuild
```

Expected: build still fails because the new Mini Dao popup and Little Dao APIs are not implemented yet, but `DaoControlCenterExtensionsSection` should not produce constructor or callback errors.

---

### Task 4: Implement `DaoMiniDaoSiteCenterPopup`

**Files:**
- Create: `src/dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.h`
- Create: `src/dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.cc`

- [ ] **Step 1: Create the header**

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_MINI_DAO_SITE_CENTER_POPUP_H_
#define DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_MINI_DAO_SITE_CENTER_POPUP_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class ImageView;
class Label;
}

namespace dao {

class DaoControlCenterExtensionsSection;

class DaoMiniDaoSiteCenterPopup : public views::View,
                                  public TabStripModelObserver,
                                  public content::WebContentsObserver,
                                  public ui::NativeThemeObserver {
  METADATA_HEADER(DaoMiniDaoSiteCenterPopup, views::View)

 public:
  DaoMiniDaoSiteCenterPopup(
      Browser* browser,
      base::RepeatingCallback<views::View*()> anchor_view_callback);
  DaoMiniDaoSiteCenterPopup(const DaoMiniDaoSiteCenterPopup&) = delete;
  DaoMiniDaoSiteCenterPopup& operator=(const DaoMiniDaoSiteCenterPopup&) =
      delete;
  ~DaoMiniDaoSiteCenterPopup() override;

  void ShowAt(const gfx::Point& anchor_bottom_right);
  void Hide();
  void ShowMainPanel();
  void ShowQrPanel();

  Browser* browser() const { return browser_; }
  views::View* GetAnchorViewForExtensions() const;

  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void DidGetUserInteraction(const blink::WebInputEvent& event) override;
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

  void Layout(PassKey) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  static constexpr int kCardWidth = 320;
  static constexpr int kCardCornerRadius = 12;
  static constexpr int kCardPadding = 12;

  void ApplyTheme();
  void BuildMainPanel();
  void BuildQrPanel();
  void RefreshSiteHeader();
  void OnPageInfoClicked();
  void OnShareClicked();
  void OnQrClicked();
  void OnMoreClicked();
  void OnBackClicked();
  std::string GetActiveUrlSpec() const;

  raw_ptr<Browser> browser_;
  base::RepeatingCallback<views::View*()> anchor_view_callback_;
  gfx::Point anchor_;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::View> main_panel_ = nullptr;
  raw_ptr<views::View> qr_panel_ = nullptr;
  raw_ptr<views::Label> domain_label_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::ImageView> qr_image_ = nullptr;
  raw_ptr<views::Label> qr_url_label_ = nullptr;
  raw_ptr<DaoControlCenterExtensionsSection> extensions_section_ = nullptr;
  raw_ptr<views::View> separator_ = nullptr;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_MINI_DAO_SITE_CENTER_POPUP_H_
```

- [ ] **Step 2: Create the implementation**

Use the existing Control Center style constants and this class structure:

```cpp
#include "dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_specification.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "components/qr_code_generator/qr_code_generator.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_extensions_section.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

#if BUILDFLAG(IS_MAC)
#include "dao/browser/ui/views/dao_native_share_mac.h"
#endif

namespace dao {
namespace {
constexpr int kActionButtonHeight = 38;
constexpr int kActionCornerRadius = 8;
constexpr int kQrSize = 200;

class MiniSiteActionButton : public views::LabelButton {
 public:
  MiniSiteActionButton(const std::u16string& label,
                       LucideIcon icon,
                       views::Button::PressedCallback callback)
      : LabelButton(std::move(callback), label), icon_(icon) {
    SetInstallFocusRingOnFocus(false);
    SetAccessibleName(label);
    SetEnabledTextColors(ControlCenterLabelColor());
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetPreferredSize(gfx::Size(0, kActionButtonHeight));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 10)));
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    LabelButton::OnMouseEntered(event);
    SetBackground(views::CreateRoundedRectBackground(
        ControlCenterHoverBg(), kActionCornerRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    LabelButton::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    LabelButton::PaintButtonContents(canvas);
    DrawLucideIcon(canvas, icon_, gfx::RectF(width() - 28, 10, 16, 16),
                   ControlCenterIconMuted());
  }

 private:
  LucideIcon icon_;
};

gfx::ImageSkia RenderQrCode(const qr_code_generator::GeneratedCode& code,
                            int size) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(size, size);
  bitmap.eraseColor(SK_ColorWHITE);
  const int qr_size = code.qr_size;
  const int data_size = code.data.size();
  const float module_size = static_cast<float>(size) / qr_size;
  for (int y = 0; y < qr_size; ++y) {
    for (int x = 0; x < qr_size; ++x) {
      const int idx = y * qr_size + x;
      if (idx >= data_size || code.data[idx] == 0) {
        continue;
      }
      const int px = static_cast<int>(x * module_size);
      const int py = static_cast<int>(y * module_size);
      const int pw = static_cast<int>((x + 1) * module_size) - px;
      const int ph = static_cast<int>((y + 1) * module_size) - py;
      for (int dy = 0; dy < ph && py + dy < size; ++dy) {
        for (int dx = 0; dx < pw && px + dx < size; ++dx) {
          *bitmap.getAddr32(px + dx, py + dy) = SK_ColorBLACK;
        }
      }
    }
  }
  return gfx::ImageSkia::CreateFromBitmap(bitmap, 1.0f);
}
}  // namespace

BEGIN_METADATA(DaoMiniDaoSiteCenterPopup)
END_METADATA

DaoMiniDaoSiteCenterPopup::DaoMiniDaoSiteCenterPopup(
    Browser* browser,
    base::RepeatingCallback<views::View*()> anchor_view_callback)
    : browser_(browser), anchor_view_callback_(std::move(anchor_view_callback)) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);
  card_ = AddChildView(std::make_unique<views::View>());
  card_->SetPaintToLayer();
  card_->layer()->SetFillsBoundsOpaquely(false);
  card_->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kCardCornerRadius));
  card_->layer()->SetIsFastRoundedCorner(true);
  card_->layer()->SetBackgroundBlur(30);

  auto* card_layout = card_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kCardPadding), 8));
  card_layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kStretch);

  BuildMainPanel();
  BuildQrPanel();
  qr_panel_->SetVisible(false);

  if (browser_ && browser_->tab_strip_model()) {
    browser_->tab_strip_model()->AddObserver(this);
  }
  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
  ApplyTheme();
}
```

Add the remaining lifecycle, layout, and panel methods:

```cpp
DaoMiniDaoSiteCenterPopup::~DaoMiniDaoSiteCenterPopup() {
  if (browser_ && browser_->tab_strip_model()) {
    browser_->tab_strip_model()->RemoveObserver(this);
  }
}

void DaoMiniDaoSiteCenterPopup::ApplyTheme() {
  if (card_) {
    card_->SetBackground(views::CreateRoundedRectBackground(
        PopupBackground(), kCardCornerRadius));
  }
  if (separator_) {
    separator_->SetBackground(views::CreateSolidBackground(SeparatorColor()));
  }
}

void DaoMiniDaoSiteCenterPopup::BuildMainPanel() {
  main_panel_ = card_->AddChildView(std::make_unique<views::View>());
  auto* layout = main_panel_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto header = std::make_unique<views::View>();
  auto* header_layout = header->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 2), 3));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  domain_label_ = header->AddChildView(std::make_unique<views::Label>());
  domain_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  domain_label_->SetEnabledColor(ControlCenterLabelColor());
  status_label_ = header->AddChildView(std::make_unique<views::Label>());
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetEnabledColor(ControlCenterSecondaryTextColor());
  main_panel_->AddChildView(std::move(header));

  separator_ = main_panel_->AddChildView(std::make_unique<views::View>());
  separator_->SetPreferredSize(gfx::Size(0, 1));

  auto extension_label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(
          IDS_DAO_MINI_DAO_SITE_CENTER_EXTENSIONS_LABEL));
  extension_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  extension_label->SetEnabledColor(ControlCenterSecondaryTextColor());
  main_panel_->AddChildView(std::move(extension_label));

  extensions_section_ = main_panel_->AddChildView(
      std::make_unique<DaoControlCenterExtensionsSection>(
          browser_,
          base::BindRepeating(&DaoMiniDaoSiteCenterPopup::Hide,
                              base::Unretained(this)),
          base::BindRepeating(
              &DaoMiniDaoSiteCenterPopup::GetAnchorViewForExtensions,
              base::Unretained(this))));

  main_panel_->AddChildView(std::make_unique<MiniSiteActionButton>(
      l10n_util::GetStringUTF16(IDS_DAO_MINI_DAO_SITE_CENTER_PAGE_INFO),
      LucideIcon::kShieldCheck,
      base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnPageInfoClicked,
                          base::Unretained(this))));
  main_panel_->AddChildView(std::make_unique<MiniSiteActionButton>(
      l10n_util::GetStringUTF16(IDS_DAO_MINI_DAO_SITE_CENTER_SHARE),
      LucideIcon::kShare,
      base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnShareClicked,
                          base::Unretained(this))));
  main_panel_->AddChildView(std::make_unique<MiniSiteActionButton>(
      l10n_util::GetStringUTF16(IDS_DAO_MINI_DAO_SITE_CENTER_QR_CODE),
      LucideIcon::kQrCode,
      base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnQrClicked,
                          base::Unretained(this))));
  main_panel_->AddChildView(std::make_unique<MiniSiteActionButton>(
      l10n_util::GetStringUTF16(IDS_DAO_MINI_DAO_SITE_CENTER_MORE),
      LucideIcon::kEllipsis,
      base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnMoreClicked,
                          base::Unretained(this))));
}

void DaoMiniDaoSiteCenterPopup::BuildQrPanel() {
  qr_panel_ = card_->AddChildView(std::make_unique<views::View>());
  auto* layout = qr_panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 0), 10));
  layout->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  qr_panel_->AddChildView(std::make_unique<MiniSiteActionButton>(
      l10n_util::GetStringUTF16(
          IDS_DAO_MINI_DAO_SITE_CENTER_BACK_BUTTON_LABEL),
      LucideIcon::kChevronLeft,
      base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnBackClicked,
                          base::Unretained(this))));
  qr_image_ = qr_panel_->AddChildView(std::make_unique<views::ImageView>());
  qr_image_->SetPreferredSize(gfx::Size(kQrSize, kQrSize));
  qr_url_label_ = qr_panel_->AddChildView(std::make_unique<views::Label>());
  qr_url_label_->SetEnabledColor(ControlCenterSecondaryTextColor());
  qr_url_label_->SetMultiLine(true);
  qr_url_label_->SetMaxLines(2);
  qr_url_label_->SetMaximumWidth(kQrSize);
  qr_url_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
}

void DaoMiniDaoSiteCenterPopup::ShowAt(const gfx::Point& anchor_bottom_right) {
  anchor_ = anchor_bottom_right;
  ShowMainPanel();
  SetVisible(true);
  if (parent()) {
    parent()->ReorderChildView(this, parent()->children().size());
  }
  RefreshSiteHeader();
  if (extensions_section_) {
    extensions_section_->Refresh();
  }
  auto* web_contents = browser_->tab_strip_model()->GetActiveWebContents();
  content::WebContentsObserver::Observe(web_contents);
}

void DaoMiniDaoSiteCenterPopup::Hide() {
  SetVisible(false);
  content::WebContentsObserver::Observe(nullptr);
}

void DaoMiniDaoSiteCenterPopup::ShowMainPanel() {
  main_panel_->SetVisible(true);
  qr_panel_->SetVisible(false);
  InvalidateLayout();
  SchedulePaint();
}

void DaoMiniDaoSiteCenterPopup::ShowQrPanel() {
  main_panel_->SetVisible(false);
  qr_panel_->SetVisible(true);
  const std::string url = GetActiveUrlSpec();
  qr_url_label_->SetText(base::UTF8ToUTF16(url));
  auto result = qr_code_generator::GenerateCode(base::as_byte_span(url));
  if (result.has_value()) {
    qr_image_->SetImage(
        ui::ImageModel::FromImageSkia(RenderQrCode(result.value(), kQrSize)));
  }
  InvalidateLayout();
  SchedulePaint();
}

views::View* DaoMiniDaoSiteCenterPopup::GetAnchorViewForExtensions() const {
  return anchor_view_callback_ ? anchor_view_callback_.Run() : nullptr;
}

void DaoMiniDaoSiteCenterPopup::RefreshSiteHeader() {
  auto* web_contents = browser_->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    domain_label_->SetText(std::u16string());
    status_label_->SetText(std::u16string());
    return;
  }
  const GURL url = web_contents->GetVisibleURL();
  domain_label_->SetText(base::UTF8ToUTF16(
      url.host().empty() ? url.spec() : url.host()));
  status_label_->SetText(l10n_util::GetStringUTF16(
      IDS_DAO_MINI_DAO_SITE_CENTER_PAGE_INFO));
}
```

The implementation must also include:

```cpp
void DaoMiniDaoSiteCenterPopup::OnPageInfoClicked() {
  auto* web_contents = browser_->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }
  content::NavigationEntry* entry =
      web_contents->GetController().GetVisibleEntry();
  if (!entry) {
    return;
  }
  gfx::Rect anchor_rect = GetAnchorViewForExtensions()
                              ? GetAnchorViewForExtensions()->GetBoundsInScreen()
                              : gfx::Rect(anchor_.x(), anchor_.y(), 0, 0);
  Hide();
  PageInfoBubbleSpecification::Builder builder(
      nullptr, browser_->window()->GetNativeWindow(), web_contents,
      entry->GetVirtualURL());
  builder.AddAnchorRect(anchor_rect)
      .AddInitializedCallback(base::DoNothing())
      .AddPageInfoClosingCallback(
          base::BindOnce([](views::Widget::ClosedReason, bool) {}));
  views::BubbleDialogDelegateView* const bubble =
      PageInfoBubbleView::CreatePageInfoBubble(builder.Build());
  bubble->SetArrow(views::BubbleBorder::TOP_RIGHT);
  bubble->GetWidget()->Show();
}

void DaoMiniDaoSiteCenterPopup::OnShareClicked() {
#if BUILDFLAG(IS_MAC)
  auto* web_contents = browser_->tab_strip_model()->GetActiveWebContents();
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!web_contents || !browser_view || !browser_view->GetWidget()) {
    return;
  }
  const std::string url = web_contents->GetVisibleURL().spec();
  const std::string title = web_contents->GetTitle().empty()
                                ? url
                                : base::UTF16ToUTF8(web_contents->GetTitle());
  dao::ShowNativeShareMac(url, title, browser_view->GetWidget()->GetNativeView(),
                          GetAnchorViewForExtensions()
                              ? GetAnchorViewForExtensions()->GetBoundsInScreen()
                              : gfx::Rect(anchor_.x(), anchor_.y(), 0, 0));
#endif
}

void DaoMiniDaoSiteCenterPopup::OnQrClicked() {
  ShowQrPanel();
}

void DaoMiniDaoSiteCenterPopup::OnMoreClicked() {
  OnPageInfoClicked();
}

void DaoMiniDaoSiteCenterPopup::OnBackClicked() {
  ShowMainPanel();
}

std::string DaoMiniDaoSiteCenterPopup::GetActiveUrlSpec() const {
  if (!browser_ || !browser_->tab_strip_model()) {
    return std::string();
  }
  auto* web_contents = browser_->tab_strip_model()->GetActiveWebContents();
  return web_contents ? web_contents->GetVisibleURL().spec() : std::string();
}

void DaoMiniDaoSiteCenterPopup::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    Hide();
  }
}

void DaoMiniDaoSiteCenterPopup::DidGetUserInteraction(
    const blink::WebInputEvent& event) {
  Hide();
}

void DaoMiniDaoSiteCenterPopup::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  ApplyTheme();
  SchedulePaint();
}

void DaoMiniDaoSiteCenterPopup::Layout(PassKey) {
  const gfx::Size card_pref = card_->GetPreferredSize();
  const int card_width = kCardWidth;
  int card_x = anchor_.x() - card_width;
  int card_y = anchor_.y();
  constexpr int kMargin = 12;
  if (card_x < kMargin) {
    card_x = kMargin;
  }
  if (card_y + card_pref.height() > height() - kMargin) {
    card_y = std::max(kMargin, height() - card_pref.height() - kMargin);
  }
  card_->SetBounds(card_x, card_y, card_width, card_pref.height());
}

void DaoMiniDaoSiteCenterPopup::OnPaintBackground(gfx::Canvas* canvas) {
  if (!card_ || !card_->GetVisible()) {
    return;
  }
  gfx::ShadowValues shadows;
  shadows.emplace_back(gfx::Vector2d(0, 0), 40, PopupShadowOuter());
  shadows.emplace_back(gfx::Vector2d(0, 4), 16, PopupShadowInner());
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(SK_ColorTRANSPARENT);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setLooper(gfx::CreateShadowDrawLooper(shadows));
  canvas->DrawRoundRect(gfx::RectF(card_->bounds()), kCardCornerRadius, flags);
}

bool DaoMiniDaoSiteCenterPopup::OnMousePressed(const ui::MouseEvent& event) {
  if (card_ && !card_->bounds().Contains(event.location())) {
    Hide();
    return true;
  }
  return false;
}
```

- [ ] **Step 3: Run import**

Run:

```bash
npm run import
```

Expected: import succeeds once the file paths are present in `dao_ui_sources.gni`.

---

### Task 5: Wire the Popup into Little Dao and BrowserView

**Files:**
- Modify: `src/dao/browser/ui/views/little_dao/dao_little_dao_view.h`
- Modify: `src/dao/browser/ui/views/little_dao/dao_little_dao_view.cc`
- Modify: `src/patches/chrome/browser/ui/views/frame/browser_view.h.patch`
- Modify: `src/patches/chrome/browser/ui/views/frame/browser_view.cc.patch`
- Modify: `src/patches/chrome/browser/ui/views/frame/layout/browser_view_popup_layout_impl.cc.patch`

- [ ] **Step 1: Add Little Dao view API**

```cpp
gfx::Rect site_center_button_bounds() const;
views::View* site_center_button_for_testing() const;
void ShowMiniDaoSiteCenterForTesting();

void ShowMiniDaoSiteCenter();

raw_ptr<views::View> url_container_ = nullptr;
raw_ptr<views::LabelButton> url_text_button_ = nullptr;
raw_ptr<views::Button> site_center_button_ = nullptr;
```

- [ ] **Step 2: Replace the URL `LabelButton` with a compound pill**

Use a `views::View` with horizontal `FlexLayout`, a left `LabelButton` for the hostname, and a right custom `views::Button` that draws `LucideIcon::kSlidersHorizontal`. The left button calls `ShowCommandBar`; the right button calls `ShowMiniDaoSiteCenter`.

```cpp
url_container_ = AddChildView(std::make_unique<views::View>());
url_container_->SetBackground(views::CreateRoundedRectBackground(
    SuggestionHover(), kDisplayCornerRadius));
url_container_->SetBorder(views::CreateEmptyBorder(gfx::Insets()));
url_container_->SetProperty(
    views::kFlexBehaviorKey,
    views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                             views::MaximumFlexSizeRule::kUnbounded));

auto* url_layout =
    url_container_->SetLayoutManager(std::make_unique<views::FlexLayout>());
url_layout->SetOrientation(views::LayoutOrientation::kHorizontal);
url_layout->SetCrossAxisAlignment(views::LayoutAlignment::kStretch);

url_text_button_ = url_container_->AddChildView(std::make_unique<views::LabelButton>(
    base::BindRepeating(&DaoLittleDaoView::ShowCommandBar,
                        base::Unretained(this)),
    u""));
url_text_button_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
url_text_button_->SetEnabledTextColors(TextSecondary());
url_text_button_->SetBorder(
    views::CreateEmptyBorder(gfx::Insets::TLBR(0, 12, 0, 8)));
url_text_button_->SetAccessibleName(l10n_util::GetStringUTF16(
    IDS_DAO_LITTLE_DAO_ADDRESS_ACCESSIBLE_NAME));
url_text_button_->SetProperty(
    views::kFlexBehaviorKey,
    views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                             views::MaximumFlexSizeRule::kUnbounded));

site_center_button_ = url_container_->AddChildView(
    std::make_unique<MiniDaoSiteCenterButton>(base::BindRepeating(
        &DaoLittleDaoView::ShowMiniDaoSiteCenter, base::Unretained(this))));
site_center_button_->SetAccessibleName(l10n_util::GetStringUTF16(
    IDS_DAO_MINI_DAO_SITE_CENTER_ACCESSIBLE_NAME));
```

- [ ] **Step 3: Update URL text assignment**

Replace `url_display_->SetText(...)` with `url_text_button_->SetText(...)` in `UpdateURLDisplay()`.

- [ ] **Step 4: Implement bounds and show helpers**

```cpp
gfx::Rect DaoLittleDaoView::site_center_button_bounds() const {
  if (!site_center_button_) {
    return gfx::Rect();
  }
  gfx::Rect button_bounds = site_center_button_->bounds();
  gfx::Point origin = button_bounds.origin();
  views::View::ConvertPointToTarget(site_center_button_->parent(), parent(),
                                    &origin);
  button_bounds.set_origin(origin);
  return button_bounds;
}

views::View* DaoLittleDaoView::site_center_button_for_testing() const {
  return site_center_button_;
}

void DaoLittleDaoView::ShowMiniDaoSiteCenterForTesting() {
  ShowMiniDaoSiteCenter();
}

void DaoLittleDaoView::ShowMiniDaoSiteCenter() {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view || !browser_view->dao_mini_dao_site_center_popup()) {
    return;
  }
  const gfx::Rect bounds = site_center_button_bounds();
  browser_view->dao_mini_dao_site_center_popup()->ShowAt(
      bounds.bottom_right());
}
```

- [ ] **Step 5: Patch BrowserView declarations**

Add to the Dao forward declarations:

```cpp
class DaoMiniDaoSiteCenterPopup;
```

Add the accessor:

```cpp
dao::DaoMiniDaoSiteCenterPopup* dao_mini_dao_site_center_popup() {
  return dao_mini_dao_site_center_popup_;
}
```

Add the member:

```cpp
raw_ptr<dao::DaoMiniDaoSiteCenterPopup> dao_mini_dao_site_center_popup_ = nullptr;
```

- [ ] **Step 6: Patch BrowserView construction and hit testing**

In the Little Dao constructor branch, create the popup after `dao_little_dao_view_` and before `dao_command_bar_`:

```cpp
dao_mini_dao_site_center_popup_ = AddChildView(
    std::make_unique<dao::DaoMiniDaoSiteCenterPopup>(
        browser_.get(),
        base::BindRepeating(
            [](dao::DaoLittleDaoView* little_view) -> views::View* {
              return little_view ? little_view->site_center_button_for_testing()
                                 : nullptr;
            },
            dao_little_dao_view_.get())));
```

Add this before command-bar hit testing:

```cpp
if (dao_mini_dao_site_center_popup_ &&
    dao_mini_dao_site_center_popup_->GetVisible() &&
    dao_mini_dao_site_center_popup_->bounds().Contains(
        point_in_browser_view_coords)) {
  return HTCLIENT;
}
```

Add the site button hit test in the Little Dao block before `url_display_bounds()`:

```cpp
if (dao_little_dao_view_->site_center_button_bounds().Contains(
        point_in_browser_view_coords)) {
  return HTCLIENT;
}
```

- [ ] **Step 7: Patch Little Dao popup layout**

Include the header:

```cpp
#include "dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.h"
```

After the command bar layout in the Little Dao branch, add:

```cpp
if (auto* const site_center = dao_bv->dao_mini_dao_site_center_popup()) {
  layout.AddChild(site_center, browser_params.visual_client_area);
}
```

- [ ] **Step 8: Run import and compile**

Run:

```bash
npm run import
npm run rebuild
```

Expected: build succeeds or fails only on straightforward include/signature issues in the touched files.

---

### Task 6: Run Focused Verification

**Files:**
- No source edits unless verification exposes a failure.

- [ ] **Step 1: Run focused browser tests**

Run:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoLittleDaoViewBrowserTest.*:DaoControlCenterPopupBrowserTest.MiniDaoButtonExistsInMainUtilityRow"
```

Expected: all selected tests pass. The Control Center test confirms the normal window still has the extraction button, while the Mini Dao site center test confirms Mini Dao does not.

- [ ] **Step 2: If source or patch files changed after tests, run the only allowed compile confirmation**

Run:

```bash
npm run rebuild
```

Expected: rebuild passes.

- [ ] **Step 3: Inspect the diff**

Run:

```bash
git diff -- src/dao/browser/ui/views/little_dao src/dao/browser/ui/views/dao_control_center_extensions_section.* src/dao/browser/ui/dao_ui_sources.gni src/dao/browser/strings/dao_strings.grd src/patches/chrome/browser/ui/views/frame src/dao/browser/ui/views/dao_browser_browsertest.cc docs/superpowers
```

Expected: diff contains only the Mini Dao site center implementation, its design/plan docs, and existing Mini Dao extraction work already present before this plan.

---

## Self-Review

- Spec coverage: the plan adds the URL-pill site center icon, Mini Dao-only popup, extension actions, Page Info, Share, QR, More, BrowserView integration, tests, and excludes the normal-window Mini Dao extraction action from Mini Dao.
- Clarity scan: each step names concrete files, APIs, commands, and expected outcomes.
- Type consistency: the new BrowserView accessor is `dao_mini_dao_site_center_popup()`, the popup class is `DaoMiniDaoSiteCenterPopup`, and the Little Dao testing hook is `ShowMiniDaoSiteCenterForTesting()`.
