# Sidebar Context Menu Shortcuts Design

## Goal

Show the existing Duplicate Tab, Copy Link, and Close Tab keyboard shortcuts in the native sidebar tab context menu.

## Design

`DaoSidebarUIHandler`, already the `ui::SimpleMenuModel::Delegate`, will implement `GetAcceleratorForCommandId()`. It will translate the three sidebar menu command IDs to their existing browser command IDs and ask the active browser's `AcceleratorProvider` for the canonical platform accelerator. Commands without an existing shortcut will return `false` and remain unchanged.

The menu labels remain localized strings; shortcut glyphs are rendered by the native menu rather than embedded in copy. This keeps display and actual browser shortcut registration aligned.

## Verification

A focused WebUI source-contract test will guard the three mappings. The canonical C++ source will then be imported and compiled with `npm run rebuild`.
