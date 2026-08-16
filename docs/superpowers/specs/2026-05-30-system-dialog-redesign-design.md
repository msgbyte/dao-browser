# System Dialog Redesign - Design

Date: 2026-05-30
Status: Approved concept, pending written-spec review

## Summary

Dao Browser will redesign decision dialogs around a foundational, opt-in dialog
style in Chromium's Views layer plus a Dao helper for Dao-owned actions. The
visual direction is the Dao-themed frosted system surface selected during
brainstorming: a dark-first, elevated surface with strong hierarchy, rounded
geometry, high-contrast primary actions, tonal secondary actions, and visible
keyboard shortcut badges inside action buttons.

The core implementation belongs at the `DialogDelegate` / `DialogClientView`
boundary, not in each dialog one by one. However, it must be opt-in. Unscoped
global changes to `MdTextButton`, `LayoutProvider`, or all `BubbleDialogDelegate`
instances would alter many unrelated Chromium surfaces such as payment,
profile, WebAuthn, bookmark, download, permission, and menu UI. Dao system
dialogs get the new treatment only after their delegate explicitly enables it.

## Decisions

- Scope: first phase covers decision dialogs only. Control Center, toast,
  download flyout, Agent WebUI, and Sparkle update sheets are not changed.
- Coverage: Dao-owned decision dialogs plus key patched Chromium dialogs can
  opt in. The first targets are the QR-code result dialog and the extension
  install dialog path already patched for Dao's MV2 notice.
- Foundation: add opt-in support to `views::DialogDelegate` and
  `views::DialogClientView`; add a Dao helper in `src/dao/browser/ui/views/`
  for Dao-owned dialogs and custom action buttons.
- Shortcuts: standard dialog buttons can show and respond to shortcut badges.
  Custom Dao action buttons can declare their own accelerator and badge.
- Accessibility: shortcut badge text is visual metadata. Accessible names stay
  focused on the action label, not the shortcut.
- Patch discipline: source-of-truth changes to Chromium files live under
  `src/patches/...`; generated `engine/src/...` files are not hand-authored as
  the long-term source of truth.

## Goals

- Give Dao decision dialogs a cohesive system-level style close to the provided
  reference image while staying compatible with Dao's existing theme language.
- Support visible shortcut badges in standard dialog buttons, such as `Esc`
  for cancel/close and `Enter` for the default action.
- Support declared shortcuts for custom dialog actions, such as `Cmd+C` for a
  Copy action and `Cmd+O` for an Open action.
- Keep business logic single-sourced: mouse clicks and keyboard accelerators
  invoke the same action callback.
- Make future Dao dialogs cheap to adopt by calling one helper instead of
  rebuilding style and shortcut plumbing in every dialog.
- Keep non-Dao Chromium UI stable unless a patched dialog explicitly opts in.

## Non-goals

- No global redesign of all Chromium dialogs, bubbles, menu buttons, or all
  `MdTextButton` usages.
- No replacement of Sparkle's standard macOS update UI in this phase.
- No changes to Control Center popup, toast, download flyout, command bar, or
  other non-blocking Dao surfaces.
- No edits to Agent WebUI vendor-generated files.
- No broad rewrite of Chromium's color system or typography provider.
- No attempt to make every upstream permission, payment, WebAuthn, profile, or
  bookmark dialog use Dao styling in the first phase.

## Current State

Relevant existing files:

| Area | Current owner |
|---|---|
| Standard Views dialog buttons | `ui/views/window/dialog_client_view.cc` |
| Dialog button labels, default button, accept/cancel semantics | `ui/views/window/dialog_delegate.{h,cc}` |
| Bubble-backed dialog frame creation | `ui/views/bubble/bubble_dialog_delegate_view.cc` and `ui/views/bubble/bubble_frame_view.cc` |
| Generic Material text button painting | `ui/views/controls/button/md_text_button.{h,cc}` |
| Dialog spacing tokens | `ui/views/layout/layout_provider.cc` and `chrome/browser/ui/views/chrome_layout_provider.cc` |
| Dao QR dialog | `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.{h,cc}` |
| Dao extension install patch | `src/patches/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch` |
| Dao colors for owned Views UI | `src/dao/browser/ui/views/dao_colors.{h,cc}` |

`DialogClientView` is the best base hook because it is where OK/Cancel
`MdTextButton` instances are created and updated. It already registers `Esc`,
delegates button clicks to `AcceptDialog()` / `CancelDialog()`, and observes
model changes from `DialogDelegate`.

## Architecture

### Layer 1: Views opt-in foundation

Add a Dao-specific opt-in configuration to `views::DialogDelegate::Params` and
public setters/getters on `DialogDelegate`.

The foundation lives in `ui/views` so standard OK/Cancel buttons and
`BubbleDialogDelegateView` clients can share it. Because `ui/views` is a base
Chromium target, it must not include Dao-owned headers such as
`dao_colors.h`. Dao-specific colors used in this base patch are local constants
or derived from `ui::NativeTheme` and `ui::ColorProvider`.

Proposed delegate data:

```cpp
struct DialogButtonShortcut {
  ui::Accelerator accelerator;
  std::u16string keycap;
};

bool use_dao_system_dialog_style = false;
std::array<std::optional<DialogButtonShortcut>, kDialogButtonCount>
    button_shortcuts;
```

Proposed delegate API:

```cpp
void SetUseDaoSystemDialogStyle(bool use_style);
bool use_dao_system_dialog_style() const;

void SetButtonShortcut(ui::mojom::DialogButton button,
                       const ui::Accelerator& accelerator,
                       std::u16string_view keycap);
std::optional<DialogButtonShortcut> GetButtonShortcut(
    ui::mojom::DialogButton button) const;
```

`DialogClientView` reads this opt-in when it creates or updates standard
buttons:

- Use Dao button padding, corner radius, background, and stroke colors.
- Render the shortcut badge inside the button without appending it to the
  accessible name.
- Register the declared accelerator at dialog-client scope.
- Trigger the same OK/Cancel path used by mouse clicks, respecting disabled
  buttons and input protection.
- Preserve existing default behavior for non-opt-in dialogs.

### Layer 2: Dao helper

Add a Dao-owned helper under `src/dao/browser/ui/views/`, for example:

```cpp
// dao_system_dialog.h
namespace dao {

struct DaoDialogShortcut {
  ui::Accelerator accelerator;
  std::u16string keycap;
};

struct DaoSystemDialogOptions {
  bool show_enter_for_default = true;
  bool show_esc_for_cancel = true;
};

void ConfigureDaoSystemDialog(views::DialogDelegate* delegate,
                              const DaoSystemDialogOptions& options = {});

std::unique_ptr<views::MdTextButton> CreateDaoDialogButton(
    views::Button::PressedCallback callback,
    std::u16string_view label,
    std::optional<DaoDialogShortcut> shortcut,
    ui::ButtonStyle style);

}  // namespace dao
```

The helper owns Dao-level conventions:

- Standard `Enter` and `Esc` shortcut setup for opt-in dialogs.
- Dao button colors and badge metrics for Dao-owned custom actions.
- Custom accelerator registration for non-standard buttons, such as QR result
  row actions.
- A single place for future dialog icon/header helpers if Dao adds richer
  decision dialogs later.

The helper can include `dao_colors.h` because it lives in Dao-owned code. The
base Views patch cannot.

### Layer 3: Target dialog adoption

First phase adoption is explicit and narrow:

- `DaoQrCodeResultDialogView`
  - Calls `dao::ConfigureDaoSystemDialog(this)`.
  - Uses helper-created Copy/Open buttons for row actions.
  - Declares shortcuts such as `Cmd+C` for Copy and `Cmd+O` for Open when they
    do not conflict with selectable text focus.
  - Keeps payload labels selectable and preserves the existing URL open flow.

- Extension install dialog patch
  - Adds the Dao MV2 notice as it does today.
  - Opts the dialog into Dao system style only in Dao's patched install path.
  - Leaves the permission list structure and upstream install logic intact.
  - Standard OK/Cancel buttons receive Dao styling and keycap badges through
    `DialogClientView`.

Future Dao decision dialogs should use the helper from the start rather than
manually constructing button rows.

## Visual Design

The selected direction is "Dao theme integrated":

- Surface: rounded elevated dialog, approximately 16-18 px radius, with a
  translucent dark blue-gray fill in dark mode and a light frosted counterpart
  in light mode.
- Border: subtle one-pixel outline to separate the dialog from dimmed or
  content-backed surroundings.
- Shadow: keep the native or BubbleFrameView shadow where possible; do not
  add a second heavy shadow unless the frame path needs it.
- Typography: system UI fonts; larger semibold title; body text uses secondary
  hierarchy.
- Primary action: high-contrast light button in dark mode, with dark text.
- Secondary action: tonal dark button in dark mode, lower contrast than the
  primary action.
- Keycap: compact rounded badge at the trailing edge of the button label,
  visually disabled when its action is disabled.
- Accessibility: screen reader label is the action name. Shortcut information
  may be exposed through tooltip/description if useful, but must not replace
  the action name.

The first phase does not require app icons on every dialog. Dao-owned dialogs
may add an icon/header when it helps hierarchy; patched Chromium dialogs should
avoid heavy layout changes.

## Shortcut Behavior

Standard buttons:

- `Enter` triggers the default dialog button when the opt-in dialog declares
  it and when the focused control does not already consume the key.
- `Esc` triggers cancel/close through the existing `DialogClientView`
  cancellation path.
- Disabled buttons do not fire from shortcuts.
- Shortcut badges update when button visibility, enabled state, label, or style
  changes.

Custom Dao action buttons:

- The helper-created button registers its declared accelerator on the relevant
  dialog/root view.
- The accelerator invokes the same callback as a click.
- If a text field, selectable text, or other child view needs the shortcut,
  the dialog does not declare that shortcut.
- Shortcuts are platform-aware in display text. On macOS, Dao can display
  `Cmd+C`; on non-mac platforms it can display `Ctrl+C` if the action is
  enabled there.

Conflict rules:

- Child views with higher-priority accelerators keep priority.
- Dialog-level accelerators are only for actions that remain sensible across
  the whole dialog.
- No shortcut is declared if it would surprise users while typing into a field.

## Data Flow

Standard OK/Cancel example:

```text
Dialog constructor
  -> dao::ConfigureDaoSystemDialog(delegate)
     -> delegate->SetUseDaoSystemDialogStyle(true)
     -> delegate->SetButtonShortcut(kOk, Enter, "Enter")
     -> delegate->SetButtonShortcut(kCancel, Esc, "Esc")

Widget creation
  -> DialogDelegate::CreateClientView()
  -> DialogClientView::UpdateDialogButton()
     -> creates Dao-styled button if opt-in
     -> applies visual keycap
     -> registers accelerator

User presses shortcut or clicks button
  -> DialogClientView::ButtonPressed()
  -> delegate->AcceptDialog() or delegate->CancelDialog()
```

Custom QR row action example:

```text
DaoQrCodeResultDialogView::BuildContents()
  -> dao::CreateDaoDialogButton(OnCopy, "Copy", Cmd+C, kTonal)
  -> button renders "Copy" plus keycap badge
  -> button registers accelerator

User presses Cmd+C or clicks Copy
  -> same OnCopy callback
  -> clipboard write
```

## Patch And File Plan

Expected source-of-truth patch files:

| Patch | Purpose |
|---|---|
| `src/patches/ui/views/window/dialog_delegate.h.patch` | Add opt-in fields and public API declarations. |
| `src/patches/ui/views/window/dialog_delegate.cc.patch` | Implement opt-in setters/getters and default shortcut storage. |
| `src/patches/ui/views/window/dialog_client_view.cc.patch` | Apply Dao-styled standard buttons, keycap rendering, and accelerator dispatch for opt-in dialogs. |
| Optional `src/patches/ui/views/controls/button/md_text_button.*.patch` | Avoid by default. Use only if `DialogClientView` cannot render a trailing badge cleanly around the existing button contents. Any `MdTextButton` change must be an inert opt-in property with no default visual or accessibility change. |
| `src/patches/chrome/browser/ui/views/extensions/extension_install_dialog_view.cc.patch` | Opt extension install dialog into Dao system dialog style and keep the MV2 notice. |

Expected Dao-owned files:

| File | Purpose |
|---|---|
| `src/dao/browser/ui/views/dao_system_dialog.h` | Public helper API for Dao dialog styling, shortcuts, and action buttons. |
| `src/dao/browser/ui/views/dao_system_dialog.cc` | Helper implementation and Dao-owned custom button logic. |
| `src/dao/browser/ui/views/dao_qr_code_result_dialog_view.cc` | Adopt the helper and replace ad hoc row buttons. |
| `src/dao/browser/ui/views/dao_browser_browsertest.cc` | Browser tests for opt-in behavior and QR dialog adoption. |
| `src/patches/chrome/browser/ui/BUILD.gn.patch` | Add `dao_system_dialog.{h,cc}` to the existing Dao Views sources wired into `chrome/browser/ui`. |

The implementation requirement is that the helper remains part of the existing
Dao Views source wiring in `chrome/browser/ui`. The base `ui/views` patches
must not depend on the Dao helper or any Dao-owned target.

## Testing

Browser tests should cover:

- An opt-in `DialogDelegate` creates OK/Cancel buttons with Dao style enabled.
- Non-opt-in dialogs still create normal `MdTextButton` instances and do not
  display keycap badges.
- `Esc` cancels an opt-in dialog through the existing cancel path.
- `Enter` accepts an opt-in dialog through the existing accept path when the OK
  button is enabled.
- Disabled standard buttons ignore their shortcut.
- Keycap text does not replace or pollute the accessible button name.
- A helper-created custom action button invokes the same callback via click and
  accelerator.
- `DaoQrCodeResultDialogView` builds Copy/Open buttons through the helper and
  preserves selectable payload text.

Manual verification should include:

- Dark and light system appearance.
- A real QR-code result dialog with one URL payload and one text payload.
- Extension install dialog with a legacy MV2 extension.
- A representative non-opt-in Chromium dialog, confirming it remains visually
  unchanged.

Build verification for implementation remains the project-standard command:
`npm run rebuild`. Direct `ninja`, `autoninja`, or `siso` commands must not be
used.

## Risks And Mitigations

| Risk | Mitigation |
|---|---|
| Global UI regression from base-layer changes | Opt-in flag defaults to false; tests verify non-opt-in behavior. |
| `ui/views` dependency cycle if it includes Dao-owned code | Base patch uses local constants/native theme only; Dao helper includes `dao_colors.h`. |
| Shortcut conflicts with text input/selectable content | Shortcuts are declarative and omitted where they would conflict; child accelerators keep priority. |
| Accessibility regression from visual keycap text | Keycap is visual metadata; accessible name remains label-only. |
| Extension install dialog layout churn | Only opt in standard buttons and keep permission list layout intact. |
| Patch maintenance burden across Chromium upgrades | Keep patches small, annotated with `Dao system dialog`, and centered on `DialogDelegate`/`DialogClientView`. |

## Future Work

- Apply the helper to additional Dao decision dialogs after the first phase
  proves stable.
- Consider a second phase for non-blocking surfaces: Control Center, toast, and
  download flyout.
- Consider replacing Sparkle's standard update UI only if Dao needs full custom
  update flows later.
- Add richer dialog header helpers for icon/title/body composition if multiple
  Dao dialogs need the same structure.
