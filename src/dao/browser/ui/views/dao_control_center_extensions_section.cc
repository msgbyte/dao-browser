// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_extensions_section.h"

#include <algorithm>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/extensions/extension_view_host_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/extensions/extension_popup_types.h"
#include "chrome/browser/ui/extensions/extension_side_panel_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_extension_action_icon.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"

namespace dao {

namespace {

constexpr int kExtIconSize = 20;
constexpr int kExtIconPadding = 8;
constexpr int kExtGridItemSize = kExtIconSize + kExtIconPadding * 2;
constexpr int kExtBtnRadius = 8;

// Header layout used with ActionsPlacement::kHeader.
constexpr int kHeaderTitleInsetLeft = 6;
constexpr int kHeaderTitleFontSize = 11;
constexpr int kHeaderActionSize = 22;
constexpr int kHeaderActionIconSize = 14;
constexpr int kHeaderActionRadius = 6;
constexpr int kHeaderActionSpacing = 2;
constexpr int kHeaderToGridSpacing = 2;

struct ActionButtonStyle {
  int size;
  int icon_size;
  int corner_radius;
  SkColor (*icon_color)();
};

constexpr ActionButtonStyle kGridActionButtonStyle = {
    kExtGridItemSize, kExtIconSize, kExtBtnRadius, &ControlCenterIconMuted};
constexpr ActionButtonStyle kHeaderActionButtonStyle = {
    kHeaderActionSize, kHeaderActionIconSize, kHeaderActionRadius,
    &ControlCenterTileSubtitleColor};

// An ImageButton that shows a rounded-rect hover highlight.
class ExtIconButton : public views::ImageButton {
 public:
  ExtIconButton(PressedCallback callback) : ImageButton(std::move(callback)) {}

  void OnMouseEntered(const ui::MouseEvent& event) override {
    ImageButton::OnMouseEntered(event);
    SetBackground(
        views::CreateRoundedRectBackground(SuggestionHover(), kExtBtnRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    ImageButton::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }
};

// An action button that draws a Lucide icon and has hover highlight.
class GridActionButton : public views::Button {
 public:
  GridActionButton(LucideIcon icon,
                   const ActionButtonStyle& style,
                   PressedCallback callback)
      : Button(std::move(callback)), icon_(icon), style_(style) {
    SetPreferredSize(gfx::Size(style_.size, style_.size));
    SetInstallFocusRingOnFocus(false);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    Button::OnMouseEntered(event);
    SetBackground(views::CreateRoundedRectBackground(SuggestionHover(),
                                                     style_.corner_radius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    float icon_size = static_cast<float>(style_.icon_size);
    float ox = (width() - icon_size) / 2.0f;
    float oy = (height() - icon_size) / 2.0f;
    DrawLucideIcon(canvas, icon_, gfx::RectF(ox, oy, icon_size, icon_size),
                   style_.icon_color());
  }

 private:
  LucideIcon icon_;
  ActionButtonStyle style_;
};

// Lays out equally sized cells left to right, wrapping to a new row when the
// next cell would overflow the available width.
class WrappingGridView : public views::View {
  METADATA_HEADER(WrappingGridView, views::View)

 public:
  explicit WrappingGridView(int cell_size) : cell_size_(cell_size) {}

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    const int count = GetVisibleChildCount();
    if (count == 0) {
      return gfx::Size();
    }
    const int columns = available_size.width().is_bounded()
                            ? GetColumnCount(available_size.width().value())
                            : count;
    const int rows = (count + columns - 1) / columns;
    return gfx::Size(std::min(count, columns) * cell_size_,
                     rows * cell_size_);
  }

  void Layout(PassKey) override {
    const int columns = GetColumnCount(width());
    int index = 0;
    for (views::View* child : children()) {
      if (!child->GetVisible()) {
        continue;
      }
      child->SetBounds((index % columns) * cell_size_,
                       (index / columns) * cell_size_, cell_size_, cell_size_);
      ++index;
    }
  }

 private:
  int GetColumnCount(int width) const {
    return std::max(1, width / cell_size_);
  }

  int GetVisibleChildCount() const {
    int count = 0;
    for (const views::View* child : children()) {
      if (child->GetVisible()) {
        ++count;
      }
    }
    return count;
  }

  const int cell_size_;
};

BEGIN_METADATA(WrappingGridView)
END_METADATA

}  // namespace

BEGIN_METADATA(DaoControlCenterExtensionsSection)
END_METADATA

DaoControlCenterExtensionsSection::DaoControlCenterExtensionsSection(
    BrowserWindowInterface* browser,
    ActionsPlacement actions_placement,
    base::RepeatingClosure close_host_callback,
    base::RepeatingCallback<views::View*()> anchor_view_callback)
    : browser_(browser),
      actions_placement_(actions_placement),
      close_host_callback_(std::move(close_host_callback)),
      anchor_view_callback_(std::move(anchor_view_callback)) {
  if (actions_placement_ == ActionsPlacement::kHeader) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(),
        kHeaderToGridSpacing));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    // Header: title on the left, Add / Manage shortcuts on the right.
    auto* header = AddChildView(std::make_unique<views::View>());
    auto* header_layout =
        header->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::TLBR(0, kHeaderTitleInsetLeft, 0, 0),
            kHeaderActionSpacing));
    header_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    header_title_ = header->AddChildView(std::make_unique<views::Label>(
        l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_EXTENSIONS)));
    header_title_->SetFontList(
        gfx::FontList({"system-ui"}, gfx::Font::NORMAL, kHeaderTitleFontSize,
                      gfx::Font::Weight::SEMIBOLD));
    header_title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    header_title_->SetElideBehavior(gfx::ELIDE_TAIL);
    header_title_->SetSkipSubpixelRenderingOpacityCheck(true);
    header_layout->SetFlexForView(header_title_.get(), 1);
    AddActionButtons(header);

    grid_ =
        AddChildView(std::make_unique<WrappingGridView>(kExtGridItemSize));
    ApplyTheme();
  } else {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 0), 4));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    // Single row of extension icons followed by the Add / Manage buttons.
    grid_ = AddChildView(std::make_unique<views::View>());
    auto* grid_layout =
        grid_->SetLayoutManager(std::make_unique<views::FlexLayout>());
    grid_layout->SetOrientation(views::LayoutOrientation::kHorizontal);
    grid_layout->SetDefault(
        views::kFlexBehaviorKey,
        views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                                 views::MaximumFlexSizeRule::kPreferred,
                                 /*adjust_height_for_width=*/true));
  }

  // Observe toolbar model for changes
  auto* profile = browser_->GetProfile();
  model_ = ToolbarActionsModel::Get(profile);
  if (model_) {
    observation_.Observe(model_.get());
  }

  // Build the initial grid eagerly so it's ready when the popup opens.
  RebuildGrid();
}

DaoControlCenterExtensionsSection::~DaoControlCenterExtensionsSection() =
    default;

void DaoControlCenterExtensionsSection::Refresh() {
  if (grid_dirty_) {
    RebuildGrid();
    return;
  }
  UpdateAllButtonIcons();
}

void DaoControlCenterExtensionsSection::ApplyTheme() {
  if (header_title_) {
    header_title_->SetEnabledColor(ControlCenterTileSubtitleColor());
  }
}

void DaoControlCenterExtensionsSection::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId& id) {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId& id) {
  // Drop the cached icon factory: it holds raw pointers to the Extension and
  // ExtensionAction, which are about to be destroyed.
  icon_factories_.erase(id);
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId& id) {
  // The Extension/ExtensionAction backing the cached factory may have been
  // replaced (e.g. extension reload or permission change). Drop the stale
  // factory so GetIconForExtension rebuilds it against the current action,
  // avoiding a use-after-free on the old raw pointers.
  icon_factories_.erase(id);
  if (!UpdateButtonIcon(id)) {
    grid_dirty_ = true;
  }
}

void DaoControlCenterExtensionsSection::OnToolbarModelInitialized() {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarPinnedActionsChanged() {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::RebuildGrid() {
  grid_->RemoveAllChildViews();
  button_to_extension_id_.clear();
  id_to_button_.clear();
  icon_factories_.clear();
  grid_dirty_ = false;

  if (!model_ || !browser_) {
    return;
  }

  auto* profile = browser_->GetProfile();
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return;
  }

  const auto& action_ids = model_->action_ids();
  for (const auto& id : action_ids) {
    const auto* extension = registry->enabled_extensions().GetByID(id);
    if (!extension) {
      continue;
    }

    gfx::ImageSkia icon = GetIconForExtension(id, *extension);

    std::u16string name = base::UTF8ToUTF16(extension->name());
    std::string ext_id = id;

    auto btn = std::make_unique<ExtIconButton>(base::BindRepeating(
        &DaoControlCenterExtensionsSection::OnExtensionClicked,
        base::Unretained(this), ext_id));
    btn->SetImageModel(views::Button::STATE_NORMAL,
                       ui::ImageModel::FromImageSkia(icon));
    btn->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    btn->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    btn->SetPreferredSize(gfx::Size(kExtGridItemSize, kExtGridItemSize));
    btn->SetInstallFocusRingOnFocus(false);
    btn->SetAccessibleName(name);
    btn->SetTooltipText(name);
    btn->set_context_menu_controller(this);

    views::View* btn_raw = btn.release();
    views::ImageButton* btn_ptr =
        static_cast<views::ImageButton*>(grid_->AddChildView(btn_raw));
    button_to_extension_id_[btn_ptr] = ext_id;
    id_to_button_[ext_id] = btn_ptr;
  }

  // The header placement keeps its Add / Manage buttons outside the grid.
  if (actions_placement_ == ActionsPlacement::kGrid) {
    AddActionButtons(grid_);
  }

  InvalidateLayout();
}

void DaoControlCenterExtensionsSection::AddActionButtons(views::View* parent) {
  const ActionButtonStyle& style =
      actions_placement_ == ActionsPlacement::kHeader
          ? kHeaderActionButtonStyle
          : kGridActionButtonStyle;

  // "Add" action button.
  auto add_btn = std::make_unique<GridActionButton>(
      LucideIcon::kPlus, style,
      base::BindRepeating(&DaoControlCenterExtensionsSection::OnAddClicked,
                          base::Unretained(this)));
  std::u16string add_label =
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_ADD_EXTENSION);
  add_btn->SetAccessibleName(add_label);
  add_btn->SetTooltipText(add_label);
  parent->AddChildView(static_cast<views::View*>(add_btn.release()));

  // "Manage" action button.
  auto manage_btn = std::make_unique<GridActionButton>(
      LucideIcon::kSettings, style,
      base::BindRepeating(&DaoControlCenterExtensionsSection::OnManageClicked,
                          base::Unretained(this)));
  std::u16string manage_label =
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MANAGE_EXTENSIONS);
  manage_btn->SetAccessibleName(manage_label);
  manage_btn->SetTooltipText(manage_label);
  parent->AddChildView(static_cast<views::View*>(manage_btn.release()));
}

gfx::ImageSkia DaoControlCenterExtensionsSection::GetIconForExtension(
    const std::string& extension_id,
    const extensions::Extension& extension) {
  if (!browser_) {
    return gfx::ImageSkia();
  }

  auto* action_manager =
      extensions::ExtensionActionManager::Get(browser_->GetProfile());
  if (!action_manager) {
    return gfx::ImageSkia();
  }
  auto* action = action_manager->GetExtensionAction(extension);
  if (!action) {
    return gfx::ImageSkia();
  }

  auto& icon_factory = icon_factories_[extension_id];
  if (!icon_factory) {
    icon_factory = std::make_unique<extensions::ExtensionActionIconFactory>(
        &extension, action, this);
  }

  content::WebContents* web_contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  return CreateExtensionActionIconWithBadge(
      *icon_factory, *action, web_contents,
      gfx::Size(kExtGridItemSize, kExtGridItemSize));
}

bool DaoControlCenterExtensionsSection::UpdateButtonIcon(
    const std::string& extension_id) {
  auto button_it = id_to_button_.find(extension_id);
  if (button_it == id_to_button_.end() || !button_it->second || !browser_) {
    return false;
  }

  auto* registry = extensions::ExtensionRegistry::Get(browser_->GetProfile());
  if (!registry) {
    return false;
  }
  const auto* extension = registry->enabled_extensions().GetByID(extension_id);
  if (!extension) {
    return false;
  }

  button_it->second->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromImageSkia(
          GetIconForExtension(extension_id, *extension)));
  return true;
}

void DaoControlCenterExtensionsSection::UpdateAllButtonIcons() {
  for (const auto& entry : id_to_button_) {
    UpdateButtonIcon(entry.first);
  }
}

void DaoControlCenterExtensionsSection::OnIconUpdated() {
  UpdateAllButtonIcons();
}

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

void DaoControlCenterExtensionsSection::OnExtensionClicked(
    const std::string& extension_id) {
  if (!browser_) {
    return;
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  CloseHostSurface();

  auto* web_contents = browser_->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }

  auto* profile = browser_->GetProfile();
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return;
  }
  const auto* extension = registry->enabled_extensions().GetByID(extension_id);
  if (!extension) {
    return;
  }

  // Use ExtensionActionRunner to trigger the action directly.
  extensions::ExtensionActionRunner* runner =
      extensions::ExtensionActionRunner::GetForWebContents(web_contents);
  if (!runner) {
    return;
  }

  extensions::ExtensionAction::ShowAction result =
      runner->RunAction(extension, /*grant_tab_permissions=*/true);

  if (result == extensions::ExtensionAction::ShowAction::kShowPopup) {
    // Show popup anchored to the CC button in the address bar.
    auto* action_manager = extensions::ExtensionActionManager::Get(profile);
    auto* action = action_manager->GetExtensionAction(*extension);
    if (!action) {
      return;
    }
    int tab_id = extensions::ExtensionTabUtil::GetTabId(web_contents);
    GURL popup_url = action->GetPopupUrl(tab_id);
    if (!popup_url.is_valid()) {
      return;
    }

    auto host = extensions::ExtensionViewHostFactory::CreatePopupHost(
        *extension, popup_url, browser_);
    if (!host) {
      return;
    }

    views::View* anchor = GetExtensionPopupAnchor(browser_view);
    if (!anchor) {
      return;
    }

    ExtensionPopup::ShowPopup(browser_, std::move(host),
                              views::BubbleAnchor(anchor),
                              views::BubbleBorder::BOTTOM_RIGHT,
                              PopupShowAction::kShow, base::DoNothing());
  } else if (result ==
             extensions::ExtensionAction::ShowAction::kToggleSidePanel) {
    extensions::side_panel_util::ToggleExtensionSidePanel(browser_,
                                                          extension->id());
  }
}

void DaoControlCenterExtensionsSection::OnAddClicked() {
  CloseHostSurface();
  NavigateParams params(browser_, GURL("https://chromewebstore.google.com/"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void DaoControlCenterExtensionsSection::OnManageClicked() {
  CloseHostSurface();
  NavigateParams params(browser_, GURL("dao://extensions/"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

std::string DaoControlCenterExtensionsSection::GetExtensionIdForView(
    views::View* view) const {
  auto it = button_to_extension_id_.find(view);
  if (it != button_to_extension_id_.end()) {
    return it->second;
  }
  return std::string();
}

void DaoControlCenterExtensionsSection::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  std::string extension_id = GetExtensionIdForView(source);
  if (extension_id.empty() || !browser_) {
    return;
  }

  auto* profile = browser_->GetProfile();
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return;
  }
  const auto* extension = registry->enabled_extensions().GetByID(extension_id);
  if (!extension) {
    return;
  }

  bool is_pinned = model_ && model_->IsActionPinned(extension_id);
  context_menu_model_ = std::make_unique<extensions::ExtensionContextMenuModel>(
      extension, browser_, is_pinned, /*delegate=*/nullptr,
      /*can_show_icon_in_toolbar=*/true,
      extensions::ExtensionContextMenuModel::ContextMenuSource::kToolbarAction);

  context_menu_adapter_ = std::make_unique<views::MenuModelAdapter>(
      context_menu_model_.get(),
      base::BindRepeating(
          &DaoControlCenterExtensionsSection::OnContextMenuClosed,
          base::Unretained(this)));

  auto menu = context_menu_adapter_->CreateMenu();
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      std::move(menu),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);

  context_menu_runner_->RunMenuAt(
      source->GetWidget(), nullptr, source->GetAnchorBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, source_type);
}

void DaoControlCenterExtensionsSection::OnContextMenuClosed() {
  context_menu_runner_.reset();
  context_menu_adapter_.reset();
  context_menu_model_.reset();
}

}  // namespace dao
