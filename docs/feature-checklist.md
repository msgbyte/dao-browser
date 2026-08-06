# Dao Browser — Feature & Patch Checklist

> **Purpose.** This is the **rebase-verification checklist**. When you bump the Chromium
> kernel version (`dao.json` → `version.version`), walk this document top to bottom and
> confirm every feature still works. It maps each Dao feature to the patches / source
> that implement it, rates each patch's **rebase conflict risk**, and gives a concrete
> **verify-after-upgrade** step.
>
> Read alongside [`chromium-upgrade-guide.md`](chromium-upgrade-guide.md) (the *how* of a
> rebase) and [`features.md`](features.md) (the prose feature tour). This file is the
> *checkbox* view.

## How to use this document

1. Before starting, confirm the current baseline builds and all features here pass (this
   is your "known-good" reference).
2. After `npm run import` on the new kernel, resolve conflicts patch-by-patch. Use the
   **Rebase risk** column to prioritize the High-risk patches — those are where a hunk
   silently drops or mis-applies.
3. After the build passes, run the **Verify** step for every row. A patch that *applied
   cleanly* is **not** proof the feature works — context-fuzzy application and
   silently-lost default flips are the main failure mode.

## Risk legend

| Risk | Meaning | Rebase attention |
|------|---------|------------------|
| 🔴 **High** | Touches hot / frequently-refactored upstream code, replaces whole functions, or depends on churny APIs | Expect conflicts; re-read the hunk against new upstream |
| 🟡 **Medium** | Moderate churn, structural anchors that drift, or default-value flips that silently revert | Diff carefully; verify behavior even if it applied |
| 🟢 **Low** | Additive members/includes/new files; isolated constants | Usually applies clean; still compile-check |

**Silent-loss patterns** (apply cleanly but revert behavior — always re-verify these):
`BASE_FEATURE` default flips, `return false`/`return LAST` hardcodes, `#if 0` wrappers,
`DCHECK` removals, single-token default changes, string-literal substitutions.

Rows with **Risk = —** are Dao-owned source features rather than Chromium patch hunks.
Keep them in the checklist anyway: WebUI handlers, native bridges, and feature-specific
controllers can regress after an upgrade even when every patch applies cleanly.

---

## 0. Architectural seams (verify these first — they underpin everything)

Three cross-cutting mechanisms that many features depend on. If one of these breaks, a
whole cluster of features breaks with it.

- [ ] **`dao://` scheme** — Dao renames the WebUI scheme from `chrome` to `dao` and keeps
  a `chrome://` compatibility layer. Implemented across ~15 patches (see §7). **A single
  missed spot silently breaks a WebUI page or subresource.**
- [ ] **`//dao/...` sidecar source tree** — All Dao C++ lives under `engine/src/dao/`
  (copied from `src/dao/`) and is wired into the build via `.gni` source lists + BUILD.gn
  patches (§10). Nearly every C++ patch `#include`s a `dao/browser/...` header; if the
  tree moves or a symbol renames, those patches fail to compile even after applying.
- [ ] **Generated rewrites** (not patches) — `scripts/chromium-rewrites.ts` mechanically
  rewrites `chrome://`→`dao://` in ~30 grd/html/json files at import time. These are
  **not** in `src/patches/`. If import reports missing rewrite targets, the file list in
  that script is stale (see §7.3).

---

## 1. Vertical Sidebar & Window Shell

The single biggest UI change: replaces Chromium's top tab strip with a left vertical
sidebar, insets + rounds the content area, and re-homes the toolbar off-screen.

| ✔ | Feature | Patch(es) / Source | Risk | Verify after upgrade |
|---|---------|--------------------|------|----------------------|
| ☐ | Sidebar injected into window; top tab strip hidden | `views/frame/browser_view.cc.patch` (+ `.h`) | 🔴 | Window opens with vertical tabs, no top tabstrip |
| ☐ | Tabbed-window layout: sidebar carve-out, content inset + rounded corners + shadow, toolbar parked at `y=-height`, bookmark bar force-hidden | `views/frame/layout/browser_view_tabbed_layout_impl.cc.patch` | 🔴 | Sidebar at preferred width; content inset with rounded lower corners; omnibox still works though toolbar off-screen |
| ☐ | Content background override (matches theme behind rounded corners) | `views/frame/contents_web_view.{cc,h}.patch` | 🟢 | No white/letterbox flash behind content |
| ☐ | Page-load progress bar over rounded content card | `browser_view.{cc,h}.patch`, `layout/browser_view_tabbed_layout_impl.cc.patch`, `src/dao/.../dao_load_progress_*` | 🔴 / — | Slow navigation shows a thin top progress bar driven by real `WebContents` progress; stop/complete hides/fades it without layout shift |
| ☐ | Split-view content host swap (keeps `ContentsWebView` alive as 3×1px placeholder) | `views/frame/contents_layout_manager.cc.patch` | 🟡 | Split fills content area; single-pane mouse events reach web view; status bubble anchors |
| ☐ | Fullscreen keeps sidebar (no immersive top-chrome) | `views/frame/browser_view.cc.patch`, `immersive_mode_controller_mac.mm.patch` (zero-size clamp) | 🔴 / 🟢 | Enter fullscreen → sidebar stays, no crash |
| ☐ | Permission/PageInfo bubbles anchor to Dao address bar | `views/bubble_anchor_util_views.cc.patch` (includes only — logic lives elsewhere) | 🟢 | Bubbles anchor to address bar, not top-left corner |
| ☐ | macOS titlebar height, frosted-glass translucency, Cmd+S → sidebar | `views/frame/browser_native_widget_mac.mm.patch` | 🔴 | Traffic lights centered; sidebar translucent; Cmd+S handled by sidebar |
| ☐ | Sidebar C++/WebUI core (240px, drag-resize 150–400px, collapse anim) | `src/dao/.../sidebar/` + `dao://dao-sidebar` WebUI | — | Sidebar renders; drag-resize clamps; collapse/expand preserves width |
| ☐ | Incognito sidebar shield indicator | `src/dao/.../sidebar/dao_sidebar_view.cc`, `dao_strings.grd` | 🟢 | Normal window has no shield; Incognito shows it left of collapse with localized privacy tooltip |
| ☐ | Native tab context-menu shortcut labels | `dao_sidebar_ui.cc` | 🟡 | Right-click a regular tab → Duplicate Tab shows ⌘D, Copy Link shows ⌘⇧C, and Close Tab shows ⌘W; each shortcut still performs the matching action |
| ☐ | Pinned sites/tabs grid, stable backing identity, and dormant pinned items | `src/dao/.../sidebar/dao_pinned_tabs_grid.ts`, `dao_pinned_tab_model.{h,cc}`, `dao_pinned_tab_storage.{h,cc}`, `dao_tab_identity.{h,cc}`, `dao_sidebar_ui.cc`, `sessions/session_service.cc.patch` | `DaoPinnedTabModelTest.*`, `DaoPinnedTabStorageTest.*`, `DaoSidebarBrowserTest.*Pinned*`, `DaoSidebarBrowserTest.ActivatingPinnedItemReusesTabFromAnotherWindow`, `DaoSidebarBrowserTest.ClosingPinnedItemFromAnotherWindowMakesItDormantAndReopenable`, `DaoSidebarBrowserTest.UnpinningPinnedItemFromAnotherWindowMovesItHere`, `DaoTabBrowserTest.SidebarTabIdentity*`, `pinned_tabs_grid.test.ts` | Pin and activate without changing tab count; navigate; discard/replace WebContents; replace then navigate; restore the session after command compaction and preserve the backing identity; activate the same Pin from another window and focus the existing tab without increasing either window's tab count; close it remotely and synchronize dormant state across windows; drag a remote Pin into the current ordinary tab list and move the backing tab into this window; block clicks and unpin drags until restore completes; close then reopen once; double-click; create two identical-URL tabs and preserve exact ownership; create a legacy identity conflict and verify active tab/tab count remain unchanged; reject partially invalid migration data without dropping Pins; verify failed atomic persistence preserves the previous file; drag/reorder does not corrupt state |
| ☐ | Active sidebar tab stays visible after tab switches | `dao_sidebar_app.ts`, `dao_tab_item.ts`, `dao_folder_item.ts`, sidebar WebUI tests | — | Scroll the tab list away from a visible tab, activate it by keyboard or another surface, and verify it smoothly enters the nearest viewport edge; activate a child of a collapsed folder and verify the folder expands, persists the expanded state, and scrolls the child into view |
| ☐ | Sidebar folder model + configurable stale-tab actions | `src/dao/.../sidebar/dao_folder_model.ts`, `dao_sidebar_app.ts`, `dao_sidebar_ui.cc`, Settings Dao page patches | `DaoSidebarBrowserTest.StaleTabExpirationPrefDefaultsTo24Hours`, `sidebar_app.test.ts`, `dao_page_test.ts` | "You and Dao" accepts only integer hours from 1–720 and defaults to 24; invalid input is not saved; "Move Stale Tabs to stale" reads the current profile value, creates/updates the `stale` folder only for qualifying ordinary tabs, expands it, and persists folder membership. Right-click `stale` → Clear Stale Tabs is present; ordinary folders omit it; the Dao native system confirmation dialog appears; Cancel preserves tabs/folder; Clear closes only current `stale` children and deletes the folder. |
| ☐ | Sidebar utility controls: downloads, media controls, update button | `dao_download_button.ts`, `dao_download_hover_details.*`, `dao_tab_tooltip_view.*`, `dao_media_control.ts`, `dao_update_button.ts`, `dao_sidebar_ui.cc` | — | Active download rows retain progress/cancel behavior; a settled 400ms hover shows a native popup beyond the sidebar boundary with full filename, known transferred/total size and percentage, positive speed, and reliable remaining time. Long filenames wrap without truncation; hovering the lowest row flips the popup above its anchor and keeps all lines inside the browser window. Moving resets the delay; leaving, cancelling, or removing the download hides it. Unknown totals degrade to transferred size only; unavailable speed/time are omitted. Verify light/dark themes and confirm the recent-files popup still works. Tab media play/pause works; ready update state appears and `applyReadyUpdate` is invoked |
| ☐ | Sidebar close/reorder motion | `src/dao/.../sidebar/dao_flip_motion.ts`, sidebar WebUI tests | — | Closing, moving, pinning, and foldering tabs animates surviving rows without duplicate placeholders or stale transforms |

> **Note.** `browser_view.cc.patch` hooks many volatile methods (`OnActiveTabChanged`,
> `NonClientHitTest`, immersive predicates, `multi_contents_view_` interaction). Budget
> the most rebase time here alongside the tabbed layout impl.

## 2. Command Bar (Spotlight-style)

| ✔ | Feature | Patch(es) / Source | Risk | Verify after upgrade |
|---|---------|--------------------|------|----------------------|
| ☐ | Cmd+T in sidebar window shows command bar instead of blank tab | `chrome/browser/ui/browser_commands.cc.patch` (`NewTab()` hook) | 🔴 | Cmd+T shows command bar; programmatic/restore new-tabs still create real tabs |
| ☐ | Cmd+L pre-fills current URL | `views/frame/browser_view.cc.patch` (`SetFocusToLocationBar` redirect) | 🔴 | Cmd+L opens command bar with URL |
| ☐ | Command bar UI + suggestions + Ask AI | `src/dao/.../dao_command_bar_view.*`, `dao_suggestion_item_view.*` | `DaoCommandBarBrowserTest.RightArrowFillsExplicitlySelectedSuggestion` | Arrow-key select; Right Arrow fills the explicitly selected suggestion without navigating; Tab-complete; Esc dismiss; Ask AI routes to agent |

## 3. AI Agent System

Flagship feature. C++ services + `dao://dao-agent` WebUI + vendor runtime.

| ✔ | Feature | Patch(es) / Source | Risk | Verify after upgrade |
|---|---------|--------------------|------|----------------------|
| ☐ | 4 keyed-service factories registered (memory, skill, workspace, dream) | `profiles/chrome_browser_main_extra_parts_profiles.cc.patch` | 🟡 | Services instantiate per profile; agent features work |
| ☐ | Agent WebUI host allowed to make network requests (LLM API) | `webui/chrome_web_ui_controller_factory.cc.patch` (`origin.host()=="agent"`) | 🟢 | `dao://agent` reaches external LLM endpoints |
| ☐ | 7 Dao WebUI configs registered (agent, dream, index, memory, sidebar, skills, welcome) | `webui/chrome_web_ui_configs.cc.patch` | 🟡 | All 7 pages load; confirm upstream `SkillsUIConfig` still exists (dao takes over `dao://skills`) |
| ☐ | Agent tab helpers, cursor overlay, lock banner | `ui/tab_helpers.cc.patch` + `src/dao/.../agent/`, `dao_agent_*_view.*` | 🟢 | AI action cursor + lock banner show while agent controls a tab |
| ☐ | Window-scoped Agent/MCP tab tools with stable identities | `src/dao/.../automation/dao_tab_tools.{h,cc}`, `dao_browser_automation_session.{h,cc}`, `dao_tab_identity.{h,cc}`, `dao_agent_ui.cc`, `browser_tool_catalog.json` | 🟡 | Run `DaoMcpTabToolsBrowserTest.*`, `DaoMcpSessionTest.*`, `DaoSidebarTabIdentityBrowserTest.*`, and `agent_bridge_call_native.test.ts`; verify WebContents replacement keeps the target, restored IDs reconcile uniquely, duplicate selectors fail closed, open rejects non-HTTP(S) schemes without mutation, constrained insertion returns the actual new tab, and unload-gated close reports the accepted/cancelled outcome once before Agent target-state cleanup |
| ☐ | Session-scoped Agent/MCP DevTools tools | `src/dao/.../automation/dao_devtools_tools.{h,cc}`, `dao_devtools_client.{h,cc}`, `dao_browser_automation_session.{h,cc}`, `dao_agent_ui.cc`, `agent_bridge.ts`, `browser_tool_catalog.json` | 🟡 | Run `DaoMcpDevToolsBrowserTest.*`, the Page/Tab MCP regression filters, `agent_bridge_call_native.test.ts`, `pi_tool_adapter.test.ts`, and `dao_chat_view.test.ts`; verify enable-window staging commits only after a matching generation/host success, cancellation/failure/rebinding drops pending staging, clear removes committed and pre-clear staged events while preserving the pending attempt and later events, monotonic same-binding domain confirmation across reordered success/failure, aggregate network/console byte budgets below entry caps, UTF-8-safe truncation, strict current-tree `(frame, URL)` size preflight before content fetch, an independent response-size backstop, exact in-budget base64, failed/oversized Script and Document search incompleteness, item/URL-byte/depth/dedup/source/4 MiB scan limits, re-entrant resolver/command destruction, exactly-once cancellation, and target/host/origin/document rebinding |
| ☐ | Default-off process-global local MCP server | `src/dao/.../mcp/dao_mcp_{service,transport,connection,protocol,runtime_files}.*`, `dao_pref_names.*`, `browser_prefs_mcp.cc.patch`, `chrome_browser_main_extra_parts_profiles.cc.patch` | 🔴 | Run `DaoMcpServiceBrowserTest.*`, `DaoMcpProtocolTest.*`, and `DaoMcpRuntimeFilesTest.*`; verify browser-IO-thread listener ownership, owner-only runtime permissions, nonce rotation, same-UID authentication, protocol/version/line limits, 64-request/8 MiB unconsumed-ingress credits, terminal-request logical closing before later same-batch tools, aggregate write backpressure, bounded graceful-close drains, one-client admission, idle hello/catalog discovery beyond the approval timeout without a prompt or disconnect, exact last-active-window selection and approval on the first tool call, pre-approval catalog access even when connection begins on Dao Settings, re-entrant approval cancellation denial, approval plus lease ordering, cancellation, and complete lease/runtime cleanup after disconnect, disable, and shutdown |
| ☐ | Settings MCP master switch, connection, quick setup, and Stop | `src/dao/.../mcp/dao_mcp_settings_handler.{h,cc}`, `resources/settings/dao_page/dao_page.{html,ts}.patch`, `webui/settings/settings_ui.cc.patch` | 🟡 | Run `DaoMcpInstallCommandTest.*`, `DaoMcpSettingsHandlerTest.*`, `DaoMcpSettingsPageBrowserTest.*`, and `DaoPage`; verify one header/connection/enabled-only-setup card, responsive selector/copy alignment, and text status updates through `dao-mcp-status-changed`. The switch must write process-global Local State rather than `prefs.dao`; client details and Stop appear only for an active authorized lease. Confirm setup is absent while disabled; when enabled it defaults to Codex and switches to user-scoped Claude Code or Generic MCP. CLI previews stay single-line, Generic MCP preview and clipboard are identical Chromium-native three-space pretty JSON, and malformed Generic JSON fails closed without changing the clipboard. Also verify option-specific feedback, POSIX-safe helper and current user-data-directory arguments, Debug/custom-profile endpoint binding, stale preview rejection, listener cleanup, and absence of the standalone configuration button. |
| ☐ | Native MCP stdio helper and macOS app bundling | `src/dao/.../mcp/helper/`, `dao_mcp_helper_browsertest.cc`, `dao_version.gni`, `chrome/BUILD_mcp_helper.gn.patch` | 🔴 | Run `DaoMcpHelperBrowserTest.*`; verify all 29 tools survive catalog adaptation, MCP `2025-11-25` and Codex-compatible `2025-06-18` negotiation with initialized gating, the `codex/tool-catalog-cache.cacheable=false` compatibility capability, string/numeric IDs, object/scalar/list `structuredContent`, real screenshot MIME, `isError` failures, cancellation with no late response, disabled-browser stderr determinism, JSON-only stdout, and executable copies at both the build output and `Dao.app/Contents/Helpers/dao-mcp` |
| ☐ | Local MCP approval, exact-window banner, Stop, and peer-agent busy UX | `dao_mcp_approval_dialog.{h,cc}`, `dao_mcp_control_banner_view.{h,cc}`, `dao_mcp_service.{h,cc}`, `dao_agent_ui.{h,cc}`, `pi_tool_adapter.ts`, `browser_view.{cc,h}.patch`, `browser_view_tabbed_layout_impl.cc.patch` | 🔴 | Run `DaoMcpApprovalDialogTest.*`, `DaoMcpControlBannerTest.*`, `DaoMcpPeerLeaseTest.*`, `pi_tool_adapter.test.ts`, and `dao_chat_view.test.ts`; verify localized reported client/version, available verified PID, window, and Profile rendering, no default Allow action, deny/close/parent destruction exactly once and fail closed, banner visibility only in the exact authorized normal Browser, no address/content overlap, clickable Stop in the non-client strip, Stop cancellation/lease release/disconnect/listening transition, chat continuity, and pre-CDP `AGENT_CONTROL_BUSY` browser-tool failures |
| ☐ | MCP exact-target eligibility and terminal lifecycle | `automation/dao_browser_target_policy.{h,cc}`, `mcp/dao_mcp_session_lifecycle_monitor.{h,cc}`, `dao_mcp_end_to_end_browsertest.cc`, `dao_mcp_service.{h,cc}` | 🔴 | Run `DaoMcpEndToEndBrowserTest.*` and the lifecycle filters in `DaoMcpServiceBrowserTest.*`; verify HTTP/HTTPS/literal blank/web-hosted PDF allow, popup/OTR/Guest/internal/extension/DevTools/Agent WebUI/file/data/custom rejection for execution without blocking catalog discovery, exact-owner `TARGET_GONE`, no tab fallback, pre-mutation forbidden switch rejection, Browser/Profile/target/navigation terminal cleanup, no Ready/Disabled status during enabled logical closing, and replacement admission only after the accepted socket disconnects |
| ☐ | MCP startup, packaging, protocol, UI, and rebinding regression sweep | `browser_prefs_mcp.cc.patch`, `chrome_browser_main_extra_parts_profiles*.patch`, `chrome/BUILD_mcp_helper.gn.patch`, `mcp/`, `dao_mcp_approval_dialog.*`, `dao_mcp_control_banner_view.*`, Settings Dao page patches | 🔴 | After Chromium upgrades, verify Local State registration and clean startup/shutdown, owner-only Unix socket/metadata plus helper executable packaging, protocol framing/version/8 MiB and ingress/write bounds, DevTools attach/cancel/detach, approval and banner BrowserView layout, Settings switch/status/enabled-only quick setup/Copy/Stop, stable tab identity across reorder/restore/WebContents replacement, and complete target rebinding/cleanup |
| ☐ | Agent page, selection, element-context, element-screenshot, and PDF-text attachments | `src/dao/.../agent/dao_chat_view.ts`, `dao_page_capture.ts`, `dao_agent_ui.cc` | — | Composer can attach current page, selected text, picked element DOM context, picked element screenshot, and PDF text without losing existing chips |
| ☐ | Agent message actions and code-block insertion | `dao_chat_view.ts`, `dao_share_image.ts`, `dao_page_capture.ts` | — | Copy/share image/regenerate/edit/rewind work on the intended message; code-block insert appears only with a focused page input and inserts at cursor |
| ☐ | SQLite `Statement::ColumnName()` accessor (agent memory DB) | `sql/statement.{cc,h}.patch` | 🟢 | `//sql` compiles; agent memory store links |
| ☐ | Agent memory histogram variant | `tools/metrics/histograms/metadata/sql/histograms.xml.patch` | 🟢 | `validate_format.py` passes |
| ☐ | Agent long-term memory store, memory context, and memory inspector | `src/dao/.../agent/dao_agent_memory_*`, `dao_memory_context.ts`, `dao_memory_app.ts`, `dao_memory_table.ts`, `dao_settings_view.ts` | — | Memory settings toggles persist; conversation/page context is saved and retrieved; `dao://memory` runs read-only SQL and clear/usage controls work |
| ☐ | Agent proactive suggestions | `src/dao/.../agent/dao_agent_proactive_*`, `dao_agent_ui.cc`, `dao_chat_view.ts`, `dao_settings_view.ts` | — | Navigation/dwell can surface a suggestion only when enabled; run/dismiss records feedback; quiet/balanced/active settings affect behavior |
| ☐ | Agent skills and tool catalog | `src/dao/.../agent/dao_agent_skill_*`, `dao_skill_manager_view.ts`, `skill_registry.ts`, `tool_catalog.ts`, `skills.html` | — | `dao://skills` lists built-in/user skills; enabling/disabling persists; activated skills and tool groups appear in chat settings |
| ☐ | Agent workspace tools + safety guards | `src/dao/.../agent/dao_agent_workspace_*`, `src/dao/.../agent/workspace/`, `agent/workspace/bridge.ts`, `tool_catalog.ts` | — | `workspace_read/write/edit`, `apply_patch`, `list_files`, `download`, and open-folder calls work; quota, path normalization, text-only filter, and audit log reject unsafe writes |
| ☐ | Agent web tools search/fetch tiering | `src/dao/.../resources/agent/web_search/`, `dao_settings_view.ts`, `dao_agent_ui.cc` | — | Provider built-in search is preferred; Auto mode uses configured Jina Search before DuckDuckGo HTML; DuckDuckGo anomaly/verification pages report that accurately instead of `HTML structure changed`; `fetch_url` still falls back from Jina Reader to browser fetch |
| ☐ | Dream scheduler and Agent settings controls | `src/dao/.../agent/dao_dream_service.*`, `dao_settings_view.ts`, `dream_bridge.ts` | — | Dream remains off by default; enabling requires memory; nightly/catch-up/manual runs honor idle/time/date gates and show status/history in settings |
| ☐ | Dream material privacy and excluded-domain filtering | `src/dao/.../agent/dao_dream_material_collector.*`, `dao_dream_domain_utils.*`, `dao_pref_names.*` | — | Excluded domains are normalized and removed before titles/search queries/debug material leave C++; stats do not leak excluded domain names |
| ☐ | Dream report page, rerun, sharing, and habit feedback | `dao_dream_app.ts`, `dao_dream_runner.ts`, `dao_share_image.ts`, `dao_agent_ui.cc` | — | `dao://dream` loads today/history; rerun by date replaces only on success; failed rerun preserves existing report; share image, debug view, confirm/reject habit actions work |

## 4. Picture-in-Picture Enhancements

| ✔ | Feature | Patch(es) / Source | Risk | Verify after upgrade |
|---|---------|--------------------|------|----------------------|
| ☐ | Auto-PiP for *any* video site + Document-PiP interception on configured sites | `picture_in_picture/auto_picture_in_picture_tab_helper.{cc,h}.patch`, `src/dao/browser/pip/dao_pip_interceptor.{cc,h}` | 🔴 | Tab-switch on a plain `<video>` page auto-opens PiP; configured site opens Document-PiP and does NOT immediately close; returning to its opener tab closes a manually or automatically opened window and restores the moved player element to the original page |
| ☐ | Per-site Document-PiP bounds persistence + no permission overlay | `picture_in_picture/picture_in_picture_window_manager.{cc,h}.patch` | 🔴 | Resize/close/reopen restores size; **no permission bubble** (security-relevant removal — review each rebase) |
| ☐ | Floating auto-hiding Document-PiP top bar (separate overlay widget, hover fade, corner-resize, drag-move) | `views/frame/picture_in_picture_browser_frame_view.{cc,h}.patch` | 🔴 | Top bar hidden initially, fades on hover; drag moves; corner drags resize; content fills width. **Confirm `render_active_` default stays `false`** |
| ☐ | PiP overlay button re-layout (back-to-tab top-left, minimize adjacent) | `views/overlay/back_to_tab_button.cc.patch`, `minimize_button.cc.patch` | 🟢 | Buttons at top-left, no overlap |
| ☐ | Video-PiP rounded corners (10px, translucent) | `views/overlay/video_overlay_window_views.cc.patch` | 🟢 | Rounded corners, no white corner artifacts |
| ☐ | Full-work-area maximum for Document PiP + video PiP | `picture_in_picture/picture_in_picture_window_manager.cc.patch`, `views/overlay/video_overlay_window_views.cc.patch`, `src/dao/.../pip/dao_pip_resize_utils.h` | 🟡 | Manually enlarge each PiP type to the full usable display area; initial size remains unchanged; moving to a smaller display clamps the window inside that work area |
| ☐ | PiP seek works without a `seekto` handler | `content/.../video_picture_in_picture_window_controller_impl.cc.patch` | 🟡 | Scrub progress bar seeks on a site with no custom handler |
| ☐ | macOS history-swipe overlay clipped | `renderer_host/chrome_render_widget_host_view_mac_delegate.mm.patch` | 🟢 | Back/forward swipe overlay stays clipped |
| ☐ | Dao dark blue-gray PiP window theme | `ui/color/chrome_color_mixer.cc.patch` | 🟡 | PiP uses Dao colors; check newly-added `kColorPipWindow*` tokens for unthemed defaults |
| ☐ | Media progress bar fills flush to playhead | `components/global_media_controls/.../media_progress_view.cc.patch` | 🟢 | No gap between fill and indicator |
| ☐ | PiP interceptor + auto-PiP visibility tab helpers | `ui/tab_helpers.cc.patch` + `src/dao/browser/pip/` | 🟢 | Interception + minimize-triggered auto-PiP work |
| ☐ | Doc-PiP permission-prompt / SODA-failure-log flags enabled | `media/base/media_switches.cc.patch` | 🟡 | Both flags still exist + default enabled |

> **Highest-risk trio:** auto-PiP eligibility rewrite, window-manager overlay/bounds
> rewrite, floating top-bar frame view. All three call into `dao/browser/pip/`
> (`DaoPipInterceptor`, `GetPersistedPipBoundsForSite`, `ResizePipWindowFromOverlayCorner`)
> — verify those symbols still match after the sidecar builds.

## 5. Split View

| ✔ | Feature | Patch(es) / Source | Risk | Verify after upgrade |
|---|---------|--------------------|------|----------------------|
| ☐ | Split container / panes / divider | `src/dao/.../split/` + `contents_layout_manager.cc.patch` | 🟡 | Split creates two panes (wired but off by default) |
| ☐ | Native tab-drag cleanup restores WebContents hit testing | `dao_native_util_mac.mm`, `split/dao_split_view.cc` | 🟡 | Cancel a tab drag over or outside the content area, then verify existing and newly opened tabs still receive mouse input in every window |
| ☐ | No crash on pane reparent double-detach (macOS) | `content/.../web_contents_view_mac.mm.patch` (null-guard) | 🟡 | Reparenting panes doesn't crash |
| ☐ | No fatal paint-chunk-duplicate during reparent | `blink/.../paint/paint_controller.cc.patch` (fatal→`LOG(ERROR)`) | 🟡 | Split deactivation doesn't crash debug build |

## 6. Little Dao Window

Lightweight popup window form factor.

| ✔ | Feature | Patch(es) / Source | Risk | Verify after upgrade |
|---|---------|--------------------|------|----------------------|
| ☐ | Little Dao popup layout (48px header, URL pill, "Open in Dao") | `views/frame/layout/browser_view_popup_layout_impl.cc.patch` | 🟡 | Popup shows 48px header, no Chrome toolbar |
| ☐ | External URLs route into Little Dao (startup path) | `ui/startup/startup_browser_creator_impl_little_dao.cc.patch` | 🔴 | With Little Dao enabled + running, OS-opened URL lands in Little Dao |
| ☐ | External URLs route into Little Dao (macOS app-controller path) | `app_controller_mac_little_dao_external.mm.patch` | 🔴 | Same as above via native open-URL event |
| ☐ | Fullscreen disabled in Little Dao windows | `dao_little_dao_controller.cc`, `ui/browser_command_controller.cc.patch` | 🔴 | From a fullscreen Dao window, open an external link; Mini Dao stays windowed and has no native or command fullscreen eligibility |
| ☐ | Little Dao core (controller/view, Browser::Create timing) | `src/dao/.../little_dao/` | — | Popup spawns with correct chrome |
| ☐ | Little Dao window tracking and persisted bounds | `dao_little_dao_controller.*`, `dao_pref_names.*` | — | Move/resize/close/reopen restores bounds; closed Little Dao windows are removed from tracking and pointer reuse does not misclassify normal windows |
| ☐ | Mini Dao extraction from Control Center | `dao_control_center_utility_section.*`, `dao_little_dao_controller.*` | — | Control Center Mini Dao action moves the active live tab into a Little Dao popup, removes the source tab, hides the popup, and rejects extraction from an existing Little Dao |
| ☐ | Mini Dao site-center popup | `dao_little_dao_view.*`, `dao_mini_dao_site_center_popup.*`, `views/frame/browser_view.cc.patch` | 🟡 / — | Site-center button is hit-testable; popup opens from URL pill with Page Info, extensions, share, QR, and more actions; no normal-window Mini Dao extraction action appears |
| ☐ | Mini Dao download card | `dao_mini_dao_download_card_view.*`, `views/frame/browser_view.cc.patch` | 🟡 / — | Only downloads started from that Mini Dao window appear; progress/speed/cancel/overflow update; regular browsers do not create the card |

> Two patches edit `app_controller_mac.mm` (`.mm.patch` and
> `_little_dao_external.mm.patch`) — apply order matters. Two patches edit
> `startup_browser_creator_impl.cc` (`_impl` then `_little_dao`).

## 7. `dao://` Scheme & WebUI Routing

The compatibility layer that lets both `dao://` (canonical) and `chrome://` (legacy) work.

### 7.1 Core scheme registration (verify the chain end-to-end)

| ✔ | Feature | Patch(es) | Risk | Verify after upgrade |
|---|---------|-----------|------|----------------------|
| ☐ | WebUI scheme constant = `dao` | `content/public/common/url_constants.h.patch` | 🟢 | `kChromeUIScheme` == `"dao"`; single source of truth |
| ☐ | Legacy `chrome` registered (standard/secure/CORS/service-worker) | `content/common/url_schemes.cc.patch` | 🟡 | All 4 scheme categories include `chrome` |
| ☐ | Serve legacy `chrome://` data + default CSP | `content/public/browser/url_data_source.cc.patch` | 🟡 | Default CSP permits both schemes |
| ☐ | `chrome` treated as WebUI in backend dispatch | `content/browser/webui/url_data_manager_backend.cc.patch` | 🟡 | `GetWebUISchemesSlow()` returns `chrome`; DCHECK allows it |
| ☐ | Cross-serve chrome↔dao in loader factory | `content/browser/webui/web_ui_url_loader_factory.cc.patch` | 🟡 | Scheme-mismatch guard permits interchange |
| ☐ | Mirror chrome↔dao in CSP override strings | `content/browser/webui/web_ui_data_source_impl.cc.patch` | 🟡 | Helper still called from `OverrideContentSecurityPolicy` |
| ☐ | `chrome`-scheme subresource factory for dao:// pages | `content/browser/renderer_host/render_frame_host_impl.cc.patch` | 🔴 | `chrome://resources/`, `chrome://theme/` subresources load (huge churny `CommitNavigation`) |
| ☐ | Omnibox virtual URL shows `dao://` | `content/browser/renderer_host/navigation_controller_impl.cc.patch` | 🟡 | `chrome://settings` displays as `dao://settings` |
| ☐ | Register both factories (PDF/component-ext subresources) | `chrome/browser/chrome_content_browser_client.cc.patch` (**zeroed diff index — apply by context**) | 🔴 | PDF isn't a blank page |
| ☐ | Register Dao WebUI controllers | `webui/chrome_web_ui_configs.cc.patch`, `chrome_web_ui_controller_factory.cc.patch` | 🟡 | Dao pages load |
| ☐ | WebUI default CSP allows both schemes | `ui/webui/webui_util.cc.patch` | 🔴 | **Diff upstream's new CSP host list — every new `chrome://` host needs a `dao://` mirror or resources silently break** |
| ☐ | `chrome://newtab` → `dao://welcome`; reverse rewriter | `chrome/browser/browser_about_handler.cc.patch` | 🔴 | NTP lands on welcome; omnibox shows `dao://`; reverse rewriter still registered |
| ☐ | URL fixer treats chrome/dao as equivalent | `components/url_formatter/url_fixer.cc.patch` | 🟢 | Omnibox fixup accepts both |
| ☐ | PDF plugin origin allows `dao://print/` | `chrome/renderer/chrome_content_renderer_client.cc.patch` | 🟢 | Print preview works |
| ☐ | Partial hardcoded URL-constant conversions | `chrome/common/webui_url_constants.h.patch`, `url_constants.h.patch` | 🟡 | Re-audit which constants should be `dao://` vs left `chrome://` |

### 7.2 PDF / Print-Preview under `dao://`

| ✔ | Feature | Patch(es) | Risk | Verify |
|---|---------|-----------|------|--------|
| ☐ | Print-preview embedder origin + plugin src + message allowlists | `pdf/pdf_view_web_plugin.cc.patch`, `resources/pdf/pdf_internal_plugin_wrapper.ts.patch`, `pdf_scripting_api.ts.patch`, `print_preview/ui/plugin_proxy.ts.patch` | 🟡 | Print Preview renders PDFs; no cross-origin console rejections. **All 4 must stay consistent** |
| ☐ | Remove `chrome://resources` link from print header/footer | `components/printing/resources/print_header_footer_template_page.html.patch` | 🟢 | Header/footer renders without 404 |

### 7.3 Generated rewrites (NOT patches — `scripts/chromium-rewrites.ts`)

Not in `src/patches/`. Import mechanically rewrites `chrome://`→`dao://` in:
- ~11 `*_strings.grdp` files + `chrome_debug_urls.h` (scheme text)
- ~10 WebUI `*.html` `<base href>` tags
- 2 `_api_features.json` (extension API scheme allowlists)
- all `components/resources/terms/terms_*.html`

| ✔ | Verify after upgrade |
|---|----------------------|
| ☐ | `npm run import` reports **0 missing** rewrite targets. Missing files → update the path lists in `scripts/chromium-rewrites.ts`. New upstream WebUI pages may need adding to `WEBUI_BASE_HREF_PATHS`. New `_api_features.json` `chrome://` hosts may need adding to the allowlist sets. |

## 8. Settings Customizations

### 8.1 "You and Dao" settings page (feature cluster A)

| ✔ | Feature | Patch(es) | Risk | Verify |
|---|---------|-----------|------|--------|
| ☐ | Dao settings page UI (Profile feature controls + global MCP card + enhanced-PiP preview) | `resources/settings/dao_page/dao_page.{html,ts}.patch` (new files), `src/dao/.../mcp/dao_mcp_settings_handler.{h,cc}` | 🟡 | Page renders the Profile-backed feature toggles plus an independent MCP header/connection/enabled-only-setup card. Verify aligned, narrow-width-stacking setup controls; lease-only client/Stop controls; whitespace-preserving preview; native three-space Generic MCP JSON identical to copied content; malformed JSON failure without clipboard change; dynamic copy feedback; no standalone configuration action; and the animated PiP preview |
| ☐ | Dao settings prefs exposed through SettingsPrivate allowlist | `extensions/api/settings_private/prefs_util.cc.patch`, `src/dao/browser/dao_pref_names.*` | 🟡 | Toggles read/write `dao.little_dao_enabled`, `dao.enhanced_pip_enabled`, and `dao.enhanced_command_bar_suggestions_enabled` without console errors |
| ☐ | Page registered in build | `resources/settings/BUILD.gn.patch` | 🟢 | `dao_page.ts` compiled/bundled |
| ☐ | `/dao` route + visibility + menu item + main view slot + export | `route.ts`, `router.ts`, `router_dao.ts`, `page_visibility.ts`, `settings_main/settings_main.{html,ts}`, `settings_menu/settings_menu.html`, `settings.ts` patches | 🟡 | `/dao` resolves; "You and Dao" menu item shows/hides; view renders |
| ☐ | Dao settings localized strings | `webui/settings/settings_localized_strings_provider.cc.patch`, `app/settings_strings.grdp.patch`, `app/resources/generated_resources_zh-CN.xtb.patch` | 🟡 / 🔴 | Dao Profile, MCP status, and quick-setup messages are present in the merged GRDP/provider patches and the hand-authored zh-CN XTB patch; grit build passes |
| ☐ | Settings WebUI test | `test/data/webui/settings/{BUILD.gn,dao_page_test.ts}.patch` | 🟡 | `settings-dao-page`, `routes.DAO`, Profile-backed `dao.*` prefs, unified MCP card sections, global MCP status/action rendering, responsive setup controls, multiline Generic MCP JSON preview and dynamic copy, stale-response handling, standalone-copy removal, and browser-proxy calls are covered |

### 8.2 macOS Sparkle updater + sign-in/sync disable (feature cluster B)

| ✔ | Feature | Patch(es) | Risk | Verify |
|---|---------|-----------|------|--------|
| ☐ | About page: mac "Check for updates" → Sparkle (Omaha/Keystone UI removed) | `resources/settings/about_page/about_page.{ts,html}.patch`, `about_page_browser_proxy.ts.patch` | 🔴 | mac About shows single button, no false update error; **verify every relocated `<if not is_macosx>` still brackets the right upstream blocks** |
| ☐ | About handler routes to `DaoUpdaterService` | `webui/settings/about_handler.{cc,h}.patch` | 🔴 | Button triggers Sparkle; **watch `base::ListValue` → `base::Value::List` migration** |
| ☐ | Copyright rebrand "The Chromium Authors" → "MsgByte" | `webui/settings/settings_localized_strings_provider.cc.patch` | 🟡 | About shows "MsgByte" (brittle string substitution — if upstream reworded, rebrand silently fails) |
| ☐ | Sign-in force-disabled | `webui/settings/settings_ui.cc.patch` (`signinAllowed=false`) | 🟡 | No sign-in UI in settings |
| ☐ | Sync rows hidden; sync routes redirect to BASIC | `people_page/people_page.html.patch`, `router.ts.patch` | 🟡 | `dao://settings/syncSetup` redirects to BASIC (not blank) |
| ☐ | Reset dialog "send to Google" checkbox removed | `reset_page/reset_profile_dialog.{html,ts}.patch` | 🟡 | Reset dialog has no reporting checkbox; always sends `false` |

## 9. Tabs, Menus, Shortcuts, Context Menu

| ✔ | Feature | Patch(es) | Risk | Verify |
|---|---------|-----------|------|--------|
| ☐ | New tabs insert at TOP (vertical tabs) | `ui/tabs/tab_strip_model.cc.patch` | 🟡 | Cmd+T / bookmark-opened tabs appear at top; opener tabs still adjacent |
| ☐ | Back-to-opener enabled by default | `ui/tabs/features.cc.patch` (`kBackToOpener` flip) | 🟢 | Feature exists; Back closes opened tab, returns to opener |
| ☐ | 6 Dao command IDs (34070–34075) | `chrome/app/chrome_command_ids.h.patch` | 🟡 | No numeric collision with new upstream IDs near 34060; all referencing patches resolve |
| ☐ | IDC_OPEN_FILE permanently disabled | `ui/browser_command_controller.cc.patch` | 🔴 | Open File disabled everywhere |
| ☐ | macOS menu items (Check for Updates, New Little Dao, Copy URL, Welcome) | `cocoa/main_menu_builder.mm.patch` | 🔴 | All 4 items at correct positions with correct actions |
| ☐ | macOS accelerators (Cmd+Shift+C=Copy URL, Cmd+D=Duplicate Tab) | `cocoa/accelerators_cocoa.mm.patch`, `global_keyboard_shortcuts_mac.mm.patch` | 🟡 | Cmd+D duplicates; Cmd+Shift+C copies URL |
| ☐ | macOS command validate/execute (no-window + key-window paths) | `app_controller_mac.mm.patch`, `cocoa/browser_window_command_handler.mm.patch` | 🔴 | Each menu item enables + fires with/without key window |
| ☐ | "Decode QR code" image context-menu item | `renderer_context_menu/render_view_context_menu.{cc,h}.patch` | 🔴 | Right-click QR image → decodes, shows dialog; **verify UMA sentinel (163→164) didn't clash with new upstream entries** |
| ☐ | Webpage Copy Image uses Dao toast feedback | `renderer_context_menu/render_view_context_menu.{cc,h}.patch`, `renderer_context_menu/render_view_context_menu_browsertest.cc.patch` | 🟡 | Right-click image → Copy Image copies normally, shows localized Dao toast, and does not show Chromium toast |
| ☐ | QR decoder + result dialog + zxing-cpp | `src/dao/browser/qrcode/`, `src/dao/third_party/zxing-cpp/` | — | QR decode returns result |

## 10. Startup, Session, Prefs

| ✔ | Feature | Patch(es) | Risk | Verify |
|---|---------|-----------|------|--------|
| ☐ | Always restore LAST session (even first-run + post-crash) | `ui/startup/startup_browser_creator.cc.patch`, `startup_browser_creator_impl.cc.patch`, `prefs/session_startup_pref.cc.patch` | 🔴 / 🟡 | Fresh profile restores session; force-crash then relaunch restores with no crash bubble |
| ☐ | Startup tab top-insertion order + no stray NTP | `startup_browser_creator_impl.cc.patch` | 🔴 | Multiple `--` URLs preserve order at top; no stray empty tab |
| ☐ | Suppress crash bubble / API-key infobar / default-browser prompt | `ui/startup/infobar_utils.cc.patch` (`#if 0` block) | 🟡 | None of the 3 prompts appear on startup |
| ☐ | Dao profile prefs registered | `prefs/browser_prefs.cc.patch` | 🟢 | No "unregistered pref" crash on fresh profile |
| ☐ | Sign-in disabled by default | `signin/account_consistency_mode_manager.cc.patch` | 🟢 | Fresh profile sign-in off |
| ☐ | macOS updater + telemetry init on first profile | `chrome_browser_main_mac.mm.patch` | 🟡 | Updater inits; telemetry "browser opened" fires once |
| ☐ | Suppress upstream IPH / tutorial promos | `views/user_education/browser_user_education_service.cc.patch` (`#if 0` blocks) | 🔴 | No upstream promo bubbles; builds clean under `-Werror` |
| ☐ | Memory Saver on by default | `components/performance_manager/user_tuning/prefs.cc.patch` | 🟢 | Fresh profile has Memory Saver enabled |
| ☐ | Google Sync disabled | `components/sync/base/command_line_switches.cc.patch` | 🟡 | No sync UI; `SyncServiceFactory::GetForProfile()` null |
| ☐ | GCM push registration disabled | `google_apis/gcm/engine/registration_request.cc.patch` | 🔴 | GCM fails cleanly, no repeated network calls (whole-function replacement — expect conflicts) |
| ☐ | Chromium download bubble/toolbar suppressed | `download/bubble/download_bubble_prefs.cc.patch` | 🟢 | No Chromium download UI; sidebar download button works |
| ☐ | SODA progress DCHECK crash fix | `accessibility/soda_installer_impl.cc.patch` | 🟡 | Live-caption enable doesn't crash; check if upstream fixed natively |

## 11. Extensions & MV2 Support

**MV2 is the single most important "did we lose a feature" checkpoint** — upstream is
actively removing MV2 infrastructure, so these flags/enums may vanish entirely on rebase.

| ✔ | Feature | Patch(es) | Risk | Verify |
|---|---------|-----------|------|--------|
| ☐ | MV2 deprecation features default-OFF | `extensions/common/extension_features.cc.patch` (**synthetic-looking diff index — verify real offsets**) | 🔴 | Both features still exist + default off; MV2 extension installs + runs |
| ☐ | MV2 availability pref default via Dao helper | `extensions/browser/extension_prefs.cc.patch`, `chrome/browser/extensions/extension_management.cc.patch` | 🟡 | Unmanaged profile treats MV2 as enabled |
| ☐ | `#restore-manifest-v2-deprecation` flag | `about_flags.cc.patch`, `flag_descriptions.h.patch` | 🟡 | Flag appears in `dao://flags`; `dao::kRestoreManifestV2Deprecation` exists |
| ☐ | `simple_feature.cc` include (verify full logic landed, not just include) | `extensions/common/features/simple_feature.cc.patch` | 🟡 | Confirm intended feature-availability change is present |
| ☐ | Legacy `chrome://` extension host permissions | `chrome/common/extensions/chrome_extensions_client.cc.patch`, `extensions/common/url_pattern.cc.patch` | 🟡 | MV2 favicon/permissions parse; `std::size` static_assert passes |
| ☐ | web_request DCHECK removals (Bitwarden/ad-blocker + restore) | `extensions/browser/api/web_request/extension_web_request_event_router.cc.patch` | 🟡 | Both DCHECKs still absent; debug build doesn't crash |
| ☐ | Extension popup styling (rounded, titled, draggable, close btn) | `views/extensions/extension_popup.cc.patch` | 🟡 | Popup shows titled rounded draggable frame |
| ☐ | MV2-aware install dialog (no default button, Dao style, MV2 notice) | `views/extensions/extension_install_dialog_view.cc.patch` | 🔴 | No default button until enabled; MV2 notice shows; OK prominent |
| ☐ | MV2 support lib | `src/dao/browser/extensions/legacy_mv2/` | — | Links into both extension libraries |

## 12. Control Center, Welcome, Webstore Branding

| ✔ | Feature | Patch(es) / Source | Risk | Verify |
|---|---------|--------------------|------|--------|
| ☐ | Control Center shell + utility actions | `src/dao/browser/ui/views/dao_control_center_*`, `dao_native_share_mac.*`, `widget_delegate.h.patch` (QR dialog friend) | 🟢 | Popup opens anchored to sidebar/control button, outside click closes it, Share/QR/Lock/More actions render and execute; More menu clears only the current site's cache/cookies, then closes with toast feedback; while the system appearance is light, Dark stays visually unavailable but clicking it leaves the preference unchanged and shows the localized system-dark requirement through `DaoToastView` |
| ☐ | Control Center extension grid + pinned extension strip | `dao_control_center_extensions_section.*`, `dao_pinned_extensions_container.*`, `dao_extension_action_icon.*`, `ui/extensions/icon_with_badge_image_source.*.patch` | 🟢 | Installed extensions appear with correct enabled/pinned state; pinned icons stay visible in browser chrome and open extension popups; active-tab badge text and colors update in both surfaces with compact glyphs and a background no taller than 10px, without shrinking the 16px base icon or changing the 24px pinned hover target |
| ☐ | Welcome page (`dao://dao-welcome`) | `src/dao/.../welcome/` + first-run pref | — | Auto-opens first launch; reopenable via menu |
| ☐ | Webstore "Add to Chrome" → "Add to Dao" | `src/dao/.../dao_webstore_branding_tab_helper.*` + `tab_helpers.cc.patch` | 🟢 | CWS pages show "Add to Dao" |

## 13. Dialogs (Dao system-dialog style)

| ✔ | Feature | Patch(es) | Risk | Verify |
|---|---------|-----------|------|--------|
| ☐ | Rounded buttons + keycap shortcut badges | `ui/views/window/dialog_client_view.cc.patch` | 🔴 | Dao dialogs show rounded buttons + keycaps; Enter/Esc/shortcuts fire; non-Dao dialogs unchanged |
| ☐ | Opt-in API + surface color + button-shortcut model | `ui/views/window/dialog_delegate.{cc,h}.patch` | 🟡 | `SetUseDaoSystemDialogStyle(true)` yields Dao surface + rounded frame |
| ☐ | Web `alert()` / `confirm()` / `prompt()` Dao styling | `chrome/browser/ui/views/javascript_tab_modal_dialog_view_views.{cc,h}.patch`, `components/constrained_window/constrained_window_views.cc.patch`, `ui/views/window/dialog_delegate.h.patch` | 🟡 | All three dialogs are centered within active web contents; alert has prominent OK + Enter; confirm/prompt add tonal Cancel + Esc; prompt input is rounded and theme-aware; origin/accessibility behavior remains intact |

## 14. Branding & Visuals

| ✔ | Feature | Patch(es) / mechanism | Risk | Verify |
|---|---------|----------------------|------|--------|
| ☐ | Product rebrand Chromium→Dao | `chromium_strings.grd.patch`, `components_chromium_strings.grd.patch` + branding assets (import copies, not patched) | 🟡 | About/version shows "Dao"; correct app icon in Dock |
| ☐ | Scheme rebrand in ~dozens of `*_strings` (translator context) | grouped grd/grdp/xtb patches (§ String localization) | 🟡 | No stray `chrome://` in user-visible strings; build has no missing/dup IDS |
| ☐ | Slim transparent scrollbars | `blink/.../html.css.patch` + `css_default_style_sheets.cc.patch` (DCHECK bound `4u`) | 🟢 / 🟡 | Scrollbars slim/transparent; no startup DCHECK crash (bound must track rule count) |
| ☐ | macOS Keychain "Dao Safe Storage" | `os_crypt/common/keychain_password_mac.mm.patch` | 🟢 | **String must NOT change** or saved passwords won't decrypt |
| ☐ | macOS profile dir `~/Library/Application Support/Dao` | `chrome/common/chrome_paths_mac.mm.patch` + Info.plist `CrProductDirName` | 🟢 | **Must NOT change** or profile is orphaned |
| ☐ | macOS icon + Sparkle Info.plist config | `chrome/app/app-Info.plist.patch` | 🟡 | Custom icon; Sparkle appcast + EdDSA sig validate; **`SUPublicEDKey` must never change without rotating the private key** |

## 15. Build Graph & Toolchain

| ✔ | Feature | Patch(es) | Risk | Verify |
|---|---------|-----------|------|--------|
| ☐ | Dao UI sources + `dao_browser_tests` target | `chrome/browser/ui/BUILD.gn.patch` (+ `dao_ui_sources.gni`) | 🔴 | `.gni` vars consumed; `sources=[`/`deps=[` prepend applied; tests build |
| ☐ | MV2/telemetry/updater deps + `group` conversions | `chrome/browser/BUILD.gn.patch` | 🟡 | `//chrome/browser` links; `kRestoreManifestV2Deprecation` resolves |
| ☐ | Sparkle framework bundling + `dao_display_version` | `chrome/BUILD.gn.patch` | 🟡 | Version stamped; framework embedded + signature-verified |
| ☐ | Dao test targets in browser_tests/unit_tests | `chrome/test/BUILD.gn.patch` | 🟢 | Dao test targets built into aggregate binaries |
| ☐ | Dao WebUI resource paks + locale repack | `chrome/chrome_paks.gni.patch`, `chrome_repack_locales.gni.patch` | 🟡 | agent/sidebar/welcome resources load; strings in every locale pak |
| ☐ | MV2 lib deps for extension libs | `chrome/browser/extensions/BUILD.gn.patch`, `extensions/browser/BUILD.gn.patch` | 🟢 | `DaoMV2PrefDefaults` links |
| ☐ | Lit TS bundles | `third_party/lit/v3_0/BUILD.gn.patch` | 🟢 | Dao TS typechecks/bundles |
| ☐ | `disable_css_lint` + eslint escape hatches | `ui/webui/resources/tools/build_webui.gni.patch` | 🟡 | Dao WebUI builds with `disable_css_lint=true` |
| ☐ | Resource-ID range reservation (8400/8605/8625/8645) | `tools/gritsettings/resource_ids.spec.patch` | 🔴 | **No grit ID-overlap — upstream adds entries so the range may need relocating** |
| ☐ | Patch inventory count and docs drift check | `src/patches/`, `docs/features.md`, `docs/chromium-upgrade-guide.md` | — | Recount `src/patches/**/*.patch`; update inventory docs if the total or category split changes |
| ☐ | Flag-unexpiry guard for small milestone | `tools/flags/generate_unexpire_flags.py.patch` | 🟡 | `unexpire_flags` produces valid C++ |
| ☐ | Unsafe-buffers exemption for `dao/third_party/` | `build/config/unsafe_buffers_paths.txt.patch` | 🟢 | Vendored code builds |
| ☐ | Empty-archive libtool workaround (cctools ≥1024) | `build/toolchain/apple/filter_libtool.py.patch` | 🟡 | Zero-source targets link; check if upstream fixed natively |

## 16. macOS SDK-Compatibility Patches (candidates to DROP on SDK bump)

These exist only because the build SDK lags. **When you upgrade the Xcode / macOS build
SDK, revisit each — upstream's version likely becomes correct and the patch should be
deleted rather than re-applied.**

| ✔ | Patch | Why it exists | On SDK bump |
|---|-------|---------------|-------------|
| ☐ | `ui/display/mac/screen_utils_mac.mm.patch` | `NSScreen.CGDirectDisplayID` absent in Xcode 16.3 SDK | Drop when on macOS 26 SDK |
| ☐ | `skia/ext/skia_utils_mac.mm.patch` + 3 more `kCGImageByteOrder32Host`→`kCGBitmapByteOrder32Host` | Constant naming in current SDK | Re-check; may be unnecessary |

## 17. Android Foundation

| ✔ | Feature | Command | Verify |
|---|---------|---------|--------|
| ☐ | Android unit tests | `cd android && ./gradlew :app:testDebugUnitTest` | Navigation, BrowserStore tab actions and restore policy, thumbnail privacy/storage, SQLite library, DownloadManager adapter, preferences, extensions, QR decoding, and engine-view lifecycle pass |
| ☐ | Android English-first i18n | `python3 -m unittest discover -s scripts/tests -p 'test_i18n*.py' -v` then `sh ./i18n.sh --only android --langs zh-CN,ja --dry-run --jobs 1` | English system locale resolves the unqualified English catalog; Simplified Chinese resolves the complete `values-zh-rCN` catalog; an unsupported locale falls back to English; history date/time formatting follows the active locale; dry-run performs no API request or write; generated XML preserves exact keys, Android placeholders, and inline markup |
| ☐ | Android new-tab UI | `cd android && ./gradlew :app:connectedDebugAndroidTest` | New-tab chrome respects safe insets; the localized top-right Settings action opens Settings, returns to the same home screen, and is absent while search editing is expanded; the idle search pill has no editable semantics; activation creates and focuses the real input; CameraX scanner opens/closes; search suggestions expose persisted visits and bookmarks |
| ☐ | Android tab grid | `cd android && ./gradlew :app:connectedDebugAndroidTest` | Both compact framed count buttons match `BrowserStore`; the new-tab count frame is absent while search editing is expanded; the full-screen two-column grid creates, selects, closes, and swipe-right-closes real Gecko tabs; closing the final tab creates a same-privacy replacement |
| ☐ | Android screen transitions | Open and return from Settings, About, Open Source Licenses, History, Bookmarks, Downloads, Extensions, and the AMO Store; open and close the tab grid; switch among New Tab, Browsing, and Address Edit | Forward hierarchy changes slide a short distance from the right and fade; Back mirrors the motion; the tab grid fades and scales subtly; primary browser changes remain immediate; the selected tab, return destination, and Gecko page remain unchanged |
| ☐ | Android tab thumbnails | Navigate multiple tabs, switch through the grid, and relaunch | Each regular card shows its last captured real page, thumbnails stay bounded and survive process death, private captures never reach disk, and closing a tab deletes its cached thumbnail |
| ☐ | Android regular-session restore | Create ordered tabs with navigation history, scroll the selected page well below the top, leave the app, force-stop, and relaunch | Regular tab order, selected tab, URLs, back/forward history, and the selected page's previous scroll position return; private tabs do not return; default-private startup clears the regular snapshot; a missing or unreadable snapshot falls back to one blank tab |
| ☐ | Android desktop-brand icon | Install the debug APK and inspect the launcher, OEM installer details, and new-tab page | All surfaces show the canonical borderless ink-circle Dao artwork; the adaptive foreground stays within the 66dp OEM-safe box while preserving its standard launcher scale, and legacy mipmap fallbacks retain independent padding under circular and rounded-square masks; no placeholder letter mark or macOS-specific frame remains |
| ☐ | Android browser drawer | Manual API 34 emulator smoke after `:app:installDebug` | Browsing shows only the top address bar; the right drawer closes on scrim/back and forwards back, forward, and reload to GeckoView; Home reuses the selected tab and Back restores its previous page; Scan opens the shared scanner; adding or removing the current-page bookmark immediately switches its star between outline and fill; Dark mode switches the persisted theme directly in both directions; unavailable back/forward actions are disabled and visibly faint; pressed ripples remain clipped inside each rounded navigation button |
| ☐ | Android pull-to-refresh | `cd android && ./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.msgbyte.dao.ui.BrowserSurfaceTest` | A downward pull reloads once only when GeckoView cannot scroll upward; mid-page pulls remain web-content gestures; the overlay indicator resets after loading or a tab switch without recreating the engine view |
| ☐ | Android embedded load progress | `cd android && ./gradlew :app:connectedDebugAndroidTest` plus a slow navigation | Gecko progress fills the clipped address-bar background from left to right; address and page-content bounds remain unchanged between idle, loading, and completion |
| ☐ | Android site security details | `cd android && ./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.msgbyte.dao.ui.BrowserChromeTest` plus one HTTPS and one HTTP page | The lock/globe has an independent address-bar action; the bottom sheet follows the selected tab's live Gecko security state; secure pages show only available certificate subject, issuer, validity, serial, and SHA-256 fingerprint fields; insecure/unknown pages never retain stale certificate data |
| ☐ | Android preference persistence | Toggle the drawer Dark mode action twice; change page font scale, search engine, tracking protection, and default-private mode; force-stop and relaunch | Drawer theme changes apply immediately from light to dark and dark to light; Google, Baidu, Bing, and DuckDuckGo are selectable and produce their expected encoded search URLs; settings remain selected; runtime-safe Gecko settings apply immediately; private mode applies to the next app session; the settings screen contains no unimplemented download-location or startup controls |
| ☐ | Android About and bundled notices | `cd android && ANDROID_HOME="/Users/moonrailgun/Library/Android/sdk" ./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.msgbyte.dao.MainActivityTest#settingsAboutFlowShowsVersionsLicensesAndBackNavigation`, then manually check English and Simplified Chinese | Both localized Settings catalogs expose About; Settings → About → Open Source Licenses and Back follow the expected route without changing the original utility return destination; app version is `0.1.0`, the browser engine is shown as `Gecko 153.0.2` with its version sourced from the Gradle catalog, and the uBlock Origin and KISS Translator notices load from local APK assets with no network requirement |
| ☐ | Android USB remote debugging | Open Settings > Developer tools, exercise the first-use warning and Connection guide, then connect an ADB-authorized device to desktop Firefox `about:debugging` | Remote debugging defaults off; Cancel keeps it off; confirmation persists acknowledgement and enables Gecko before the first session; later toggles apply without restart; guide opens Android developer settings and copies the desktop address; disabling removes Dao tabs from Firefox discovery; Dao exposes no network listener and does not set `android:debuggable` |
| ☐ | Android library screens | Add and remove the current page from the browser drawer, then open History and Bookmarks | The current-page star changes between outline and fill while the drawer remains open; completed Gecko visits persist; bookmark/folder/read-list mutations survive relaunch and update new-tab suggestions |
| ☐ | Android downloads | Download an HTTP(S) file, cancel or retry an active task, and open/delete a completed task | Android DownloadManager owns transfer state; UI reports real progress/status and contains no pause/resume simulation |
| ☐ | Android extensions | On an API 34+ emulator, open Extensions > Store; load recommendations; search a public add-on not bundled in the APK; install it, approve permissions, relaunch, open its browser action when available, disable and uninstall it; retry the Store offline; cancel a permission prompt; then select a local unsigned XPI | Store is native Compose UI and leaves the selected `BrowserStore` tab unchanged; blank search returns AMO recommendations and submitted search returns Android/Gecko-compatible public HTTPS XPI results; Store uses Gecko `RTAMO`, while the system picker remains a storage-permission-free `FROM_FILE` local-XPI path; requested browser permissions, site access, and data collection require explicit approval; successful Mozilla-signed installs persist across relaunch and can be disabled/uninstalled; catalog failures show Retry; remote install failures use the localized extension-install status, and dismissing the result with Done makes Install available again; cancellation reports no install; Gecko rejects unsigned, incompatible, corrupt, and blocklisted packages with localized failures; bundled rows cannot be uninstalled |
| ☐ | Android QR scanner | Open Scan from both the new-tab pill and browser drawer, grant camera permission, and scan a QR code containing an HTTP(S) URL | Camera preview is visible, decoded content navigates the selected tab once through the normal resolver, and closing releases the camera |
| ☐ | Android lint | `cd android && ./gradlew :app:lintDebug` | No Android lint errors |
| ☐ | Android debug APK | `cd android && ./gradlew :app:assembleDebug` | Debug APK builds on macOS with JDK 17 and SDK 36 |
| ☐ | Android manifest surface | `aapt2 dump permissions android/app/build/outputs/apk/debug/app-debug.apk` | Internet and camera are declared; package discovery and notification permissions remain removed; no WorkManager components are merged |
| ☐ | Bundled uBlock Origin provenance | `cd android && ./gradlew :app:verifyBundledUBlockOrigin` | Version 1.72.2, MV2, ID `uBlock0@raymondhill.net`, required files, release URL, SHA-256, GPL-3.0 text, and source link all match; the manifest omits GeckoView-unsupported `menus` and `commands` declarations |
| ☐ | Built-in ad blocker runtime | `cd android && ./gradlew :app:testDebugUnitTest --tests 'com.msgbyte.dao.browser.BuiltInAdBlockerTest' --tests 'com.msgbyte.dao.browser.BrowserRuntimeTest'` | Installation is requested once before session creation; callback failure leaves the engine usable |
| ☐ | Built-in ad blocker APK assets | `unzip -l android/app/build/outputs/apk/debug/app-debug.apk` | Manifest, background code, filter assets, all `_locales` directories, extension license, and provenance notice are packaged; AAPT does not apply its default `<dir>_*` exclusion |
| ☐ | Built-in ad blocker device smoke | `cd android && ./gradlew :app:connectedDebugAndroidTest` plus a controlled blocked request | GeckoView lists uBlock Origin as built-in and enabled; the controlled request is blocked on API 26+ and ordinary navigation remains usable |
| ☐ | Bundled KISS Translator provenance | `cd android && ./gradlew :app:verifyBundledKissTranslator` | Version 2.0.29, MV2, stable Gecko ID, source/listing URL, artifact SHA-256, GPL-3.0-only text, popup/options UI, locales, and Mozilla signature metadata all match |
| ☐ | Built-in KISS Translator runtime | `cd android && ./gradlew :app:testDebugUnitTest --tests 'com.msgbyte.dao.browser.BuiltInTranslatorTest' --tests 'com.msgbyte.dao.browser.BrowserRuntimeTest'` | A Gecko profile without KISS installs it disabled with `EnableSource.USER`; an existing KISS installation keeps its enabled state; upgraded profiles remove only the legacy TWP ID after KISS succeeds; inventory, installation, disablement, or cleanup failures do not prevent engine access |
| ☐ | Built-in KISS Translator device smoke | On a clean API 34 emulator profile, open Extensions before and after enabling KISS Translator, then restart Dao | GeckoView initially lists KISS Translator as built-in and disabled without TWP; the user can enable it and that state survives restart; Open displays its full-screen popup without replacing the selected web tab; paragraph-by-paragraph bilingual translation works on the originating page; selected text and subtitle controls remain usable with the selected third-party service |
| ☐ | Android device smoke | `cd android && ./gradlew :app:connectedDebugAndroidTest` | Browser chrome launches and Gecko-backed tabs render on API 26+; address controls stay clear of cutouts and both gesture and three-button navigation bars |

---

## Post-rebase sign-off

The build passing is **necessary but not sufficient**. Before declaring the rebase done:

- [ ] Every 🔴 patch's hunk was re-read against new upstream (not just "it applied").
- [ ] Every silent-loss pattern was diffed (feature flag flips, `return false`/`LAST`
      hardcodes, `#if 0` blocks, DCHECK removals, string substitutions).
- [ ] `npm run import` reports **0 failed patches** and **0 missing rewrite targets**.
- [ ] `npm run test` (Dao browser_tests) is green (note the known-`DISABLED_` set).
- [ ] Manual smoke test of §1–§6 and §12 (sidebar, command bar, agent/workspace,
      Dream, PiP, Split, Little Dao/Mini Dao, Control Center) in a running build.
- [ ] Every source-only `—` row was verified, not skipped just because no patch conflicted.
- [ ] `grep -r "chrome://"` finds no stray user-visible occurrences that should be `dao://`.
- [ ] Grit/resource-ID ranges don't collide (§15).
- [ ] MV2 extension installs and runs (§11).
