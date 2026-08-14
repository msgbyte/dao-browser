# Dao Browser Feature Inventory

> This document catalogs the features Dao Browser adds on top of the Chromium
> baseline configured in `dao.json`. Dao-owned code lives in `src/dao/`;
> Chromium integration patches live in `src/patches/`.

Android GeckoView features, including the native Compose browser shell, are documented separately in
[`features-android.md`](features-android.md).
The Android shell now uses Android Components `BrowserStore` tabs with regular-session restore and
real tab thumbnails, plus persistent history and bookmarks, Android system downloads, persistent
engine preferences, installed-extension state, and QR scanning.

## 1. Vertical Sidebar

An Arc-inspired vertical sidebar replaces Chromium's top tab strip — the single biggest UI change in Dao Browser. The sidebar is a hybrid: a C++ Views container hosts a Lit/TypeScript WebUI that renders the actual tab list, favorites, and controls.

### 1.1 Sidebar Core (C++ Views)
- **DaoSidebarView** (`sidebar/dao_sidebar_view.{h,cc}`) — Main container, 240px default, collapsible to 4px with animation
- **Drag-to-resize** — Mouse drag, 150–400px range, ignored while collapsed; width preserved across collapse/expand cycles
- **DaoSidebarUIHandler** — Mojo bridge between sidebar C++ and the WebUI (media state, folder persistence, tab commands)
- **Incognito indicator** — Native shield-check status icon beside the sidebar collapse button in Incognito windows, with a localized privacy tooltip
- **Sidebar context menu** — Native right-click menu support with registered
  shortcut labels for Duplicate Tab, Copy Link, and Close Tab
- **DaoTabTooltipView** (`sidebar/dao_tab_tooltip_view.{h,cc}`) — Hover contextual info next to the sidebar

### 1.2 Sidebar WebUI (`dao://sidebar`)
- **dao_sidebar_app.ts** + **sidebar.{html,css,ts}** — Lit application root
- **sidebar_bridge.ts** — Mojo client wrapper
- **dao_sidebar_section.ts** — Reusable collapsible section container
- **dao_tab_list.ts** / **dao_tab_item.ts** — Vertical tab list, dual-line layout for the active tab (title + URL)
- **Active-tab auto-scroll** — Expands a collapsed folder when one of its tabs
  becomes active, then smoothly keeps the active tab inside the tab-list
  viewport
- **dao_favorites_view.ts** — Pinned site icon row
- **dao_pinned_tabs_grid.ts** + **DaoPinnedTabModel** — Persistent Pin grid
  with stable logical backing identities across navigation, discard/WebContents
  replacement, session restore, and Chromium session-command compaction.
  Open backing tabs are resolved across every browser window in the same
  profile, so activating a Pin focuses the existing tab instead of opening a
  second copy. Pin mutations are propagated to every same-profile sidebar
  handler before persistence, preventing a stale window from rewriting newer
  state. Dragging a Pin into another window's ordinary tab list moves its
  backing tab into that window. URL remains display/reopen metadata and is used
  for legacy migration only when exactly one candidate exists. Confirmed
  dormant items reopen once; identity conflicts fail closed without increasing
  the tab count. Pin state is serialized through a shared sequenced writer and
  atomically replaces the previous profile file.
- **dao_folder_item.ts** / **dao_folder_model.ts** — Folder grouping with
  profile-path persistence (load/save round-trip). Every folder context menu
  exposes Unfolder, which removes the folder and releases its child tabs in
  place, and Delete Folder, which uses a Dao native system confirmation dialog
  before closing the folder's currently matched child tabs. The folder is
  deleted only after those tabs actually close; cancelling either confirmation
  or a page's beforeunload prompt preserves the folder and remaining tabs.
- **Configurable stale-tab expiration** — The "You and Dao" Settings page
  stores a profile-scoped integer from 1–720 hours (24 hours by default);
  the sidebar's "Move Stale Tabs to stale" action reads that value when run,
  validates it defensively, and archives only qualifying ordinary tabs
- **dao_new_tab_button.ts** — New-tab button
- **dao_download_button.ts** — Download flyout trigger
- **dao_media_control.ts** — Per-tab media playback controls

### 1.3 Tab System Foundations
- **DaoTabIdentity** (`dao_tab_identity.h`) — Stable cross-window tab IDs decoupled from `TabStripModel` indices, migrated across WebContents replacement and persisted in session extra data
- **DaoTabCommands** (`dao_tab_commands.h`) — Tab action vocabulary (duplicate, pin, copy URL, close, etc.)
- **DaoCrossWindowDrag** (`dao_cross_window_drag.{h,cc}`) — `dao-tab-drag:<session_id>:<tab_index>` pasteboard payload + parser, shared by `dao_tab_item.ts` and the macOS drop handler in `dao_native_util_mac.mm`
- **Detach guards** — Prevents accidental reordering while dragging
- Patches: `tab_strip_model.cc.patch`, `tab_helpers.cc.patch`

### 1.4 Address Bar and Active-Tab URL
- **DaoAddressBarView** (`dao_address_bar_view.{h,cc}`) — Embedded URL pill (14px radius), referenced by sidebar / command bar / control center; adapts text color to background luminance
- Active-tab URL display is rendered inline in the dual-line tab item

### 1.5 Command Bar (Spotlight-style)
- **DaoCommandBarView** (`dao_command_bar_view.{h,cc}`) — Translucent scrim + frosted floating panel + layered shadows
  - Cmd+L → `Show()` pre-fills current URL (`SetFocusToLocationBar(is_user_initiated=true)`)
  - Cmd+T → `ShowForNewTab(prev)` opens blank tab, remembers previous tab; Esc / click-outside calls `CancelNewTab()` to close the blank and return
  - **Ask AI** — Submits prompt directly to the Agent
  - URL-vs-query detection heuristics + ghost-text completion
  - Every non-blank query reserves one exact-input Search action within the
    five-row suggestion limit, even when history, tabs, bookmarks, or URL
    matches rank above it; empty and whitespace-only input shows no suggestions
  - Keyboard-first: arrow keys to select, Right Arrow to fill the selected
    suggestion into the input, Tab to complete, Esc to dismiss
- **DaoSuggestionItemView** (`dao_suggestion_item_view.{h,cc}`) — Suggestion row
- **DaoNewTabButton** also routes through `ShowForNewTab()` with the recorded previous index

### 1.6 Downloads (sidebar-anchored)
- **DaoDownloadFlyoutView** (`sidebar/dao_download_flyout_view.{h,cc}`) — Anchored flyout panel
- **DaoFileIconUtilMac** (`sidebar/dao_file_icon_util_mac.{h,mm}`) — Native macOS file icon retrieval
- **Active-download hover details** — After a settled 400ms hover, an active
  download row reuses the native sidebar tooltip to show the full filename,
  transferred/total size, percentage, current speed, and reliable remaining
  time. Long filenames wrap in detailed mode, and the popup flips above its
  anchor when needed to remain inside the browser window. Unknown totals,
  percentages, speeds, and estimates are omitted rather than replaced with
  misleading values; leaving or removing the row hides the popup immediately.

### 1.7 Content Area Styling
- **DaoCornerOverlayView** (`dao_corner_overlay_view.{h,cc}`) — 10px rounded corners + 6-step soft shadow overlay on web contents
- **Adaptive theming** — Content area switches light/dark text and separators based on page luminance
- **DaoColors** (`dao_colors.{h,cc}`) — Shared blue accent (70,120,190) plus the light/dark hierarchy tokens; consumed by `chrome_color_mixer.cc.patch`
- **DaoLucideIcons** (`dao_lucide_icons.{h,cc}`) — Unified Lucide icon set

### 1.8 Toast and Feedback
- **DaoToastView** (`dao_toast_view.{h,cc}`) — Shared lightweight fade-in/fade-out toast for native browser feedback, including Copy URL, webpage Copy Image, QR decode failures, and Control Center actions. The Control Center Dark action remains clickable while system appearance is light and explains the system-dark requirement through this toast; callers retain action-specific localized text

## 2. AI Agent System

**The AI Agent is Dao's flagship feature.** Unlike browsers that bolt on a chat sidebar, Dao integrates the agent into the page lifecycle itself — it can read DOM, click elements, navigate, and remember context across sessions. Every layer (C++ Views, WebUI, Mojo bridge, LLM runtime, vendor pipeline) is purpose-built for agentic browsing.

The stack includes: **LLM tool calling**, **long-term memory** (SQLite + FTS5), **proactive suggestions**, a **skill system**, and **page-aware tools** (page capture, readable extraction, element interaction, accessibility tree, resource inspection).

### 2.1 Agent Core Services (`src/dao/browser/agent/`)
- **DaoAgentMemoryService** (+ `DaoAgentMemoryServiceFactory`) — Profile-keyed long-term memory service, SQLite backend, background sequenced execution
- **DaoAgentMemoryStore** — SQLite **FTS5** full-text search store. Five direct
  store round-trip tests remain disabled because test-time `RazeAndPoison`
  conflicts with direct `Init()`; focused service and read-path coverage remain active.
- **DaoAgentMemoryTypes** (`dao_agent_memory_types.{h,cc}`) — Episodic / semantic memory record types
- **DaoAgentProactiveEngine** — Navigation-triggered proactive suggestion engine
  - Scenario matching (URL pattern + page hints)
  - Episodic memory matching
  - Learning pipeline triggers
- **DaoAgentScenarioRegistry** — Scenario registry (seed + personal)
- **DaoAgentSkillService** (+ `DaoAgentSkillServiceFactory`)
- **DaoAgentSkillTypes** (`dao_agent_skill_types.{h,cc}`)

### 2.2 Agent Tab Integration (C++ Views)
- **DaoAgentSidebarView** (`dao_agent_sidebar_view.{h,cc}`) — WebUI-driven container, preloaded for fast toggle
- **DaoAgentCursorView** (`dao_agent_cursor_view.{h,cc}`) — Animated cursor + element-highlight overlay visualizing AI actions
- **DaoAgentLockBannerView** (`dao_agent_lock_banner_view.{h,cc}`) — Animated banner shown while AI controls a tab
- **DaoAgentLockTabHelper** (`agent/dao_agent_lock_tab_helper.{h,cc}`) — Per-tab lock state observer
- **Window-scoped tab automation** (`automation/dao_tab_tools.{h,cc}`) — Shared Agent/MCP list, switch, open, and close operations stay inside the authorized browser window. Sessions follow the same stable tab across WebContents replacement, tab IDs are reconciled to remain unique within a window, and ambiguous selectors fail closed. New targets are limited to HTTP, HTTPS, or `about:blank`; close results wait for unload approval and retarget only after the tab is actually removed.
- **Session-scoped DevTools automation** (`automation/dao_devtools_tools.{h,cc}`) — Agent and MCP clients share native network, console, and page-resource tools. Domain requests persist independently from confirmed state; events arriving while an enable attempt is pending are staged for that exact generation and host, then committed by a matching success or dropped after all matching attempts fail, cancellation, or a binding change. Clear actions remove both committed and currently staged events without cancelling the matching enable attempt, so only later events can commit. Within one binding, confirmation is monotonic after any successful enable and resets only when the binding changes. Network and console capture retain recent entries under UTF-8-safe field, entry-count, and aggregate-byte budgets with explicit dropped/truncated metadata. One iterative, deduplicating resource traversal synthesizes each frame's Document and reports item, URL-byte, depth, source, scan, and match limits separately. Resource reads require the current exact `(frame, URL)` tree entry when a frame is supplied and reject known decoded sizes over 1 MiB before fetching content; independent per-command response ceilings cover missing metadata. Resource search reports attempted, successfully searched, failed, and content-limited sources plus explicit incompleteness, including bounded Document DOM ingress.

### 2.3 Local MCP Server

- **Process-global native service** (`mcp/dao_mcp_service.{h,cc}`) — A default-off Local State preference controls a single browser-process MCP endpoint. Authentication and catalog discovery never start the approval timer, so an idle client can remain connected and continue listing tools without losing the endpoint. The first tool call snapshots the exact last-active eligible normal browser window and active tab, then requests native user approval before execution and acquisition of the same tab automation lease used by Dao Agent.
- **Settings master switch, connection, and quick setup** (`mcp/dao_mcp_settings_handler.{h,cc}`, `resources/settings/dao_page/`) — You and Dao groups the process-global MCP Local State switch, live connection status, and enabled-only setup into one native-style card. It renders Disabled, Ready, Approval requested, and Connected states; shows sanitized client details and Stop only for an active authorized lease; and aligns the selector with its copy action while stacking them at narrow widths. Quick setup defaults to Codex CLI and also offers a user-scoped Claude Code CLI command plus Generic MCP JSON. The preview and clipboard always contain the same native-generated content: CLI commands remain single-line, while Generic MCP is Chromium-native three-space pretty JSON. Malformed Generic MCP JSON fails closed by clearing the preview and leaving the clipboard unchanged; the former standalone configuration button is removed. Native POSIX shell quoting protects the running bundle's actual helper path and its explicit current user-data-directory argument, so Debug and custom-profile installs connect to the correct runtime endpoint.
- **Hardened local transport** (`mcp/dao_mcp_transport.{h,cc}`, `dao_mcp_connection.{h,cc}`, `dao_mcp_runtime_files.{h,mm}`) — The server owns all listener and connection I/O on Chromium's browser IO thread, uses a non-abstract Unix domain socket inside the user-data MCP directory, enforces owner-only directory/socket/metadata permissions, authenticates the peer UID plus a fresh 256-bit nonce, admits one external client, and removes runtime artifacts on disable or shutdown. Atomic `runtime.json` metadata publishes the socket and nonce only inside that private directory. IO-thread credits bound requests posted but not yet consumed by the UI thread, terminal responses synchronously close the logical request gate and stop further reads, aggregate write budgets fail closed under backpressure, and graceful close has bounded request and write-drain deadlines.
- **Versioned NDJSON protocol** (`mcp/dao_mcp_protocol.{h,cc}`) — Protocol version 1 supports `hello`, `tools/list`, `tools/call`, and `tools/cancel`, with an 8 MiB line ceiling, 64-request/8 MiB pending-ingress budget, and structured errors. Catalog discovery is available before approval, while execution waits for approval and the external automation lease.
- **Central external-target eligibility and MCP lifecycle policy** (`automation/dao_browser_target_policy.{h,cc}`, `mcp/dao_mcp_session_lifecycle_monitor.{h,cc}`) — MCP stays pinned to the exact approved normal Browser, regular Profile, and tab, with no eligible-tab fallback. HTTP, HTTPS, literal `about:blank`, and web-hosted PDFs are allowed; popup, Incognito, Guest, internal, extension, DevTools, Agent WebUI, file, data, and custom-scheme targets are rejected. Browser/Profile/target destruction or forbidden navigation enters logical closing, cancels work, clears locks/overlays/CDP state, and releases the lease while retaining the single-client slot until the accepted socket actually disconnects.
- **Exact-window approval and control UX** (`dao_mcp_approval_dialog.{h,cc}`, `dao_mcp_control_banner_view.{h,cc}`) — Every execution lease displays a localized, fail-closed Dao system dialog in the exact normal Browser selected for approval, with sanitized reported client metadata, verified PID when available, window, Profile, and current-login warning. Allow is intentionally not the default action. While the lease is active, only that BrowserView shows the client and current target in a dedicated row above the address bar; Stop cancels external work, releases the lease, closes the connection, and returns the enabled service to listening.
- **Peer-agent contention** — Dao Agent chat and non-browser tools remain available while an external MCP peer owns browser control. Dao Agent browser tools fail before CDP execution with the stable, retryable `AGENT_CONTROL_BUSY` error instead of blocking the whole chat turn.
- **Native stdio MCP helper** (`mcp/helper/`) — The standalone `dao-mcp` executable speaks newline-delimited JSON-RPC and negotiates MCP `2025-11-25` or `2025-06-18` over stdin/stdout, discovers the authenticated browser endpoint from the active user-data directory, and bridges MCP string or numeric request IDs to bounded browser IPC IDs. Its initialization capability opts out of Codex's shared tool-catalog cache so runtime refreshes keep the live Dao client authoritative instead of exposing a stale catalog without a callable connection. It maps the 29-tool catalog to MCP annotations, normalized object/scalar/list results to text plus object `structuredContent`, screenshot media to image content with its real MIME type, and tool failures to `isError: true`. Cancellation is forwarded to the browser and late responses are discarded. Stdout remains protocol-only; unavailable-browser diagnostics are deterministic stderr output.
- **29-tool external scope** — The shared native catalog contains 30 Dao Agent browser tools; MCP exposes 29 and excludes only the Agent-specific `resolve_element_context`. Agent memory, skill, workspace, and web-provider tools are not part of the local MCP server.
- **macOS helper packaging** (`mcp/BUILD.gn`, `dao_version.gni`, `chrome/BUILD_mcp_helper.gn.patch`) — The helper is built independently from the browser service, receives the same Dao product-version build argument as the app, and is copied with executable permissions to `Dao.app/Contents/Helpers/dao-mcp`.
- **Independent execution peer** — MCP owns its own automation session, DevTools client, executor, cursor integration, and cancellation state. It reuses the shared native tab/page/DevTools implementations and lease coordinator without depending on the Agent WebUI lifecycle.

### 2.4 Agent WebUI (`dao://agent` and `dao://skills`)

**Application shell** (`src/dao/browser/ui/webui/resources/agent/`)
- `agent.{html,css,ts}` + `dao_agent_app.ts` — Chat app entry; its settings
  button uses a fixed native navigation command to open the unified
  `dao://settings/agent` secondary page and closes the sidebar only after the navigation succeeds
- `skills.html` + `skills.ts` — Skill manager standalone entry
- `agent_bridge.ts` — Mojo bridge
- `agent_settings_{sync,native_bridge}.ts` — One-time migration from the
  `dao://agent` local-storage origin and live synchronization with the
  Profile-scoped settings source of truth

**Chat surface**
- `dao_chat_view.ts` — Main conversation view (session resume, skill picker, dynamic chips, composer height tracking, cost stats / usage)
- **Latest Agent error retry** — A terminal provider error exposes a retry
  action that reuses the original user submission, replaces only its failed
  assistant/tool branch, and preserves every earlier timeline entry; cancelled
  and historical errors cannot truncate the conversation
- `dao_chat_history_panel.ts` — History panel
- `dao_compact.ts` — Conversation compaction for context management
- `dao_page_capture.ts` — Convert current page to markdown and insert into the message
- `dao_share_image.ts` — Share-card image generation
- `dao_tool_renderer.ts` — Tool-call result rendering

**Skills + tools**
- `dao_skill_manager_view.ts` — Skill management surface
- `skill_registry.ts` — Skill catalog and lookup
- `tool_catalog.ts` — Tool catalog schema

**Unified Agent settings** (`dao://settings/agent`, overview entry `#agent`)
- `DaoAgentSettingsHandler` stores durable Agent choices in Profile prefs and
  is shared by the Agent and Settings WebUIs
- `Agent` remains a compact top-level Dao-exclusive Settings entry beside
  `You and Dao`. `You and Dao` owns browser capabilities only; the complete
  Agent configuration, Memory, Workspace, Usage, Skills, and Dream controls
  appear exactly once on the Agent secondary page
- The Agent module is packaged in Chromium's shared Settings lazy bundle and
  reached from the compact overview entry. The detail page uses five vertical Settings sections:
  Model and connection, Behavior and context,
  Capabilities, Learning and analysis, and Data and management. It adds no
  section rail or tabs
- Model/provider credentials, session/display behavior, persona, page and
  conversation context, web search, memory/proactive suggestions, and Dream
  analysis are managed in the Agent Settings section
- The persona card can restore the runtime default, while the Dream card can
  generate a report immediately when Memory and Dream analysis are enabled and
  keeps the existing Dream report history link available
- Tool group shortcuts and the expandable individual-permission list preserve
  the existing per-tool enable/disable controls. Rapid changes update one
  optimistic disabled-tool set and serialize complete-array writes before
  resynchronizing from the Profile snapshot
- Existing origin-scoped Agent settings migrate once, filling only values that
  have not already been configured in Settings; runtime/session state remains
  local to the Agent. Partial legacy usage records receive canonical defaults,
  while malformed values are rejected and initialized Profile data wins
- Session resume retains its established three-hour default and accepts zero
  hours. Configuration has independent loading, error, and retry feedback, so
  a configuration failure does not hide or disable management summaries
- The Agent section owns complete Memory, Workspace, and Usage management.
  These modules render as independent compact cards inside Data and management
  so their headings and actions remain visually separated.
  Memory and workspace data come from their Profile-keyed services; usage
  counters are stored in the Profile `dao.agent_usage_stats` pref, which is the
  source of truth shared with already-open Agent WebUIs
- `DaoAgentSettingsHandler` exposes only the management facade needed by the
  Agent section: memory summary/clear, workspace summary/reveal, and usage
  read/reset/record. It does not register workspace mutation messages
- `dao://memory`, `dao://skills`, and `dao://dream` remain secondary management
  pages linked from the Agent section rather than duplicate top-level settings

**Settings shell** (`dao://settings`)
- All top-level Chromium and Dao settings render as one continuous overview in
  the Dao Open Design shell. Its 184px table of contents, integrated search,
  680px content column, 30px section rhythm, and light/dark tokens share one
  page background without a repeated Dao brand header or footer. Nested page
  index views are kept in normal document flow so their cards contribute real
  height instead of overlapping later sections. Search filters the table of
  contents to matching sections, clears hidden selection on zero results, and
  restores the captured pre-search scroll position without top-level routing
- Table-of-contents activation scrolls to a section and updates the URL hash;
  scrolling updates the selected item. Direct legacy top-level paths continue
  to resolve and normalize to overview hashes
- Complex management flows remain independent secondary pages. Returning from
  one restores the overview and its section/scroll context
- Chromium settings components continue to own their routes, preferences,
  visibility gates, search integration, and external destinations; the shell
  changes presentation without replacing the settings inventory

**LLM / runtime plumbing**
- `llm_config.ts` — Model + provider configuration
- `pi_app_storage.ts` — Persistent storage abstraction
- `pi_llm_stream.ts` — Streaming LLM client
- `pi_tool_adapter.ts` — Tool adapter
- `chromium_types.d.ts` — Type bindings for chromium WebUI APIs
- `readability_bundle.ts` / `turndown_bundle.ts` — Reader-mode + HTML→Markdown bundles

**LLM tool set** (see `agent-console-api.md` for the full schema)
- Page access: `get_page_content` / `get_page_info` / `get_readable_content`
- Page interaction: `click_element`, fill, scroll, etc.
- Accessibility tree generation and interaction
- Resource inspection (reverse-engineering helpers)
- Web access: `web_search` uses provider built-in search when available,
  optional Jina Search with a user-supplied API key as the local fallback,
  and DuckDuckGo HTML as a best-effort final tier with explicit anomaly
  verification-page reporting; `fetch_url` uses Jina Reader before browser
  fetch.

### 2.5 Vendor Pipeline (Generated, never hand-edit)
- **`npm run vendor`** — Compiles pi-mono / pi-web-ui and related deps from `vendor.config.ts` + `vendor/entries/*`
- Artifacts: `agent/vendor/pi_runtime_bundle.ts` and `agent/vendor/pi_web_ui.css`
- `manifest.json` sha256 drift check guards against direct edits

### 2.6 Dream Analysis (nightly behavior learning)
- **DaoDreamService** (+ Factory) — profile-keyed scheduler: nightly
  (22:00–06:00 local, system idle ≥1h), daytime catch-up for yesterday,
  and manual trigger from Agent settings. Off by default
  (`dao.dream_enabled`), double-gated behind agent memory.
- **DreamMaterialCollector** — aggregates one day of signals: history
  (domain+title+time-bucket granularity, top 50; full URLs never leave
  the browser), search keywords (extracted in C++), agent conversation
  excerpts, proactive-feedback stats. Visit-count buckets remain available to
  the model, while measured foreground seconds are also aggregated by morning,
  afternoon, evening, and night for the report rhythm visualization.
- **dao_dream_runner.ts** — resident agent WebUI executes the LLM
  summarization (user's configured provider) and returns structured
  one-minute recap data (summary, time-of-day rhythm, and up to three themes),
  habits, and a full morning-report markdown. The recap is persisted inside
  the report's existing material-stats JSON, so older databases need no
  migration.
- Results: memory suggestions remain report-local candidates until the user
  confirms one at confidence 0.95; rejection never deletes or rewrites an
  existing preference. The report is archived in the `dream_reports` table,
  with a morning report card in the Agent panel, per-candidate confirm/reject,
  and an optional debug view of the exact LLM input (`dao.dream_debug`).
- **`dao://dream` one-minute recap** — responsive two-column report with a
  53-week real-report activity heatmap, compact daily-and-weekly history rail,
  localized date-and-active-duration tooltips for report-bearing heatmap cells
  on pointer hover and keyboard focus, summary card,
  measured foreground-focus rhythm, topic cards, aggregate counts,
  memory-candidate actions, and a folded full report. Existing rerun, image
  sharing, source-domain exclusion,
  and debug controls remain available. Legacy markdown-only reports derive a
  summary and topic cards locally instead of rendering an incomplete page.

## 3. Picture-in-Picture Enhancements

Built on Chromium's native PiP, adds a Document-PiP interception layer plus several visual + behavior tweaks.

### 3.1 Dao-owned (`src/dao/browser/pip/`)
- **DaoPipInterceptor** — Intercepts PiP requests on configured sites and redirects to Document PiP with a specific DOM element; selecting the opener tab closes the active Document PiP and restores the moved element for both manual and automatic entry, while capture-driven visibility changes leave the PiP open
- **DaoPipSiteRules** — Site rules loaded from `pip_site_rules.json`

### 3.2 Auto-PiP triggers
- **DaoAutoPipVisibilityHelper** (`browser/dao_auto_pip_visibility_helper.{h,cc}`) — Watches window-visibility changes (minimize, hide) and triggers auto-PiP for the active tab's playing video; complements Chromium's `AutoPictureInPictureTabHelper` (which only handles tab switching)
- Patches: `auto_picture_in_picture_tab_helper.cc.patch`, `picture_in_picture_window_manager.cc.patch`

### 3.3 PiP window chrome
- Patches: `picture_in_picture_browser_frame_view.{h,cc}.patch` — Custom Document-PiP top bar
- Patches: `back_to_tab_button.cc.patch`, `minimize_button.cc.patch`, `video_overlay_window_views.cc.patch` — Overlay button restyling
- Patches: `video_picture_in_picture_window_controller_impl.cc.patch` — Capturer guard preventing tab throttling
- User-interaction event forwarding via `chrome_render_widget_host_view_mac_delegate.mm.patch`
- **Full-work-area manual resize** — Document PiP and video PiP windows can be enlarged to the display's complete usable work area; initial sizing and site-request limits remain unchanged

## 4. Split View

- **DaoSplitView** (`split/dao_split_view.{h,cc}`) — Split container
- **DaoSplitNode** / **DaoSplitPaneView** — Split tree nodes and panes
- **DaoSplitDividerView** — Draggable divider
- **Native tab-drag cleanup** — Successful and cancelled macOS tab drags reset
  drag-only Split View hit testing in every browser window, so WebContents
  mouse interaction cannot remain blocked when WebUI `dragend` is skipped
- Status: wired up but not enabled by default

## 5. Control Center

macOS-style floating control center panel bundling extensions and utilities.

- **DaoControlCenterButton** — Trigger button
- **DaoControlCenterPopup** — Floating popup with transparent overlay click-to-close
- **DaoControlCenterExtensionsSection** — Extensions grid
- **DaoControlCenterUtilitySection** — Utility buttons (share, QR, lock, more)
- **DaoControlCenterQrView** — QR code generation
- **DaoControlCenterMoreMenu** — More menu, including cache and cookie cleanup scoped to the active page's current site with completion toast feedback
- **DaoPinnedExtensionsContainer** — Pinned extension icon container
- **Extension action badges** — Pinned and Control Center extension icons render
  the active tab's badge text and extension-defined badge colors using Chromium's
  native badge compositor, with independently compacted text and a 10px-tall
  background sized for Dao's smaller icon surfaces
- **DaoNativeShareMac** (`dao_native_share_mac.{h,mm}`) — Native macOS share sheet
- **DaoNativeUtilMac** (`dao_native_util_mac.{h,mm}`) — Misc native helpers (incl. cross-window-drag drop side)

## 6. Welcome Page

- **DaoWelcomeUI** (`webui/dao_welcome_ui.{h,cc}`) — `dao://welcome` WebUI controller
- WebUI: `welcome.{html,css,ts}` + `dao_welcome_app.ts` + `welcome_bridge.ts` (Lit)
- **Menu item + command handling** — User can reopen anytime
- **First-run preference tracking** — Auto-opens only on first launch (managed via `dao_pref_names`)

## 6.1 Browser Data Migration

- **Standalone migration surface** — `dao://import` is a dedicated Lit WebUI,
  with a localized **Import browser data** document title for its browser tab,
  exposed as an explicit **Import browser data** row on the **You and Dao**
  Settings page and from the existing system **Import Bookmarks and Settings…**
  command instead of Chromium's modal importer. Its source grid always shows
  Chrome, Arc, Edge, Safari, and Firefox: detected profiles remain individually
  selectable, while browser kinds without a detected profile appear as disabled
  cards. The flow lets the user choose supported data categories, reconnects to
  an active profile-scoped job after reload, and reports per-category progress
  and retryable partial failures. Stopped jobs retain completed-batch counts and
  are presented as cancelled, not completed. Partial completion identifies the
  categories that need retrying in a dedicated summary and visually marks their
  result cards without hiding any items that were imported before the failure.
  The active migration rail uses the detected source browser's product logo and
  Dao's packaged product logo instead of generic letter placeholders.
- **Asynchronous candidate counts** — Selecting a Chromium-family profile
  starts independent background counts for each supported category without
  blocking navigation or migration. Bookmark, history, password, and extension
  counts read source metadata directly; password counting never decrypts a
  credential or triggers Keychain authorization. Tab sessions are counted only
  from a temporary snapshot because Chromium's session reader can rotate files.
  These preflight values describe scanned candidates and may differ from final
  imported totals if the source changes or Dao skips conflicts. Legacy Safari
  and Firefox importers report the count as unavailable until migration.
- **Supported sources** — Chrome, Arc, and Edge profiles use Dao's snapshot
  adapters; Safari and Firefox profiles use Chromium's sandboxed platform
  importers for the categories those importers support on macOS.
- **Safe source reads** — Chromium-family stores are copied to a temporary
  profile snapshot with bounded metadata-stability retries. SQLite sidecars and
  session directories are included, so source browsers can normally remain
  open. Temporary snapshots are deleted with the category operation. Cleanup
  stays off the UI thread and blocks browser shutdown until copied history,
  password, and session data has been removed.
- **Merge-only destination writes** — Bookmarks are placed under a localized
  imported root, destination password conflicts are preserved, already-open
  tab URLs and installed extensions are skipped, and history writes use the
  profile History service. History and password counts advance only after the
  destination services confirm the persisted records. No category replaces
  existing Dao data.
- **Passwords and extensions** — The selection screen warns that password
  decryption may trigger a macOS Keychain authorization prompt. A denial fails
  only passwords. Compatible web-store extensions are reinstalled in sequence;
  extension storage and sign-in state are not copied.
- **Imported tabs** — Source session tabs retain order, become background
  discarded tabs, and are collected in a collapsed sidebar folder. Tabs are
  created in cancellable batches; a failed folder write rolls back tabs from
  that import instead of leaving orphaned browser tabs. Successful folder
  persistence invalidates every live same-profile sidebar cache.
- **Privacy boundary** — Migration records, snapshots, and progress stay local;
  only official extension reinstallation may use the network. Cookies are not
  imported because Chromium does not expose a safe cross-profile cookie import
  API and copied encrypted cookie stores are not portable.
- Core owners: `browser/import/dao_migration_service.*`,
  `dao_profile_snapshot.*`, source adapters and target writers;
  `webui/dao_import_ui.*` and `webui/resources/import/` own the WebUI.

## 7. Little Dao Window

Lightweight window form factor for popups / mini-tools.

- **DaoLittleDaoController** / **DaoLittleDaoView** (`little_dao/`) — Top bar (48px) with hostname display + "Open in Dao" button
- **Browser::Create timing control** — Static-flag pattern (`IsCreatingLittleDao()`) to pass state into BrowserView during construction
- **Permanently windowed** — Mini Dao disables browser fullscreen commands and macOS native maximize/fullscreen eligibility, including when an external link creates it while the main Dao window is fullscreen
- macOS traffic-light repositioning

## 8. Webstore Branding

- **DaoWebstoreBrandingTabHelper** (`browser/dao_webstore_branding_tab_helper.{h,cc}`) — `WebContentsObserver` that injects a script on Chrome Web Store pages to rewrite "Add to Chrome" / "Remove from Chrome" → "Add to Dao" / "Remove from Dao"

## 9. Preferences and Centralized Pref Names

- **DaoPrefNames** (`browser/dao_pref_names.{h,cc}`) — Single source of truth for Dao-owned pref keys (welcome shown, sidebar width, folder file path, etc.)
- Patches: `prefs/browser_prefs.cc.patch`, `prefs/session_startup_pref.cc.patch`

## 10. Web JavaScript Dialogs

- **Dao-styled `alert()`, `confirm()`, and `prompt()`** — Keeps Chromium's tab-modal JavaScript dialog lifecycle, origin title, accessibility behavior, and callbacks while centering the dialog within the active web contents and applying Dao's rounded surface, primary/secondary actions, Enter/Esc keycaps, and themed prompt input
- Patches: `chrome/browser/ui/views/javascript_tab_modal_dialog_view_views.{cc,h}.patch`, `components/constrained_window/constrained_window_views.cc.patch`, `ui/views/window/dialog_delegate.h.patch`

## 11. Branding and Visuals

- **Dao brand assets** — Logos / SVGs; product name globally rebranded ("Chromium" → "Dao")
- **`chrome://` → `dao://`** — Internal URL schemes rewritten via `content/common/url_schemes.cc.patch`
- **`chrome_color_mixer.cc.patch`** — Threads `dao_colors` tokens into the global color pipeline
- **Custom scrollbar** — `third_party/blink/renderer/core/css/css_default_style_sheets.cc.patch` + `html.css.patch` for globally restyled scrollbars
- **Chromium string rebranding** — `*_strings.grd[p]` patch files plus import-time rewrites for generated / terms resources

## 12. Shortcuts and Menus

- **macOS global shortcuts** — `chrome/browser/global_keyboard_shortcuts_mac.mm.patch`
- **macOS main menu** — `cocoa/main_menu_builder.mm.patch` + `cocoa/accelerators_cocoa.mm.patch`
- **App controller** — `app_controller_mac.mm.patch` (validate + execute commands when no browser window exists)
- **IDC_\* command IDs** — `chrome/app/chrome_command_ids.h.patch`
- **Command handling** — `browser_command_controller.cc.patch` + `browser_commands.cc.patch`
- Adding a new `IDC_*` requires keeping its declaration, shortcut/menu wiring,
  and command-controller handling synchronized.

## 13. Chromium Core Integration Patches

The live patch inventory is the tracked tree under `src/patches/`. The groups
below describe its major integration surfaces without duplicating a volatile
file count.

### 13.1 URLs and Schemes
- `content/common/url_schemes.cc.patch` — Register `dao://`
- `chrome/common/url_constants.h.patch`, `chrome/common/webui_url_constants.h.patch`, `content/public/common/url_constants.h.patch` — Constant tables
- `content/browser/webui/{url_data_manager_backend,web_ui_data_source_impl,web_ui_url_loader_factory}.cc.patch` + `content/public/browser/url_data_source.cc.patch` — Data source / loader factory hooks
- `chrome/browser/ui/webui/{chrome_web_ui_configs,chrome_web_ui_controller_factory}.cc.patch` — Register Dao WebUI controllers
- `components/url_formatter/url_fixer.cc.patch` — URL fixer adaptations
- `chrome/browser/browser_about_handler.cc.patch` — `dao://` about-page routing
- `third_party/blink/public/common/chrome_debug_urls.h.patch` — Debug URL alias

### 13.2 BrowserView Integration
- `views/frame/browser_view.{h,cc}.patch` — Inject `DaoSidebarView` and `DaoCornerOverlayView`; toolbar stays alive but is parked at `y=-height` so Chromium omnibox and command plumbing remain available
- `views/frame/browser_view_layout` (implicit via patches) — Layout adjustments
- `views/frame/{browser_native_widget_mac,browser_frame_mac,immersive_mode_controller_mac}.mm.patch` — macOS frame integrations
- `views/frame/contents_web_view.{h,cc}.patch` + `views/frame/contents_layout_manager.cc.patch` — Content area layout
- `views/frame/layout/browser_view_{popup,tabbed}_layout_impl.cc.patch` — Per-mode layout impls
- `views/bubble_anchor_util_views.cc.patch` — Bubble anchoring near the sidebar

### 13.3 Tab Strip and Tab Helpers
- `chrome/browser/ui/tabs/tab_strip_model.cc.patch` — Tab model hooks (incl. cross-window drag)
- `chrome/browser/ui/tab_helpers.cc.patch` — Attach Dao tab helpers (`DaoAgentLockTabHelper`, `DaoWebstoreBrandingTabHelper`, `DaoAutoPipVisibilityHelper`)
- `content/browser/web_contents/web_contents_view_mac.mm.patch` — Native drag-drop integration
- `content/browser/renderer_host/render_frame_host_impl.cc.patch` — Frame-host hook

### 13.4 Startup Flow
- `ui/startup/startup_browser_creator.cc.patch` + `startup_browser_creator_impl.cc.patch` — Startup behavior
- `ui/startup/infobar_utils.cc.patch` — Infobar suppression
- `signin/account_consistency_mode_manager.cc.patch` — Account consistency mode
- `profiles/chrome_browser_main_extra_parts_profiles.cc.patch` — Profile init hook

### 13.5 Extensions
- `views/extensions/extension_popup.cc.patch` — Popup layout
- `extensions/browser/api/web_request/extension_web_request_event_router.cc.patch` — Event router hook
- `extensions/common/features/simple_feature.cc.patch` + `extensions/common/api/_api_features.json.patch` + `chrome/common/extensions/api/_api_features.json.patch` — Feature allowlists

### 13.6 WebUI Pages (HTML rebranding / customization)
- `app_home`, `bookmarks`, `certificate_manager`, `downloads`, `extensions`, `history`, `password_manager`, `print_preview`, `settings/people_page`, `settings/settings`, `signin/profile_picker` — HTML patches
- `settings/reset_page/reset_profile_dialog.{html,ts}.patch` — Reset profile dialog customization
- `chrome/browser/ui/webui/settings/settings_ui.cc.patch`

### 13.7 macOS Platform
- `skia/ext/skia_utils_mac.mm.patch`
- `third_party/blink/renderer/platform/mac/graphics_context_canvas.mm.patch`
- `ui/display/mac/screen_utils_mac.mm.patch`
- `ui/gfx/image/image_unittest_util_apple.mm.patch`
- `services/shape_detection/text_detection_impl_mac_unittest.mm.patch`
- `components/os_crypt/common/keychain_password_mac.mm.patch`
- `chrome/common/chrome_paths_mac.mm.patch`
- `chrome/app/app-Info.plist.patch`
- `build/toolchain/apple/filter_libtool.py.patch`

### 13.8 Blink and Rendering
- `third_party/blink/renderer/core/css/css_default_style_sheets.cc.patch` — Default stylesheets
- `third_party/blink/renderer/core/html/resources/html.css.patch` — Default HTML CSS
- `third_party/blink/renderer/platform/graphics/paint/paint_controller.cc.patch` — Paint hook
- `components/global_media_controls/public/views/media_progress_view.cc.patch` — Media progress UI
- `media/base/media_switches.cc.patch` — Media switches

### 13.9 Build / WebUI Toolchain
- `chrome/BUILD_mcp_helper.gn.patch`, `chrome/browser/BUILD.gn.patch`, `chrome/browser/ui/BUILD.gn.patch`, `chrome/test/BUILD.gn.patch`, `chrome/chrome_paks.gni.patch`, `chrome/app/theme/chromium/BRANDING.patch` — Build graph
- `third_party/lit/v3_0/BUILD.gn.patch` — Lit framework integration
- `ui/webui/resources/tools/build_webui.gni.patch` + `ui/webui/webui_util.cc.patch` — WebUI build helpers
- `tools/gritsettings/resource_ids.spec.patch` — Resource ID allocation
- `tools/metrics/histograms/metadata/sql/histograms.xml.patch` — SQL histograms
- `chrome/browser/download/bubble/download_bubble_prefs.cc.patch`
- `chrome/browser/accessibility/soda_installer_impl.cc.patch`
- `components/performance_manager/user_tuning/prefs.cc.patch`

### 13.10 String Localization and Rebranding
- `chrome/app/*_strings.grd[p]` and `chromium_strings.grd` / `google_chrome_strings.grd` — Branded strings
- `components/*_strings.grdp` — Component strings (autofill, crash, error, history clusters, management, omnibox, policy, privacy sandbox, security interstitials, site settings, version, etc.)
- Settings localized-string provider patches — Dao settings page strings
- `components/resources/terms/terms_*.html` rebranding is handled by import-time rewrites, not patch files

## 14. Testing

- **browser_tests** (`dao_browser_browsertest.cc`) — Integration test framework
- **Coverage**: sidebar presence / default width / collapse-expand, drag resize, address bar, command bar show/hide & idempotency, tab CRUD, SplitView, CornerOverlay, folder persistence, URL detection heuristics, extension icon and badge updates, PiP site rules / interception / top bar overlay
- **Known disabled**: 5 `DaoAgentMemoryStore` round-trip tests due to FTS5
  `RazeAndPoison` under direct `Init()`, plus 4 PiP overlay tests awaiting
  replacement after upstream API drift

## 15. Build and Packaging

- **`scripts/cli.ts`** — Unified CLI (download / import / export / build / rebuild / test / start) — the single entrypoint; **never run `autoninja` / `ninja` / `siso` directly**
- **`appdmg` integration** — DMG packaging
- **`configs/common.gn`** + **`configs/macos.gn`** — Shared + macOS GN args (component build, no NaCl, proprietary codecs, no Google API keys)
- **Custom checkout variables** — Build configuration flexibility
- **Vendor pipeline** — Agent WebUI dependency bundling (`npm run vendor`)

---

## Current Status

- **Version source**: `dao.json` is authoritative for both the Dao display
  version and Chromium baseline
- **Target platform**: macOS arm64
- **Source inventory**: `src/dao/` contains Dao-owned sources and
  `src/patches/` contains the live Chromium integration patch set
