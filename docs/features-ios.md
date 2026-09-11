# Dao Browser — iOS Feature Inventory

This describes the initial native iPhone implementation under `ios/`, targeting
iOS 18.4+. It is not a claim of Android feature parity or completed device QA.

| Surface | Implemented behavior | Source |
| --- | --- | --- |
| New tab | Dao branding, localized greeting/date, search pill 32 points below the banner, one persistent input that moves upward before receiving focus, saved-page suggestions, scanner entry | `NewTabView.swift` |
| Browser | WKWebView extending behind the home indicator without an app-colored bottom strip, keyboard-safe layout, URL/loading chrome, history gestures, pull to refresh, native page find, sharing, page QR, certificate chain/fingerprints, failure/reload view | `BrowserView.swift`, `BrowserSheets.swift`, `BrowserSession.swift` |
| Right drawer | Back/forward/reload; a three-column, two-row grid for home, bookmark, read-later, share, page QR and find with consistent tile styling; library and settings remain list rows | `BrowserView.swift` |
| Tabs | Two-column snapshot grid, select, add regular/private tab, close button and swipe right, last-tab replacement | `NewTabView.swift`, `BrowserModel.swift` |
| Sessions | Lazy WebKit sessions, normal tab metadata and interaction-state archive, memory-pressure eviction of inactive normal tabs | `BrowserModel.swift`, `BrowserSession.swift` |
| Privacy | Shared nonpersistent store for private tabs, no private session/history/thumbnail persistence, background privacy cover, inspector disabled for private pages | `BrowserModel.swift`, `BrowserView.swift` |
| Library | Persistent history/bookmarks/reading list, search, clear history, edit saved page, folder names/filtering, move or remove folder assignment, delete entries | `LibraryView.swift`, `Storage.swift` |
| Downloads | Explicit confirmation before file writes, progress, cancel, memory-only resume, persistent normal records, Quick Look, share and confirmed deletion | `DownloadStore.swift`, `DownloadsView.swift` |
| Preferences | System/light/dark theme, Google/Baidu/Bing/DuckDuckGo, page scale, private startup, opt-in Safari Web Inspector, website-data clearing | `SettingsView.swift`, `Storage.swift` |
| System integration | Camera QR scanner, native JS dialogs and media permission prompts, confirmed external-app links, `dao://open` cold/warm links | `BrowserSheets.swift`, `BrowserSession.swift`, `DaoBrowserApp.swift` |
| Resources | English and Simplified Chinese, dynamic colors, reduced motion, VoiceOver labels, local license text and upstream Lucide icons | `Resources/`, `NovaTheme.swift` |

Known differences: iPhone-only, no Agent/MCP/sync, password manager, default-browser
entitlement, AMO/XPI installation, uBlock or KISS Translator. There is no Extensions
page or entry in the browser drawer or Settings. Folder membership is stored on
saved pages; empty or nested folders are not modeled. Downloads are
WebKit-managed without an independent background transfer service; resume data
does not survive process termination. Normal thumbnails are captured in memory,
not restored from disk. Text and permission/keyboard/share sheets use native iOS
rendering. Tracking-protection controls are not ported from GeckoView.

The visual tokens, rounded shapes, custom rows, two-column tabs, right drawer and
search transition follow Android Nova source values. Screenshot parity, actual
WebKit back-forward/scroll restoration, website compatibility and physical camera
scanning require Simulator/device acceptance. Restoring
an archive can reload network content; it is not an offline page snapshot.
