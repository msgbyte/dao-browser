# System Dialog Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in Dao system dialog style with visible shortcut badges and shared Dao helper APIs, then apply it to the QR-code result dialog and the extension install dialog path.

**Architecture:** The base Chromium Views patch adds opt-in metadata to `views::DialogDelegate` and renders Dao-styled standard buttons from `views::DialogClientView` without depending on Dao headers. Dao-owned code adds `dao::ConfigureDaoSystemDialog()` and helper-created shortcut buttons, then individual Dao dialogs adopt the helper.

**Tech Stack:** Chromium Views C++, Dao source files under `src/dao`, Chromium patch source under `src/patches`, browser tests in `browser_tests`, project build commands through npm scripts only.

---

## Guardrails

- Do not run `npm run build`; use `npm run rebuild` or `npm run build:debug -- --target browser_tests`.
- Do not run `ninja`, `autoninja`, or `siso` directly.
- Do not run `git add`, `git commit`, `git push`, `git reset`, or `git stash` unless the latest user message explicitly authorizes that git action.
- Commit checkpoint steps below are planning markers. Skip the git commands during execution unless the user explicitly authorizes them in the latest message.
- Source-of-truth Chromium changes belong in `src/patches/...`. `npm run import` applies them into `engine/src/...`.

## File Structure

| File | Responsibility |
|---|---|
| `src/patches/ui/views/window/dialog_delegate.h.patch` | Adds opt-in style fields, shortcut storage, and public API to `views::DialogDelegate`. |
| `src/patches/ui/views/window/dialog_delegate.cc.patch` | Implements opt-in setters/getters, model-change notifications, and opt-in dialog frame surface color. |
| `src/patches/ui/views/window/dialog_client_view.cc.patch` | Renders opt-in standard buttons with Dao surface styling and keycap badge child labels. |
| `src/patches/chrome/browser/ui/BUILD.gn.patch` | Wires `dao_system_dialog.{h,cc}` into the existing Dao Views source list. |
| `src/patches/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch` | Includes the Dao helper and opts the extension install dialog into Dao system styling. |
| `src/dao/browser/ui/views/dao_system_dialog.h` | Public Dao helper API for configuring dialogs and creating shortcut action buttons. |
| `src/dao/browser/ui/views/dao_system_dialog.cc` | Dao-owned button subclass, shortcut registration for custom buttons, and Dao color application. |
| `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.cc` | Calls the helper and replaces ad hoc row `MdTextButton` creation. |
| `src/dao/browser/ui/views/dao_browser_browsertest.cc` | Focused tests for base opt-in, helper button shortcuts, and QR dialog adoption. |

## Task 1: Add Failing Browser Tests

**Files:**
- Modify: `src/dao/browser/ui/views/dao_browser_browsertest.cc`

- [ ] **Step 1: Add includes for dialog and shortcut tests**

Add these includes near the existing Views/UI includes:

```cpp
#include "dao/browser/ui/views/dao_qr_code_result_dialog_view.h"
#include "dao/browser/ui/views/dao_system_dialog.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/view_utils.h"
#include "ui/views/window/dialog_delegate.h"
```

- [ ] **Step 2: Add local test helpers**

Add this block inside `namespace dao { namespace {` after `GetBrowserView()`:

```cpp
bool HasDescendantLabelText(views::View* root, std::u16string_view text) {
  if (!root) {
    return false;
  }
  if (auto* label = views::AsViewClass<views::Label>(root);
      label && label->GetText() == text) {
    return true;
  }
  for (views::View* child : root->children()) {
    if (HasDescendantLabelText(child, text)) {
      return true;
    }
  }
  return false;
}

void SendDialogKey(views::Widget* widget,
                   ui::KeyboardCode key_code,
                   int flags = ui::EF_NONE) {
  ui::KeyEvent event(ui::EventType::kKeyPressed, key_code, flags);
  if (widget->GetFocusManager()->OnKeyEvent(event)) {
    widget->OnKeyEvent(&event);
  }
}

class CountingDialogDelegate : public views::DialogDelegate {
 public:
  CountingDialogDelegate() {
    SetTitle(u"Dao system dialog test");
    SetModalType(ui::mojom::ModalType::kWindow);
    SetShowCloseButton(false);
    SetContentsView(std::make_unique<views::View>());
    SetAcceptCallbackWithClose(base::BindRepeating(
        &CountingDialogDelegate::OnAccepted, base::Unretained(this)));
    SetCancelCallbackWithClose(base::BindRepeating(
        &CountingDialogDelegate::OnCancelled, base::Unretained(this)));
  }

  int accepted_count() const { return accepted_count_; }
  int cancelled_count() const { return cancelled_count_; }

 private:
  bool OnAccepted() {
    ++accepted_count_;
    return false;
  }

  bool OnCancelled() {
    ++cancelled_count_;
    return false;
  }

  int accepted_count_ = 0;
  int cancelled_count_ = 0;
};

views::Widget* ShowCountingDialog(Browser* browser,
                                  CountingDialogDelegate* dialog) {
  return constrained_window::CreateBrowserModalDialogViews(
      dialog, browser->window()->GetNativeWindow());
}
```

- [ ] **Step 3: Add tests for the base opt-in surface**

Append this section before the final `}  // namespace` block:

```cpp
// =============================================================================
// DaoSystemDialogBrowserTest
// =============================================================================

class DaoSystemDialogBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoSystemDialogBrowserTest,
                       NonOptInDialogHasNoDaoShortcutBadges) {
  auto dialog = std::make_unique<CountingDialogDelegate>();
  CountingDialogDelegate* raw_dialog = dialog.get();
  views::Widget* widget = ShowCountingDialog(browser(), raw_dialog);
  widget->Show();

  ASSERT_FALSE(raw_dialog->use_dao_system_dialog_style());
  ASSERT_NE(nullptr, raw_dialog->GetOkButton());
  ASSERT_NE(nullptr, raw_dialog->GetCancelButton());
  EXPECT_FALSE(raw_dialog->GetButtonShortcut(
      ui::mojom::DialogButton::kOk).has_value());
  EXPECT_FALSE(HasDescendantLabelText(raw_dialog->GetOkButton(), u"Enter"));
  EXPECT_FALSE(HasDescendantLabelText(raw_dialog->GetCancelButton(), u"Esc"));

  widget->CloseNow();
  dialog.reset();
}

IN_PROC_BROWSER_TEST_F(DaoSystemDialogBrowserTest,
                       OptInDialogShowsShortcutBadges) {
  auto dialog = std::make_unique<CountingDialogDelegate>();
  CountingDialogDelegate* raw_dialog = dialog.get();
  dao::ConfigureDaoSystemDialog(raw_dialog);
  views::Widget* widget = ShowCountingDialog(browser(), raw_dialog);
  widget->Show();

  ASSERT_TRUE(raw_dialog->use_dao_system_dialog_style());
  ASSERT_NE(nullptr, raw_dialog->GetOkButton());
  ASSERT_NE(nullptr, raw_dialog->GetCancelButton());
  EXPECT_EQ(u"Enter", raw_dialog
                         ->GetButtonShortcut(ui::mojom::DialogButton::kOk)
                         ->keycap);
  EXPECT_EQ(u"Esc", raw_dialog
                      ->GetButtonShortcut(ui::mojom::DialogButton::kCancel)
                      ->keycap);
  EXPECT_TRUE(HasDescendantLabelText(raw_dialog->GetOkButton(), u"Enter"));
  EXPECT_TRUE(HasDescendantLabelText(raw_dialog->GetCancelButton(), u"Esc"));
  EXPECT_EQ(raw_dialog->GetDialogButtonLabel(ui::mojom::DialogButton::kOk),
            raw_dialog->GetOkButton()->GetText());

  widget->CloseNow();
  dialog.reset();
}

IN_PROC_BROWSER_TEST_F(DaoSystemDialogBrowserTest,
                       OptInDialogKeyboardActionsUseDialogCallbacks) {
  auto dialog = std::make_unique<CountingDialogDelegate>();
  CountingDialogDelegate* raw_dialog = dialog.get();
  dao::ConfigureDaoSystemDialog(raw_dialog);
  views::Widget* widget = ShowCountingDialog(browser(), raw_dialog);
  widget->Show();

  SendDialogKey(widget, ui::VKEY_RETURN);
  EXPECT_EQ(1, raw_dialog->accepted_count());
  EXPECT_EQ(0, raw_dialog->cancelled_count());

  SendDialogKey(widget, ui::VKEY_ESCAPE);
  EXPECT_EQ(1, raw_dialog->accepted_count());
  EXPECT_EQ(1, raw_dialog->cancelled_count());

  raw_dialog->SetButtonEnabled(ui::mojom::DialogButton::kOk, false);
  SendDialogKey(widget, ui::VKEY_RETURN);
  EXPECT_EQ(1, raw_dialog->accepted_count());

  widget->CloseNow();
  dialog.reset();
}
```

- [ ] **Step 4: Add tests for helper-created custom action buttons and QR adoption**

Append these tests after the `DaoSystemDialogBrowserTest` tests:

```cpp
IN_PROC_BROWSER_TEST_F(DaoSystemDialogBrowserTest,
                       HelperButtonInvokesSameCallbackFromAccelerator) {
  int pressed_count = 0;
  auto button = dao::CreateDaoDialogButton(
      base::BindLambdaForTesting([&](const ui::Event&) { ++pressed_count; }),
      u"Copy",
      dao::DaoDialogShortcut{ui::Accelerator(ui::VKEY_C,
                                             ui::EF_PLATFORM_ACCELERATOR |
                                                 ui::EF_SHIFT_DOWN),
                             dao::PlatformShortcutKeycap(u"C", true)},
      ui::ButtonStyle::kTonal);

  EXPECT_TRUE(HasDescendantLabelText(button.get(),
                                     dao::PlatformShortcutKeycap(u"C", true)));
  EXPECT_TRUE(button->AcceleratorPressed(ui::Accelerator(
      ui::VKEY_C, ui::EF_PLATFORM_ACCELERATOR | ui::EF_SHIFT_DOWN)));
  EXPECT_EQ(1, pressed_count);

  button->SetEnabled(false);
  EXPECT_FALSE(button->AcceleratorPressed(ui::Accelerator(
      ui::VKEY_C, ui::EF_PLATFORM_ACCELERATOR | ui::EF_SHIFT_DOWN)));
  EXPECT_EQ(1, pressed_count);
}

class DaoQrCodeResultDialogBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoQrCodeResultDialogBrowserTest,
                       SingleResultUsesDaoSystemDialogHelper) {
  DecodedQrCodes results;
  DecodedQrCode result;
  result.text = "https://example.com/";
  result.is_url = true;
  result.url = GURL("https://example.com/");
  results.push_back(std::move(result));

  DaoQrCodeResultDialogView dialog(
      browser()->tab_strip_model()->GetActiveWebContents(), std::move(results));

  EXPECT_TRUE(dialog.use_dao_system_dialog_style());
  EXPECT_TRUE(HasDescendantLabelText(dialog.GetContentsView(),
                                     dao::PlatformShortcutKeycap(u"C", true)));
  EXPECT_TRUE(HasDescendantLabelText(dialog.GetContentsView(),
                                     dao::PlatformShortcutKeycap(u"O", false)));
}

IN_PROC_BROWSER_TEST_F(DaoQrCodeResultDialogBrowserTest,
                       MultipleResultsOmitAmbiguousRowShortcuts) {
  DecodedQrCodes results;
  DecodedQrCode first;
  first.text = "first payload";
  results.push_back(std::move(first));
  DecodedQrCode second;
  second.text = "second payload";
  results.push_back(std::move(second));

  DaoQrCodeResultDialogView dialog(
      browser()->tab_strip_model()->GetActiveWebContents(), std::move(results));

  EXPECT_TRUE(dialog.use_dao_system_dialog_style());
  EXPECT_FALSE(HasDescendantLabelText(
      dialog.GetContentsView(), dao::PlatformShortcutKeycap(u"C", true)));
}
```

- [ ] **Step 5: Run the tests and verify they fail for missing APIs**

Run:

```bash
npm run import
npm run build:debug -- --target browser_tests
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoSystemDialogBrowserTest.*:DaoQrCodeResultDialogBrowserTest.*"
```

Expected: compile failure naming missing `dao/browser/ui/views/dao_system_dialog.h`, missing `DialogDelegate::use_dao_system_dialog_style`, and missing `DialogDelegate::GetButtonShortcut`.

- [ ] **Step 6: Commit checkpoint with explicit user authorization**

Only after the user explicitly authorizes git state changes, run:

```bash
git add src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "test(dialog): cover dao system dialog opt-in"
```

## Task 2: Add DialogDelegate Opt-In API

**Files:**
- Create: `src/patches/ui/views/window/dialog_delegate.h.patch`
- Create: `src/patches/ui/views/window/dialog_delegate.cc.patch`

- [ ] **Step 1: Patch `dialog_delegate.h`**

Create `src/patches/ui/views/window/dialog_delegate.h.patch` with this source-level change:

```diff
diff --git a/ui/views/window/dialog_delegate.h b/ui/views/window/dialog_delegate.h
index 0000000000..0000000000 100644
--- a/ui/views/window/dialog_delegate.h
+++ b/ui/views/window/dialog_delegate.h
@@
 #include "ui/base/metadata/metadata_header_macros.h"
+#include "ui/base/accelerators/accelerator.h"
 #include "ui/base/mojom/dialog_button.mojom.h"
@@
 class VIEWS_EXPORT DialogDelegate : public WidgetDelegate {
  public:
+  struct DialogButtonShortcut {
+    DialogButtonShortcut();
+    DialogButtonShortcut(const ui::Accelerator& accelerator,
+                         std::u16string keycap);
+    DialogButtonShortcut(const DialogButtonShortcut&);
+    DialogButtonShortcut& operator=(const DialogButtonShortcut&);
+    ~DialogButtonShortcut();
+
+    bool operator==(const DialogButtonShortcut&) const = default;
+
+    ui::Accelerator accelerator;
+    std::u16string keycap;
+  };
+
   struct Params {
@@
     std::array<std::optional<ui::ButtonStyle>,
                static_cast<size_t>(ui::mojom::DialogButton::kCancel) + 1>
         button_styles;
+
+    // Dao system dialog opt-in. Defaults off so upstream Chromium dialogs keep
+    // their existing appearance and behavior unless explicitly enabled.
+    bool use_dao_system_dialog_style = false;
+
+    std::array<std::optional<DialogButtonShortcut>,
+               static_cast<size_t>(ui::mojom::DialogButton::kCancel) + 1>
+        button_shortcuts;
@@
   ui::ButtonStyle GetDialogButtonStyle(
       ui::mojom::DialogButton button) const;
+
+  void SetUseDaoSystemDialogStyle(bool use_style);
+  bool use_dao_system_dialog_style() const {
+    return params_.use_dao_system_dialog_style;
+  }
+
+  void SetButtonShortcut(ui::mojom::DialogButton button,
+                         const ui::Accelerator& accelerator,
+                         std::u16string_view keycap);
+  void ClearButtonShortcut(ui::mojom::DialogButton button);
+  std::optional<DialogButtonShortcut> GetButtonShortcut(
+      ui::mojom::DialogButton button) const;
```

When applying manually, keep the actual `index` line generated by `git diff`; the code above shows exact insertions.

- [ ] **Step 2: Patch `dialog_delegate.cc`**

Create `src/patches/ui/views/window/dialog_delegate.cc.patch` with this source-level change:

```diff
diff --git a/ui/views/window/dialog_delegate.cc b/ui/views/window/dialog_delegate.cc
index 0000000000..0000000000 100644
--- a/ui/views/window/dialog_delegate.cc
+++ b/ui/views/window/dialog_delegate.cc
@@
 #include "ui/gfx/geometry/insets.h"
 #include "ui/gfx/geometry/rounded_corners_f.h"
+#include "ui/native_theme/native_theme.h"
 #include "ui/strings/grit/ui_strings.h"
@@
 bool HasCallback(
     const std::variant<base::OnceClosure, base::RepeatingCallback<bool()>>&
         callback) {
@@
 }
+
+SkColor DaoSystemDialogSurfaceColor() {
+  const ui::NativeTheme* theme = ui::NativeTheme::GetInstanceForNativeUi();
+  const bool dark =
+      theme && theme->preferred_color_scheme() ==
+                   ui::NativeTheme::PreferredColorScheme::kDark;
+  return dark ? SkColorSetARGB(242, 47, 53, 60)
+              : SkColorSetARGB(242, 255, 255, 255);
+}
 
 #if !BUILDFLAG(IS_APPLE)
 bool UseDesktopWidgetOverride(WidgetDelegate* delegate) {
@@
   frame->SetBubbleBorder(std::move(border));
+  if (delegate && delegate->use_dao_system_dialog_style()) {
+    frame->SetBackgroundColor(DaoSystemDialogSurfaceColor());
+  }
   return frame;
 }
@@
 ////////////////////////////////////////////////////////////////////////////////
+// DialogDelegate::DialogButtonShortcut:
+
+DialogDelegate::DialogButtonShortcut::DialogButtonShortcut() = default;
+DialogDelegate::DialogButtonShortcut::DialogButtonShortcut(
+    const ui::Accelerator& accelerator,
+    std::u16string keycap)
+    : accelerator(accelerator), keycap(std::move(keycap)) {}
+DialogDelegate::DialogButtonShortcut::DialogButtonShortcut(
+    const DialogButtonShortcut&) = default;
+DialogDelegate::DialogButtonShortcut&
+DialogDelegate::DialogButtonShortcut::operator=(const DialogButtonShortcut&) =
+    default;
+DialogDelegate::DialogButtonShortcut::~DialogButtonShortcut() = default;
+
+////////////////////////////////////////////////////////////////////////////////
 // DialogDelegate::Params:
 DialogDelegate::Params::Params() = default;
@@
 ui::ButtonStyle DialogDelegate::GetDialogButtonStyle(
     ui::mojom::DialogButton button) const {
@@
 }
+
+void DialogDelegate::SetUseDaoSystemDialogStyle(bool use_style) {
+  if (params_.use_dao_system_dialog_style == use_style) {
+    return;
+  }
+  params_.use_dao_system_dialog_style = use_style;
+  DialogModelChanged();
+}
+
+void DialogDelegate::SetButtonShortcut(ui::mojom::DialogButton button,
+                                       const ui::Accelerator& accelerator,
+                                       std::u16string_view keycap) {
+  DialogButtonShortcut shortcut(accelerator, std::u16string(keycap));
+  auto& target = params_.button_shortcuts[static_cast<size_t>(button)];
+  if (target == shortcut) {
+    return;
+  }
+  target = std::move(shortcut);
+  DialogModelChanged();
+}
+
+void DialogDelegate::ClearButtonShortcut(ui::mojom::DialogButton button) {
+  auto& target = params_.button_shortcuts[static_cast<size_t>(button)];
+  if (!target.has_value()) {
+    return;
+  }
+  target.reset();
+  DialogModelChanged();
+}
+
+std::optional<DialogDelegate::DialogButtonShortcut>
+DialogDelegate::GetButtonShortcut(ui::mojom::DialogButton button) const {
+  return params_.button_shortcuts[static_cast<size_t>(button)];
+}
```

- [ ] **Step 3: Apply patches into `engine/src`**

Run:

```bash
npm run import
```

Expected: both new `src/patches/ui/views/window/*.patch` files apply cleanly to `engine/src/ui/views/window/...`.

- [ ] **Step 4: Build focused target and confirm test failure moves forward**

Run:

```bash
npm run build:debug -- --target browser_tests
```

Expected: failure still occurs because `dao_system_dialog.h` does not exist yet, not because of `DialogDelegate` API.

- [ ] **Step 5: Commit checkpoint with explicit user authorization**

Only after the user explicitly authorizes git state changes, run:

```bash
git add src/patches/ui/views/window/dialog_delegate.h.patch src/patches/ui/views/window/dialog_delegate.cc.patch
git commit -m "feat(dialog): add dao system dialog opt-in"
```

## Task 3: Render Dao-Styled Standard Dialog Buttons

**Files:**
- Create: `src/patches/ui/views/window/dialog_client_view.cc.patch`

- [ ] **Step 1: Patch includes in `dialog_client_view.cc`**

Create `src/patches/ui/views/window/dialog_client_view.cc.patch` and add these includes:

```diff
diff --git a/ui/views/window/dialog_client_view.cc b/ui/views/window/dialog_client_view.cc
index 0000000000..0000000000 100644
--- a/ui/views/window/dialog_client_view.cc
+++ b/ui/views/window/dialog_client_view.cc
@@
 #include "ui/color/color_provider.h"
 #include "ui/events/keycodes/keyboard_codes.h"
 #include "ui/gfx/geometry/rounded_corners_f.h"
+#include "ui/native_theme/native_theme.h"
 #include "ui/views/background.h"
 #include "ui/views/border.h"
 #include "ui/views/controls/button/button.h"
@@
 #include "ui/views/controls/button/md_text_button.h"
+#include "ui/views/controls/label.h"
```

- [ ] **Step 2: Add Dao visual constants and button subclass**

Inside the anonymous namespace in `dialog_client_view.cc`, after `GetButtonId()`, add:

```cpp
constexpr int kDaoDialogButtonRadius = 8;
constexpr int kDaoShortcutGap = 8;
constexpr int kDaoShortcutBadgeHorizontalInset = 7;
constexpr int kDaoShortcutBadgeVerticalInset = 2;
constexpr int kDaoShortcutBadgeRadius = 6;

constexpr gfx::Insets kDaoDialogButtonPadding = gfx::Insets::VH(7, 13);

bool IsDaoDialogDark(const views::View* view) {
  const ui::NativeTheme* theme =
      view ? view->GetNativeTheme()
           : ui::NativeTheme::GetInstanceForNativeUi();
  return theme && theme->preferred_color_scheme() ==
                      ui::NativeTheme::PreferredColorScheme::kDark;
}

SkColor DaoDialogButtonBackground(bool dark, ui::ButtonStyle style) {
  if (style == ui::ButtonStyle::kProminent) {
    return dark ? SkColorSetRGB(248, 250, 252)
                : SkColorSetRGB(16, 24, 40);
  }
  return dark ? SkColorSetARGB(46, 255, 255, 255)
              : SkColorSetARGB(18, 16, 24, 40);
}

SkColor DaoDialogButtonText(bool dark, ui::ButtonStyle style) {
  if (style == ui::ButtonStyle::kProminent) {
    return dark ? SkColorSetRGB(17, 24, 39)
                : SkColorSetRGB(255, 255, 255);
  }
  return dark ? SkColorSetRGB(248, 250, 252)
              : SkColorSetRGB(17, 24, 39);
}

SkColor DaoDialogButtonStroke(bool dark, ui::ButtonStyle style) {
  if (style == ui::ButtonStyle::kProminent) {
    return dark ? SkColorSetARGB(56, 255, 255, 255)
                : SkColorSetARGB(28, 15, 23, 42);
  }
  return dark ? SkColorSetARGB(36, 255, 255, 255)
              : SkColorSetARGB(24, 15, 23, 42);
}

SkColor DaoShortcutBadgeBackground(bool dark) {
  return dark ? SkColorSetARGB(32, 15, 23, 42)
              : SkColorSetARGB(20, 255, 255, 255);
}

SkColor DaoShortcutBadgeText(bool dark, bool enabled) {
  const int alpha = enabled ? 190 : 90;
  return dark ? SkColorSetARGB(alpha, 15, 23, 42)
              : SkColorSetARGB(alpha, 255, 255, 255);
}

class DaoSystemDialogButton : public MdTextButton {
  METADATA_HEADER(DaoSystemDialogButton, MdTextButton)

 public:
  DaoSystemDialogButton(PressedCallback callback,
                        std::u16string_view text,
                        ui::ButtonStyle style)
      : MdTextButton(std::move(callback), text) {
    shortcut_badge_ = AddChildView(std::make_unique<Label>());
    shortcut_badge_->SetVisible(false);
    shortcut_badge_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    shortcut_badge_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(
        kDaoShortcutBadgeVerticalInset, kDaoShortcutBadgeHorizontalInset)));
    SetStyle(style);
    ApplyDaoStyle();
  }

  DaoSystemDialogButton(const DaoSystemDialogButton&) = delete;
  DaoSystemDialogButton& operator=(const DaoSystemDialogButton&) = delete;
  ~DaoSystemDialogButton() override = default;

  void SetShortcutKeycap(std::u16string keycap) {
    shortcut_badge_->SetText(std::move(keycap));
    shortcut_badge_->SetVisible(!shortcut_badge_->GetText().empty());
    PreferredSizeChanged();
  }

  void SetStyle(ui::ButtonStyle style) {
    MdTextButton::SetStyle(style);
    ApplyDaoStyle();
  }

  void OnThemeChanged() override {
    MdTextButton::OnThemeChanged();
    ApplyDaoStyle();
  }

  void StateChanged(ButtonState old_state) override {
    MdTextButton::StateChanged(old_state);
    ApplyDaoStyle();
  }

  gfx::Size CalculatePreferredSize(
      const SizeBounds& available_size) const override {
    gfx::Size size = MdTextButton::CalculatePreferredSize(available_size);
    if (shortcut_badge_->GetVisible()) {
      const gfx::Size badge_size = shortcut_badge_->GetPreferredSize({});
      size.Enlarge(kDaoShortcutGap + badge_size.width(), 0);
      size.SetToMax(gfx::Size(0, badge_size.height() + GetInsets().height()));
    }
    return size;
  }

  ProposedLayout CalculateProposedLayout(
      const SizeBounds& size_bounds) const override {
    ProposedLayout layout = MdTextButton::CalculateProposedLayout(size_bounds);
    if (!size_bounds.is_fully_bounded() || !shortcut_badge_->GetVisible()) {
      return layout;
    }

    const gfx::Size badge_size = shortcut_badge_->GetPreferredSize({});
    const int reserved_width = kDaoShortcutGap + badge_size.width();
    for (auto& child_layout : layout.child_layouts) {
      if (child_layout.child_view == label()) {
        gfx::Rect label_bounds = child_layout.bounds;
        label_bounds.set_width(std::max(0, label_bounds.width() - reserved_width));
        child_layout.bounds = label_bounds;

        gfx::Rect badge_bounds(gfx::Point(label_bounds.right() + kDaoShortcutGap,
                                          (size_bounds.height().value() -
                                           badge_size.height()) /
                                              2),
                               badge_size);
        layout.child_layouts.emplace_back(shortcut_badge_.get(), true,
                                          badge_bounds, SizeBounds());
        break;
      }
    }
    return layout;
  }

 private:
  void ApplyDaoStyle() {
    const bool dark = IsDaoDialogDark(this);
    const ui::ButtonStyle style = GetStyle();
    SetCornerRadius(kDaoDialogButtonRadius);
    SetFocusRingCornerRadius(kDaoDialogButtonRadius);
    SetCustomPadding(kDaoDialogButtonPadding);
    SetBgColorOverrideDeprecated(DaoDialogButtonBackground(dark, style));
    SetStrokeColorOverrideDeprecated(DaoDialogButtonStroke(dark, style));
    SetEnabledTextColors(ui::ColorVariant(DaoDialogButtonText(dark, style)));
    SetTextColor(Button::STATE_DISABLED,
                 ui::ColorVariant(SkColorSetA(DaoDialogButtonText(dark, style),
                                              102)));

    shortcut_badge_->SetEnabledColor(
        DaoShortcutBadgeText(dark, GetEnabled()));
    shortcut_badge_->SetBackground(views::CreateRoundedRectBackground(
        DaoShortcutBadgeBackground(dark), kDaoShortcutBadgeRadius));
  }

  raw_ptr<views::Label> shortcut_badge_ = nullptr;
};

BEGIN_METADATA(DaoSystemDialogButton)
END_METADATA
```

- [ ] **Step 3: Create Dao buttons in `UpdateDialogButton()`**

Replace the builder block that creates `Builder<MdTextButton>()` with explicit construction:

```cpp
const auto maybe_shortcut =
    delegate->use_dao_system_dialog_style()
        ? delegate->GetButtonShortcut(type)
        : std::optional<DialogDelegate::DialogButtonShortcut>();

std::unique_ptr<MdTextButton> new_button;
if (delegate->use_dao_system_dialog_style()) {
  auto dao_button = std::make_unique<DaoSystemDialogButton>(
      base::BindRepeating(&DialogClientView::ButtonPressed,
                          base::Unretained(this), type),
      title, style);
  dao_button->SetShortcutKeycap(maybe_shortcut ? maybe_shortcut->keycap
                                               : std::u16string());
  new_button = std::move(dao_button);
} else {
  new_button = std::make_unique<MdTextButton>(
      base::BindRepeating(&DialogClientView::ButtonPressed,
                          base::Unretained(this), type),
      title);
  new_button->SetStyle(style);
}

new_button->SetProperty(views::kElementIdentifierKey, GetButtonId(type));
new_button->SetIsDefault(is_default);
new_button->SetEnabled(delegate->IsDialogButtonEnabled(type));
new_button->SetMinSize(gfx::Size(minimum_width, 0));
new_button->SetGroup(kButtonGroup);
*member = button_row_container_->AddChildView(std::move(new_button));
```

Keep the existing member-update branch, but add this line after `button->SetStyle(style);`:

```cpp
if (auto* dao_button = AsViewClass<DaoSystemDialogButton>(button)) {
  dao_button->SetShortcutKeycap(maybe_shortcut ? maybe_shortcut->keycap
                                               : std::u16string());
}
```

- [ ] **Step 4: Apply patches**

Run:

```bash
npm run import
```

Expected: `src/patches/ui/views/window/dialog_client_view.cc.patch` applies cleanly.

- [ ] **Step 5: Build focused target**

Run:

```bash
npm run build:debug -- --target browser_tests
```

Expected: build still fails only on missing Dao helper symbols.

- [ ] **Step 6: Commit checkpoint with explicit user authorization**

Only after the user explicitly authorizes git state changes, run:

```bash
git add src/patches/ui/views/window/dialog_client_view.cc.patch
git commit -m "feat(dialog): render dao styled standard buttons"
```

## Task 4: Add Dao System Dialog Helper

**Files:**
- Create: `src/dao/browser/ui/views/dao_system_dialog.h`
- Create: `src/dao/browser/ui/views/dao_system_dialog.cc`
- Modify: `src/patches/chrome/browser/ui/BUILD.gn.patch`

- [ ] **Step 1: Create helper header**

Create `src/dao/browser/ui/views/dao_system_dialog.h`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_SYSTEM_DIALOG_H_
#define DAO_BROWSER_UI_VIEWS_DAO_SYSTEM_DIALOG_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ui/base/accelerators/accelerator.h"
#include "ui/base/ui_base_types.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"

namespace views {
class DialogDelegate;
}  // namespace views

namespace dao {

struct DaoDialogShortcut {
  ui::Accelerator accelerator;
  std::u16string keycap;
};

struct DaoSystemDialogOptions {
  bool show_enter_for_default = true;
  bool show_esc_for_cancel = true;
};

std::u16string PlatformShortcutKeycap(std::u16string_view key,
                                      bool include_shift);

void ConfigureDaoSystemDialog(
    views::DialogDelegate* delegate,
    const DaoSystemDialogOptions& options = DaoSystemDialogOptions());

std::unique_ptr<views::MdTextButton> CreateDaoDialogButton(
    views::Button::PressedCallback callback,
    std::u16string_view label,
    std::optional<DaoDialogShortcut> shortcut,
    ui::ButtonStyle style);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_SYSTEM_DIALOG_H_
```

- [ ] **Step 2: Create helper implementation**

Create `src/dao/browser/ui/views/dao_system_dialog.cc`:

```cpp
// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_system_dialog.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/window/dialog_delegate.h"

namespace dao {
namespace {

constexpr int kDaoDialogCornerRadius = 16;
constexpr int kDaoDialogButtonRadius = 8;
constexpr int kDaoShortcutGap = 8;
constexpr int kDaoShortcutBadgeHorizontalInset = 7;
constexpr int kDaoShortcutBadgeVerticalInset = 2;
constexpr int kDaoShortcutBadgeRadius = 6;

constexpr gfx::Insets kDaoDialogContentMargins =
    gfx::Insets::TLBR(18, 22, 16, 22);
constexpr gfx::Insets kDaoDialogTitleMargins =
    gfx::Insets::TLBR(20, 22, 8, 22);
constexpr gfx::Insets kDaoDialogButtonPadding = gfx::Insets::VH(7, 13);

SkColor DialogButtonBackground(ui::ButtonStyle style) {
  if (style == ui::ButtonStyle::kProminent) {
    return IsDarkMode() ? SkColorSetRGB(248, 250, 252)
                        : SkColorSetRGB(16, 24, 40);
  }
  return IsDarkMode() ? SkColorSetARGB(46, 255, 255, 255)
                      : SkColorSetARGB(18, 16, 24, 40);
}

SkColor DialogButtonText(ui::ButtonStyle style) {
  if (style == ui::ButtonStyle::kProminent) {
    return IsDarkMode() ? SkColorSetRGB(17, 24, 39)
                        : SkColorSetRGB(255, 255, 255);
  }
  return TextPrimary();
}

SkColor DialogButtonStroke(ui::ButtonStyle style) {
  if (style == ui::ButtonStyle::kProminent) {
    return IsDarkMode() ? SkColorSetARGB(56, 255, 255, 255)
                        : SkColorSetARGB(28, 15, 23, 42);
  }
  return IsDarkMode() ? SkColorSetARGB(36, 255, 255, 255)
                      : SkColorSetARGB(24, 15, 23, 42);
}

SkColor ShortcutBadgeBackground() {
  return IsDarkMode() ? SkColorSetARGB(34, 15, 23, 42)
                      : SkColorSetARGB(22, 255, 255, 255);
}

SkColor ShortcutBadgeText(bool enabled) {
  return IsDarkMode()
             ? SkColorSetARGB(enabled ? 190 : 90, 15, 23, 42)
             : SkColorSetARGB(enabled ? 190 : 90, 255, 255, 255);
}

class DaoShortcutTextButton : public views::MdTextButton {
  METADATA_HEADER(DaoShortcutTextButton, views::MdTextButton)

 public:
  DaoShortcutTextButton(PressedCallback callback,
                        std::u16string_view text,
                        std::optional<DaoDialogShortcut> shortcut,
                        ui::ButtonStyle style)
      : MdTextButton(std::move(callback), text),
        shortcut_(std::move(shortcut)) {
    shortcut_badge_ = AddChildView(std::make_unique<views::Label>());
    shortcut_badge_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    shortcut_badge_->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(
        kDaoShortcutBadgeVerticalInset, kDaoShortcutBadgeHorizontalInset)));
    shortcut_badge_->SetVisible(shortcut_.has_value());
    if (shortcut_) {
      shortcut_badge_->SetText(shortcut_->keycap);
      AddAccelerator(shortcut_->accelerator);
    }
    SetStyle(style);
    ApplyDaoStyle();
  }

  DaoShortcutTextButton(const DaoShortcutTextButton&) = delete;
  DaoShortcutTextButton& operator=(const DaoShortcutTextButton&) = delete;
  ~DaoShortcutTextButton() override = default;

  bool AcceleratorPressed(const ui::Accelerator& accelerator) override {
    if (!shortcut_ || accelerator != shortcut_->accelerator || !GetEnabled()) {
      return false;
    }
    ui::KeyEvent event(ui::EventType::kKeyPressed, accelerator.key_code(),
                       accelerator.modifiers());
    NotifyClick(event);
    return true;
  }

  void OnThemeChanged() override {
    views::MdTextButton::OnThemeChanged();
    ApplyDaoStyle();
  }

  void StateChanged(ButtonState old_state) override {
    views::MdTextButton::StateChanged(old_state);
    ApplyDaoStyle();
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    gfx::Size size = views::MdTextButton::CalculatePreferredSize(available_size);
    if (shortcut_badge_->GetVisible()) {
      const gfx::Size badge_size = shortcut_badge_->GetPreferredSize({});
      size.Enlarge(kDaoShortcutGap + badge_size.width(), 0);
      size.SetToMax(gfx::Size(0, badge_size.height() + GetInsets().height()));
    }
    return size;
  }

  views::ProposedLayout CalculateProposedLayout(
      const views::SizeBounds& size_bounds) const override {
    views::ProposedLayout layout =
        views::MdTextButton::CalculateProposedLayout(size_bounds);
    if (!size_bounds.is_fully_bounded() || !shortcut_badge_->GetVisible()) {
      return layout;
    }

    const gfx::Size badge_size = shortcut_badge_->GetPreferredSize({});
    const int reserved_width = kDaoShortcutGap + badge_size.width();
    for (auto& child_layout : layout.child_layouts) {
      if (child_layout.child_view == label()) {
        gfx::Rect label_bounds = child_layout.bounds;
        label_bounds.set_width(std::max(0, label_bounds.width() - reserved_width));
        child_layout.bounds = label_bounds;

        gfx::Rect badge_bounds(gfx::Point(label_bounds.right() + kDaoShortcutGap,
                                          (size_bounds.height().value() -
                                           badge_size.height()) /
                                              2),
                               badge_size);
        layout.child_layouts.emplace_back(shortcut_badge_.get(), true,
                                          badge_bounds, views::SizeBounds());
        break;
      }
    }
    return layout;
  }

 private:
  void ApplyDaoStyle() {
    const ui::ButtonStyle style = GetStyle();
    SetCornerRadius(kDaoDialogButtonRadius);
    SetFocusRingCornerRadius(kDaoDialogButtonRadius);
    SetCustomPadding(kDaoDialogButtonPadding);
    SetBgColorOverrideDeprecated(DialogButtonBackground(style));
    SetStrokeColorOverrideDeprecated(DialogButtonStroke(style));
    SetEnabledTextColors(ui::ColorVariant(DialogButtonText(style)));
    SetTextColor(views::Button::STATE_DISABLED,
                 ui::ColorVariant(SkColorSetA(DialogButtonText(style), 102)));
    shortcut_badge_->SetEnabledColor(ShortcutBadgeText(GetEnabled()));
    shortcut_badge_->SetBackground(views::CreateRoundedRectBackground(
        ShortcutBadgeBackground(), kDaoShortcutBadgeRadius));
  }

  std::optional<DaoDialogShortcut> shortcut_;
  raw_ptr<views::Label> shortcut_badge_ = nullptr;
};

BEGIN_METADATA(DaoShortcutTextButton)
END_METADATA

std::u16string PlatformAcceleratorPrefix() {
#if BUILDFLAG(IS_MAC)
  return u"Cmd";
#else
  return u"Ctrl";
#endif
}

}  // namespace

std::u16string PlatformShortcutKeycap(std::u16string_view key,
                                      bool include_shift) {
  std::u16string keycap = PlatformAcceleratorPrefix();
  if (include_shift) {
    keycap += u"+Shift";
  }
  keycap += u"+";
  keycap += key;
  return keycap;
}

void ConfigureDaoSystemDialog(views::DialogDelegate* delegate,
                              const DaoSystemDialogOptions& options) {
  if (!delegate) {
    return;
  }
  delegate->SetUseDaoSystemDialogStyle(true);
  delegate->set_corner_radius(kDaoDialogCornerRadius);
  delegate->set_frame_margins({
      .contents = kDaoDialogContentMargins,
      .title = kDaoDialogTitleMargins,
      .footnote = gfx::Insets(),
  });

  const int buttons = delegate->buttons();
  if (options.show_enter_for_default &&
      (buttons & static_cast<int>(ui::mojom::DialogButton::kOk)) &&
      delegate->GetIsDefault(ui::mojom::DialogButton::kOk)) {
    delegate->SetButtonShortcut(
        ui::mojom::DialogButton::kOk,
        ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE), u"Enter");
  }
  if (options.show_esc_for_cancel &&
      (buttons & static_cast<int>(ui::mojom::DialogButton::kCancel))) {
    delegate->SetButtonShortcut(
        ui::mojom::DialogButton::kCancel,
        ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE), u"Esc");
  }
}

std::unique_ptr<views::MdTextButton> CreateDaoDialogButton(
    views::Button::PressedCallback callback,
    std::u16string_view label,
    std::optional<DaoDialogShortcut> shortcut,
    ui::ButtonStyle style) {
  return std::make_unique<DaoShortcutTextButton>(
      std::move(callback), label, std::move(shortcut), style);
}

}  // namespace dao
```

- [ ] **Step 3: Wire helper source into BUILD patch**

Modify `src/patches/chrome/browser/ui/BUILD.gn.patch` in the Dao Views source list, placing the new files near `dao_qr_code_result_dialog_view`:

```diff
     "//dao/browser/ui/views/dao_qr_code_result_dialog_view.cc",
     "//dao/browser/ui/views/dao_qr_code_result_dialog_view.h",
+    "//dao/browser/ui/views/dao_system_dialog.cc",
+    "//dao/browser/ui/views/dao_system_dialog.h",
```

- [ ] **Step 4: Apply source and patches**

Run:

```bash
npm run import
```

Expected: Dao helper files copy into `engine/src/dao/browser/ui/views/`, and the BUILD patch applies cleanly.

- [ ] **Step 5: Build focused target**

Run:

```bash
npm run build:debug -- --target browser_tests
```

Expected: compile failure moves to QR dialog not yet using the helper or to minor include/signature fixes. Fix only the named compile issue, then rerun this step until the focused target compiles.

- [ ] **Step 6: Commit checkpoint with explicit user authorization**

Only after the user explicitly authorizes git state changes, run:

```bash
git add src/dao/browser/ui/views/dao_system_dialog.h src/dao/browser/ui/views/dao_system_dialog.cc src/patches/chrome/browser/ui/BUILD.gn.patch
git commit -m "feat(dialog): add dao system dialog helper"
```

## Task 5: Adopt Helper in QR Result Dialog

**Files:**
- Modify: `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.cc`

- [ ] **Step 1: Add helper include**

Add:

```cpp
#include "dao/browser/ui/views/dao_system_dialog.h"
```

- [ ] **Step 2: Configure the dialog in the constructor**

After `SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));`, add:

```cpp
  dao::ConfigureDaoSystemDialog(
      this, dao::DaoSystemDialogOptions{
                .show_enter_for_default = false,
                .show_esc_for_cancel = false,
            });
```

- [ ] **Step 3: Add a shortcut helper for row actions**

Inside the anonymous namespace in `dao_qr_code_result_dialog_view.cc`, add:

```cpp
std::optional<DaoDialogShortcut> RowActionShortcut(size_t result_count,
                                                   ui::KeyboardCode key_code,
                                                   std::u16string_view key,
                                                   bool include_shift) {
  if (result_count != 1) {
    return std::nullopt;
  }
  return DaoDialogShortcut{
      ui::Accelerator(key_code,
                      ui::EF_PLATFORM_ACCELERATOR |
                          (include_shift ? ui::EF_SHIFT_DOWN : ui::EF_NONE)),
      PlatformShortcutKeycap(key, include_shift),
  };
}
```

- [ ] **Step 4: Replace Copy button creation**

Replace the current `button_row->AddChildView(std::make_unique<views::MdTextButton>(...))` Copy block with:

```cpp
    button_row->AddChildView(dao::CreateDaoDialogButton(
        base::BindRepeating(&DaoQrCodeResultDialogView::OnCopy,
                            base::Unretained(this), entry.text),
        l10n_util::GetStringUTF16(IDS_DAO_QR_RESULT_COPY),
        RowActionShortcut(results_.size(), ui::VKEY_C, u"C",
                          /*include_shift=*/true),
        ui::ButtonStyle::kTonal));
```

- [ ] **Step 5: Replace Open button creation**

Replace the current `auto* open_button = ...; open_button->SetStyle(...)` block with:

```cpp
      button_row->AddChildView(dao::CreateDaoDialogButton(
          base::BindRepeating(&DaoQrCodeResultDialogView::OnOpen,
                              base::Unretained(this), entry.url),
          l10n_util::GetStringUTF16(IDS_DAO_QR_RESULT_OPEN),
          RowActionShortcut(results_.size(), ui::VKEY_O, u"O",
                            /*include_shift=*/false),
          ui::ButtonStyle::kProminent));
```

- [ ] **Step 6: Apply source and rebuild focused target**

Run:

```bash
npm run import
npm run build:debug -- --target browser_tests
```

Expected: focused target compiles. If it fails, fix the exact compile error in the QR dialog/helper boundary and rerun.

- [ ] **Step 7: Run focused tests**

Run:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoSystemDialogBrowserTest.*:DaoQrCodeResultDialogBrowserTest.*"
```

Expected: all focused tests pass.

- [ ] **Step 8: Commit checkpoint with explicit user authorization**

Only after the user explicitly authorizes git state changes, run:

```bash
git add src/dao/browser/ui/views/dao_qr_code_result_dialog_view.cc
git commit -m "feat(qr): adopt dao system dialog buttons"
```

## Task 6: Opt Extension Install Dialog Into Dao Style

**Files:**
- Modify: `src/patches/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch`

- [ ] **Step 1: Add helper include to the existing patch**

Update `src/patches/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch` so the include section adds:

```diff
 #include "dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.h"
+#include "dao/browser/ui/views/dao_system_dialog.h"
```

- [ ] **Step 2: Configure the dialog in the constructor**

In the existing patch file, add this source change immediately after the upstream `SetButtonLabel(ui::mojom::DialogButton::kCancel, prompt_->GetAbortButtonLabel());` call:

```diff
   SetButtonLabel(ui::mojom::DialogButton::kOk, prompt_->GetAcceptButtonLabel());
   SetButtonLabel(ui::mojom::DialogButton::kCancel,
                  prompt_->GetAbortButtonLabel());
+  dao::ConfigureDaoSystemDialog(this);
   set_close_on_deactivate(false);
```

- [ ] **Step 3: Apply patches**

Run:

```bash
npm run import
```

Expected: extension install patch applies cleanly and `engine/src/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc` includes `dao_system_dialog.h`.

- [ ] **Step 4: Build focused target**

Run:

```bash
npm run build:debug -- --target browser_tests
```

Expected: `browser_tests` target compiles.

- [ ] **Step 5: Run focused tests**

Run:

```bash
engine/src/out/dao-debug/browser_tests --gtest_filter="DaoSystemDialogBrowserTest.*:DaoQrCodeResultDialogBrowserTest.*"
```

Expected: all focused tests pass.

- [ ] **Step 6: Commit checkpoint with explicit user authorization**

Only after the user explicitly authorizes git state changes, run:

```bash
git add src/patches/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch
git commit -m "feat(extensions): style install dialog actions"
```

## Task 7: Full Verification

**Files:**
- No planned file edits.

- [ ] **Step 1: Run project rebuild**

Run:

```bash
npm run rebuild
```

Expected: import and debug build complete successfully.

- [ ] **Step 2: Run Dao browser tests**

Run:

```bash
npm run test
```

Expected: `Dao*` browser tests pass, including `DaoSystemDialogBrowserTest.*` and `DaoQrCodeResultDialogBrowserTest.*`.

- [ ] **Step 3: Manual QA in debug app**

Run:

```bash
npm run start:debug
```

Manual checks:

- QR decode result dialog with one URL payload shows Dao-styled Copy and Open buttons with keycaps.
- QR decode result dialog with multiple payloads shows Dao-styled row buttons without ambiguous row keycaps.
- `Cmd+Shift+C` copies a single QR payload via the Copy button callback.
- `Cmd+O` opens a single URL QR payload.
- Extension install dialog shows Dao-styled OK/Cancel buttons with `Enter` and `Esc` badges.
- A representative non-opt-in Chromium dialog keeps its normal button style and has no Dao keycap labels.

- [ ] **Step 4: Inspect working tree**

Run:

```bash
git status --short
```

Expected: only the planned files from this plan are changed.

- [ ] **Step 5: Final commit checkpoint with explicit user authorization**

Only after the user explicitly authorizes git state changes, run:

```bash
git add src/patches/ui/views/window/dialog_delegate.h.patch src/patches/ui/views/window/dialog_delegate.cc.patch src/patches/ui/views/window/dialog_client_view.cc.patch src/patches/chrome/browser/ui/BUILD.gn.patch src/patches/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch src/dao/browser/ui/views/dao_system_dialog.h src/dao/browser/ui/views/dao_system_dialog.cc src/dao/browser/ui/views/dao_qr_code_result_dialog_view.cc src/dao/browser/ui/views/dao_browser_browsertest.cc
git commit -m "feat(dialog): add dao system dialog styling"
```

## Self-Review Notes

- Spec coverage:
  - Opt-in base layer: Tasks 2 and 3.
  - Dao helper: Task 4.
  - QR adoption: Task 5.
  - Extension install adoption: Task 6.
  - Shortcut badges and accelerator behavior: Tasks 1, 3, 4, 5.
  - Non-opt-in stability: Task 1 test `NonOptInDialogHasNoDaoShortcutBadges`.
  - Build/test verification: Task 7.
- Type consistency:
  - Public shortcut type is `views::DialogDelegate::DialogButtonShortcut`.
  - Dao helper shortcut type is `dao::DaoDialogShortcut`.
  - Public helper API is `dao::ConfigureDaoSystemDialog`, `dao::CreateDaoDialogButton`, and `dao::PlatformShortcutKeycap`.
- Git rule reconciliation:
  - Commit steps remain in the plan as checkpoints, but execution must skip them without latest-message authorization.
