// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_qr_code_result_dialog_view.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "components/constrained_window/constrained_window_views.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace dao {

namespace {

constexpr int kDialogContentHorizontalInset = 16;
constexpr int kDialogContentVerticalInset = 12;
constexpr int kRowSpacing = 12;
constexpr int kRowPaddingV = 8;
constexpr int kRowPaddingH = 0;
constexpr int kRowInternalSpacing = 8;
constexpr int kButtonRowSpacing = 8;
constexpr int kPayloadFontSize = 13;
constexpr int kPayloadMaxLines = 3;
constexpr int kPayloadMaxWidth = 360;
constexpr int kDialogMinWidth = 420;
constexpr int kDialogMinHeight = 200;

// View that floors its preferred size at (min_width, min_height) but lets
// the BoxLayout grow above it when content requires more space.
class FlooredView : public views::View {
 public:
  FlooredView(int min_width, int min_height)
      : min_width_(min_width), min_height_(min_height) {}

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size from_layout = views::View::CalculatePreferredSize(available_size);
    return gfx::Size(std::max(from_layout.width(), min_width_),
                     std::max(from_layout.height(), min_height_));
  }

 private:
  const int min_width_;
  const int min_height_;
};

}  // namespace

// static
void DaoQrCodeResultDialogView::Show(content::WebContents* web_contents,
                                     DecodedQrCodes results) {
  if (!web_contents || results.empty()) {
    return;
  }
  auto dialog = std::make_unique<DaoQrCodeResultDialogView>(
      web_contents, std::move(results));
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), web_contents->GetTopLevelNativeWindow());
  widget->Show();
}

DaoQrCodeResultDialogView::DaoQrCodeResultDialogView(
    content::WebContents* web_contents,
    DecodedQrCodes results)
    : host_web_contents_(web_contents ? web_contents->GetWeakPtr()
                                      : base::WeakPtr<content::WebContents>()),
      results_(std::move(results)) {
  SetTitle(l10n_util::GetStringUTF16(IDS_DAO_QR_RESULT_DIALOG_TITLE));
  SetShowCloseButton(true);
  SetModalType(ui::mojom::ModalType::kWindow);
  SetOwnedByWidget(OwnedByWidgetPassKey());
  // No dialog buttons — the close (X) in the title bar is the only dismiss
  // affordance. Per-row Copy / Open buttons live in the contents.
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));

  SetContentsView(BuildContents());
}

DaoQrCodeResultDialogView::~DaoQrCodeResultDialogView() = default;

std::unique_ptr<views::View> DaoQrCodeResultDialogView::BuildContents() {
  auto contents =
      std::make_unique<FlooredView>(kDialogMinWidth, kDialogMinHeight);
  auto* contents_layout =
      contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets::VH(kDialogContentVerticalInset,
                          kDialogContentHorizontalInset),
          kRowSpacing));

  for (size_t i = 0; i < results_.size(); ++i) {
    const auto& entry = results_[i];
    const bool is_last = (i + 1 == results_.size());

    auto* row = contents->AddChildView(std::make_unique<views::View>());
    // Last row absorbs the remaining vertical space so its button row sits
    // flush against the bottom of the dialog.
    if (is_last) {
      contents_layout->SetFlexForView(row, 1);
    }

    auto* row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::VH(kRowPaddingV, kRowPaddingH), kRowInternalSpacing));
    // Stretch children horizontally so the button row can right-align inside
    // the row's full width.
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto label_unique =
        std::make_unique<views::Label>(base::UTF8ToUTF16(entry.text));
    auto* label = label_unique.get();
    label->SetMultiLine(true);
    label->SetMaxLines(kPayloadMaxLines);
    label->SetSelectable(true);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    label->SetFontList(gfx::FontList({"system-ui", "Menlo", "monospace"},
                                     gfx::Font::NORMAL, kPayloadFontSize,
                                     gfx::Font::Weight::NORMAL));
    label->SetEnabledColor(TextPrimary());
    label->SetMaximumWidth(kPayloadMaxWidth);
    label->SetVerticalAlignment(gfx::ALIGN_TOP);
    row->AddChildView(std::move(label_unique));
    // Label takes any extra vertical space so the button row stays pinned
    // to the bottom of the row.
    row_layout->SetFlexForView(label, 1);

    auto* button_row = row->AddChildView(std::make_unique<views::View>());
    auto* button_row_layout =
        button_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
            kButtonRowSpacing));
    button_row_layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kEnd);

    button_row->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&DaoQrCodeResultDialogView::OnCopy,
                            base::Unretained(this), entry.text),
        l10n_util::GetStringUTF16(IDS_DAO_QR_RESULT_COPY)));

    if (entry.is_url && entry.url.is_valid()) {
      auto* open_button =
          button_row->AddChildView(std::make_unique<views::MdTextButton>(
              base::BindRepeating(&DaoQrCodeResultDialogView::OnOpen,
                                  base::Unretained(this), entry.url),
              l10n_util::GetStringUTF16(IDS_DAO_QR_RESULT_OPEN)));
      open_button->SetStyle(ui::ButtonStyle::kProminent);
    }
  }
  return contents;
}

void DaoQrCodeResultDialogView::OnCopy(const std::string& text) {
  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(base::UTF8ToUTF16(text));
}

void DaoQrCodeResultDialogView::OnOpen(const GURL& url) {
  if (!host_web_contents_ || !url.is_valid()) {
    return;
  }
  content::OpenURLParams params(url, content::Referrer(),
                                WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                ui::PAGE_TRANSITION_LINK,
                                /*is_renderer_initiated=*/false);
  host_web_contents_->OpenURL(params, /*navigation_handle_callback=*/{});
  if (auto* widget = GetWidget()) {
    widget->CloseWithReason(views::Widget::ClosedReason::kAcceptButtonClicked);
  }
}

}  // namespace dao
