# Dao Browser — iOS Feature Inventory

This describes the initial native iPhone implementation under `ios/`, targeting
iOS 18.4+. It is not a claim of Android feature parity or completed device QA.

| Surface | Implemented behavior | Source |
| --- | --- | --- |
| New tab | Dao branding, localized greeting/date, search pill 32 points below the banner, one persistent input that moves upward before receiving focus, saved-page suggestions, scanner entry; editing a page address opens with the whole URL selected so typing replaces it; tapping the search icon in the expanded pill brings back a dismissed keyboard; tapping blank space cancels editing, discards unsubmitted input and hides the keyboard and suggestions, restoring the new tab or the previously browsed page | `NewTabView.swift` |
| Browser | WKWebView extending behind the home indicator without an app-colored bottom strip, keyboard-safe layout, URL/loading chrome with an address-edit hit area covering text, surrounding space and the private indicator; independent site-information button; history gestures, pull to refresh, native page find, sharing, page QR, certificate chain/fingerprints, failure view whose retry reloads the URL that failed; drawer bookmark, read-later and page QR disabled until a popup has a web URL; when iOS ends a page's content process the selected tab reloads immediately and background tabs reload when selected, losing scroll position, while a second termination within 30 seconds shows the failure view instead of looping; recovering or restoring a page reached by submitting a form cancels WebKit's POST reload and shows the failure view instead of sending the form again; closing a tab declines its queued dialogs, download and external-app prompts, while a page closing its own window keeps its download prompts; short action feedback appears as a non-blocking toast | `BrowserView.swift`, `BrowserSheets.swift`, `BrowserSession.swift` |
| Right drawer | Back/forward/reload; a three-column, two-row grid for home, bookmark, read-later, share, page QR and find with consistent tile styling; library and settings remain list rows; slides in/out above the page unless Reduce Motion is enabled; dismiss by tapping outside or using VoiceOver escape, with no dedicated close button | `BrowserView.swift` |
| Page images | Long-pressing an image, including one inside a link, opens a menu with Download image, Copy image, Preview image and, for HTTP(S) images, Open image in new tab. An isolated-world script reports the touched image and, while the finger is down, turns off WebKit's own callout, selection and drag on that image so they cannot race the menu; pages that set `-webkit-touch-callout: none` keep their own long press. WebKit's context-menu delegate supplies the menu for linked images and a Dao context-menu interaction, lifting only the image, supplies it elsewhere; the preview is an in-place overlay so it can open while either menu dismisses. Images are fetched through the tab's own WebKit download path, so cookies and private browsing match the page; HTML responses, non-2xx statuses and files over 32 MB fail with action-specific toasts. Download saves into Downloads without a second confirmation, and a transfer that fails before it starts also shows the failure toast; Copy writes the original encoding plus a PNG for other formats; Preview shows a black full-screen viewer with pinch zoom up to 5× and pan, downsampled to 2048 px; Open in new tab adds a lazily loaded background tab with the same privacy; a private one keeps the private cookies even if its opener closes first. Decoding runs off the main thread, and the lifted image follows the page-scale preference. Text links keep WebKit's default menu and preview. SVG cannot be copied or previewed; page-scoped `blob:` images and inline images over 1 MB of source text may fall back to WebKit behavior | `BrowserSession.swift`, `PageImage.swift`, `BrowserModel.swift` |
| Tabs | Two-column snapshot grid with normal-tab previews restored after relaunch, select, add regular/private tab, close button and swipe right, last-tab replacement; the grid opens on the selected tab and scrolls only as needed to keep it visible when the selection or tab count changes | `NewTabView.swift`, `BrowserModel.swift` |
| Sessions | Lazy WebKit sessions, normal tab metadata, interaction state and compressed thumbnails in the protected session archive; capture after page load, before switching surfaces and when becoming inactive; memory-pressure eviction preserves archived previews | `BrowserModel.swift`, `BrowserSession.swift` |
| Privacy | Shared nonpersistent store for private tabs, no private session/history/thumbnail persistence, background privacy cover, inspector disabled for private pages | `BrowserModel.swift`, `BrowserView.swift` |
| Library | Persistent history/bookmarks/reading list, search, clear history, edit saved page, folder names/filtering, move or remove folder assignment, delete entries | `LibraryView.swift`, `Storage.swift` |
| Downloads | Explicit confirmation before file writes: the file name is the alert title, the question, known size and on-device notice are secondary, and Download is the highlighted action; nothing appears in Downloads until it is approved, and declining, dismissing or closing the source tab leaves no record or file, including when the response arrives after the tab closed; a page closing its own window keeps the prompt, and a transfer that fails before it can ask shows a toast. Running transfers show transferred/total size and a speed sampled over one-second windows that drops to zero after five seconds without progress and hides after resets or longer gaps; progress, cancel, memory-only resume, reopening the originating page when a transfer cannot resume, persistent normal records, Quick Look, share, and confirmed deletion with a toast stating whether the file was deleted or already missing | `DownloadStore.swift`, `DownloadsView.swift` |
| Preferences | System/light/dark theme, Google/Baidu/Bing/DuckDuckGo, page scale, private startup, opt-in Safari Web Inspector, website-data clearing | `SettingsView.swift`, `Storage.swift` |
| System integration | Camera QR scanner with a centered 300-point framing guide (recognition is not limited to it), native JS dialogs and media permission prompts, confirmed external-app links, `dao://open` cold/warm links | `BrowserSheets.swift`, `BrowserSession.swift`, `DaoBrowserApp.swift` |
| Resources | English source strings with hand-authored Simplified Chinese; other system locales generated into `<lang>.lproj` by `sh ./i18n.sh --only ios`, falling back to English; dynamic colors, reduced motion, VoiceOver labels, local license text and upstream Lucide icons | `Resources/`, `NovaTheme.swift`, `scripts/i18n_ios.py` |

Known differences: iPhone-only, no Agent/MCP/sync, password manager, default-browser
entitlement, AMO/XPI installation, uBlock or KISS Translator. There is no Extensions
page or entry in the browser drawer or Settings. Folder membership is stored on
saved pages; empty or nested folders are not modeled. Downloads are
WebKit-managed without an independent background transfer service; resume data
does not survive process termination. Normal thumbnails restore from the session
archive without loading inactive pages; closing a tab, returning it home or clearing
website data removes its cached preview. Private thumbnails stay in memory. Older
archives without thumbnails remain readable and gain previews as pages are visited.
Text and permission/keyboard/share sheets use native iOS
rendering. Tracking-protection controls are not ported from GeckoView.

The visual tokens, rounded shapes, custom rows, two-column tabs, right drawer and
search transition follow Android Nova source values. Screenshot parity, actual
WebKit back-forward/scroll restoration, website compatibility and physical camera
scanning require Simulator/device acceptance. Restoring
an archive can reload network content; it is not an offline page snapshot.

Distribution uses the manual **Publish iOS to TestFlight** GitHub workflow:
portable tests → signed Release archive → App Store IPA → App Store Connect
upload. The bundle ID is `com.msgbyte.dao`, matching Android's application ID.
Credentials and the Apple app record must be configured first; upload
success does not prove TestFlight acceptance or App Store approval. The same
tested build can be selected for App Store review. See the
[setup and release guide](../ios/README.md#testflight-and-app-store-distribution).
The app includes a privacy manifest for its app-local UserDefaults access.
Builds declare `ITSAppUsesNonExemptEncryption=false` for the current system
WebKit networking and CryptoKit certificate fingerprints to avoid repeated
App Store Connect encryption questions. Reassess this declaration if adding
custom or third-party cryptography.
