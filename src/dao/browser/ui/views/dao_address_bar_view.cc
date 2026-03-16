// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_address_bar_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/font_list.h"
#include "ui/compositor/layer.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"

namespace dao {

namespace {
constexpr SkColor kHostColor = SkColorSetRGB(100, 100, 100);
constexpr SkColor kPathColor = SkColorSetRGB(124, 124, 124);
constexpr int kFontSize = 12;
}  // namespace

BEGIN_METADATA(DaoAddressBarView)
END_METADATA

DaoAddressBarView::DaoAddressBarView(Browser* browser)
    : browser_(browser), tab_strip_model_(browser->tab_strip_model()) {
  SetBackground(views::CreateSolidBackground(SK_ColorWHITE));  // default

  // Top rounded corners (bottom corners are on the contents container)
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(
      dao::kContentCornerRadius, dao::kContentCornerRadius, 0, 0));
  layer()->SetIsFastRoundedCorner(true);

  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  layout->SetMainAxisAlignment(views::LayoutAlignment::kCenter);
  layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);

  gfx::FontList font({"system-ui"}, gfx::Font::NORMAL, kFontSize,
                      gfx::Font::Weight::NORMAL);

  host_label_ = AddChildView(std::make_unique<views::Label>());
  host_label_->SetFontList(font);
  host_label_->SetEnabledColor(kHostColor);
  host_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  host_label_->SetCanProcessEventsWithinSubtree(false);

  path_label_ = AddChildView(std::make_unique<views::Label>());
  path_label_->SetFontList(font);
  path_label_->SetEnabledColor(kPathColor);
  path_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  path_label_->SetCanProcessEventsWithinSubtree(false);
  path_label_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                               views::MaximumFlexSizeRule::kPreferred));

  tab_strip_model_->AddObserver(this);
  ObserveActiveWebContents();
  UpdateURL();
  UpdateBackgroundColor();
}

DaoAddressBarView::~DaoAddressBarView() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void DaoAddressBarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  ObserveActiveWebContents();
  UpdateURL();
  UpdateBackgroundColor();
}

void DaoAddressBarView::TabChangedAt(content::WebContents* contents,
                                     int index,
                                     TabChangeType change_type) {
  // Only update if the changed tab is the active one
  if (tab_strip_model_ && index == tab_strip_model_->active_index()) {
    UpdateURL();
    UpdateBackgroundColor();
  }
}

void DaoAddressBarView::UpdateURL() {
  if (!tab_strip_model_) {
    host_label_->SetText(u"");
    path_label_->SetText(u"");
    return;
  }

  auto* web_contents = tab_strip_model_->GetActiveWebContents();
  if (!web_contents) {
    host_label_->SetText(u"");
    path_label_->SetText(u"");
    return;
  }

  GURL url = web_contents->GetVisibleURL();
  if (!url.is_valid()) {
    host_label_->SetText(u"");
    path_label_->SetText(u"");
    return;
  }

  // For non-standard schemes (about:blank, chrome://, etc.), show full URL
  // in the host label with no path split.
  if (!url.SchemeIsHTTPOrHTTPS()) {
    host_label_->SetText(base::UTF8ToUTF16(url.spec()));
    path_label_->SetText(u"");
    return;
  }

  // Host part
  std::string host = url.host();
  host_label_->SetText(base::UTF8ToUTF16(host));

  // Path + query part
  std::string path_and_query = url.path();
  if (url.has_query()) {
    path_and_query += "?" + url.query();
  }
  if (path_and_query == "/") {
    path_and_query.clear();
  }
  if (!path_and_query.empty()) {
    path_label_->SetText(base::UTF8ToUTF16(" " + path_and_query));
  } else {
    path_label_->SetText(u"");
  }
}

bool DaoAddressBarView::OnMousePressed(const ui::MouseEvent& event) {
  // Click on address bar opens the command bar
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_command_bar()) {
    browser_view->dao_command_bar()->Show();
  }
  return true;
}

gfx::Size DaoAddressBarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kBarHeight);
}

void DaoAddressBarView::OnBackgroundColorChanged() {
  UpdateBackgroundColor();
}

void DaoAddressBarView::UpdateBackgroundColor() {
  SkColor bg_color = SK_ColorWHITE;
  if (tab_strip_model_) {
    auto* web_contents = tab_strip_model_->GetActiveWebContents();
    if (web_contents) {
      auto* rwhv = web_contents->GetRenderWidgetHostView();
      if (rwhv) {
        auto color = rwhv->GetBackgroundColor();
        if (color.has_value()) {
          bg_color = color.value();
        }
      }
    }
  }
  SetBackground(views::CreateSolidBackground(bg_color));

  // Adaptive bottom separator: 0.1 opacity white on dark, 0.1 opacity black on light
  int r = SkColorGetR(bg_color);
  int g = SkColorGetG(bg_color);
  int b = SkColorGetB(bg_color);
  double luminance = 0.299 * r + 0.587 * g + 0.114 * b;
  SkColor separator_color = luminance < 128
      ? SkColorSetARGB(25, 255, 255, 255)   // dark bg → white 0.1
      : SkColorSetARGB(25, 0, 0, 0);        // light bg → black 0.1
  SetBorder(views::CreateSolidSidedBorder(
      gfx::Insets::TLBR(0, 0, 1, 0), separator_color));

  SchedulePaint();
}

void DaoAddressBarView::ObserveActiveWebContents() {
  if (!tab_strip_model_) {
    return;
  }
  auto* web_contents = tab_strip_model_->GetActiveWebContents();
  if (web_contents != content::WebContentsObserver::web_contents()) {
    content::WebContentsObserver::Observe(web_contents);
  }
}

}  // namespace dao
