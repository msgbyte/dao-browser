# Native iOS browser implementation

Approved direction: a separate iPhone-first SwiftUI app in `ios/`, backed by
stable WKWebView sessions on iOS 18.4+. Android Nova is the visual baseline.
No Chromium or Android behavior changes. No Git mutations are authorized.

1. Add the iOS project and isolated `npm run rebuild` command through the existing
   CLI. Add portable state tests first: URL safety, search escaping, private
   snapshot filtering, last-tab behavior and download filenames.
2. Implement observable tab state, lazy WKWebView sessions, interaction-state
   restoration and SwiftData history/bookmarks/reading list. Use nonpersistent
   website data for private sessions and never archive private state.
3. Implement Nova home/address editing, toolbar, right drawer, tab grid,
   libraries, downloads, QR, settings, page security and native web dialogs.
   Localize strings through English and Simplified Chinese resource tables.
4. Validate the actual bundled extension manifests against the WebKit APIs;
   record compatibility gaps honestly. Do not label Firefox packages compatible
   without runtime evidence or silently replace full uBlock with weaker rules.
5. Run the isolated rebuild, inspect failures and fix them, check source and
   localization consistency, and document device QA that cannot run locally.

First acceptance: complete basic browser flow, Android-derived visual tokens,
regular session restoration, private storage isolation, and an explicit extension
route. Full AMO/XPI parity is conditional on extension runtime validation.

Verification: `cd ios` then `npm run rebuild`; `-- --core-only` for the portable
checks. Simulator/device QA covers browsing, popup tabs, history and cookies,
restart/back-forward state, private mode, downloads, dialogs, QR and screenshots
in light/dark mode. The installed Xcode SDK is 18.4; no simulator runtime is
currently installed, so compilation cannot establish visual/runtime acceptance.
