# Dao Browser — Design System

> Single source of truth for colors, typography, spacing, shape, motion, and iconography.
> All Dao UI — native Views (C++) and WebUI pages (Lit/CSS) — must follow this document.

---

## 1. Color

Dao uses a **dark purple-gray** palette. All UI elements derive from two base colors: a dark background and white at varying opacities for hierarchy. A single purple accent provides brand identity.

### 1.1 Core Palette

| Role | Value | CSS Token | C++ Constant |
|------|-------|-----------|--------------|
| Background | `rgb(40, 32, 48)` | `--bg` | `kSidebarBackground` / `kFrameColor` |
| Surface | `rgba(255,255,255, 0.08)` | `--surface` | `kActiveTabBackground` / `kAddressBarBackground` |
| Surface hover | `rgba(255,255,255, 0.12)` | `--surface-hover` | — |
| Border / separator | `rgba(255,255,255, 0.12)` | `--border` | `kSeparatorColor` (0.12) |
| Accent | `rgb(140, 100, 220)` | `--accent` | `kSpaceActive` |
| Accent dim | `rgba(140, 100, 220, 0.3)` | `--accent-dim` | — |
| Accent subtle | `rgba(140, 100, 220, 0.15)` | `--accent-subtle` | — |
| Error | `#ef4444` | `--error` | — |

### 1.2 Text Hierarchy

| Level | Opacity | CSS Token | C++ Constant |
|-------|---------|-----------|--------------|
| Primary | 87% white | `--text` | `kTextPrimary` (SK_ColorWHITE) |
| Secondary | 59% white | `--text-secondary` | `kTextSecondary` (150/255 ≈ 59%) |
| Tertiary / muted | 39% white | `--text-tertiary` | `kTextMuted` (100/255 ≈ 39%) |

### 1.3 Semantic Colors

| Purpose | Value | CSS Token |
|---------|-------|-----------|
| User chat bubble | `rgba(140, 100, 220, 0.25)` | `--user-bubble` |
| Assistant chat bubble | `rgba(255,255,255, 0.06)` | `--assistant-bubble` |
| InkDrop ripple | white 6% | — (C++: `kInkDropBase` + `kInkDropOpacity`) |
| Command bar scrim | `rgba(0, 0, 0, 0.47)` | — (C++: `kCommandBarScrim`) |
| Command bar background | `rgb(50, 42, 58)` | — (C++: `kCommandBarBackground`) |
| Suggestion hover | `rgba(255,255,255, 0.08)` | — (C++: `kSuggestionHover`) |
| Suggestion selected | `rgba(255,255,255, 0.16)` | — (C++: `kSuggestionSelected`) |
| Ghost text | `rgba(255,255,255, 0.31)` | — (C++: `kGhostTextColor`) |

### 1.4 Split View Colors

| Purpose | Value | C++ Constant |
|---------|-------|--------------|
| Divider | `rgba(255,255,255, 0.12)` | `kDividerColor` |
| Divider hover | `rgba(140, 100, 220, 0.50)` | `kDividerHoverColor` |
| Drop zone overlay | `rgba(140, 100, 220, 0.15)` | `kDropZoneOverlay` |
| Active pane border | `rgba(140, 100, 220, 0.60)` | `kActivePaneBorder` |
| Active pane glow | `rgba(140, 100, 220, 0.15)` | `kActivePaneGlow` |
| Pane header background | `rgba(30, 24, 38, 0.85)` | `kPaneHeaderBackground` |
| Pane header shadow | `rgba(0, 0, 0, 0.30)` | `kPaneHeaderShadow` |
| Pane header button hover | `rgba(255,255,255, 0.10)` | `kPaneHeaderButtonHover` |
| Pane header button icon | `rgba(255,255,255, 0.70)` | `kPaneHeaderButtonIcon` |

### 1.5 Content Area

The content area (web page region) dynamically adopts the web page's own background color. Text, separators, and overlays within this area switch between light/dark adaptive variants based on the page's luminance — the content area does NOT use Dao's dark theme tokens.

### 1.6 Design Rule

- **Never use full-opacity white for backgrounds.** All non-text white values use opacity (6%–16%).
- **Accent purple is for active/interactive states only.** Do not use it for passive/decorative elements.
- The dark background `(40, 32, 48)` has a slight purple warmth — never use pure black `(0,0,0)`.

---

## 2. Typography

### 2.1 Font Stack

```
font-family: system-ui, -apple-system, sans-serif;
```

No custom fonts. The browser uses the platform's native font on every OS.

### 2.2 Scale

| Use | Size | Weight | Token / Notes |
|-----|------|--------|---------------|
| Page title | 16px | 600 (SemiBold) | WebUI heading |
| Section heading | 14px | 600 (SemiBold) | Sidebar sections, card titles |
| Body / default | 13px | 400 (Normal) | `font-size` on `html, body` in `agent.css` |
| Small / caption | 12px | 400 (Normal) | Tab URL, timestamps, secondary info |
| Tiny / badge | 11px | 400 (Normal) | Badge counts, micro labels |

### 2.3 Rules

- **Titles use SemiBold (600), everything else Normal (400).** No Bold (700), no Light (300).
- Line height: use browser defaults (`normal` ≈ 1.2). Only override for multi-line body text (`line-height: 1.5`).
- Text color follows the 3-level hierarchy: primary → secondary → tertiary. Never mix hierarchy levels in the same visual group.
- Truncation: single-line text overflows with `text-overflow: ellipsis`. Never wrap tab titles or URL text.

---

## 3. Spacing

### 3.1 Base Unit

The spacing system is based on **4px increments**.

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
| Sidebar width (expanded) | 240px |
| Sidebar width (collapsed) | 4px |
| Sidebar internal padding | 8px horizontal |
| Tab item height | ~36px |
| Section gap | 8px |
| Content inset from sidebar | 6px right, 6px bottom, 6px top |

### 3.3 Content Area

| Measurement | Value | C++ Constant |
|-------------|-------|--------------|
| Corner radius | 10px | `kContentCornerRadius` |
| Shadow margin | 8px | `kContentShadowMargin` |
| Inset top | 6px | `kContentInsetTop` |
| Inset right | 6px | `kContentInsetRight` |
| Inset bottom | 6px | `kContentInsetBottom` |

---

## 4. Shape (Border Radius)

A consistent radius hierarchy from large (containers) to small (icons):

| Level | Radius | Use |
|-------|--------|-----|
| Level 1 | 16px | Command bar container, step indicator pills |
| Level 2 | 14px | URL pill, provider chips |
| Level 3 | 12px (`--radius`) | Cards, buttons, tab items, chat bubbles |
| Level 4 | 10px | Content area corners (`kContentCornerRadius`) |
| Level 5 | 8px | Favorite icons, pane header corners (`kPaneHeaderCornerRadius`), site icons |
| Level 6 | 6px | Pane header buttons (`kPaneHeaderButtonRadius`), small chips |

### Rules

- Nested elements use a smaller radius than their parent (e.g., card at 12px → button inside at 8px).
- Never use `border-radius: 50%` (full circle) except for avatar images.
- CSS token `--radius: 12px` is the default; only override when the hierarchy demands a different level.

---

## 5. Motion & Animation

### 5.1 Principles

- **Functional, not decorative.** Animation communicates state changes (expand/collapse, appear/disappear). No entrance animations, no scroll-linked effects.
- **Fast.** Most transitions complete in 150–200ms. Nothing exceeds 300ms.

### 5.2 Timing

| Animation | Duration | Easing | Notes |
|-----------|----------|--------|-------|
| Sidebar collapse/expand | 150ms | `ease-in-out` | Width transition from 240px ↔ 4px |
| Tab item hover | 100ms | `ease` | Background opacity change |
| Icon rotation (loading) | 600ms | `linear` | Continuous rotation, infinite |
| Step transition (wizard) | 200ms | `ease` | Fade between welcome wizard steps |
| Skeleton shimmer | 1500ms | `linear` | Infinite pulse for loading states |
| InkDrop ripple | 150ms | default | Chromium Views InkDrop default |

### 5.3 Shimmer Token

For loading/skeleton states in WebUI:

```css
--shimmer: linear-gradient(90deg, var(--surface) 25%, var(--surface-hover) 50%, var(--surface) 75%);
```

Animate with `background-size: 200% 100%` and `background-position` keyframe.

### 5.4 Rules

- **No `transition: all`.** Always specify the exact properties being animated.
- Prefer `transform` and `opacity` for GPU-accelerated animation. Avoid animating `width`, `height`, or `top`/`left` where possible.
- Disable animation for users who prefer reduced motion: `@media (prefers-reduced-motion: reduce)`.

---

## 6. Icons

### 6.1 Library

All icons use **[Lucide](https://lucide.dev/)** — an open-source icon set.

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
  <!-- path data -->
</svg>
```

### 6.3 Sizes

| Context | Size | Notes |
|---------|------|-------|
| Sidebar tab icon | 16×16 | Default |
| Favorite icon | 20×20 | In favorites bar |
| Button icon | 16×16 | Inside buttons |
| Pane header button | 14×14 | Compact controls |
| Page section icon | 20×20 | WebUI section headings |

### 6.4 Rules

- **Always use `stroke="currentColor"`** — icon color inherits from the text color of its context.
- **Never use emoji** as icons in UI. Always pick from the Lucide set.
- **Never use custom SVG paths** — find the closest Lucide icon instead.
- `fill="none"` for outlined style, never switch to filled variants.
- `stroke-width="2"` is the standard. Use `1.5` only for larger decorative icons (24px+).

---

## 7. Interaction

### 7.1 Hover

- Background highlight: white at 6%–8% opacity (same as `--surface`).
- Reveal hidden controls on hover (e.g., tab close button appears).
- Transition: 100ms ease.

### 7.2 Active / Pressed

- Background: white at 12%–16% opacity.
- For accent items: use `--accent-dim` (purple 30%).

### 7.3 Focus

- `FocusRing` is globally disabled in native Views.
- In WebUI: use `outline: 2px solid var(--accent)` with `outline-offset: 2px` for keyboard focus (`:focus-visible` only).
- Every focusable element MUST have an accessible name (`aria-label`, `aria-labelledby`, or visible label).

### 7.4 InkDrop (Native Views)

- Base: `SK_ColorWHITE`
- Opacity: 6% (`kInkDropOpacity = 0.06f`)
- Applied uniformly to all clickable Views.

### 7.5 Disabled State

- Opacity: 39% (`--text-tertiary` level).
- No hover, no ripple, `cursor: not-allowed` in WebUI.

---

## 8. Shadows

### 8.1 Content Area Shadow

The content area uses a 6-step progressive soft shadow for floating effect:

```css
box-shadow:
  0 2px 4px rgba(0,0,0,0.08),
  0 4px 8px rgba(0,0,0,0.08),
  0 8px 16px rgba(0,0,0,0.06),
  0 16px 32px rgba(0,0,0,0.04),
  0 32px 64px rgba(0,0,0,0.02);
```

### 8.2 Pane Header Shadow

Frosted glass pill effect:

```
shadow color: rgba(0, 0, 0, 0.30)  /* kPaneHeaderShadow */
```

### 8.3 Rules

- **No sharp/hard shadows.** Always use multi-layer soft shadows.
- Shadow is reserved for elevated surfaces: content area, command bar, floating panels.
- Sidebar elements do NOT have shadows — they use background color changes for hierarchy.

---

## 9. Layout Patterns

### 9.1 Sidebar + Content

```
┌──────────┬──────────────────────────────────┐
│          │                                  │
│ Sidebar  │         Content Area             │
│  240px   │      (rounded corners)           │
│          │                                  │
│          │                                  │
└──────────┴──────────────────────────────────┘
```

- Sidebar: fixed left, 240px expanded / 4px collapsed.
- Content area: fills remaining space with 6px margins and 10px rounded corners.

### 9.2 WebUI Page Layout

All WebUI pages (`dao://agent`, `dao://welcome`, `dao://summary`) share:

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

Page-specific layouts build on top of this base.

### 9.3 Responsive Breakpoint

Single breakpoint for the sidebar-narrow edge case:

- **< 700px content width**: Dual-column layouts (e.g., summary page) stack to single-column.
- Welcome page is always single-column, max-width 480px centered.
- No other responsive breakpoints — macOS desktop only.

---

## 10. CSS Token Reference

The canonical token source is the `:root` block in each WebUI page's CSS. All pages MUST define this shared set:

```css
:root {
  /* Backgrounds */
  --bg: rgb(40, 32, 48);
  --surface: rgba(255,255,255, 0.08);
  --surface-hover: rgba(255,255,255, 0.12);
  --border: rgba(255,255,255, 0.12);

  /* Text */
  --text: rgba(255,255,255, 0.87);
  --text-secondary: rgba(255,255,255, 0.59);
  --text-tertiary: rgba(255,255,255, 0.39);

  /* Accent */
  --accent: rgb(140, 100, 220);
  --accent-dim: rgba(140, 100, 220, 0.3);

  /* Status */
  --error: #ef4444;

  /* Shape */
  --radius: 12px;
}
```

Page-specific extensions (added as needed, not in the shared set):

```css
:root {
  /* Agent page */
  --user-bubble: rgba(140, 100, 220, 0.25);
  --assistant-bubble: rgba(255,255,255, 0.06);

  /* Summary/Welcome pages */
  --accent-subtle: rgba(140, 100, 220, 0.15);
  --shimmer: linear-gradient(90deg, var(--surface) 25%, var(--surface-hover) 50%, var(--surface) 75%);
}
```

---

## 11. C++ Color Constant Reference

All constants are in `src/dao/browser/ui/views/dao_colors.h`, namespace `dao::`.

| Constant | Value | Used By |
|----------|-------|---------|
| `kSidebarBackground` | `rgb(40, 32, 48)` | Sidebar panel |
| `kFrameColor` | `rgb(40, 32, 48)` | Window frame |
| `kTextPrimary` | `SK_ColorWHITE` | Primary labels |
| `kTextSecondary` | `rgba(255,255,255, 0.59)` | Secondary labels |
| `kTextMuted` | `rgba(255,255,255, 0.39)` | Muted/caption text |
| `kActiveTabBackground` | `rgba(255,255,255, 0.14)` | Selected tab |
| `kSeparatorColor` | `rgba(255,255,255, 0.12)` | Divider lines |
| `kAddressBarBackground` | `rgba(255,255,255, 0.14)` | URL bar |
| `kSpaceActive` | `rgb(140, 100, 220)` | Active space indicator |
| `kSpaceInactive` | `rgba(255,255,255, 0.24)` | Inactive space |
| `kInkDropBase` | `SK_ColorWHITE` | Ripple base color |
| `kInkDropOpacity` | `0.06f` | Ripple opacity |
| `kCommandBarScrim` | `rgba(0,0,0, 0.47)` | Backdrop scrim |
| `kCommandBarBackground` | `rgb(50, 42, 58)` | Panel background |
| `kSuggestionHover` | `rgba(255,255,255, 0.08)` | List hover |
| `kSuggestionSelected` | `rgba(255,255,255, 0.16)` | List selection |
| `kGhostTextColor` | `rgba(255,255,255, 0.31)` | Autocomplete ghost |
| `kContentCornerRadius` | `10` | Content roundness |
| `kContentShadowMargin` | `8` | Shadow offset |

---

## 12. Accessibility

### 12.1 Native Views (C++)

- Every focusable View (Textfield, Button, etc.) MUST have `SetAccessibleName()` or `SetPlaceholderText()`.
- Chromium runs accessibility paint checks that **FATAL crash** if a focusable view has no accessible name.
- `FocusRing` is disabled globally — keyboard focus is indicated via background color change.

### 12.2 WebUI

- All interactive elements: `aria-label` or `aria-labelledby`.
- Wizard steps: `role="group"` with `aria-label="Step N of M: [heading]"`.
- Page sections: `role="region"` with `aria-label`.
- Loading states: skeleton shimmer uses `aria-hidden="true"`, streaming text uses `aria-live="polite"`.
- Focus visible: `outline: 2px solid var(--accent)` on `:focus-visible` only.

### 12.3 Contrast

- Primary text (87% white) on background `(40,32,48)`: contrast ratio ≈ 10.5:1 (exceeds AAA).
- Secondary text (59% white): contrast ratio ≈ 6.2:1 (exceeds AA).
- Tertiary text (39% white): contrast ratio ≈ 3.7:1 (decorative/non-essential only — does NOT meet AA for body text).

---

## 13. Philosophy

**Dark minimalism + Arc-style vertical tabs, maximizing content immersion with a purple brand identity.**

- The browser chrome should disappear. Content is king.
- The sidebar is a tool, not a destination. It gets out of the way when not needed.
- Purple accent is used sparingly — it should feel special, not overwhelming.
- Every UI element earns its space. No decorative chrome, no gratuitous ornamentation.
- System fonts, not custom fonts. The browser should feel native to the platform.
- Interactions are instant and responsive. No animation exceeds 300ms.
