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
`dao.session.get(key)` and `dao.session.set(key, value)`. Call
`dao.navigation.open(url)` from a click or keyboard handler to open an HTTP(S)
destination directly in a new tab.
Start independent cards concurrently. To display both the first available
result and any automatic update, use
`render(await dao.sources.collect(connectorId, {}, {onUpdate: render}))`
with a render function for that card. A read can reuse a successful result for
30 minutes. From 30 minutes until 6 hours it returns the cached value and
starts a refresh. A successful refresh calls `onUpdate` and replaces the cached
value. Temporary failures and routine cookie renewals keep the previous result.
Cookie, cookie expiry, and local storage changes on a source's site, including
writes from other tabs, discard matching cached results but never interrupt a
read or reload the page. A read that saw such a change still returns its data,
and the next read visits the site again. Writes the source page makes itself
while Home reads it keep the result reusable unless another page of the site is
open. Clearing site data or a native sign-in requirement ends the source
session, even if the connector catches the error: pending reads fail with
`session_changed`, and Home reloads documents that showed the affected data.
A cold read that has yet to show data can retry once after `session_changed`.
Home checks the source document for a sign-in form before delivery.
Issued media handles remain usable after ordinary site data changes.
At 6 hours, including time spent asleep, the read waits for new data.
Structured results can be shared across Home tabs in the same Profile.
Media handles belong to the current Home document.
For an explicit refresh, `{fresh: true}` waits for a live result.
Resolve opaque media handles with `await dao.media.resolve(handle)`; never treat
them as network URLs. Refreshes are triggered by reads on active Home pages.

Use existing source as the baseline. Choose one coherent visual direction for
the user's purpose: clear hierarchy, a consistent spacing grid, responsive
breakpoints, deliberate typography, a restrained color palette, and accessible
keyboard interactions. Prefer icon buttons for familiar actions. Copy the
upstream Lucide SVG into the project or inline it in a native button. Give
each icon-only button an accessible name and hide the decorative SVG from
assistive technology. Add a visible label when the action needs explanation.
Prefer content and alignment over decoration. Avoid
invented statistics, filler content, emoji icons, generic card grids, and
animation without a useful response. Render truthful loading, empty, populated,
and unavailable states. Do not hide connector failures behind made-up content.
Review layout, function, responsiveness, and runtime compliance before preview.

The dedicated real Dao preview is the validation authority. A localhost server,
ordinary Home tab, separate Profile, or browser screenshot cannot substitute for
its runtime/source checks. Ordinary Home windows keep their published content
until publication. Navigating, closing, minimizing, or switching tabs inside the
preview cancels its receipt; explicitly preview again after fixing the cause.
