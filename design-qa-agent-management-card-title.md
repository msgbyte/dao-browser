# Agent Management Card Title Design QA

**Source visual truth path**

`/var/folders/0l/4dc990md3yn_g3b46dtmhp880000gn/T/orca-paste-1786355815911-c1768617-d9d5-4b7c-bc02-17762b0964a4.png`

**Implementation screenshot paths**

- Memory and Workspace: `/var/folders/0l/4dc990md3yn_g3b46dtmhp880000gn/T/orca-computer-use/8319c5ab-6c27-406a-b73d-46545f9b40f0-screenshot.png`
- Usage: `/var/folders/0l/4dc990md3yn_g3b46dtmhp880000gn/T/orca-computer-use/4239e809-2f34-4949-be50-d3d161209a31-screenshot.png`

**Viewport and normalization**

- Source: 1496 x 1036 pixels, cropped to the Settings content region; density metadata is unavailable.
- Implementation: 2796 x 1818 pixels at 2x, representing a 1398 x 909 CSS-pixel Dao Debug window.
- State: light theme, `dao://settings/agent`, Data and management section with loaded metrics.
- The source is a content crop rather than a full browser viewport. The comparison therefore uses the visible card-header region for alignment and hierarchy, not pixel-perfect browser-frame geometry.

**Full-view comparison evidence**

The source and both implementation screenshots were opened together in one comparison input. The implementation preserves the independent Memory, Workspace, and Usage cards, existing row density, action surfaces, card spacing, borders, radii, and shadows. Browser chrome and sidebar are expected implementation-only context.

**Focused region comparison evidence**

Focused comparison was required because the requested change concerns the compact 52px card headers. Memory, Workspace, and Usage each show the title visually centered between the header boundaries. Each title uses the same 14px semibold hierarchy, 3px Dao-blue inline-start border, and 10px text inset. No title is clipped, wrapped, or offset toward the top edge.

**Required fidelity surfaces**

- Fonts and typography: the system font remains unchanged; the titles are consistently 14px, weight 650, and visually stronger than metric labels without competing with the section heading.
- Spacing and layout rhythm: vertical centering is corrected; the existing 52px header, card insets, 16px desktop gap, row heights, radii, and shadows remain visually stable.
- Colors and visual tokens: the accent border uses the existing Dao Settings blue; text and surfaces retain their existing light-theme tokens. Dark-theme behavior is token-backed but was not separately captured.
- Image quality and asset fidelity: no image, icon, logo, illustration, or generated asset is part of this change.
- Copy and content: Memory, Workspace, and Usage copy and all metrics/actions remain unchanged.

**Findings**

- No actionable P0, P1, or P2 mismatch remains for the requested card-title treatment.
- P3 verification gap: a separate dark-theme screenshot was not captured; the implementation reuses the existing accent and text tokens rather than adding theme-specific colors.

**Primary interactions checked**

- The page loads at `dao://settings/agent` in the freshly rebuilt Dao Debug app.
- Scrolling exposes all three management cards without changing their layout.
- Existing action rows remain visible and aligned; no action was invoked because this change is presentation-only.

**Comparison history**

- Iteration 1: the source showed titles sitting too close to the top of their headers and lacking a card identity marker.
- Fix: shared header alignment changed to vertical center; shared titles gained 14px semibold text and a Dao-blue inline-start border.
- Post-fix evidence: the Memory and Workspace screenshot confirms the shared treatment in adjacent cards; the Usage screenshot confirms the third card receives the same treatment.

**Implementation checklist**

- [x] Vertically center all three titles in the existing 52px headers.
- [x] Preserve horizontal left alignment and content inset.
- [x] Apply the same 3px accent border, 14px type, 650 weight, and 10px inset to all three titles.
- [x] Preserve card content, actions, semantics, responsive spacing, and localization.
- [x] Import and rebuild successfully through the approved project commands.
- [x] Compare the supplied screenshot and freshly rendered implementation together.

final result: passed
