# iOS Store Preparation

Working release packet for the first TestFlight and App Store release. These are
drafts and acceptance steps, not a record of a live upload or approval.
For signing and CI setup, use the [distribution guide](README.md#testflight-and-app-store-distribution).

## App record

The owner confirmed successful App Store Connect app creation on September 18,
2026. Signing credentials, build upload, and device acceptance remain pending.

| Field | Value or decision |
| --- | --- |
| Platform | iOS; current app supports iPhone on iOS 18.4+ |
| Bundle ID | `com.msgbyte.dao`, shared with Android; app creation confirmed by the owner |
| Store name | Proposed: `Dao Browser`, subject to owner confirmation and availability |
| Home-screen name | `Dao` |
| Primary language | Proposed: English (U.S.); add Simplified Chinese localization |
| SKU | `dao-browser-ios` |
| Initial version | `0.1.0`; select the exact build accepted in TestFlight |
| Primary category | Proposed: Utilities |
| Price and availability | Owner must select the price and launch territories |
| Release option | Recommended: Manually release this version |
| Copyright | Confirm the rights-holder name before entering `2026 <rights holder>` |
| Support and privacy URLs | Pending public pages; do not submit placeholder URLs |
| Review contact | Owner-provided name, email, and phone; distinct from the public support contact |

Register an explicit App ID in the chosen Apple Developer team before creating
the App Store Connect record. The current app does not require optional push,
iCloud, or Sign in with Apple capabilities. Ordinary WebKit browsing does not
require the default-browser entitlement; enabling Dao as the system default is
tracked below. The provider label
in App Store Connect is not the 10-character developer Team ID. After an upload,
changing the bundle ID requires a different app record; keep this identifier stable.
See Apple's [app-record instructions](https://developer.apple.com/help/app-store-connect/create-an-app-record/add-a-new-app/).

## iOS TODO: Default browser

Status: planned; Apple approval, project configuration, and device verification
are pending. This does not block the first TestFlight upload.

- [ ] Request Apple's managed `com.apple.developer.web-browser` entitlement for
  `com.msgbyte.dao` in the selected developer team. Follow the
  [default-browser requirements and application process](https://developer.apple.com/documentation/xcode/preparing-your-app-to-be-the-default-browser).
- [ ] After approval, enable the capability for the App ID, add the entitlement
  to the app target, and register HTTP/HTTPS URL schemes. Handle incoming web
  links on both cold launch and while the app is running.
- [ ] Regenerate the App Store Connect provisioning profile with the approved
  entitlement and replace `IOS_PROVISION_PROFILE_BASE64` in GitHub Actions.
- [ ] Install the signed build on a real iPhone, select Dao as the default
  browser in Settings, and verify HTTP/HTTPS links from another app in both
  launch states. Update the feature inventory and regression checklist when
  this behavior is implemented and verified.

## English store copy

The text below describes the implemented iPhone app, independently of desktop
and Android features. Use after device acceptance confirms these behaviors.

**Subtitle:** Tabs, bookmarks & private mode

**Promotional text:** Browse with a simple tab grid, keep useful pages in your library, and choose private tabs when you want a separate browsing session.

**Keywords:** web,search,tabs,bookmarks,reading,list,downloads,qr,webkit

**Description:**

Dao Browser is an open-source browser for iPhone, built with Apple's WebKit.

- Move between pages with a visual tab grid and browsing gestures.
- Save bookmarks and reading-list links, organize them with folders, and search your library.
- Choose Google, Baidu, Bing, or DuckDuckGo as your search engine.
- Open private tabs without saving their browsing history or restoring them after relaunch.
- Confirm downloads, preview files, and share them through iOS.
- Scan QR codes, share a page as a QR code, and find text on the current page.
- Choose a light, dark, or system appearance and adjust page scale.

Private browsing does not make you anonymous to websites or network providers.
Bookmarks you explicitly save and files you download remain on the device.
Reading-list entries save links, not offline copies of websites.

Available in English and Simplified Chinese. Requires iOS 18.4 or later.

Do not advertise AI Agent, MCP, cross-device sync, extension support, ad blocking,
translation, or setting Dao as the default browser: these are absent from this build.

## TestFlight and review notes

**Beta description:** A native iPhone browser with regular and private tabs, bookmarks, a reading list, downloads, and QR tools. No Dao account is required.

**What to test:** Open and switch tabs, restart the app, save and reopen bookmarks, test private browsing, download and share a file, and try camera and microphone permission denial. Report the app version/build, iPhone model, iOS version, and reproduction steps. Remove private URLs and personal data from attachments.

**Review notes draft:**

Dao Browser is a general-purpose iPhone web browser using WKWebView. No Dao login
or demo account is required. Enter a URL or search on the home page. The browser
drawer opens the library, downloads, and settings; the tab grid supports regular
and private tabs. The camera is used for QR scanning and websites with permission;
the microphone is available to websites with permission. Camera scanning requires
a supported physical iPhone. Private browsing excludes automatic history and
session persistence, while explicitly saved bookmarks and downloads remain.

Before submission, add the final privacy-policy location to these notes and
confirm that the review contact and TestFlight feedback email are monitored.

## Privacy preparation

Source audit on September 18, 2026:

| Surface | Evidence and disclosure considerations |
| --- | --- |
| Local state | `Storage.swift` and `BrowserModel.swift` store preferences, library entries, normal history, and normal sessions on device. No Dao account or app-level sync is implemented. |
| Private state | `BrowserSession.swift` uses a nonpersistent WebKit store; private history and session state are excluded from archives. Explicit bookmarks and download files persist. |
| Browsing and search | WebKit contacts the visited sites and selected search engine. They receive requests and apply their own data practices. Private mode does not hide network requests. |
| Camera and microphone | QR scanning uses the camera; websites request media permission through WebKit. Purpose strings exist in English and Simplified Chinese. |
| App telemetry | No app analytics, advertising, or third-party crash-reporting SDK was found in the current native sources or XcodeGen dependencies. This is a source audit, not a captured release network trace. |
| Dao website | The app's About page opens `dao.msgbyte.com`. Its tracked website layout loads Tianji analytics. Review website collection, hosting logs, retention, and embedded first-party pages before answering App Privacy. |
| Required-reason APIs | The app bundles the UserDefaults reason `CA92.1` in `PrivacyInfo.xcprivacy`. This does not supply the privacy policy or App Privacy answers. |

Apple distinguishes on-device processing and user-directed open-web browsing
from developer data collection. Do not automatically select "Data Not Collected"
based only on local storage: first settle the first-party website and service
practices. See [Apple's privacy details guidance](https://developer.apple.com/app-store/app-privacy-details/).

The public policy still needs the operator name and public contact, an effective
date, the practices above, service-provider handling, retention/deletion terms,
and a way to request deletion of support data. Describe clearing website data,
clearing history, deleting saved pages/downloads, and revoking camera/microphone
access in iOS Settings. Do not imply that clearing website data removes the
library, all downloaded files, or copies shared with other apps.

Publish the policy at a working public HTTPS URL and add a localized, accessible
entry in the app's About page. Publish a support page with current contact details.
Neither page nor the policy entry exists in the current tracked sources.
These are review prerequisites under Apple's [review guidance](https://developer.apple.com/app-store/review/)
and [privacy guidelines](https://developer.apple.com/app-store/review/guidelines/#privacy).

## Screenshots

Capture the real release UI with sample data and no personal browsing history.
Prepare English screenshots first, then matching Simplified Chinese images:

1. Home/search with a clean starting state.
2. A loaded page with browser controls visible.
3. The tab grid with a few distinct sample pages.
4. Bookmarks or reading list with representative saved pages.
5. Downloads or QR sharing after physical-device acceptance.

Use a supported 6.9-inch size, such as 1320 × 2868 pixels in portrait, with no
alpha channel. Apple accepts 1–10 screenshots; five are a preparation target,
not an Apple minimum. No iPad set is needed for this iPhone-only target.
See the current [screenshot specifications](https://developer.apple.com/help/app-store-connect/reference/app-information/screenshot-specifications).
Screenshot capture and visual acceptance are still pending; no UI automation was used.

## Submission decisions and acceptance

- [x] Register the App ID and create the App Store Connect app record (owner
  confirmation on September 18, 2026).
- [x] Export the Apple Distribution certificate/private key as a `.p12`, create
  the App Store Connect provisioning profile, and create the API key (owner
  confirmation on September 18, 2026; signing and upload are not yet verified).
- [x] Configure the six repository Actions secrets from the guide (secret names
  confirmed on September 18, 2026; credential validity and team consistency
  still require the first CI signing/upload run).
- [ ] Complete a hosted Xcode 26+ signed build and Apple processing. The local
  Xcode 16.3 unsigned build is development evidence only.
- [ ] Resolve export compliance based on the actual WebKit/CryptoKit usage; do
  not add an encryption-exemption declaration without checking its applicability.
- [ ] Install from TestFlight on a real iPhone. Run the
  [iOS regression checklist](../docs/feature-checklist.md#ios-native-browser),
  including the oldest supported iOS and the current iOS, permissions, privacy,
  persistence, and an upgrade without data loss.
- [ ] Confirm final store name, copy, screenshots, public URLs, privacy answers,
  copyright, contact details, price, and territories. Remove draft placeholders.
- [ ] Answer the age questionnaire with **Unrestricted Web Access: Yes**; Dao
  allows arbitrary web navigation. Use Apple's resulting regional ratings, not
  a guessed children's rating. See [age-rating definitions](https://developer.apple.com/help/app-store-connect/reference/app-information/age-ratings-values-and-definitions).
- [ ] For China mainland availability, resolve any applicable ICP/documentation
  requirements shown in [App Information](https://developer.apple.com/help/app-store-connect/reference/app-information/app-information).
  Complete the [DSA trader declaration](https://developer.apple.com/help/app-store-connect/manage-compliance-information/manage-european-union-digital-services-act-trader-requirements)
  based on the owner's actual status; EU availability may require verified public contact details.
- [ ] Select the exact accepted TestFlight build, submit it for review, then
  release manually after approval and verify the live listing/install.

Changing from a previously installed `com.msgbyte.dao.ios` development app does
not migrate its sandbox to `com.msgbyte.dao`. Test upgrade persistence between
builds using the new identifier; keep any old development data until no longer needed.
