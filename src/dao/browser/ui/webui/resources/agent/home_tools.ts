// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {ToolDefinition} from './agent_bridge.js';

export interface HomeToolContext {
  active: boolean;
  revision: string;
  bootstrapKind?: 'history';
}

type Property = {
  type: string;
  description?: string;
  items?: Property;
  properties?: Record<string, Property>;
  required?: string[];
  minItems?: number;
  maxItems?: number;
  enum?: string[];
  additionalProperties?: boolean;
};

let context: HomeToolContext|null = null;

const HOME_PROJECT_CONTRACT = `<dao-home-project-contract>
You are editing a Dao Home project that runs directly in a sandboxed browser
runtime. Follow this contract exactly whenever you use Home tools:

1. Call home_get_manifest before editing. Use its exact revision as
   base_revision. When no project exists, base_revision is the empty string.
   For an existing project, call home_list_files and home_read_file for every
   file you intend to change before using a mutation tool. Never reconstruct
   an existing file from a summary, screenshot, or memory.
2. A first project must add manifest.json and the entry file named by it. This
   is a minimal valid manifest:
   {"format_version":1,"entry":"index.html","routes":["/"],"connectors":[],"limits":{"max_result_bytes":1048576,"max_items_per_connector":100}}
3. Generate files that run directly in a browser: HTML, CSS, and plain
   JavaScript. Do not create a Vite, React, npm, package-manager, JSX, TSX, or
   other build-step project. Do not depend on CDNs or arbitrary network access.
   The runtime clears the default html and body margins before project styles;
   add layout spacing only where the design intentionally needs it.
4. The generated app has an opaque origin and a strict runtime policy:
   - Put JavaScript in a project file and load it with
     <script src="app.js" defer></script>. Inline <script> blocks and inline event handlers are rejected by CSP.
   - Do not use localStorage, sessionStorage, cookies, or IndexedDB. For
     memory-only state use dao.session.get(key) and dao.session.set(key, value).
   - Do not use fetch, XMLHttpRequest, WebSocket, or direct external links.
     Open a user-confirmed HTTP(S) destination with dao.navigation.open(url)
     from project JavaScript. Source data uses declared connectors only.
5. Use home_replace_files when rewriting most or all of one or more existing
   text files. Put every related replacement in the same call so index.html,
   styles.css, app.js, and metadata changes share one atomic draft. Pass the
   complete new contents directly; do not delete and re-add the same path. Use
   home_apply_patch for creating files or making small, exact edits.
   It accepts V4A patch text, not JSON or a prose file listing. Every line of
   an added file must start with +. Creation example:
   *** Begin Patch
   *** Add File: manifest.json
   +{"format_version":1,"entry":"index.html","routes":["/"],"connectors":[],"limits":{"max_result_bytes":1048576,"max_items_per_connector":100}}
   *** Add File: index.html
   +<!doctype html><html><body><main>Hello</main></body></html>
   *** End Patch
   Existing-file edit example, using exact lines returned by home_read_file:
   *** Begin Patch
   *** Update File: index.html
   @@
   -<main>Old</main>
   +<main>New</main>
   *** End of File
   *** End Patch
   An Update File must contain an @@ hunk, match the current source exactly,
   and close with *** End of File. Never use Delete File plus Add File for the
   same path in one patch. Do not delete manifest.json or its entry file.
6. Complete creation or editing in this order:
   home_apply_patch/home_replace_files -> home_preview -> home_publish
   Pass the returned draft_id through preview and publish. Use publish kind
   "initial" for the first ordinary project, "history_bootstrap" for an
   explicit history bootstrap, and "user_request" for later edits.
7. Keep the Home tab selected and do not call general tab or page-navigation
   tools during this workflow. Do not claim that Home changed until
   home_publish returns success. If a tool
   error includes [code: ...], use that stable code to correct the request
   instead of retrying unrelated project formats.
</dao-home-project-contract>`;

// Adapted from the OpenDesign director protocol. This is intentionally bundled
// with Dao Home so end users get a coherent design pass without installing a
// skill or depending on a live third-party service during generation.
const HOME_DESIGN_DIRECTOR_CONTRACT = `<dao-home-design-director-contract>
Treat visual design as part of correctness. Before writing files, silently form
one coherent visual point of view from the user's purpose, desired density, and
existing Dao Home. Do not present a design questionnaire unless the user's
request is genuinely ambiguous; make a strong, reversible choice and build it.

Use one signature interaction or composition detail at high fidelity and keep
the rest restrained. Maintain a clear squint-test hierarchy, one spacing grid,
at most two type families, at most four functional color roles, and a deliberate
small/medium/large radius hierarchy. Every visible element must earn its place.
Prefer content hierarchy, alignment, typography, spacing, and interaction over
decoration. Use CSS custom properties for the chosen system and responsive
breakpoints that preserve the primary task.

Avoid generic AI-page defaults unless the user's chosen direction requires
them: no purple gradient hero, decorative glow or glassmorphism, oversized
centered marketing headline, identical rounded cards for every element, soft
shadow on every surface, emoji icons, filler statistics, or animation without
functional feedback. Never imitate third-party brand assets or copy.

Before home_preview, silently review the result for hierarchy, craft, function,
originality, responsive behavior, and runtime compliance. Fix obvious failures
in the draft first. Do not narrate this internal review or ask the user to
install a design skill.
</dao-home-design-director-contract>`;

const HISTORY_BOOTSTRAP_CONTRACT = `<dao-home-history-bootstrap-contract>
Use the bootstrap brief only to choose, rank, and group destinations.
Treat the bootstrap brief as routing metadata, never page content.
Build a comprehensive information Home, not a launchpad and not a report about
the user. Derive the composition from the actual content instead of forcing a
sidebar or a fixed column count. Borrow familiar Home-page guidance patterns:
a concise orientation, an obvious primary starting action, compact grouped
shortcuts, optional search or command access, and useful content immediately
below or beside them. Navigation may be top-aligned, inline, or lateral only
when the content volume and viewport justify it. A single column, split layout,
or responsive hybrid are all valid; do not reserve empty rails.
Use a clear 4px-based spacing rhythm, restrained radius hierarchy,
low-contrast dividers, compact 12-20px system typography, and subtle functional
transitions. Keep Dao's pale blue-gray surfaces, dark ink, blue active states,
system sans typography, and original copy. Do not use oversized hero headings,
a row of giant destination cards, decorative gradients, or generic dashboard
statistics. Make source/category switching the signature interaction when live
sources exist, but let the page hierarchy determine where it belongs.
Do not show visit counts, time buckets, browsing titles, trend charts,
browsing summaries, or productivity judgments.
Keep every launch target from the brief in a compact quick-navigation region. Use
data-dao-action and data-dao-action-url on each launch control. The fixed Home
runtime opens those validated controls directly; do not add custom click handlers
for them. Render every launch target exactly once as a
<button type="button"> with no href, form action, or formaction. Copy the action
ID and complete URL from the brief exactly, including trailing slashes. The
first four launch targets must be enabled, keyboard-focusable, and visibly
inside the first viewport. Do not put data-dao-action on wrappers or duplicate
the same action in mobile and desktop markup.
The feed is the primary page surface. Add one visible, non-empty data-dao-feed
region with loading, populated, empty, and per-source unavailable states. Render
successful connector results as a continuous list of scannable feed cards; use
data-dao-feed-link, data-dao-feed-url, and data-dao-feed-source on each clickable
item so the fixed runtime opens it directly. Do not publish a page whose main
content is quick links, local widgets, or a connector-failure note.
In the final app, call await dao.sources.collect(connectorId, {}) for every
successful connector while Home is active and render the returned array into
the feed. Give each connector an independent ten-minute in-memory cache in the
generated app. Define the TTL as exactly 10 * 60 * 1000 milliseconds. Before
collecting, read a stable connector-specific cache entry with dao.session.get;
reuse it only when it contains an array and a finite collection timestamp less
than the TTL old. On a miss, malformed entry, or expiry, collect once and store
the successful returned array plus Date.now() with dao.session.set. Cache an
empty successful array, but never cache an exception or unavailable state, and
never create a refresh timer. Source filters and rerenders must reuse the same
cached array instead of collecting again. If an item contains an image handle,
resolve it with
await dao.media.resolve(handle); never treat the handle as a network URL.
Represent every successful connector exactly once in the rendered HTML with an
element carrying both data-dao-source-slot="<connector_id>" and
data-dao-connector="<connector_id>". A source tab, filter, heading, or status
control can be that element. Keep it present even when the current collection
is empty; do not add these attributes for failed or deselected sources. These
markers are required by trusted preview validation and must match the successful
connector IDs exactly.
When no source succeeds, keep the required feed region compact and directional;
do not stretch an empty card across most of the viewport. Show the disconnected
source status beside useful launch content without inventing articles.
Only render live content from connectors that the trusted host approved and
tested. Otherwise render a launch-only or disconnected state.
Use each source candidate's launch_target_id as its connector ID. Candidate
collection_url, content_intent, and content_kinds are generic starting hints,
not a site recipe. For each candidate, choose the site's most specific
meaningful feed surface that you know how to identify, including a same-site
subdomain when appropriate, and submit that exact collection_url,
content_intent, and content_kinds in the grouped source proposal. Use a
personalized intent only when the chosen surface is specifically a signed-in
following, subscription, or activity feed; otherwise use site_feed. The trusted
browser rejects cross-site proposals and binds the proposed URL to the visible
permission decision. Navigate to the proposed collection_url exactly and keep
the collector aligned with its proposed content_intent and content_kinds.
Return only the declared content kinds; a source declaring video must not return text posts,
reposts, profiles, or other merely adjacent activity. For following_feed or
subscription_feed, return only recent content from accounts or channels the
signed-in user follows or subscribes to; never substitute home discovery,
popular/trending, navigation, or promotional links. For activity_feed, return
the signed-in user's actual activity/dashboard items and exclude product
marketing. If the intended personalized surface is signed out or unavailable,
return an empty result instead of a different kind of feed.
For following_feed, subscription_feed, and activity_feed, every result must be
selected from inside a semantic feed-card or activity-event container. Never
fall back to unscoped selectors such as "a[href]", "main a[href]", "body
a[href]", or their plain-anchor equivalents. If no semantic cards exist,
return an empty array. The trusted manifest validator rejects personalized
collectors that contain an unscoped anchor fallback.
That validator is intentionally lexical: the literal selector "a[href]" must
not appear anywhere in a personalized collector, including waitFor strings,
card-root selectors, optionalFields arrays, or helper constants. Inside a card,
use only content-path-constrained anchor selectors and finish an optionalFields
fallback with the runtime root shorthand "href", never with "a[href]".
Treat collector authoring as a one-shot task. The first submitted module must
already contain its complete bounded fallback chain; do not rely on a failed or
empty source test to obtain another generation turn. Before submitting it,
mentally execute every selector against both the site's expected signed-in
surface and a plausible class-name redesign.
For those personalized intents, query semantic card roots with queryAll and an
optionalFields object instead of querying guessed final-title anchors. Give
each field a short ordered selector array: a dedicated title/body candidate
followed by the root "text" shorthand, content-link candidates constrained by
the site's article/video/item URL semantics, and "media" for an optional image.
This lets a collector survive internal class-name changes while keeping every
value scoped to a real feed card. Normalize the structured card rows after the
query and reject rows without a content URL.
Use a comma-separated union of plausible repeated semantic card roots so class
drift can be handled without leaving the personalized feed region. Every
queryAll call in a personalized collector must select those card roots and pass
an optionalFields object. Put the complete comma-separated card-root selector
directly in queryAll as a quoted string literal; never assign it to a variable,
build it dynamically, or pass an identifier as the first argument, because the
trusted host must statically audit the query scope;
never query content links directly at document,
main, body, or html scope. If none of the scoped roots match, return an empty
array. The trusted validator rejects even content-path-constrained whole-page
link fallbacks because a valid item URL does not prove that the item came from
the user's following or subscription surface. Normalize absolute,
protocol-relative, and root-relative href values inside the scoped rows before
checking them, and include every canonical content URL form needed by the
declared content_kinds.
A content-semantic URL predicate must prove an item URL, not merely prove the
site or origin. Never accept a URL because it contains the source hostname,
collection origin, or collection path alone. Explicitly reject collection_url
itself, its origin root, navigation routes, and links whose content path has no
non-empty item identifier. Every accepted branch must describe a complete
content-path family and validate the identifier after that path using string
operations. For example, a video branch may require "/video/" followed by a
non-empty video ID, an opus branch may require "/opus/" followed by a non-empty
post ID, and a root-level activity permalink must validate its first path
segment as an actual item ID. A selector such as
"a[href*='content.example.com/']" is not content-semantic and must never be
used, because it also selects home, navigation, and account links. Apply this
rule to every structured card field.
Pair every content-path family with an exact canonical content host allowlist.
Validate the normalized URL's complete host before inspecting its path; never
discard, truncate, or ignore the host and then accept a matching path from an
account, upload, membership, authentication, or other same-site subdomain. A
URL from member.example.com containing "/video/" is not a video permalink when
the canonical video host is www.example.com. Resolve relative links only
against an explicitly chosen canonical host, and require both the allowed host
and the content-path identifier for every accepted item.
For every source candidate, dynamically author a collector module at
connectors/<launch_target_id>.js. Export a default object with
async collect(page, input). The page object is the only source API and exposes
navigate(url), waitFor(selector, milliseconds), exists(selector),
query(selector), queryAll(selector, optionalFields), getText(selector),
getAttribute(selector, allowedName), getComputedStyle(selector, names),
scroll(amount), and snapshot(selector). Use the candidate's website semantics
to choose resilient selectors and normalize the result to the supplied
schema_source. Do not use fetch or request credentials. The trusted runtime
loads the target in a detached regular-Profile page, so it automatically reuses
the user's existing signed-in session while keeping the page invisible.
The module source must be a standalone, syntactically valid ECMAScript module
that can be imported directly from a JavaScript Blob. Keep generated collector
syntax deliberately simple: use quoted string literals, string methods, and
string concatenation. Do not use regular-expression literals or template
strings; their slash, backslash, and backtick escaping is fragile inside a JSON
patch. Do not reference document, window, location, fetch, XMLHttpRequest, or
any DOM global directly. Before submitting the provisional patch, self-review
every collector for balanced quotes/braces and verify that its only page access
goes through the documented page methods.
For general, non-personalized feeds, queryAll may select final anchor elements
and omit optionalFields; each result is then {text, tag, href, media}.
Personalized feeds must instead select card roots and provide optionalFields,
which maps output names to a descendant CSS selector or to the root shorthands
"text", "href", and "media". A shorthand
may also be an ordered array such as ["media", "img"] for fallback. Never use
invented field descriptors outside these forms. After navigation, wait for a
semantic feed/card/link selector rather than only "body"; if it times out,
continue through the bounded selector fallbacks instead of failing the whole
collector.
Prefer a small ordered selector fallback inside each generated module. For
general feeds, try the site's semantic feed or card containers first, then a
bounded main-region link fallback. Personalized intents must stay scoped to
semantic cards as described above. Normalize only entries with a non-empty
title and HTTP(S) URL, and deduplicate by URL. Prefer a dedicated title element,
title attribute, or
aria-label over the full text of an anchor when that anchor also contains
controls, counters, durations, or other metadata. Do not treat navigation,
pricing, product-promotion, sign-in, or footer links as feed items. An empty
result is valid only after all fallbacks run.
First create a connector-free provisional manifest and UI draft containing all
launch actions plus every authored collector module. Do not author connector
manifest entries or schema files in this provisional draft. Then call
home_request_bootstrap_sources once with one source proposal for every
source_candidates launch_target_id, or sources: [] when source_candidates is
empty. Each proposal contains connector_id, collection_url, content_intent, and
content_kinds. The trusted browser rejects omissions on the first request,
preserves the authored collector modules, and injects the complete candidate set, exact origins,
read-only permissions, and browser-owned schema files before showing access UI.
This normalization prevents malformed model-authored permissions from widening
access while keeping collection logic dynamic. When source_candidates is empty,
the same call enters the disconnected final-building state without showing an
empty permission dialog.
The grouped request waits for the user's decision and has no ordinary
tool timeout; do not issue a second request while it is pending. If an older
request already timed out and a retry reports already_exists, recover by
calling the same provisional draft once with sources: []. Do not create
another provisional draft in that state. Do not preview or publish the
provisional draft. Test each returned connector exactly once, sequentially.
Treat a test result with status \`succeeded\` as eligible for the final project.
Successful collection returns only a bounded \`sample_shape\`: use its JSON kind
and empty/non-empty state together with the connector schema to design live
loading, empty, and populated states. It never contains raw values or dynamic
property names. Never persist connector sample content; the published Home must
collect fresh values from its approved connector at runtime.
For \`authentication_required\`, \`runtime_failed\`, or \`schema_failed\`, omit every
failed connector definition but preserve its launch action. A clearly labeled
disconnected or authentication state is allowed only with no fake sample content.
Then create a new final draft from the published base revision: keep every
launch target as an action, render only successful source IDs, and keep the
final manifest connector-free. Do not copy or recreate connector declarations,
collector modules, or schemas in the final patch. Before preview, the trusted
browser copies each successfully tested module and schema byte-for-byte from
the provisional draft and binds its canonical declaration and limits into the
final draft. This preserves the approved fingerprint without relying on the
model to reconstruct security-sensitive manifest fields.
The native Home host writes canonical experience.json metadata from a small
internal action subset and the final successful connector IDs before preview;
that subset does not define the page layout. Do not spend tool calls repairing
that file. Ensure every launch action exists, with the canonical subset visible
and focusable. If final preview fails, create a replacement final draft from the
same published base and correct its UI; do not call either source-access tool
again and do not recreate or retest the provisional source draft. Then preview
and publish. Continue to an honest feed-empty final
draft when the selected
connector list is empty or every connector test fails. During this history
flow, never call home_request_source_access; only the grouped bootstrap source
tool is valid.
Do not ask the user to send another chat message to continue this sequence.
</dao-home-history-bootstrap-contract>`;

function definition(
    name: string, description: string,
    properties: Record<string, Property> = {},
    required: string[] = []): ToolDefinition {
  return {
    type: 'function',
    function: {
      name,
      description,
      parameters: {type: 'object', properties, required},
    },
  };
}

const BASE_REVISION: Property = {
  type: 'string',
  description: 'The exact current Home revision returned by home_get_manifest.',
};

const HOME_TOOL_DEFINITIONS: ToolDefinition[] = [
  definition(
      'home_get_manifest',
      'Read the current Dao Home revision and validated project manifest.'),
  definition(
      'home_list_files', 'List files in the current Dao Home revision.',
      {revision: {type: 'string'}}, ['revision']),
  definition(
      'home_read_file', 'Read one text file from the current Dao Home revision.',
      {revision: {type: 'string'}, path: {type: 'string'}},
      ['revision', 'path']),
  definition(
      'home_get_diagnostics',
      'Read bounded non-content diagnostics for the active Home session.'),
  definition(
      'home_get_selected_element',
      'Resolve the selected Home node ID through the project node map.'),
  definition(
      'home_apply_patch',
      'Apply a V4A patch to a new validated Home draft. Read the manifest first and generate a directly runnable HTML/CSS/JavaScript Home project with no build step. This never mutates a published revision.',
      {
        base_revision: BASE_REVISION,
        patch: {type: 'string'},
        summary: {type: 'string'},
      },
      ['base_revision', 'patch', 'summary']),
  definition(
      'home_replace_files',
      'Atomically replace one or more existing Home text files with complete contents in one validated draft. Read every current file first. Use this for whole-file rewrites instead of deleting and re-adding paths or creating separate drafts.',
      {
        base_revision: BASE_REVISION,
        files: {
          type: 'array',
          minItems: 1,
          maxItems: 32,
          items: {
            type: 'object',
            properties: {
              path: {type: 'string'},
              contents: {type: 'string'},
            },
            required: ['path', 'contents'],
          },
        },
        summary: {type: 'string'},
      },
      ['base_revision', 'files', 'summary']),
  definition(
      'home_add_asset',
      'Decode one base64 asset into the project assets directory as a validated Home draft.',
      {
        base_revision: BASE_REVISION,
        path: {type: 'string'},
        base64_contents: {type: 'string'},
        summary: {type: 'string'},
      },
      ['base_revision', 'path', 'base64_contents', 'summary']),
  definition(
      'home_preview',
      'Load an unpublished Home draft in the isolated runtime and validate it before publishing.',
      {
        base_revision: BASE_REVISION,
        draft_id: {type: 'string'},
      },
      ['base_revision', 'draft_id']),
  definition(
      'home_publish',
      'Atomically publish a draft that passed isolated preview. Expanded source scopes must also be approved and successfully tested first.',
      {
        base_revision: BASE_REVISION,
        draft_id: {type: 'string'},
        kind: {type: 'string'},
      },
      ['base_revision', 'draft_id']),
  definition(
      'home_rollback',
      'Restore an older Home version as a new immutable head revision.',
      {
        base_revision: BASE_REVISION,
        target_revision: {type: 'string'},
        summary: {type: 'string'},
      },
      ['base_revision', 'target_revision']),
  definition(
      'home_list_connectors',
      'List declared connectors and whether their exact permission scope is granted.'),
  definition(
      'home_collect_sample',
      'Collect one bounded, non-content sample shape from a granted Home connector. Raw connector values remain native and session-only.',
      {connector_id: {type: 'string'}, input_json: {type: 'string'}},
      ['connector_id']),
  definition(
      'home_test_connector',
      'Test a connector from a draft without publishing it. Success returns only a bounded non-content sample_shape.',
      {
        draft_id: {type: 'string'},
        connector_id: {type: 'string'},
        input_json: {type: 'string'},
      },
      ['draft_id', 'connector_id']),
  definition(
      'home_request_source_access',
      'Ask the trusted Home host to show a permission confirmation. This tool cannot grant access itself.',
      {
        base_revision: BASE_REVISION,
        draft_id: {type: 'string'},
        connector_id: {type: 'string'},
      },
      ['base_revision', 'draft_id', 'connector_id']),
  definition('home_list_versions', 'List immutable Dao Home versions.'),
  definition(
      'home_export_project',
      'Export the transparent Dao Home JSON package without grants or live source data.'),
];

const HISTORY_BOOTSTRAP_TOOL_DEFINITIONS = [
  definition(
      'home_get_bootstrap_brief',
      'Read the privacy-minimized bootstrap brief prepared by the explicit Home history action.'),
  definition(
      'home_request_bootstrap_sources',
      'Propose an exact same-site collection URL and feed semantics for every available history-bootstrap source, then ask the trusted browser to bind AI-authored collector modules to canonical read-only permissions and browser-owned result schemas before one grouped access decision.',
      {
        base_revision: BASE_REVISION,
        draft_id: {type: 'string'},
        sources: {
          type: 'array',
          items: {
            type: 'object',
            properties: {
              connector_id: {type: 'string'},
              collection_url: {type: 'string'},
              content_intent: {
                type: 'string',
                description:
                    'One of site_feed, following_feed, subscription_feed, or activity_feed.',
                enum: [
                  'site_feed',
                  'following_feed',
                  'subscription_feed',
                  'activity_feed',
                ],
              },
              content_kinds: {
                type: 'array',
                items: {type: 'string'},
                minItems: 1,
                maxItems: 4,
              },
            },
            required: [
              'connector_id',
              'collection_url',
              'content_intent',
              'content_kinds',
            ],
            additionalProperties: false,
          },
          minItems: 0,
          maxItems: 3,
        },
      },
      ['base_revision', 'draft_id', 'sources']),
];

export function setHomeToolContext(next: HomeToolContext): void {
  context = next.active ? {...next} : null;
}

export function clearHomeToolContext(): void {
  context = null;
}

export function getHomeToolContext(): HomeToolContext|null {
  return context ? {...context} : null;
}

export function getHomeToolDefinitions(): ToolDefinition[] {
  if (!context?.active) return [];
  return context.bootstrapKind === 'history' ?
      [...HOME_TOOL_DEFINITIONS, ...HISTORY_BOOTSTRAP_TOOL_DEFINITIONS] :
      HOME_TOOL_DEFINITIONS;
}

export function getHomeSystemPrompt(): string {
  if (!context?.active) return '';
  return context.bootstrapKind === 'history' ?
      HOME_PROJECT_CONTRACT + HOME_DESIGN_DIRECTOR_CONTRACT +
          HISTORY_BOOTSTRAP_CONTRACT :
      HOME_PROJECT_CONTRACT + HOME_DESIGN_DIRECTOR_CONTRACT;
}

export function isHomeTool(name: string): boolean {
  return HOME_TOOL_DEFINITIONS.some(tool => tool.function.name === name) ||
      HISTORY_BOOTSTRAP_TOOL_DEFINITIONS.some(
          tool => tool.function.name === name);
}
