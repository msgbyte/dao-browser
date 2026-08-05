# Android Design QA

## Sources

- Open Design HTML authority: `newtab.html`, `browsing.html`, `settings.html`, `history.html`, `bookmarks.html`, `downloads.html`, and `extensions.html`.
- Tokens and behavior: `DESIGN-TOKENS.md`, `HANDOFF.md`, and `ACCEPTANCE.md`.
- Exported visual reference: `image.png`.
- Desktop brand authority: the borderless `branding/dao_logo.png` source artwork.
- Runtime captures: API 34 Pixel 6 emulator at 1080 x 2400 in light and dark themes.

## Comparison

The exported new-tab reference and the emulator capture were normalized to the same canvas and inspected side by side. The Android implementation keeps the neutral palette, typography hierarchy, mark, greeting, pill search field, spacing rhythm, and icon treatment. Android system bars replace the prototype device frame as required.

The exported reference contains an overlap between the search pill and greeting. The implementation follows the newer acceptance requirement instead: the greeting and search field have clear separation at the target viewport.

The borderless desktop artwork was rendered beside the API 34 launcher and new-tab captures. The Android adaptive icon keeps the complete ink-circle Dao mark inside the system safe zone, and the new-tab page uses the transparent original without cropping, a macOS frame, or a replacement text glyph.

## Findings and fixes

- P0: none.
- P1 fixed: new-tab greeting/search overlap.
- P1 fixed: focusing the editable text field now activates the animated search state.
- P1 fixed: browsing drawer order now matches the acceptance list exactly.
- P1 fixed: dark-theme status and navigation bar icons now use a readable light appearance after launch and focus changes.
- P2 fixed: Material 3 download progress stop indicator removed to match the simple prototype bar.
- P2 fixed: localized resource collision for the active-download section label removed.
- P2 fixed: placeholder `N` tile and macOS-specific framed icon replaced by the canonical borderless Dao artwork in both launcher and in-app surfaces.
- P2 fixed: scanner entry uses the requested Lucide `ScanLine` glyph instead of the generic `Scan` glyph.

## Interaction checks

- Search field docks to the top and exposes recent or filtered suggestions.
- Query matching is case-insensitive, limited to five results, and highlights the matching text.
- Scanner overlay opens, animates, and closes.
- Address submission loads a real GeckoView page.
- Right drawer opens from the address bar and closes through the scrim or back action.
- History search, bookmark segment, download pause, and extension switches update locally.
- Dark mode updates immediately and persists through Preferences DataStore across force-stop and relaunch.
- Launcher and new-tab branding remain legible at their rendered sizes without clipping the ink-circle mark.

## Tab-count refinement — 2026-08-04

- Source visual truth: `/var/folders/0l/4dc990md3yn_g3b46dtmhp880000gn/T/orca-paste-1785776258694-95491411-83d3-4a0b-a16d-d41a4793d627.png` plus the explicit instruction to remove the count from expanded search and use a tighter Chrome-style frame elsewhere.
- Implementation captures: `/tmp/dao-tab-count-home.png`, `/tmp/dao-tab-count-search.png`, and `/tmp/dao-tab-count-browser.png`.
- Full-view comparison: `/tmp/dao-tab-count-comparison.png`.
- Focused comparison: `/tmp/dao-tab-count-focused-comparison.png`; left shows the reported oversized search-state control, center shows the count removed from expanded search, and right shows the compact browsing-state frame.
- Viewport: Pixel 6 API 34 emulator, 1080 × 2400 physical pixels. The 848 × 1788 framed source was cropped to its 761 × 1687 app screen; the implementation was normalized to 761 × 1687 for the full-view comparison. Native Compose density was preserved in the runtime captures.
- State: light theme, expanded search with `http://www.google.cn/m`; idle new tab; browsing the submitted URL.

### Findings and comparison history

- P1 fixed: the tab-count control no longer appears in expanded search, leaving only clear and exit actions.
- P2 fixed: the visible frame no longer fills its 32/36 dp touch target. It is a centered 24 dp rounded square with a 2 dp foreground border, while the larger accessible hit area remains intact.
- Post-fix evidence: the focused comparison shows no count between the two search actions and a compact framed count in the browsing address bar. No actionable P0/P1/P2 differences remain.

### Required fidelity surfaces

- Fonts and typography: the existing system font, semibold weight, and centered numeric label remain consistent; the number was reduced to 11 sp to fit the tighter frame without crowding.
- Spacing and layout rhythm: the visible frame is close to the numeral while the original touch-target spacing is preserved; removing the search-state frame and divider gives the editing actions an even rhythm.
- Colors and visual tokens: the frame and numeral use the existing Nova foreground token for Chrome-like contrast in both themes.
- Image quality and asset fidelity: no raster or icon assets were added or replaced; the control remains native Compose UI.
- Copy and content: no user-visible strings changed, and the live `BrowserStore` count remains the displayed value outside expanded search.

final result: passed

## Search cursor alignment and empty close behavior — 2026-08-04

- Source visual truth: `/var/folders/0l/4dc990md3yn_g3b46dtmhp880000gn/T/orca-paste-1785825111072-1e131078-c517-42cf-811f-4f06c420b9da.png`.
- Implementation screenshot: `/tmp/dao-search-cursor-final.png`.
- Side-by-side comparison: `/tmp/dao-search-reference-vs-final.png`; source is on the left and the revised emulator capture is on the right.
- Viewport: Pixel 6 API 34 emulator at 1080 × 2400 physical pixels and 420 dpi. The 940 × 220 implementation crop was normalized to the 744 × 174 source pixels for comparison.
- State: light theme, empty expanded search, focused real input, visible cursor and clear action.
- Full-view evidence: `/tmp/dao-search-alignment-after.png` confirms the expanded search layout, suggestions region, keyboard, and safe-area placement.
- Focused evidence: `/tmp/dao-search-reference-vs-final.png` confirms the cursor and placeholder share the same centered 16 sp text metrics. A focused region was required because the reported defect was not legible at full-view scale.

### Findings and comparison history

- P2 fixed: the placeholder and editable text previously used separate unconstrained decoration children, which made the focused cursor appear vertically disconnected from the placeholder. Both now share one center-start container and the same 16 sp font size and line height.
- P1 fixed: the clear action previously remained a no-op when the expanded query was already empty. It now exits expanded search and restores the homepage; address editing still uses its supplied exit callback.
- First iteration: a shared 20 sp line height aligned the layout structure but made the cursor visibly taller than the reference.
- Final iteration: reducing the shared line height to 16 sp matched the visible cursor/text height while keeping their baseline and origin unified. No actionable P0/P1/P2 differences remain.

### Required fidelity surfaces

- Fonts and typography: system font, 16 sp size, 16 sp line height, weight, and placeholder color are shared between the editable text and placeholder.
- Spacing and layout rhythm: search icon, text origin, clear action, 56 dp pill height, border, and outer padding remain unchanged; only the internal text layout was unified.
- Colors and visual tokens: foreground, faint placeholder, border, and surface continue using the existing Nova theme tokens.
- Image quality and asset fidelity: no image or icon assets changed; the existing Lucide search and close icons remain intact.
- Copy and content: localized placeholder and accessibility labels are unchanged.
- Primary interactions: text entry, non-empty clear, empty close-to-home, keyboard focus, and homepage tab-count restoration were exercised on the emulator.
- Runtime errors: no app fatal exception was observed during the capture and interaction pass.

final result: passed
