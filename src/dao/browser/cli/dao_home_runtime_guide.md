# Dao Home runtime contract

A project runs directly as sandboxed HTML, CSS, and plain JavaScript. It has an
opaque origin. Do not create npm, Vite, JSX/TSX, build caches, or CDN dependencies.
A minimal manifest is:

```json
{"format_version":1,"entry":"index.html","routes":["/"],"connectors":[],"limits":{"max_result_bytes":1048576,"max_items_per_connector":100}}
```

Load local application scripts with `defer` (or `type="module"`), for example
`<script src="src/app.js" defer></script>`, so Dao's injected runtime is installed
before application code accesses `dao`. Inline scripts and inline event handlers
fail CSP. The runtime resets default
html/body margins; add intentional layout spacing in project CSS. No cookies,
localStorage, sessionStorage, IndexedDB, fetch, XMLHttpRequest, WebSocket, direct
external links, or arbitrary browser APIs are available. Use memory-only
`dao.session.get(key)` and `dao.session.set(key, value)`. Open a user-confirmed
HTTP(S) destination through `dao.navigation.open(url)` from project JavaScript.
Collect declared sources with `await dao.sources.collect(connectorId, {})`.
Resolve opaque media handles with `await dao.media.resolve(handle)`; never treat
them as network URLs. No polling or background refresh is introduced.

Use existing source as the baseline. Choose one coherent visual direction for
the user's purpose: clear hierarchy, a consistent spacing grid, responsive
breakpoints, deliberate typography, a restrained color palette, and accessible
keyboard interactions. Prefer content and alignment over decoration. Avoid
invented statistics, filler content, emoji icons, generic card grids, and
animation without a useful response. Render truthful loading, empty, populated,
and unavailable states. Do not hide connector failures behind made-up content.
Review layout, function, responsiveness, and runtime compliance before preview.

The dedicated real Dao preview is the validation authority. A localhost server,
ordinary Home tab, separate Profile, or browser screenshot cannot substitute for
its runtime/source checks. Ordinary Home windows keep their published content
until publication. Navigating, closing, minimizing, or switching tabs inside the
preview cancels its receipt; explicitly preview again after fixing the cause.
