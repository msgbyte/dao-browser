# Dao Browser — Design System

> Single source of truth for colors, typography, spacing, shape, motion, and iconography.
> All Dao UI — native Views (C++) and WebUI pages (Lit/CSS) — must follow this document.

> **Source of truth ordering:** When this document and the code disagree, `src/dao/browser/ui/views/dao_colors.h/.cc` and the WebUI `:root` blocks win — update this document instead of the code. The Design Language summary in `CLAUDE.md` is the short brief; this file is the long form.

---

## 1. Color

Dao runs in **two themes** that follow the OS appearance setting (not user-toggleable):

- **Light mode (default)** — pale blue-gray sidebar, dark text, black-tinted overlays.
- **Dark mode** — deep blue-gray sidebar, white text, white-tinted overlays.

Both themes share a single **blue accent** for active/interactive states. The chrome is monochromatic and recedes; the web page is the focal point.

### 1.1 Theme Resolution

Native Views call `dao::IsDarkMode()` (in `dao_colors.h`) which reads `ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme()`. Every Dao color is a **function**, not a constant — call it on every paint so theme changes apply live.

WebUI pages resolve themes via `@media (prefers-color-scheme: dark)` overrides on `:root`.

### 1.2 Core Palette

| Role | Light mode | Dark mode | C++ Function |
|------|------------|-----------|--------------|
| Sidebar / frame background | `rgb(231, 238, 245)` | `rgb(54, 59, 64)` | `SidebarBackground()` / `FrameColor()` |
| Surface (active tab, address bar) | `rgba(0, 0, 0, 0.08)` (≈20/255) | `rgba(255, 255, 255, 0.08)` | `ActiveTabBackground()` |
| Surface low (URL pill) | `rgba(0, 0, 0, 0.06)` (≈15/255) | `rgba(255, 255, 255, 0.06)` | `AddressBarBackground()` |
| Separator / border | `rgba(0, 0, 0, 0.08)` | `rgba(255, 255, 255, 0.08)` | `SeparatorColor()` (alias of surface) |
| Accent (shared) | `rgb(70, 120, 190)` | `rgb(70, 120, 190)` | `SpaceActive()` |

The accent is the **only chromatic color** in the chrome. Backgrounds and text are pure neutrals tinted by alpha.

### 1.3 Text Hierarchy

The base ink is `rgb(30, 20, 40)` in light mode and `rgb(245, 245, 245)` in dark mode. Hierarchy is expressed by alpha against that base:

| Level | Light mode | Dark mode | C++ Function |
|-------|------------|-----------|--------------|
| Primary | `rgb(30, 20, 40)` solid | `rgb(245, 245, 245)` solid | `TextPrimary()` |
| Secondary | 60% of base ink (153/255) | 60% white (153/255) | `TextSecondary()` |
| Muted / tertiary | 40% of base ink (102/255) | 40% white (102/255) | `TextMuted()` |

> **Rule:** Never mix hierarchy levels in the same visual group. Pick one level per row/section.

### 1.4 Command Bar (Spotlight)

The command bar is a translucent floating panel over a scrim — both must adapt by theme.

| Purpose | Light mode | Dark mode | C++ Function |
|---------|------------|-----------|--------------|
| Backdrop scrim | `rgba(0, 0, 0, 0.31)` (80/255) | `rgba(0, 0, 0, 0.47)` (120/255) | `CommandBarScrim()` |
| Panel background | `rgba(255, 255, 255, 0.73)` (186/255) | `rgba(72, 78, 84, 0.82)` (210/255) | `CommandBarBackground()` |
| Panel border | `rgba(0, 0, 0, 0.16)` (40/255) | `rgba(255, 255, 255, 0.16)` (40/255) | `CommandBarBorder()` |
| Backdrop blur | 16px sigma | 16px sigma | `kCommandBarBlurSigma = 16.0f` |
| Suggestion hover | `rgba(0, 0, 0, 0.06)` | `rgba(255, 255, 255, 0.06)` | `SuggestionHover()` |
| Suggestion selected | `rgba(0, 0, 0, 0.10)` (25/255) | `rgba(255, 255, 255, 0.10)` (25/255) | `SuggestionSelected()` |
| Suggestion title | `rgb(10, 8, 16)` | `rgb(250, 250, 250)` | `SuggestionTitleColor()` |
| Suggestion icon | inherits secondary | inherits secondary | `SuggestionIconColor()` (alias of `TextSecondary`) |
| Ghost text (autocomplete) | `rgba(30, 20, 40, 0.30)` (77/255) | `rgba(255, 255, 255, 0.30)` | `GhostTextColor()` |

### 1.5 Split View

| Purpose | Light mode | Dark mode | C++ Function |
|---------|------------|-----------|--------------|
| Divider | inherits surface | inherits surface | `DividerColor()` (alias) |
| Divider hover | `rgba(70, 120, 190, 0.50)` | same | `DividerHoverColor()` |
| Drop zone overlay | `rgba(70, 120, 190, 0.15)` (38/255) | same | `DropZoneOverlay()` |
| Active pane border | `rgba(70, 120, 190, 0.60)` (153/255) | same | `ActivePaneBorder()` |
| Active pane glow | `rgba(70, 120, 190, 0.15)` (38/255) | same | `ActivePaneGlow()` |
| Pane header bg | `rgba(231, 238, 245, 0.90)` (230/255) | `rgba(70, 76, 82, 0.90)` (230/255) | `PaneHeaderBackground()` |
| Pane header shadow | `rgba(0, 0, 0, 0.16)` (40/255) | `rgba(0, 0, 0, 0.24)` (60/255) | `PaneHeaderShadow()` |
| Pane header button hover | inherits surface | inherits surface | `PaneHeaderButtonHover()` (alias) |
| Pane header button icon | inherits secondary | inherits secondary | `PaneHeaderButtonIcon()` (alias) |

Geometry: divider 4px wide, drop zone edge 40px, min pane size 200px (`kDividerWidth`, `kDropZoneEdgeSize`, `kMinPaneSize`).

### 1.6 Popup / Toast / Control Center

Popups (menus, toasts, control center cards) use a slightly brighter surface than the sidebar in dark mode for elevation.

| Purpose | Light mode | Dark mode | C++ Function |
|---------|------------|-----------|--------------|
| Popup background | `rgba(255, 255, 255, 0.90)` (230/255) | `rgba(70, 76, 82, 0.90)` (230/255) | `PopupBackground()` |
| Toast background | `rgb(255, 255, 255)` solid | `rgb(70, 76, 82)` solid | `ToastBackground()` |
| Toast text | `rgb(35, 35, 40)` | `rgb(240, 240, 245)` | `ToastTextColor()` |
| Popup shadow outer (40px blur) | `rgba(0, 0, 0, 0.12)` (30/255) | `rgba(0, 0, 0, 0.24)` (60/255) | `PopupShadowOuter()` |
| Popup shadow inner (16px blur, y=4) | `rgba(0, 0, 0, 0.18)` (45/255) | `rgba(0, 0, 0, 0.35)` (90/255) | `PopupShadowInner()` |

Control center buttons use neutral grays, not the accent:

| Purpose | Light mode | Dark mode | C++ Function |
|---------|------------|-----------|--------------|
| Icon default | `rgba(0, 0, 0, 0.63)` (160/255) | `rgba(255, 255, 255, 0.63)` | `ControlCenterIconDefault()` |
| Icon muted | `rgb(55, 55, 60)` | `rgb(170, 170, 175)` | `ControlCenterIconMuted()` |
| Hover bg | `rgba(0, 0, 0, 0.08)` (20/255) | `rgba(255, 255, 255, 0.08)` | `ControlCenterHoverBg()` |
| Active bg | `rgba(0, 0, 0, 0.10)` (25/255) | `rgba(255, 255, 255, 0.10)` | `ControlCenterActiveBg()` |
| Label | `rgb(100, 100, 100)` | `rgb(200, 200, 205)` | `ControlCenterLabelColor()` |
| Secondary text | `rgb(160, 160, 160)` | `rgb(160, 160, 165)` | `ControlCenterSecondaryTextColor()` |

### 1.7 Agent Lock Banner (Little Dao Overlay)

The agent-lock surface floats over the brand overlay; values are tuned so the brand reads through.

| Purpose | Light mode | Dark mode | C++ Function |
|---------|------------|-----------|--------------|
| Header fill | `rgba(255, 255, 255, 0.83)` (212/255) | `rgba(70, 76, 82, 0.86)` (220/255) | `AgentLockHeaderFill()` |
| Header shadow | `rgba(24, 16, 36, 0.11)` (28/255) | `rgba(0, 0, 0, 0.24)` (60/255) | `AgentLockHeaderShadow()` |
| Dot color (alpha applied by caller) | white base | black base | `AgentLockDotColor()` |
| Mist gradient step `n` | `rgba(255, 255, 255, 10 + 10n)` | `rgba(0, 0, 0, 10 + 10n)` | `AgentLockMistColor(step)` |

### 1.8 Spaces

| Purpose | Light mode | Dark mode | C++ Function |
|---------|------------|-----------|--------------|
| Active space dot | `rgb(70, 120, 190)` (accent) | same | `SpaceActive()` |
| Inactive space dot | `rgba(30, 20, 40, 0.24)` (60/255) | `rgba(255, 255, 255, 0.24)` | `SpaceInactive()` |

### 1.9 InkDrop

Uniform ripple feedback on every clickable native View — a single source so all surfaces feel identical.

| | Light mode | Dark mode |
|---|------------|-----------|
| Base color | `SK_ColorBLACK` | `SK_ColorWHITE` |
| Opacity | `0.04f` | `0.06f` |

Dark surfaces need slightly stronger feedback to be perceptible — that's why dark mode uses 6% vs light's 4%. C++: `InkDropBase()`, `InkDropOpacity()`.

### 1.10 Drop Shadow Alpha Base

`DaoCornerOverlayView` paints a 6-step soft shadow under the rounded content area. The per-step alpha base scales by theme:

| | Value | C++ Function |
|---|-------|--------------|
| Light mode | `12.0f` | `CornerShadowAlphaBase()` |
| Dark mode | `18.0f` (×1.5) | `CornerShadowAlphaBase()` |

Dark surfaces absorb shadow, so dark mode multiplies the alpha base by 1.5 to keep the floating effect visible.

### 1.11 Content Area (Web Page)

The content area does NOT use Dao's theme tokens. It dynamically adopts the **web page's own background color** and switches its overlay/separator/text adaptive variants based on the page's luminance. The chrome surrounds the page; the page is responsible for its own contrast.

### 1.12 Design Rules

- **Never use full-opacity white or black for backgrounds in chrome.** Use alpha-tinted surfaces (4–16%).
- **Never use pure black `rgb(0,0,0)` for text.** Light mode uses `rgb(30, 20, 40)` — slightly purple, slightly warm.
- **Accent is for active/interactive states only.** Active tab indicator, focused space, drop zone, divider hover, active pane outline. Do not use it for passive decoration.
- **Every color is a function call.** Never cache a `SkColor` across a paint — the user can flip dark mode at any time.
- **In WebUI, mirror the same hierarchy** but with media-query overrides; see §10.

---

## 2. Typography

### 2.1 Font Stack

```
font-family: system-ui, -apple-system, sans-serif;
```

No custom fonts. The browser uses the platform's native font.

### 2.2 Scale

| Use | Size | Weight | Notes |
|-----|------|--------|-------|
| Page title | 16px | 600 (SemiBold) | WebUI heading |
| Section heading | 14px | 600 (SemiBold) | Sidebar sections, card titles |
| Body / default | 13px | 400 (Normal) | `font-size` on `html, body` in agent.css |
| Small / caption | 12px | 400 (Normal) | Tab URL, timestamps, secondary info |
| Tiny / badge | 11px | 400 (Normal) | Badge counts, micro labels |

### 2.3 Rules

- **SemiBold (600) for titles, Normal (400) for everything else.** No Bold (700), no Light (300).
- Line height: browser default (`normal` ≈ 1.2). Override only for multi-line body (`line-height: 1.5`).
- Text color follows the 3-level hierarchy: primary → secondary → muted. Never mix levels in the same group.
- Truncation: `text-overflow: ellipsis` on a single line. Tab titles and URLs never wrap.

---

## 3. Spacing

### 3.1 Base Unit — 4px

| Token | Value | Use |
|-------|-------|-----|
| `xs` | 4px | Tight gaps (icon-to-text) |
| `sm` | 8px | Intra-component spacing |
| `md` | 12px | Component padding, section gaps |
| `lg` | 16px | Section padding, card padding |
| `xl` | 24px | Page-level margins |
| `2xl` | 32px | Major section separation |

### 3.2 Sidebar Layout

| Measurement | Value |
|-------------|-------|
| Sidebar width (expanded) | 240px (default) |
| Sidebar width (min / max) | 150px / 400px (drag-resize range) |
| Sidebar width (collapsed) | 4px |
| Sidebar internal padding | 8px horizontal |
| Tab item height | ~36px |
| Section gap | 8px |

### 3.3 Content Area

| Measurement | Value | C++ Constant |
|-------------|-------|--------------|
| Corner radius | 10px | `kContentCornerRadius` |
| Shadow margin | 8px | `kContentShadowMargin` |
| Inset top / right / bottom | 6px / 6px / 6px | `kContentInsetTop` / `Right` / `Bottom` |

(No left inset — the content area abuts the sidebar's right edge.)

---

## 4. Shape (Border Radius)

A consistent radius hierarchy from large containers down to small icons:

| Level | Radius | Use |
|-------|--------|-----|
| Level 1 | 16px | Command bar container, step indicator pills |
| Level 2 | 14px | URL pill, provider chips |
| Level 3 | 12px (`--radius`) | Cards, buttons, tab items, chat bubbles |
| Level 4 | 10px | Content area corners (`kContentCornerRadius`) |
| Level 5 | 8px | Favorite icons, pane header (`kPaneHeaderCornerRadius`), site icons |
| Level 6 | 6px | Pane header buttons (`kPaneHeaderButtonRadius`), small chips |

### Rules

- Nested elements use a smaller radius than their parent (card 12px → button inside 8px).
- Never use `border-radius: 50%` (full circle) except for avatar images.
- CSS token `--radius: 12px` is the WebUI default; only override when hierarchy demands.

---

## 5. Motion & Animation

### 5.1 Principles

- **Functional, not decorative.** Animation communicates state changes (expand/collapse, appear/disappear). No entrance animations, no scroll-linked effects.
- **Fast.** Most transitions complete in 150–200ms. Nothing exceeds 300ms.

### 5.2 Timing

| Animation | Duration | Easing | Notes |
|-----------|----------|--------|-------|
| Sidebar collapse/expand | 150ms | `ease-in-out` | Width 240px ↔ 4px |
| Tab item hover | 100ms | `ease` | Background opacity change |
| Icon rotation (loading) | 600ms | `linear` | Continuous, infinite |
| Step transition (wizard) | 200ms | `ease` | Fade between welcome wizard steps |
| Skeleton shimmer | 1500ms | `linear` | Infinite pulse |
| InkDrop ripple | 150ms | default | Chromium Views InkDrop default |

### 5.3 Shimmer Token (WebUI)

```css
--shimmer: linear-gradient(90deg, var(--surface) 25%, var(--surface-hover) 50%, var(--surface) 75%);
```

Animate with `background-size: 200% 100%` and a `background-position` keyframe.

### 5.4 Rules

- **No `transition: all`.** Always specify exact properties.
- Prefer `transform` and `opacity` (GPU-accelerated). Avoid animating `width`/`height`/`top`/`left` where possible.
- Honor reduced motion: `@media (prefers-reduced-motion: reduce)` disables non-essential animation.

---

## 6. Icons

### 6.1 Library — Lucide

All icons come from **[Lucide](https://lucide.dev/)**. No custom icon paths, no emoji.

### 6.2 Rendering

```html
<svg xmlns="http://www.w3.org/2000/svg"
     width="16" height="16"
     viewBox="0 0 24 24"
     fill="none"
     stroke="currentColor"
     stroke-width="2"
     stroke-linecap="round"
     stroke-linejoin="round">
  <!-- path data fetched from lucide upstream -->
</svg>
```

### 6.3 Sizes

| Context | Size |
|---------|------|
| Sidebar tab icon | 16×16 |
| Favorite icon | 20×20 |
| Button icon | 16×16 |
| Pane header button | 14×14 |
| Page section icon | 20×20 |

### 6.4 Rules

- **Always `stroke="currentColor"`** — icons inherit text color, including theme switches.
- **Never use emoji** as icons.
- **Never hand-write or recall SVG path data from memory.** Lucide updates frequently — older cached versions of `play`/`skip-back`/`volume-2` use polygon shapes that newer Lucide replaces with bezier curves. Always fetch the authoritative SVG before adding/modifying an icon:
  ```bash
  curl -s https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/<name>.svg
  ```
  Copy the `<path>` / `<rect>` / `<line>` children **verbatim** — do not "simplify" coordinates.
- `fill="none"` always. Never switch to filled variants.
- `stroke-width="2"` is standard. Use `1.5` only for decorative icons ≥ 24px.

---

## 7. Interaction

### 7.1 Hover

- Background highlight: surface tint (4–8% opacity of the appropriate ink — black in light, white in dark).
- Reveal hidden controls on hover (e.g., tab close button appears).
- Transition: 100ms ease.

### 7.2 Active / Pressed

- Background: surface tint at 10–14% opacity.
- For accent items (active tab, focused space): use the accent at 100% (or `kActiveTabBackground` for the tab body).

### 7.3 Focus

- `FocusRing` is **globally disabled** in native Views — Chromium's default ring would clash with Dao's calm chrome. Focus is instead indicated by background-color change.
- In WebUI: `outline: 2px solid var(--accent)` with `outline-offset: 2px` on `:focus-visible` only (never `:focus`).
- **Every focusable element must have an accessible name.** See §12.1 — Chromium FATAL-crashes if a focusable view has no accessible name.

### 7.4 InkDrop (Native Views)

- Base + opacity from §1.9. Applied uniformly to all clickable Views via the standard Chromium InkDrop pipeline.

### 7.5 Disabled State

- Opacity: 40% (matches `TextMuted` level).
- No hover, no ripple, `cursor: not-allowed` in WebUI.

---

## 8. Shadows

### 8.1 Content Area Shadow

The content area floats over the sidebar's pale/dark backdrop. `DaoCornerOverlayView` paints a 6-step progressive soft shadow with per-step alpha derived from `CornerShadowAlphaBase()` (12.0 light, 18.0 dark). The WebUI equivalent:

```css
box-shadow:
  0 2px 4px rgba(0,0,0,0.08),
  0 4px 8px rgba(0,0,0,0.08),
  0 8px 16px rgba(0,0,0,0.06),
  0 16px 32px rgba(0,0,0,0.04),
  0 32px 64px rgba(0,0,0,0.02);
```

In dark mode, increase each layer's alpha proportionally (×1.5) to remain visible.

### 8.2 Pane Header Shadow

Frosted-glass pill effect for split-view pane headers — see `PaneHeaderShadow()` in §1.5.

### 8.3 Popup Shadow

Two-layer shadow for popups, control center cards, agent lock banner — see `PopupShadowOuter()` / `PopupShadowInner()` in §1.6.

### 8.4 Rules

- **No sharp/hard shadows.** Always multi-layer soft shadows.
- Shadow is reserved for elevated surfaces: content area, command bar, popups, agent lock banner, pane headers.
- Sidebar elements do NOT have shadows — they use background color changes for hierarchy.

---

## 9. Layout Patterns

### 9.1 Sidebar + Content

```
┌──────────┬──────────────────────────────────┐
│          │                                  │
│ Sidebar  │         Content Area             │
│  240px   │      (10px rounded corners)      │
│          │                                  │
│          │                                  │
└──────────┴──────────────────────────────────┘
```

- Sidebar fixed left, 240px expanded / 4px collapsed (drag-resize 150–400px).
- Content area fills remaining space with 6px insets and 10px rounded corners + 6-step soft shadow.

### 9.2 WebUI Page Base

All Dao WebUI pages (`dao://agent`, `dao://welcome`, `dao://summary`) share:

```css
html, body {
  height: 100%;
  font-family: system-ui, -apple-system, sans-serif;
  font-size: 13px;
  background: var(--bg);
  color: var(--text);
  overflow: hidden;
}
```

### 9.3 Responsive Breakpoint

Single breakpoint for the sidebar-narrow edge case:

- **< 700px content width**: dual-column layouts (e.g., summary page) collapse to single-column.
- Welcome page is always single-column, max-width 480px centered.
- No other breakpoints — macOS desktop only.

---

## 10. CSS Token Reference

WebUI pages mirror the native palette via `:root` blocks. **Light mode is the default**; a `@media (prefers-color-scheme: dark)` override remaps the same tokens for dark mode, so component CSS never branches on theme.

### 10.1 Shared Base (default = light)

```css
:root {
  /* Backgrounds */
  --bg: rgb(231, 238, 245);
  --surface: rgba(0, 0, 0, 0.06);
  --surface-hover: rgba(0, 0, 0, 0.10);
  --border: rgba(0, 0, 0, 0.08);

  /* Text (base ink rgb(30,20,40)) */
  --text: rgba(30, 20, 40, 0.87);
  --text-secondary: rgba(30, 20, 40, 0.60);
  --text-tertiary: rgba(30, 20, 40, 0.40);

  /* Accent (shared across themes) */
  --accent: rgb(70, 120, 190);
  --accent-dim: rgba(70, 120, 190, 0.15);
  --accent-subtle: rgba(70, 120, 190, 0.10);

  /* Status */
  --error: #ef4444;

  /* Shape */
  --radius: 12px;
}

@media (prefers-color-scheme: dark) {
  :root {
    --bg: rgb(54, 59, 64);
    --surface: rgba(255, 255, 255, 0.06);
    --surface-hover: rgba(255, 255, 255, 0.12);
    --border: rgba(255, 255, 255, 0.12);

    --text: rgba(245, 245, 245, 0.92);
    --text-secondary: rgba(255, 255, 255, 0.60);
    --text-tertiary: rgba(255, 255, 255, 0.40);

    --accent-dim: rgba(70, 120, 190, 0.28);
    --accent-subtle: rgba(70, 120, 190, 0.18);
  }
}
```

### 10.2 Page-Specific Extensions

```css
:root {
  /* Agent page */
  --user-bubble: rgba(70, 120, 190, 0.18);
  --assistant-bubble: rgba(0, 0, 0, 0.04);

  /* Loading skeletons */
  --shimmer: linear-gradient(90deg, var(--surface) 25%, var(--surface-hover) 50%, var(--surface) 75%);
}

@media (prefers-color-scheme: dark) {
  :root {
    --user-bubble: rgba(70, 120, 190, 0.28);
    --assistant-bubble: rgba(255, 255, 255, 0.06);
  }
}
```

> **Reminder:** `agent.html` and other Dao-owned WebUI files MUST NOT use Tailwind utility classes — `pi_web_ui.css` is precompiled by the vendor pipeline and Dao-written classes are not in that compiled output. Use inline `style=""` or scoped rules in `agent.css` / Lit `<style>` blocks. See `CLAUDE.md` for the full rule.

---

## 11. C++ API Reference

All theme-aware colors are **functions** declared in `src/dao/browser/ui/views/dao_colors.h`, namespace `dao::`. Call them on every paint — never cache the returned `SkColor` across paints, since `IsDarkMode()` can flip mid-session.

| Function | Returns (light / dark) | Used By |
|----------|------------------------|---------|
| `IsDarkMode()` | `bool` | Theme dispatch |
| `SidebarBackground()` | `rgb(231,238,245)` / `rgb(54,59,64)` | Sidebar panel |
| `FrameColor()` | alias of `SidebarBackground` | Window frame |
| `TextPrimary()` | `rgb(30,20,40)` / `rgb(245,245,245)` | Primary labels |
| `TextSecondary()` | 60% base ink | Secondary labels |
| `TextMuted()` | 40% base ink | Caption / muted text |
| `ActiveTabBackground()` | `rgba(±,0.08)` | Selected tab, surface |
| `SeparatorColor()` | alias of surface | Dividers |
| `AddressBarBackground()` | `rgba(±,0.06)` | URL pill |
| `SpaceActive()` | `rgb(70,120,190)` | Active space dot |
| `SpaceInactive()` | 24% base ink | Inactive space dot |
| `InkDropBase()` | `BLACK` / `WHITE` | Ripple base |
| `InkDropOpacity()` | `0.04f` / `0.06f` | Ripple opacity |
| `CommandBarScrim()` | `rgba(0,0,0,0.31)` / `0.47` | Command bar backdrop |
| `CommandBarBackground()` | translucent white / dark gray | Command bar panel |
| `CommandBarBorder()` | 16% ink | Command bar border |
| `kCommandBarBlurSigma` | `16.0f` | Backdrop blur |
| `SuggestionHover()` | 6% ink | Command bar hover row |
| `SuggestionSelected()` | 10% ink | Command bar selected row |
| `SuggestionTitleColor()` | `rgb(10,8,16)` / `rgb(250,250,250)` | Command bar title |
| `SuggestionIconColor()` | alias `TextSecondary` | Command bar icons |
| `GhostTextColor()` | 30% ink | Autocomplete ghost text |
| `DividerColor()` | alias of surface | Split divider |
| `DividerHoverColor()` | accent at 50% | Split divider hover |
| `DropZoneOverlay()` | accent at 15% | Split drop zone |
| `ActivePaneBorder()` | accent at 60% | Split active pane |
| `ActivePaneGlow()` | accent at 15% | Split active pane glow |
| `PaneHeaderBackground()` | translucent surface | Pane header pill |
| `PaneHeaderShadow()` | 16% / 24% black | Pane header shadow |
| `PaneHeaderButtonHover()` | alias of surface | Pane header button hover |
| `PaneHeaderButtonIcon()` | alias of secondary text | Pane header icon |
| `CornerShadowAlphaBase()` | `12.0f` / `18.0f` | Content area drop shadow |
| `PopupBackground()` | translucent surface | Menus / cards |
| `ToastBackground()` | solid surface | Toasts |
| `ToastTextColor()` | dark / light text | Toast labels |
| `ControlCenterIconDefault()` | 63% ink | Control center icons |
| `ControlCenterIconMuted()` | mid-gray | Control center muted icons |
| `ControlCenterHoverBg()` | 8% ink | Control center button hover |
| `ControlCenterActiveBg()` | 10% ink | Control center button active |
| `ControlCenterLabelColor()` | tuned grays | Control center labels |
| `ControlCenterSecondaryTextColor()` | tuned grays | Control center secondary |
| `PopupShadowOuter()` | 12% / 24% black | Popup outer shadow |
| `PopupShadowInner()` | 18% / 35% black | Popup inner shadow |
| `AgentLockHeaderFill()` | translucent surface | Agent lock banner |
| `AgentLockHeaderShadow()` | tuned shadow | Agent lock banner shadow |
| `AgentLockDotColor()` | white / black (alpha applied by caller) | Agent lock dot pattern |
| `AgentLockMistColor(step)` | gradient stop alpha | Agent lock mist gradient |

Geometry / shape constants (theme-independent):

| Constant | Value | Used By |
|----------|-------|---------|
| `kContentCornerRadius` | `10` | Content area roundness |
| `kContentShadowMargin` | `8` | Shadow offset |
| `kContentInsetTop` / `Right` / `Bottom` | `6` | Content area inset |
| `kDividerWidth` | `4` | Split divider width |
| `kDropZoneEdgeSize` | `40` | Split drop-zone edge |
| `kMinPaneSize` | `200` | Split min pane |
| `kPaneHeaderCornerRadius` | `8` | Pane header pill corners |
| `kPaneHeaderButtonSize` | `22` | Pane header button size |
| `kPaneHeaderButtonRadius` | `6` | Pane header button corners |
| `kActivePaneBorderWidth` | `2` | Split active pane outline |
| `kActivePaneGlowRadius` | `12` | Split active pane glow blur |

---

## 12. Accessibility

### 12.1 Native Views (C++)

- **Every focusable View (Textfield, Button, etc.) MUST have `SetAccessibleName()` or `SetPlaceholderText()`.** Chromium runs accessibility paint checks that **FATAL crash** if a focusable view has no accessible name. Always set this when creating any new focusable UI element.
- `FocusRing` is globally disabled — keyboard focus is indicated by background-color change.
- The browser test suite validates this; missing names will crash `browser_tests`, not just degrade UX.

### 12.2 WebUI

- All interactive elements: `aria-label` or `aria-labelledby`.
- Wizard steps: `role="group"` with `aria-label="Step N of M: [heading]"`.
- Page sections: `role="region"` with `aria-label`.
- Loading: skeleton shimmer is `aria-hidden="true"`; streaming text is `aria-live="polite"`.
- Focus visible: `outline: 2px solid var(--accent)` on `:focus-visible` only.

### 12.3 Contrast

Light mode (text on `rgb(231,238,245)`):
- Primary `rgb(30,20,40)` solid → ≈ 13.5:1 (exceeds AAA).
- Secondary 60% ink → ≈ 6.6:1 (exceeds AA).
- Muted 40% ink → ≈ 3.6:1 (decorative only, fails AA body).

Dark mode (text on `rgb(54,59,64)`):
- Primary `rgb(245,245,245)` → ≈ 11.6:1 (exceeds AAA).
- Secondary 60% white → ≈ 5.9:1 (exceeds AA).
- Muted 40% white → ≈ 3.4:1 (decorative only, fails AA body).

> Tertiary/muted is reserved for non-essential decoration (timestamps, hint text). Never use it for body content or interactive labels.

---

## 13. Philosophy

**Calm minimalism + Arc-style vertical tabs, maximizing content immersion with a blue brand identity.**

- The browser chrome should disappear. Content is king — whether the page is light or dark.
- The sidebar is a tool, not a destination. It collapses to 4px when not needed.
- Blue accent is used sparingly — it should feel deliberate, not overwhelming. Active tab, focused space, drop zone, divider hover, active pane outline.
- Every UI element earns its space. No decorative chrome, no gratuitous ornamentation.
- System fonts, not custom fonts. The browser should feel native to macOS.
- Interactions are instant and responsive. No animation exceeds 300ms.
- Two themes, one identity. Light mode is the default; dark mode follows the OS. The accent never changes — only the surfaces and ink invert.
