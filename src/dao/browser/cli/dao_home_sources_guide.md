# Dao Home declared sources

Declare each connector in `manifest.json`, keep its JavaScript module and JSON
schema in the project, and keep permission origins, paths, and capabilities as
narrow as its purpose needs. A minimal declaration is:

```json
{"id":"feed","module":"connectors/feed.js","schema":"schemas/feed.json","permissions":{"origins":["https://example.com"],"paths":["/feed"],"capabilities":["read_dom"],"mode":"read"}}
```

A connector module exports a default object with `async collect(page)`. Its
restricted page capability supports the existing Home connector runner; it is
not an arbitrary browser/Node environment. Follow declared read-only DOM scope,
schema, item and byte limits. Return only the content the user requested. Never
read sensitive controls or credentials, broaden access to bypass denial, or
substitute discovery/trending content for a missing personalized feed.

Use `home preview --workspace W` to ask for any native source permissions in the
preview window, test connectors, then validate the actual generated runtime.
Collection uses that exact regular Profile's signed-in state. Cookie bytes and
raw connector payloads never enter CLI responses or the checkout. CLI flags
cannot grant permission. `home sources` only lists state; `home sources test ID`
explicitly retests a staged connector through the same native permission rules.

Read statuses distinctly: an empty successful result is an empty state; missing
permission, required sign-in, schema failure, timeout, and execution failure are
not successful tests. New/changed connectors must pass before publication.
Unchanged unavailable sources may be reported as warnings only when Dao returns
`publishable: true`. Fix the source, restore sign-in with the user, or remove the
failing new connector and preview again. Never manufacture a successful result.

Source API details are constrained by the installed runtime. Start from existing
working connectors when present. A minimal array schema is
`{"type":"array","items":{"type":"object"}}`; tighten properties to the data
your page renders. Handle errors independently per source and cache successful
arrays only in `dao.session` memory, with no timers or background collection.

## Connector page capability

Every operation is asynchronous. Available operations are `navigate(url)`,
`waitFor(selector)`, `exists(selector)`, `query(selector)`, `queryAll(selector,
fields)`, `getText(selector)`, `getAttribute(selector, name)`,
`getComputedStyle(selector, names)`, `scroll(amount)`, and `snapshot(selector)`.
Navigation stays inside approved origins/paths. DOM reads need `read_dom`,
computed style needs `read_style`, and scroll needs `scroll`. Run operations
sequentially; the runner rejects concurrent page calls and bounds the number of
operations. There is no evaluate/click/type capability.

`query`/`snapshot` return bounded plain records (`text`, `tag`, `href`, `media`),
not DOM nodes. `queryAll` can map each semantic item container to fields:

```javascript
export default {
  async collect(page) {
    await page.navigate('https://example.com/feed');
    return await page.queryAll('article.feed-item', {
      title: ['h2', '.title'],
      url: '$href',
      image: '$media'
    });
  }
};
```

Replace these example selectors with the source's real semantic containers.
Field selector arrays are fallbacks within each item. `$text` reads item text,
`$href` reads a contained link, and `$media` yields an opaque media handle.
Do not select password controls. Attribute reads are restricted to href, src,
alt, title, datetime, aria-label, and role; src yields a media handle, never
credential-bearing or unrestricted raw source access. A missing signed-in feed
must remain unavailable/empty, not fall back to all links on the page.
