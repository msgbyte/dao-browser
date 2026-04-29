# Dao Browser Feature Inventory

> This document catalogs all features Dao Browser adds on top of Chromium 147.0.7727.135. Dao-owned code lives in `src/dao/`; Chromium integration patches live in `src/patches/`.

## 1. Vertical Sidebar

An Arc-inspired vertical sidebar replaces Chromium's top tab strip — the single biggest UI change in Dao Browser.

### 1.1 Sidebar Core
- **DaoSidebarView** — Main container, 240px default, collapsible to 4px with animation
- **Drag-to-resize** — Mouse drag, 150–400px range, ignored while collapsed
- **Collapsible sections** (`DaoSidebarSectionView`) — Reusable section container
- **Space switcher** (`DaoSpaceBarView`) — Workspace switching
- **Sidebar context menu** — Right-click menu support

### 1.2 Tab Management
- **Vertical tab list** (`DaoTabListView` / `DaoTabItemView`) — Replaces the top tab strip
- **Active tab dual-line layout** — Active tab shows title + URL on two lines
- **Tab tooltip** (`DaoTabTooltipView`) — Hover contextual info next to the sidebar
- **Tab context menu** — Copy link, pin, close, etc.
- **Tab duplication** — Based on distinct sidebar tab IDs
- **Detach guards** — Prevents accidental reordering while dragging
- **URL copy toast notification** — Feedback after copy
- **Favicon lightness detection** — Visibility adjustment on dark backgrounds
- **Retina 2x images** — High-DPI image quality

### 1.3 Favorites
- **DaoFavoritesView** — Pinned site icon row
- **Folder management** (`dao_folder_item.ts` / `dao_folder_model.ts`) — Persisted to profile path, load/save support

### 1.4 New Tab and Command Bar
- **DaoNewTabButton** — New-tab button in the sidebar
- **DaoCommandBarView** — Spotlight-style command bar
  - Translucent scrim + floating panel + frosted glass + layered shadows
  - Keyboard-first: arrow keys to select, Tab to complete, Esc to dismiss
  - Cmd+L focuses the location bar (pre-fills current URL)
  - Cmd+T opens a blank tab (remembers previous tab; Esc reverts to it)
  - **Ask AI** — Submit prompt directly to the Agent
  - URL detection heuristics + ghost-text completion
- **DaoSuggestionItemView** — Suggestion item view

### 1.5 Downloads
- **DaoDownloadButton** / **DaoDownloadFlyoutView** — Download flyout
- **File icon util (mac)** — Native macOS file icon retrieval

### 1.6 Content Area Styling
- **DaoCornerOverlayView** — 10px rounded corners + 6-step soft shadow overlay on web contents
- **Adaptive theming** — Content area switches light/dark text and separators based on page luminance
- **Address bar** (`DaoAddressBarView`) — Embedded URL pill (14px radius), adapts text color to background
- **Color system** (`dao_colors.h/.cc`) — Shared blue accent (70,120,190) across light/dark modes
- **Lucide icon library** (`dao_lucide_icons.h/.cc`) — Unified icon set

## 2. AI Agent System

Dao Browser ships a full AI agent stack: LLM tool calling, long-term memory, proactive suggestions, and a skill system.

### 2.1 Agent Core Services
- **DaoAgentMemoryService** — Profile-keyed long-term memory service (SQLite backend, background sequenced execution)
- **DaoAgentMemoryStore** — SQLite FTS5 full-text search store
- **DaoAgentProactiveEngine** — Navigation-triggered proactive suggestion engine
  - Scenario matching (URL pattern + page hints)
  - Episodic memory matching
  - Learning pipeline triggers
- **DaoAgentScenarioRegistry** — Scenario registry (seed + personal)
- **DaoAgentSkillService** — Skill system

### 2.2 Agent UI
- **DaoAgentSidebarView** — Agent sidebar container (WebUI-driven, preloaded for fast toggle)
- **DaoAgentCursorView** — Visualizes AI actions: animated cursor + element highlight
- **DaoAgentLockBannerView** — Animated lock banner while AI controls a tab
- **DaoAgentLockTabHelper** — Tab helper for lock state

### 2.3 Agent WebUI (chrome://dao-agent)
- **dao_chat_view.ts** — Main conversation view
  - Session resume (reopens the most recent conversation)
  - Conversation compaction for context management
  - Skill picker
  - Page capture — convert current page to markdown and insert into the message
  - Selection capture
  - Dynamic chip refresh + composer height tracking
  - Share image generation
  - Cost stats and usage tracking
- **dao_settings_view.ts** — Expandable tool groups with persistent state
- **dao_skill_manager_view.ts** — Skill management
- **dao_chat_history_panel.ts** — History panel
- **dao_tool_renderer.ts** — Tool-call result rendering
- **LLM tool set** (see `agent-console-api.md`):
  - Page access: `get_page_content` / `get_page_info` / `get_readable_content`
  - Page interaction: `click_element`, fill, scroll, etc.
  - Accessibility tree generation and interaction
  - Resource inspection (reverse-engineering skills)

### 2.4 Vendor Pipeline
- **npm run vendor** — Compiles pi-mono / pi-web-ui and related deps from `vendor.config.ts`
- Artifacts: `pi_runtime_bundle.ts` / `pi_web_ui.css` (never hand-edit)

## 3. Picture-in-Picture Enhancements

Built on Chromium's native PiP, adds a "Document PiP interception" layer.

- **DaoPipInterceptor** — Intercepts PiP requests on configured sites and redirects to Document PiP with a specific DOM element
- **DaoPipSiteRules** — Site rules (`pip_site_rules.json`)
- **PiP top bar overlay** — Custom top bar on Document PiP windows
- **Capturer guard** — Prevents tab throttling
- **User-interaction event forwarding** — Improves Document PiP responsiveness
- **Auto PiP triggers** — On window minimize / tab switch

## 4. Split View

- **DaoSplitView** — Split container
- **DaoSplitNode / DaoSplitPaneView** — Split tree nodes and panes
- **DaoSplitDividerView** — Draggable divider
- Status: wired up but not enabled by default

## 5. Control Center

macOS-style floating control center panel bundling extensions and utilities.

- **DaoControlCenterButton** — Trigger button
- **DaoControlCenterPopup** — Floating popup with transparent overlay click-to-close
- **DaoControlCenterExtensionsSection** — Extensions grid
- **DaoControlCenterUtilitySection** — Utility buttons (share, QR, lock, more)
- **DaoControlCenterQrView** — QR code generation
- **DaoControlCenterMoreMenu** — More menu
- **DaoPinnedExtensionsContainer** — Pinned extension icon container
- **DaoNativeShareMac** — Native macOS share sheet

## 6. Media Controls

- **dao_media_control.ts** — Sidebar media playback controls
- **Media session state management** — User-pause tracking
- **DaoSidebarUIHandler** — Media state handling

## 7. Welcome Page

- **DaoWelcomeUI** — First-run welcome page (`chrome://dao-welcome`)
- **Menu item + command handling** — User can reopen anytime
- **User preference tracking** — Auto-opens only on first launch

## 8. Little Dao Window

Lightweight window form factor for popups / mini-tools.

- **DaoLittleDaoController** / **DaoLittleDaoView** — Little Dao top bar (48px)
- **Hostname display + "Open in Dao" button**
- **Browser::Create timing control** — Static-flag pattern to pass state into BrowserView construction
- macOS traffic-light repositioning

## 9. Branding and Visuals

- **Dao brand assets** — Logos / SVGs; product name globally rebranded ("Chromium" → "Dao")
- **chrome:// → dao://** — Internal URL schemes rewritten
- **Chromium string rebranding** — 93+ string resources localized
- **Custom scrollbar** (`css_default_style_sheets`) — Globally customized scrollbar styling
- **DaoToastView** — Lightweight toast notifications with fade-in/fade-out

## 10. Shortcuts and Menus

- **macOS global shortcuts** (`global_keyboard_shortcuts_mac.mm.patch`)
- **macOS main menu customization** (`main_menu_builder.mm.patch` / `accelerators_cocoa.mm.patch`)
- **IDC_\* command ID extensions** (`chrome_command_ids.h.patch`)
- **browser_command_controller / browser_commands** — Command handling extensions

## 11. Chromium Core Integration Patches

Non-UI deep integration changes.

### 11.1 URLs and Schemes
- `dao://` scheme registration (`content/common/url_schemes.cc`)
- WebUI controller factory / config extensions
- URL data source / loader factory extensions
- URL fixer adaptations

### 11.2 BrowserView Integration
- `browser_view.{h,cc}` / `browser_view_layout` / `browser_frame_mac` / `browser_native_widget_mac` — Inject Dao sidebar and overlay
- `contents_web_view` / `contents_layout_manager` — Content area layout adjustments
- `immersive_mode_controller_mac` — Immersive mode adaptation
- Hide top tab strip (keep toolbar off-screen at y=-height rather than invisible)

### 11.3 Startup Flow
- `startup_browser_creator` / `startup_browser_creator_impl` — Startup behavior
- `infobar_utils` — Infobar suppression
- `session_startup_pref` / `browser_prefs` — Preferences
- `account_consistency_mode_manager` — Account consistency mode

### 11.4 Extensions and Other
- Extension popup layout (`extension_popup.cc`)
- Web request event router extensions
- Simple feature extensions (extension allowlist)
- WebUI HTML patches: Password Manager / Settings / Extensions / Bookmarks / History / Downloads
- Reset profile dialog customization
- Profile picker customization

### 11.5 macOS Platform Patches
- Skia utils / graphics context canvas
- Screen utils / image unittest util
- Keychain password adaptations
- Filter libtool toolchain

### 11.6 Third-Party and Blink
- `third_party/blink/renderer/core/css/css_default_style_sheets` — Default stylesheets
- `html.css` / `paint_controller` — Rendering tweaks
- `third_party/lit/v3_0` — Lit framework build integration

## 12. Testing

- **browser_tests** (`dao_browser_browsertest.cc`) — Integration test framework
- Coverage: sidebar presence / default width / collapse-expand, drag resize, address bar, command bar show/hide, tab CRUD, SplitView, CornerOverlay, folder persistence, command bar idempotency, URL detection heuristics, PiP site rules / interception / top bar overlay

## 13. Build and Packaging

- **scripts/cli.ts** — Unified CLI (download / import / export / build / rebuild / test / start)
- **appdmg integration** — DMG packaging
- **Custom checkout variables** — Build configuration flexibility
- **configs/common.gn** + **configs/macos.gn** — GN config
- **Vendor pipeline** — Agent WebUI dependency bundling

---

## Current Status

- **Version**: 0.3.0 (based on Chromium 147.0.7727.135)
- **Target platform**: macOS arm64
- **Source footprint**: ~50+ C++ components under `src/dao/` + ~180+ patch files under `src/patches/` (93 non-string patches)
