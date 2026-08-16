# Mini Dao Site Center Design

## Summary

Add a Mini Dao site center entry inside the right edge of the existing Mini Dao
URL pill. The site center is scoped to the current page and exposes extension
actions plus site-level controls without turning Mini Dao into a full browser
toolbar.

This design is separate from the normal-window `Mini Dao` extraction action.
Normal Dao windows may offer "extract current tab to Mini Dao"; Mini Dao windows
must not show that action because they are already Mini Dao.

## Goals

- Add a site center icon inside `DaoLittleDaoView`'s URL display pill.
- Open a compact site center popover anchored to that icon.
- Include extension actions for the current Mini Dao page.
- Include site controls such as Page Info / permissions, Share, QR Code, and
  more site actions.
- Preserve the calm Mini Dao top bar: hostname pill, site center affordance, and
  `Open in Dao`.
- Reuse existing Control Center, extension action, Page Info, Share, QR, and
  color infrastructure where practical.

## Non-Goals

- Do not add a Mini Dao extraction action inside Mini Dao.
- Do not duplicate the normal Dao address bar in Mini Dao.
- Do not add sidebar tabs, navigation buttons, chat controls, or pinned
  extension icons directly to the Mini Dao top bar.
- Do not create a second extension execution path that bypasses Chromium's
  `ExtensionActionRunner`.
- Do not hardcode user-visible copy outside Dao's localization system.

## User Experience

The Mini Dao top bar remains 48 px tall. The URL pill changes from a single
hostname button to a compound control:

```text
[ hostname / path                              | site center icon ] [ Open in Dao ]
```

Clicking the hostname area keeps the existing behavior: it opens the Dao command
bar for navigation.

Clicking the site center icon opens a popover anchored to the URL pill's right
edge. The popover contains:

- Site header: current domain plus security status / site settings affordance.
- Extension actions for the active page.
- Site permissions / Page Info.
- Share and QR Code actions for the current URL.
- More site actions, such as clearing site data, if already supported by the
  existing Dao Control Center menu.

`Open in Dao` remains the only window-conversion action in Mini Dao. Once the
page is moved back to a normal Dao browser window, the normal Control Center may
again offer the separate `Mini Dao` extraction action.

## Recommended Technical Approach

Introduce a Mini Dao-specific site center surface while reusing the existing
Control Center internals:

```text
DaoLittleDaoView
  URL pill
    hostname button area -> ShowCommandBar()
    site center icon     -> ShowMiniDaoSiteCenter()

DaoMiniDaoSiteCenterPopup
  extensions section
  site utility section
  QR subview / more subview as needed
```

### Top Bar

Update `DaoLittleDaoView` to make the URL display a small container instead of a
single `LabelButton`. The left text area opens the command bar, while the right
icon button opens the site center.

The icon should be a Lucide icon already present in `DaoLucideIcons` if
available. Good first choices are a shield, sliders, or panel-style icon. If a
new Lucide icon is needed, fetch the upstream SVG child nodes verbatim and add
it through `DaoLucideIcons`.

Expose `site_center_button_bounds()` for macOS titlebar hit testing, matching
the existing `url_display_bounds()` and `open_in_dao_button_bounds()` pattern.

### Popup

Create `DaoMiniDaoSiteCenterPopup` rather than using the normal
`DaoControlCenterPopup` directly. The normal Control Center includes the
normal-window `Mini Dao` extraction utility, which must not appear in Mini Dao.

The new popup can share smaller components or helper functions with the normal
Control Center:

- Extension grid/action execution from `DaoControlCenterExtensionsSection`.
- Page Info anchoring logic from `DaoControlCenterUtilitySection`.
- QR view implementation from `DaoControlCenterQrView`, if it can be reused
  cleanly.
- More menu actions from `DaoControlCenterMoreMenu`, excluding actions that do
  not make sense in Mini Dao.

If sharing the current concrete view classes requires awkward flags, extract
small reusable helpers first, for example:

- `dao_extension_action_util.{h,cc}` for running extension actions and showing
  extension popups.
- `dao_page_info_util.{h,cc}` for showing Page Info at a provided anchor rect.
- Optional utility row configuration for normal Control Center vs Mini Dao site
  center.

Keep the first implementation narrow. It is acceptable for the Mini Dao site
center to start with extensions, Page Info, Share, QR Code, and More while
leaving deeper per-site settings to Chromium's Page Info bubble.

## Data Flow

1. User clicks the site center icon inside the Mini Dao URL pill.
2. `DaoLittleDaoView` asks the `BrowserView` for the Mini Dao site center popup.
3. The popup is shown relative to the icon's bottom-right point.
4. The popup reads the active `WebContents` from the Mini Dao `Browser`.
5. Extension icons are resolved for the current tab id.
6. Clicking an extension uses Chromium's existing extension action runner.
7. Page Info uses the active `NavigationEntry` virtual URL and the icon's screen
   rect as its anchor.
8. QR and Share use the active page's visible URL and title.

## BrowserView Integration

Mini Dao `BrowserView` currently creates only `DaoLittleDaoView` and
`DaoCommandBarView`. Add the Mini Dao site center popup to the Little Dao branch
so it is available only for Little Dao windows.

The normal `DaoControlCenterPopup` remains created only for normal tabbed Dao
windows. This keeps the normal extraction action out of Mini Dao by construction.

Layout needs to position the Mini Dao site center popup as an overlay covering
the BrowserView, similar to the normal Control Center popup. The popup should
hide on outside click, active-tab change, page interaction, and window close.

## Edge Cases

- No active `WebContents`: show an empty/disabled state or do nothing.
- Internal pages such as `dao://` or `about:blank`: hide or disable site actions
  that do not apply, but keep the popover stable.
- No installed extensions: show only add/manage affordances if those are useful
  in Mini Dao; otherwise keep the section empty and compact.
- Extension popup requests: anchor to the site center icon or popover, not the
  absent normal address bar.
- Extension side panel requests: if side panels are unsupported in Mini Dao,
  ignore safely or open the page in Dao before toggling, depending on Chromium's
  existing behavior.
- Narrow Mini Dao windows: the hostname text should ellipsize before the site
  center icon or `Open in Dao` button disappears.
- Mini Dao transferred back to Dao: any open site center popup should close
  before `TransferToMainBrowser()` detaches the page.

## Testing Plan

Focused browser tests should cover:

- Little Dao windows create `DaoLittleDaoView`, `DaoCommandBarView`, and the Mini
  Dao site center popup, but not the normal `DaoControlCenterPopup`.
- The site center icon is present inside the URL pill and remains hit-testable in
  the macOS titlebar area.
- Clicking the hostname area still opens the command bar.
- Clicking the site center icon opens the Mini Dao site center popup.
- The Mini Dao site center does not contain the normal-window `Mini Dao`
  extraction action.
- Extension actions in Mini Dao run against the active Mini Dao `WebContents`.
- Page Info anchors near the site center icon.

For implementation verification, use focused browser tests first. If compile
confirmation is needed, run `npm run rebuild` per project rules.

## Decisions

- The site center icon lives inside the URL pill's right edge.
- Mini Dao does not show the normal-window Mini Dao extraction action.
- `Open in Dao` remains the only window-conversion control in Mini Dao.
- Build a Mini Dao-specific popup surface and share internals with the normal
  Control Center where that keeps the code clean.
