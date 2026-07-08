# Dao Browser Feature Inventory

> This document catalogs all features Dao Browser adds on top of Chromium 147.0.7727.135. Dao-owned code lives in `src/dao/`; Chromium integration patches live in `src/patches/`.

## 1. Vertical Sidebar

An Arc-inspired vertical sidebar replaces Chromium's top tab strip — the single biggest UI change in Dao Browser. The sidebar is a hybrid: a C++ Views container hosts a Lit/TypeScript WebUI that renders the actual tab list, favorites, and controls.

### 1.1 Sidebar Core (C++ Views)
- **DaoSidebarView** (`sidebar/dao_sidebar_view.{h,cc}`) — Main container, 240px default, collapsible to 4px with animation
- **Drag-to-resize** — Mouse drag, 150–400px range, ignored while collapsed; width preserved across collapse/expand cycles
- **DaoSidebarUIHandler** — Mojo bridge between sidebar C++ and the WebUI (media state, folder persistence, tab commands)
- **Sidebar context menu** — Right-click menu support
- **DaoTabTooltipView** (`sidebar/dao_tab_tooltip_view.{h,cc}`) — Hover contextual info next to the sidebar

### 1.2 Sidebar WebUI (`chrome://dao-sidebar`)
- **dao_sidebar_app.ts** + **sidebar.{html,css,ts}** — Lit application root
- **sidebar_bridge.ts** — Mojo client wrapper
- **dao_sidebar_section.ts** — Reusable collapsible section container
- **dao_tab_list.ts** / **dao_tab_item.ts** — Vertical tab list, dual-line layout for the active tab (title + URL)
- **dao_favorites_view.ts** — Pinned site icon row
- **dao_folder_item.ts** / **dao_folder_model.ts** — Folder grouping with profile-path persistence (load/save round-trip)
- **dao_new_tab_button.ts** — New-tab button
- **dao_download_button.ts** — Download flyout trigger
- **dao_media_control.ts** — Per-tab media playback controls

### 1.3 Tab System Foundations
- **DaoTabIdentity** (`dao_tab_identity.h`) — Stable cross-window tab IDs decoupled from `TabStripModel` indices
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
  - Keyboard-first: arrow keys to select, Tab to complete, Esc to dismiss
- **DaoSuggestionItemView** (`dao_suggestion_item_view.{h,cc}`) — Suggestion row
- **DaoNewTabButton** also routes through `ShowForNewTab()` with the recorded previous index

### 1.6 Downloads (sidebar-anchored)
- **DaoDownloadFlyoutView** (`sidebar/dao_download_flyout_view.{h,cc}`) — Anchored flyout panel
- **DaoFileIconUtilMac** (`sidebar/dao_file_icon_util_mac.{h,mm}`) — Native macOS file icon retrieval

### 1.7 Content Area Styling
- **DaoCornerOverlayView** (`dao_corner_overlay_view.{h,cc}`) — 10px rounded corners + 6-step soft shadow overlay on web contents
- **Adaptive theming** — Content area switches light/dark text and separators based on page luminance
- **DaoColors** (`dao_colors.{h,cc}`) — Shared blue accent (70,120,190) plus the light/dark hierarchy tokens; consumed by `chrome_color_mixer.cc.patch`
- **DaoLucideIcons** (`dao_lucide_icons.{h,cc}`) — Unified Lucide icon set

### 1.8 Toast and Feedback
- **DaoToastView** (`dao_toast_view.{h,cc}`) — Lightweight fade-in/fade-out toast (URL copy, etc.)

## 2. AI Agent System

**The AI Agent is Dao's flagship feature.** Unlike browsers that bolt on a chat sidebar, Dao integrates the agent into the page lifecycle itself — it can read DOM, click elements, navigate, and remember context across sessions. Every layer (C++ Views, WebUI, Mojo bridge, LLM runtime, vendor pipeline) is purpose-built for agentic browsing.

The stack includes: **LLM tool calling**, **long-term memory** (SQLite + FTS5), **proactive suggestions**, a **skill system**, and **page-aware tools** (page capture, readable extraction, element interaction, accessibility tree, resource inspection).

### 2.1 Agent Core Services (`src/dao/browser/agent/`)
- **DaoAgentMemoryService** (+ `DaoAgentMemoryServiceFactory`) — Profile-keyed long-term memory service, SQLite backend, background sequenced execution
- **DaoAgentMemoryStore** — SQLite **FTS5** full-text search store (note: see `MEMORY.md` re. test-time `RazeAndPoison` issue → 5 tests `DISABLED_`)
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

### 2.3 Agent WebUI (`chrome://dao-agent` and `chrome://dao-agent/skills`)

**Application shell** (`src/dao/browser/ui/webui/resources/agent/`)
- `agent.{html,css,ts}` + `dao_agent_app.ts` — Chat app entry
- `skills.html` + `skills.ts` — Skill manager standalone entry
- `agent_bridge.ts` — Mojo bridge

**Chat surface**
- `dao_chat_view.ts` — Main conversation view (session resume, skill picker, dynamic chips, composer height tracking, cost stats / usage)
- `dao_chat_history_panel.ts` — History panel
- `dao_compact.ts` — Conversation compaction for context management
- `dao_page_capture.ts` — Convert current page to markdown and insert into the message
- `dao_share_image.ts` — Share-card image generation
- `dao_tool_renderer.ts` — Tool-call result rendering

**Skills + tools**
- `dao_skill_manager_view.ts` — Skill management surface
- `skill_registry.ts` — Skill catalog and lookup
- `tool_catalog.ts` — Tool catalog schema
- `dao_settings_view.ts` — Expandable tool groups with persistent state

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

### 2.4 Vendor Pipeline (Generated, never hand-edit)
- **`npm run vendor`** — Compiles pi-mono / pi-web-ui and related deps from `vendor.config.ts` + `vendor/entries/*`
- Artifacts: `agent/vendor/pi_runtime_bundle.ts` and `agent/vendor/pi_web_ui.css`
- `manifest.json` sha256 drift check guards against direct edits

### 2.5 Dream Analysis (nightly behavior learning)
- **DaoDreamService** (+ Factory) — profile-keyed scheduler: nightly
  (22:00–06:00 local, system idle ≥1h), daytime catch-up for yesterday,
  and manual trigger from Agent settings. Off by default
  (`dao.dream_enabled`), double-gated behind agent memory.
- **DreamMaterialCollector** — aggregates one day of signals: history
  (domain+title+time-bucket granularity, top 50; full URLs never leave
  the browser), search keywords (extracted in C++), agent conversation
  excerpts, proactive-feedback stats.
- **dao_dream_runner.ts** — resident agent WebUI executes the LLM
  summarization (user's configured provider) and returns structured
  habits + a morning-report markdown.
- Results: habits merged into `preferences` (LLM confidence capped at
  0.8; user confirmation raises to 0.95), report archived in the
  `dream_reports` table, morning report card in the Agent panel with
  per-habit confirm/reject and an optional debug view of the exact LLM
  input (`dao.dream_debug`).

## 3. Picture-in-Picture Enhancements

Built on Chromium's native PiP, adds a Document-PiP interception layer plus several visual + behavior tweaks.

### 3.1 Dao-owned (`src/dao/browser/pip/`)
- **DaoPipInterceptor** — Intercepts PiP requests on configured sites and redirects to Document PiP with a specific DOM element
- **DaoPipSiteRules** — Site rules loaded from `pip_site_rules.json`

### 3.2 Auto-PiP triggers
- **DaoAutoPipVisibilityHelper** (`browser/dao_auto_pip_visibility_helper.{h,cc}`) — Watches window-visibility changes (minimize, hide) and triggers auto-PiP for the active tab's playing video; complements Chromium's `AutoPictureInPictureTabHelper` (which only handles tab switching)
- Patches: `auto_picture_in_picture_tab_helper.cc.patch`, `picture_in_picture_window_manager.cc.patch`

### 3.3 PiP window chrome
- Patches: `picture_in_picture_browser_frame_view.{h,cc}.patch` — Custom Document-PiP top bar
- Patches: `back_to_tab_button.cc.patch`, `minimize_button.cc.patch`, `video_overlay_window_views.cc.patch` — Overlay button restyling
- Patches: `video_picture_in_picture_window_controller_impl.cc.patch` — Capturer guard preventing tab throttling
- User-interaction event forwarding via `chrome_render_widget_host_view_mac_delegate.mm.patch`

## 4. Split View

- **DaoSplitView** (`split/dao_split_view.{h,cc}`) — Split container
- **DaoSplitNode** / **DaoSplitPaneView** — Split tree nodes and panes
- **DaoSplitDividerView** — Draggable divider
- Status: wired up but not enabled by default

## 5. Control Center

macOS-style floating control center panel bundling extensions and utilities.

- **DaoControlCenterButton** — Trigger button
- **DaoControlCenterPopup** — Floating popup with transparent overlay click-to-close
- **DaoControlCenterExtensionsSection** — Extensions grid
- **DaoControlCenterUtilitySection** — Utility buttons (share, QR, lock, more)
- **DaoControlCenterQrView** — QR code generation
- **DaoControlCenterMoreMenu** — More menu, including cache and cookie cleanup scoped to the active page's current site
- **DaoPinnedExtensionsContainer** — Pinned extension icon container
- **DaoNativeShareMac** (`dao_native_share_mac.{h,mm}`) — Native macOS share sheet
- **DaoNativeUtilMac** (`dao_native_util_mac.{h,mm}`) — Misc native helpers (incl. cross-window-drag drop side)

## 6. Welcome Page

- **DaoWelcomeUI** (`webui/dao_welcome_ui.{h,cc}`) — `chrome://dao-welcome` WebUI controller
- WebUI: `welcome.{html,css,ts}` + `dao_welcome_app.ts` + `welcome_bridge.ts` (Lit)
- **Menu item + command handling** — User can reopen anytime
- **First-run preference tracking** — Auto-opens only on first launch (managed via `dao_pref_names`)

## 7. Little Dao Window

Lightweight window form factor for popups / mini-tools.

- **DaoLittleDaoController** / **DaoLittleDaoView** (`little_dao/`) — Top bar (48px) with hostname display + "Open in Dao" button
- **Browser::Create timing control** — Static-flag pattern (`IsCreatingLittleDao()`) to pass state into BrowserView during construction
- macOS traffic-light repositioning

## 8. Webstore Branding

- **DaoWebstoreBrandingTabHelper** (`browser/dao_webstore_branding_tab_helper.{h,cc}`) — `WebContentsObserver` that injects a script on Chrome Web Store pages to rewrite "Add to Chrome" / "Remove from Chrome" → "Add to Dao" / "Remove from Dao"

## 9. Preferences and Centralized Pref Names

- **DaoPrefNames** (`browser/dao_pref_names.{h,cc}`) — Single source of truth for Dao-owned pref keys (welcome shown, sidebar width, folder file path, etc.)
- Patches: `prefs/browser_prefs.cc.patch`, `prefs/session_startup_pref.cc.patch`

## 10. Branding and Visuals

- **Dao brand assets** — Logos / SVGs; product name globally rebranded ("Chromium" → "Dao")
- **`chrome://` → `dao://`** — Internal URL schemes rewritten via `content/common/url_schemes.cc.patch`
- **`chrome_color_mixer.cc.patch`** — Threads `dao_colors` tokens into the global color pipeline
- **Custom scrollbar** — `third_party/blink/renderer/core/css/css_default_style_sheets.cc.patch` + `html.css.patch` for globally restyled scrollbars
- **Chromium string rebranding** — `*_strings.grd[p]` patch files plus import-time rewrites for generated / terms resources

## 11. Shortcuts and Menus

- **macOS global shortcuts** — `chrome/browser/global_keyboard_shortcuts_mac.mm.patch`
- **macOS main menu** — `cocoa/main_menu_builder.mm.patch` + `cocoa/accelerators_cocoa.mm.patch`
- **App controller** — `app_controller_mac.mm.patch` (validate + execute commands when no browser window exists)
- **IDC_\* command IDs** — `chrome/app/chrome_command_ids.h.patch`
- **Command handling** — `browser_command_controller.cc.patch` + `browser_commands.cc.patch`
- Note: adding a new IDC_* requires three edit sites (see `MEMORY.md`)

## 12. Chromium Core Integration Patches (166 total)

Roughly **166 patch files** across functional Chromium integration and
string-localization/rebranding changes.

### 12.1 URLs and Schemes
- `content/common/url_schemes.cc.patch` — Register `dao://`
- `chrome/common/url_constants.h.patch`, `chrome/common/webui_url_constants.h.patch`, `content/public/common/url_constants.h.patch` — Constant tables
- `content/browser/webui/{url_data_manager_backend,web_ui_data_source_impl,web_ui_url_loader_factory}.cc.patch` + `content/public/browser/url_data_source.cc.patch` — Data source / loader factory hooks
- `chrome/browser/ui/webui/{chrome_web_ui_configs,chrome_web_ui_controller_factory}.cc.patch` — Register Dao WebUI controllers
- `components/url_formatter/url_fixer.cc.patch` — URL fixer adaptations
- `chrome/browser/browser_about_handler.cc.patch` — `dao://` about-page routing
- `third_party/blink/public/common/chrome_debug_urls.h.patch` — Debug URL alias

### 12.2 BrowserView Integration
- `views/frame/browser_view.{h,cc}.patch` — Inject `DaoSidebarView` and `DaoCornerOverlayView`; toolbar kept alive but parked at `y=-height` (NOT `IsToolbarVisible() = false`, see `MEMORY.md`)
- `views/frame/browser_view_layout` (implicit via patches) — Layout adjustments
- `views/frame/{browser_native_widget_mac,browser_frame_mac,immersive_mode_controller_mac}.mm.patch` — macOS frame integrations
- `views/frame/contents_web_view.{h,cc}.patch` + `views/frame/contents_layout_manager.cc.patch` — Content area layout
- `views/frame/layout/browser_view_{popup,tabbed}_layout_impl.cc.patch` — Per-mode layout impls
- `views/bubble_anchor_util_views.cc.patch` — Bubble anchoring near the sidebar

### 12.3 Tab Strip and Tab Helpers
- `chrome/browser/ui/tabs/tab_strip_model.cc.patch` — Tab model hooks (incl. cross-window drag)
- `chrome/browser/ui/tab_helpers.cc.patch` — Attach Dao tab helpers (`DaoAgentLockTabHelper`, `DaoWebstoreBrandingTabHelper`, `DaoAutoPipVisibilityHelper`)
- `content/browser/web_contents/web_contents_view_mac.mm.patch` — Native drag-drop integration
- `content/browser/renderer_host/render_frame_host_impl.cc.patch` — Frame-host hook

### 12.4 Startup Flow
- `ui/startup/startup_browser_creator.cc.patch` + `startup_browser_creator_impl.cc.patch` — Startup behavior
- `ui/startup/infobar_utils.cc.patch` — Infobar suppression
- `signin/account_consistency_mode_manager.cc.patch` — Account consistency mode
- `profiles/chrome_browser_main_extra_parts_profiles.cc.patch` — Profile init hook

### 12.5 Extensions
- `views/extensions/extension_popup.cc.patch` — Popup layout
- `extensions/browser/api/web_request/extension_web_request_event_router.cc.patch` — Event router hook
- `extensions/common/features/simple_feature.cc.patch` + `extensions/common/api/_api_features.json.patch` + `chrome/common/extensions/api/_api_features.json.patch` — Feature allowlists

### 12.6 WebUI Pages (HTML rebranding / customization)
- `app_home`, `bookmarks`, `certificate_manager`, `downloads`, `extensions`, `history`, `password_manager`, `print_preview`, `settings/people_page`, `settings/settings`, `signin/profile_picker` — HTML patches
- `settings/reset_page/reset_profile_dialog.{html,ts}.patch` — Reset profile dialog customization
- `chrome/browser/ui/webui/settings/settings_ui.cc.patch`

### 12.7 macOS Platform
- `skia/ext/skia_utils_mac.mm.patch`
- `third_party/blink/renderer/platform/mac/graphics_context_canvas.mm.patch`
- `ui/display/mac/screen_utils_mac.mm.patch`
- `ui/gfx/image/image_unittest_util_apple.mm.patch`
- `services/shape_detection/text_detection_impl_mac_unittest.mm.patch`
- `components/os_crypt/common/keychain_password_mac.mm.patch`
- `chrome/common/chrome_paths_mac.mm.patch`
- `chrome/app/app-Info.plist.patch`
- `build/toolchain/apple/filter_libtool.py.patch`

### 12.8 Blink and Rendering
- `third_party/blink/renderer/core/css/css_default_style_sheets.cc.patch` — Default stylesheets
- `third_party/blink/renderer/core/html/resources/html.css.patch` — Default HTML CSS
- `third_party/blink/renderer/platform/graphics/paint/paint_controller.cc.patch` — Paint hook
- `components/global_media_controls/public/views/media_progress_view.cc.patch` — Media progress UI
- `media/base/media_switches.cc.patch` — Media switches

### 12.9 Build / WebUI Toolchain
- `chrome/browser/BUILD.gn.patch`, `chrome/browser/ui/BUILD.gn.patch`, `chrome/test/BUILD.gn.patch`, `chrome/chrome_paks.gni.patch`, `chrome/app/theme/chromium/BRANDING.patch` — Build graph
- `third_party/lit/v3_0/BUILD.gn.patch` — Lit framework integration
- `ui/webui/resources/tools/build_webui.gni.patch` + `ui/webui/webui_util.cc.patch` — WebUI build helpers
- `tools/gritsettings/resource_ids.spec.patch` — Resource ID allocation
- `tools/metrics/histograms/metadata/sql/histograms.xml.patch` — SQL histograms
- `chrome/browser/download/bubble/download_bubble_prefs.cc.patch`
- `chrome/browser/accessibility/soda_installer_impl.cc.patch`
- `components/performance_manager/user_tuning/prefs.cc.patch`

### 12.10 String Localization and Rebranding
- `chrome/app/*_strings.grd[p]` and `chromium_strings.grd` / `google_chrome_strings.grd` — Branded strings
- `components/*_strings.grdp` — Component strings (autofill, crash, error, history clusters, management, omnibox, policy, privacy sandbox, security interstitials, site settings, version, etc.)
- Settings localized-string provider patches — Dao settings page strings
- `components/resources/terms/terms_*.html` rebranding is handled by import-time rewrites, not patch files

## 13. Testing

- **browser_tests** (`dao_browser_browsertest.cc`) — Integration test framework
- **Coverage**: sidebar presence / default width / collapse-expand, drag resize, address bar, command bar show/hide & idempotency, tab CRUD, SplitView, CornerOverlay, folder persistence, URL detection heuristics, PiP site rules / interception / top bar overlay
- **Known disabled**: 5 `DaoAgentMemoryStore` tests `DISABLED_` due to FTS5 `RazeAndPoison` under direct `Init()` (see `MEMORY.md`)

## 14. Build and Packaging

- **`scripts/cli.ts`** — Unified CLI (download / import / export / build / rebuild / test / start) — the single entrypoint; **never run `autoninja` / `ninja` / `siso` directly**
- **`appdmg` integration** — DMG packaging
- **`configs/common.gn`** + **`configs/macos.gn`** — Shared + macOS GN args (component build, no NaCl, proprietary codecs, no Google API keys)
- **Custom checkout variables** — Build configuration flexibility
- **Vendor pipeline** — Agent WebUI dependency bundling (`npm run vendor`)

---

## Current Status

- **Version**: 0.3.0 (based on Chromium 147.0.7727.135)
- **Target platform**: macOS arm64
- **Source footprint**: ~45 C++ component pairs under `src/dao/` + 166 patches under `src/patches/`
