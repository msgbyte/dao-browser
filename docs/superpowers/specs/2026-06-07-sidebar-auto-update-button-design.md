# Sidebar Auto-Update Button Design

## Summary

Dao Browser should surface a compact auto-update action in the vertical sidebar after Sparkle has already downloaded and verified an update and is ready to install it on quit. The action appears in the bottom toolbar, aligned to the right next to the plus button. In its idle visual state it shows a Lucide `circle-arrow-up` icon; on hover it expands leftward into an `Update` pill. Clicking it directly applies the ready update through Sparkle's immediate installation handler.

The existing Downloads button remains unchanged. File downloads and application updates stay visually and behaviorally separate.

## Goals

- Show an update affordance only when an update is already ready to install.
- Keep the default sidebar bottom toolbar compact.
- Align the update action near the plus button on the right, while keeping the plus button fixed at the far right.
- Use a Lucide `circle-arrow-up` icon copied from the current upstream SVG when implemented.
- Let one click apply the update without an intermediate confirmation prompt.
- Keep all open sidebar WebUIs synchronized from one process-level updater state.

## Non-Goals

- Do not show update checking, download, or verification progress in the sidebar.
- Do not replace Sparkle's updater, appcast validation, signing validation, or install implementation.
- Do not reuse the regular Downloads button for app updates.
- Do not introduce a generic notification center or toast system for updates in this design.

## Existing Context

Dao already initializes Sparkle through `DaoUpdaterService` after the initial profile is ready. `DaoSparkleUpdaterMac` owns the `SPUStandardUpdaterController`, starts scheduled background checks, and exposes a user-initiated check path for menu/settings actions.

The sidebar is a Lit WebUI hosted by `DaoSidebarView`. Its bottom toolbar is currently rendered by `dao-download-button`, with the download trigger on the left and a slotted plus menu on the right. `DaoSidebarUIHandler` already pushes native download state into the WebUI through `FireWebUIListener`, making it the right bridge for update button state too.

Sparkle 2 exposes `SPUUpdaterDelegate`:

```objc
- (BOOL)updater:(SPUUpdater *)updater
       willInstallUpdateOnQuit:(SUAppcastItem *)item
    immediateInstallationBlock:(void (^)(void))immediateInstallHandler;
```

This callback is the right boundary for Dao's UI. It means the update has reached the "ready to install on quit" phase, and Sparkle provides a block that can immediately install instead of waiting for the next quit.

## Architecture

`DaoSparkleUpdaterMac` becomes the Sparkle delegate owner for update-ready state. It captures `willInstallUpdateOnQuit`, stores the `immediateInstallHandler`, extracts user-facing update metadata such as `displayVersionString`, and reports the ready state upward.

`DaoUpdaterService` becomes the process-wide state source. It exposes:

- a small update state model, such as `idle`, `ready`, `applying`, and `unsupported`;
- observer registration for UI controllers;
- `GetUpdateState()` for late subscribers;
- `ApplyReadyUpdate()` for the sidebar click path.

`DaoSidebarUIHandler` subscribes to `DaoUpdaterService`. It registers new WebUI messages for requesting update state and applying a ready update, then pushes state through an `updateStateChanged` listener.

The sidebar WebUI adds `dao-update-button`, rendered by `dao-sidebar-app` inside the existing `dao-download-button` toolbar slot. It is placed to the left of the plus button in a right-aligned action group.

## UI Behavior

The button is hidden unless the update state is `ready` or `applying`.

In `ready`:

- The button is a 26px blue icon button.
- The icon is Lucide `circle-arrow-up`.
- The tooltip and expanded label are `Update`.
- Hover expands the button leftward into an `Update` pill.
- The plus button remains anchored at the far right and does not move.

In `applying`:

- The button is disabled.
- The expanded label may briefly read `Applying`.
- Duplicate clicks are ignored.
- Sparkle takes over the install/relaunch flow.

The button should use existing sidebar color tokens where available: Dao accent blue for the ready action, the existing 8px toolbar radius, and the same 26px baseline dimensions as the Downloads and plus controls.

## Data Flow

1. `DaoUpdaterService::Init()` creates and starts `DaoSparkleUpdaterMac`.
2. Sparkle performs scheduled checks and silent downloads using the existing `Info.plist` configuration.
3. When an update is ready to install on quit, Sparkle calls `willInstallUpdateOnQuit`.
4. `DaoSparkleUpdaterMac` captures the immediate installation block and update metadata.
5. `DaoUpdaterService` updates its process-wide state to `ready` and notifies observers.
6. Each `DaoSidebarUIHandler` pushes `updateStateChanged` to its WebUI with `{state: "ready", displayVersion}`.
7. `dao-sidebar-app` stores the state and renders `dao-update-button`.
8. User clicks `dao-update-button`.
9. WebUI sends `applyReadyUpdate`.
10. `DaoSidebarUIHandler` calls `DaoUpdaterService::ApplyReadyUpdate()`.
11. The service atomically transitions to `applying`, clears or consumes the stored block, and invokes Sparkle's immediate install handler.
12. All sidebar windows receive the new state and disable or hide the button.

## Error Handling

- `ApplyReadyUpdate()` with no ready update is a no-op and broadcasts `idle`.
- The stored Sparkle block may only be consumed once.
- If multiple windows click at the same time, the first call wins and later calls see `applying` or `idle`.
- If the WebUI loads after an update is already ready, it requests current updater state and renders correctly.
- If Sparkle ends the update session, cancels the ready install, or invalidates the install handler, Dao clears the ready state and broadcasts `idle`.
- On unsupported platforms, the service reports `unsupported` or `idle`, and the WebUI renders nothing.
- If immediate install does not visibly relaunch right away, the UI remains disabled while the state is `applying`; Sparkle remains the source of truth for the actual install flow.

## Internationalization

The visible label is `Update`; the applying label is `Applying` if shown. These strings must follow Dao's WebUI localization path instead of becoming permanent hardcoded UI text. If the sidebar currently lacks a dedicated i18n locale layer, the implementation plan should include the smallest project-consistent string plumbing needed for this component.

## Testing

Updater service tests should cover:

- ready callback changes state to `ready`;
- metadata such as `displayVersion` is retained;
- `ApplyReadyUpdate()` invokes the block exactly once;
- duplicate apply calls do not re-invoke the block;
- observers receive ready and applying/idle transitions;
- late subscribers can read the current state;
- unsupported builds never surface a ready state.

Sidebar WebUI tests should cover:

- `updateStateChanged` with `ready` renders `dao-update-button`;
- `idle` removes the button and leaves no toolbar gap;
- the button is right-aligned next to the plus button;
- hover expansion keeps the plus button anchored at the far right;
- clicking sends `applyReadyUpdate` and disables the button;
- the icon uses the Lucide `circle-arrow-up` SVG child nodes.

Integration/manual verification should cover:

- forcing a fake ready update state shows the button in one window;
- opening another window while ready shows the same state;
- clicking one window disables or hides the button in all windows;
- the regular Downloads button behavior is unchanged.

For verification after implementation:

- WebUI-only behavior: run `npm run test:webui` and `npm run lint:lit` when relevant.
- C++/Sparkle changes: run `npm run rebuild` for compile confirmation.

## Implementation Notes

- Keep canonical changes in `src/dao/` and required Chromium integration in `src/patches/`; do not edit `engine/` directly.
- The implementation should not run Chromium build tools directly. Compile confirmation must use `npm run rebuild`.
- The Lucide icon must be copied from the current upstream `circle-arrow-up` SVG child nodes during implementation, not recreated from memory.
- The existing `DaoLucideIcons` C++ registry already includes `kRotateCw`; this design intentionally uses `circle-arrow-up` for the WebUI button because the selected UI meaning is "upgrade available".
