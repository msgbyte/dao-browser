# Dao Home Personal Web Runtime — Design

**Date:** 2026-08-13
**Status:** Approved design, pending implementation plan
**Platform:** Desktop Chromium (macOS arm64) first

## Summary

`dao://home` is a hidden, URL-only product surface where a user creates one
personal website with the existing Dao Agent. The website can contain multiple
pages and routes, and its HTML, CSS, JavaScript, assets, and generated source
connectors are owned by the user. Source code is always viewable, versioned,
exportable, and re-importable, but it is intentionally secondary to the normal
natural-language experience.

The generated Home application can compose live content from websites where
the user is already signed in. It calls a narrow `dao.*` runtime API. A trusted
native capability broker then opens an invisible temporary `WebContents`, runs
an AI-generated connector in a separate sandbox against a restricted page
facade, validates the connector's structured result, and returns that result to
the Home application for rendering.

Source content is current and ephemeral. It is collected only while
`dao://home` is the active page, is discarded when the user leaves Home, and is
never maintained as a historical feed database. The persistent artifact is the
Home project code. AI maintenance of that code happens only after an explicit
user request; Dao never invokes a model or edits Home automatically.

## Product Definition

> Dao Home is a user-owned personal website whose interface and source
> connectors are created and maintained with AI, while its live content is
> assembled from the web at the moment the user opens it.

This definition keeps three concepts separate:

1. **History bootstraps the interface.** An explicit empty-state shortcut may
   use the most recent 30 days of browsing history to propose the first Home.
2. **Live websites fill the interface.** Generated connectors collect current
   source content when Home is open.
3. **AI maintains code, not a content archive.** The user can ask Dao to change
   or repair the persistent Home project, but source payloads are not retained.

## Goals

- Let a user create a personal multi-page website by talking to the existing
  Dao Agent.
- Let generated Home JavaScript use narrow browser-native capabilities without
  receiving privileged WebUI access, cookies, credentials, or raw browser
  objects.
- Let AI generate source-specific JavaScript connectors rather than requiring a
  Dao release for every supported website.
- Collect authenticated, current website content without navigating away from
  the active Home page.
- Keep every Home modification versioned, reviewable, reversible, exportable,
  and re-importable.
- Keep source code available as an ownership and escape hatch while presenting
  natural language as the primary interface.
- Reuse the existing right-side Dao Agent and dynamically provide Home-specific
  atomic tools only while Home is active.
- Make failures local and recoverable so a broken card or generated application
  cannot break the trusted Home host or the Agent.

## Non-Goals

- Replacing the new-tab page, startup page, sidebar, welcome page, or settings
  entry in the first release.
- Exposing any discovery entry other than typing `dao://home` directly.
- Building a second chat UI, second Agent runtime, or Home-specific prompt box.
- Refreshing sources while Home is closed, inactive, or the browser is not
  running.
- Polling sources on a timer or maintaining a background crawler.
- Persisting feed items, source snapshots, reading history, or connector result
  archives.
- Automatically calling a model to repair, redesign, or otherwise maintain
  Home.
- Giving generated code direct Mojo, WebUI, cookie, storage, DevTools, or
  arbitrary script-execution access.
- Providing a full in-browser IDE. Source viewing and diffs are supported;
  natural language remains the primary editing interface.
- Exporting a deployable backend that reproduces Dao's authenticated source
  collection outside Dao.
- A connector marketplace, shared templates, cloud synchronization, or cloud
  execution in v1.
- Website write-back in v1. Future write actions require separate capabilities,
  risk review, and explicit user confirmation.

## Decisions Made During Brainstorming

| Question | Decision |
|---|---|
| Product entry | Hidden `dao://home`; direct URL entry only |
| Project model | One Home project per regular Profile; multiple internal pages and routes |
| Primary UX | Natural-language interaction through the existing right-side Agent |
| Source visibility | Complete source, diffs, versions, export, and re-import are available but visually secondary |
| Generated code | Real HTML, CSS, and JavaScript, not a fixed component-only schema |
| Native access | Generated Home code calls a narrow `dao.*` capability API |
| Connector model | AI-generated JavaScript runs in a connector sandbox against a restricted page facade |
| Target page | Invisible temporary `WebContents` using the current Profile session |
| Home tool availability | Injected only when the exact active page belongs to `dao://home/*`; removed otherwise |
| Source-operation entry | Every create, connect, edit, and repair request starts from Home, never from a source website |
| Missing login | Pause in Home, open a normal visible source tab for manual sign-in, then return to Home and explicitly continue |
| Refresh lifecycle | One current collection when a visible card loads; manual Home refresh recollects; no timers |
| Source payload lifetime | Current Home session only; discard on leaving or closing Home |
| History bootstrap | Explicit empty-state action; most recent 30 days; used to design Home, not to provide displayed content |
| AI maintenance | User-triggered only; errors are reported but never sent to a model automatically |
| Low-risk changes | Validate, publish immediately, and create an undo version |
| High-risk changes | Permission diff and preview require explicit confirmation |
| Export | Complete project package; static use outside Dao; live `dao.*` capabilities require Dao |
| Import | Restore project and versions, but never restore source grants or credentials |

## User Experience

### URL-only entry

The first release registers `dao://home/` and its internal paths, but does not
link to them from any Dao-owned UI. The address bar is the only entry. Navigating
to `dao://home/anything` resolves within the same Home project.

The right-side Agent remains the existing Dao Agent. Opening Home does not
automatically open the Agent and does not start an LLM request.

### Empty Home

An empty Home contains only a restrained trusted empty state:

- **Create with Dao**
- **Create from my browsing history**

Both actions open the existing right-side Agent and submit or prefill a
Home-scoped request. Home does not implement another composer or chat history.

#### Create with Dao

The Agent asks only for information required to make a first version, writes a
project draft with Home tools, validates it, and publishes it as version 1.

#### Create from browsing history

This is an explicit, one-shot action. It queries the most recent 30 days of
local history and builds a privacy-minimized material pack containing:

- domain;
- deduplicated page titles;
- visit counts;
- coarse time buckets;
- no full URLs by default.

The user's configured model uses the material pack to propose the information
architecture, visual emphasis, and candidate source cards. The analysis result
is not retained after the first project version is created.

History never grants source access. A candidate X or Bilibili card starts in a
disconnected state until the user connects it from Home.

### Existing Home

On opening an existing Home:

1. The trusted host loads the last published project version.
2. The generated application renders without an LLM call.
3. Each visible source card requests a current collection.
4. Duplicate requests for the same connector and input are coalesced for the
   active Home session.
5. Results render in the generated application and remain in memory only.

Reloading Home recollects. Leaving Home cancels collection and clears all source
payloads. Reopening Home starts from the persistent code and fresh sources.

### Adding a source

All source operations begin with Home active. For example, the user tells the
right-side Agent:

> Bring my X feed into Home as a card.

The flow is:

1. The contextual Agent creates a connector draft and result schema.
2. The trusted host presents the connector's requested site scope and page
   capabilities.
3. The user confirms from Home.
4. The connector runs against an invisible temporary source page.
5. The Agent uses a validated sample result to build the Home card.
6. The card and connector publish atomically as one project version.

No Home tool is exposed while the user is on X, Bilibili, or another ordinary
web page. There is no "add current page to Home" action outside Home.

### Missing login

If the temporary page resolves to a login or access-denied state:

1. Collection returns `auth_required` without reading or filling credentials.
2. Home shows a disconnected source state and a **Connect** action.
3. The action opens a normal visible tab at the approved source URL.
4. The user completes sign-in directly with the website.
5. The user returns to `dao://home`.
6. The user explicitly retries or asks the Agent to continue.

Home tools are absent while the source tab is active. A pending source draft
may remain in the project service, but it cannot publish or collect until Home
is active again.

### Editing Home

The user can describe a change in the right-side Agent, such as:

- "Move the X feed to the left."
- "Make this card use three columns."
- "Hide engagement counts."
- "Why did this card stop loading?"
- "Restore the previous version."

Major generated elements carry stable node IDs. A project-owned node map links
those IDs to source files and symbols. Selecting an element before asking for a
change lets the Agent resolve the visual target to source without searching the
entire project.

### User-triggered maintenance

Runtime and connector errors produce diagnostics and visible card states, but
never trigger an LLM request. Maintenance starts only when the user chooses
**Ask Dao to fix** or explicitly asks the Agent to inspect or repair Home.

The Agent then reads bounded diagnostics, recollects a current source sample if
needed, modifies code through Home tools, validates the draft, and publishes or
requests confirmation according to the risk model.

## Architecture

```
┌────────────────────────────────────────────────────────────────────┐
│ dao://home Trusted Host                                            │
│                                                                    │
│  ┌─────────────────────────────┐     ┌───────────────────────────┐  │
│  │ Generated Home Application  │ RPC │ Home Capability Broker    │  │
│  │ untrusted document          ├────►│ schema + grant + lifecycle│  │
│  └─────────────────────────────┘     └─────────────┬─────────────┘  │
│                                                   │                │
└───────────────────────────────────────────────────┼────────────────┘
                                                    │
                         ┌──────────────────────────┴───────────────┐
                         │ DaoHomeConnectorExecutor                 │
                         │                                         │
                         │  connector sandbox ──restricted RPC──┐   │
                         │                                      ▼   │
                         │  invisible temporary WebContents +      │
                         │  fixed audited DOM adapter               │
                         └───────────────────────────────────────────┘

┌─────────────────────────────┐   ┌─────────────────────────────────┐
│ DaoHomeProjectService       │   │ Existing right-side Dao Agent   │
│ files, versions, grants,    │◄──│ contextual home_* tool pack     │
│ diagnostics, transactions   │   │ available only on dao://home/*  │
└─────────────────────────────┘   └─────────────────────────────────┘
```

### Trusted Home host

The outer `dao://home` document is fixed Dao-owned code. It owns:

- project load and route resolution;
- the generated-application container;
- capability requests and confirmation UI;
- source and runtime status;
- preview, publish, rollback, export, and import coordination;
- crash recovery that remains available when generated code fails.

Generated code must not execute in the privileged WebUI origin. It runs in a
separate untrusted document following Chromium's untrusted-WebUI pattern. The
exact internal scheme is an implementation detail, but the security invariant
is not: generated code receives no direct WebUI message channel or native
bindings.

### Generated Home application

The generated application owns the user's presentation and application logic:

- HTML, CSS, and JavaScript;
- internal pages and routes;
- card composition and interaction;
- requests to source connectors;
- in-session UI state.

The untrusted document uses a restrictive CSP. Arbitrary network access is not
available by default. External navigation and source media are brokered so the
application cannot silently exfiltrate collected content.

The initial runtime surface is deliberately small:

```js
dao.sources.collect(connectorId, input?)
dao.session.get(key)
dao.session.set(key, value)
dao.navigation.open(url)
dao.media.resolve(handle)
```

- `dao.sources.collect` returns schema-validated JSON plus opaque media handles.
- `dao.session` is memory-only and clears when Home loses active ownership.
- `dao.navigation.open` asks the trusted host to open an HTTP(S) destination.
- `dao.media.resolve` returns a session-scoped blob URL for approved media; it
  does not reveal cookies or credential headers.

Generated application code cannot modify its own project files. Persistent
changes go through contextual Agent tools and project transactions.

### `DaoHomeProjectService`

A new Profile-keyed service is the source of truth for the single Home project.
It is a separate product layer from `DaoAgentWorkspaceService` because Home
needs project revisions, grants, preview transactions, published-version
semantics, and export filtering.

It owns:

- canonical project files and assets;
- current published revision;
- draft transactions;
- version metadata and diffs;
- source permission declarations and grants;
- sanitized, bounded diagnostics;
- import and export.

The implementation may reuse low-level path normalization, text filtering, or
patch parsing primitives where their contracts fit. It must not expose Home
files through ordinary `workspace_*` tools.

### Home capability broker

Every `dao.*` call crosses a typed RPC boundary. The broker validates:

- the calling document belongs to the current generated application;
- its outer owner is the exact active `dao://home/*` tab;
- the request belongs to the current Profile and project revision;
- the requested connector and capability exist in the published manifest;
- the user still grants the declared scope;
- request and response budgets;
- cancellation when active ownership changes.

The broker returns typed errors rather than browser objects or exceptions with
sensitive internals.

### Connector executor

The connector executor coordinates three isolated pieces:

1. An AI-generated connector module in a JavaScript sandbox.
2. An invisible temporary `WebContents` using the regular Profile's existing
   session.
3. A fixed Dao-owned DOM adapter that performs a small audited operation set.

Generated connector JavaScript is not injected directly into the target
renderer. Even a Chromium isolated world can observe sensitive page state, so
the connector instead receives a restricted `page` facade over RPC.

The temporary `WebContents`:

- never becomes the active tab or receives focus;
- uses the current regular Profile and its website login state;
- is navigated only within the granted origin and path scope;
- is destroyed after success, failure, timeout, cancellation, or loss of Home
  ownership;
- never exposes cookie values, password fields, storage databases, or raw
  network credentials to the connector or model.

## Generated Connector Contract

### Example connector

```js
export default {
  id: 'x-home-feed',
  source: {
    origins: ['https://x.com'],
    paths: ['/home'],
    capabilities: ['read_dom', 'read_style', 'scroll'],
  },

  async collect(page) {
    await page.waitFor('[data-testid="tweet"]');
    return page.queryAll('[data-testid="tweet"]', {
      author: '[data-testid="User-Name"]',
      text: '[data-testid="tweetText"]',
      media: 'img[src], video',
      link: 'a[href*="/status/"]',
    });
  },
};
```

### Restricted page facade

The v1 page facade may expose bounded versions of:

```text
page.navigate
page.waitFor
page.exists
page.query
page.queryAll
page.getText
page.getAttribute
page.getComputedStyle
page.scroll
page.snapshot
```

It does not expose:

```text
window or document
document.cookie
localStorage, sessionStorage, or IndexedDB
raw network requests or response headers
arbitrary script execution
password or sensitive form values
browser internals, tabs, Mojo, or DevTools
```

The fixed DOM adapter rejects selectors or operations targeting password
controls and other centrally classified sensitive fields. Operations are
counted against the connector's declared budgets.

### Result schema and media

Each connector includes a JSON Schema for its output. The executor rejects:

- values that fail the schema;
- cyclic or non-JSON values;
- excessive item counts;
- excessive field or aggregate byte sizes;
- media counts above the manifest budget;
- unapproved external locations.

Images, video posters, and other source media are represented as opaque,
session-scoped handles. The capability broker resolves a handle to a blob URL
without exposing authenticated request metadata to generated code.

Source text and attributes are always untrusted data. They may render in Home,
but cannot become Agent instructions or invoke tools. When the user explicitly
asks the Agent to inspect a result, it is passed as a clearly delimited,
untrusted sample and cannot grant permissions or publish code by itself.

## Source Permissions

### Grant shape

Permissions are tied to a connector and contain:

- allowed origins;
- allowed path patterns;
- allowed page-facade capabilities;
- read-only or write mode;
- active-Home-only lifecycle;
- resource budgets.

The user confirms the security-relevant scope rather than individual output
field names. A Home confirmation can read:

```text
Connect X Feed

Website: x.com
Page: /home
Capabilities: Read page content; scroll to load more
Runs: Only while dao://home is active
Write access: Not allowed
```

### Grant rules

- Only the trusted Home host can present or record a grant.
- A source website, normal Agent turn, generated application, or connector
  cannot grant itself access.
- A code repair within the existing origin, path, capabilities, mode, and
  budgets needs no new grant.
- Expanding any security-relevant scope requires an explicit permission diff
  and confirmation before publication.
- Revoking a grant disconnects the source immediately and cancels active work.
- Rolling back code never restores a revoked grant.
- Imported projects receive no grants, even when their manifests declare
  sources.

Read-only collection is the complete v1 source contract. The manifest reserves
an access-mode field so a future design can add write-back without weakening
the current boundary.

## Contextual Agent Integration

### One Agent, contextual tools

Dao reuses the existing right-side Agent and its provider, model, memory,
session, tool adapter, and permission settings. There is no second Agent
runtime.

When the exact active tab is `dao://home/*`, the Agent dynamically receives a
Home tool pack. Navigating, switching tabs, closing Home, changing Profile, or
losing the exact project binding removes the pack. Tools disabled by the user
remain disabled even in Home.

The native handler revalidates the active Home binding on every tool call. The
front-end tool list is a usability boundary; the native check is the security
boundary.

### Atomic tool pack

Read and diagnose:

```text
home_get_manifest
home_list_files
home_read_file
home_get_diagnostics
home_get_selected_element
```

Modify and publish:

```text
home_apply_patch
home_add_asset
home_preview
home_publish
home_rollback
```

Manage sources:

```text
home_list_connectors
home_collect_sample
home_test_connector
home_request_source_access
```

Ownership and history:

```text
home_list_versions
home_export_project
```

The model does not receive the entire project automatically. It reads the
manifest, selected-node mapping, diagnostics, and relevant files on demand.

### Element-to-source mapping

Generated major UI nodes carry stable IDs:

```html
<section data-dao-node-id="x-feed-card"></section>
```

The project stores a node map:

```json
{
  "x-feed-card": {
    "file": "src/cards/x-feed.js",
    "symbol": "XFeedCard"
  }
}
```

`home_get_selected_element` resolves the user's current Home selection to this
map. A stale or ambiguous mapping fails closed and asks the Agent to inspect the
project instead of modifying a guessed file.

### Tool-call concurrency

Every mutating tool requires a base revision. The project service rejects stale
revisions and never merges concurrent patches implicitly. A tool call started
for one Home tab cannot retarget another Home tab or project revision after a
navigation.

## Change Risk and Publication

### Low-risk changes

Changes that do not expand permissions may publish immediately after
validation, including:

- layout and styles;
- copy changes through the appropriate i18n mechanism where the trusted host
  supplies user-visible Dao-owned strings;
- routes within the existing Home project;
- existing component logic;
- connector repairs inside the existing grant.

The transaction is:

```text
base revision
  -> apply patch to draft
  -> static validation
  -> isolated preview
  -> permission diff = none
  -> atomic publish
  -> automatic undo version
```

If validation fails, the published version remains unchanged.

### High-risk changes

The following require a preview and explicit confirmation:

- a new source;
- a new origin or broader path;
- a new page-facade capability;
- increased resource budgets above safe defaults;
- a future write-back capability;
- any other new browser-native capability.

The confirmation is owned by the trusted Home host and shows the permission
diff. Agent text cannot substitute for the native confirmation.

## Project Format

Each regular Profile owns at most one project:

```text
home/
├── manifest.json
├── index.html
├── src/
│   ├── app.js
│   ├── styles.css
│   ├── routes.js
│   └── cards/
│       ├── x-feed.js
│       └── bilibili-feed.js
├── connectors/
│   ├── x-feed.js
│   └── bilibili-feed.js
├── schemas/
│   ├── x-feed.json
│   └── bilibili-feed.json
├── assets/
└── dao/
    └── node-map.json
```

An illustrative manifest is:

```json
{
  "format_version": 1,
  "entry": "index.html",
  "routes": ["/", "/feed", "/watch"],
  "connectors": [{
    "id": "x-home-feed",
    "module": "connectors/x-feed.js",
    "schema": "schemas/x-feed.json",
    "permissions": {
      "origins": ["https://x.com"],
      "paths": ["/home"],
      "capabilities": ["read_dom", "read_style", "scroll"],
      "mode": "read"
    }
  }],
  "limits": {
    "max_result_bytes": 1048576,
    "max_items_per_connector": 100
  }
}
```

The implementation owns the exact limits and may choose stricter defaults.
Generated code cannot raise them without a manifest diff and, when applicable,
confirmation.

## Persistence and Data Lifecycle

### Persisted

- project HTML, CSS, JavaScript, and assets;
- routes and node mapping;
- connector code and result schemas;
- permission declarations and local grants;
- published versions, diffs, and summaries;
- bounded non-content diagnostics;
- small project presentation preferences.

### Session-only

- connector result JSON;
- resolved media blobs;
- source-page state;
- in-session generated-application state;
- request coalescing cache;
- current collection samples used for an explicit repair turn.

### Never persisted by Home

- feed or source-content history;
- raw target HTML or DOM snapshots;
- cookies, tokens, passwords, or credential headers;
- hidden temporary page storage copies;
- browsing material used for the one-shot history bootstrap;
- automatic model analysis.

Leaving Home means the exact active tab no longer belongs to `dao://home/*`.
The broker cancels active connector work, destroys temporary pages, revokes
session handles, and clears source payloads. Project code and grants remain.

## Versioning

Each successful publication records:

- immutable revision ID;
- parent revision;
- timestamp;
- natural-language change summary;
- changed-file hashes and diff;
- manifest permission diff;
- creation kind: initial, user request, history bootstrap, source connection,
  repair, or rollback.

Rollback creates a new head revision based on the selected old project state;
it does not delete later history. It restores code and configuration but not
revoked grants. A connector without a currently valid grant renders
disconnected.

Version history is presented summary-first. Source diffs are behind an explicit
secondary action.

## Source Visibility and Export

### Glass-box principle

> Natural language first; source code always available.

Normal Home UI shows the generated application, source status, and lightweight
maintenance state. A secondary menu exposes:

- **Version history**
- **View source**
- **Export project**

The source viewer needs a file tree, read-only syntax view, and version diff. It
does not need to recreate an IDE in v1.

### Export package

The package includes:

- all current source files and assets;
- connectors and schemas;
- manifest and project format version;
- version metadata;
- a README explaining Dao runtime dependencies;
- an optional current static-render snapshot selected by the user.

It excludes:

- cookies, tokens, credentials, and grants;
- Agent conversations, provider configuration, and API keys;
- connector result caches and live source content unless the user explicitly
  chooses the static snapshot;
- internal browser paths or machine-specific secrets.

Without Dao, the exported static page remains inspectable and the optional
snapshot remains viewable, but `dao.*` live capabilities are unavailable.

### Import

Import validates the package format, paths, file sizes, manifest, connector
schemas, and code budgets in a draft. It never imports grants. After preview
and user confirmation, it replaces the one Home project as a new version. The
previous project remains recoverable in version history.

## Error Handling

### Card-local source states

One connector failure cannot fail the generated application. A source card can
render:

```text
loading
ready
auth_required
permission_required
timed_out
site_changed
invalid_response
temporarily_unavailable
```

Normal actions are **Retry**, **Connect**, and **Details**. **Ask Dao to fix**
is an explicit transition into an Agent maintenance turn.

### Generated-application failure

The trusted host remains alive if generated code has a syntax error, throws,
loops, exceeds budgets, or crashes its renderer. It can terminate the untrusted
application and show:

- reload;
- return to previous version;
- Ask Dao to fix.

Dao keeps the last known runnable revision. It does not automatically roll
back, call a model, or edit code.

### Connector diagnostics

The project service may retain a bounded diagnostic record containing:

- connector ID;
- failure stage and typed error code;
- origin and path without query or fragment;
- timestamp;
- missing selector or result-schema field;
- truncated, sanitized console error.

It does not retain page text, feed results, raw HTML, screenshots, cookies, or
headers. When the user requests a repair, the Agent can ask the connector
executor for a new session-only sample.

### Resource enforcement

Native limits cover:

- temporary page count;
- wall-clock execution time;
- navigation and scroll count;
- DOM query count;
- item, media, field, and aggregate result sizes;
- connector CPU and memory;
- per-source concurrency.

Exceeding a limit stops the connector, destroys its temporary page, and returns
a typed error. Generated code cannot disable or increase enforcement directly.

## Security and Privacy Invariants

1. Generated Home code never executes with the trusted `dao://home` WebUI
   origin or native bindings.
2. Generated connector code never receives target `window`, `document`, cookie
   values, website storage, raw network credentials, or arbitrary execution.
3. Every native request is bound to the exact active Home tab, Profile,
   published revision, manifest declaration, and current grant.
4. Home-specific Agent tools are not exposed to the model outside Home, and
   native handlers independently enforce the same rule.
5. All source operations begin and complete from Home. A visible source tab is
   used only for manual sign-in.
6. Source content is untrusted data, cannot become instructions, and cannot
   grant permissions or publish code.
7. Source payloads, media handles, and temporary pages are destroyed when Home
   loses active ownership.
8. No model call, code repair, redesign, source refresh, or permission change
   happens automatically.
9. Export and import never transfer credentials or grants.
10. Incognito, Guest, internal pages, extensions, DevTools, `file:`, `data:`,
    and custom schemes are ineligible connector targets in v1.
11. Dao telemetry never contains generated project source, browsing-history
    material, connector payloads, source content, or credentials.

## Verification Strategy

### Unit tests

- manifest parsing, normalization, and version rejection;
- origin and path-scope matching;
- permission-diff classification;
- connector result-schema validation and size enforcement;
- project path traversal and unsafe asset rejection;
- stale base-revision rejection;
- atomic publish and rollback semantics;
- export filtering and import grant removal;
- 30-day history boundary and full-URL minimization;
- session-data clearing.

### WebUI tests

- empty-state actions reuse the existing Agent surface;
- generated code cannot access trusted WebUI bindings;
- Home tool pack appears only for `dao://home/*` and refreshes on active-tab
  changes;
- user-disabled Home tools remain disabled;
- selected node resolves through the node map;
- generated-app crash leaves the trusted host usable;
- card errors remain local;
- source status, version history, source view, and export remain secondary UI.

### Browser tests

- invisible temporary `WebContents` uses the correct regular Profile session
  without becoming active or focused;
- connector success, timeout, cancellation, and teardown;
- active-Home loss cancels work and clears source data;
- cross-window and cross-Profile tool calls fail closed;
- connector attempts to navigate outside its origin or path fail;
- direct attempts to read cookies, website storage, passwords, or raw network
  data fail;
- missing login opens only a user-triggered visible tab and resumes only after
  returning to Home;
- imported projects have no source grants;
- Incognito, Guest, and forbidden targets fail closed.

### Agent tool tests

- tools are absent outside Home;
- every mutation requires the current base revision;
- low-risk patch validates and atomically publishes an undo revision;
- high-risk manifest diff blocks publication pending native confirmation;
- stale, cancelled, or wrong-tab calls cannot retarget;
- ordinary workspace tools cannot read or write the Home project;
- Ask Dao to fix is user-triggered and receives only bounded diagnostics and
  an explicitly requested sample.

### Adversarial integration tests

Use deterministic local test sites rather than live X or Bilibili pages to
cover:

- authenticated feed collection;
- DOM redesign and missing selectors;
- prompt-injection text in source items;
- oversized and cyclic connector output;
- infinite connector loops and excessive scrolling;
- media-handle lifecycle;
- generated Home exfiltration attempts;
- source login expiry;
- rollback after permission revocation.

### Compile and regression verification

Implementation verification must use focused WebUI and browser tests first.
The only permitted compile-confirmation command is `npm run rebuild`; no direct
Chromium build command or alternate npm build path may substitute for it.

An implementation that materially adds this desktop feature must update
`docs/features.md` and `docs/feature-checklist.md` in the same change.

## MVP Scope

The first implementation is complete when a user can:

1. Type `dao://home` and reach the otherwise hidden product surface.
2. Create a first project with the existing Agent or the explicit 30-day
   history shortcut.
3. Render generated HTML, CSS, and JavaScript in an untrusted application
   document with multiple internal routes.
4. Ask the Home-scoped Agent to create one read-only connector and card against
   a deterministic authenticated test site.
5. Approve source scope from Home and collect current content in an invisible
   temporary page without losing Home focus.
6. Observe that leaving Home cancels collection and discards source content.
7. Select a Home element and ask the Agent to change it through an atomic,
   versioned patch.
8. Trigger a connector failure, explicitly ask Dao to repair it, and keep the
   previous runnable version until the repair validates.
9. Roll back a version.
10. Export the transparent project package and re-import it without restoring
    grants.

The MVP does not require production guarantees for any specific third-party
site. X and Bilibili are motivating examples; the security and lifecycle
contracts are proven with deterministic fixtures before any best-effort live
connector validation.
