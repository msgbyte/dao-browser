# Dao Browser Website — Design Spec

> **Date**: 2026-05-03
> **Author**: Brainstorming session (user + assistant)
> **Status**: Approved design, pending implementation plan
> **Domain**: https://dao.msgbyte.com

---

## 1. Goal

Build the official product landing page for Dao Browser, hosted at `dao.msgbyte.com`. The page introduces the product to two audiences in parallel:

- **General users** — anchor on the Arc-style vertical sidebar experience and "calm" feel.
- **Developers / power users** — surface the open-source nature, Chromium underpinnings, and AI Agent capabilities further down the page.

The hero centers on a single positioning statement:

> **An opinionated browser, built on Chromium.**

The page is a single-route static landing page (no docs site, no blog, no marketing funnel). It launches with macOS Apple Silicon as the only buildable platform; Linux/Windows show "Coming soon" placeholders.

## 2. Non-Goals (Explicit)

The following are deliberately **out of scope** for this iteration. They are listed so future contributors don't expand the spec mid-flight.

- Multi-language (Chinese / English switch)
- Release Notes route or markdown pipeline
- Download statistics or analytics (Plausible / CF Web Analytics)
- Email subscription
- Lighthouse CI thresholds
- Visual regression tests
- Linux / Windows download buttons (CTA label is "Coming soon")
- Dynamic per-release Open Graph image generation
- A user-facing dark-mode toggle (theme follows OS, matching `DESIGN.md`)

## 3. Architecture

### 3.1 Repository Layout

The website lives in a sibling subdirectory of the main Chromium build pipeline so it ships with the same repository but does not pollute Chromium-related npm scripts.

```
dao-browser/
├── package.json              # main repo — adds `npm run website` forwarder only
├── dao.json                  # shared source of version + chromium info
├── website/                  # ← Next.js project lives here
│   ├── package.json          # independent dependencies (Next.js, React, TypeScript)
│   ├── next.config.mjs       # output: 'export' → static HTML
│   ├── tsconfig.json
│   ├── app/
│   │   ├── layout.tsx        # root layout: <html>, fonts, metadata, globals.css
│   │   ├── page.tsx          # landing page (single route)
│   │   ├── not-found.tsx     # minimal 404
│   │   └── globals.css       # design tokens + reset
│   ├── components/
│   │   ├── Hero.tsx
│   │   ├── FeatureSidebar.tsx
│   │   ├── FeatureCommandBar.tsx
│   │   ├── FeatureAgent.tsx
│   │   ├── FeatureGrid.tsx
│   │   ├── DownloadCTA.tsx
│   │   ├── Footer.tsx
│   │   └── ui/
│   │       ├── BrowserFrame.tsx       # macOS-style window chrome shell (rounded corners + traffic lights + 6-step shadow)
│   │       ├── BrowserFrameMockup.tsx # CSS-painted fake screenshot used while real screenshots are missing
│   │       └── LucideIcon.tsx         # typed wrapper around Lucide inline SVG
│   ├── lib/
│   │   └── version.ts        # reads dao.json at build time
│   ├── public/
│   │   ├── favicon.svg
│   │   ├── og.png            # 1200×630 static OG image
│   │   └── screenshots/
│   │       └── .gitkeep      # populated when real screenshots replace the mockup
│   └── README.md             # how to develop / build / deploy + manual QA checklist
```

### 3.2 Main Repo Forwarder

The main repo's `package.json` gets one new script:

```json
{
  "scripts": {
    "website": "cd website && npm run dev"
  }
}
```

The `website` directory has its own `package.json` and lockfile. The main repo never depends on the website's `node_modules` and vice versa.

### 3.3 Build Output

`next.config.mjs` enables static export:

```js
export default { output: 'export' };
```

This produces `website/out/` containing static HTML/CSS/JS, suitable for any static host. **Cloudflare Pages** is the chosen target.

### 3.4 Rendering Model

- **All components default to React Server Components.** The page ships with zero client JavaScript beyond Next.js's framework runtime.
- **No `useState` / `useEffect`** is permitted in the initial build.
- **No client-side framework** (no framer-motion, no animation library). All motion is CSS transitions.
- **No CSS framework** (no Tailwind / vanilla-extract / styled-components). Vanilla CSS via `*.module.css` and `globals.css`. This intentionally mirrors the main project's CLAUDE.md warning that Tailwind utilities are not viable in Dao-owned WebUI code.

## 4. Page Content

The page is a single scroll, top to bottom, in this order:

1. **Top Nav** — minimal, sticky-on-scroll, transparent until scroll
2. **Hero** — H1 + subtitle + dual CTA + large product mockup
3. **Feature 1: Vertical Sidebar** — text-left, mockup-right
4. **Feature 2: Command Bar** — mockup-left, text-right (alternating direction)
5. **Feature 3: AI Agent** — text-left, mockup-right
6. **Feature Grid** — 3×2 small cards
7. **Download CTA** — centered, single big button
8. **Footer**

### 4.1 Top Nav

- Left: Dao wordmark (text "Dao", weight 600, letter-spacing -0.01em)
- Right: links — `Features` · `GitHub` · `Download` (the last is rendered as the accent-colored pill button)
- Behavior: fully transparent at scrollY=0; gains a 1px bottom border (`var(--border)`) and a subtle backdrop blur once scrolled. Implemented with CSS `position: sticky` + `@supports (backdrop-filter)`. **No JS scroll listener.**

### 4.2 Hero

| Element | Content |
|---|---|
| Eyebrow | `DAO BROWSER` (uppercase, 12px, letter-spacing 0.18em, `--text-tertiary`) |
| H1 | `An opinionated browser, built on Chromium.` (48px / clamp 36–56, weight 600, letter-spacing -0.025em, `--text`) |
| Subtitle | `Vertical tabs, soft corners, content first.` (17px, `--text-secondary`) |
| Primary CTA | `Download for Mac (Apple Silicon)` — accent-filled pill, 12px radius |
| Secondary CTA | `★ Star on GitHub` — transparent border |
| Below CTAs | Hint line: `Latest: v{display} · Chromium {version}` (13px, `--text-tertiary`) |
| Below the fold | `<BrowserFrame>` containing the CSS-painted mockup of Dao's window |

Hero spacing: 96px top padding (desktop), 64px on mobile. CTAs stack vertically on `< 640px`.

### 4.3 Feature Sections (1–3)

All three share the same template. Two-column on desktop (`5/12 text + 7/12 mockup`), single-column on `< 768px`.

#### Feature 1 — Vertical Sidebar (text-left, mockup-right)

- Eyebrow: `01 / VERTICAL TABS`
- Heading: `Tabs that read like a list, not a strip.`
- Body: `Drag to resize from 150 to 400 pixels. Collapse to 4 with one keystroke. Group with folders, switch with spaces, pin what you visit daily.`
- Three bullets with Lucide icons: `panel-left` (Collapse), `folder` (Folders), `pin` (Favorites)
- Mockup: BrowserFrame with the sidebar visually emphasized

#### Feature 2 — Command Bar (mockup-left, text-right)

- Eyebrow: `02 / COMMAND BAR`
- Heading: `Cmd+T, Cmd+L, ask anything.`
- Body: `Spotlight-style command bar with URL detection, search routing, and ghost-text completion. Press Esc on a fresh tab to cancel — Dao remembers where you came from.`
- Three bullets: `command` (New tab), `search` (Smart routing), `sparkles` (Ask AI)
- Mockup: BrowserFrame with the command bar overlay shown

#### Feature 3 — AI Agent (text-left, mockup-right)

- Eyebrow: `03 / AI AGENT`
- Heading: `An assistant that lives next to your tabs.`
- Body: `A native AI sidebar with long-term memory, proactive suggestions on every navigation, a skill system, and a visible cursor that shows you exactly what it touched.`
- Three bullets: `brain` (Long-term memory), `zap` (Proactive), `mouse-pointer-2` (Visible actions)
- Mockup: BrowserFrame showing the agent sidebar + chat surface

### 4.4 Feature Grid (3×2 desktop / 2×3 tablet / 1×6 mobile)

Each card: Lucide icon (20px) on top, h3 title, one-line description in `--text-secondary`. No CTAs inside cards.

| # | Title | Lucide Icon | Description |
|---|---|---|---|
| 1 | Picture-in-Picture | `picture-in-picture-2` | Pop a video out, keep watching while you read |
| 2 | Split View | `columns-2` | Drag two tabs together, work side by side |
| 3 | Control Center | `sliders-horizontal` | One panel, every browser knob you need |
| 4 | Little Dao | `square` | A miniature Dao window for quick lookups |
| 5 | Adaptive Theming | `palette` | The chrome adapts to the page you're reading |
| 6 | Native Chromium | `chrome` | Built on real Chromium 147 — not a wrapper |

### 4.5 Download CTA

Centered block.

- Heading (32px, weight 600): `Try Dao Browser.`
- Sub (16px, `--text-secondary`): `v{display} · Built on Chromium {version}`
- Primary button: `Download for Mac (Apple Silicon)` (same accent pill)
- Below button (13px, `--text-tertiary`): `Linux and Windows · Coming soon · ★ Star on GitHub for updates`

### 4.6 Footer

Four columns on desktop, two on tablet, one on mobile.

- Col 1: Dao wordmark + tagline `An opinionated browser. Built on Chromium. Open source.`
- Col 2 — Product: Features / Download / GitHub / Releases
- Col 3 — Resources: Docs / Design System / Source on GitHub
- Col 4 — Acknowledgements: Inspired by Arc / Built on Chromium
- Bottom strip (1px `--border` separator above): `© 2026 Dao Browser` + small `Built with Next.js, hosted on Cloudflare Pages` line

## 5. Visual System

### 5.1 Design Tokens (Aligned with `DESIGN.md`)

Light-mode is the default; dark-mode is the `prefers-color-scheme: dark` override. The tokens **mirror the main project's `DESIGN.md` §10.1 verbatim** so the website feels like the same product.

```css
:root {
  /* Backgrounds */
  --bg:              rgb(231, 238, 245);
  --bg-elevated:     #ffffff;
  --surface:         rgba(0, 0, 0, 0.06);
  --surface-hover:   rgba(0, 0, 0, 0.10);
  --border:          rgba(0, 0, 0, 0.08);

  /* Text — base ink rgb(30, 20, 40) */
  --text:            rgba(30, 20, 40, 0.87);
  --text-secondary:  rgba(30, 20, 40, 0.60);
  --text-tertiary:   rgba(30, 20, 40, 0.40);

  /* Accent — shared across themes */
  --accent:          rgb(70, 120, 190);
  --accent-hover:    rgb(60, 105, 175);
  --accent-dim:      rgba(70, 120, 190, 0.15);
  --accent-subtle:   rgba(70, 120, 190, 0.10);

  /* Status */
  --error:           #ef4444;

  /* Shape */
  --radius:          12px;

  /* 6-step soft shadow (DESIGN.md §8.1) */
  --shadow-frame:
    0  2px  4px  rgba(0, 0, 0, 0.08),
    0  4px  8px  rgba(0, 0, 0, 0.08),
    0  8px 16px  rgba(0, 0, 0, 0.06),
    0 16px 32px  rgba(0, 0, 0, 0.04),
    0 32px 64px  rgba(0, 0, 0, 0.02);
}

@media (prefers-color-scheme: dark) {
  :root {
    --bg:              rgb(54, 59, 64);
    --bg-elevated:     rgba(255, 255, 255, 0.04);
    --surface:         rgba(255, 255, 255, 0.06);
    --surface-hover:   rgba(255, 255, 255, 0.12);
    --border:          rgba(255, 255, 255, 0.12);

    --text:            rgba(245, 245, 245, 0.92);
    --text-secondary:  rgba(255, 255, 255, 0.60);
    --text-tertiary:   rgba(255, 255, 255, 0.40);

    --accent-hover:    rgb(85, 140, 210);
    --accent-dim:      rgba(70, 120, 190, 0.28);
    --accent-subtle:   rgba(70, 120, 190, 0.18);

    /* Dark shadow ×1.5 (DESIGN.md §1.10) */
    --shadow-frame:
      0  2px  4px  rgba(0, 0, 0, 0.12),
      0  4px  8px  rgba(0, 0, 0, 0.12),
      0  8px 16px  rgba(0, 0, 0, 0.09),
      0 16px 32px  rgba(0, 0, 0, 0.06),
      0 32px 64px  rgba(0, 0, 0, 0.03);
  }
}
```

### 5.2 Typography

Font stack — Windows-friendly extension of the main project's `system-ui` rule:

```css
font-family:
  -apple-system,
  BlinkMacSystemFont,
  "Segoe UI Variable",
  "Segoe UI",
  system-ui,
  Roboto,
  "Helvetica Neue",
  Arial,
  "PingFang SC",
  "Microsoft YaHei UI",
  "Microsoft YaHei",
  sans-serif,
  "Apple Color Emoji",
  "Segoe UI Emoji";
```

CJK fonts come **after** Latin fonts so English glyphs are not accidentally pulled from a CJK face.

Mono stack (used for version numbers / shell snippets if any):

```css
font-family:
  ui-monospace,
  "SF Mono",
  "Cascadia Code",
  "Cascadia Mono",
  Menlo,
  Consolas,
  monospace;
```

Type scale (marketing scale, proportional to DESIGN.md UI scale):

| Use | Size | Weight | Letter-spacing | Line-height |
|---|---|---|---|---|
| H1 (hero) | 48px (`clamp(36px, 5vw, 56px)`) | 600 | -0.025em | 1.1 |
| H2 (feature) | 32px | 600 | -0.02em | 1.15 |
| H3 (card) | 18px | 600 | -0.01em | 1.3 |
| Body | 16px | 400 | 0 | 1.55 |
| Small | 13px | 400 | 0 | 1.5 |
| Eyebrow | 12px | 500 | 0.18em | 1.4 (uppercase) |

**Rules** (DESIGN.md §2.3): titles use SemiBold (600), everything else Normal (400). No Bold (700), no Light (300).

### 5.3 Layout

- Max content width: **1120px**, horizontally centered
- Section vertical padding: **96px desktop, 64px mobile**
- Section separators: whitespace only (no horizontal rules — DESIGN.md philosophy)
- Card padding: **24px**

### 5.4 Shape (DESIGN.md §4)

| Element | Radius |
|---|---|
| BrowserFrame content area | **10px** (matches `kContentCornerRadius`) |
| Card / button | **12px** (matches `--radius`) |
| Site icon mockup inside BrowserFrame | **8px** |

### 5.5 Interaction

- Button hover: 100ms ease background-color change to `--accent-hover` (or `var(--surface)` for ghost buttons)
- Link hover: 150ms underline offset reveal
- Card hover: 200ms shadow lift (subtle)
- **No scroll-driven animations.** No fade-in on viewport. No parallax.
- All animations gated by `@media (prefers-reduced-motion: reduce)` — disabled when user prefers reduced motion (DESIGN.md §5.4)

### 5.6 Iconography

Lucide only, fetched per icon from upstream when first introduced. The wrapper component:

```tsx
// components/ui/LucideIcon.tsx
type IconName = 'panel-left' | 'folder' | 'pin' | 'command' | 'search' | 'sparkles'
              | 'brain' | 'zap' | 'mouse-pointer-2'
              | 'picture-in-picture-2' | 'columns-2' | 'sliders-horizontal'
              | 'square' | 'palette' | 'chrome';
// renders <svg width=size height=size viewBox="0 0 24 24" fill="none"
//        stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
//   {/* path data fetched verbatim from lucide upstream */}
// </svg>
```

Path data must be fetched via:

```bash
curl -s https://raw.githubusercontent.com/lucide-icons/lucide/main/icons/<name>.svg
```

and pasted **verbatim** into the wrapper. Never hand-write or "simplify" path data (DESIGN.md §6.4).

### 5.7 Responsive Breakpoints

```
sm: 640px   md: 768px   lg: 1024px   xl: 1280px (desktop default)
```

Behaviors:
- Hero CTAs stack vertically on `< sm`
- Feature sections collapse to single column (mockup on top, text below) on `< md`
- Feature grid: 3×2 → md 2×3 → sm 1×6
- Footer: 4 → 2 → 1 columns
- Top nav: links collapse to a hamburger button on `< sm` (button shows a popover with the same links — popover is CSS `:popover-open` API, no JS state)

## 6. Data Flow

### 6.1 Build-time Constants

The website reads version metadata from the main repo's `dao.json` at build time:

```ts
// website/lib/version.ts
import daoJson from '../../dao.json' assert { type: 'json' };

export const PRODUCT_VERSION  = daoJson.version.display;   // e.g. "0.5.0"
export const CHROMIUM_VERSION = daoJson.version.version;   // e.g. "147.0.7727.135"
export const GITHUB_URL       = "https://github.com/moonrailgun/dao-browser";
export const SITE_URL         = "https://dao.msgbyte.com";
export const DOWNLOAD_URL_MAC_ARM64 =
  `${GITHUB_URL}/releases/download/v${PRODUCT_VERSION}/dao-browser-${PRODUCT_VERSION}-mac-arm64.dmg`;
```

**Validation**: `lib/version.ts` validates the shape of `dao.json` at module load (small zod or hand-written guard). On a malformed file, the build fails with a clear message rather than silently rendering `undefined`.

### 6.2 Theme

Theme is driven entirely by `prefers-color-scheme`. There is no toggle, no `localStorage`, no theme JS. The server-rendered HTML carries no theme class, so there is no mismatch / FOUC risk.

### 6.3 Responsive

CSS media queries only. No `useMediaQuery` hook, no resize observers.

### 6.4 Client-side State

Target: **zero `useState`, zero `useEffect`, zero `'use client'` directives** in this iteration. If a future feature truly needs interactivity (e.g., video autoplay control), the client component is added in isolation rather than the whole tree.

### 6.5 SEO / Metadata

```ts
// website/app/layout.tsx
export const metadata: Metadata = {
  metadataBase: new URL("https://dao.msgbyte.com"),
  title: "Dao Browser — An opinionated browser.",
  description: "An opinionated browser, built on Chromium. Vertical tabs, soft corners, content first.",
  openGraph: {
    title: "Dao Browser",
    description: "An opinionated browser, built on Chromium.",
    url: "https://dao.msgbyte.com",
    siteName: "Dao Browser",
    images: ["/og.png"],
  },
  icons: { icon: "/favicon.svg" },
};
```

OG image (`/public/og.png`) is a 1200×630 static PNG for v1. Dynamic per-release OG generation is explicitly out of scope.

### 6.6 Link Behaviors

| Link | Behavior |
|---|---|
| `Download for Mac` | `<a href={DOWNLOAD_URL_MAC_ARM64} download>` triggers download |
| `Star on GitHub` | `<a href={GITHUB_URL} target="_blank" rel="noopener noreferrer">` |
| Internal anchors (`#features`, `#download`) | Smooth scroll via CSS `scroll-behavior: smooth` on `html` |
| External (Arc, Chromium credits) | Same `target="_blank"` + `rel="noopener noreferrer"` |

## 7. Error Handling

| Scenario | Outcome | Mitigation |
|---|---|---|
| `dao.json` field missing or malformed | Build error with line number | Validation in `lib/version.ts` |
| Real screenshot file missing | Hero still renders | `BrowserFrameMockup` is pure CSS — no image deps until real screenshots are added |
| Lucide icon name typo | Compile error | `LucideIcon` accepts a TypeScript union of valid names; runtime fallback is a 24×24 dashed-border placeholder |
| Download URL 404 (release not yet published) | Visible to user | Hint line below CTA shows current version; richer Release-API check is out of scope |
| User hits an unknown path | Friendly 404 | `app/not-found.tsx` — minimal page, "back to home" link |

## 8. Testing

This iteration ships **no automated unit / E2E tests**. It does ship the following automatic checks:

| Check | Tool | Trigger |
|---|---|---|
| TypeScript compile | `tsc --noEmit` | `npm run check` (manual) and pre-build |
| Lint | Next.js built-in ESLint (`next lint`) | Same as above |
| Build smoke | `next build` succeeds | CI / pre-deploy |

**Manual QA checklist** lives in `website/README.md` and is run before each deploy:

- [ ] Light & dark mode both render every section without contrast regressions
- [ ] Mobile (375px) — no horizontal overflow, all CTAs reachable
- [ ] Tablet (768px) — feature sections single-column, no orphaned mockups
- [ ] Desktop (1280px+) — content centered at max-width 1120px
- [ ] All Lucide icons render (no empty boxes, no fallback placeholders)
- [ ] BrowserFrame shadow is visible in both themes
- [ ] OG share preview at https://dao.msgbyte.com correctly shows og.png
- [ ] `Download for Mac` triggers download (or shows browser save dialog)
- [ ] `Star on GitHub` opens new window
- [ ] `prefers-reduced-motion: reduce` disables hover transitions
- [ ] Top nav transparent at scrollY=0, gains border + blur on scroll

## 9. Performance Budget

| Metric | Target |
|---|---|
| LCP | < 1.5s on cable connection |
| Total client JS (gzip) | < 30 KB |
| Total image weight | < 200 KB (≈ 0 KB during mockup phase) |
| Lighthouse Performance | > 95 |

Implementation guarantees:
- All RSC, zero client components → only Next.js framework runtime ships
- Zero web fonts → no network font request
- All icons inline SVG → no image requests for icons
- Hero mockup is CSS-painted → zero hero image weight
- When real screenshots replace the mockup, use Next.js `<Image>` with `priority` on the hero

## 10. Deployment

**Target**: Cloudflare Pages

| Setting | Value |
|---|---|
| Build command | `cd website && npm install && npm run build` |
| Build output | `website/out` |
| Root directory | `/` (repo root) |
| Custom domain | `dao.msgbyte.com` (CNAME) |
| Node version | 20 (matches main repo's `>= 18` minimum, picks current LTS) |

Local commands:

```bash
npm run website                  # main repo forwarder → cd website && npm run dev
cd website && npm run build      # produces website/out/
cd website && npm run start      # local serve of static export (next start with output:export)
```

No environment variables for v1. No secrets.

## 11. Future Work (Tracked, Not Built)

- Multi-language toggle (English / Chinese)
- Release Notes route
- Animated / interactive product mockups
- Per-release dynamic OG image
- Linux / Windows download builds + buttons
- Lighthouse CI threshold gating in PR checks
- Email subscription for release announcements
- Privacy-first analytics (Cloudflare Web Analytics)
