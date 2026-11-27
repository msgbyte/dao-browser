# Windows update window implementation plan

**Goal** Show the selected Dao halo window throughout an interactive Windows browser update.

**Design** [Windows update window](../specs/2026-11-28-windows-update-window-design.md)

Use the existing standalone installer host, English and Simplified Chinese installer resources and the authenticated update handoff. Keep the window responsive on its own message loop. Observe the first visible, registered Dao browser window after relaunch, including updates initiated by older versions.

- [x] Add a DPI-aware native progress window to the installer host, with an indeterminate ring, reduced-motion behavior and separate recovery actions for installation and relaunch.
- [x] Drive the window from the authenticated preparation, shutdown, installer and browser events. Keep cancellation, binary verification, safe process ownership and launch-only retry intact.
- [x] Extend the installer build inputs and bilingual resources. Exercise the native host with isolated fixtures, run the focused tests and compile the installer host through the supported rebuild target.
- [x] Update the desktop inventory, acceptance checklist and both source changelogs.

## Windows verification

```powershell
npm run rebuild -- -- --release --target dao_installer_ui --platform windows
npm run rebuild -- -- --release --target dao_update_window_test_host --platform windows
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/test_windows_update_window.ps1 -CapturePath .dao/installer-tests/update-window.png
```

The fixture runs the native host and NSIS update wrapper on a private desktop with a temporary registration, browser and installer payload. It checks the original browser handshake, cancellation, animation, a single accessibility event per tested installation transition, minimization, both retry paths and first-window detection for delayed launches. Release acceptance still includes a published signed update on a disposable installation and manual theme, text scaling and reduced-motion checks.
