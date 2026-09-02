# Dao Browser Website

Official landing page for [Dao Browser](https://github.com/moonrailgun/dao-browser), deployed at <https://dao.msgbyte.com>.

## Stack

- Next.js 15 App Router (server-rendered; `/download` uses a Route Handler for UA-based redirect)
- React 19 RSC only — zero client JavaScript beyond Next.js runtime
- CSS Modules + design tokens mirrored from main repo `DESIGN.md`
- Lucide icons inlined as SVG (path data fetched verbatim from upstream)
- OG image generated at build time via `next/og` (no external image tools needed)

## Commands

```bash
npm install        # first-time setup
npm run dev        # http://localhost:3000
npm run build      # produces ./.next (server build)
npm run start      # serve the production build locally
npm run check      # tsc --noEmit
npm run lint
```

From the repo root, `npm run website` is a shortcut for `cd website && npm run dev`.

## Source of Truth

- Product version, Chromium version, and latest download URL → `public/info.json`
- Version history metadata and R2 enclosure URLs → `public/appcast.xml`
- Historical download URLs → GitHub Releases, derived by removing the fixed
  trailing `.0` from each appcast `shortVersionString`
- Design tokens → `app/globals.css` (mirrors `../DESIGN.md` §10.1)
- Icons → `components/ui/LucideIcon.tsx` (path data verbatim from <https://lucide.dev>)
- OG image → `app/opengraph-image.tsx` (rebuilt on every `npm run build`)

## Deploy (Vercel)

The site needs a Node runtime — `/download` is a server Route Handler that reads
the User-Agent and 302-redirects to the right platform DMG. Cloudflare Pages
static hosting does NOT work for this; deploy to Vercel (or any host that runs
Next.js's Node/Edge server).

| Setting | Value |
|---|---|
| Framework preset | Next.js (auto-detected) |
| Root directory | `website` |
| Build command | `npm run build` (default) |
| Output directory | `.next` (default) |
| Install command | `npm install` (default) |
| Node version | 20 |
| Custom domain | `dao.msgbyte.com` |

Local `website/vercel.json` only carries the `$schema` reference — all
deploy settings live in the Vercel project dashboard.

To publish a new release: edit `public/info.json` (version + platform URLs).
The Route Handler reads it at request time; no rebuild required.

The newest row on `/history` uses its R2 enclosure URL. Older rows use the
matching GitHub Release asset URL. Before deleting historical DMGs from R2,
run `npm run release:github:backfill` at the repository root and confirm every
referenced asset has been archived successfully. For the first rollout of
this behavior, finish that backfill before deploying the website change so
existing history links already have matching GitHub assets.

## Manual QA checklist

Run before each deploy:

- [ ] Light & dark mode both render every section without contrast regressions
- [ ] Mobile (375px) — no horizontal overflow, all CTAs reachable
- [ ] Tablet (768px) — feature sections single-column, no orphaned mockups
- [ ] Desktop (1280px+) — content centered at max-width 1120px
- [ ] All Lucide icons render (no empty boxes, no fallback placeholders)
- [ ] BrowserFrame shadow visible in both themes
- [ ] OG share preview at https://dao.msgbyte.com correctly shows the generated PNG
- [ ] `Download for Mac` link triggers download / save dialog
- [ ] `Star on GitHub` opens new window with `rel="noopener"`
- [ ] `prefers-reduced-motion: reduce` disables hover transitions
- [ ] Top nav transparent at scrollY=0, gains border + blur on scroll
- [ ] `npm run check` passes
- [ ] `npm run lint` passes
- [ ] `npm run build` succeeds with no errors

## Future work (not in v1)

See `../docs/superpowers/specs/2026-05-03-website-design.md` §11.
