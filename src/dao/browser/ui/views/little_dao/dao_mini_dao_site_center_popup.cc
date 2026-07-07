// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/little_dao/dao_mini_dao_site_center_popup.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_specification.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/qr_code_generator/qr_code_generator.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_extensions_section.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "dao/browser/ui/views/dao_qr_code_image.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image.h"
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
#include "ui/views/view_utils.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_MAC)
#include "dao/browser/ui/views/dao_native_share_mac.h"
#endif

namespace dao {

namespace {

constexpr int kActionButtonHeight = 38;
constexpr int kActionCornerRadius = 8;
constexpr int kActionRowInsetH = 6;
constexpr int kHeaderInsetH = 2;
constexpr int kHeaderInsetV = 4;
constexpr int kQrCardCornerRadius = 10;
constexpr int kQrCardInset = 6;
constexpr int kQrSize = 240;

void PrepareGlassLabel(views::Label* label) {
  label->SetSkipSubpixelRenderingOpacityCheck(true);
}

void ClearButtonHoverState(views::View* root) {
  if (!root) {
    return;
  }
  if (auto* button = views::AsViewClass<views::Button>(root)) {
    button->SetBackground(nullptr);
    if (button->GetState() != views::Button::STATE_DISABLED) {
      button->SetState(views::Button::STATE_NORMAL);
    }
  }
  for (views::View* child : root->children()) {
    ClearButtonHoverState(child);
  }
}

class MiniSiteActionButton : public views::LabelButton {
  METADATA_HEADER(MiniSiteActionButton, views::LabelButton)

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
    PrepareGlassLabel(this->label());
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

BEGIN_METADATA(MiniSiteActionButton)
END_METADATA

std::unique_ptr<views::View> CreateActionButtonRow(
    std::unique_ptr<MiniSiteActionButton> button) {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, kActionRowInsetH), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  views::View* button_view = row->AddChildView(std::move(button));
  layout->SetFlexForView(button_view, 1);
  return row;
}

gfx::Image GetFaviconForWebContents(content::WebContents* web_contents) {
  if (!web_contents) {
    return gfx::Image();
  }
  auto* favicon_driver =
      favicon::ContentFaviconDriver::FromWebContents(web_contents);
  if (!favicon_driver || !favicon_driver->FaviconIsValid()) {
    return gfx::Image();
  }
  return favicon_driver->GetFavicon();
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
  card_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kCardCornerRadius));
  card_->layer()->SetIsFastRoundedCorner(true);
  card_->layer()->SetBackgroundBlur(30);

  auto* card_layout =
      card_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(kCardPadding),
          8));
  card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  BuildMainPanel();
  BuildQrPanel();
  qr_panel_->SetVisible(false);

  if (browser_ && browser_->tab_strip_model()) {
    browser_->tab_strip_model()->AddObserver(this);
  }
  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
  ApplyTheme();
}

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
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets::VH(kHeaderInsetV, kHeaderInsetH), 3));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  domain_label_ = header->AddChildView(std::make_unique<views::Label>());
  PrepareGlassLabel(domain_label_);
  domain_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  domain_label_->SetEnabledColor(ControlCenterLabelColor());

  status_label_ = header->AddChildView(std::make_unique<views::Label>());
  PrepareGlassLabel(status_label_);
  status_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_label_->SetEnabledColor(ControlCenterSecondaryTextColor());
  main_panel_->AddChildView(std::move(header));

  separator_ = main_panel_->AddChildView(std::make_unique<views::View>());
  separator_->SetPreferredSize(gfx::Size(0, 1));

  auto extension_label = std::make_unique<views::Label>(
      l10n_util::GetStringUTF16(
          IDS_DAO_MINI_DAO_SITE_CENTER_EXTENSIONS_LABEL));
  PrepareGlassLabel(extension_label.get());
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

  main_panel_->AddChildView(CreateActionButtonRow(
      std::make_unique<MiniSiteActionButton>(
          l10n_util::GetStringUTF16(IDS_DAO_MINI_DAO_SITE_CENTER_PAGE_INFO),
          LucideIcon::kShieldCheck,
          base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnPageInfoClicked,
                              base::Unretained(this)))));
  main_panel_->AddChildView(CreateActionButtonRow(
      std::make_unique<MiniSiteActionButton>(
          l10n_util::GetStringUTF16(IDS_DAO_MINI_DAO_SITE_CENTER_SHARE),
          LucideIcon::kShare,
          base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnShareClicked,
                              base::Unretained(this)))));
  main_panel_->AddChildView(CreateActionButtonRow(
      std::make_unique<MiniSiteActionButton>(
          l10n_util::GetStringUTF16(IDS_DAO_MINI_DAO_SITE_CENTER_QR_CODE),
          LucideIcon::kQrCode,
          base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnQrClicked,
                              base::Unretained(this)))));
  main_panel_->AddChildView(CreateActionButtonRow(
      std::make_unique<MiniSiteActionButton>(
          l10n_util::GetStringUTF16(IDS_DAO_MINI_DAO_SITE_CENTER_MORE),
          LucideIcon::kEllipsis,
          base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnMoreClicked,
                              base::Unretained(this)))));
}

void DaoMiniDaoSiteCenterPopup::BuildQrPanel() {
  qr_panel_ = card_->AddChildView(std::make_unique<views::View>());
  auto* layout = qr_panel_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 0), 10));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  qr_panel_->AddChildView(std::make_unique<MiniSiteActionButton>(
      l10n_util::GetStringUTF16(
          IDS_DAO_MINI_DAO_SITE_CENTER_BACK_BUTTON_LABEL),
      LucideIcon::kChevronLeft,
      base::BindRepeating(&DaoMiniDaoSiteCenterPopup::OnBackClicked,
                          base::Unretained(this))));

  auto qr_card = std::make_unique<views::View>();
  qr_card->SetBackground(views::CreateRoundedRectBackground(
      SK_ColorWHITE, kQrCardCornerRadius));
  qr_card->SetBorder(views::CreateEmptyBorder(gfx::Insets(kQrCardInset)));
  auto* qr_card_layout =
      qr_card->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  qr_card_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  qr_card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  qr_image_ = qr_card->AddChildView(std::make_unique<views::ImageView>());
  qr_image_->SetPreferredSize(gfx::Size(kQrSize, kQrSize));
  qr_panel_->AddChildView(std::move(qr_card));

  qr_url_label_ = qr_panel_->AddChildView(std::make_unique<views::Label>());
  PrepareGlassLabel(qr_url_label_);
  qr_url_label_->SetEnabledColor(ControlCenterSecondaryTextColor());
  qr_url_label_->SetMultiLine(true);
  qr_url_label_->SetMaxLines(2);
  qr_url_label_->SetMaximumWidth(kQrSize + 2 * kQrCardInset);
  qr_url_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
}

void DaoMiniDaoSiteCenterPopup::ShowAt(
    const gfx::Point& anchor_bottom_right) {
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
  ClearButtonHoverState(card_);
  SetVisible(false);
  content::WebContentsObserver::Observe(nullptr);
}

void DaoMiniDaoSiteCenterPopup::ShowMainPanel() {
  ClearButtonHoverState(card_);
  main_panel_->SetVisible(true);
  qr_panel_->SetVisible(false);
  InvalidateLayout();
  SchedulePaint();
}

void DaoMiniDaoSiteCenterPopup::ShowQrPanel() {
  ClearButtonHoverState(card_);
  main_panel_->SetVisible(false);
  qr_panel_->SetVisible(true);

  const std::string url = GetActiveUrlSpec();
  qr_url_label_->SetText(base::UTF8ToUTF16(url));
  auto result = qr_code_generator::GenerateCode(base::as_byte_span(url));
  if (result.has_value()) {
    auto* web_contents = browser_->tab_strip_model()->GetActiveWebContents();
    qr_image_->SetImage(ui::ImageModel::FromImageSkia(
        RenderDaoQrCode(result.value(), kQrSize,
                        GetFaviconForWebContents(web_contents))));
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
  domain_label_->SetText(
      base::UTF8ToUTF16(url.host().empty() ? url.spec() : url.host()));
  status_label_->SetText(
      l10n_util::GetStringUTF16(IDS_DAO_MINI_DAO_SITE_CENTER_PAGE_INFO));
}

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

  views::View* anchor = GetAnchorViewForExtensions();
  gfx::Rect anchor_rect =
      anchor ? anchor->GetBoundsInScreen() : gfx::Rect(anchor_.x(), anchor_.y(), 0, 0);
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
  views::View* anchor = GetAnchorViewForExtensions();
  dao::ShowNativeShareMac(
      url, title, browser_view->GetWidget()->GetNativeView(),
      anchor ? anchor->GetBoundsInScreen()
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
  gfx::Size card_pref = card_->GetPreferredSize();
  const int card_width = kCardWidth;
  int card_height = card_pref.height();
  constexpr int kMargin = 12;

  int card_x = anchor_.x() - card_width;
  int card_y = anchor_.y();
  if (card_x < kMargin) {
    card_x = kMargin;
  }
  if (card_y + card_height > height() - kMargin) {
    card_y = std::max(kMargin, height() - card_height - kMargin);
  }
  if (card_height > height() - 2 * kMargin) {
    card_height = std::max(100, height() - 2 * kMargin);
    card_y = kMargin;
  }

  card_->SetBounds(card_x, card_y, card_width, card_height);
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

}  // namespace dao
