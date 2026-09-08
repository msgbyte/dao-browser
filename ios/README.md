# Dao Browser for iOS

An independent iPhone app, built with SwiftUI and WebKit. Requires iOS 18.4+.
The Android Nova UI is the visual reference. No Chromium checkout is needed.

## Build

Install the root npm dependencies, Xcode 16.3+ and XcodeGen, then run:

```sh
cd ios
npm run rebuild
```

This command runs the Foundation-only browser tests, regenerates
`DaoBrowser.xcodeproj` from `project.yml`, and builds an unsigned iPhone app
under `ios/build`. The repository's root `npm run rebuild` still targets Chromium.
Use the command from **this directory** for iOS compile confirmation.

To build a complete Simulator app instead:

```sh
npm run rebuild -- --simulator
```

The app bundle is `build/Products/Debug-iphonesimulator/DaoBrowser.app`.

To run only the portable state checks, including on a Mac without a Simulator:

```sh
npm run rebuild -- --core-only
```

Open `DaoBrowser.xcodeproj` in Xcode and select the `DaoBrowser` scheme and an
installed iPhone Simulator. For a physical device, configure your development
team and signing identity in Xcode. An installed SDK alone is not a Simulator
runtime. Camera scanning also requires a supported physical iPhone.

For a source compilation check on a machine with an SDK but no runtime:

```sh
npm run rebuild -- --sources-only
```

This excludes the image catalog and writes to `build/SourceCheck`. Its output
is **not a complete app** and cannot establish icon or UI acceptance.

Verification on September 8, 2026: iOS 18.4 (22E238) is installed with its Apple
signature verified. `npm run rebuild` passes all four portable tests and the
complete unsigned iPhone app build, including the image catalog and app icons.
`npm run rebuild -- --simulator` also passes all four tests and builds a complete
Simulator app. A smoke run on iPhone 16 / iOS 18.4 covers the home page, address
entry, loading `https://example.com`, creating/closing/switching tabs, the drawer,
settings and restoring the page after relaunch. Settings labels are visually
checked. Full regression and physical-device acceptance remain outstanding.

## Structure

- `DaoBrowser/BrowserModel.swift`: observed tab state and session ownership.
- `DaoBrowser/BrowserSession.swift`: one persistent WKWebView per live tab,
  navigation delegates, popups, permissions and web downloads.
- `DaoBrowser/*View.swift`: custom Nova UI with native system sheets.
- `DaoBrowser/Storage.swift`: SwiftData library, preferences and protected archives.
- `CoreTests/Sources`: Foundation-only logic compiled into both the app and tests.
- `DaoBrowser/Resources`: English and Simplified Chinese strings, license text,
  Lucide assets and existing Dao branding.

Normal tabs restore lazily. Private tabs use a nonpersistent WebKit data store;
their URLs, interaction states, history and screenshots are not archived. Private
tab thumbnails exist only in memory. Explicitly saved bookmarks and confirmed
downloads remain on disk. Download resume data is memory-only; an interrupted
download without resume data must be restarted from its source page, avoiding
incorrect replay of authenticated requests or POST/blob downloads.

Deep links use `dao://open?url=<percent-encoded-http-or-https-url>`. Default-browser
entitlements, signing, App Store distribution and universal links are not set up.

## Extension boundary

The iOS app has no Extensions page or entry in the browser drawer or Settings.
There is no extension installer, ad blocker or translator in this build.
Android's bundled manifests require:

| Package | Requirements to validate on WebKit |
| --- | --- |
| uBlock Origin 1.72.2, MV2 | persistent background page, `webRequestBlocking`, `dns`, `privacy`, tab/navigation events, filter storage |
| KISS Translator 2.0.29, MV2 | background scripts, `scripting`, `declarativeNetRequest`, context menus, all-site content scripts |

The iOS 18.4+ [WKWebExtensionController API](https://developer.apple.com/documentation/webkit/wkwebextensioncontroller)
is an integration path, not proof of Firefox XPI compatibility. Before enabling
installation, test these actual packages, permissions, tab/window events,
background execution and popup context on iOS. If uBlock cannot be ported, a
separately disclosed [WKContentRuleList](https://developer.apple.com/documentation/webkit/wkcontentruleliststore)
implementation is the smaller first ad-filtering option; it is not full uBlock.
Translation should use an adapted WebKit extension after its required APIs pass.
Neither package has been run on iOS in this change.

See [the feature inventory](../docs/features-ios.md) and
[the regression checklist](../docs/feature-checklist.md#ios-native-browser).
