# Dao Browser Website

Official landing page for [Dao Browser](https://github.com/moonrailgun/dao-browser), deployed at <https://dao.msgbyte.com>.

## Stack

- Next.js 15 App Router static export (`output: 'export'`, output in `./out`)
- React 19 RSC + lightweight client download detector on `/download-browser`
- CSS Modules + design tokens mirrored from main repo `DESIGN.md`
- Lucide icons inlined as SVG (path data fetched verbatim from upstream)
- OG image generated at build time via `next/og` (no external image tools needed)

## Commands

```bash
npm install        # first-time setup
npm run dev        # http://localhost:3000
npm run build      # produces ./out (static export)
npm run check      # tsc --noEmit
npm run lint
```

From the repo root, `npm run website` is a shortcut for `cd website && npm run dev`.

## Dependency audit

The scoped minimatch override upgrades modern consumers to the patched 11.x API.
ESLint and several plugins still require minimatch 3's callable CommonJS export;
a global override to 11.x breaks those consumers.

`npm audit` also flags minimatch 3.1.5 under
[GHSA-g3hv-75wg-m249](https://github.com/advisories/GHSA-g3hv-75wg-m249).
The installed 3.1.5 source lacks the `firstPhasePreProcess`,
`secondPhasePreProcess`, and `optimizationLevel` implementation described by the
[maintainer's advisory](https://github.com/isaacs/minimatch/security/advisories/GHSA-g3hv-75wg-m249).
The affected range is being questioned in
[advisory-database#10811](https://github.com/github/advisory-database/issues/10811).
Keep this development-only finding visible pending upstream clarification or a
compatible plugin upgrade; it is not an audit exemption or a general claim that
minimatch 3 is safe. `npm audit --omit=dev` checks the deployed dependency tree.

## Source of Truth

- Product version, Chromium version, and latest download URL → `public/info.json`
- Version history metadata and R2 enclosure URLs → `public/appcast.xml`
- Historical download URLs → GitHub Releases, derived by removing the fixed
  trailing `.0` from each appcast `shortVersionString`
- Design tokens → `app/globals.css` (mirrors `../DESIGN.md` §10.1)
- Icons → `components/ui/LucideIcon.tsx` (path data verbatim from <https://lucide.dev>)
- Download page translations → `locales/en/download.json` and `locales/zh-CN/download.json`
- OG image → `app/opengraph-image.tsx` (rebuilt on every `npm run build`)

## Deploy (Cloudflare Pages)

The site builds as a static Next.js export (`website/out`) and deploys to
Cloudflare Pages via `.github/workflows/deploy-website.yml`
(`cloudflare/wrangler-action@v3`).

- `public/_redirects` redirects `/download` (`302`) to `/download-browser` and
  `/changelog/zh-CN` (`308`) to `/changelog`. Local `next dev` mirrors these
  redirects in `next.config.mjs`.
- `public/_headers` sets `Cache-Control: no-store`,
  `CDN-Cache-Control: no-store`, and `Cloudflare-CDN-Cache-Control: no-store`
  on `/windows-update.json`, and `Content-Type: image/png` on `/opengraph-image`.
- Only macOS automatically downloads the configured Mac build; other devices
  choose from the platform links in `public/info.json`. iPhones and iPads,
  including iPads with a desktop Mac user agent, use the manual page. A missing
  Mac build never falls back to another platform's installer.

| Setting | Value |
|---|---|
| Cloudflare Pages project | `dao-website` |
| Production branch | `main` |
| Root directory | `website` |
| Build command | `npm run build` |
| Output directory | `website/out` |
| Node version | 24 |
| Custom domain | `dao.msgbyte.com` |

Required GitHub Actions secrets: `CLOUDFLARE_API_TOKEN` (scoped to
`Account -> Cloudflare Pages: Edit`) and `CLOUDFLARE_ACCOUNT_ID`.

To publish a new release: edit `public/info.json` (version + platform URLs)
and push to `main` to trigger deployment. The download page also fetches
`/info.json` at runtime.

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
- [ ] `/download` automatically downloads on macOS, with manual links still available
- [ ] Windows, Linux, Android, iPhone, iPad (including desktop mode), and unknown devices see manual links without an automatic download
- [ ] Manual links show each configured platform and its own version; missing Mac builds and failed configuration requests do not download a different installer
- [ ] Download page copy follows English/Chinese browser language, with English fallback
- [ ] `Star on GitHub` opens new window with `rel="noopener"`
- [ ] `prefers-reduced-motion: reduce` disables hover transitions
- [ ] Top nav transparent at scrollY=0, gains border + blur on scroll
- [ ] `npm run check` passes
- [ ] `npm run lint` passes
- [ ] `npm run build` succeeds with no errors

## Future work (not in v1)

See `../docs/superpowers/specs/2026-05-03-website-design.md` §11.
